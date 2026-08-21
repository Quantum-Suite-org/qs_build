/*
 * tests/unit/test_hash.c — Unit tests for hash.c (FNV-1a + xxhash64 + Hasher).
 * TAP output: "ok N - description" / "not ok N - description".
 * Exit 0 = all pass, 1 = any failure.
 */
#include "../../core/include/qs_hash.h"
#include "../../core/include/qs_types.h"
#include <stdio.h>
#include <string.h>

static int g_test=0, g_fail=0;
#define OK(cond,desc) do{ g_test++; \
    if(cond) printf("ok %d - %s\n",g_test,desc); \
    else { printf("not ok %d - %s\n",g_test,desc); g_fail++; } }while(0)
#define EQ64(a,b,desc) OK((a)==(b),desc)

int main(void) {
    printf("TAP version 13\n");

    /* FNV-1a known vectors */
    EQ64(qs_fnv1a_64("",0), 14695981039346656037ULL, "fnv1a empty = basis");
    EQ64(qs_fnv1a_64("foobar",6), 0x85944171f73967e8ULL, "fnv1a foobar known vector");
    OK(qs_fnv1a_64("hello",5) != qs_fnv1a_64("world",5), "fnv1a hello != world");
    EQ64(qs_fnv1a_str(""), 14695981039346656037ULL, "fnv1a_str empty");
    OK(qs_fnv1a_str("abc") == qs_fnv1a_64("abc",3), "fnv1a_str matches _64");

    /* FNV-1a combine is not commutative */
    qs_u64 ca=qs_fnv1a_combine(0xDEAD,0xBEEF);
    qs_u64 cb=qs_fnv1a_combine(0xBEEF,0xDEAD);
    OK(ca!=cb, "fnv1a_combine not commutative");

    /* xxhash64 determinism */
    qs_u64 h1=qs_xxhash64("quantum build system",20,42);
    qs_u64 h2=qs_xxhash64("quantum build system",20,42);
    EQ64(h1,h2,"xxhash64 deterministic");

    /* xxhash64 seed sensitivity */
    qs_u64 s0=qs_xxhash64("test",4,0);
    qs_u64 s1=qs_xxhash64("test",4,1);
    OK(s0!=s1,"xxhash64 different seeds differ");

    /* xxhash64 known empty vector */
    EQ64(qs_xxhash64("",0,0), 0xef46db3751d8e999ULL, "xxhash64 empty seed=0 known");

    /* Streaming hasher matches one-shot */
    const char *data="the quick brown fox jumps over the lazy dog";
    qs_u64 one_shot=qs_xxhash64(data,strlen(data),0);
    qs_hasher_t h; qs_hasher_init(&h,0);
    qs_hasher_update(&h,data,7);
    qs_hasher_update(&h,data+7,strlen(data)-7);
    EQ64(qs_hasher_finish(&h),one_shot,"streaming hasher matches one-shot");

    /* Streaming: chunked arbitrary sizes */
    qs_hasher_init(&h,0);
    for(size_t i=0;i<strlen(data);i++) qs_hasher_update(&h,data+i,1);
    EQ64(qs_hasher_finish(&h),one_shot,"byte-by-byte streaming matches");

    /* Hasher update_u64 */
    qs_hasher_t ha,hb; qs_hasher_init(&ha,0); qs_hasher_init(&hb,0);
    qs_hasher_update_u64(&ha,0xDEADBEEFCAFEBABEULL);
    qs_hasher_update_u64(&hb,0xDEADBEEFCAFEBABEULL);
    EQ64(qs_hasher_finish(&ha),qs_hasher_finish(&hb),"update_u64 deterministic");

    /* Hex encode round-trip */
    char hex[17]; qs_hex_encode64(0xDEADBEEFCAFEBABEULL,hex);
    OK(strcmp(hex,"deadbeefcafebabe")==0,"hex_encode64 deadbeef");
    EQ64(qs_hex_decode64(hex),0xDEADBEEFCAFEBABEULL,"hex round-trip");
    EQ64(qs_hex_decode64("0000000000000000"),0ULL,"hex zero");

    printf("1..%d\n",g_test);
    return g_fail>0?1:0;
}
