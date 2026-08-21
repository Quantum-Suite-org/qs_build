/* str.c — string slice utilities + intern table. Zero deps. C11. */
#include "qs_str.h"
#include "qs_hash.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>

qs_bool_t qs_str_eq(qs_str_t a, qs_str_t b) {
    if (a.len!=b.len) return QS_FALSE;
    if (a.ptr==b.ptr) return QS_TRUE;
    return memcmp(a.ptr,b.ptr,(size_t)a.len)==0?QS_TRUE:QS_FALSE;
}
qs_bool_t qs_str_eq_cstr(qs_str_t s, const char *c) {
    qs_size_t cl=(qs_size_t)strlen(c);
    return s.len==cl && memcmp(s.ptr,c,(size_t)cl)==0?QS_TRUE:QS_FALSE;
}
qs_bool_t qs_str_starts_with(qs_str_t s, qs_str_t p) {
    return s.len>=p.len && memcmp(s.ptr,p.ptr,(size_t)p.len)==0?QS_TRUE:QS_FALSE;
}
qs_bool_t qs_str_ends_with(qs_str_t s, qs_str_t p) {
    return s.len>=p.len && memcmp(s.ptr+(s.len-p.len),p.ptr,(size_t)p.len)==0?QS_TRUE:QS_FALSE;
}
qs_bool_t qs_str_ends_with_cstr(qs_str_t s, const char *p) {
    qs_size_t pl=(qs_size_t)strlen(p);
    return s.len>=pl && memcmp(s.ptr+(s.len-pl),p,(size_t)pl)==0?QS_TRUE:QS_FALSE;
}
qs_bool_t qs_str_contains(qs_str_t hay, qs_str_t needle) {
    if (!needle.len) return QS_TRUE;
    if (needle.len>hay.len) return QS_FALSE;
    qs_size_t lim=hay.len-needle.len;
    for (qs_size_t i=0;i<=lim;i++)
        if (memcmp(hay.ptr+i,needle.ptr,(size_t)needle.len)==0) return QS_TRUE;
    return QS_FALSE;
}
qs_str_t qs_str_slice(qs_str_t s, qs_size_t start, qs_size_t end) {
    if (start>s.len) start=s.len;
    if (end>s.len)   end=s.len;
    if (start>end)   start=end;
    return (qs_str_t){s.ptr+start, end-start};
}
qs_str_t qs_str_trim(qs_str_t s) {
    while (s.len && isspace((unsigned char)s.ptr[0]))     { s.ptr++; s.len--; }
    while (s.len && isspace((unsigned char)s.ptr[s.len-1])) s.len--;
    return s;
}
qs_str_t qs_str_from_cstr(const char *s) {
    return (qs_str_t){(const qs_u8*)s, s?(qs_size_t)strlen(s):0};
}
char *qs_str_cstr(qs_arena_t *a, qs_str_t s) { return qs_arena_str_to_cstr(a,s); }

qs_str_t qs_str_concat(qs_arena_t *a, qs_str_t l, qs_str_t r) {
    qs_size_t total=l.len+r.len;
    if (!total) return QS_STR_EMPTY;
    qs_u8 *buf=(qs_u8*)qs_arena_alloc(a,total+1,1); if(!buf) return QS_STR_EMPTY;
    if (l.len) memcpy(buf,l.ptr,(size_t)l.len);
    if (r.len) memcpy(buf+l.len,r.ptr,(size_t)r.len);
    buf[total]='\0';
    return (qs_str_t){buf,total};
}
qs_str_t qs_str_join(qs_arena_t *a, const qs_str_t *parts, qs_size_t n, qs_str_t sep) {
    if (!n) return QS_STR_EMPTY;
    qs_size_t total=0;
    for (qs_size_t i=0;i<n;i++) { total+=parts[i].len; if (i+1<n) total+=sep.len; }
    qs_u8 *buf=(qs_u8*)qs_arena_alloc(a,total+1,1); if(!buf) return QS_STR_EMPTY;
    qs_size_t pos=0;
    for (qs_size_t i=0;i<n;i++) {
        memcpy(buf+pos,parts[i].ptr,(size_t)parts[i].len); pos+=parts[i].len;
        if (i+1<n) { memcpy(buf+pos,sep.ptr,(size_t)sep.len); pos+=sep.len; }
    }
    buf[total]='\0';
    return (qs_str_t){buf,total};
}
qs_str_t qs_str_fmt(qs_arena_t *a, const char *fmt, ...) {
    va_list ap; va_start(ap,fmt);
    int n=vsnprintf(NULL,0,fmt,ap); va_end(ap);
    if (n<0) return QS_STR_EMPTY;
    qs_u8 *buf=(qs_u8*)qs_arena_alloc(a,(qs_size_t)(n+1),1);
    if (!buf) return QS_STR_EMPTY;
    va_start(ap,fmt); vsnprintf((char*)buf,(size_t)(n+1),fmt,ap); va_end(ap);
    return (qs_str_t){buf,(qs_size_t)n};
}
void qs_str_split_first(qs_str_t s, qs_u8 d, qs_str_t *l, qs_str_t *r) {
    for (qs_size_t i=0;i<s.len;i++) {
        if (s.ptr[i]==d) { *l=qs_str_slice(s,0,i); *r=qs_str_slice(s,i+1,s.len); return; }
    }
    *l=s; *r=QS_STR_EMPTY;
}
void qs_str_split_last(qs_str_t s, qs_u8 d, qs_str_t *l, qs_str_t *r) {
    for (qs_size_t i=s.len;i>0;i--) {
        if (s.ptr[i-1]==d) { *l=qs_str_slice(s,0,i-1); *r=qs_str_slice(s,i,s.len); return; }
    }
    *l=s; *r=QS_STR_EMPTY;
}
void qs_intern_init(qs_intern_table_t *t, qs_arena_t *arena) {
    t->arena=arena; t->count=t->total_bytes=0;
    memset(t->buckets,0,sizeof(t->buckets));
}
qs_str_t qs_intern(qs_intern_table_t *t, const qs_u8 *data, qs_size_t len) {
    qs_u64 h=qs_fnv1a_64(data,len);
    qs_size_t idx=(qs_size_t)(h&(QS_INTERN_BUCKETS-1));
    for (qs_intern_entry_t *e=t->buckets[idx];e;e=e->next)
        if (e->hash==h&&e->str.len==len&&memcmp(e->str.ptr,data,(size_t)len)==0) return e->str;
    qs_u8 *copy=(qs_u8*)qs_arena_alloc(t->arena,len+1,1);
    if (!copy) return (qs_str_t){data,len};
    if (len) memcpy(copy,data,(size_t)len); copy[len]='\0';
    qs_intern_entry_t *ne=QS_ARENA_NEW(t->arena,qs_intern_entry_t);
    if (!ne) return (qs_str_t){copy,len};
    ne->str=(qs_str_t){copy,len}; ne->hash=h; ne->next=t->buckets[idx];
    t->buckets[idx]=ne; t->count++; t->total_bytes+=len;
    return ne->str;
}
qs_str_t qs_intern_cstr(qs_intern_table_t *t, const char *s) {
    return qs_intern(t,(const qs_u8*)s,(qs_size_t)strlen(s));
}
qs_str_t qs_intern_str(qs_intern_table_t *t, qs_str_t s) {
    return qs_intern(t,s.ptr,s.len);
}
