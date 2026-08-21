/*
 * tests/unit/test_diag.c — Unit tests for diagnostic engine and parsers.
 */
#include "../../core/include/qs_diag.h"
#include "../../core/include/qs_arena.h"
#include <stdio.h>
#include <string.h>

static int g_test=0,g_fail=0;
#define OK(cond,desc) do{g_test++;\
    if(cond)printf("ok %d - %s\n",g_test,desc);\
    else{printf("not ok %d - %s\n",g_test,desc);g_fail++;}}while(0)
#define EQ(a,b,desc) OK((a)==(b),desc)

int main(void) {
    printf("TAP version 13\n");
    qs_arena_t a; qs_arena_init(&a);

    /* Engine init */
    qs_diag_engine_t e; qs_diag_engine_init(&e,&a);
    OK(!qs_diag_has_errors(&e), "fresh engine has no errors");
    EQ(qs_diag_error_count(&e),0ULL,"error count 0");

    /* Emit note */
    qs_diag_emit_simple(&e,QS_SEV_NOTE,"tool","CODE",QS_LOC_UNKNOWN,"a note");
    OK(!qs_diag_has_errors(&e),"note does not set error flag");
    EQ(qs_diag_warning_count(&e),0ULL,"note is not warning");

    /* Emit warning */
    qs_diag_emit_simple(&e,QS_SEV_WARNING,"tool","W001",QS_LOC_UNKNOWN,"a warning");
    EQ(qs_diag_warning_count(&e),1ULL,"warning count=1");
    OK(!qs_diag_has_errors(&e),"warning does not set error flag");

    /* Emit error */
    qs_diag_emit_simple(&e,QS_SEV_ERROR,"clang","E001",QS_LOC_UNKNOWN,"undeclared identifier");
    OK(qs_diag_has_errors(&e),"error sets error flag");
    EQ(qs_diag_error_count(&e),1ULL,"error count=1");

    /* Error limit */
    e.error_limit=2;
    qs_diag_emit_simple(&e,QS_SEV_ERROR,"clang","E002",QS_LOC_UNKNOWN,"second error");
    OK(e.limit_reached,"limit reached after 2 errors");

    /* Clear */
    qs_diag_clear(&e);
    OK(!qs_diag_has_errors(&e),"clear resets errors");
    EQ(qs_diag_error_count(&e),0ULL,"clear resets count");
    OK(!e.limit_reached,"clear resets limit_reached");

    /* clang stderr parser */
    const char *clang_out=
        "src/main.cpp:10:5: error: use of undeclared identifier 'foo' [-Wundeclared]\n"
        "src/main.cpp:11:1: note: did you mean 'foobar'?\n"
        "src/main.cpp:15:3: warning: unused variable 'x' [-Wunused-variable]\n";
    qs_diag_t parsed[32]; qs_size_t n;
    n=qs_diag_parse_clang(&a,clang_out,QS_STR("clang"),parsed,32);
    EQ(n,3ULL,"clang parser: 3 diagnostics");
    OK(parsed[0].sev==QS_SEV_ERROR,  "clang[0] is error");
    OK(parsed[1].sev==QS_SEV_NOTE,   "clang[1] is note");
    OK(parsed[2].sev==QS_SEV_WARNING,"clang[2] is warning");
    OK(parsed[0].loc.line==10,"clang[0] line=10");
    OK(parsed[0].loc.col ==5, "clang[0] col=5");

    /* MSVC stderr parser */
    const char *msvc_out=
        "src\\main.cpp(42,13): error C2065: 'foo': undeclared identifier\n"
        "src\\main.cpp(50,1): warning C4100: 'x': unreferenced formal parameter\n";
    n=qs_diag_parse_msvc(&a,msvc_out,QS_STR("cl"),parsed,32);
    EQ(n,2ULL,"msvc parser: 2 diagnostics");
    OK(parsed[0].sev==QS_SEV_ERROR,  "msvc[0] is error");
    OK(parsed[1].sev==QS_SEV_WARNING,"msvc[1] is warning");
    OK(parsed[0].loc.line==42,"msvc[0] line=42");

    /* Go stderr parser */
    const char *go_out=
        "./main.go:7:2: undefined: fmt.Printlnx\n"
        "# example/cmd\n";
    n=qs_diag_parse_go(&a,go_out,parsed,32);
    OK(n>=1,"go parser: at least 1 diag");
    OK(parsed[0].loc.line==7,"go[0] line=7");

    /* Python stderr parser */
    const char *py_out=
        "Traceback (most recent call last):\n"
        "  File \"main.py\", line 3, in <module>\n"
        "NameError: name 'foo' is not defined\n";
    n=qs_diag_parse_python(&a,py_out,parsed,32);
    OK(n>=1,"python parser: at least 1 diag");
    OK(qs_str_eq_cstr(parsed[0].code,"NameError"),"python[0] code=NameError");

    /* Zig stderr parser (same format as clang) */
    const char *zig_out=
        "src/main.zig:5:3: error: expected ';', found '}'\n";
    n=qs_diag_parse_zig(&a,zig_out,parsed,32);
    EQ(n,1ULL,"zig parser: 1 diag");
    OK(parsed[0].loc.line==5,"zig[0] line=5");

    /* severity strings */
    OK(strcmp(qs_severity_str(QS_SEV_ERROR),"error")==0,   "sev_str error");
    OK(strcmp(qs_severity_str(QS_SEV_WARNING),"warning")==0,"sev_str warning");
    OK(strcmp(qs_severity_str(QS_SEV_NOTE),"note")==0,     "sev_str note");
    OK(strcmp(qs_severity_str(QS_SEV_FATAL),"fatal")==0,   "sev_str fatal");

    qs_arena_destroy(&a);
    printf("1..%d\n",g_test);
    return g_fail>0?1:0;
}
