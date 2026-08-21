/* qs_manifest.h — build.qs manifest parser and validator */
#ifndef QS_MANIFEST_H
#define QS_MANIFEST_H
#include "qs_types.h"
#include "qs_arena.h"
#include "qs_str.h"
#include "qs_vec.h"
#include "qs_path.h"
#include "qs_diag.h"

typedef enum {
    QS_OUT_EXE=0, QS_OUT_DLL, QS_OUT_LIB, QS_OUT_APP,
    QS_OUT_QPKG, QS_OUT_CDYLIB, QS_OUT_SHAREDLIB, QS_OUT_STATICLIB,
    QS_OUT_WHEEL,       /* Python → PyPI wheel */
    QS_OUT_JAR,         /* Java .jar */
    QS_OUT_GEM,         /* Ruby .gem */
    QS_OUT_NUPKG,       /* .NET NuGet package */
    QS_OUT_GOBIN,       /* Go binary */
    QS_OUT_OBJECT,      /* raw .o/.obj */
} qs_output_type_t;

static inline const char *qs_output_type_str(qs_output_type_t t) {
    switch(t) {
        case QS_OUT_EXE:       return "exe";
        case QS_OUT_DLL:       return "dll";
        case QS_OUT_LIB:       return "lib";
        case QS_OUT_APP:       return "app";
        case QS_OUT_QPKG:      return "qpkg";
        case QS_OUT_CDYLIB:    return "cdylib";
        case QS_OUT_STATICLIB: return "staticlib";
        case QS_OUT_WHEEL:     return "wheel";
        case QS_OUT_JAR:       return "jar";
        case QS_OUT_GEM:       return "gem";
        case QS_OUT_NUPKG:     return "nupkg";
        case QS_OUT_GOBIN:     return "gobin";
        case QS_OUT_OBJECT:    return "object";
        default:               return "unknown";
    }
}

typedef struct {
    qs_str_t name;
    qs_str_t version;
    qs_lang_t language;
    qs_str_t standard;      /* "c++20", "c17", "java21", etc. */
    qs_str_vec_t sources;
    qs_str_vec_t headers;
    qs_str_vec_t modules;   /* C++20 .ixx / Java modules */
    qs_str_vec_t dependencies;
    qs_str_vec_t tests;
    qs_str_vec_t defines;
    qs_str_vec_t include_dirs;
    qs_str_vec_t lib_dirs;
    qs_str_vec_t link_libs;
    qs_str_vec_t extra_flags;
    qs_output_type_t output_type;
    qs_u64       chunk_size;    /* 0 = auto */
    qs_bool_t    enable_lto;
    qs_bool_t    enable_modules;
    qs_bool_t    enable_pch;    /* 0 = auto (set when >=QS_PCH_THRESHOLD sources) */
    qs_bool_t    enable_chunks; /* 0 = auto (set when >=QS_CHUNK_THRESHOLD sources) */
    qs_str_t     pch_header;    /* explicit PCH header; empty = auto-generated */
    qs_str_t     out_dir;
    qs_str_t     manifest_path; /* path to the build.qs file */
    /* Language-specific extras */
    qs_str_t     dotnet_sdk;     /* e.g. "net8.0" */
    qs_str_t     java_version;   /* e.g. "21" */
    qs_str_t     python_version; /* e.g. "3.12" */
    qs_str_t     go_module;      /* go.mod module path */
    qs_str_t     ruby_gemspec;
    qs_str_t     pypi_index;     /* PyPI upload URL */
    qs_str_t     maven_repo;
    qs_bool_t    publish;        /* --publish: upload wheel/gem/jar after build */
} qs_manifest_t;

qs_result_t qs_manifest_parse(qs_arena_t *a, const char *path,
                               qs_manifest_t *out, qs_diag_engine_t *diag);
qs_result_t qs_manifest_validate(qs_manifest_t *m, qs_diag_engine_t *diag);
/* Apply auto-rules: set enable_pch/enable_chunks based on thresholds. */
void        qs_manifest_apply_auto_rules(qs_manifest_t *m);
void        qs_manifest_print(const qs_manifest_t *m);

/* Parse --types flag string into qs_output_type_t */
qs_result_t qs_output_type_parse(const char *s, qs_output_type_t *out);
#endif /* QS_MANIFEST_H */
