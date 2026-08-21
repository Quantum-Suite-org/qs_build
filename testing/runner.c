/*
 * testing/runner.c — Test runner: discovers and executes test binaries,
 * collects results, emits TAP output, and feeds failures into the diag engine.
 *
 * Discovery rules:
 *   - Files listed in manifest.tests are compiled and linked as test executables.
 *   - Each test binary is run; exit code 0 = pass, non-zero = fail.
 *   - stdout is scanned for "ok N - description" / "not ok N - description" (TAP).
 *   - A summary is printed: N passed, M failed, K skipped.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_process.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_path.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    qs_u64     total, passed, failed, skipped;
    qs_bool_t  any_fail;
} qs_test_summary_t;

static void parse_tap(const char *out, qs_u64 *passed, qs_u64 *failed, qs_u64 *skipped) {
    const char *p = out;
    while (*p) {
        /* Find newline */
        const char *nl = strchr(p,'\n');
        qs_size_t ll = nl ? (qs_size_t)(nl-p) : strlen(p);
        if (ll>=2) {
            if (strncmp(p,"ok ",3)==0)      (*passed)++;
            else if (strncmp(p,"not ok ",7)==0) {
                if (strstr(p,"# SKIP")||strstr(p,"# skip")) (*skipped)++;
                else (*failed)++;
            }
        }
        if (!nl) break;
        p = nl+1;
    }
}

static qs_result_t run_one_test(qs_arena_t *a, const char *bin_path,
                                  qs_u64 timeout_us,
                                  qs_u64 *passed, qs_u64 *failed, qs_u64 *skipped,
                                  qs_diag_engine_t *diag, qs_bool_t verbose) {
    const char *argv[] = { bin_path, NULL };
    qs_proc_result_t pr;
    qs_result_t r = qs_proc_run(a, argv, NULL, timeout_us, &pr);
    if (r != QS_OK) {
        qs_diag_emit_simple(diag, QS_SEV_ERROR,"runner","QSB-TST001",QS_LOC_UNKNOWN,
            qs_arena_sprintf(a,"failed to launch test: %s",bin_path));
        return r;
    }

    if (verbose && pr.stdout_text[0])
        fprintf(stdout,"%s",pr.stdout_text);

    qs_u64 p=0,f=0,s=0;
    parse_tap(pr.stdout_text,&p,&f,&s);

    /* If no TAP output, use exit code */
    if (!p&&!f&&!s) {
        if (pr.exit_code==0) p=1; else f=1;
    }
    *passed  += p;
    *failed  += f;
    *skipped += s;

    if (pr.exit_code!=0 && f==0) {
        f++; *failed=*failed+1;
        qs_diag_emit_simple(diag,QS_SEV_ERROR,"runner","QSB-TST002",QS_LOC_UNKNOWN,
            qs_arena_sprintf(a,"test exited with code %lld: %s",
                (long long)pr.exit_code, bin_path));
        if (pr.stderr_text[0])
            qs_diag_ingest_tool_output(diag,a,QS_STR("test"),pr.stderr_text,pr.exit_code);
    }

    const char *status_col = (f>0)?"\033[31m✗\033[0m":"\033[32m✓\033[0m";
    fprintf(stderr,"  %s  %s",status_col,bin_path);
    if (p||f||s)
        fprintf(stderr,"  (pass=%llu fail=%llu skip=%llu)",
            (unsigned long long)p,(unsigned long long)f,(unsigned long long)s);
    fprintf(stderr,"\n");

    return f>0?QS_ERROR_COMPILE:QS_OK;
}

qs_result_t qs_test_run_all(qs_arena_t *a, const char *const *test_bins,
                               qs_size_t count, qs_u64 timeout_us,
                               qs_diag_engine_t *diag, qs_bool_t verbose) {
    if (!count) {
        fprintf(stderr,"  [test] no test binaries\n");
        return QS_OK;
    }
    fprintf(stderr,"\n\033[1m  Running %llu test(s):\033[0m\n",(unsigned long long)count);

    qs_u64 total_pass=0,total_fail=0,total_skip=0;
    for (qs_size_t i=0;i<count;i++) {
        if (!qs_fs_exists(test_bins[i])) {
            qs_diag_emit_simple(diag,QS_SEV_WARNING,"runner","QSB-TST003",
                QS_LOC_UNKNOWN,qs_arena_sprintf(a,"test binary not found: %s",test_bins[i]));
            continue;
        }
        run_one_test(a,test_bins[i],timeout_us,
            &total_pass,&total_fail,&total_skip,diag,verbose);
    }

    fprintf(stderr,"\n\033[1m  Test summary:\033[0m");
    fprintf(stderr,"  \033[32m%llu passed\033[0m",  (unsigned long long)total_pass);
    fprintf(stderr,"  \033[31m%llu failed\033[0m",  (unsigned long long)total_fail);
    if (total_skip)
        fprintf(stderr,"  \033[33m%llu skipped\033[0m",(unsigned long long)total_skip);
    fprintf(stderr,"\n\n");

    return total_fail>0?QS_ERROR_COMPILE:QS_OK;
}
