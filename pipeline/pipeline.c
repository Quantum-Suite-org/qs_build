/*
 * pipeline/pipeline.c — Top-level build pipeline orchestrator.
 * Reads manifest → probes toolchains → applies auto rules → builds PCH
 * → plans chunks → fingerprints → compiles (parallel) → links → packages.
 * All language drivers are dispatched from here.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_toolchain.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_pch.h"
#include "../core/include/qs_chunker.h"
#include "../core/include/qs_cache.h"
#include "../core/include/qs_linker.h"
#include "../core/include/qs_cli.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_path.h"
#include "../core/include/qs_jobs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* Forward declarations for language drivers */
typedef struct c_compile_ctx c_compile_ctx_t;
qs_result_t qs_c_compile_all(qs_arena_t*,const void*,const char*,qs_str_vec_t*);
qs_result_t qs_cpp_compile_all(qs_arena_t*,const qs_manifest_t*,const qs_toolchain_t*,
    const qs_pch_info_t*,const char*,qs_diag_engine_t*,qs_bool_t,qs_bool_t,qs_str_vec_t*);
qs_result_t qs_csharp_compile_all(qs_arena_t*,const qs_manifest_t*,const qs_toolchain_t*,
    const char*,qs_diag_engine_t*,qs_bool_t,qs_bool_t,char**);
qs_result_t qs_java_compile_all(qs_arena_t*,const qs_manifest_t*,const qs_toolchain_t*,
    const char*,qs_diag_engine_t*,qs_bool_t,qs_bool_t,char**);
qs_result_t qs_java_package_jar(qs_arena_t*,const qs_manifest_t*,const char*,const char*,
    qs_diag_engine_t*,qs_bool_t);
qs_result_t qs_python_build(qs_arena_t*,const qs_manifest_t*,const qs_toolchain_t*,
    const char*,const char*,qs_diag_engine_t*,qs_bool_t,qs_bool_t);
qs_result_t qs_python_publish(qs_arena_t*,const qs_manifest_t*,const qs_toolchain_t*,
    const char*,qs_diag_engine_t*,qs_bool_t);
qs_result_t qs_go_build(qs_arena_t*,const qs_manifest_t*,const qs_toolchain_t*,
    const char*,const char*,const char*,qs_diag_engine_t*,qs_bool_t,qs_bool_t,char**);
qs_result_t qs_ruby_build(qs_arena_t*,const qs_manifest_t*,const qs_toolchain_t*,
    const char*,const char*,qs_diag_engine_t*,qs_bool_t,qs_bool_t,char**);
qs_result_t qs_ruby_publish(qs_arena_t*,const qs_manifest_t*,const char*,
    qs_diag_engine_t*,qs_bool_t);
qs_result_t qs_rust_build(qs_arena_t*,const qs_manifest_t*,const qs_toolchain_t*,
    const char*,const char*,const char*,qs_diag_engine_t*,qs_bool_t,qs_bool_t,char**);
qs_result_t qs_zig_build(qs_arena_t*,const qs_manifest_t*,const qs_toolchain_t*,
    const char*,const char*,const char*,qs_diag_engine_t*,qs_bool_t,qs_bool_t,char**);

typedef struct {
    qs_manifest_t      manifest;
    qs_toolchain_set_t toolchains;
    qs_cache_t         cache;
    qs_diag_engine_t   diag;
    qs_cli_args_t      args;
    qs_arena_t        *arena;
    qs_arena_t        *scratch;
} qs_pipeline_t;

static qs_u64 time_us_now(void) {
    struct timespec ts;
#if defined(_WIN32)
    return (qs_u64)time(NULL)*1000000ULL;
#else
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return (qs_u64)ts.tv_sec*1000000ULL + (qs_u64)(ts.tv_nsec/1000);
#endif
}

static void print_banner(const qs_pipeline_t *p) {
    fprintf(stderr,"\n");
    fprintf(stderr,"  \033[1;36mqs_build\033[0m %s  |  %.*s  |  lang: %s  |  out: %s\n",
        "1.0.0-alpha",
        (int)p->manifest.name.len,(const char*)p->manifest.name.ptr,
        qs_lang_name(p->manifest.language),
        qs_output_type_str(p->manifest.output_type));
    fprintf(stderr,"  target: %s\n\n",
        p->args.target_triple ? p->args.target_triple : "(native)");
}

static qs_result_t dispatch_compile(qs_pipeline_t *pip,
                                     const char *src_dir,
                                     const char *obj_dir,
                                     qs_str_vec_t *obj_files) {
    qs_manifest_t *m   = &pip->manifest;
    qs_arena_t    *a   = pip->arena;
    qs_bool_t      vb  = pip->args.verbose||pip->args.show_commands;
    qs_bool_t      dry = pip->args.dry_run;
    /* Apply --toolchain override if specified */
    const qs_toolchain_t *tc = NULL;
    if (pip->args.toolchain_override && pip->args.toolchain_override[0]) {
        tc = qs_toolchain_by_name(&pip->toolchains, pip->args.toolchain_override);
        if (!tc) {
            qs_diag_emit_simple(&pip->diag,QS_SEV_FATAL,"pipeline","QSB-PRB001",
                QS_LOC_UNKNOWN,
                qs_arena_sprintf(a,"toolchain '%s' not found or not installed; "
                    "run qs_build --verbose --dry-run to see discovered compilers",
                    pip->args.toolchain_override));
            return QS_ERROR_TOOLCHAIN;
        }
    } else {
        tc = qs_toolchain_for_lang(&pip->toolchains, m->language);
    }

    if (!tc) {
        /* Explain WHY instead of just "not found" */
        qs_diag_emit_simple(&pip->diag,QS_SEV_FATAL,"pipeline","QSB-PIP001",
            QS_LOC_UNKNOWN,
            qs_arena_sprintf(a,
                "no toolchain found for language '%s'. "
                "Discovered %llu compiler(s) but none support this language. "
                "Install a compiler (e.g. MSVC for C/C++ on Windows) and ensure "
                "it is on PATH, or use --toolchain to specify one explicitly.",
                qs_lang_name(m->language),
                (unsigned long long)pip->toolchains.count));
        if (pip->toolchains.count > 0) qs_toolchain_print(&pip->toolchains);
        return QS_ERROR_TOOLCHAIN;
    }

    /* PCH */
    qs_pch_info_t pch; memset(&pch,0,sizeof(pch));
    qs_bool_t use_pch = (m->enable_pch && !pip->args.no_pch)||pip->args.force_pch;
    if (use_pch) {
        qs_result_t pr = QS_OK;
        switch(m->language) {
            case QS_LANG_C:      pr=qs_pch_build_c(a,m,tc,&pch,&pip->diag); break;
            case QS_LANG_CPP:    pr=qs_pch_build_cpp(a,m,tc,&pch,&pip->diag); break;
            case QS_LANG_CSHARP: pr=qs_pch_build_csharp(a,m,tc,&pch,&pip->diag); break;
            case QS_LANG_JAVA:   pr=qs_pch_build_java(a,m,tc,&pch,&pip->diag); break;
            case QS_LANG_PYTHON: pr=qs_pch_build_python(a,m,tc,&pch,&pip->diag); break;
            case QS_LANG_GO:     pr=qs_pch_build_go(a,m,tc,&pch,&pip->diag); break;
            case QS_LANG_RUBY:   pr=qs_pch_build_ruby(a,m,tc,&pch,&pip->diag); break;
            case QS_LANG_RUST:   pr=qs_pch_build_rust(a,m,tc,&pch,&pip->diag); break;
            case QS_LANG_ZIG:    pr=qs_pch_build_zig(a,m,tc,&pch,&pip->diag); break;
            default: break;
        }
        if (pr!=QS_OK)
            fprintf(stderr,"  \033[33mwarning:\033[0m PCH build failed; continuing without it\n");
    }

    /* Chunking */
    qs_bool_t use_chunks = (m->enable_chunks && !pip->args.no_chunks)||pip->args.force_chunks;
    if (use_chunks && (m->language==QS_LANG_C||m->language==QS_LANG_CPP)) {
        qs_chunk_plan_t plan;
        if (pip->args.chunk_size_override) m->chunk_size=pip->args.chunk_size_override;
        qs_chunker_plan(a,m,&plan,&pip->diag);
        char *fp_dir=qs_arena_sprintf(a,"%s/.qs_cache/chunks",pip->args.out_dir);
        qs_fs_mkdir_p(fp_dir);
        qs_chunker_fingerprint(a,&plan,fp_dir);
        if (m->language==QS_LANG_C) qs_chunker_write_c(a,&plan,&pip->diag);
        else                        qs_chunker_write_cpp(a,&plan,&pip->diag);
        /* Replace manifest sources with chunk files */
        qs_str_vec_clear(&m->sources);
        for (qs_size_t i=0;i<plan.chunks.len;i++)
            if (plan.chunks.data[i].needs_rebuild)
                qs_str_vec_push(&m->sources,
                    qs_str_from_cstr(plan.chunks.data[i].generated_path));
        if (vb) qs_chunker_print_plan(&plan);
    }

    /* Dispatch */
    char *output = NULL;
    qs_result_t r = QS_OK;
    switch(m->language) {
        case QS_LANG_CPP:
            r=qs_cpp_compile_all(a,m,tc,use_pch?&pch:NULL,
                obj_dir,&pip->diag,vb,dry,obj_files); break;
        case QS_LANG_CSHARP:
            r=qs_csharp_compile_all(a,m,tc,obj_dir,&pip->diag,vb,dry,&output);
            if (output) qs_str_vec_push(obj_files,qs_str_from_cstr(output));
            break;
        case QS_LANG_JAVA:
            r=qs_java_compile_all(a,m,tc,obj_dir,&pip->diag,vb,dry,&output);
            if (r==QS_OK&&!dry&&m->output_type==QS_OUT_JAR)
                r=qs_java_package_jar(a,m,output,pip->args.out_dir,&pip->diag,vb);
            break;
        case QS_LANG_PYTHON:
            r=qs_python_build(a,m,tc,src_dir,pip->args.out_dir,&pip->diag,vb,dry);
            if (r==QS_OK&&pip->args.publish&&!dry)
                qs_python_publish(a,m,tc,pip->args.out_dir,&pip->diag,vb);
            break;
        case QS_LANG_GO:
            r=qs_go_build(a,m,tc,src_dir,pip->args.out_dir,
                pip->args.target_triple,&pip->diag,vb,dry,&output);
            break;
        case QS_LANG_RUBY:
            r=qs_ruby_build(a,m,tc,src_dir,pip->args.out_dir,&pip->diag,vb,dry,&output);
            if (r==QS_OK&&pip->args.publish&&!dry)
                qs_ruby_publish(a,m,pip->args.out_dir,&pip->diag,vb);
            break;
        case QS_LANG_RUST:
            r=qs_rust_build(a,m,tc,src_dir,pip->args.out_dir,
                pip->args.target_triple,&pip->diag,vb,dry,&output);
            break;
        case QS_LANG_ZIG:
            r=qs_zig_build(a,m,tc,src_dir,pip->args.out_dir,
                pip->args.target_triple,&pip->diag,vb,dry,&output);
            break;
        case QS_LANG_C: {
            /* Build c_compile_ctx_t on the stack and call the C driver */
            struct {
                const qs_manifest_t  *manifest;
                const qs_toolchain_t *tc;
                const qs_pch_info_t  *pch;
                qs_cache_t           *cache;
                qs_diag_engine_t     *diag;
                qs_bool_t             verbose;
                qs_bool_t             dry_run;
            } c_ctx;
            c_ctx.manifest = m;
            c_ctx.tc       = tc;
            c_ctx.pch      = use_pch ? &pch : NULL;
            c_ctx.cache    = &pip->cache;
            c_ctx.diag     = &pip->diag;
            c_ctx.verbose  = vb;
            c_ctx.dry_run  = dry;
            r = qs_c_compile_all(a, &c_ctx, obj_dir, obj_files);
            break;
        }
        default:
            qs_diag_emit_simple(&pip->diag, QS_SEV_FATAL, "pipeline",
                "QSB-DRV001", QS_LOC_UNKNOWN,
                qs_arena_sprintf(a, "no driver registered for language: %s",
                    qs_lang_name(m->language)));
            r = QS_ERROR_TOOLCHAIN;
            break;
    }
    return r;
}

qs_result_t qs_pipeline_run(qs_arena_t *arena, qs_arena_t *scratch,
                              const qs_cli_args_t *args) {
    qs_pipeline_t pip; memset(&pip,0,sizeof(pip));
    pip.arena=arena; pip.scratch=scratch; pip.args=*args;
    qs_diag_engine_init(&pip.diag,arena);
    pip.diag.color      = args->color;
    pip.diag.json_mode  = args->json_diag;
    pip.diag.show_caret = QS_TRUE;
    pip.diag.show_raw   = args->verbose;
    pip.diag.error_limit= args->error_limit;

    qs_u64 t_start = time_us_now();

    /* 1. Parse manifest */
    qs_result_t r = qs_manifest_parse(arena, args->manifest_path, &pip.manifest, &pip.diag);
    if (r!=QS_OK||qs_diag_has_errors(&pip.diag)) goto done;

    /* 2. Override output type from CLI */
    pip.manifest.output_type = args->output_type;
    if (args->out_dir) pip.manifest.out_dir=qs_str_from_cstr(args->out_dir);

    /* 3. Apply auto-rules (PCH/chunk thresholds) */
    qs_manifest_apply_auto_rules(&pip.manifest);
    if (pip.args.force_pch)   pip.manifest.enable_pch    =QS_TRUE;
    if (pip.args.no_pch)      pip.manifest.enable_pch    =QS_FALSE;
    if (pip.args.force_chunks)pip.manifest.enable_chunks =QS_TRUE;
    if (pip.args.no_chunks)   pip.manifest.enable_chunks =QS_FALSE;

    /* 4. Validate */
    r = qs_manifest_validate(&pip.manifest,&pip.diag);
    if (qs_diag_has_errors(&pip.diag)) goto done;

    /* 5. Clean */
    if (args->clean) {
        fprintf(stderr,"[clean] removing %s\n",args->out_dir);
        qs_fs_remove_dir_recursive(args->out_dir);
    }
    qs_fs_mkdir_p(args->out_dir);

    /* 6. Probe toolchains.
     * Always probe if --toolchain override is set so we can validate it.
     * Skip probe on plain dry-run (no compilation, no override) for speed. */
    if (!args->dry_run || (args->toolchain_override && args->toolchain_override[0])) {
        qs_toolchain_probe_all(arena,&pip.toolchains,&pip.diag);
        if (args->verbose) qs_toolchain_print(&pip.toolchains);
    }

    /* 7. Init cache */
    qs_cache_init(arena,args->out_dir,&pip.cache);

    /* 8. Print banner */
    print_banner(&pip);

    /* 9. Determine source dir (directory containing manifest) */
    char *src_dir = qs_arena_strdup(arena,".");
    {
        qs_str_t mdir=qs_path_dir(arena,qs_str_from_cstr(args->manifest_path));
        if (!qs_str_eq_cstr(mdir,".")) src_dir=qs_arena_str_to_cstr(arena,mdir);
    }

    /* 9b. Resolve source paths relative to manifest directory.
     * Manifest sources like "src/main.c" are relative to the manifest file's
     * directory, not the cwd. Rewrite them to absolute paths now so every
     * driver receives a path that cl.exe / gcc can open from any cwd. */
    {
        for (qs_size_t i = 0; i < pip.manifest.sources.len; i++) {
            qs_str_t s2 = pip.manifest.sources.data[i];
            /* Skip if already absolute */
            if (qs_path_is_absolute(s2)) continue;
            char *abs = qs_arena_sprintf(arena, "%s/%.*s",
                src_dir, (int)s2.len, (const char*)s2.ptr);
            pip.manifest.sources.data[i] = qs_str_from_cstr(abs);
        }
    }

    /* 9c. Validate toolchain override is resolvable even on dry-run.
     * This ensures --toolchain nonexistent fails clearly regardless of
     * whether we are doing a real build or a dry-run. */
    if (args->toolchain_override && args->toolchain_override[0]) {
        if (!qs_toolchain_by_name(&pip.toolchains, args->toolchain_override)) {
            qs_diag_emit_simple(&pip.diag, QS_SEV_FATAL, "pipeline",
                "QSB-PRB001", QS_LOC_UNKNOWN,
                qs_arena_sprintf(arena,
                    "toolchain '%s' not found. "
                    "Run qs_build --verbose to see discovered compilers.",
                    args->toolchain_override));
            r = QS_ERROR_TOOLCHAIN;
            goto done;
        }
    }

    /* 10. Compile (or print dry-run plan) */
    if (args->dry_run) {
        fprintf(stderr,"[qs_build] dry-run: would compile %llu source(s) -> %s\n",
            (unsigned long long)pip.manifest.sources.len,
            qs_output_type_str(pip.manifest.output_type));
        for (qs_size_t i=0;i<pip.manifest.sources.len;i++)
            fprintf(stderr,"  [%llu] %.*s\n",(unsigned long long)i+1,
                (int)pip.manifest.sources.data[i].len,
                (const char*)pip.manifest.sources.data[i].ptr);
        goto done;
    }
    char *obj_dir=qs_arena_sprintf(arena,"%s/obj",args->out_dir);
    qs_fs_mkdir_p(obj_dir);
    qs_str_vec_t obj_files; qs_str_vec_init(&obj_files,arena);
    r = dispatch_compile(&pip,src_dir,obj_dir,&obj_files);
    if (qs_diag_has_errors(&pip.diag)) goto done;

    /* 11. Link (C/C++ only; other languages self-link) */
    if ((pip.manifest.language==QS_LANG_C||pip.manifest.language==QS_LANG_CPP)
         && obj_files.len>0 && !args->dry_run) {
        const qs_toolchain_t *tc = (pip.args.toolchain_override && pip.args.toolchain_override[0])
            ? qs_toolchain_by_name(&pip.toolchains, pip.args.toolchain_override)
            : qs_toolchain_for_lang(&pip.toolchains, pip.manifest.language);
        if (tc) {
            char *base=qs_arena_str_to_cstr(arena,pip.manifest.name);
            char *out_bin=qs_arena_sprintf(arena,"%s/%s",args->out_dir,
                qs_linker_output_name(arena,pip.manifest.output_type,base));
            qs_link_config_t lc; memset(&lc,0,sizeof(lc));
            lc.object_files=obj_files;
            lc.lib_dirs    =pip.manifest.lib_dirs;
            lc.link_libs   =pip.manifest.link_libs;
            lc.extra_flags =pip.manifest.extra_flags;
            lc.output_type =pip.manifest.output_type;
            lc.output_path =out_bin;
            lc.enable_lto  =pip.manifest.enable_lto;
            r=qs_linker_link(arena,&lc,tc,&pip.diag);
            if (r==QS_OK)
                fprintf(stderr,"\n  \033[1;32m✓\033[0m  %s\n",out_bin);
        }
    }

    /* 12. Cache stats */
    if (args->verbose) qs_cache_print_stats(&pip.cache);

done:
    /* 13. Emit diagnostics */
    if (pip.diag.json_mode) qs_diag_print_json(&pip.diag);
    else                     qs_diag_print_all(&pip.diag);
    qs_diag_print_summary(&pip.diag);

    qs_u64 elapsed=(time_us_now()-t_start);
    fprintf(stderr,"  time: %llu.%03llus\n",
        elapsed/1000000ULL,(elapsed%1000000ULL)/1000ULL);

    return qs_diag_has_errors(&pip.diag)?QS_ERROR_COMPILE:QS_OK;
}
