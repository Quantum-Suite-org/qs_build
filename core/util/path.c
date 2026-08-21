/* path.c — UTF-8 path manipulation + language detection. Zero deps. C11. */
#include "qs_path.h"
#include <string.h>

static qs_bool_t is_sep(qs_u8 c) { return c=='/'||c=='\\'?QS_TRUE:QS_FALSE; }

static qs_str_t normalise(qs_arena_t *a, const qs_u8 *src, qs_size_t len) {
    if (!len) return QS_STR_EMPTY;
    qs_u8 *dst=(qs_u8*)qs_arena_alloc(a,len+1,1); if(!dst) return QS_STR_EMPTY;
    qs_size_t di=0;
    for (qs_size_t i=0;i<len;i++) {
        qs_u8 c=src[i];
        if (is_sep(c)) { if (di>0&&dst[di-1]=='/') continue; dst[di++]='/'; }
        else dst[di++]=c;
    }
    if (di>1&&dst[di-1]=='/') di--;
    dst[di]='\0';
    return (qs_str_t){dst,di};
}

qs_str_t qs_path_normalise(qs_arena_t *a, qs_str_t p) { return normalise(a,p.ptr,p.len); }

qs_str_t qs_path_dir(qs_arena_t *a, qs_str_t p) {
    for (qs_size_t i=p.len;i>0;i--) {
        if (is_sep(p.ptr[i-1])) {
            qs_str_t d=qs_str_slice(p,0,i-1);
            if (!d.len) return QS_STR("/");
            return normalise(a,d.ptr,d.len);
        }
    }
    return QS_STR(".");
}
qs_str_t qs_path_basename(qs_str_t p) {
    for (qs_size_t i=p.len;i>0;i--) if (is_sep(p.ptr[i-1])) return qs_str_slice(p,i,p.len);
    return p;
}
qs_str_t qs_path_stem(qs_str_t p) {
    qs_str_t b=qs_path_basename(p);
    for (qs_size_t i=b.len;i>0;i--) if (b.ptr[i-1]=='.') return qs_str_slice(b,0,i-1);
    return b;
}
qs_str_t qs_path_ext(qs_str_t p) {
    qs_str_t b=qs_path_basename(p);
    for (qs_size_t i=b.len;i>0;i--) if (b.ptr[i-1]=='.') return qs_str_slice(b,i-1,b.len);
    return QS_STR_EMPTY;
}
qs_str_t qs_path_join(qs_arena_t *a, qs_str_t dir, qs_str_t file) {
    if (QS_STR_IS_EMPTY(dir))  return normalise(a,file.ptr,file.len);
    if (QS_STR_IS_EMPTY(file)) return normalise(a,dir.ptr,dir.len);
    if (!QS_STR_IS_EMPTY(file)&&is_sep(file.ptr[0])) return normalise(a,file.ptr,file.len);
    qs_size_t total=dir.len+1+file.len;
    qs_u8 *buf=(qs_u8*)qs_arena_alloc(a,total+1,1); if(!buf) return QS_STR_EMPTY;
    memcpy(buf,dir.ptr,(size_t)dir.len); buf[dir.len]='/';
    memcpy(buf+dir.len+1,file.ptr,(size_t)file.len); buf[total]='\0';
    return normalise(a,buf,total);
}
qs_str_t qs_path_join_arr(qs_arena_t *a, const qs_str_t *parts, qs_size_t n) {
    if (!n) return QS_STR_EMPTY;
    qs_str_t r=parts[0];
    for (qs_size_t i=1;i<n;i++) r=qs_path_join(a,r,parts[i]);
    return r;
}
qs_str_t qs_path_replace_ext(qs_arena_t *a, qs_str_t p, qs_str_t new_ext) {
    qs_str_t dir=qs_path_dir(a,p);
    qs_str_t stem=qs_path_stem(qs_path_basename(p));
    qs_str_t name=qs_str_concat(a,stem,new_ext);
    if (qs_str_eq_cstr(dir,".")) return name;
    return qs_path_join(a,dir,name);
}
qs_bool_t qs_path_is_absolute(qs_str_t p) {
    if (!p.len) return QS_FALSE;
    if (p.ptr[0]=='/') return QS_TRUE;
    if (p.len>=3&&p.ptr[1]==':'&&is_sep(p.ptr[2])) return QS_TRUE;
    return QS_FALSE;
}
qs_bool_t qs_path_has_ext(qs_str_t p, const char *const *exts) {
    qs_str_t ext = qs_path_ext(p); /* includes leading dot, e.g. ".cpp" */
    for (qs_size_t i = 0; exts[i]; i++) {
        const char *e = exts[i];
        /* Accept both ".cpp" and "cpp" in the list */
        if (e[0] == '.') {
            if (qs_str_eq_cstr(ext, e)) return QS_TRUE;
        } else {
            /* caller omitted dot — compare against ext+1 */
            if (ext.len > 0 && qs_str_eq_cstr(
                    qs_str_slice(ext, 1, ext.len), e)) return QS_TRUE;
        }
    }
    return QS_FALSE;
}
qs_lang_t qs_path_detect_lang(qs_str_t p) {
    qs_str_t e=qs_path_ext(p);
    if (qs_str_eq_cstr(e,".c"))            return QS_LANG_C;
    if (qs_str_eq_cstr(e,".h"))            return QS_LANG_HEADER_C;
    if (qs_str_eq_cstr(e,".cpp")||qs_str_eq_cstr(e,".cxx")||
        qs_str_eq_cstr(e,".cc")||qs_str_eq_cstr(e,".C"))  return QS_LANG_CPP;
    if (qs_str_eq_cstr(e,".hpp")||qs_str_eq_cstr(e,".hxx")||
        qs_str_eq_cstr(e,".hh"))          return QS_LANG_HEADER_CPP;
    if (qs_str_eq_cstr(e,".ixx")||qs_str_eq_cstr(e,".cppm")) return QS_LANG_CPP;
    if (qs_str_eq_cstr(e,".cs"))           return QS_LANG_CSHARP;
    if (qs_str_eq_cstr(e,".java"))         return QS_LANG_JAVA;
    if (qs_str_eq_cstr(e,".py"))           return QS_LANG_PYTHON;
    if (qs_str_eq_cstr(e,".go"))           return QS_LANG_GO;
    if (qs_str_eq_cstr(e,".rb")||qs_str_eq_cstr(e,".rake")) return QS_LANG_RUBY;
    if (qs_str_eq_cstr(e,".zig"))          return QS_LANG_ZIG;
    if (qs_str_eq_cstr(e,".rs"))           return QS_LANG_RUST;
    return QS_LANG_UNKNOWN;
}
const char *qs_lang_name(qs_lang_t l) {
    switch(l){
        case QS_LANG_C:          return "C";
        case QS_LANG_CPP:        return "C++";
        case QS_LANG_CSHARP:     return "C#";
        case QS_LANG_JAVA:       return "Java";
        case QS_LANG_PYTHON:     return "Python";
        case QS_LANG_GO:         return "Go";
        case QS_LANG_RUBY:       return "Ruby";
        case QS_LANG_ZIG:        return "Zig";
        case QS_LANG_RUST:       return "Rust";
        case QS_LANG_HEADER_C:   return "C header";
        case QS_LANG_HEADER_CPP: return "C++ header";
        default:                 return "unknown";
    }
}
const char *qs_lang_std_default(qs_lang_t l) {
    switch(l){
        case QS_LANG_C:      return "c17";
        case QS_LANG_CPP:    return "c++20";
        case QS_LANG_CSHARP: return "net8.0";
        case QS_LANG_JAVA:   return "21";
        case QS_LANG_PYTHON: return "3.12";
        case QS_LANG_GO:     return "1.22";
        case QS_LANG_RUBY:   return "3.3";
        case QS_LANG_ZIG:    return "0.13.0";
        case QS_LANG_RUST:   return "2021";
        default:             return "";
    }
}
