/*
 * benchmarks/bench_arena.c — Arena allocator benchmark.
 * Measures allocation throughput vs malloc() for various allocation patterns.
 * Used to justify arena-over-malloc in qs_build's hot paths.
 */
#include "../core/include/qs_arena.h"
#include "../core/include/qs_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static qs_u64 now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (qs_u64)ts.tv_sec*1000000000ULL+(qs_u64)ts.tv_nsec;
}

static void bench_arena_alloc(qs_size_t alloc_sz, qs_size_t iters) {
    qs_arena_t a; qs_arena_init(&a);
    qs_u64 t0=now_ns();
    volatile qs_u64 sink=0;
    for (qs_size_t i=0;i<iters;i++) {
        void *p=qs_arena_alloc(&a,alloc_sz,8);
        sink^=(qs_u64)(qs_size_t)p;
    }
    qs_u64 t1=now_ns();
    qs_arena_destroy(&a);
    double ns_per=(double)(t1-t0)/(double)iters;
    printf("  arena  %6llu B  %7.1f ns/alloc  (sink=%llx)\n",
        (unsigned long long)alloc_sz,ns_per,(unsigned long long)sink);
}

static void bench_malloc_free(qs_size_t alloc_sz, qs_size_t iters) {
    qs_u64 t0=now_ns();
    volatile qs_u64 sink=0;
    for (qs_size_t i=0;i<iters;i++) {
        void *p=malloc(alloc_sz);
        sink^=(qs_u64)(qs_size_t)p;
        free(p);
    }
    qs_u64 t1=now_ns();
    double ns_per=(double)(t1-t0)/(double)iters;
    printf("  malloc %6llu B  %7.1f ns/alloc  (sink=%llx)\n",
        (unsigned long long)alloc_sz,ns_per,(unsigned long long)sink);
}

int main(void) {
    printf("qs_build arena benchmark\n");
    printf("=========================\n\n");
    static const qs_size_t SIZES[]={8,32,128,512,2048,0};
    for (int si=0;SIZES[si];si++) {
        qs_size_t sz=SIZES[si];
        qs_size_t iters=1000000;
        printf("[%llu byte allocs x %llu]\n",
            (unsigned long long)sz,(unsigned long long)iters);
        bench_arena_alloc(sz,iters);
        bench_malloc_free (sz,iters);
        printf("\n");
    }
    return 0;
}
