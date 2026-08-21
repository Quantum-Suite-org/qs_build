/* qs_diag.h — structured diagnostics engine for qs_build (zero deps, C11)
 *
 * Every error produced by qs_build or any language driver flows through
 * this engine.  A diagnostic carries:
 *   - severity (note / warning / error / fatal)
 *   - an error code string ("QSB-E001", "CLG-E042", etc.)
 *   - the originating tool name ("clang", "javac", "go", "dotnet", ...)
 *   - the source file, line, column, and span
 *   - a primary human-readable message
 *   - an optional structured note (extra context)
 *   - an optional hint (actionable fix suggestion)
 *   - the raw stderr line that was parsed
 *   - a link to documentation
 *
 * Parsers for each language's compiler stderr are in the individual
 * language driver files and call qs_diag_emit() to submit.
 */
#ifndef QS_DIAG_H
#define QS_DIAG_H
#include "qs_types.h"
#include "qs_str.h"
#include "qs_arena.h"
#include "qs_vec.h"

/* ── Severity ────────────────────────────────────────────────────────────── */
typedef enum {
    QS_SEV_NOTE    = 0,
    QS_SEV_HINT    = 1,
    QS_SEV_WARNING = 2,
    QS_SEV_ERROR   = 3,
    QS_SEV_FATAL   = 4,
} qs_severity_t;

static inline const char *qs_severity_str(qs_severity_t s) {
    switch(s) {
        case QS_SEV_NOTE:    return "note";
        case QS_SEV_HINT:    return "hint";
        case QS_SEV_WARNING: return "warning";
        case QS_SEV_ERROR:   return "error";
        case QS_SEV_FATAL:   return "fatal";
        default:             return "?";
    }
}

/* ANSI colour for terminal output */
static inline const char *qs_severity_colour(qs_severity_t s) {
    switch(s) {
        case QS_SEV_NOTE:    return "\033[36m";  /* cyan   */
        case QS_SEV_HINT:    return "\033[34m";  /* blue   */
        case QS_SEV_WARNING: return "\033[33m";  /* yellow */
        case QS_SEV_ERROR:   return "\033[31m";  /* red    */
        case QS_SEV_FATAL:   return "\033[35m";  /* magenta*/
        default:             return "\033[0m";
    }
}
#define QS_COLOUR_RESET "\033[0m"
#define QS_COLOUR_BOLD  "\033[1m"

/* ── Source location ─────────────────────────────────────────────────────── */
typedef struct {
    qs_str_t  file;       /* path to source; empty = unknown */
    qs_u64    line;       /* 1-based; 0 = unknown */
    qs_u64    col;        /* 1-based; 0 = unknown */
    qs_u64    span;       /* byte length of highlighted span; 0 = point */
    qs_str_t  src_line;   /* the actual source line text (for caret display) */
} qs_loc_t;

#define QS_LOC_UNKNOWN ((qs_loc_t){ QS_STR_EMPTY, 0, 0, 0, QS_STR_EMPTY })

/* ── Diagnostic record ───────────────────────────────────────────────────── */
typedef struct {
    qs_severity_t sev;
    qs_str_t      code;      /* e.g. "QSB-E001", empty if from raw tool output */
    qs_str_t      tool;      /* "clang","cl","javac","go","dotnet","rustc","zig"... */
    qs_str_t      message;   /* primary human-readable text */
    qs_loc_t      loc;       /* primary source location */
    qs_str_t      note;      /* supplementary context */
    qs_str_t      hint;      /* actionable fix suggestion */
    qs_str_t      raw;       /* verbatim stderr line that was parsed */
    qs_str_t      url;       /* link to error docs; may be empty */
    /* Related locations (e.g. "previously declared here") */
    qs_loc_t     *related;
    qs_size_t     related_count;
} qs_diag_t;

/* ── Diagnostic list ─────────────────────────────────────────────────────── */
QS_VEC_DECL(qs_diag_t, qs_diag_vec)

/* ── Engine ──────────────────────────────────────────────────────────────── */
typedef void (*qs_diag_callback_t)(const qs_diag_t *d, void *ctx);

typedef struct {
    qs_arena_t         *arena;
    qs_diag_vec_t       diags;
    qs_u64              counts[5];     /* indexed by qs_severity_t */
    qs_diag_callback_t  on_emit;       /* called for every diag; may be NULL */
    void               *on_emit_ctx;
    qs_bool_t           color;         /* emit ANSI colour codes */
    qs_bool_t           show_raw;      /* print raw stderr line under diag */
    qs_bool_t           show_caret;    /* print source line + caret */
    qs_bool_t           json_mode;     /* emit as JSON instead of text */
    qs_u64              error_limit;   /* stop after this many errors (0=unlimited) */
    qs_bool_t           limit_reached;
} qs_diag_engine_t;

void          qs_diag_engine_init(qs_diag_engine_t *e, qs_arena_t *arena);
void          qs_diag_emit(qs_diag_engine_t *e, qs_diag_t d);
void          qs_diag_emit_simple(qs_diag_engine_t *e, qs_severity_t sev,
                                  const char *tool, const char *code,
                                  qs_loc_t loc, const char *message);
void          qs_diag_print_all(const qs_diag_engine_t *e);
void          qs_diag_print_summary(const qs_diag_engine_t *e);
void          qs_diag_print_json(const qs_diag_engine_t *e);
qs_bool_t     qs_diag_has_errors(const qs_diag_engine_t *e);
qs_u64        qs_diag_error_count(const qs_diag_engine_t *e);
qs_u64        qs_diag_warning_count(const qs_diag_engine_t *e);
void          qs_diag_clear(qs_diag_engine_t *e);

/* Per-language stderr parsers — each fills *out and returns number of diags parsed */
qs_size_t qs_diag_parse_clang(qs_arena_t *a, const char *stderr_text, qs_str_t tool,
                               qs_diag_t *out, qs_size_t out_cap);
qs_size_t qs_diag_parse_msvc(qs_arena_t *a, const char *stderr_text, qs_str_t tool,
                              qs_diag_t *out, qs_size_t out_cap);
qs_size_t qs_diag_parse_gcc(qs_arena_t *a, const char *stderr_text, qs_str_t tool,
                             qs_diag_t *out, qs_size_t out_cap);
qs_size_t qs_diag_parse_javac(qs_arena_t *a, const char *stderr_text,
                               qs_diag_t *out, qs_size_t out_cap);
qs_size_t qs_diag_parse_dotnet(qs_arena_t *a, const char *stderr_text,
                                qs_diag_t *out, qs_size_t out_cap);
qs_size_t qs_diag_parse_go(qs_arena_t *a, const char *stderr_text,
                            qs_diag_t *out, qs_size_t out_cap);
qs_size_t qs_diag_parse_python(qs_arena_t *a, const char *stderr_text,
                                qs_diag_t *out, qs_size_t out_cap);
qs_size_t qs_diag_parse_rustc(qs_arena_t *a, const char *stderr_text,
                               qs_diag_t *out, qs_size_t out_cap);
qs_size_t qs_diag_parse_zig(qs_arena_t *a, const char *stderr_text,
                             qs_diag_t *out, qs_size_t out_cap);
qs_size_t qs_diag_parse_ruby(qs_arena_t *a, const char *stderr_text,
                              qs_diag_t *out, qs_size_t out_cap);

/* Convenience: parse stderr for a given tool name and emit into engine */
qs_result_t qs_diag_ingest_tool_output(qs_diag_engine_t *e, qs_arena_t *scratch,
                                        qs_str_t tool, const char *stderr_text,
                                        qs_i64 exit_code);
#endif /* QS_DIAG_H */
