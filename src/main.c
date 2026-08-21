/*
 * main.c — qs_build entry point.
 * Parses CLI args, sets up arenas, runs the build pipeline, exits with
 * code 0 on success or 1 on any error.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_cli.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

qs_result_t qs_pipeline_run(qs_arena_t *arena, qs_arena_t *scratch, const qs_cli_args_t *args);

int main(int argc, char **argv) {
    qs_arena_t arena, scratch;
    qs_arena_init(&arena);
    qs_arena_init(&scratch);

    qs_cli_args_t args;
    qs_result_t r = qs_cli_parse(&arena, argc, argv, &args);
    if (r != QS_OK) {
        fprintf(stderr, "qs_build: argument error: %s\n", qs_result_str(r));
        qs_arena_destroy(&scratch);
        qs_arena_destroy(&arena);
        return 1;
    }

    if (!qs_fs_exists(args.manifest_path)) {
        fprintf(stderr, "qs_build: \033[1;31merror:\033[0m manifest not found: %s\n",
                args.manifest_path);
        fprintf(stderr, "  Run 'qs_build --help' for usage.\n");
        qs_arena_destroy(&scratch);
        qs_arena_destroy(&arena);
        return 1;
    }

    r = qs_pipeline_run(&arena, &scratch, &args);

    if (args.verbose) {
        qs_arena_print_stats(&arena,  "session");
        qs_arena_print_stats(&scratch, "scratch");
    }

    qs_arena_destroy(&scratch);
    qs_arena_destroy(&arena);
    return r == QS_OK ? 0 : 1;
}
