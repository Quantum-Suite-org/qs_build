/*
 * compiler/rust_driver.c — Rust build driver via cargo/rustc.
 * Prefers cargo when available. Falls back to direct rustc invocation.
 * Parses --error-format=json for fully structured diagnostics.
 * Output types: exe, dll (cdylib), lib (staticlib), rlib.
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

/* Generate a minimal Cargo.toml if none exists */
static void ensure_cargo_toml(qs_arena_t *a, const qs_manifest_t *m, const char *src_dir) {
    char *path = qs_arena_sprintf(a,"%s/Cargo.toml",src_dir);
    if (qs_fs_exists(path)) return;
    FILE *f = fopen(path,"w"); if(!f) return;
    fprintf(f,"[package]\n");
    fprintf(f,"name    = \"%.*s\"\n",(int)m->name.len,(const char*)m->name.ptr);
    fprintf(f,"version = \"%.*s\"\n",
        (int)m->version.len,QS_STR_IS_EMPTY(m->version)?(const qs_u8*)"0.1.0":m->version.ptr);
    fprintf(f,"edition = \"%s\"\n",
        QS_STR_IS_EMPTY(m->standard)?"2021":(const char*)m->standard.ptr);

    /* Map output type to [lib] crate-type */
    const char *crate_type = "bin";
    if      (m->output_type==QS_OUT_LIB)    crate_type="staticlib";
    else if (m->output_type==QS_OUT_DLL||
             m->output_type==QS_OUT_CDYLIB) crate_type="cdylib";

    if (strcmp(crate_type,"bin")!=0) {
        fprintf(f,"\n[lib]\n");
        fprintf(f,"crate-type = [\"%s\"]\n",crate_type);
    }
    if (m->enable_lto) {
        fprintf(f,"\n[profile.release]\n");
        fprintf(f,"lto = true\n");
        fprintf(f,"codegen-units = 1\n");
        fprintf(f,"opt-level = 3\n");
    }
    fclose(f);
}

#define ARGV_MAX 256
static int rs_ac; static const char *rs_av[ARGV_MAX];
#define PUSH(s) do{if(rs_ac<ARGV_MAX-1){rs_av[rs_ac++]=(s);rs_av[rs_ac]=NULL;}}while(0)
#define PUSHF(f,...) PUSH(qs_arena_sprintf(a,f,__VA_ARGS__))

qs_result_t qs_rust_build(qs_arena_t *a, const qs_manifest_t *m,
                            const qs_toolchain_t *tc,
                            const char *src_dir, const char *out_dir,
                            const char *target_triple,
                            qs_diag_engine_t *diag,
                            qs_bool_t verbose, qs_bool_t dry_run,
                            char **output_path_out) {
    qs_fs_mkdir_p(out_dir);
    char *cargo = qs_proc_find_in_path(a,"cargo");

    if (cargo) {
        ensure_cargo_toml(a,m,src_dir);
        rs_ac=0;
        PUSH(cargo); PUSH("build"); PUSH("--release");
        PUSH("--message-format=json");
        PUSHF("--target-dir=%s",out_dir);
        if (target_triple && *target_triple) PUSHF("--target=%s",target_triple);
        if (m->enable_lto) PUSH("--profile=release");
        PUSH("--manifest-path");
        PUSH(qs_arena_sprintf(a,"%s/Cargo.toml",src_dir));
        if (verbose) qs_proc_print_argv(rs_av);
        if (dry_run) return QS_OK;
        qs_proc_result_t pr;
        qs_result_t r=qs_proc_run(a,rs_av,NULL,600000000ULL,&pr);
        if (r!=QS_OK) return r;
        qs_diag_ingest_tool_output(diag,a,QS_STR("rustc"),pr.stdout_text,pr.exit_code);
        if (pr.exit_code!=0)
            qs_diag_ingest_tool_output(diag,a,QS_STR("rustc"),pr.stderr_text,pr.exit_code);
        if (output_path_out)
            *output_path_out=qs_arena_sprintf(a,"%s/release/%.*s",
                out_dir,(int)m->name.len,(const char*)m->name.ptr);
        return pr.exit_code==0?QS_OK:QS_ERROR_COMPILE;
    }

    /* Fallback: direct rustc */
    rs_ac=0;
    PUSH(tc->executable);
    PUSH("--error-format=json");
    PUSHF("--edition=%s",QS_STR_IS_EMPTY(m->standard)?"2021":(const char*)m->standard.ptr);
    if (m->enable_lto) PUSH("-C"); if (m->enable_lto) PUSH("lto=fat");
    if (target_triple && *target_triple) PUSHF("--target=%s",target_triple);

    /* crate type */
    if      (m->output_type==QS_OUT_LIB)    { PUSH("--crate-type=staticlib"); }
    else if (m->output_type==QS_OUT_DLL||
             m->output_type==QS_OUT_CDYLIB) { PUSH("--crate-type=cdylib");    }
    else                                     { PUSH("--crate-type=bin");       }

    PUSHF("--crate-name=%.*s",(int)m->name.len,(const char*)m->name.ptr);
    PUSH("-C"); PUSH("opt-level=3");
    PUSH("-o"); PUSH(qs_arena_sprintf(a,"%s/%.*s",
        out_dir,(int)m->name.len,(const char*)m->name.ptr));

    /* Find main.rs or lib.rs */
    char *main_rs=qs_arena_sprintf(a,"%s/src/main.rs",src_dir);
    char *lib_rs =qs_arena_sprintf(a,"%s/src/lib.rs",src_dir);
    if (qs_fs_exists(main_rs)) PUSH(main_rs);
    else if (qs_fs_exists(lib_rs)) PUSH(lib_rs);
    else if (m->sources.len) PUSH(qs_arena_str_to_cstr(a,m->sources.data[0]));

    if (verbose) qs_proc_print_argv(rs_av);
    if (dry_run) return QS_OK;
    qs_proc_result_t pr;
    qs_result_t r=qs_proc_run(a,rs_av,NULL,300000000ULL,&pr);
    if (r!=QS_OK) return r;
    qs_diag_ingest_tool_output(diag,a,QS_STR("rustc"),pr.stderr_text,pr.exit_code);
    return pr.exit_code==0?QS_OK:QS_ERROR_COMPILE;
}
