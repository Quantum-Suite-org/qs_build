/*
 * scripting/script_api.c — Scripting hooks for build.qs custom build steps.
 *
 * build.qs can declare pre_build / post_build / on_error script hooks:
 *
 *   pre_build  = "scripts/gen_version.py";
 *   post_build = "scripts/package.sh";
 *   on_error   = "scripts/notify_slack.sh";
 *
 * Scripts receive these environment variables:
 *   QS_MODULE_NAME      — manifest name
 *   QS_MODULE_VERSION   — manifest version
 *   QS_LANGUAGE         — language identifier
 *   QS_OUTPUT_TYPE      — output type string
 *   QS_OUT_DIR          — build output directory
 *   QS_MANIFEST_PATH    — path to build.qs
 *   QS_BUILD_STATUS     — "ok" or "error" (only for post_build / on_error)
 *
 * Scripts exit 0 on success; non-zero aborts the build with an error.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_process.h"
#include "../core/include/qs_fs.h"
#include <string.h>
#include <stdio.h>

typedef enum { QS_HOOK_PRE=0, QS_HOOK_POST, QS_HOOK_ON_ERROR } qs_hook_t;

static const char **build_env(qs_arena_t *a, const qs_manifest_t *m,
                                const char *out_dir, qs_bool_t success) {
    const char **env=(const char**)qs_arena_alloc(a,sizeof(char*)*16,_Alignof(char*));
    int i=0;
    env[i++]=qs_arena_sprintf(a,"QS_MODULE_NAME=%.*s",
        (int)m->name.len,(const char*)m->name.ptr);
    env[i++]=qs_arena_sprintf(a,"QS_MODULE_VERSION=%.*s",
        (int)m->version.len,QS_STR_IS_EMPTY(m->version)?(const qs_u8*)"0.1.0":m->version.ptr);
    env[i++]=qs_arena_sprintf(a,"QS_LANGUAGE=%s",qs_lang_name(m->language));
    env[i++]=qs_arena_sprintf(a,"QS_OUTPUT_TYPE=%s",qs_output_type_str(m->output_type));
    env[i++]=qs_arena_sprintf(a,"QS_OUT_DIR=%s",out_dir?out_dir:"./out");
    env[i++]=qs_arena_sprintf(a,"QS_MANIFEST_PATH=%.*s",
        (int)m->manifest_path.len,(const char*)m->manifest_path.ptr);
    env[i++]=qs_arena_sprintf(a,"QS_BUILD_STATUS=%s",success?"ok":"error");
    env[i]=NULL;
    return env;
}

static const char *detect_interpreter(qs_arena_t *a, const char *script) {
    /* Pick interpreter from extension */
    qs_str_t ext=qs_path_ext(qs_str_from_cstr(script));
    if (qs_str_eq_cstr(ext,".py"))  {
        char *py=qs_proc_find_in_path(a,"python3");
        return py?py:"python3";
    }
    if (qs_str_eq_cstr(ext,".rb"))  { char *r=qs_proc_find_in_path(a,"ruby"); return r?r:"ruby"; }
    if (qs_str_eq_cstr(ext,".js"))  { char *n=qs_proc_find_in_path(a,"node"); return n?n:"node"; }
    if (qs_str_eq_cstr(ext,".sh"))  { char *b=qs_proc_find_in_path(a,"bash"); return b?b:"sh"; }
    if (qs_str_eq_cstr(ext,".ps1")) return "powershell";
    /* Assume executable */
    return script;
}

qs_result_t qs_hook_run(qs_arena_t *a, const qs_manifest_t *m,
                          const char *script_path, qs_hook_t hook,
                          const char *out_dir, qs_bool_t build_ok,
                          qs_diag_engine_t *diag, qs_bool_t verbose) {
    if (!script_path||!*script_path) return QS_OK;
    if (!qs_fs_exists(script_path)) {
        qs_diag_emit_simple(diag,QS_SEV_WARNING,"script","QSB-SCR001",
            QS_LOC_UNKNOWN,qs_arena_sprintf(a,"hook script not found: %s",script_path));
        return QS_OK;
    }

    const char *interp=detect_interpreter(a,script_path);
    const char *argv[4];
    int ac=0;
    if (strcmp(interp,script_path)!=0) { argv[ac++]=interp; }
    argv[ac++]=script_path;
    argv[ac]=NULL;

    const char **env=build_env(a,m,out_dir,build_ok);
    if (verbose) {
        fprintf(stderr,"  [hook:%s] %s\n",
            hook==QS_HOOK_PRE?"pre":hook==QS_HOOK_POST?"post":"on_error",
            script_path);
    }

    qs_proc_result_t pr;
    qs_result_t r=qs_proc_run(a,argv,env,60000000ULL,&pr);
    if (r!=QS_OK) return r;
    if (pr.exit_code!=0) {
        const char *err=pr.stderr_text[0]?pr.stderr_text:pr.stdout_text;
        qs_diag_emit_simple(diag,QS_SEV_ERROR,"script","QSB-SCR002",
            QS_LOC_UNKNOWN,
            qs_arena_sprintf(a,"hook script failed (exit %lld): %s",
                (long long)pr.exit_code,script_path));
        if (err&&*err)
            qs_diag_emit_simple(diag,QS_SEV_NOTE,"script","",QS_LOC_UNKNOWN,err);
        return QS_ERROR_PROCESS;
    }
    return QS_OK;
}
