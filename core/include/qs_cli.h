/* qs_cli.h — command-line argument parsing and help text */
#ifndef QS_CLI_H
#define QS_CLI_H
#include "qs_types.h"
#include "qs_arena.h"
#include "qs_manifest.h"

typedef struct {
    const char      *manifest_path;
    const char      *out_dir;
    qs_output_type_t output_type;
    qs_u64           jobs;           /* parallelism; 0 = auto */
    qs_bool_t        verbose;
    qs_bool_t        color;
    qs_bool_t        dry_run;
    qs_bool_t        clean;
    qs_bool_t        publish;
    qs_bool_t        json_diag;
    qs_bool_t        show_commands;
    qs_bool_t        incremental;
    qs_u64           error_limit;    /* 0 = unlimited */
    const char      *target_triple;  /* cross-compile target */
    const char      *sysroot;
    const char      *toolchain_override; /* e.g. "clang-17" */
    /* Explicit overrides */
    qs_bool_t        force_pch;
    qs_bool_t        force_chunks;
    qs_bool_t        no_pch;
    qs_bool_t        no_chunks;
    qs_u64           chunk_size_override; /* 0 = use manifest value */
} qs_cli_args_t;

qs_result_t qs_cli_parse(qs_arena_t *a, int argc, char **argv, qs_cli_args_t *out);
void        qs_cli_print_help(void);
void        qs_cli_print_version(void);
QS_NORETURN void qs_cli_fatal(const char *fmt, ...) QS_PRINTF(1,2);
#endif /* QS_CLI_H */
