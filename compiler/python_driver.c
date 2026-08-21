/*
 * compiler/python_driver.c — Python build driver.
 *
 * Modes:
 *   QS_OUT_WHEEL  → build a PEP 517 wheel via "python -m build --wheel"
 *                   then optionally "pip upload" via twine
 *   QS_OUT_EXE    → compile extension module (.pyd/.so) via python setup
 *   default       → python -m compileall (bytecode only)
 *
 * No dependencies: invokes python/pip/twine as subprocesses.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_toolchain.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_process.h"
#include "../core/include/qs_fs.h"
#include <string.h>
#include <stdio.h>

/* Generate a minimal pyproject.toml if one doesn't exist */
static void ensure_pyproject(qs_arena_t *a, const qs_manifest_t *m,
                               const char *src_dir) {
    char *path = qs_arena_sprintf(a, "%s/pyproject.toml", src_dir);
    if (qs_fs_exists(path)) return;
    FILE *f = fopen(path, "w"); if (!f) return;
    fprintf(f,"[build-system]\n");
    fprintf(f,"requires = [\"setuptools>=68\"]\n");
    fprintf(f,"build-backend = \"setuptools.backends.legacy:build\"\n\n");
    fprintf(f,"[project]\n");
    fprintf(f,"name = \"%.*s\"\n",(int)m->name.len,(const char*)m->name.ptr);
    fprintf(f,"version = \"%.*s\"\n",
        (int)m->version.len, QS_STR_IS_EMPTY(m->version)?(const qs_u8*)"0.1.0":m->version.ptr);
    fprintf(f,"requires-python = \">=%s\"\n",
        QS_STR_IS_EMPTY(m->python_version)?"3.9":(const char*)m->python_version.ptr);
    fclose(f);
}

qs_result_t qs_python_build(qs_arena_t *a, const qs_manifest_t *m,
                              const qs_toolchain_t *tc,
                              const char *src_dir, const char *out_dir,
                              qs_diag_engine_t *diag,
                              qs_bool_t verbose, qs_bool_t dry_run) {
    qs_fs_mkdir_p(out_dir);

    if (m->output_type == QS_OUT_WHEEL) {
        ensure_pyproject(a, m, src_dir);
        const char *argv[] = {
            tc->executable, "-m", "build",
            "--wheel",
            qs_arena_sprintf(a,"--outdir=%s",out_dir),
            src_dir, NULL
        };
        if (verbose) qs_proc_print_argv(argv);
        if (dry_run) return QS_OK;
        qs_proc_result_t pr;
        qs_result_t r = qs_proc_run(a, argv, NULL, 300000000ULL, &pr);
        if (r != QS_OK) return r;
        const char *err = pr.stderr_text[0]?pr.stderr_text:pr.stdout_text;
        qs_diag_ingest_tool_output(diag, a, QS_STR("python"), err, pr.exit_code);
        return pr.exit_code == 0 ? QS_OK : QS_ERROR_COMPILE;
    }

    /* Default: byte-compile all .py files */
    const char *argv[] = {
        tc->executable, "-m", "compileall",
        "-b", "-j", "0", "-q", src_dir, NULL
    };
    if (verbose) qs_proc_print_argv(argv);
    if (dry_run) return QS_OK;
    qs_proc_result_t pr;
    qs_result_t r = qs_proc_run(a, argv, NULL, 120000000ULL, &pr);
    if (r != QS_OK) return r;
    const char *err = pr.stderr_text[0]?pr.stderr_text:pr.stdout_text;
    qs_diag_ingest_tool_output(diag, a, QS_STR("python"), err, pr.exit_code);
    return pr.exit_code == 0 ? QS_OK : QS_ERROR_COMPILE;
}

/* Upload wheel to PyPI using twine */
qs_result_t qs_python_publish(qs_arena_t *a, const qs_manifest_t *m,
                                const qs_toolchain_t *tc,
                                const char *out_dir, qs_diag_engine_t *diag,
                                qs_bool_t verbose) {
    const char *index = QS_STR_IS_EMPTY(m->pypi_index)
        ? "https://upload.pypi.org/legacy/"
        : (const char*)m->pypi_index.ptr;
    char *wheel_glob = qs_arena_sprintf(a, "%s/*.whl", out_dir);
    const char *argv[] = {
        tc->executable, "-m", "twine", "upload",
        "--repository-url", index,
        wheel_glob, NULL
    };
    if (verbose) qs_proc_print_argv(argv);
    qs_proc_result_t pr;
    qs_result_t r = qs_proc_run(a, argv, NULL, 120000000ULL, &pr);
    if (r != QS_OK) return r;
    qs_diag_ingest_tool_output(diag, a, QS_STR("twine"), pr.stderr_text, pr.exit_code);
    return pr.exit_code == 0 ? QS_OK : QS_ERROR_PROCESS;
}
