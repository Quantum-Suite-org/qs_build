/*
 * compiler/cpp_driver.c — C++ compilation driver.
 * Extends c_driver with C++20 modules support (.ixx → .pcm), concepts,
 * coroutines flags, and module interface compilation before TU compilation.
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
#include <string.h>
#include <stdio.h>

#define ARGV_MAX 512
static int cpp_ac;
static const char *cpp_av[ARGV_MAX];
#define PUSH(s) do{if(cpp_ac<ARGV_MAX-1){cpp_av[cpp_ac++]=(s);cpp_av[cpp_ac]=NULL;}}while(0)
#define PUSHF(fmt,...) PUSH(qs_arena_sprintf(a,fmt,__VA_ARGS__))

static void push_cpp_flags(qs_arena_t *a, const qs_manifest_t *m,
                             const qs_toolchain_t *tc) {
    if (tc->is_msvc) {
        PUSH("/nologo"); PUSH("/W4"); PUSH("/WX-"); PUSH("/EHsc");
        PUSH("/Zc:preprocessor"); PUSH("/Zc:__cplusplus");
        PUSH("/std:c++20");
        if (m->enable_lto) PUSH("/GL");
        if (tc->supports_modules) PUSH("/experimental:module");
    } else {
        PUSH("-Wall"); PUSH("-Wextra"); PUSH("-Wpedantic");
        PUSHF("-std=%s",QS_STR_IS_EMPTY(m->standard)?"c++20":(const char*)m->standard.ptr);
        if (m->enable_lto) PUSH("-flto");
        if (tc->supports_modules) { PUSH("-fmodules"); PUSH("-fmodules-ts"); }
        PUSH("-fdiagnostics-color=always");
        PUSH("-fdiagnostics-show-option");
        PUSH("-ftemplate-backtrace-limit=0"); /* show full template errors */
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

/* Compile a C++20 module interface (.ixx/.cppm) → .pcm */
qs_result_t qs_cpp_compile_module_iface(qs_arena_t *a, const qs_manifest_t *m,
                                          const qs_toolchain_t *tc,
                                          const char *ixx_path, const char *pcm_path,
                                          qs_diag_engine_t *diag,
                                          qs_bool_t verbose, qs_bool_t dry_run) {
    cpp_ac = 0;
    PUSH(tc->executable);
    push_cpp_flags(a, m, tc);
    if (!tc->is_msvc) {
        PUSH("--precompile");
        PUSH("-o"); PUSH(pcm_path);
        PUSH(ixx_path);
    } else {
        PUSH("/interface"); PUSHF("/Fo%s", pcm_path); PUSH(ixx_path);
    }
    if (verbose) qs_proc_print_argv(cpp_av);
    if (dry_run) return QS_OK;
    qs_proc_result_t pr;
    qs_result_t r = qs_proc_run(a, cpp_av, NULL, 300000000ULL, &pr);
    if (r != QS_OK) return r;
    qs_diag_ingest_tool_output(diag, a,
        qs_str_from_cstr(tc->name), pr.stderr_text, pr.exit_code);
    return pr.exit_code == 0 ? QS_OK : QS_ERROR_COMPILE;
}

/* Compile a C++ TU → object */
qs_result_t qs_cpp_compile_file(qs_arena_t *a, const qs_manifest_t *m,
                                  const qs_toolchain_t *tc, const qs_pch_info_t *pch,
                                  const char *src_path, const char *obj_path,
                                  const char *pcm_dir,
                                  qs_diag_engine_t *diag,
                                  qs_bool_t verbose, qs_bool_t dry_run) {
    cpp_ac = 0;
    PUSH(tc->executable);
    push_cpp_flags(a, m, tc);
    if (pch && pch->valid) {
        if (tc->is_msvc) { PUSHF("/Yu%s",pch->header_path); PUSHF("/Fp%s",pch->pch_path); }
        else { PUSH("-include-pch"); PUSH(pch->pch_path); }
    }
    /* Add precompiled module search path */
    if (pcm_dir && tc->supports_modules && !tc->is_msvc)
        PUSHF("-fprebuilt-module-path=%s", pcm_dir);

    if (tc->is_msvc) { PUSH("/c"); PUSHF("/Fo%s",obj_path); PUSH(src_path); }
    else             { PUSH("-c"); PUSH("-o"); PUSH(obj_path); PUSH(src_path); }

    if (verbose) qs_proc_print_argv(cpp_av);
    if (dry_run) return QS_OK;
    qs_proc_result_t pr;
    qs_result_t r = qs_proc_run(a, cpp_av, NULL, 300000000ULL, &pr);
    if (r != QS_OK) return r;
    qs_diag_ingest_tool_output(diag, a,
        qs_str_from_cstr(tc->name), pr.stderr_text, pr.exit_code);
    return pr.exit_code == 0 ? QS_OK : QS_ERROR_COMPILE;
}

/* Compile all modules first, then all TUs */
qs_result_t qs_cpp_compile_all(qs_arena_t *a, const qs_manifest_t *m,
                                 const qs_toolchain_t *tc, const qs_pch_info_t *pch,
                                 const char *out_dir, qs_diag_engine_t *diag,
                                 qs_bool_t verbose, qs_bool_t dry_run,
                                 qs_str_vec_t *obj_files_out) {
    qs_str_vec_init(obj_files_out, a);
    qs_fs_mkdir_p(out_dir);
    char *pcm_dir = qs_arena_sprintf(a, "%s/pcm", out_dir);
    qs_fs_mkdir_p(pcm_dir);

    /* Phase 1: compile module interfaces */
    for (qs_size_t i = 0; i < m->modules.len; i++) {
        char *ixx = qs_arena_str_to_cstr(a, m->modules.data[i]);
        qs_str_t stem = qs_path_stem(qs_path_basename(m->modules.data[i]));
        char *pcm = qs_arena_sprintf(a, "%s/%.*s.pcm",
            pcm_dir, (int)stem.len, (const char*)stem.ptr);
        qs_result_t r = qs_cpp_compile_module_iface(a, m, tc, ixx, pcm, diag, verbose, dry_run);
        if (r != QS_OK && qs_diag_has_errors(diag)) return r;
    }

    /* Phase 2: compile TUs */
    for (qs_size_t i = 0; i < m->sources.len; i++) {
        char *src = qs_arena_str_to_cstr(a, m->sources.data[i]);
        qs_str_t stem = qs_path_stem(qs_path_basename(m->sources.data[i]));
        char *obj = qs_arena_sprintf(a, "%s/%.*s_%llu.o",
            out_dir, (int)stem.len, (const char*)stem.ptr, (unsigned long long)i);
        qs_result_t r = qs_cpp_compile_file(a,m,tc,pch,src,obj,pcm_dir,diag,verbose,dry_run);
        if (r != QS_OK && qs_diag_has_errors(diag)) {
            if (diag->limit_reached) return r;
            continue;
        }
        qs_str_vec_push(obj_files_out, qs_str_from_cstr(obj));
    }
    return QS_OK;
}
