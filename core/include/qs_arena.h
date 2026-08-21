/* qs_arena.h — bump-pointer arena allocator interface (zero deps, C11) */
#ifndef QS_ARENA_H
#define QS_ARENA_H
#include "qs_types.h"

#define QS_ARENA_BLOCK_SIZE QS_MB(4)
#define QS_ARENA_MAX_ALIGN  16

typedef struct qs_arena_block qs_arena_block_t;
struct qs_arena_block { qs_arena_block_t *next; qs_size_t cap; qs_size_t used; };

typedef struct {
    qs_arena_block_t *first;
    qs_arena_block_t *current;
    qs_size_t total_allocated;
    qs_size_t total_wasted;
    qs_size_t block_count;
} qs_arena_t;

typedef struct { qs_arena_block_t *block; qs_size_t used; } qs_arena_mark_t;

void    qs_arena_init(qs_arena_t *a);
void    qs_arena_destroy(qs_arena_t *a);
QS_NODISCARD void  *qs_arena_alloc(qs_arena_t *a, qs_size_t size, qs_size_t align);
QS_NODISCARD void  *qs_arena_calloc(qs_arena_t *a, qs_size_t size, qs_size_t align);
QS_NODISCARD void  *qs_arena_realloc(qs_arena_t *a, void *p, qs_size_t old_sz, qs_size_t new_sz, qs_size_t align);
QS_NODISCARD void  *qs_arena_memdup(qs_arena_t *a, const void *src, qs_size_t len);
QS_NODISCARD char  *qs_arena_strdup(qs_arena_t *a, const char *s);
QS_NODISCARD char  *qs_arena_str_to_cstr(qs_arena_t *a, qs_str_t s);
QS_NODISCARD QS_PRINTF(2,3) char *qs_arena_sprintf(qs_arena_t *a, const char *fmt, ...);
qs_arena_mark_t qs_arena_save(const qs_arena_t *a);
void    qs_arena_restore(qs_arena_t *a, qs_arena_mark_t m);
void    qs_arena_print_stats(const qs_arena_t *a, const char *label);

#define QS_ARENA_NEW(a,T)        ((T*)qs_arena_alloc((a),sizeof(T),_Alignof(T)))
#define QS_ARENA_NEW_Z(a,T)      ((T*)qs_arena_calloc((a),sizeof(T),_Alignof(T)))
#define QS_ARENA_NEW_N(a,T,n)    ((T*)qs_arena_alloc((a),sizeof(T)*(n),_Alignof(T)))
#define QS_ARENA_NEW_NZ(a,T,n)   ((T*)qs_arena_calloc((a),sizeof(T)*(n),_Alignof(T)))
#endif /* QS_ARENA_H */
