/*
 * build.c — bootstrapper for qs_build.
 *
 *   cl /nologo /std:c11 /Fe:qs_build_boot.exe src\build.c
 *   .\qs_build_boot.exe
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SOURCES[] = {
    "errors\\errors.c",
    "core\\util\\arena.c",
    "core\\util\\hash.c",
    "core\\util\\str.c",
    "core\\util\\path.c",
    "utilities\\string_utils.c",
    "core\\process\\process.c",
    "core\\platform\\fs.c",
    "core\\diagnostics\\diag_engine.c",
    "core\\diagnostics\\diag_parsers.c",
    "lexer\\manifest_lexer.c",
    "parser\\ast.c",
    "manifest\\parser.c",
    "cli\\args.c",
    "config\\config.c",
    "workspace\\workspace.c",
    "dependency\\dep_graph.c",
    "scanner\\source_scanner.c",
    "headers\\header_scan.c",
    "toolchain\\probe.c",
    "targets\\cross.c",
    "core\\pch\\pch.c",
    "chunking\\chunker.c",
    "unity\\unity_writer.c",
    "cache\\cache.c",
    "incremental\\incremental.c",
    "validation\\validator.c",
    "profiles\\profiles.c",
    "jobs\\pool.c",
    "scheduler\\scheduler.c",
    "compiler\\c_driver.c",
    "compiler\\cpp_driver.c",
    "compiler\\csharp_driver.c",
    "compiler\\java_driver.c",
    "compiler\\python_driver.c",
    "compiler\\go_driver.c",
    "compiler\\ruby_driver.c",
    "compiler\\rust_driver.c",
    "compiler\\zig_driver.c",
    "linker\\linker.c",
    "optimization\\lto.c",
    "packages\\packager.c",
    "serialization\\qpkg_format.c",
    "assets\\asset_pipeline.c",
    "shaders\\shader_compiler.c",
    "logging\\logger.c",
    "monitoring\\monitor.c",
    "telemetry\\telemetry.c",
    "remote\\remote_cache.c",
    "plugins\\plugin_api.c",
    "extensions\\ext_loader.c",
    "scripting\\script_api.c",
    "reflection\\reflect.c",
    "metadata\\build_info.c",
    "generator\\code_gen.c",
    "templates\\gen_module.c",
    "migration\\migrator.c",
    "documentation\\docs_gen.c",
    "testing\\runner.c",
    "pipeline\\pipeline.c",
    "src\\main.c",
    NULL
};

static int run(const char *cmd) {
    printf("\n>>> %s\n", cmd);
    fflush(stdout);
    int rc = system(cmd);
    printf("<<< exit %d\n", rc);
    fflush(stdout);
    return rc;
}

int main(void) {
    int total = 0;
    for (int i = 0; SOURCES[i]; i++) total++;

    printf("=== qs_build bootstrapper ===\n");
    printf("compiler : cl.exe (MSVC)\n");
    printf("sources  : %d files\n\n", total);
    fflush(stdout);

    system("if not exist build_obj mkdir build_obj");

    int  errors  = 0;
    char cmd[4096];
    char objs[65536];
    int  obj_pos = 0;

    for (int i = 0; SOURCES[i]; i++) {
        const char *src = SOURCES[i];
        char obj[256];
        snprintf(obj, sizeof(obj), "build_obj\\%02d.obj", i);
        obj_pos += snprintf(objs + obj_pos, sizeof(objs) - obj_pos, "%s ", obj);

        printf("[%2d/%2d] %s\n", i + 1, total, src);
        fflush(stdout);

        snprintf(cmd, sizeof(cmd),
            "cl /nologo /std:c11 /O2 /W3 /WX- "
            "/D_CRT_SECURE_NO_WARNINGS "
            "/Icore\\include /Isrc "
            "/c /Fo:%s %s",
            obj, src);

        if (run(cmd) != 0) {
            errors++;
            printf("!!! FAILED: %s\n\n", src);
        }
    }

    printf("\n=== compile: %d ok, %d failed (of %d) ===\n\n",
           total - errors, errors, total);

    if (errors) {
        printf("Fix errors above, then re-run .\\qs_build_boot.exe\n");
        return 1;
    }

    FILE *rsp = fopen("build_obj\\link.rsp", "w");
    if (!rsp) { perror("fopen build_obj\\link.rsp"); return 1; }
    fprintf(rsp, "%s\n", objs);
    fclose(rsp);

    printf("=== linking ===\n");
    fflush(stdout);

    snprintf(cmd, sizeof(cmd),
        "cl /nologo /Fe:qs_build.exe "
        "@build_obj\\link.rsp "
        "/link kernel32.lib user32.lib");

    if (run(cmd) != 0) {
        printf("!!! Link FAILED\n");
        return 1;
    }

    printf("\n=== SUCCESS ===\n");
    printf("Run: .\\qs_build.exe --help\n");
    return 0;
}
