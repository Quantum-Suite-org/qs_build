/*
 * incremental/incremental.c — Dependency tracking for incremental builds.
 *
 * Tracks which object files are stale by comparing:
 *   - Source file mtime vs object file mtime
 *   - Header file mtimes (extracted from .d depfiles produced by -MD)
 *   - Compiler flags fingerprint
 *   - Toolchain version string
 *
 * .d depfile format (produced by clang/gcc -MD -MF file.d):
 *   object.o: source.c include/foo.h include/bar.h \
 *             include/baz.h
 * We parse these and track all headers as dependencies of their TU.
 */
#include "qs_str.h"
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_hash.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_vec.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Parse a .d depfile and return all dependency paths (including the source) */
qs_result_t qs_depfile_parse(qs_arena_t *a, const char *depfile_path,
                               qs_str_vec_t *deps_out) {
    qs_str_vec_init(deps_out, a);
    char *text=NULL; qs_size_t tlen=0;
    if (qs_fs_read_file(a,depfile_path,&text,&tlen)!=QS_OK) return QS_ERROR_NOT_FOUND;

    /* Skip "target: " prefix — find the colon */
    char *colon=strchr(text,':');
    if (!colon) return QS_OK;
    char *p=colon+1;

    /* Tokenise on whitespace and backslash-newline continuations */
    while (*p) {
        while (*p==' '||*p=='\t'||*p=='\r') p++;
        if (*p=='\\') { p++; while(*p=='\r'||*p=='\n') p++; continue; }
        if (*p=='\n') { p++; continue; }
        if (!*p) break;
        char *start=p;
        while (*p&&*p!=' '&&*p!='\t'&&*p!='\n'&&*p!='\r'&&!(*p=='\\'&&*(p+1)=='\n')) p++;
        qs_size_t len=(qs_size_t)(p-start);
        if (len) {
            char *dep=qs_arena_alloc(a,len+1,1);
            memcpy(dep,start,len); dep[len]='\0';
            qs_str_vec_push(deps_out,qs_str_from_cstr(dep));
        }
    }
    return QS_OK;
}

/* Check if an object file is stale relative to its dependencies */
qs_bool_t qs_is_stale(qs_arena_t *a, const char *obj_path,
                        const char *depfile_path, const char *flag_fingerprint) {
    qs_stat_t obj_st; qs_fs_stat(obj_path,&obj_st);
    if (!obj_st.exists) return QS_TRUE;

    /* Check flag fingerprint file */
    char *fp_path=qs_arena_sprintf(a,"%s.flags",obj_path);
    char *stored=NULL; qs_size_t slen=0;
    if (qs_fs_read_file(a,fp_path,&stored,&slen)==QS_OK&&stored) {
        if (strcmp(stored,flag_fingerprint)!=0) return QS_TRUE;
    } else {
        return QS_TRUE; /* No flag record → stale */
    }

    if (!depfile_path||!qs_fs_exists(depfile_path)) return QS_FALSE;

    qs_str_vec_t deps;
    if (qs_depfile_parse(a,depfile_path,&deps)!=QS_OK) return QS_FALSE;

    for (qs_size_t i=0;i<deps.len;i++) {
        char *dep=qs_arena_str_to_cstr(a,deps.data[i]);
        qs_stat_t dst; qs_fs_stat(dep,&dst);
        if (!dst.exists) continue; /* dep deleted — not stale on its account */
        if (dst.mtime_us > obj_st.mtime_us) return QS_TRUE;
    }
    return QS_FALSE;
}

/* Write flag fingerprint next to an object file */
qs_result_t qs_write_flag_fingerprint(qs_arena_t *a, const char *obj_path,
                                        const char *fingerprint) {
    char *fp_path=qs_arena_sprintf(a,"%s.flags",obj_path);
    return qs_fs_write_file(fp_path,fingerprint,strlen(fingerprint));
}

/* Compute a stable flag fingerprint string from an argv array */
char *qs_flags_fingerprint(qs_arena_t *a, const char *const *argv,
                             const char *toolchain_version) {
    qs_hasher_t h; qs_hasher_init(&h,0);
    if (toolchain_version) qs_hasher_update_str(&h,toolchain_version);
    for (int i=0;argv[i];i++) qs_hasher_update_str(&h,argv[i]);
    char hex[17]; qs_hex_encode64(qs_hasher_finish(&h),hex);
    return qs_arena_strdup(a,hex);
}
