/* qs_process.h — subprocess spawn + capture (zero deps, POSIX/Win32, C11) */
#ifndef QS_PROCESS_H
#define QS_PROCESS_H
#include "qs_types.h"
#include "qs_arena.h"

typedef struct {
    qs_i64    exit_code;
    char     *stdout_text;   /* null-terminated, arena-owned */
    char     *stderr_text;   /* null-terminated, arena-owned */
    qs_u64    wall_time_us;
    qs_bool_t timed_out;
} qs_proc_result_t;

/* argv is NULL-terminated array of char*. env is NULL = inherit. */
qs_result_t qs_proc_run(qs_arena_t *a, const char *const *argv,
                         const char *const *env, qs_u64 timeout_us,
                         qs_proc_result_t *out);

/* Print the argv as a shell-like command line to stderr (for --verbose). */
void qs_proc_print_argv(const char *const *argv);

/* Check if an executable exists on PATH; return full path into arena or NULL. */
char *qs_proc_find_in_path(qs_arena_t *a, const char *exe_name);
#endif /* QS_PROCESS_H */
