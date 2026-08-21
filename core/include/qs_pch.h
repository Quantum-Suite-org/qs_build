/* qs_pch.h — precompiled header management for C, C++, C#, Java, Python,
 *             Go, Ruby, Rust and Zig (zero deps, C11)
 *
 * PCH strategy:
 *   - Auto-enabled when a folder has >= QS_PCH_THRESHOLD (30) source files.
 *   - Each language gets its own PCH format. For C/C++ it is a .gch/.pch
 *     file produced by clang/gcc/cl. For Java it is a shared compilation
 *     unit. For .NET it is a NuGet reference cache. For Python it is a
 *     .pyc bytecode cache seed. Go uses build cache. Rust uses sysroot
 *     precompilation. Zig uses the zig cache.
 *
 * All PCH artefacts are stored in <out_dir>/.qs_pch/<lang>/ so they can
 * be cleaned without touching source.
 */
#ifndef QS_PCH_H
#define QS_PCH_H
#include "qs_types.h"
#include "qs_arena.h"
#include "qs_manifest.h"
#include "qs_toolchain.h"
#include "qs_diag.h"

typedef struct {
    qs_lang_t   lang;
    char       *pch_path;    /* path to generated PCH artefact */
    char       *header_path; /* amalgam header / seed file */
    qs_u64      mtime_us;    /* mtime of the PCH artefact */
    qs_bool_t   valid;       /* false = needs rebuild */
} qs_pch_info_t;

/* Decide whether PCH should be used for this manifest (checks threshold). */
qs_bool_t qs_pch_should_enable(const qs_manifest_t *m);

/* Generate the amalgam header from all public headers in the manifest. */
qs_result_t qs_pch_generate_header(qs_arena_t *a, const qs_manifest_t *m,
                                    const char *out_path, qs_diag_engine_t *diag);

/* Build the PCH artefact via the given toolchain. */
qs_result_t qs_pch_build_c(qs_arena_t *a, const qs_manifest_t *m,
                             const qs_toolchain_t *tc, qs_pch_info_t *out,
                             qs_diag_engine_t *diag);
qs_result_t qs_pch_build_cpp(qs_arena_t *a, const qs_manifest_t *m,
                               const qs_toolchain_t *tc, qs_pch_info_t *out,
                               qs_diag_engine_t *diag);
qs_result_t qs_pch_build_csharp(qs_arena_t *a, const qs_manifest_t *m,
                                  const qs_toolchain_t *tc, qs_pch_info_t *out,
                                  qs_diag_engine_t *diag);
qs_result_t qs_pch_build_java(qs_arena_t *a, const qs_manifest_t *m,
                                const qs_toolchain_t *tc, qs_pch_info_t *out,
                                qs_diag_engine_t *diag);
qs_result_t qs_pch_build_python(qs_arena_t *a, const qs_manifest_t *m,
                                  const qs_toolchain_t *tc, qs_pch_info_t *out,
                                  qs_diag_engine_t *diag);
qs_result_t qs_pch_build_go(qs_arena_t *a, const qs_manifest_t *m,
                              const qs_toolchain_t *tc, qs_pch_info_t *out,
                              qs_diag_engine_t *diag);
qs_result_t qs_pch_build_ruby(qs_arena_t *a, const qs_manifest_t *m,
                                const qs_toolchain_t *tc, qs_pch_info_t *out,
                                qs_diag_engine_t *diag);
qs_result_t qs_pch_build_rust(qs_arena_t *a, const qs_manifest_t *m,
                                const qs_toolchain_t *tc, qs_pch_info_t *out,
                                qs_diag_engine_t *diag);
qs_result_t qs_pch_build_zig(qs_arena_t *a, const qs_manifest_t *m,
                               const qs_toolchain_t *tc, qs_pch_info_t *out,
                               qs_diag_engine_t *diag);

/* Check whether the cached PCH is still valid (compare header mtimes). */
qs_bool_t   qs_pch_is_valid(qs_arena_t *a, const qs_pch_info_t *pch,
                              const qs_manifest_t *m);

/* Return the compiler flags to USE an existing PCH artefact. */
qs_result_t qs_pch_use_flags_c(qs_arena_t *a, const qs_pch_info_t *pch,
                                 const qs_toolchain_t *tc,
                                 qs_str_vec_t *flags_out);
qs_result_t qs_pch_use_flags_cpp(qs_arena_t *a, const qs_pch_info_t *pch,
                                   const qs_toolchain_t *tc,
                                   qs_str_vec_t *flags_out);
#endif /* QS_PCH_H */
