/* qs_linker.h — unified linker driver for all output types */
#ifndef QS_LINKER_H
#define QS_LINKER_H
#include "qs_types.h"
#include "qs_arena.h"
#include "qs_manifest.h"
#include "qs_toolchain.h"
#include "qs_diag.h"

typedef struct {
    qs_str_vec_t        object_files;
    qs_str_vec_t        lib_dirs;
    qs_str_vec_t        link_libs;
    qs_str_vec_t        extra_flags;
    qs_output_type_t    output_type;
    char               *output_path;
    qs_bool_t           enable_lto;
    qs_bool_t           strip_debug;
    qs_bool_t           whole_archive;
} qs_link_config_t;

qs_result_t qs_linker_link(qs_arena_t *a, const qs_link_config_t *cfg,
                             const qs_toolchain_t *tc, qs_diag_engine_t *diag);

/* Build the linker argv array for inspection / dry-run. */
qs_result_t qs_linker_build_argv(qs_arena_t *a, const qs_link_config_t *cfg,
                                  const qs_toolchain_t *tc,
                                  const char ***argv_out, qs_size_t *argc_out);

/* Determine the output filename for a given type + base name. */
char *qs_linker_output_name(qs_arena_t *a, qs_output_type_t t,
                              const char *base_name);
#endif /* QS_LINKER_H */
