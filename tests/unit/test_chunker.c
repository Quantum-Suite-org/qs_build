/*
 * tests/unit/test_chunker.c — Unit tests for chunker.
 */
#include "../../core/include/qs_chunker.h"
#include "../../core/include/qs_arena.h"
#include "../../core/include/qs_diag.h"
#include "../../core/include/qs_manifest.h"
#include <stdio.h>
#include <string.h>

static int g_test=0,g_fail=0;
#define OK(cond,desc) do{g_test++;\
    if(cond)printf("ok %d - %s\n",g_test,desc);\
    else{printf("not ok %d - %s\n",g_test,desc);g_fail++;}}while(0)
#define EQ(a,b,desc) OK((qs_u64)(a)==(qs_u64)(b),desc)

static qs_manifest_t make_manifest(qs_arena_t *a, qs_size_t nsrc,
                                    qs_lang_t lang, qs_u64 chunk_size) {
    qs_manifest_t m; memset(&m,0,sizeof(m));
    m.language   = lang;
    m.chunk_size = chunk_size;
    qs_str_vec_init(&m.sources,a);
    for (qs_size_t i=0;i<nsrc;i++) {
        char *s=qs_arena_sprintf(a,"src/file%llu.cpp",(unsigned long long)i);
        qs_str_vec_push(&m.sources,qs_str_from_cstr(s));
    }
    qs_str_vec_init(&m.headers,a);
    qs_str_vec_init(&m.defines,a);
    qs_str_vec_init(&m.include_dirs,a);
    qs_str_vec_init(&m.dependencies,a);
    qs_str_vec_init(&m.lib_dirs,a);
    qs_str_vec_init(&m.link_libs,a);
    qs_str_vec_init(&m.extra_flags,a);
    qs_str_vec_init(&m.modules,a);
    qs_str_vec_init(&m.tests,a);
    m.out_dir=QS_STR("/tmp/qs_chunk_test");
    return m;
}

int main(void) {
    printf("TAP version 13\n");
    qs_arena_t a; qs_arena_init(&a);
    qs_diag_engine_t diag; qs_diag_engine_init(&diag,&a);

    /* should_enable: below threshold */
    qs_manifest_t m_small=make_manifest(&a,10,QS_LANG_CPP,0);
    OK(!qs_chunker_should_enable(&m_small),"should_enable false below threshold");

    /* should_enable: at threshold */
    qs_manifest_t m_at=make_manifest(&a,QS_CHUNK_THRESHOLD,QS_LANG_CPP,0);
    OK(qs_chunker_should_enable(&m_at),"should_enable true at threshold");

    /* should_enable: explicit enable_chunks */
    qs_manifest_t m_force=make_manifest(&a,5,QS_LANG_CPP,0);
    m_force.enable_chunks=QS_TRUE;
    OK(qs_chunker_should_enable(&m_force),"should_enable true when forced");

    /* Plan: 200 sources, chunk_size=50 → 4 chunks */
    qs_manifest_t m200=make_manifest(&a,200,QS_LANG_CPP,50);
    qs_chunk_plan_t plan; memset(&plan,0,sizeof(plan));
    qs_result_t r=qs_chunker_plan(&a,&m200,&plan,&diag);
    OK(r==QS_OK,           "plan 200 sources OK");
    EQ(plan.chunks.len,4,  "200/50 = 4 chunks");
    EQ(plan.chunk_size,50, "chunk_size=50");
    EQ(plan.total_sources,200,"total_sources=200");
    EQ(plan.chunks.data[0].members.len,50,"chunk[0] has 50 members");
    EQ(plan.chunks.data[3].members.len,50,"chunk[3] has 50 members");

    /* Plan: 201 sources, chunk_size=50 → 5 chunks, last has 1 */
    qs_manifest_t m201=make_manifest(&a,201,QS_LANG_CPP,50);
    qs_chunk_plan_t plan2; memset(&plan2,0,sizeof(plan2));
    r=qs_chunker_plan(&a,&m201,&plan2,&diag);
    OK(r==QS_OK,            "plan 201 sources OK");
    EQ(plan2.chunks.len,5,  "201/50 = 5 chunks");
    EQ(plan2.chunks.data[4].members.len,1,"last chunk has 1 member");

    /* Plan: 0 sources → 1 empty chunk */
    qs_manifest_t m0=make_manifest(&a,0,QS_LANG_CPP,50);
    qs_chunk_plan_t plan0; memset(&plan0,0,sizeof(plan0));
    r=qs_chunker_plan(&a,&m0,&plan0,&diag);
    OK(r==QS_OK,           "plan 0 sources OK");
    EQ(plan0.chunks.len,1, "0 sources → 1 empty chunk");

    /* Different languages produce correct extensions */
    qs_manifest_t m_cs=make_manifest(&a,5,QS_LANG_CSHARP,5);
    qs_chunk_plan_t pcs; memset(&pcs,0,sizeof(pcs));
    qs_chunker_plan(&a,&m_cs,&pcs,&diag);
    OK(strstr(pcs.chunks.data[0].generated_path,".cs")!=NULL,
        "csharp chunk has .cs extension");

    qs_manifest_t m_zig=make_manifest(&a,5,QS_LANG_ZIG,5);
    qs_chunk_plan_t pzig; memset(&pzig,0,sizeof(pzig));
    qs_chunker_plan(&a,&m_zig,&pzig,&diag);
    OK(strstr(pzig.chunks.data[0].generated_path,".zig")!=NULL,
        "zig chunk has .zig extension");

    qs_arena_destroy(&a);
    printf("1..%d\n",g_test);
    return g_fail>0?1:0;
}
