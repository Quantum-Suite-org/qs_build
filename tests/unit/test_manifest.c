/*
 * tests/unit/test_manifest.c — Unit tests for manifest parser.
 */
#include "../../core/include/qs_manifest.h"
#include "../../core/include/qs_arena.h"
#include "../../core/include/qs_diag.h"
#include "../../core/include/qs_fs.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <stdio.h>
#include <string.h>

static int g_test=0,g_fail=0;
#define OK(cond,desc) do{g_test++;\
    if(cond)printf("ok %d - %s\n",g_test,desc);\
    else{printf("not ok %d - %s\n",g_test,desc);g_fail++;}}while(0)

static char *write_tmp(qs_arena_t *a, const char *content) {
    char *path;
#ifdef _WIN32
    char tmpdir[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, tmpdir);
    if (n == 0) strcpy(tmpdir, "C:\\Temp\\");
    /* strip trailing backslash so sprintf works cleanly */
    if (n > 0 && (tmpdir[n-1] == '\\' || tmpdir[n-1] == '/')) tmpdir[n-1] = '\0';
    path = qs_arena_sprintf(a, "%s\\qs_test_manifest.qs", tmpdir);
#else
    path = qs_arena_strdup(a, "/tmp/qs_test_manifest.qs");
#endif
    qs_fs_write_file(path, content, strlen(content));
    return path;
}

int main(void) {
    printf("TAP version 13\n");
    qs_arena_t a; qs_arena_init(&a);
    qs_diag_engine_t diag; qs_diag_engine_init(&diag,&a);

    /* Basic parse */
    const char *src =
        "module mylib {\n"
        "    name     = \"mylib\";\n"
        "    version  = \"1.2.3\";\n"
        "    language = \"cpp\";\n"
        "    standard = \"c++20\";\n"
        "    sources  = [ \"src/a.cpp\", \"src/b.cpp\" ];\n"
        "    headers  = [ \"include/a.h\" ];\n"
        "    output_type = \"dll\";\n"
        "    enable_lto  = true;\n"
        "    chunk_size  = 25;\n"
        "}\n";
    char *path=write_tmp(&a,src);
    qs_manifest_t m; memset(&m,0,sizeof(m));
    qs_result_t r=qs_manifest_parse(&a,path,&m,&diag);
    OK(r==QS_OK,                    "parse returns QS_OK");
    OK(!qs_diag_has_errors(&diag),  "no errors after parse");
    OK(qs_str_eq_cstr(m.name,"mylib"),  "name=mylib");
    OK(qs_str_eq_cstr(m.version,"1.2.3"),"version=1.2.3");
    OK(m.language==QS_LANG_CPP,     "language=cpp");
    OK(qs_str_eq_cstr(m.standard,"c++20"),"standard=c++20");
    OK(m.sources.len==2,            "sources count=2");
    OK(m.headers.len==1,            "headers count=1");
    OK(m.output_type==QS_OUT_DLL,   "output_type=dll");
    OK(m.enable_lto==QS_TRUE,       "enable_lto=true");
    OK(m.chunk_size==25,            "chunk_size=25");

    /* output_type_parse */
    qs_output_type_t ot;
    OK(qs_output_type_parse("exe",&ot)==QS_OK&&ot==QS_OUT_EXE,  "parse exe");
    OK(qs_output_type_parse("wheel",&ot)==QS_OK&&ot==QS_OUT_WHEEL,"parse wheel");
    OK(qs_output_type_parse("jar",&ot)==QS_OK&&ot==QS_OUT_JAR,  "parse jar");
    OK(qs_output_type_parse("gem",&ot)==QS_OK&&ot==QS_OUT_GEM,  "parse gem");
    OK(qs_output_type_parse("qpkg",&ot)==QS_OK&&ot==QS_OUT_QPKG,"parse qpkg");
    OK(qs_output_type_parse("???",&ot)==QS_ERROR_INVALID_ARG,   "parse unknown → error");

    /* Auto-rules: PCH threshold */
    qs_manifest_t m2; memset(&m2,0,sizeof(m2));
    qs_str_vec_init(&m2.sources,&a);
    m2.language=QS_LANG_CPP;
    /* Add exactly QS_PCH_THRESHOLD sources */
    for(qs_size_t i=0;i<QS_PCH_THRESHOLD;i++) {
        char *s=qs_arena_sprintf(&a,"src/file%llu.cpp",(unsigned long long)i);
        qs_str_vec_push(&m2.sources,qs_str_from_cstr(s));
    }
    qs_manifest_apply_auto_rules(&m2);
    OK(m2.enable_pch==QS_TRUE,"auto_rules: pch enabled at threshold");
    OK(m2.chunk_size==QS_DEFAULT_CHUNK_SIZE,"auto_rules: chunk_size defaulted");

    /* Chunk threshold */
    for(qs_size_t i=QS_PCH_THRESHOLD;i<QS_CHUNK_THRESHOLD;i++) {
        char *s=qs_arena_sprintf(&a,"src/file%llu.cpp",(unsigned long long)i);
        qs_str_vec_push(&m2.sources,qs_str_from_cstr(s));
    }
    qs_manifest_apply_auto_rules(&m2);
    OK(m2.enable_chunks==QS_TRUE,"auto_rules: chunks enabled at threshold");

    qs_arena_destroy(&a);
    printf("1..%d\n",g_test);
    return g_fail>0?1:0;
}
