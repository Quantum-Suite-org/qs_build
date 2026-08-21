/* qs_hash.h — FNV-1a + xxHash-64 + streaming hasher (zero deps, C11) */
#ifndef QS_HASH_H
#define QS_HASH_H
#include "qs_types.h"

qs_u64 qs_fnv1a_64(const void *data, qs_size_t len);
qs_u64 qs_fnv1a_str(const char *s);
qs_u64 qs_fnv1a_combine(qs_u64 a, qs_u64 b);
qs_u64 qs_xxhash64(const void *data, qs_size_t len, qs_u64 seed);

typedef struct {
    qs_u64 seed, v1, v2, v3, v4;
    qs_u8  buf[32];
    qs_u64 buf_len, total;
} qs_hasher_t;

void   qs_hasher_init(qs_hasher_t *h, qs_u64 seed);
void   qs_hasher_update(qs_hasher_t *h, const void *data, qs_size_t len);
void   qs_hasher_update_u64(qs_hasher_t *h, qs_u64 v);
void   qs_hasher_update_str(qs_hasher_t *h, const char *s);
qs_u64 qs_hasher_finish(const qs_hasher_t *h);

/* Hash a file by path; returns 0 on error */
qs_u64 qs_hash_file(const char *path);

void   qs_hex_encode64(qs_u64 v, char out[17]);
qs_u64 qs_hex_decode64(const char *s);
#endif /* QS_HASH_H */
