/*
 * compiler/java_driver.c — Java compilation driver via javac + jar.
 * Supports exe (via manifest Main-Class), jar output, chunked compilation
 * using @argfiles, and Maven/Gradle publish stubs.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_toolchain.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_process.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_path.h"
#include <string.h>
#include <stdio.h>

#define ARGV_MAX 256
static int jv_ac; static const char *jv_av[ARGV_MAX];
#define PUSH(s) do{if(jv_ac<ARGV_MAX-1){jv_av[jv_ac++]=(s);jv_av[jv_ac]=NULL;}}while(0)
#define PUSHF(fmt,...) PUSH(qs_arena_sprintf(a,fmt,__VA_ARGS__))

/* Write all sources to a response @argfile, return its path */
static char *write_argfile(qs_arena_t *a, const qs_manifest_t *m, const char *out_dir) {
    char *af = qs_arena_sprintf(a, "%s/sources.argfile", out_dir);
    FILE *f = fopen(af, "w"); if (!f) return NULL;
    for (qs_size_t i=0;i<m->sources.len;i++)
        fprintf(f,"%.*s\n",(int)m->sources.data[i].len,(const char*)m->sources.data[i].ptr);
    fclose(f);
    return af;
}

qs_result_t qs_java_compile_all(qs_arena_t *a, const qs_manifest_t *m,
                                  const qs_toolchain_t *tc,
                                  const char *out_dir,
                                  qs_diag_engine_t *diag,
                                  qs_bool_t verbose, qs_bool_t dry_run,
                                  char **classes_dir_out) {
    qs_fs_mkdir_p(out_dir);
    char *classes = qs_arena_sprintf(a, "%s/classes", out_dir);
    qs_fs_mkdir_p(classes);
    *classes_dir_out = classes;

    char *argfile = write_argfile(a, m, out_dir);
    if (!argfile) return QS_ERROR_IO;

    jv_ac = 0;
    PUSH(tc->executable);
    PUSHF("--release=%s",QS_STR_IS_EMPTY(m->java_version)?"21":(const char*)m->java_version.ptr);
    PUSH("-d"); PUSH(classes);
    PUSH("-encoding"); PUSH("UTF-8");
    PUSH("-g");           /* full debug info */
    PUSH("-Xlint:all");   /* all lint warnings */
    PUSH("-Werror");      /* warnings as errors */
    if (!QS_STR_IS_EMPTY(m->standard) && m->enable_modules)
        PUSH("--enable-preview");
    /* classpath from lib_dirs */
    if (m->lib_dirs.len) {
        PUSH("-classpath");
        /* Build colon/semicolon separated classpath */
        char *cp = qs_arena_strdup(a,".");
        for (qs_size_t i=0;i<m->lib_dirs.len;i++) {
            char *next = qs_arena_sprintf(a,"%s%c%.*s",cp,
#ifdef _WIN32 ';' ,
#else ':',
#endif
                (int)m->lib_dirs.data[i].len,(const char*)m->lib_dirs.data[i].ptr);
            cp = next;
        }
        PUSH(cp);
    }
    PUSHF("@%s", argfile);

    if (verbose) qs_proc_print_argv(jv_av);
    if (dry_run) return QS_OK;

    qs_proc_result_t pr;
    qs_result_t r = qs_proc_run(a, jv_av, NULL, 300000000ULL, &pr);
    if (r != QS_OK) return r;
    const char *err = pr.stderr_text[0] ? pr.stderr_text : pr.stdout_text;
    qs_diag_ingest_tool_output(diag, a, QS_STR("javac"), err, pr.exit_code);
    return pr.exit_code == 0 ? QS_OK : QS_ERROR_COMPILE;
}

/* Package classes into a .jar */
qs_result_t qs_java_package_jar(qs_arena_t *a, const qs_manifest_t *m,
                                  const char *classes_dir, const char *out_dir,
                                  qs_diag_engine_t *diag, qs_bool_t verbose) {
    char *jar_path = qs_arena_sprintf(a, "%s/%.*s.jar",
        out_dir,(int)m->name.len,(const char*)m->name.ptr);
    /* Find jar tool next to javac */
    char *jar_tool = qs_proc_find_in_path(a, "jar");
    if (!jar_tool) jar_tool = (char*)"jar";

    const char *argv[] = { jar_tool,"--create",
        qs_arena_sprintf(a,"--file=%s",jar_path),
        "--main-class=Main", /* TODO: configurable */
        "-C", classes_dir, ".",
        NULL };
    if (verbose) qs_proc_print_argv(argv);
    qs_proc_result_t pr;
    qs_result_t r = qs_proc_run(a, argv, NULL, 60000000ULL, &pr);
    if (r != QS_OK) return r;
    qs_diag_ingest_tool_output(diag, a, QS_STR("jar"), pr.stderr_text, pr.exit_code);
    return pr.exit_code == 0 ? QS_OK : QS_ERROR_COMPILE;
}
