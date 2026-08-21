/*
 * tests/unit/test_arena.c — Unit tests for arena allocator.
 */
#include "../../core/include/qs_arena.h"
#include "../../core/include/qs_types.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int g_test=0, g_fail=0;
#define OK(cond,desc) do{ g_test++; \
    if(cond) printf("ok %d - %s\n",g_test,desc); \
    else{printf("not ok %d - %s\n",g_test,desc);g_fail++;}}while(0)

int main(void) {
    printf("TAP version 13\n");

    qs_arena_t a; qs_arena_init(&a);

    /* Basic alloc */
    void *p=qs_arena_alloc(&a,64,8);
    OK(p!=NULL,"alloc 64 bytes returns non-null");
    OK(((qs_u64)p&7)==0,"alloc respects 8-byte alignment");

    /* calloc zeroes memory */
    int *arr=(int*)qs_arena_calloc(&a,sizeof(int)*16,_Alignof(int));
    OK(arr!=NULL,"calloc returns non-null");
    qs_bool_t all_zero=QS_TRUE;
    for(int i=0;i<16;i++) if(arr[i]!=0){all_zero=QS_FALSE;break;}
    OK(all_zero,"calloc memory is zeroed");

    /* strdup */
    char *s=qs_arena_strdup(&a,"hello world");
    OK(s!=NULL,"strdup returns non-null");
    OK(strcmp(s,"hello world")==0,"strdup content correct");

    /* str_to_cstr */
    qs_str_t qs=QS_STR("foobar");
    char *cs=qs_arena_str_to_cstr(&a,qs);
    OK(cs!=NULL,"str_to_cstr non-null");
    OK(strcmp(cs,"foobar")==0,"str_to_cstr content correct");

    /* sprintf */
    char *fmt=qs_arena_sprintf(&a,"chunk_%d",42);
    OK(fmt!=NULL,"sprintf non-null");
    OK(strcmp(fmt,"chunk_42")==0,"sprintf content correct");

    /* zero-size alloc */
    void *z=qs_arena_alloc(&a,0,1);
    OK(z!=NULL,"zero-size alloc returns sentinel (non-null)");

    /* save/restore */
    qs_arena_mark_t mark=qs_arena_save(&a);
    void *before=qs_arena_alloc(&a,1024,1);
    OK(before!=NULL,"alloc after save succeeds");
    qs_arena_restore(&a,mark);
    /* After restore the next allocation should be at (approximately) the
     * same position. We just verify the arena is still usable. */
    void *after=qs_arena_alloc(&a,8,1);
    OK(after!=NULL,"alloc after restore succeeds");

    /* realloc: grow in-place */
    qs_arena_mark_t m2=qs_arena_save(&a);
    char *rb=(char*)qs_arena_alloc(&a,16,1);
    memcpy(rb,"hello",6);
    char *rb2=(char*)qs_arena_realloc(&a,rb,16,64,1);
    OK(rb2!=NULL,"realloc non-null");
    OK(strcmp(rb2,"hello")==0,"realloc content preserved");

    /* Large allocation (forces new block) */
    void *big=qs_arena_alloc(&a,QS_MB(5),1);
    OK(big!=NULL,"5MB alloc succeeds (new block)");
    OK(a.block_count>=2,"new block created for large alloc");

    /* memdup */
    const char src[]="test data 1234";
    void *dup=qs_arena_memdup(&a,src,sizeof(src));
    OK(dup!=NULL,"memdup non-null");
    OK(memcmp(dup,src,sizeof(src))==0,"memdup content matches");

    qs_arena_destroy(&a);
    OK(a.first==NULL,"destroy clears first pointer");
    OK(a.current==NULL,"destroy clears current pointer");

    printf("1..%d\n",g_test);
    return g_fail>0?1:0;
}
