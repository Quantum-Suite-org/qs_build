/* qs_vec.h — generic growable array (type-safe via macros, zero deps, C11) */
#ifndef QS_VEC_H
#define QS_VEC_H
#include "qs_types.h"
#include "qs_arena.h"
#include <string.h>

/* Declare a typed vector: QS_VEC_DECL(int, int_vec) */
#define QS_VEC_DECL(T, Name) \
    typedef struct { T *data; qs_size_t len; qs_size_t cap; qs_arena_t *arena; } Name##_t; \
    static inline void Name##_init(Name##_t *v, qs_arena_t *a) { v->data=NULL; v->len=0; v->cap=0; v->arena=a; } \
    static inline qs_result_t Name##_push(Name##_t *v, T item) { \
        if (v->len >= v->cap) { \
            qs_size_t ncap = v->cap ? v->cap*2 : 8; \
            T *nd = (T*)qs_arena_realloc(v->arena, v->data, sizeof(T)*v->cap, sizeof(T)*ncap, _Alignof(T)); \
            if (!nd) return QS_ERROR_OOM; v->data=nd; v->cap=ncap; \
        } v->data[v->len++]=item; return QS_OK; } \
    static inline T Name##_get(const Name##_t *v, qs_size_t i) { return v->data[i]; } \
    static inline T *Name##_at(Name##_t *v, qs_size_t i) { return &v->data[i]; } \
    static inline void Name##_clear(Name##_t *v) { v->len=0; } \
    static inline qs_bool_t Name##_empty(const Name##_t *v) { return v->len==0 ? QS_TRUE : QS_FALSE; }

/* Str vector used throughout */
QS_VEC_DECL(qs_str_t, qs_str_vec)

#endif /* QS_VEC_H */
