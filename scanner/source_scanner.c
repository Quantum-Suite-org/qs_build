/*
 * scanner/source_scanner.c — Recursively scans source trees.
 * Given a root directory and a language, discovers all source files
 * and populates a qs_manifest_t's source list automatically.
 * Called when build.qs has no explicit sources= list.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_path.h"
#include "../core/include/qs_vec.h"
#include <string.h>
#include <stdio.h>

static const char *C_EXTS[]      = {".c", ".h", NULL};
static const char *CPP_EXTS[]    = {".cpp",".cxx",".cc",".C",".ixx",".cppm",
                                      ".hpp",".hxx",".hh",".h",NULL};
static const char *CS_EXTS[]     = {".cs", NULL};
static const char *JAVA_EXTS[]   = {".java", NULL};
static const char *PY_EXTS[]     = {".py", NULL};
static const char *GO_EXTS[]     = {".go", NULL};
static const char *RB_EXTS[]     = {".rb", ".rake", NULL};
static const char *ZIG_EXTS[]    = {".zig", NULL};
static const char *RUST_EXTS[]   = {".rs", NULL};

static const char *const *lang_exts(qs_lang_t lang) {
    switch(lang) {
        case QS_LANG_C:      return C_EXTS;
        case QS_LANG_CPP:    return CPP_EXTS;
        case QS_LANG_CSHARP: return CS_EXTS;
        case QS_LANG_JAVA:   return JAVA_EXTS;
        case QS_LANG_PYTHON: return PY_EXTS;
        case QS_LANG_GO:     return GO_EXTS;
        case QS_LANG_RUBY:   return RB_EXTS;
        case QS_LANG_ZIG:    return ZIG_EXTS;
        case QS_LANG_RUST:   return RUST_EXTS;
        default:             return NULL;
    }
}

/* Skip common non-source directories */
static qs_bool_t should_skip_dir(const char *name) {
    static const char *SKIP[] = {
        ".git",".hg",".svn","node_modules","vendor","target",
        ".zig-cache","zig-out","__pycache__",".eggs","dist",
        "build","out",".qs_cache",".qs_pch",".qs_chunks",NULL
    };
    for (int i = 0; SKIP[i]; i++)
        if (strcmp(name, SKIP[i]) == 0) return QS_TRUE;
    return QS_FALSE;
}

qs_result_t qs_scan_sources(qs_arena_t *a, const char *root_dir,
                               qs_lang_t lang, qs_str_vec_t *sources_out,
                               qs_str_vec_t *headers_out) {
    const char *const *exts = lang_exts(lang);
    if (!exts) return QS_ERROR_UNSUPPORTED;

    /* Separate header extensions from source extensions for C/C++ */
    static const char *HDR_EXTS[] = {".h",".hpp",".hxx",".hh",NULL};
    static const char *SRC_ONLY_C[]   = {".c",NULL};
    static const char *SRC_ONLY_CPP[] = {".cpp",".cxx",".cc",".C",".ixx",".cppm",NULL};

    qs_str_vec_t all; qs_str_vec_init(&all, a);
    qs_fs_scan_sources(a, root_dir, exts, &all);

    for (qs_size_t i = 0; i < all.len; i++) {
        qs_str_t p = all.data[i];
        /* Skip build/output directories anywhere in path */
        qs_bool_t skip = QS_FALSE;
        const char *ps = (const char*)p.ptr;
        /* Check each path component */
        for (qs_size_t j = 0; j < p.len; j++) {
            if (ps[j]=='/'||ps[j]=='\\') {
                /* extract component before this separator */
                qs_size_t start = j;
                while (start>0 && ps[start-1]!='/' && ps[start-1]!='\\') start--;
                char comp[256]; qs_size_t clen=j-start;
                if (clen<255) { memcpy(comp,ps+start,clen); comp[clen]='\0';
                    if (should_skip_dir(comp)) { skip=QS_TRUE; break; } }
            }
        }
        if (skip) continue;

        if ((lang==QS_LANG_C||lang==QS_LANG_CPP) && headers_out) {
            if (qs_path_has_ext(p, HDR_EXTS))
                qs_str_vec_push(headers_out, p);
            else
                qs_str_vec_push(sources_out, p);
        } else {
            qs_str_vec_push(sources_out, p);
        }
    }

    fprintf(stderr, "  [scan] %s: %llu sources, %llu headers\n",
        root_dir,
        (unsigned long long)sources_out->len,
        (unsigned long long)(headers_out ? headers_out->len : 0));
    return QS_OK;
}
