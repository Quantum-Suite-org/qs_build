/* qs_toolchain.h — toolchain probing: find & version every compiler */
#ifndef QS_TOOLCHAIN_H
#define QS_TOOLCHAIN_H
#include "qs_types.h"
#include "qs_arena.h"
#include "qs_path.h"
#include "qs_diag.h"

typedef struct {
    qs_lang_t   lang;
    char       *name;        /* "clang", "cl", "javac", "go", "dotnet", "rustc", "zig", "gem" */
    char       *executable;  /* absolute path */
    char       *version;     /* "17.0.6", "21.0.1" etc */
    char       *triple;      /* target triple for clang/zig; NULL otherwise */
    qs_bool_t   supports_pch;
    qs_bool_t   supports_lto;
    qs_bool_t   supports_modules;    /* C++20 named modules */
    qs_bool_t   supports_unity;      /* can accept -include for PCH */
    qs_bool_t   is_msvc;             /* cl.exe vs clang/gcc */
} qs_toolchain_t;

#define QS_MAX_TOOLCHAINS 64
typedef struct {
    qs_toolchain_t entries[QS_MAX_TOOLCHAINS];
    qs_size_t      count;
} qs_toolchain_set_t;

/* Probe all compilers available on PATH and fill the set. */
qs_result_t qs_toolchain_probe_all(qs_arena_t *a, qs_toolchain_set_t *out,
                                    qs_diag_engine_t *diag);

/* Find the best toolchain for a given language. Returns NULL if none found. */
const qs_toolchain_t *qs_toolchain_for_lang(const qs_toolchain_set_t *set,
                                              qs_lang_t lang);

/* Find a toolchain by name or alias ("msvc","cl","clang-17").
 * Used to apply --toolchain CLI override. Returns NULL if not found. */
const qs_toolchain_t *qs_toolchain_by_name(const qs_toolchain_set_t *set,
                                             const char *name);

/* Individual probers */
qs_result_t qs_toolchain_probe_clang(qs_arena_t *a, qs_toolchain_t *out);
qs_result_t qs_toolchain_probe_msvc(qs_arena_t *a, qs_toolchain_t *out);
qs_result_t qs_toolchain_probe_gcc(qs_arena_t *a, qs_toolchain_t *out);
qs_result_t qs_toolchain_probe_javac(qs_arena_t *a, qs_toolchain_t *out);
qs_result_t qs_toolchain_probe_dotnet(qs_arena_t *a, qs_toolchain_t *out);
qs_result_t qs_toolchain_probe_go(qs_arena_t *a, qs_toolchain_t *out);
qs_result_t qs_toolchain_probe_python(qs_arena_t *a, qs_toolchain_t *out);
qs_result_t qs_toolchain_probe_rustc(qs_arena_t *a, qs_toolchain_t *out);
qs_result_t qs_toolchain_probe_zig(qs_arena_t *a, qs_toolchain_t *out);
qs_result_t qs_toolchain_probe_ruby(qs_arena_t *a, qs_toolchain_t *out);
void        qs_toolchain_print(const qs_toolchain_set_t *set);
#endif /* QS_TOOLCHAIN_H */
