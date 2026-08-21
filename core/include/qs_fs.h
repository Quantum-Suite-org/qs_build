/* qs_fs.h — filesystem helpers (zero deps, POSIX/Win32, C11) */
#ifndef QS_FS_H
#define QS_FS_H
#include "qs_types.h"
#include "qs_arena.h"
#include "qs_vec.h"
#include "qs_str.h"

typedef struct {
    qs_u64    size;
    qs_u64    mtime_us;   /* modification time in microseconds since epoch */
    qs_bool_t is_dir;
    qs_bool_t exists;
} qs_stat_t;

qs_result_t qs_fs_stat(const char *path, qs_stat_t *out);
qs_result_t qs_fs_mkdir_p(const char *path);
qs_result_t qs_fs_read_file(qs_arena_t *a, const char *path, char **out, qs_size_t *out_len);
qs_result_t qs_fs_write_file(const char *path, const char *data, qs_size_t len);
qs_result_t qs_fs_append_file(const char *path, const char *data, qs_size_t len);
qs_result_t qs_fs_copy_file(const char *src, const char *dst);
qs_result_t qs_fs_remove(const char *path);
qs_result_t qs_fs_remove_dir_recursive(const char *path);

/* List all files in a directory (non-recursive). Returns arena-allocated qs_str_vec_t. */
qs_result_t qs_fs_list_dir(qs_arena_t *a, const char *path, qs_str_vec_t *out);
/* Recursive scan: collect all files matching extensions (NULL-terminated). */
qs_result_t qs_fs_scan_sources(qs_arena_t *a, const char *dir,
                                const char *const *exts,
                                qs_str_vec_t *out);

qs_bool_t qs_fs_exists(const char *path);
qs_bool_t qs_fs_is_dir(const char *path);

/* Return the directory of the current executable. */
char *qs_fs_exe_dir(qs_arena_t *a);
/* Temp directory for scratch files. */
char *qs_fs_tmp_dir(qs_arena_t *a);
#endif /* QS_FS_H */
