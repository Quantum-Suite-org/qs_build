/*
 * probe.c — Toolchain detection: find every compiler on PATH, probe version,
 * capabilities, and fill qs_toolchain_set_t.
 *
 * Strategy per language:
 *   C/C++    → try clang-17, clang-16, clang, gcc, g++, cl.exe in order
 *   C#       → dotnet --version, then csc if dotnet missing
 *   Java     → javac -version
 *   Python   → python3 --version, then python --version
 *   Go       → go version
 *   Ruby     → ruby --version
 *   Rust     → rustc --version
 *   Zig      → zig version
 *
 * All version strings are parsed into semver so callers can require minimums.
 */
#include "../core/include/qs_toolchain.h"
#include "../core/include/qs_process.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_arena.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#ifdef _WIN32
#  ifndef strcasecmp
#    define strcasecmp  _stricmp
#  endif
#  ifndef strncasecmp
#    define strncasecmp _strnicmp
#  endif
#endif


/* ── Version string extraction ───────────────────────────────────────────── */
/* Scan `text` for the first sequence matching \d+\.\d+[\.\d]* */
static char *extract_version(qs_arena_t *a, const char *text) {
    while (*text) {
        if (isdigit((unsigned char)*text)) {
            const char *start = text;
            while (isdigit((unsigned char)*text) || *text == '.') text++;
            qs_size_t len = (qs_size_t)(text - start);
            /* Must have at least one dot to be a version */
            qs_bool_t has_dot = QS_FALSE;
            for (qs_size_t i = 0; i < len; i++)
                if (start[i] == '.') { has_dot = QS_TRUE; break; }
            if (has_dot) {
                char *v = qs_arena_alloc(a, len+1, 1);
                memcpy(v, start, len); v[len] = '\0';
                return v;
            }
        } else text++;
    }
    return qs_arena_strdup(a, "unknown");
}

/* ── Generic probe: run "exe --version" or "exe version", grab first line ── */
static char *run_version_cmd(qs_arena_t *a, const char *exe, const char *flag) {
    const char *argv[] = { exe, flag, NULL };
    qs_proc_result_t pr;
    if (qs_proc_run(a, argv, NULL, 10000000ULL, &pr) != QS_OK) return NULL;
    if (pr.exit_code != 0 && !pr.stdout_text[0] && !pr.stderr_text[0]) return NULL;
    /* Version info may be in stdout or stderr */
    const char *out = pr.stdout_text[0] ? pr.stdout_text : pr.stderr_text;
    return extract_version(a, out);
}

/* ── Try to find exe on PATH, fill in a qs_toolchain_t ──────────────────── */
static qs_bool_t probe_one(qs_arena_t *a, const char *exe_name,
                             qs_lang_t lang, const char *version_flag,
                             qs_toolchain_t *out) {
    char *path = qs_proc_find_in_path(a, exe_name);
    if (!path) return QS_FALSE;
    char *ver = run_version_cmd(a, path, version_flag);
    if (!ver) return QS_FALSE;

    memset(out, 0, sizeof(*out));
    out->lang       = lang;
    out->name       = qs_arena_strdup(a, exe_name);
    out->executable = path;
    out->version    = ver;
    out->is_msvc    = (strstr(exe_name, "cl") != NULL && strstr(exe_name, "clang") == NULL)
                      ? QS_TRUE : QS_FALSE;

    /* Capability flags — probe with a quick compile if possible */
    if (lang == QS_LANG_C || lang == QS_LANG_CPP) {
        out->supports_pch     = QS_TRUE;  /* all supported C/C++ compilers */
        out->supports_lto     = !out->is_msvc ? QS_TRUE : QS_FALSE;
        out->supports_modules = QS_FALSE; /* set true for clang 16+ / gcc 14+ */
        /* Rough version check for modules support */
        int major = atoi(ver);
        if (!out->is_msvc) {
            if (strstr(exe_name, "clang") && major >= 16) out->supports_modules = QS_TRUE;
            if ((strstr(exe_name, "g++") || strstr(exe_name, "gcc")) && major >= 14)
                out->supports_modules = QS_TRUE;
        } else {
            out->supports_modules = QS_TRUE; /* MSVC has modules since VS2019 */
        }
    }
    return QS_TRUE;
}

/* ── Per-language probers ─────────────────────────────────────────────────── */
qs_result_t qs_toolchain_probe_clang(qs_arena_t *a, qs_toolchain_t *out) {
    const char *candidates[] = {
        "clang-18","clang-17","clang-16","clang-15","clang","cc",NULL
    };
    for (int i = 0; candidates[i]; i++)
        if (probe_one(a, candidates[i], QS_LANG_C, "--version", out)) return QS_OK;
    return QS_ERROR_NOT_FOUND;
}

/* probe_msvc: cl.exe with no source file exits immediately with error C1003
 * (no source files) but still prints the banner with the version — we treat
 * any exit that produced stderr output as a successful probe.
 * We also register MSVC under QS_LANG_C so C builds find it directly. */
static qs_bool_t probe_msvc_one(qs_arena_t *a, const char *exe, qs_toolchain_t *out) {
    char *path = qs_proc_find_in_path(a, exe);
    if (!path) return QS_FALSE;

    /* Run "cl /nologo" with no source — exits non-zero but prints version to stderr.
     * Do NOT use "/?" — that reads stdin on some MSVC versions and hangs. */
    const char *argv[] = { path, "/nologo", NULL };
    qs_proc_result_t pr;
    memset(&pr, 0, sizeof(pr));
    /* 5-second timeout — if it hangs at all, give up */
    if (qs_proc_run(a, argv, NULL, 5000000ULL, &pr) != QS_OK) return QS_FALSE;

    /* Version is in stderr banner: "Microsoft (R) C/C++ Optimizing Compiler Version XX.Y" */
    const char *banner = pr.stderr_text[0] ? pr.stderr_text : pr.stdout_text;
    if (!banner || !banner[0]) return QS_FALSE;

    /* Must look like an MSVC banner to avoid false positives */
    if (!strstr(banner, "Microsoft") && !strstr(banner, "Compiler") &&
        !strstr(banner, "MSVC") && !strstr(banner, "cl :")) return QS_FALSE;

    char *ver = extract_version(a, banner);
    if (!ver) ver = qs_arena_strdup(a, "unknown");

    memset(out, 0, sizeof(*out));
    out->lang             = QS_LANG_C;   /* primary: C */
    out->name             = qs_arena_strdup(a, exe);
    out->executable       = path;
    out->version          = ver;
    out->is_msvc          = QS_TRUE;
    out->supports_pch     = QS_TRUE;
    out->supports_lto     = QS_TRUE;  /* /GL + /LTCG */
    out->supports_modules = QS_TRUE;  /* VS2019+ */
    out->supports_unity   = QS_TRUE;
    return QS_TRUE;
}

qs_result_t qs_toolchain_probe_msvc(qs_arena_t *a, qs_toolchain_t *out) {
    if (probe_msvc_one(a, "cl.exe", out)) return QS_OK;
    if (probe_msvc_one(a, "cl",     out)) return QS_OK;
    return QS_ERROR_NOT_FOUND;
}

qs_result_t qs_toolchain_probe_gcc(qs_arena_t *a, qs_toolchain_t *out) {
    const char *candidates[] = { "gcc-14","gcc-13","gcc-12","gcc","cc",NULL };
    for (int i = 0; candidates[i]; i++)
        if (probe_one(a, candidates[i], QS_LANG_C, "--version", out)) return QS_OK;
    return QS_ERROR_NOT_FOUND;
}

qs_result_t qs_toolchain_probe_javac(qs_arena_t *a, qs_toolchain_t *out) {
    if (probe_one(a, "javac", QS_LANG_JAVA, "-version", out)) return QS_OK;
    return QS_ERROR_NOT_FOUND;
}

qs_result_t qs_toolchain_probe_dotnet(qs_arena_t *a, qs_toolchain_t *out) {
    if (probe_one(a, "dotnet", QS_LANG_CSHARP, "--version", out)) return QS_OK;
    if (probe_one(a, "csc",    QS_LANG_CSHARP, "--version", out)) return QS_OK;
    return QS_ERROR_NOT_FOUND;
}

qs_result_t qs_toolchain_probe_go(qs_arena_t *a, qs_toolchain_t *out) {
    if (probe_one(a, "go", QS_LANG_GO, "version", out)) return QS_OK;
    return QS_ERROR_NOT_FOUND;
}

qs_result_t qs_toolchain_probe_python(qs_arena_t *a, qs_toolchain_t *out) {
    if (probe_one(a, "python3", QS_LANG_PYTHON, "--version", out)) return QS_OK;
    if (probe_one(a, "python",  QS_LANG_PYTHON, "--version", out)) return QS_OK;
    return QS_ERROR_NOT_FOUND;
}

qs_result_t qs_toolchain_probe_rustc(qs_arena_t *a, qs_toolchain_t *out) {
    if (probe_one(a, "rustc", QS_LANG_RUST, "--version", out)) return QS_OK;
    return QS_ERROR_NOT_FOUND;
}

qs_result_t qs_toolchain_probe_zig(qs_arena_t *a, qs_toolchain_t *out) {
    if (probe_one(a, "zig", QS_LANG_ZIG, "version", out)) return QS_OK;
    return QS_ERROR_NOT_FOUND;
}

qs_result_t qs_toolchain_probe_ruby(qs_arena_t *a, qs_toolchain_t *out) {
    if (probe_one(a, "ruby", QS_LANG_RUBY, "--version", out)) return QS_OK;
    return QS_ERROR_NOT_FOUND;
}

qs_result_t qs_toolchain_probe_all(qs_arena_t *a, qs_toolchain_set_t *out,
                                     qs_diag_engine_t *diag) {
    out->count = 0;
    qs_toolchain_t tc;

    typedef qs_result_t (*probe_fn)(qs_arena_t*, qs_toolchain_t*);
    probe_fn probes[] = {
        qs_toolchain_probe_clang,
        qs_toolchain_probe_gcc,
        qs_toolchain_probe_msvc,
        qs_toolchain_probe_javac,
        qs_toolchain_probe_dotnet,
        qs_toolchain_probe_go,
        qs_toolchain_probe_python,
        qs_toolchain_probe_rustc,
        qs_toolchain_probe_zig,
        qs_toolchain_probe_ruby,
        NULL
    };
    for (int i = 0; probes[i] && out->count < QS_MAX_TOOLCHAINS; i++) {
        if (probes[i](a, &tc) == QS_OK) {
            out->entries[out->count++] = tc;
            /* MSVC serves both C and C++ — register a second slot for C++
             * so C++ builds get an exact lang match, not just the fallback. */
            if (tc.is_msvc && out->count < QS_MAX_TOOLCHAINS) {
                qs_toolchain_t tc_cpp = tc;
                tc_cpp.lang = QS_LANG_CPP;
                out->entries[out->count++] = tc_cpp;
            }
        }
    }
    return QS_OK;
}

/* Normalize a language lookup: C and C++ compilers are interchangeable.
 * MSVC (is_msvc=true) serves both C and C++ regardless of which lang slot
 * it was registered under. */
const qs_toolchain_t *qs_toolchain_for_lang(const qs_toolchain_set_t *set, qs_lang_t lang) {
    /* Exact match first */
    for (qs_size_t i = 0; i < set->count; i++)
        if (set->entries[i].lang == lang) return &set->entries[i];

    /* C/C++ are interchangeable — any C compiler can compile C, any C++ can too */
    if (lang == QS_LANG_C || lang == QS_LANG_CPP) {
        for (qs_size_t i = 0; i < set->count; i++) {
            qs_lang_t el = set->entries[i].lang;
            if (el == QS_LANG_C || el == QS_LANG_CPP)
                return &set->entries[i];
        }
    }
    return NULL;
}

/* Find a toolchain by executable name or alias (e.g. "msvc", "cl", "clang").
 * Used to apply --toolchain override. Case-insensitive prefix match. */
const qs_toolchain_t *qs_toolchain_by_name(const qs_toolchain_set_t *set,
                                             const char *name) {
    if (!name || !name[0]) return NULL;

    /* Normalize common aliases */
    const char *alias = name;
    char lower[64]; qs_size_t nl = strlen(name);
    if (nl < sizeof(lower)) {
        for (qs_size_t i=0;i<=nl;i++) lower[i]=(char)tolower((unsigned char)name[i]);
        alias = lower;
    }

    /* "msvc" / "cl" / "cl.exe" → MSVC */
    if (strcmp(alias,"msvc")==0 || strcmp(alias,"cl")==0 || strcmp(alias,"cl.exe")==0) {
        for (qs_size_t i=0;i<set->count;i++)
            if (set->entries[i].is_msvc) return &set->entries[i];
        return NULL;
    }

    /* Exact name match */
    for (qs_size_t i=0;i<set->count;i++)
        if (strcasecmp(set->entries[i].name, name)==0) return &set->entries[i];

    /* Prefix match on executable basename */
    for (qs_size_t i=0;i<set->count;i++) {
        const char *exe = set->entries[i].executable;
        const char *base = strrchr(exe, '/');
        if (!base) base = strrchr(exe, '\\');
        base = base ? base+1 : exe;
        /* strip .exe */
        char bname[256]; qs_size_t blen=strlen(base);
        if (blen<sizeof(bname)) {
            memcpy(bname,base,blen+1);
            if (blen>4&&strcasecmp(bname+blen-4,".exe")==0) bname[blen-4]='\0';
        }
        if (strcasecmp(bname,name)==0) return &set->entries[i];
    }
    return NULL;
}

void qs_toolchain_print(const qs_toolchain_set_t *set) {
    fprintf(stderr, "[toolchain] %llu compilers found:\n",
            (unsigned long long)set->count);
    for (qs_size_t i = 0; i < set->count; i++) {
        const qs_toolchain_t *tc = &set->entries[i];
        fprintf(stderr, "  %-12s %-8s %s\n", tc->name, tc->version, tc->executable);
    }
}
