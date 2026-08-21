/*
 * headers/header_scan.c — Header dependency scanner.
 * Scans C/C++ source files for #include directives and builds a
 * complete transitive dependency graph. Used for:
 *   - Determining which TUs to recompile when a header changes
 *   - Ordering headers in the PCH amalgam (most-included first)
 *   - Detecting include cycles
 *   - Generating include-what-you-use reports
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_path.h"
#include "../core/include/qs_vec.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* One #include directive found in a source file */
typedef struct {
    char     *included;   /* the string inside the quotes/angles */
    qs_bool_t system;     /* true = <header>, false = "header" */
    qs_u64    line;
} qs_include_t;

QS_VEC_DECL(qs_include_t, qs_include_vec)

/* Scan one file for #include directives (non-recursive) */
qs_result_t qs_header_scan_file(qs_arena_t *a, const char *path,
                                   qs_include_vec_t *out) {
    qs_include_vec_init(out, a);
    char *text=NULL; qs_size_t tlen=0;
    if (qs_fs_read_file(a,path,&text,&tlen)!=QS_OK) return QS_ERROR_NOT_FOUND;

    const char *p=text; qs_u64 line=1;
    while (*p) {
        if (*p=='\n'){line++;p++;continue;}
        /* Skip non-# lines fast */
        if (*p!='#'){while(*p&&*p!='\n')p++;continue;}
        p++;
        while(*p==' '||*p=='\t') p++;
        if (strncmp(p,"include",7)!=0){while(*p&&*p!='\n')p++;continue;}
        p+=7;
        while(*p==' '||*p=='\t') p++;
        char delim_close;
        qs_bool_t system_hdr;
        if      (*p=='"') { delim_close='"';  system_hdr=QS_FALSE; }
        else if (*p=='<') { delim_close='>';  system_hdr=QS_TRUE;  }
        else {while(*p&&*p!='\n')p++;continue;}
        p++;
        const char *start=p;
        while(*p&&*p!=delim_close&&*p!='\n') p++;
        qs_size_t ilen=(qs_size_t)(p-start);
        if(ilen>0){
            qs_include_t inc;
            inc.included=qs_arena_alloc(a,ilen+1,1);
            memcpy(inc.included,start,ilen); inc.included[ilen]='\0';
            inc.system=system_hdr;
            inc.line=line;
            qs_include_vec_push(out,inc);
        }
        while(*p&&*p!='\n') p++;
    }
    return QS_OK;
}

/* Count how many times each header is included across all sources */
typedef struct { char *header; qs_u64 count; } hdr_freq_t;
QS_VEC_DECL(hdr_freq_t, hdr_freq_vec)

qs_result_t qs_header_frequency(qs_arena_t *a, const qs_str_vec_t *sources,
                                   hdr_freq_vec_t *out) {
    hdr_freq_vec_init(out, a);
    for (qs_size_t i=0;i<sources->len;i++) {
        char *src=qs_arena_str_to_cstr(a,sources->data[i]);
        qs_include_vec_t incs;
        if (qs_header_scan_file(a,src,&incs)!=QS_OK) continue;
        for (qs_size_t j=0;j<incs.len;j++) {
            if (incs.data[j].system) continue; /* skip <system> headers */
            /* Find or add to frequency table */
            qs_bool_t found=QS_FALSE;
            for (qs_size_t k=0;k<out->len;k++) {
                if(strcmp(out->data[k].header,incs.data[j].included)==0){
                    out->data[k].count++;
                    found=QS_TRUE; break;
                }
            }
            if(!found){
                hdr_freq_t hf;
                hf.header=qs_arena_strdup(a,incs.data[j].included);
                hf.count=1;
                hdr_freq_vec_push(out,hf);
            }
        }
    }
    /* Sort descending by count (simple insertion sort — small N) */
    for (qs_size_t i=1;i<out->len;i++){
        hdr_freq_t key=out->data[i]; qs_size_t j=i;
        while(j>0&&out->data[j-1].count<key.count){out->data[j]=out->data[j-1];j--;}
        out->data[j]=key;
    }
    return QS_OK;
}

void qs_header_print_frequency(const hdr_freq_vec_t *freq) {
    fprintf(stderr,"[headers] include frequency (top 20):\n");
    qs_size_t limit=freq->len<20?freq->len:20;
    for(qs_size_t i=0;i<limit;i++)
        fprintf(stderr,"  %4llu  %s\n",
            (unsigned long long)freq->data[i].count,freq->data[i].header);
}
