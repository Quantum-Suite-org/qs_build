/*
 * extensions/ext_loader.c — Extension loader for qs_build.
 * Extensions are like plugins but simpler: they are scripts or executables
 * that follow the qs_build extension protocol via stdin/stdout JSON RPC.
 *
 * Extension protocol (one JSON object per line):
 *   → {"method":"init","params":{"version":1,"manifest":{...}}}
 *   ← {"result":"ok","name":"my-ext","version":"1.0"}
 *   → {"method":"pre_build","params":{}}
 *   ← {"result":"ok"}
 *   → {"method":"post_build","params":{"success":true}}
 *   ← {"result":"ok"}
 *   → {"method":"shutdown"}
 *   ← {"result":"ok"}
 *
 * Extensions are discovered in <workspace>/.qs_extensions/ and
 * QS_EXTENSION_PATH env variable.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_process.h"
#include "../core/include/qs_fs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define QS_MAX_EXTENSIONS 32

typedef struct {
    char     *name;
    char     *path;       /* executable or script path */
    char     *version;
    qs_bool_t active;
} qs_extension_t;

typedef struct {
    qs_extension_t  exts[QS_MAX_EXTENSIONS];
    qs_size_t       count;
    qs_arena_t     *arena;
    qs_diag_engine_t *diag;
} qs_ext_manager_t;

void qs_ext_manager_init(qs_ext_manager_t *em, qs_arena_t *a, qs_diag_engine_t *d) {
    memset(em,0,sizeof(*em));
    em->arena=a; em->diag=d;
}

/* Send one JSON-RPC call to an extension and get the response */
static qs_bool_t ext_call(qs_arena_t *a, const char *exe, const char *request,
                            char **response_out) {
    /* Write request to a temp file and pass as stdin */
    char *req_path=qs_arena_sprintf(a,"/tmp/qs_ext_req_%llu.json",
        (unsigned long long)(qs_size_t)request);
    qs_fs_write_file(req_path,request,strlen(request));

    /* Run extension with request on stdin */
    const char *argv[]={exe,NULL};
    qs_proc_result_t pr;
    /* Use a very short timeout for extension calls */
    if (qs_proc_run(a,argv,NULL,5000000ULL,&pr)!=QS_OK) return QS_FALSE;
    if (response_out) *response_out=pr.stdout_text;
    return pr.exit_code==0?QS_TRUE:QS_FALSE;
}

qs_result_t qs_ext_register(qs_ext_manager_t *em, const char *path) {
    if (em->count>=QS_MAX_EXTENSIONS) return QS_ERROR_INTERNAL;
    if (!qs_fs_exists(path)) return QS_ERROR_NOT_FOUND;

    /* Send init probe */
    char *resp=NULL;
    const char *init_req="{\"method\":\"init\",\"params\":{\"version\":1}}\n";
    qs_bool_t ok=ext_call(em->arena,path,init_req,&resp);
    if (!ok) {
        qs_diag_emit_simple(em->diag,QS_SEV_WARNING,"ext","QSB-EXT001",
            QS_LOC_UNKNOWN,qs_arena_sprintf(em->arena,
                "extension init failed: %s",path));
        return QS_ERROR_PROCESS;
    }

    qs_extension_t *e=&em->exts[em->count++];
    e->path   = qs_arena_strdup(em->arena,path);
    e->active = QS_TRUE;
    /* Extract name from response (simple scan) */
    char *np=resp?strstr(resp,"\"name\":")  :NULL;
    char *vp=resp?strstr(resp,"\"version\"):"):NULL;
    e->name    = np?qs_arena_strdup(em->arena,np+8)
                  :qs_arena_strdup(em->arena,"unknown");
    e->version = vp?qs_arena_strdup(em->arena,vp+10)
                  :qs_arena_strdup(em->arena,"?");
    /* Trim quotes from simple string parse */
    if(e->name[0]=='"') { e->name++; char*q=strchr(e->name,'"'); if(q)*q='\0'; }
    fprintf(stderr,"  [ext] registered: %s @ %s\n",e->name,path);
    return QS_OK;
}

qs_result_t qs_ext_scan_dir(qs_ext_manager_t *em, const char *dir) {
    if (!qs_fs_is_dir(dir)) return QS_OK;
    qs_str_vec_t entries; qs_str_vec_init(&entries,em->arena);
    qs_fs_list_dir(em->arena,dir,&entries);
    for (qs_size_t i=0;i<entries.len;i++) {
        char *p=qs_arena_str_to_cstr(em->arena,entries.data[i]);
        qs_ext_register(em,p);
    }
    return QS_OK;
}

void qs_ext_run_pre_build(qs_ext_manager_t *em) {
    for (qs_size_t i=0;i<em->count;i++) {
        if (!em->exts[i].active) continue;
        ext_call(em->arena,em->exts[i].path,
            "{\"method\":\"pre_build\",\"params\":{}}\n",NULL);
    }
}

void qs_ext_run_post_build(qs_ext_manager_t *em, qs_bool_t success) {
    for (qs_size_t i=0;i<em->count;i++) {
        if (!em->exts[i].active) continue;
        char req[128];
        snprintf(req,sizeof(req),
            "{\"method\":\"post_build\",\"params\":{\"success\":%s}}\n",
            success?"true":"false");
        ext_call(em->arena,em->exts[i].path,req,NULL);
    }
}
