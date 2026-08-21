/*
 * cli/args.c — Command-line argument parsing for qs_build.
 *
 * Usage:
 *   qs_build [options] [build.qs]
 *
 * Options:
 *   --types exe|dll|lib|app|qpkg|wheel|jar|gem|nupkg|gobin|object
 *   --out <dir>             output directory (default: ./out)
 *   --jobs <n>              parallel jobs (default: auto)
 *   --verbose               print every compiler invocation
 *   --no-color              disable ANSI colour output
 *   --dry-run               print what would be done, don't do it
 *   --clean                 remove output directory before build
 *   --publish               upload wheel/jar/gem after successful build
 *   --json-diag             emit diagnostics as JSON to stdout
 *   --show-commands         print every subprocess argv before running
 *   --no-incremental        disable the build cache (full rebuild)
 *   --error-limit <n>       stop after n errors (default: unlimited)
 *   --target <triple>       cross-compile target triple
 *   --sysroot <path>        sysroot for cross-compilation
 *   --toolchain <name>      force a specific compiler (e.g. "clang-17")
 *   --force-pch             enable PCH even below threshold
 *   --no-pch                disable PCH even above threshold
 *   --force-chunks          enable unity build even below threshold
 *   --no-chunks             disable unity build even above threshold
 *   --chunk-size <n>        override chunk size
 *   --version               print qs_build version and exit
 *   --help                  print help and exit
 */
#include "qs_cli.h"
#include "qs_arena.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define QS_BUILD_VERSION_STR "1.0.0-alpha"

static qs_u64 parse_u64_arg(const char *s) {
    qs_u64 v=0; while(*s>='0'&&*s<='9') v=v*10+(qs_u64)(*s++)-'0'; return v;
}

qs_result_t qs_cli_parse(qs_arena_t *a, int argc, char **argv, qs_cli_args_t *out) {
    memset(out, 0, sizeof(*out));
    /* Defaults */
    out->manifest_path  = "build.qs";
    out->out_dir        = "./out";
    out->output_type    = QS_OUT_EXE;
    out->jobs           = 0; /* auto */
    out->color          = QS_TRUE;
    out->incremental    = QS_TRUE;
    out->error_limit    = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        /* Positional: manifest path */
        if (arg[0] != '-') { out->manifest_path = arg; continue; }

#define NEXT_ARG() (i+1<argc ? argv[++i] : (qs_cli_fatal("option %s requires an argument",arg),NULL))
#define FLAG(f) (strcmp(arg,f)==0)

        if      (FLAG("--verbose"))         out->verbose       = QS_TRUE;
        else if (FLAG("--no-color"))        out->color         = QS_FALSE;
        else if (FLAG("--dry-run"))         out->dry_run       = QS_TRUE;
        else if (FLAG("--clean"))           out->clean         = QS_TRUE;
        else if (FLAG("--publish"))         out->publish       = QS_TRUE;
        else if (FLAG("--json-diag"))       out->json_diag     = QS_TRUE;
        else if (FLAG("--show-commands"))   out->show_commands = QS_TRUE;
        else if (FLAG("--no-incremental"))  out->incremental   = QS_FALSE;
        else if (FLAG("--force-pch"))       out->force_pch     = QS_TRUE;
        else if (FLAG("--no-pch"))          out->no_pch        = QS_TRUE;
        else if (FLAG("--force-chunks"))    out->force_chunks  = QS_TRUE;
        else if (FLAG("--no-chunks"))       out->no_chunks     = QS_TRUE;
        else if (FLAG("--version"))         { qs_cli_print_version(); exit(0); }
        else if (FLAG("--help") || FLAG("-h")) { qs_cli_print_help(); exit(0); }
        else if (FLAG("--types") || FLAG("-t")) {
            const char *v = NEXT_ARG();
            if (qs_output_type_parse(v, &out->output_type) != QS_OK)
                qs_cli_fatal("unknown output type: %s\n  valid: exe dll lib app qpkg "
                             "cdylib wheel jar gem nupkg gobin object", v);
        }
        else if (FLAG("--out") || FLAG("-o"))     out->out_dir   = NEXT_ARG();
        else if (FLAG("--jobs") || FLAG("-j"))    out->jobs      = parse_u64_arg(NEXT_ARG());
        else if (FLAG("--error-limit"))           out->error_limit = parse_u64_arg(NEXT_ARG());
        else if (FLAG("--target"))                out->target_triple = NEXT_ARG();
        else if (FLAG("--sysroot"))               out->sysroot   = NEXT_ARG();
        else if (FLAG("--toolchain"))             out->toolchain_override = NEXT_ARG();
        else if (FLAG("--chunk-size"))            out->chunk_size_override = parse_u64_arg(NEXT_ARG());
        else {
            fprintf(stderr, "qs_build: unknown option: %s\n", arg);
            fprintf(stderr, "Run 'qs_build --help' for usage.\n");
            return QS_ERROR_INVALID_ARG;
        }
    }
    return QS_OK;
}

void qs_cli_print_version(void) {
    printf("qs_build %s\n", QS_BUILD_VERSION_STR);
    printf("Part of the Quantum Suite — https://quantum-suite.dev\n");
    printf("Supports: C, C++, C#, Java, Python, Go, Ruby, Zig, Rust\n");
}

void qs_cli_print_help(void) {
    printf("qs_build %s — multi-language build system\n\n", QS_BUILD_VERSION_STR);
    printf("USAGE:\n");
    printf("  qs_build [options] [build.qs]\n\n");
    printf("OUTPUT TYPES (--types):\n");
    printf("  exe          Native executable (default)\n");
    printf("  dll          Shared library (.dll/.so/.dylib)\n");
    printf("  lib          Static library (.lib/.a)\n");
    printf("  app          Application bundle (.app/.qapp)\n");
    printf("  qpkg         Quantum package (.qpkg)\n");
    printf("  cdylib       C-ABI shared library\n");
    printf("  wheel        Python wheel → PyPI\n");
    printf("  jar          Java archive → Maven\n");
    printf("  gem          Ruby gem → RubyGems\n");
    printf("  nupkg        .NET NuGet package\n");
    printf("  gobin        Go binary module\n");
    printf("  object       Raw object file (.o/.obj)\n\n");
    printf("OPTIONS:\n");
    printf("  --out <dir>           Output directory (default: ./out)\n");
    printf("  --jobs <n>            Parallel compilation jobs (0=auto)\n");
    printf("  --types <type>        Output type (see above)\n");
    printf("  --verbose             Print toolchain commands\n");
    printf("  --no-color            Disable ANSI color in diagnostics\n");
    printf("  --dry-run             Print what would run, don't execute\n");
    printf("  --clean               Delete output dir before build\n");
    printf("  --publish             Upload wheel/jar/gem after build\n");
    printf("  --json-diag           Emit diagnostics as JSON to stdout\n");
    printf("  --show-commands       Print every subprocess argv\n");
    printf("  --no-incremental      Full rebuild (ignore cache)\n");
    printf("  --error-limit <n>     Stop after n errors\n");
    printf("  --target <triple>     Cross-compile target triple\n");
    printf("  --sysroot <path>      Sysroot for cross-compilation\n");
    printf("  --toolchain <name>    Force compiler (e.g. clang-17)\n");
    printf("  --force-pch           Enable PCH regardless of file count\n");
    printf("  --no-pch              Disable PCH\n");
    printf("  --force-chunks        Enable unity build regardless of count\n");
    printf("  --no-chunks           Disable unity build\n");
    printf("  --chunk-size <n>      Files per unity chunk (default: 50)\n");
    printf("  --version             Print version\n");
    printf("  --help                Print this help\n\n");
    printf("PCH:      Auto-enabled when a directory has >= 30 source files\n");
    printf("Chunking: Auto-enabled when a module has >= 150 source files\n\n");
    printf("EXAMPLES:\n");
    printf("  qs_build                         # build.qs in current dir\n");
    printf("  qs_build --types dll             # build a shared library\n");
    printf("  qs_build --types wheel --publish # build+upload Python wheel\n");
    printf("  qs_build --jobs 8 --verbose      # 8-core build with commands\n");
    printf("  qs_build --target x86_64-w64-mingw32  # cross to Windows\n");
}

void qs_cli_fatal(const char *fmt, ...) {
    va_list ap; va_start(ap,fmt);
    fprintf(stderr,"qs_build: fatal: ");
    vfprintf(stderr,fmt,ap);
    fprintf(stderr,"\n");
    va_end(ap);
    exit(1);
}
