/*
 * benchmarks/bench_hash.c — Hash function benchmark.
 * Measures throughput of FNV-1a and xxhash64 on various payload sizes.
 * Reports MB/s for each. Used to verify hash performance is acceptable
 * before merging changes to util/hash.c.
 *
 * Build: cc -O3 -o bench_hash benchmarks/bench_hash.c core/util/hash.c
 * Run:   ./bench_hash
 */
#include "../core/include/qs_hash.h"
#include "../core/include/qs_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static qs_u64 now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return (qs_u64)ts.tv_sec*1000000000ULL+(qs_u64)ts.tv_nsec;
}

static void bench_fnv(const qs_u8 *data, qs_size_t len, qs_size_t iters) {
    qs_u64 t0=now_ns();
    volatile qs_u64 sink=0;
    for (qs_size_t i=0;i<iters;i++) sink^=qs_fnv1a_64(data,len);
    qs_u64 t1=now_ns();
    double ns_per=(double)(t1-t0)/(double)iters;
    double mbps=(double)len/ns_per*1000.0;
    printf("  fnv1a  %8llu B  %8.0f ns/call  %6.0f MB/s  (sink=%llx)\n",
        (unsigned long long)len,ns_per,mbps,(unsigned long long)sink);
}

static void bench_xx(const qs_u8 *data, qs_size_t len, qs_size_t iters) {
    qs_u64 t0=now_ns();
    volatile qs_u64 sink=0;
    for (qs_size_t i=0;i<iters;i++) sink^=qs_xxhash64(data,len,0);
    qs_u64 t1=now_ns();
    double ns_per=(double)(t1-t0)/(double)iters;
    double mbps=(double)len/ns_per*1000.0;
    printf("  xxh64  %8llu B  %8.0f ns/call  %6.0f MB/s  (sink=%llx)\n",
        (unsigned long long)len,ns_per,mbps,(unsigned long long)sink);
}

static void bench_hasher(const qs_u8 *data, qs_size_t len, qs_size_t iters,
                           qs_size_t chunk) {
    qs_u64 t0=now_ns();
    volatile qs_u64 sink=0;
    for (qs_size_t i=0;i<iters;i++) {
        qs_hasher_t h; qs_hasher_init(&h,0);
        for (qs_size_t off=0;off<len;off+=chunk)
            qs_hasher_update(&h,data+off,off+chunk<=len?chunk:len-off);
        sink^=qs_hasher_finish(&h);
    }
    qs_u64 t1=now_ns();
    double ns_per=(double)(t1-t0)/(double)iters;
    double mbps=(double)len/ns_per*1000.0;
    printf("  hasher %8llu B  %8.0f ns/call  %6.0f MB/s  chunk=%llu\n",
        (unsigned long long)len,ns_per,mbps,(unsigned long long)chunk);
}

int main(void) {
    printf("qs_build hash benchmark\n");
    printf("========================\n\n");

    static const qs_size_t SIZES[]={8,64,256,1024,4096,65536,1048576,0};
    for (int si=0;SIZES[si];si++) {
        qs_size_t sz=SIZES[si];
        qs_u8 *buf=(qs_u8*)malloc(sz);
        for (qs_size_t i=0;i<sz;i++) buf[i]=(qs_u8)i;
        qs_size_t iters=sz<4096?1000000:sz<65536?100000:10000;
        printf("[%llu bytes x %llu iters]\n",
            (unsigned long long)sz,(unsigned long long)iters);
        bench_fnv(buf,sz,iters);
        bench_xx (buf,sz,iters);
        bench_hasher(buf,sz,sz<4096?iters/10:1000,64);
        free(buf);
        printf("\n");
    }
    printf("bench complete\n");
    return 0;
}
