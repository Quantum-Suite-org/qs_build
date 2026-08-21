/*
 * compiler/ruby_driver.c — Ruby build driver.
 * Handles: gem build (.gemspec → .gem), mruby embedding (mrbc → .c),
 * syntax checking (ruby --check), and gem publish via gem push.
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

/* Generate a minimal .gemspec if none exists */
static char *ensure_gemspec(qs_arena_t *a, const qs_manifest_t *m, const char *src_dir) {
    if (!QS_STR_IS_EMPTY(m->ruby_gemspec))
        return qs_arena_str_to_cstr(a, m->ruby_gemspec);
    char *path = qs_arena_sprintf(a,"%s/%.*s.gemspec",
        src_dir,(int)m->name.len,(const char*)m->name.ptr);
    if (qs_fs_exists(path)) return path;
    FILE *f = fopen(path,"w"); if(!f) return NULL;
    fprintf(f,"Gem::Specification.new do |s|\n");
    fprintf(f,"  s.name    = '%.*s'\n",(int)m->name.len,(const char*)m->name.ptr);
    fprintf(f,"  s.version = '%.*s'\n",
        (int)m->version.len,QS_STR_IS_EMPTY(m->version)?(const qs_u8*)"0.1.0":m->version.ptr);
    fprintf(f,"  s.summary = 'Built by qs_build'\n");
    fprintf(f,"  s.files   = Dir['lib/**/*.rb']\n");
    fprintf(f,"  s.require_paths = ['lib']\n");
    fprintf(f,"end\n");
    fclose(f);
    return path;
}

/* Syntax-check all .rb files: ruby --check file */
qs_result_t qs_ruby_syntax_check(qs_arena_t *a, const qs_manifest_t *m,
                                   const qs_toolchain_t *tc,
                                   qs_diag_engine_t *diag, qs_bool_t verbose) {
    qs_bool_t any_fail = QS_FALSE;
    for (qs_size_t i = 0; i < m->sources.len; i++) {
        char *src = qs_arena_str_to_cstr(a, m->sources.data[i]);
        const char *argv[] = { tc->executable,"--check",src,NULL };
        if (verbose) qs_proc_print_argv(argv);
        qs_proc_result_t pr;
        qs_result_t r = qs_proc_run(a,argv,NULL,30000000ULL,&pr);
        if (r!=QS_OK) continue;
        if (pr.exit_code!=0) {
            qs_diag_ingest_tool_output(diag,a,QS_STR("ruby"),pr.stderr_text,pr.exit_code);
            any_fail=QS_TRUE;
        }
    }
    return any_fail?QS_ERROR_COMPILE:QS_OK;
}

qs_result_t qs_ruby_build(qs_arena_t *a, const qs_manifest_t *m,
                            const qs_toolchain_t *tc,
                            const char *src_dir, const char *out_dir,
                            qs_diag_engine_t *diag,
                            qs_bool_t verbose, qs_bool_t dry_run,
                            char **output_path_out) {
    qs_fs_mkdir_p(out_dir);

    /* Always syntax-check first */
    qs_ruby_syntax_check(a,m,tc,diag,verbose);
    if (qs_diag_has_errors(diag)) return QS_ERROR_COMPILE;

    if (m->output_type == QS_OUT_GEM) {
        char *gemspec = ensure_gemspec(a,m,src_dir);
        if (!gemspec) return QS_ERROR_IO;
        char *gem_tool = qs_proc_find_in_path(a,"gem");
        if (!gem_tool) {
            qs_diag_emit_simple(diag,QS_SEV_ERROR,"ruby","QSB-RB001",QS_LOC_UNKNOWN,
                "'gem' not found on PATH; install RubyGems");
            return QS_ERROR_TOOLCHAIN;
        }
        const char *argv[] = {
            gem_tool,"build",gemspec,
            qs_arena_sprintf(a,"--output-dir=%s",out_dir), NULL
        };
        if (verbose) qs_proc_print_argv(argv);
        if (dry_run) return QS_OK;
        qs_proc_result_t pr;
        qs_result_t r = qs_proc_run(a,argv,NULL,60000000ULL,&pr);
        if (r!=QS_OK) return r;
        qs_diag_ingest_tool_output(diag,a,QS_STR("gem"),pr.stderr_text,pr.exit_code);
        if (output_path_out)
            *output_path_out=qs_arena_sprintf(a,"%s/%.*s-%.*s.gem",
                out_dir,(int)m->name.len,(const char*)m->name.ptr,
                (int)m->version.len,QS_STR_IS_EMPTY(m->version)?(const qs_u8*)"0.1.0":m->version.ptr);
        return pr.exit_code==0?QS_OK:QS_ERROR_COMPILE;
    }

    /* Default: just syntax-check (Ruby is interpreted) */
    qs_diag_emit_simple(diag,QS_SEV_NOTE,"ruby","QSB-RB002",QS_LOC_UNKNOWN,
        "Ruby is interpreted; syntax check passed, no binary produced");
    return QS_OK;
}

qs_result_t qs_ruby_publish(qs_arena_t *a, const qs_manifest_t *m,
                              const char *out_dir, qs_diag_engine_t *diag,
                              qs_bool_t verbose) {
    char *gem_tool = qs_proc_find_in_path(a,"gem");
    if (!gem_tool) return QS_ERROR_TOOLCHAIN;
    char *gem_file = qs_arena_sprintf(a,"%s/%.*s-%.*s.gem",
        out_dir,(int)m->name.len,(const char*)m->name.ptr,
        (int)m->version.len,QS_STR_IS_EMPTY(m->version)?(const qs_u8*)"0.1.0":m->version.ptr);
    const char *argv[] = { gem_tool,"push",gem_file,NULL };
    if (verbose) qs_proc_print_argv(argv);
    qs_proc_result_t pr;
    qs_result_t r = qs_proc_run(a,argv,NULL,60000000ULL,&pr);
    if (r!=QS_OK) return r;
    qs_diag_ingest_tool_output(diag,a,QS_STR("gem"),pr.stderr_text,pr.exit_code);
    return pr.exit_code==0?QS_OK:QS_ERROR_PROCESS;
}
