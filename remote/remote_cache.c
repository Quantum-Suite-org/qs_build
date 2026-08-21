/*
 * remote/remote_cache.c — Remote build cache stub.
 * Allows sharing compiled object files across machines via HTTP PUT/GET.
 * The remote server stores objects keyed by fingerprint hex.
 * Zero dependencies: uses raw POSIX sockets (or WinSock2 on Windows).
 *
 * Protocol: plain HTTP/1.1
 *   GET  /cache/<hex16>      → 200 + object bytes, or 404
 *   PUT  /cache/<hex16>      → 201
 *
 * Configure via env:
 *   QS_REMOTE_CACHE_URL=http://buildcache.internal:8080
 *   QS_REMOTE_CACHE_TOKEN=<bearer-token>
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_hash.h"
#include "../core/include/qs_fs.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
/* MSVC lacks strndup/strdup — provide minimal shims */
#include <malloc.h>
static char *strndup(const char *s, size_t n) {
    char *r = (char*)malloc(n + 1);
    if (r) { memcpy(r, s, n); r[n] = '\0'; }
    return r;
}
#define strdup _strdup
#endif


#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib,"ws2_32.lib")
#  define SOCK_T SOCKET
#  define SOCK_INVALID INVALID_SOCKET
#  define sock_close(s) closesocket(s)
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  define SOCK_T int
#  define SOCK_INVALID (-1)
#  define sock_close(s) close(s)
#endif

typedef struct {
    char  *base_url;   /* e.g. "http://host:8080" */
    char  *token;      /* bearer token; may be NULL */
    qs_bool_t enabled;
} qs_remote_cache_t;

static void rc_parse_url(const char *url, char **host_out, int *port_out, char **path_out) {
    /* Minimal URL parser: http://host:port/path */
    const char *p = url;
    if (strncmp(p,"http://",7)==0) p+=7;
    const char *host_start=p;
    while(*p&&*p!=':'&&*p!='/') p++;
    *host_out = strndup(host_start,(size_t)(p-host_start));
    *port_out = 8080;
    if (*p==':') { p++; *port_out=atoi(p); while(*p&&*p!='/') p++; }
    *path_out = *p?strdup(p):(char*)"/";
}

qs_result_t qs_remote_cache_init(qs_arena_t *a, qs_remote_cache_t *rc) {
    const char *url   = getenv("QS_REMOTE_CACHE_URL");
    const char *token = getenv("QS_REMOTE_CACHE_TOKEN");
    if (!url) { rc->enabled=QS_FALSE; return QS_OK; }
    rc->base_url = qs_arena_strdup(a,url);
    rc->token    = token?qs_arena_strdup(a,token):NULL;
    rc->enabled  = QS_TRUE;
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2),&wsa);
#endif
    return QS_OK;
}

/* Try to fetch a cached object from the remote. Returns QS_OK on hit. */
qs_result_t qs_remote_cache_get(qs_remote_cache_t *rc, qs_arena_t *a,
                                  qs_u64 fp, const char *local_path) {
    if (!rc->enabled) return QS_ERROR_NOT_FOUND;
    char hex[17]; qs_hex_encode64(fp,hex);

    char *host; int port; char *base_path;
    rc_parse_url(rc->base_url,&host,&port,&base_path);

    /* Resolve host */
    struct addrinfo hints={0}, *res;
    hints.ai_socktype=SOCK_STREAM; hints.ai_family=AF_INET;
    char port_str[16]; snprintf(port_str,sizeof(port_str),"%d",port);
    if (getaddrinfo(host,port_str,&hints,&res)!=0) return QS_ERROR_NOT_FOUND;

    SOCK_T sock=socket(res->ai_family,SOCK_STREAM,0);
    if (sock==SOCK_INVALID) { freeaddrinfo(res); return QS_ERROR_NOT_FOUND; }
    if (connect(sock,res->ai_addr,(int)res->ai_addrlen)!=0) {
        sock_close(sock); freeaddrinfo(res); return QS_ERROR_NOT_FOUND;
    }
    freeaddrinfo(res);

    /* Send HTTP GET */
    char req[1024];
    int rlen=snprintf(req,sizeof(req),
        "GET %s/cache/%s HTTP/1.0\r\nHost: %s:%d\r\n%s\r\n",
        base_path,hex,host,port,
        rc->token?qs_arena_sprintf(a,"Authorization: Bearer %s\r\n",rc->token):"");
    send(sock,req,(size_t)rlen,0);

    /* Read response header */
    char resp[4096]={0}; int total=0;
    while(total<(int)sizeof(resp)-1) {
        int n=(int)recv(sock,resp+total,sizeof(resp)-total-1,0);
        if(n<=0) break; total+=n;
        if(strstr(resp,"\r\n\r\n")) break;
    }
    int code=0; sscanf(resp,"HTTP/%*s %d",&code);
    qs_result_t result=QS_ERROR_NOT_FOUND;
    if (code==200) {
        /* Find body start */
        char *body=strstr(resp,"\r\n\r\n");
        if(body) {
            body+=4;
            /* Read remaining body and write to local_path */
            FILE *f=fopen(local_path,"wb");
            if(f) {
                int already=(int)(resp+total-body);
                if(already>0) fwrite(body,1,(size_t)already,f);
                char buf[65536];
                int n;
                while((n=(int)recv(sock,buf,sizeof(buf),0))>0)
                    fwrite(buf,1,(size_t)n,f);
                fclose(f);
                result=QS_OK;
            }
        }
    }
    sock_close(sock);
    return result;
}

/* Upload a compiled object to the remote cache */
qs_result_t qs_remote_cache_put(qs_remote_cache_t *rc, qs_arena_t *a,
                                  qs_u64 fp, const char *local_path) {
    if (!rc->enabled) return QS_OK;
    char *data=NULL; qs_size_t dlen=0;
    if (qs_fs_read_file(a,local_path,&data,&dlen)!=QS_OK) return QS_ERROR_IO;

    char hex[17]; qs_hex_encode64(fp,hex);
    char *host; int port; char *base_path;
    rc_parse_url(rc->base_url,&host,&port,&base_path);

    struct addrinfo hints={0},*res;
    hints.ai_socktype=SOCK_STREAM; hints.ai_family=AF_INET;
    char ps[16]; snprintf(ps,sizeof(ps),"%d",port);
    if(getaddrinfo(host,ps,&hints,&res)!=0) return QS_ERROR_PROCESS;

    SOCK_T sock=socket(res->ai_family,SOCK_STREAM,0);
    if(sock==SOCK_INVALID){freeaddrinfo(res);return QS_ERROR_PROCESS;}
    if(connect(sock,res->ai_addr,(int)res->ai_addrlen)!=0){
        sock_close(sock);freeaddrinfo(res);return QS_ERROR_PROCESS;
    }
    freeaddrinfo(res);

    char hdr[1024];
    int hlen=snprintf(hdr,sizeof(hdr),
        "PUT %s/cache/%s HTTP/1.0\r\nHost: %s:%d\r\n"
        "Content-Length: %llu\r\nContent-Type: application/octet-stream\r\n%s\r\n",
        base_path,hex,host,port,(unsigned long long)dlen,
        rc->token?qs_arena_sprintf(a,"Authorization: Bearer %s\r\n",rc->token):"");
    send(sock,hdr,(size_t)hlen,0);
    send(sock,data,(size_t)dlen,0);
    sock_close(sock);
    return QS_OK;
}
