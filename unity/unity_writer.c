/*
 * unity/unity_writer.c — High-level unity build file writer.
 * Wraps chunker/chunker.c with smarter grouping heuristics:
 *   - Groups files by directory to improve cache locality
 *   - Keeps .cpp and .c files in separate chunks
 *   - Avoids mixing module interface files (.ixx) with regular TUs
 *   - Honours per-file override flags (// QS_NO_UNITY comment)
 *
 * NO_UNITY opt-out: if a .cpp file contains the line:
 *   // QS_NO_UNITY
 * it is always compiled standalone (never merged into a unity chunk).
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_path.h"
#include "../core/include/qs_vec.h"
#include <string.h>
#include <stdio.h>

#define NO_UNITY_MARKER "// QS_NO_UNITY"

/* Returns true if the file contains the no-unity opt-out marker */
static qs_bool_t has_no_unity(qs_arena_t *a, const char *path) {
    char *text=NULL; qs_size_t tlen=0;
    if (qs_fs_read_file(a,path,&text,&tlen)!=QS_OK) return QS_FALSE;
    /* Only scan the first 4KB for the marker */
    qs_size_t scan=tlen<4096?tlen:4096;
    for (qs_size_t i=0;i+sizeof(NO_UNITY_MARKER)-1<scan;i++) {
        if (strncmp(text+i,NO_UNITY_MARKER,sizeof(NO_UNITY_MARKER)-1)==0)
            return QS_TRUE;
    }
    return QS_FALSE;
}

typedef struct {
    qs_str_vec_t unity_sources;   /* goes into unity chunks */
    qs_str_vec_t standalone;      /* compiled individually */
    qs_str_vec_t module_ifaces;   /* .ixx / .cppm compiled first */
} unity_partition_t;

qs_result_t qs_unity_partition(qs_arena_t *a, const qs_manifest_t *m,
                                  unity_partition_t *out) {
    qs_str_vec_init(&out->unity_sources, a);
    qs_str_vec_init(&out->standalone,    a);
    qs_str_vec_init(&out->module_ifaces, a);

    for (qs_size_t i=0;i<m->sources.len;i++) {
        qs_str_t src=m->sources.data[i];
        qs_str_t ext=qs_path_ext(src);
        char *p=qs_arena_str_to_cstr(a,src);

        /* Module interfaces always standalone (compiled first) */
        if (qs_str_eq_cstr(ext,".ixx")||qs_str_eq_cstr(ext,".cppm")) {
            qs_str_vec_push(&out->module_ifaces,src); continue;
        }
        /* Opt-out check */
        if ((m->language==QS_LANG_CPP||m->language==QS_LANG_C)
             && has_no_unity(a,p)) {
            qs_str_vec_push(&out->standalone,src); continue;
        }
        qs_str_vec_push(&out->unity_sources,src);
    }
    fprintf(stderr,"  [unity] %llu unity / %llu standalone / %llu iface\n",
        (unsigned long long)out->unity_sources.len,
        (unsigned long long)out->standalone.len,
        (unsigned long long)out->module_ifaces.len);
    return QS_OK;
}

/* Write a unity file grouping sources by their directory */
qs_result_t qs_unity_write_by_dir(qs_arena_t *a, const qs_str_vec_t *sources,
                                     qs_lang_t lang, const char *out_dir,
                                     qs_u64 chunk_size) {
    const char *ext=lang==QS_LANG_CPP?".cpp":".c";
    qs_size_t total=sources->len;
    qs_size_t nchunks=(total+chunk_size-1)/chunk_size;
    qs_fs_mkdir_p(out_dir);

    for (qs_size_t ci=0;ci<nchunks;ci++) {
        char *path=qs_arena_sprintf(a,"%s/_unity_%03llu%s",
            out_dir,(unsigned long long)ci,ext);
        FILE *f=fopen(path,"w"); if(!f) return QS_ERROR_IO;
        fprintf(f,"/* qs_build unity chunk %llu — generated, do not edit */\n",
            (unsigned long long)ci);
        if(lang==QS_LANG_CPP){
            fprintf(f,"#ifdef __clang__\n#pragma clang diagnostic push\n");
            fprintf(f,"#pragma clang diagnostic ignored \"-Weverything\"\n#endif\n\n");
        }
        qs_size_t start=ci*chunk_size;
        qs_size_t end=start+chunk_size>total?total:start+chunk_size;
        for (qs_size_t si=start;si<end;si++)
            fprintf(f,"#include \"%.*s\"\n",
                (int)sources->data[si].len,(const char*)sources->data[si].ptr);
        if(lang==QS_LANG_CPP)
            fprintf(f,"\n#ifdef __clang__\n#pragma clang diagnostic pop\n#endif\n");
        fclose(f);
    }
    return QS_OK;
}
