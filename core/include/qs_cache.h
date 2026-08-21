/* qs_cache.h — build cache: fingerprint-based incremental compilation */
#ifndef QS_CACHE_H
#define QS_CACHE_H
#include "qs_types.h"
#include "qs_arena.h"

/* Cache lives in <out_dir>/.qs_cache/.
 * Each entry is a text file named <xxhash_hex> containing key=value pairs.
 * On lookup: compute fingerprint of (source paths, flags, toolchain version).
 * On hit:    skip recompile; reuse cached object path.
 * On miss:   compile, store result. */

typedef struct {
    char     *cache_dir;   /* path to .qs_cache directory */
    qs_u64    hits;
    qs_u64    misses;
    qs_u64    evictions;
    qs_u64    max_size_bytes;
} qs_cache_t;

qs_result_t qs_cache_init(qs_arena_t *a, const char *out_dir, qs_cache_t *out);
qs_result_t qs_cache_lookup(qs_cache_t *c, qs_arena_t *a,
                              qs_u64 fingerprint, char **object_path_out);
qs_result_t qs_cache_store(qs_cache_t *c, qs_arena_t *a,
                             qs_u64 fingerprint, const char *object_path);
qs_result_t qs_cache_evict_lru(qs_cache_t *c);
void        qs_cache_print_stats(const qs_cache_t *c);

/* Compute the fingerprint for a set of source files + flags. */
qs_u64 qs_cache_fingerprint(qs_arena_t *a, const char *const *src_paths,
                              qs_size_t src_count, const char *const *flags,
                              qs_size_t flag_count, const char *tool_version);
#endif /* QS_CACHE_H */
