/* qs_str.h — string slice utilities and intern table (zero deps, C11) */
#ifndef QS_STR_H
#define QS_STR_H
#include "qs_types.h"
#include "qs_arena.h"

qs_bool_t qs_str_eq(qs_str_t a, qs_str_t b);
qs_bool_t qs_str_eq_cstr(qs_str_t s, const char *c);
qs_bool_t qs_str_starts_with(qs_str_t s, qs_str_t prefix);
qs_bool_t qs_str_ends_with(qs_str_t s, qs_str_t suffix);
qs_bool_t qs_str_ends_with_cstr(qs_str_t s, const char *suffix);
qs_bool_t qs_str_contains(qs_str_t haystack, qs_str_t needle);
qs_str_t  qs_str_slice(qs_str_t s, qs_size_t start, qs_size_t end);
qs_str_t  qs_str_trim(qs_str_t s);
qs_str_t  qs_str_from_cstr(const char *s);
char     *qs_str_cstr(qs_arena_t *a, qs_str_t s);
qs_str_t  qs_str_concat(qs_arena_t *a, qs_str_t l, qs_str_t r);
qs_str_t  qs_str_join(qs_arena_t *a, const qs_str_t *parts, qs_size_t n, qs_str_t sep);
QS_PRINTF(2,3) qs_str_t qs_str_fmt(qs_arena_t *a, const char *fmt, ...);
void      qs_str_split_first(qs_str_t s, qs_u8 delim, qs_str_t *l, qs_str_t *r);
void      qs_str_split_last(qs_str_t s, qs_u8 delim, qs_str_t *l, qs_str_t *r);

/* Intern table */
#define QS_INTERN_BUCKETS 8192
typedef struct qs_intern_entry { qs_str_t str; qs_u64 hash; struct qs_intern_entry *next; } qs_intern_entry_t;
typedef struct { qs_arena_t *arena; qs_intern_entry_t *buckets[QS_INTERN_BUCKETS]; qs_size_t count; qs_size_t total_bytes; } qs_intern_table_t;

void     qs_intern_init(qs_intern_table_t *t, qs_arena_t *arena);
qs_str_t qs_intern(qs_intern_table_t *t, const qs_u8 *data, qs_size_t len);
qs_str_t qs_intern_cstr(qs_intern_table_t *t, const char *s);
qs_str_t qs_intern_str(qs_intern_table_t *t, qs_str_t s);
#endif /* QS_STR_H */
