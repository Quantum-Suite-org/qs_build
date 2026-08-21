/*
 * tests/unit/test_path.c — Unit tests for path.c utilities.
 */
#include "../../core/include/qs_path.h"
#include "../../core/include/qs_arena.h"
#include "../../core/include/qs_types.h"
#include <stdio.h>
#include <string.h>

static int g_test=0,g_fail=0;
#define OK(cond,desc) do{g_test++;\
    if(cond)printf("ok %d - %s\n",g_test,desc);\
    else{printf("not ok %d - %s\n",g_test,desc);g_fail++;}}while(0)
#define EQS(s,cstr,desc) OK(qs_str_eq_cstr((s),(cstr)),desc)

int main(void) {
    printf("TAP version 13\n");
    qs_arena_t a; qs_arena_init(&a);

    /* normalise */
    EQS(qs_path_normalise(&a,QS_STR("foo//bar///baz")),
        "foo/bar/baz","normalise collapses slashes");
    EQS(qs_path_normalise(&a,QS_STR("foo/bar/")),
        "foo/bar","normalise strips trailing slash");
    EQS(qs_path_normalise(&a,QS_STR("/")),
        "/","normalise root stays /");

    /* basename */
    EQS(qs_path_basename(QS_STR("/home/user/main.cpp")),
        "main.cpp","basename of absolute path");
    EQS(qs_path_basename(QS_STR("main.cpp")),
        "main.cpp","basename of filename only");
    EQS(qs_path_basename(QS_STR("/")),
        "","basename of root is empty");

    /* stem */
    EQS(qs_path_stem(QS_STR("/home/user/main.cpp")),
        "main","stem strips extension");
    EQS(qs_path_stem(QS_STR("noext")),
        "noext","stem with no extension");
    EQS(qs_path_stem(QS_STR("a.b.c")),
        "a.b","stem strips last extension only");

    /* ext */
    EQS(qs_path_ext(QS_STR("main.cpp")), ".cpp","ext .cpp");
    EQS(qs_path_ext(QS_STR("main.h")),   ".h",  "ext .h");
    OK(QS_STR_IS_EMPTY(qs_path_ext(QS_STR("Makefile"))), "ext none → empty");

    /* join */
    EQS(qs_path_join(&a,QS_STR("/home/user"),QS_STR("src/main.cpp")),
        "/home/user/src/main.cpp","join absolute+relative");
    EQS(qs_path_join(&a,QS_STR(""),QS_STR("src/main.cpp")),
        "src/main.cpp","join empty dir");
    EQS(qs_path_join(&a,QS_STR("/usr"),QS_STR("/etc/foo")),
        "/etc/foo","join absolute file overrides dir");

    /* replace_ext */
    EQS(qs_path_replace_ext(&a,QS_STR("src/main.cpp"),QS_STR(".o")),
        "src/main.o","replace_ext cpp→o");
    EQS(qs_path_replace_ext(&a,QS_STR("main.c"),QS_STR(".h")),
        "main.h","replace_ext c→h");

    /* is_absolute */
    OK( qs_path_is_absolute(QS_STR("/usr/local")), "is_absolute /usr");
    OK(!qs_path_is_absolute(QS_STR("src/main.c")),"!is_absolute relative");
    OK(!qs_path_is_absolute(QS_STR("")),           "!is_absolute empty");

    /* has_ext */
    const char *cpp_exts[]={"cpp","cxx","cc",NULL};
    OK( qs_path_has_ext(QS_STR("main.cpp"),cpp_exts),"has_ext .cpp in list");
    OK(!qs_path_has_ext(QS_STR("main.c"),  cpp_exts),"!has_ext .c not in cpp list");

    /* detect_lang */
    OK(qs_path_detect_lang(QS_STR("foo.cpp"))==QS_LANG_CPP, "detect .cpp → CPP");
    OK(qs_path_detect_lang(QS_STR("bar.c"))  ==QS_LANG_C,   "detect .c   → C");
    OK(qs_path_detect_lang(QS_STR("Foo.java"))==QS_LANG_JAVA,"detect .java → JAVA");
    OK(qs_path_detect_lang(QS_STR("mod.go")) ==QS_LANG_GO,  "detect .go  → GO");
    OK(qs_path_detect_lang(QS_STR("app.py")) ==QS_LANG_PYTHON,"detect .py → PYTHON");
    OK(qs_path_detect_lang(QS_STR("lib.zig"))==QS_LANG_ZIG, "detect .zig → ZIG");
    OK(qs_path_detect_lang(QS_STR("main.rs"))==QS_LANG_RUST,"detect .rs  → RUST");
    OK(qs_path_detect_lang(QS_STR("app.rb")) ==QS_LANG_RUBY,"detect .rb  → RUBY");
    OK(qs_path_detect_lang(QS_STR("Foo.cs")) ==QS_LANG_CSHARP,"detect .cs→ CSHARP");
    OK(qs_path_detect_lang(QS_STR("Makefile"))==QS_LANG_UNKNOWN,"detect unknown");

    /* lang_name */
    OK(strcmp(qs_lang_name(QS_LANG_CPP),"C++")==0,  "lang_name CPP");
    OK(strcmp(qs_lang_name(QS_LANG_RUST),"Rust")==0, "lang_name RUST");

    qs_arena_destroy(&a);
    printf("1..%d\n",g_test);
    return g_fail>0?1:0;
}
