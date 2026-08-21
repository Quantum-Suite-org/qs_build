/*
 * fs.c — Filesystem helpers (stat, mkdir -p, read/write, scan). Zero deps.
 * POSIX + Win32. All paths are UTF-8 char*. Win32 converts at the syscall layer.
 */
#include "qs_fs.h"
#include "qs_path.h"
#include "qs_arena.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#  define stat _stat64
#  define QS_MKDIR(p) CreateDirectoryA(p,NULL)
#else
#  include <unistd.h>
#  include <dirent.h>
#  include <fcntl.h>
#  define QS_MKDIR(p) (mkdir(p,0755)==0)
#endif

qs_result_t qs_fs_stat(const char *path, qs_stat_t *out) {
    memset(out,0,sizeof(*out));
#ifdef _WIN32
    struct __stat64 st;
    if (_stat64(path,&st)!=0) { out->exists=QS_FALSE; return QS_OK; }
#else
    struct stat st;
    if (stat(path,&st)!=0) { out->exists=QS_FALSE; return QS_OK; }
#endif
    out->exists=QS_TRUE;
    out->size=(qs_u64)st.st_size;
    out->mtime_us=(qs_u64)st.st_mtime*1000000ULL;
#ifndef _WIN32
    out->is_dir=S_ISDIR(st.st_mode)?QS_TRUE:QS_FALSE;
#else
    out->is_dir=(st.st_mode&_S_IFDIR)?QS_TRUE:QS_FALSE;
#endif
    return QS_OK;
}

qs_bool_t qs_fs_exists(const char *path) {
    qs_stat_t st; qs_fs_stat(path,&st); return st.exists;
}
qs_bool_t qs_fs_is_dir(const char *path) {
    qs_stat_t st; qs_fs_stat(path,&st); return st.is_dir;
}

qs_result_t qs_fs_mkdir_p(const char *path) {
    char buf[4096]; size_t len=strlen(path);
    if (len>=sizeof(buf)) return QS_ERROR_INVALID_ARG;
    memcpy(buf,path,len+1);
    for (size_t i=1;i<=len;i++) {
        if (buf[i]=='/'||buf[i]=='\\'||buf[i]=='\0') {
            char saved=buf[i]; buf[i]='\0';
            if (!qs_fs_exists(buf)) {
                if (!QS_MKDIR(buf)&&errno!=EEXIST) { return QS_ERROR_IO; }
            }
            buf[i]=saved;
        }
    }
    return QS_OK;
}

qs_result_t qs_fs_read_file(qs_arena_t *a, const char *path, char **out, qs_size_t *out_len) {
    FILE *f=fopen(path,"rb");
    if (!f) return QS_ERROR_NOT_FOUND;
    fseek(f,0,SEEK_END); long sz=ftell(f); rewind(f);
    if (sz<0) { fclose(f); return QS_ERROR_IO; }
    char *buf=(char*)qs_arena_alloc(a,(qs_size_t)(sz+1),1);
    if (!buf) { fclose(f); return QS_ERROR_OOM; }
    qs_size_t rd=(qs_size_t)fread(buf,1,(size_t)sz,f); fclose(f);
    buf[rd]='\0';
    if (out) *out=buf;
    if (out_len) *out_len=rd;
    return QS_OK;
}

qs_result_t qs_fs_write_file(const char *path, const char *data, qs_size_t len) {
    FILE *f=fopen(path,"wb");
    if (!f) return QS_ERROR_IO;
    fwrite(data,1,(size_t)len,f); fclose(f);
    return QS_OK;
}
qs_result_t qs_fs_append_file(const char *path, const char *data, qs_size_t len) {
    FILE *f=fopen(path,"ab");
    if (!f) return QS_ERROR_IO;
    fwrite(data,1,(size_t)len,f); fclose(f);
    return QS_OK;
}
qs_result_t qs_fs_copy_file(const char *src, const char *dst) {
    FILE *in=fopen(src,"rb"), *out=fopen(dst,"wb");
    if (!in||!out) { if(in)fclose(in); if(out)fclose(out); return QS_ERROR_IO; }
    char buf[65536]; size_t n;
    while ((n=fread(buf,1,sizeof(buf),in))>0) fwrite(buf,1,n,out);
    fclose(in); fclose(out);
    return QS_OK;
}
qs_result_t qs_fs_remove(const char *path) {
    return remove(path)==0?QS_OK:QS_ERROR_IO;
}

#ifndef _WIN32
static qs_result_t remove_dir_r(const char *path) {
    DIR *d=opendir(path); if(!d) return QS_ERROR_IO;
    struct dirent *e; char buf[4096];
    while((e=readdir(d))!=NULL){
        if(!strcmp(e->d_name,".")||!strcmp(e->d_name,"..")) continue;
        snprintf(buf,sizeof(buf),"%s/%s",path,e->d_name);
        if(qs_fs_is_dir(buf)) remove_dir_r(buf); else remove(buf);
    }
    closedir(d); rmdir(path);
    return QS_OK;
}
#endif

qs_result_t qs_fs_remove_dir_recursive(const char *path) {
#ifdef _WIN32
    /* Simple recursive via FindFirstFile */
    char pat[MAX_PATH]; snprintf(pat,MAX_PATH,"%s\\*",path);
    WIN32_FIND_DATAA fd; HANDLE h=FindFirstFileA(pat,&fd);
    if(h!=INVALID_HANDLE_VALUE){
        do {
            if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
            char child[MAX_PATH]; snprintf(child,MAX_PATH,"%s\\%s",path,fd.cFileName);
            if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) qs_fs_remove_dir_recursive(child);
            else DeleteFileA(child);
        } while(FindNextFileA(h,&fd));
        FindClose(h);
    }
    RemoveDirectoryA(path);
    return QS_OK;
#else
    return remove_dir_r(path);
#endif
}

qs_result_t qs_fs_list_dir(qs_arena_t *a, const char *path, qs_str_vec_t *out) {
    qs_str_vec_init(out,a);
#ifdef _WIN32
    char pat[MAX_PATH]; snprintf(pat,MAX_PATH,"%s\\*",path);
    WIN32_FIND_DATAA fd; HANDLE h=FindFirstFileA(pat,&fd);
    if(h==INVALID_HANDLE_VALUE) return QS_ERROR_NOT_FOUND;
    do {
        if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
        char full[MAX_PATH]; snprintf(full,MAX_PATH,"%s/%s",path,fd.cFileName);
        qs_str_vec_push(out,qs_str_from_cstr(qs_arena_strdup(a,full)));
    } while(FindNextFileA(h,&fd));
    FindClose(h);
#else
    DIR *d=opendir(path); if(!d) return QS_ERROR_NOT_FOUND;
    struct dirent *e;
    while((e=readdir(d))!=NULL){
        if(!strcmp(e->d_name,".")||!strcmp(e->d_name,"..")) continue;
        char full[4096]; snprintf(full,sizeof(full),"%s/%s",path,e->d_name);
        qs_str_vec_push(out,qs_str_from_cstr(qs_arena_strdup(a,full)));
    }
    closedir(d);
#endif
    return QS_OK;
}

/* Recursive source scan — only files with matching extensions */
static qs_result_t scan_r(qs_arena_t *a, const char *dir,
                            const char *const *exts, qs_str_vec_t *out) {
#ifdef _WIN32
    char pat[MAX_PATH]; snprintf(pat,MAX_PATH,"%s\\*",dir);
    WIN32_FIND_DATAA fd; HANDLE h=FindFirstFileA(pat,&fd);
    if(h==INVALID_HANDLE_VALUE) return QS_OK;
    do {
        if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,"..")) continue;
        char full[MAX_PATH]; snprintf(full,MAX_PATH,"%s/%s",dir,fd.cFileName);
        if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) { scan_r(a,full,exts,out); continue; }
        if(!exts||qs_path_has_ext(qs_str_from_cstr(full),exts))
            qs_str_vec_push(out,qs_str_from_cstr(qs_arena_strdup(a,full)));
    } while(FindNextFileA(h,&fd));
    FindClose(h);
#else
    DIR *d=opendir(dir); if(!d) return QS_OK;
    struct dirent *e;
    while((e=readdir(d))!=NULL){
        if(!strcmp(e->d_name,".")||!strcmp(e->d_name,"..")) continue;
        char full[4096]; snprintf(full,sizeof(full),"%s/%s",dir,e->d_name);
        if(qs_fs_is_dir(full)){ scan_r(a,full,exts,out); continue; }
        if(!exts||qs_path_has_ext(qs_str_from_cstr(full),exts))
            qs_str_vec_push(out,qs_str_from_cstr(qs_arena_strdup(a,full)));
    }
    closedir(d);
#endif
    return QS_OK;
}

qs_result_t qs_fs_scan_sources(qs_arena_t *a, const char *dir,
                                 const char *const *exts, qs_str_vec_t *out) {
    qs_str_vec_init(out,a);
    return scan_r(a,dir,exts,out);
}

char *qs_fs_exe_dir(qs_arena_t *a) {
#ifdef _WIN32
    char buf[MAX_PATH]; GetModuleFileNameA(NULL,buf,MAX_PATH);
    char *slash=strrchr(buf,'\\'); if(slash)*slash='\0';
    return qs_arena_strdup(a,buf);
#elif defined(__linux__)
    char buf[4096]; ssize_t n=readlink("/proc/self/exe",buf,sizeof(buf)-1);
    if(n<0) return qs_arena_strdup(a,".");
    buf[n]='\0'; char *sl=strrchr(buf,'/'); if(sl)*sl='\0';
    return qs_arena_strdup(a,buf);
#elif defined(__APPLE__)
    (void)a; return qs_arena_strdup(a,".");
#else
    return qs_arena_strdup(a,".");
#endif
}
char *qs_fs_tmp_dir(qs_arena_t *a) {
#ifdef _WIN32
    char buf[MAX_PATH]; GetTempPathA(MAX_PATH,buf);
    return qs_arena_strdup(a,buf);
#else
    const char *t=getenv("TMPDIR");
    return qs_arena_strdup(a,t?t:"/tmp");
#endif
}
