/*
 * tests/unit/test_str.c — Unit tests for str.c utilities.
 */
#include "../../core/include/qs_str.h"
#include "../../core/include/qs_arena.h"
#include "../../core/include/qs_types.h"
#include <stdio.h>
#include <string.h>

static int g_test=0, g_fail=0;
#define OK(cond,desc) do{g_test++;\
    if(cond)printf("ok %d - %s\n",g_test,desc);\
    else{printf("not ok %d - %s\n",g_test,desc);g_fail++;}}while(0)

int main(void) {
    printf("TAP version 13\n");
    qs_arena_t a; qs_arena_init(&a);

    /* Basic equality */
    qs_str_t h=QS_STR("hello"), w=QS_STR("world"), h2=QS_STR("hello");
    OK( qs_str_eq(h,h2),      "eq: hello==hello");
    OK(!qs_str_eq(h,w),       "eq: hello!=world");
    OK( qs_str_eq_cstr(h,"hello"), "eq_cstr: hello");
    OK(!qs_str_eq_cstr(h,"hell"),  "eq_cstr: hello!=hell");

    /* starts_with / ends_with */
    qs_str_t path=QS_STR("/home/user/src/main.cpp");
    OK( qs_str_starts_with(path,QS_STR("/home")),    "starts_with /home");
    OK(!qs_str_starts_with(path,QS_STR("/usr")),     "!starts_with /usr");
    OK( qs_str_ends_with_cstr(path,".cpp"),           "ends_with .cpp");
    OK(!qs_str_ends_with_cstr(path,".c"),             "!ends_with .c");

    /* contains */
    OK( qs_str_contains(path,QS_STR("src")),  "contains src");
    OK(!qs_str_contains(path,QS_STR("bin")),  "!contains bin");
    OK( qs_str_contains(path,QS_STR("")),     "contains empty always true");

    /* slice */
    qs_str_t sl=qs_str_slice(h,1,4);
    OK(qs_str_eq_cstr(sl,"ell"),"slice [1,4] = ell");
    qs_str_t sl2=qs_str_slice(h,0,5);
    OK(qs_str_eq(sl2,h),"slice [0,5] = full");

    /* trim */
    qs_str_t spaced=QS_STR("  hello  ");
    qs_str_t trimmed=qs_str_trim(spaced);
    OK(qs_str_eq_cstr(trimmed,"hello"),"trim removes surrounding spaces");
    OK(qs_str_eq(qs_str_trim(h),h),"trim no-op on clean string");

    /* from_cstr */
    qs_str_t fc=qs_str_from_cstr("quantum");
    OK(fc.len==7,"from_cstr length correct");
    OK(qs_str_eq_cstr(fc,"quantum"),"from_cstr content correct");

    /* concat */
    qs_str_t cat=qs_str_concat(&a,QS_STR("foo"),QS_STR("bar"));
    OK(qs_str_eq_cstr(cat,"foobar"),"concat foobar");

    /* join */
    qs_str_t parts[]={QS_STR("a"),QS_STR("b"),QS_STR("c")};
    qs_str_t joined=qs_str_join(&a,parts,3,QS_STR(","));
    OK(qs_str_eq_cstr(joined,"a,b,c"),"join with comma");

    /* fmt */
    qs_str_t fmts=qs_str_fmt(&a,"chunk_%03llu",(unsigned long long)7);
    OK(qs_str_eq_cstr(fmts,"chunk_007"),"fmt chunk_007");

    /* split_first */
    qs_str_t kv=QS_STR("key=value=extra");
    qs_str_t lft,rgt;
    qs_str_split_first(kv,'=',&lft,&rgt);
    OK(qs_str_eq_cstr(lft,"key"),         "split_first left=key");
    OK(qs_str_eq_cstr(rgt,"value=extra"), "split_first right=value=extra");

    /* split_last */
    qs_str_split_last(kv,'=',&lft,&rgt);
    OK(qs_str_eq_cstr(lft,"key=value"),"split_last left=key=value");
    OK(qs_str_eq_cstr(rgt,"extra"),    "split_last right=extra");

    /* split: no delimiter found */
    qs_str_split_first(h,'=',&lft,&rgt);
    OK(qs_str_eq(lft,h),              "split_first no delim: left=original");
    OK(QS_STR_IS_EMPTY(rgt),           "split_first no delim: right=empty");

    /* intern table */
    qs_intern_table_t tbl; qs_intern_init(&tbl,&a);
    qs_str_t ia=qs_intern_cstr(&tbl,"hello");
    qs_str_t ib=qs_intern_cstr(&tbl,"hello");
    qs_str_t ic=qs_intern_cstr(&tbl,"world");
    OK(ia.ptr==ib.ptr,     "intern: same string → same pointer");
    OK(ia.ptr!=ic.ptr,     "intern: different string → different pointer");
    OK(tbl.count==2,       "intern: count=2 after hello+world");
    qs_str_t id=qs_intern_str(&tbl,QS_STR("hello"));
    OK(id.ptr==ia.ptr,     "intern_str: matches intern_cstr");

    qs_arena_destroy(&a);
    printf("1..%d\n",g_test);
    return g_fail>0?1:0;
}
