/*
 * compiler/go_driver.c — Go build driver.
 * Invokes `go build`, `go test`, `go install`. Handles module detection,
 * cross-compilation via GOOS/GOARCH env vars, and go vet diagnostics.
 * Output types: exe (go build -o), lib (go build -buildmode=c-archive),
 * dll (go build -buildmode=c-shared), gobin (go install).
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_toolchain.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_process.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_path.h"
#include <string.h>
#include <stdio.h>

#define ARGV_MAX 256
static int go_ac; static const char *go_av[ARGV_MAX];
#define PUSH(s)       do{if(go_ac<ARGV_MAX-1){go_av[go_ac++]=(s);go_av[go_ac]=NULL;}}while(0)
#define PUSHF(f,...)  PUSH(qs_arena_sprintf(a,f,__VA_ARGS__))

/* Ensure go.mod exists in src_dir, create stub if missing */
static void ensure_gomod(qs_arena_t *a, const qs_manifest_t *m, const char *src_dir) {
    char *gomod = qs_arena_sprintf(a, "%s/go.mod", src_dir);
    if (qs_fs_exists(gomod)) return;
    const char *mod = QS_STR_IS_EMPTY(m->go_module)
        ? qs_arena_sprintf(a,"qs/%.*s",(int)m->name.len,(const char*)m->name.ptr)
        : (const char*)m->go_module.ptr;
    const char *ver = QS_STR_IS_EMPTY(m->standard) ? "1.22" : (const char*)m->standard.ptr;
    FILE *f = fopen(gomod,"w"); if (!f) return;
    fprintf(f,"module %s\n\ngo %s\n",mod,ver);
    fclose(f);
}

/* Map qs_output_type_t → go -buildmode flag */
static const char *go_buildmode(qs_output_type_t t) {
    switch(t) {
        case QS_OUT_LIB:    return "c-archive";
        case QS_OUT_DLL:    return "c-shared";
        case QS_OUT_GOBIN:  return "exe";
        default:            return "exe";
    }
}

qs_result_t qs_go_build(qs_arena_t *a, const qs_manifest_t *m,
                          const qs_toolchain_t *tc,
                          const char *src_dir, const char *out_dir,
                          const char *target_triple,
                          qs_diag_engine_t *diag,
                          qs_bool_t verbose, qs_bool_t dry_run,
                          char **output_path_out) {
    qs_fs_mkdir_p(out_dir);
    ensure_gomod(a, m, src_dir);

    /* Determine output binary name */
    const char *ext = "";
#ifdef _WIN32
    if (m->output_type==QS_OUT_EXE||m->output_type==QS_OUT_GOBIN) ext=".exe";
    else if (m->output_type==QS_OUT_DLL) ext=".dll";
    else if (m->output_type==QS_OUT_LIB) ext=".lib";
#else
    if (m->output_type==QS_OUT_DLL) ext=".so";
    else if (m->output_type==QS_OUT_LIB) ext=".a";
#endif
    char *outbin = qs_arena_sprintf(a,"%s/%.*s%s",
        out_dir,(int)m->name.len,(const char*)m->name.ptr,ext);
    if (output_path_out) *output_path_out = outbin;

    /* Build env for cross-compilation */
    char *env_goos=NULL, *env_goarch=NULL;
    const char *env_arr[32]; int env_c=0;
    if (target_triple && *target_triple) {
        /* Parse triple like "x86_64-linux-gnu" or "aarch64-apple-darwin" */
        if (strstr(target_triple,"windows")) env_goos=qs_arena_strdup(a,"GOOS=windows");
        else if (strstr(target_triple,"darwin")) env_goos=qs_arena_strdup(a,"GOOS=darwin");
        else if (strstr(target_triple,"linux"))  env_goos=qs_arena_strdup(a,"GOOS=linux");
        if (strstr(target_triple,"aarch64")||strstr(target_triple,"arm64"))
            env_goarch=qs_arena_strdup(a,"GOARCH=arm64");
        else if (strstr(target_triple,"x86_64")||strstr(target_triple,"amd64"))
            env_goarch=qs_arena_strdup(a,"GOARCH=amd64");
        if (env_goos)   { env_arr[env_c++]=env_goos;  env_arr[env_c]=NULL; }
        if (env_goarch) { env_arr[env_c++]=env_goarch; env_arr[env_c]=NULL; }
    }

    go_ac = 0;
    PUSH(tc->executable); PUSH("build");
    PUSHF("-buildmode=%s", go_buildmode(m->output_type));
    if (m->enable_lto) PUSH("-trimpath");
    PUSHF("-o=%s", outbin);
    /* gcflags for better diagnostics */
    PUSH("-gcflags=all=-e"); /* report all errors, not just first 10 */
    for (qs_size_t i=0;i<m->defines.len;i++)
        PUSHF("-ldflags=-X main.%.*s=1",
            (int)m->defines.data[i].len,(const char*)m->defines.data[i].ptr);
    PUSH(qs_arena_sprintf(a,"%s/...",src_dir)); /* all packages */

    if (verbose) qs_proc_print_argv(go_av);
    if (dry_run) return QS_OK;

    qs_proc_result_t pr;
    qs_result_t r = qs_proc_run(a, go_av, env_c?env_arr:NULL, 300000000ULL, &pr);
    if (r != QS_OK) return r;
    const char *err = pr.stderr_text[0]?pr.stderr_text:pr.stdout_text;
    qs_diag_ingest_tool_output(diag, a, QS_STR("go"), err, pr.exit_code);
    return pr.exit_code == 0 ? QS_OK : QS_ERROR_COMPILE;
}

/* Run go vet for static analysis */
qs_result_t qs_go_vet(qs_arena_t *a, const qs_manifest_t *m,
                        const qs_toolchain_t *tc, const char *src_dir,
                        qs_diag_engine_t *diag, qs_bool_t verbose) {
    const char *argv[] = {
        tc->executable,"vet",
        qs_arena_sprintf(a,"%s/...",src_dir), NULL
    };
    if (verbose) qs_proc_print_argv(argv);
    qs_proc_result_t pr;
    qs_result_t r = qs_proc_run(a, argv, NULL, 120000000ULL, &pr);
    if (r != QS_OK) return r;
    qs_diag_ingest_tool_output(diag, a, QS_STR("go"), pr.stderr_text, pr.exit_code);
    return QS_OK; /* vet failures are warnings, not fatal */
}

/* Run go test */
qs_result_t qs_go_test(qs_arena_t *a, const qs_manifest_t *m,
                         const qs_toolchain_t *tc, const char *src_dir,
                         qs_diag_engine_t *diag, qs_bool_t verbose) {
    const char *argv[] = {
        tc->executable,"test","-v","-count=1",
        qs_arena_sprintf(a,"%s/...",src_dir), NULL
    };
    if (verbose) qs_proc_print_argv(argv);
    qs_proc_result_t pr;
    qs_result_t r = qs_proc_run(a, argv, NULL, 300000000ULL, &pr);
    if (r != QS_OK) return r;
    qs_diag_ingest_tool_output(diag, a, QS_STR("go"), pr.stderr_text, pr.exit_code);
    if (pr.exit_code != 0) return QS_ERROR_COMPILE;
    return QS_OK;
}
