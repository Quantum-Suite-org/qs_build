/*
 * hash.c — FNV-1a + xxHash-64 + streaming hasher. Zero deps. Pure C11.
 * Same output on every machine: little-endian reads via memcpy.
 */
#include "qs_hash.h"
#include <string.h>
#include <stdio.h>

#define FNV_BASIS 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL

qs_u64 qs_fnv1a_64(const void *data, qs_size_t len) {
    const qs_u8 *p = (const qs_u8*)data;
    qs_u64 h = FNV_BASIS;
    for (qs_size_t i=0; i<len; i++) { h ^= p[i]; h *= FNV_PRIME; }
    return h;
}
qs_u64 qs_fnv1a_str(const char *s) {
    qs_u64 h = FNV_BASIS;
    while (*s) { h ^= (qs_u8)*s++; h *= FNV_PRIME; }
    return h;
}
qs_u64 qs_fnv1a_combine(qs_u64 a, qs_u64 b) {
    qs_u64 h = a;
    for (int i=0; i<8; i++) { h ^= (b>>(qs_u64)(i*8))&0xFF; h *= FNV_PRIME; }
    return h;
}

#define XX1 0x9E3779B185EBCA87ULL
#define XX2 0xC2B2AE3D27D4EB4FULL
#define XX3 0x165667B19E3779F9ULL
#define XX4 0x85EBCA77C2B27589ULL
#define XX5 0x27D4EB2F165667C5ULL

static qs_u64 rotl(qs_u64 v, int n) { return (v<<n)|(v>>(64-n)); }
static qs_u64 u64le(const qs_u8 *p) { qs_u64 v; memcpy(&v,p,8); return v; }
static qs_u64 u32le(const qs_u8 *p) { qs_u32 v; memcpy(&v,p,4); return (qs_u64)v; }
static qs_u64 xround(qs_u64 acc, qs_u64 lane) { return rotl(acc + lane*XX2, 31)*XX1; }
static qs_u64 xmerge(qs_u64 acc, qs_u64 v) { return (acc ^ xround(0,v))*XX1 + XX4; }

qs_u64 qs_xxhash64(const void *data, qs_size_t len, qs_u64 seed) {
    const qs_u8 *p = (const qs_u8*)data, *end = p+len;
    qs_u64 h;
    if (len >= 32) {
        qs_u64 v1=seed+XX1+XX2, v2=seed+XX2, v3=seed, v4=seed-XX1;
        const qs_u8 *lim = end-32;
        while (p<=lim) {
            v1=xround(v1,u64le(p)); v2=xround(v2,u64le(p+8));
            v3=xround(v3,u64le(p+16)); v4=xround(v4,u64le(p+24)); p+=32;
        }
        h=rotl(v1,1)+rotl(v2,7)+rotl(v3,12)+rotl(v4,18);
        h=xmerge(h,v1); h=xmerge(h,v2); h=xmerge(h,v3); h=xmerge(h,v4);
    } else h=seed+XX5;
    h += (qs_u64)len;
    while (p+8<=end) { h^=xround(0,u64le(p)); h=rotl(h,27)*XX1+XX4; p+=8; }
    if (p+4<=end) { h^=u32le(p)*XX3; h=rotl(h,23)*XX2+XX3; p+=4; }
    while (p<end) { h^=(qs_u64)*p*XX5; h=rotl(h,11)*XX1; p++; }
    h^=h>>33; h*=XX2; h^=h>>29; h*=XX3; h^=h>>32;
    return h;
}

void qs_hasher_init(qs_hasher_t *h, qs_u64 seed) {
    h->seed=seed; h->v1=seed+XX1+XX2; h->v2=seed+XX2;
    h->v3=seed; h->v4=seed-XX1; h->buf_len=h->total=0;
}
void qs_hasher_update(qs_hasher_t *h, const void *data, qs_size_t len) {
    const qs_u8 *p=(const qs_u8*)data;
    h->total += len;
    if (h->buf_len) {
        qs_u64 space=32-h->buf_len, cp=len<space?len:space;
        memcpy(h->buf+h->buf_len, p, (size_t)cp);
        h->buf_len+=cp; p+=cp; len-=cp;
        if (h->buf_len==32) {
            h->v1=xround(h->v1,u64le(h->buf));   h->v2=xround(h->v2,u64le(h->buf+8));
            h->v3=xround(h->v3,u64le(h->buf+16)); h->v4=xround(h->v4,u64le(h->buf+24));
            h->buf_len=0;
        }
    }
    while (len>=32) {
        h->v1=xround(h->v1,u64le(p)); h->v2=xround(h->v2,u64le(p+8));
        h->v3=xround(h->v3,u64le(p+16)); h->v4=xround(h->v4,u64le(p+24));
        p+=32; len-=32;
    }
    if (len) { memcpy(h->buf+h->buf_len, p, (size_t)len); h->buf_len+=len; }
}
void qs_hasher_update_u64(qs_hasher_t *h, qs_u64 v) {
    qs_u8 b[8]; memcpy(b,&v,8); qs_hasher_update(h,b,8);
}
void qs_hasher_update_str(qs_hasher_t *h, const char *s) {
    qs_hasher_update(h, s, (qs_size_t)strlen(s));
}
qs_u64 qs_hasher_finish(const qs_hasher_t *h) {
    qs_u64 hv;
    if (h->total>=32) {
        hv=rotl(h->v1,1)+rotl(h->v2,7)+rotl(h->v3,12)+rotl(h->v4,18);
        hv=xmerge(hv,h->v1); hv=xmerge(hv,h->v2);
        hv=xmerge(hv,h->v3); hv=xmerge(hv,h->v4);
    } else hv=h->seed+XX5;
    hv+=h->total;
    const qs_u8 *p=h->buf; qs_u64 rem=h->buf_len;
    while(rem>=8){hv^=xround(0,u64le(p));hv=rotl(hv,27)*XX1+XX4;p+=8;rem-=8;}
    if(rem>=4){hv^=u32le(p)*XX3;hv=rotl(hv,23)*XX2+XX3;p+=4;rem-=4;}
    while(rem--){hv^=(qs_u64)*p*XX5;hv=rotl(hv,11)*XX1;p++;}
    hv^=hv>>33; hv*=XX2; hv^=hv>>29; hv*=XX3; hv^=hv>>32;
    return hv;
}

qs_u64 qs_hash_file(const char *path) {
    FILE *f = fopen(path,"rb");
    if (!f) return 0;
    qs_hasher_t h; qs_hasher_init(&h,0);
    qs_u8 buf[65536]; size_t n;
    while ((n=fread(buf,1,sizeof(buf),f))>0) qs_hasher_update(&h,buf,(qs_size_t)n);
    fclose(f);
    return qs_hasher_finish(&h);
}

static const char HEX[]="0123456789abcdef";
void qs_hex_encode64(qs_u64 v, char out[17]) {
    for(int i=15;i>=0;i--){out[i]=HEX[v&0xF];v>>=4;} out[16]='\0';
}
qs_u64 qs_hex_decode64(const char *s) {
    qs_u64 v=0;
    for(int i=0;i<16&&s[i];i++){
        char c=s[i]; qs_u64 n;
        if(c>='0'&&c<='9') n=(qs_u64)(c-'0');
        else if(c>='a'&&c<='f') n=(qs_u64)(c-'a'+10);
        else if(c>='A'&&c<='F') n=(qs_u64)(c-'A'+10);
        else break;
        v=(v<<4)|n;
    }
    return v;
}
