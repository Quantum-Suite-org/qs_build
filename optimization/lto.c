/*
 * optimization/lto.c — LTO (Link-Time Optimization) helpers.
 * Decides whether to enable LTO per toolchain + output type,
 * and builds the correct LTO-related compiler/linker flags.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_toolchain.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_vec.h"
#include <string.h>
#include <stdio.h>

/* Returns true if LTO is safe for this combination */
qs_bool_t qs_lto_should_enable(const qs_manifest_t *m, const qs_toolchain_t *tc) {
    if (!m->enable_lto) return QS_FALSE;
    /* LTO on shared libs is tricky — only with PIC */
    if (m->output_type==QS_OUT_DLL||m->output_type==QS_OUT_CDYLIB)
        return tc->supports_lto;
    /* Object files don't benefit */
    if (m->output_type==QS_OUT_OBJECT) return QS_FALSE;
    return tc->supports_lto;
}

/* Append LTO compile flags to argv */
void qs_lto_compile_flags(qs_arena_t *a, const qs_toolchain_t *tc, qs_str_vec_t *flags) {
    if (tc->is_msvc) {
        qs_str_vec_push(flags, QS_STR("/GL"));
    } else {
        qs_str_vec_push(flags, QS_STR("-flto"));
        /* Clang ThinLTO is faster for large codebases */
        if (strstr(tc->name,"clang"))
            qs_str_vec_push(flags, QS_STR("-flto=thin"));
    }
}

/* Append LTO link flags to argv */
void qs_lto_link_flags(qs_arena_t *a, const qs_toolchain_t *tc,
                         qs_u64 thread_count, qs_str_vec_t *flags) {
    if (tc->is_msvc) {
        qs_str_vec_push(flags, QS_STR("/LTCG"));
    } else {
        qs_str_vec_push(flags, QS_STR("-flto"));
        if (strstr(tc->name,"clang")) {
            qs_str_vec_push(flags, QS_STR("-flto=thin"));
            if (thread_count>1)
                qs_str_vec_push(flags,
                    qs_str_from_cstr(qs_arena_sprintf(a,
                        "-Wl,--thinlto-jobs=%llu",(unsigned long long)thread_count)));
        } else {
            /* GCC fat LTO */
            if (thread_count>1)
                qs_str_vec_push(flags,
                    qs_str_from_cstr(qs_arena_sprintf(a,
                        "-flto-partition=balanced")));
        }
    }
}
