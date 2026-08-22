# qs_build

**A zero-dependency, multi-language build system for the Quantum Suite.**

> C11 core · 9 languages · 113 source files · ~10,700 lines of code

---

## Overview

`qs_build` compiles C, C++, C#, Java, Python, Go, Ruby, Rust, and Zig from
a single unified manifest format (`build.qs`). It invokes all language
compilers as subprocesses — no library dependencies, no CMake, no Ninja.

Key features:

| Feature | Detail |
|---|---|
| **Zero deps** | Pure C11 + POSIX (Win32 on Windows) |
| **9 languages** | C · C++ · C# · Java · Python · Go · Ruby · Rust · Zig |
| **PCH** | Auto-enabled at ≥ 30 source files per language |
| **Unity builds** | Auto-chunking at ≥ 150 source files, chunk size 50 |
| **Parallel** | Thread pool, one worker per CPU core |
| **Incremental** | `.d` depfile tracking + flag fingerprints |
| **Remote cache** | HTTP GET/PUT cache, zero deps (raw sockets) |
| **Plugin system** | `dlopen`-based plugins + JSON-RPC subprocess extensions |
| **Diagnostics** | Structured engine + per-language stderr parsers (clang/GCC/MSVC/javac/go/python/rustc/zig/ruby) |
| **Cross-compile** | `--target triple` → GOOS/GOARCH / `--target` / Zig target |
| **Asset pipeline** | Mesh · Texture · Audio · Shader (glslc/dxc) dispatch |
| **Forge:Visuals** | Reflection metadata + doc-comment → JSON block registry |

---

## Quick Start

### Bootstrap

```bash
# Compile the bootstrapper with any C compiler
cc -o qs_build_boot src/build.c

# Build qs_build itself
./qs_build_boot

# You now have a ./qs_build binary
./qs_build --help
```

### Your first build

```bash
# In any directory with a build.qs:
qs_build

# Specify output directory
qs_build --out ./out

# Choose output type
qs_build --types exe
qs_build --types dll
qs_build --types lib

# Cross-compile
qs_build --target aarch64-unknown-linux-gnu

# Dry run (parse + plan, no compile)
qs_build --dry-run

# Scaffold a new module
qs_build new mymodule --lang cpp --types exe
```

---

## build.qs Format

```
module mylib {
    name         = "mylib";
    version      = "1.0.0";
    language     = "cpp";          // c | cpp | csharp | java | python | go | ruby | rust | zig
    standard     = "c++20";        // or c17, net8.0, 21, 3.12, 1.22, 2021 ...
    output_type  = "dll";          // exe | dll | lib | app | qpkg | wheel | jar | gem | nupkg | gobin | cdylib | object

    sources      = [ "src/a.cpp", "src/b.cpp" ];
    headers      = [ "include/api.h" ];
    include_dirs = [ "include", "third_party/include" ];
    dependencies = [ "core", "utils" ];
    tests        = [ "tests/test_main.cpp" ];
    defines      = [ "MYLIB_EXPORTS", "VERSION=2" ];

    enable_lto   = false;
    chunk_size   = 50;             // unity build chunk size (auto at ≥150 sources)
    pch_header   = "include/pch.h"; // explicit PCH (auto at ≥30 sources)

    // Language-specific
    dotnet_sdk   = "net8.0";       // C#
    java_version = "21";           // Java
    python_version = "3.12";       // Python
    go_module    = "github.com/org/mylib"; // Go
    publish      = false;          // wheel/gem/nupkg publish after build
}
```

---

## CLI Reference

```
qs_build [OPTIONS] [manifest]

Arguments:
  manifest               Path to build.qs (default: ./build.qs)

Build options:
  --out DIR              Output directory (default: ./out)
  --types TYPE[,TYPE]    Output types: exe|dll|lib|app|qpkg|wheel|jar|gem|gobin
  --target TRIPLE        Cross-compilation target triple
  --profile PROFILE      Build profile: debug|release|relwithdebinfo|profiling
  --jobs N               Parallel jobs (default: CPU count)
  --chunk-size N         Override unity chunk size
  --error-limit N        Stop after N errors (default: 20)

Feature flags:
  --pch / --no-pch       Force-enable or disable PCH
  --chunks / --no-chunks Force-enable or disable unity chunking
  --lto / --no-lto       Force-enable or disable LTO
  --incremental=0        Disable incremental builds (full rebuild)

Output control:
  --verbose / -v         Show compiler commands and toolchain info
  --dry-run              Parse + plan without compiling
  --json-diag            Emit diagnostics as JSON (for IDE integration)
  --no-color             Disable ANSI colour in output

Other commands:
  --clean                Remove output directory before build
  --test                 Build and run all declared tests
  --publish              Publish after build (wheel/gem/nupkg)
  new NAME               Scaffold a new module directory
  migrate DIR            Migrate CMake/Cargo/go.mod/... to build.qs
  verify FILE.qpkg       Verify a .qpkg archive
  --help / -h            Show this help
  --version              Print qs_build version
```

---

## Project Structure

```
qs_build/
├── src/               main.c (entry) · build.c (bootstrapper)
├── core/              Zero-dep C library used by all other modules
│   ├── include/       qs_types.h · qs_arena.h · qs_hash.h · qs_str.h
│   │                  qs_path.h · qs_vec.h · qs_manifest.h · qs_diag.h
│   │                  qs_toolchain.h · qs_pch.h · qs_chunker.h · qs_cache.h
│   │                  qs_jobs.h · qs_linker.h · qs_cli.h · qs_process.h · qs_fs.h
│   ├── util/          arena.c · hash.c · str.c · path.c
│   ├── process/       process.c (subprocess spawn + capture)
│   ├── platform/      fs.c (POSIX + Win32 filesystem)
│   ├── diagnostics/   diag_engine.c · diag_parsers.c
│   └── pch/           pch.c (PCH strategy for all 9 languages)
├── cli/               args.c
├── config/            config.c (.qsbuildrc + env + CLI merge)
├── workspace/         workspace.c (multi-module workspace)
├── manifest/          parser.c (build.qs parser)
├── lexer/             manifest_lexer.c
├── parser/            ast.c
├── dependency/        dep_graph.c (topological sort, cycle detection)
├── scanner/           source_scanner.c (recursive source discovery)
├── toolchain/         probe.c (compiler detection)
├── compiler/          c · cpp · csharp · java · python · go · ruby · rust · zig drivers
├── linker/            linker.c (ar · link.exe · clang/gcc)
├── chunking/          chunker.c (unity build planner)
├── unity/             unity_writer.c (smart grouping + NO_UNITY opt-out)
├── headers/           header_scan.c (#include frequency analysis)
├── pch/               (see core/pch/)
├── cache/             cache.c (fingerprint-based incremental cache)
├── incremental/       incremental.c (.d depfile tracker)
├── scheduler/         scheduler.c (dependency-aware DAG scheduler)
├── jobs/              pool.c (POSIX pthreads / Win32 thread pool)
├── pipeline/          pipeline.c (top-level orchestrator)
├── optimization/      lto.c
├── profiles/          profiles.c (debug · release · profiling)
├── validation/        validator.c (pre-compile manifest checks)
├── targets/           cross.c (target triple parser + env mapping)
├── packages/          packager.c (.qpkg v1)
├── serialization/     qpkg_format.c (.qpkg v2 with index + checksums)
├── logging/           logger.c
├── monitoring/        monitor.c (phase timing + JSON telemetry)
├── telemetry/         telemetry.c (optional anonymous HTTP telemetry)
├── remote/            remote_cache.c (HTTP GET/PUT object cache)
├── plugins/           plugin_api.c (dlopen plugin loader)
├── extensions/        ext_loader.c (JSON-RPC subprocess extensions)
├── scripting/         script_api.c (pre/post build hooks)
├── reflection/        reflect.c (QS_REFLECT → JSON block registry)
├── metadata/          build_info.c (git hash · date · host embedding)
├── generator/         code_gen.c (Zig bridge · compile_commands.json)
├── templates/         gen_module.c (qs_build new scaffold)
├── migration/         migrator.c (CMake · Cargo · go.mod → build.qs)
├── documentation/     docs_gen.c (QS_DOC → Markdown + JSON)
├── assets/            asset_pipeline.c (mesh · tex · audio · shader)
├── shaders/           shader_compiler.c (glslc · dxc · metal dispatch)
├── utilities/         string_utils.c (word-wrap · glob · Levenshtein)
├── testing/           runner.c (TAP test runner)
├── benchmarks/        bench_hash.c · bench_arena.c
├── examples/          hello_cpp · hello_python · hello_go · hello_zig
│                      hello_rust · hello_java · hello_csharp · hello_ruby
└── tests/
    ├── unit/          test_hash · test_arena · test_str · test_path
    │                  test_manifest · test_diag · test_chunker
    └── integration/   test_build_c.sh · test_output_types.sh
```

---

## Auto-Rules

qs_build applies automatic optimisations based on source file count:

| Sources | Effect |
|---|---|
| ≥ 30 | PCH auto-enabled for C and C++ |
| ≥ 150 | Unity chunking auto-enabled |
| Any | Parallel compilation (thread pool) |

Override with `--pch` / `--no-pch` / `--chunks` / `--no-chunks`.

---

## Environment Variables

| Variable | Description |
|---|---|
| `QS_BUILD_JOBS` | Parallel job count |
| `QS_BUILD_NO_COLOR` | Disable colour output |
| `QS_BUILD_VERBOSE` | Verbose mode |
| `QS_BUILD_NO_CACHE` | Disable incremental cache |
| `QS_BUILD_CACHE_DIR` | Cache directory path |
| `QS_BUILD_LOG` | Log file path |
| `QS_REMOTE_CACHE_URL` | Remote cache HTTP endpoint |
| `QS_REMOTE_CACHE_TOKEN` | Remote cache bearer token |
| `QS_BUILD_TELEMETRY_URL` | Anonymous telemetry endpoint (disabled by default) |
| `QS_PLUGIN_PATH` | Colon-separated plugin search directories |
| `QS_EXTENSION_PATH` | Colon-separated extension search directories |

---

## Forge:Visuals Integration

qs_build emits metadata consumed by Forge:Visuals:

- **`QS_REFLECT()`** annotations on declarations → `reflect.json` block registry
- **`/** QS_DOC`** comments → `docs.json` + per-module Markdown
- **`<name>.build_info.json`** → git commit, build date, host, version
- **`compile_commands.json`** → IDE/LSP integration

---

## Plugin API

Plugins are shared libraries exporting `qs_plugin_init`:

```c
#include "qs_plugin_api.h"

static qs_plugin_info_t INFO = {
    .api_version = 1,
    .name        = "my-plugin",
    .version     = "1.0.0",
    .description = "Does something useful",
};

qs_plugin_info_t *qs_plugin_init(qs_plugin_host_t *host) {
    host->register_pre_build(host, my_pre_build_hook, NULL);
    return &INFO;
}
```

Place compiled plugins in `<exe_dir>/plugins/` or set `QS_PLUGIN_PATH`.

---

## License

Apache License 2.0. — see [LICENSE](LICENSE).

---

## Status

`1.0.0-alpha` — core pipeline functional. Language drivers, PCH, chunking,
incremental builds, and diagnostics are complete. Asset pipeline and shader
compilation require external tools (`glslc`, `dxc`, etc.) on `PATH`.
