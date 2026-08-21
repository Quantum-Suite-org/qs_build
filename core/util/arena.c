/*
 * arena.c — Bump-pointer arena allocator.
 * Zero external deps. malloc() used only for the block header itself.
 * Everything else in qs_build allocates from arenas.
 */
#include "qs_arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

static qs_u8 *block_data(qs_arena_block_t *b) { return (qs_u8*)b + sizeof(*b); }

static qs_arena_block_t *block_new(qs_size_t min_bytes) {
    qs_size_t cap   = min_bytes > QS_ARENA_BLOCK_SIZE ? min_bytes : QS_ARENA_BLOCK_SIZE;
    qs_size_t total = sizeof(qs_arena_block_t) + cap;
    qs_arena_block_t *b = (qs_arena_block_t*)malloc((size_t)total);
    if (!b) return NULL;
    b->next = NULL; b->cap = cap; b->used = 0;
    return b;
}

static void block_free_chain(qs_arena_block_t *b) {
    while (b) { qs_arena_block_t *n = b->next; free(b); b = n; }
}

static qs_size_t align_up(qs_size_t v, qs_size_t a) {
    assert(a && !(a & (a-1)));
    return (v + a - 1) & ~(a - 1);
}

void qs_arena_init(qs_arena_t *a) {
    a->first = a->current = NULL;
    a->total_allocated = a->total_wasted = a->block_count = 0;
}

void qs_arena_destroy(qs_arena_t *a) {
    block_free_chain(a->first);
    qs_arena_init(a);
}

void *qs_arena_alloc(qs_arena_t *a, qs_size_t size, qs_size_t align) {
    static qs_u8 zero_sentinel;
    if (!align || align > QS_ARENA_MAX_ALIGN) align = QS_ARENA_MAX_ALIGN;
    if (!size) return &zero_sentinel;

    if (a->current) {
        qs_size_t al = align_up(a->current->used, align);
        qs_size_t waste = al - a->current->used;
        if (al + size <= a->current->cap) {
            a->current->used = al + size;
            a->total_allocated += size;
            a->total_wasted    += waste;
            return block_data(a->current) + al;
        }
    }
    qs_arena_block_t *nb = block_new(size + align);
    if (!nb) return NULL;
    if (a->current) a->current->next = nb; else a->first = nb;
    a->current = nb; a->block_count++;
    qs_size_t al = align_up(0, align);
    nb->used = al + size;
    a->total_allocated += size; a->total_wasted += al;
    return block_data(nb) + al;
}

void *qs_arena_calloc(qs_arena_t *a, qs_size_t size, qs_size_t align) {
    void *p = qs_arena_alloc(a, size, align);
    if (p && size) memset(p, 0, (size_t)size);
    return p;
}

void *qs_arena_realloc(qs_arena_t *a, void *ptr, qs_size_t old_sz, qs_size_t new_sz, qs_size_t align) {
    if (!ptr || !old_sz) return qs_arena_alloc(a, new_sz, align);
    if (new_sz <= old_sz) return ptr;
    if (a->current) {
        qs_u8 *end = block_data(a->current) + a->current->used;
        if ((qs_u8*)ptr + old_sz == end) {
            qs_size_t extra = new_sz - old_sz;
            if (a->current->used + extra <= a->current->cap) {
                a->current->used += extra; a->total_allocated += extra; return ptr;
            }
        }
    }
    void *np = qs_arena_alloc(a, new_sz, align);
    if (np) memcpy(np, ptr, (size_t)old_sz);
    return np;
}

void *qs_arena_memdup(qs_arena_t *a, const void *src, qs_size_t len) {
    if (!src || !len) return NULL;
    void *d = qs_arena_alloc(a, len, 1);
    if (d) memcpy(d, src, (size_t)len);
    return d;
}

char *qs_arena_strdup(qs_arena_t *a, const char *s) {
    if (!s) return NULL;
    qs_size_t len = (qs_size_t)strlen(s);
    char *d = (char*)qs_arena_alloc(a, len+1, 1);
    if (d) { memcpy(d, s, (size_t)len); d[len] = '\0'; }
    return d;
}

char *qs_arena_str_to_cstr(qs_arena_t *a, qs_str_t s) {
    char *d = (char*)qs_arena_alloc(a, s.len+1, 1);
    if (!d) return NULL;
    if (s.len) memcpy(d, s.ptr, (size_t)s.len);
    d[s.len] = '\0';
    return d;
}

char *qs_arena_sprintf(qs_arena_t *a, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    if (n < 0) return NULL;
    char *buf = (char*)qs_arena_alloc(a, (qs_size_t)(n+1), 1);
    if (!buf) return NULL;
    va_start(ap, fmt); vsnprintf(buf, (size_t)(n+1), fmt, ap); va_end(ap);
    return buf;
}

qs_arena_mark_t qs_arena_save(const qs_arena_t *a) {
    return (qs_arena_mark_t){ a->current, a->current ? a->current->used : 0 };
}

void qs_arena_restore(qs_arena_t *a, qs_arena_mark_t m) {
    if (!m.block) { block_free_chain(a->first); a->first=a->current=NULL; a->block_count=0; return; }
    if (m.block->next) { block_free_chain(m.block->next); m.block->next=NULL; }
    qs_size_t c=0; for(qs_arena_block_t *b=a->first; b; b=b->next) c++;
    a->block_count=c; m.block->used=m.used; a->current=m.block;
}

void qs_arena_print_stats(const qs_arena_t *a, const char *label) {
    fprintf(stderr,"[arena:%s] blocks=%llu allocated=%llu wasted=%llu (%.1f%%)\n",
        label?label:"?",
        (unsigned long long)a->block_count,
        (unsigned long long)a->total_allocated,
        (unsigned long long)a->total_wasted,
        a->total_allocated
          ? (double)a->total_wasted/(double)(a->total_allocated+a->total_wasted)*100.0
          : 0.0);
}
