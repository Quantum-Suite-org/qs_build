/*
 * compiler/c_driver.c — C compilation driver.
 * Builds argv for clang/gcc/cl.exe, handles PCH flags, invokes subprocess,
 * parses stderr into structured diagnostics.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_toolchain.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_pch.h"
#include "../core/include/qs_process.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_path.h"
#include "../core/include/qs_cache.h"
#include <string.h>
#include <stdio.h>

#define ARGV_MAX 512
static int acount;
static const char *abuf[ARGV_MAX];
#define PUSH(s) do{ if(acount<ARGV_MAX-1){abuf[acount++]=(s);abuf[acount]=NULL;}}while(0)
#define PUSHF(fmt,...) PUSH(qs_arena_sprintf(a,fmt,__VA_ARGS__))

typedef struct {
    const qs_manifest_t  *manifest;
    const qs_toolchain_t *tc;
    const qs_pch_info_t  *pch;       /* NULL if PCH not enabled */
    qs_cache_t           *cache;
    qs_diag_engine_t     *diag;
    qs_bool_t             verbose;
    qs_bool_t             dry_run;
} c_compile_ctx_t;

/* Build shared flags used for all C TUs */
static void push_common_c_flags(qs_arena_t *a, const c_compile_ctx_t *ctx) {
    const qs_manifest_t *m = ctx->manifest;
    const qs_toolchain_t *tc = ctx->tc;
    if (tc->is_msvc) {
        PUSH("/nologo"); PUSH("/W4"); PUSH("/WX-"); PUSH("/TC");
        PUSH("/Zc:preprocessor");
        PUSHF("/std:%s", QS_STR_IS_EMPTY(m->standard)?"clatest":(const char*)m->standard.ptr);
        if (m->enable_lto) PUSH("/GL");
    } else {
        PUSH("-Wall"); PUSH("-Wextra"); PUSH("-Wpedantic");
        PUSH("-fno-exceptions");
        PUSHF("-std=%s", QS_STR_IS_EMPTY(m->standard)?"c17":(const char*)m->standard.ptr);
        if (m->enable_lto) PUSH("-flto");
        PUSH("-fdiagnostics-color=always");
        PUSH("-fdiagnostics-show-option");
        PUSH("-fdiagnostics-column-unit=byte");
    }
    for (qs_size_t i=0;i<m->include_dirs.len;i++)
        PUSHF(tc->is_msvc?"/I%.*s":"-I%.*s",
            (int)m->include_dirs.data[i].len,(const char*)m->include_dirs.data[i].ptr);
    for (qs_size_t i=0;i<m->defines.len;i++)
        PUSHF(tc->is_msvc?"/D%.*s":"-D%.*s",
            (int)m->defines.data[i].len,(const char*)m->defines.data[i].ptr);
    for (qs_size_t i=0;i<m->extra_flags.len;i++)
        PUSH((const char*)m->extra_flags.data[i].ptr);
}

/* Compile a single C source file → object. Returns QS_OK or QS_ERROR_COMPILE. */
qs_result_t qs_c_compile_file(qs_arena_t *a, const c_compile_ctx_t *ctx,
                                const char *src_path, const char *obj_path) {
    acount = 0;
    PUSH(ctx->tc->executable);
    push_common_c_flags(a, ctx);
    /* PCH */
    if (ctx->pch && ctx->pch->valid) {
        if (ctx->tc->is_msvc) {
            PUSHF("/Yu%s", ctx->pch->header_path);
            PUSHF("/Fp%s", ctx->pch->pch_path);
        } else {
            PUSH("-include-pch"); PUSH(ctx->pch->pch_path);
        }
    }
    if (ctx->tc->is_msvc) {
        PUSH("/c"); PUSHF("/Fo%s", obj_path); PUSH(src_path);
    } else {
        PUSH("-c"); PUSH("-o"); PUSH(obj_path); PUSH(src_path);
    }
    if (ctx->verbose) qs_proc_print_argv(abuf);
    if (ctx->dry_run) return QS_OK;

    qs_proc_result_t pr;
    qs_result_t r = qs_proc_run(a, abuf, NULL, 300000000ULL, &pr);
    if (r != QS_OK) return r;
    const char *tool = ctx->tc->is_msvc ? "cl" :
                        strstr(ctx->tc->name,"clang") ? "clang" : "gcc";
    qs_diag_ingest_tool_output(ctx->diag, a,
        qs_str_from_cstr(tool), pr.stderr_text, pr.exit_code);
    return pr.exit_code == 0 ? QS_OK : QS_ERROR_COMPILE;
}

/* Compile all sources in manifest (or chunks if chunking enabled) */
qs_result_t qs_c_compile_all(qs_arena_t *a, const c_compile_ctx_t *ctx,
                               const char *out_dir, qs_str_vec_t *obj_files_out) {
    qs_str_vec_init(obj_files_out, a);
    qs_fs_mkdir_p(out_dir);
    const qs_manifest_t *m = ctx->manifest;

    for (qs_size_t i = 0; i < m->sources.len; i++) {
        char *src = qs_arena_str_to_cstr(a, m->sources.data[i]);
        qs_str_t stem = qs_path_stem(qs_path_basename(m->sources.data[i]));
        /* MSVC requires .obj extension; GCC/Clang accept .o */
        const char *obj_ext = ctx->tc->is_msvc ? ".obj" : ".o";
        char *obj = qs_arena_sprintf(a, "%s/%.*s_%llu%s",
            out_dir, (int)stem.len, (const char*)stem.ptr,
            (unsigned long long)i, obj_ext);

        /* Cache lookup */
        if (ctx->cache) {
            const char *srcs[] = {src, NULL};
            qs_u64 fp = qs_cache_fingerprint(a, srcs, 1, abuf, (qs_size_t)acount,
                                              ctx->tc->version);
            char *cached_obj = NULL;
            if (qs_cache_lookup(ctx->cache, a, fp, &cached_obj) == QS_OK) {
                qs_str_vec_push(obj_files_out, qs_str_from_cstr(cached_obj));
                continue;
            }
        }

        qs_result_t r = qs_c_compile_file(a, ctx, src, obj);
        if (r != QS_OK && qs_diag_has_errors(ctx->diag)) {
            if (ctx->diag->error_limit && !ctx->diag->limit_reached) continue;
            return r;
        }
        qs_str_vec_push(obj_files_out, qs_str_from_cstr(obj));
    }
    return QS_OK;
}
