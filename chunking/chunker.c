/*
 * chunker.c — Unity-build chunk management for all supported languages.
 *
 * AUTO-RULE: chunking is enabled automatically when a module has >= QS_CHUNK_THRESHOLD
 * (150) source files. Files are grouped into chunks of QS_DEFAULT_CHUNK_SIZE (50).
 * Each language generates a different unity file format:
 *   C/C++   → _chunk_N.c / _chunk_N.cpp with sequential #includes
 *   C#      → _chunk_N.cs with partial class amalgam + using directives
 *   Java    → per-chunk javac invocation lists (no unity file; javac handles batching)
 *   Python  → _chunk_N_manifest.txt (list of modules for compileall)
 *   Go      → package-level grouping (go build handles internally)
 *   Ruby    → _chunk_N_loader.rb with sequential require statements
 *   Rust    → _chunk_N.rs with mod declarations
 *   Zig     → _chunk_N.zig with @import chains
 *
 * Incremental: each chunk carries an xxhash fingerprint of (member mtimes + flags).
 * Only chunks with changed fingerprints are recompiled.
 */
#include "../core/include/qs_chunker.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_hash.h"
#include "../core/include/qs_str.h"
#include "../core/include/qs_path.h"
#include "../core/include/qs_arena.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Threshold check ─────────────────────────────────────────────────────── */
qs_bool_t qs_chunker_should_enable(const qs_manifest_t *m) {
    if (m->enable_chunks) return QS_TRUE;
    return m->sources.len >= QS_CHUNK_THRESHOLD ? QS_TRUE : QS_FALSE;
}

/* ── Build chunk plan ────────────────────────────────────────────────────── */
qs_result_t qs_chunker_plan(qs_arena_t *a, const qs_manifest_t *m,
                              qs_chunk_plan_t *out, qs_diag_engine_t *diag) {
    qs_size_t chunk_sz = m->chunk_size ? m->chunk_size : QS_DEFAULT_CHUNK_SIZE;
    qs_size_t nsrc     = m->sources.len;
    qs_size_t nchunks  = (nsrc + chunk_sz - 1) / chunk_sz;
    if (nchunks == 0) nchunks = 1;

    out->chunk_size    = chunk_sz;
    out->total_sources = nsrc;
    out->lang          = m->language;
    out->out_dir       = QS_STR_IS_EMPTY(m->out_dir) ? (char*)"./out"
                                                      : qs_arena_str_to_cstr(a, m->out_dir);
    qs_chunk_vec_init(&out->chunks, a);

    for (qs_size_t ci = 0; ci < nchunks; ci++) {
        qs_chunk_t ch;
        memset(&ch, 0, sizeof(ch));
        ch.index = ci;
        qs_str_vec_init(&ch.members, a);

        qs_size_t start = ci * chunk_sz;
        qs_size_t end   = start + chunk_sz;
        if (end > nsrc) end = nsrc;

        for (qs_size_t fi = start; fi < end; fi++)
            qs_str_vec_push(&ch.members, m->sources.data[fi]);

        /* Determine file extension for generated unity file */
        const char *ext = ".c";
        switch (m->language) {
            case QS_LANG_CPP:    ext = ".cpp";   break;
            case QS_LANG_CSHARP: ext = ".cs";    break;
            case QS_LANG_JAVA:   ext = ".java";  break;
            case QS_LANG_PYTHON: ext = ".txt";   break;
            case QS_LANG_RUBY:   ext = ".rb";    break;
            case QS_LANG_RUST:   ext = ".rs";    break;
            case QS_LANG_ZIG:    ext = ".zig";   break;
            default:             ext = ".c";     break;
        }

        char *chunk_dir = qs_arena_sprintf(a, "%s/.qs_chunks", out->out_dir);
        qs_fs_mkdir_p(chunk_dir);
        ch.generated_path = qs_arena_sprintf(a, "%s/_chunk_%03llu%s",
                                              chunk_dir, (unsigned long long)ci, ext);
        ch.object_path    = qs_arena_sprintf(a, "%s/_chunk_%03llu.o",
                                              chunk_dir, (unsigned long long)ci);
        ch.needs_rebuild  = QS_TRUE; /* will be updated by fingerprint pass */
        qs_chunk_vec_push(&out->chunks, ch);
    }

    if (diag && nchunks > 1) {
        qs_diag_emit_simple(diag, QS_SEV_NOTE, "chunker", "QSB-CHK001",
            QS_LOC_UNKNOWN,
            qs_arena_sprintf(a, "chunking %llu sources into %llu chunks of %llu",
                (unsigned long long)nsrc, (unsigned long long)nchunks,
                (unsigned long long)chunk_sz));
    }
    return QS_OK;
}

/* ── Fingerprint all chunks ──────────────────────────────────────────────── */
qs_result_t qs_chunker_fingerprint(qs_arena_t *a, qs_chunk_plan_t *plan,
                                    const char *cache_dir) {
    char cache_path[4096];
    for (qs_size_t ci = 0; ci < plan->chunks.len; ci++) {
        qs_chunk_t *ch = &plan->chunks.data[ci];
        qs_hasher_t h; qs_hasher_init(&h, 0);
        for (qs_size_t fi = 0; fi < ch->members.len; fi++) {
            char *p = qs_arena_str_to_cstr(a, ch->members.data[fi]);
            qs_stat_t st; qs_fs_stat(p, &st);
            qs_hasher_update_u64(&h, st.mtime_us);
            qs_hasher_update_u64(&h, st.size);
            qs_hasher_update_str(&h, p);
        }
        ch->fingerprint = qs_hasher_finish(&h);

        /* Check if cached fingerprint matches */
        char hex[17]; qs_hex_encode64(ch->fingerprint, hex);
        snprintf(cache_path, sizeof(cache_path), "%s/chunk_%03llu.fp",
                 cache_dir, (unsigned long long)ci);
        char *cached = NULL; qs_size_t clen = 0;
        if (qs_fs_read_file(a, cache_path, &cached, &clen) == QS_OK && cached) {
            ch->needs_rebuild = (strncmp(cached, hex, 16) == 0) ? QS_FALSE : QS_TRUE;
        } else {
            ch->needs_rebuild = QS_TRUE;
        }
    }
    return QS_OK;
}

/* ── Write unity file for C ──────────────────────────────────────────────── */
qs_result_t qs_chunker_write_c(qs_arena_t *a, qs_chunk_plan_t *plan,
                                 qs_diag_engine_t *diag) {
    for (qs_size_t ci = 0; ci < plan->chunks.len; ci++) {
        qs_chunk_t *ch = &plan->chunks.data[ci];
        if (!ch->needs_rebuild) continue;
        FILE *f = fopen(ch->generated_path, "w");
        if (!f) { qs_diag_emit_simple(diag, QS_SEV_ERROR, "chunker", "QSB-CHK010",
            QS_LOC_UNKNOWN, qs_arena_sprintf(a,"cannot write chunk: %s",ch->generated_path));
            return QS_ERROR_CHUNK; }
        fprintf(f, "/* qs_build unity chunk %llu — do not edit */\n",
                (unsigned long long)ci);
        fprintf(f, "/* Sources: %llu files */\n\n", (unsigned long long)ch->members.len);
        /* Disable clang/GCC warnings that fire on unity builds */
        fprintf(f, "#ifdef __clang__\n");
        fprintf(f, "#pragma clang diagnostic push\n");
        fprintf(f, "#pragma clang diagnostic ignored \"-Weverything\"\n");
        fprintf(f, "#endif\n\n");
        for (qs_size_t fi = 0; fi < ch->members.len; fi++)
            fprintf(f, "#include \"%.*s\"\n",
                (int)ch->members.data[fi].len, (const char*)ch->members.data[fi].ptr);
        fprintf(f, "\n#ifdef __clang__\n#pragma clang diagnostic pop\n#endif\n");
        fclose(f);
    }
    return QS_OK;
}

/* ── Write unity file for C++ ────────────────────────────────────────────── */
qs_result_t qs_chunker_write_cpp(qs_arena_t *a, qs_chunk_plan_t *plan,
                                   qs_diag_engine_t *diag) {
    /* C++ unity file is identical to C but with C++ pragma suppression */
    for (qs_size_t ci = 0; ci < plan->chunks.len; ci++) {
        qs_chunk_t *ch = &plan->chunks.data[ci];
        if (!ch->needs_rebuild) continue;
        FILE *f = fopen(ch->generated_path, "w");
        if (!f) return QS_ERROR_CHUNK;
        fprintf(f, "// qs_build C++ unity chunk %llu — do not edit\n",
                (unsigned long long)ci);
        fprintf(f, "// Sources: %llu files\n\n", (unsigned long long)ch->members.len);
        fprintf(f, "#ifdef __clang__\n#pragma clang diagnostic push\n");
        fprintf(f, "#pragma clang diagnostic ignored \"-Weverything\"\n#endif\n");
        fprintf(f, "#if defined(__GNUC__) && !defined(__clang__)\n");
        fprintf(f, "#pragma GCC diagnostic push\n");
        fprintf(f, "#pragma GCC diagnostic ignored \"-Wall\"\n");
        fprintf(f, "#pragma GCC diagnostic ignored \"-Wextra\"\n#endif\n\n");
        for (qs_size_t fi = 0; fi < ch->members.len; fi++)
            fprintf(f, "#include \"%.*s\"\n",
                (int)ch->members.data[fi].len, (const char*)ch->members.data[fi].ptr);
        fprintf(f, "\n#ifdef __clang__\n#pragma clang diagnostic pop\n#endif\n");
        fprintf(f, "#if defined(__GNUC__) && !defined(__clang__)\n#pragma GCC diagnostic pop\n#endif\n");
        fclose(f);
    }
    return QS_OK;
}

/* ── Write unity file for C# ─────────────────────────────────────────────── */
qs_result_t qs_chunker_write_csharp(qs_arena_t *a, qs_chunk_plan_t *plan,
                                      qs_diag_engine_t *diag) {
    for (qs_size_t ci = 0; ci < plan->chunks.len; ci++) {
        qs_chunk_t *ch = &plan->chunks.data[ci];
        if (!ch->needs_rebuild) continue;
        FILE *f = fopen(ch->generated_path, "w");
        if (!f) return QS_ERROR_CHUNK;
        fprintf(f, "// qs_build C# chunk %llu — do not edit\n",
                (unsigned long long)ci);
        /* C# doesn't have #include; write a response file list instead */
        /* The actual unity mechanism is dotnet compile @responsefile */
        for (qs_size_t fi = 0; fi < ch->members.len; fi++)
            fprintf(f, "%.*s\n",
                (int)ch->members.data[fi].len, (const char*)ch->members.data[fi].ptr);
        fclose(f);
    }
    return QS_OK;
}

/* ── Write unity file for Java ───────────────────────────────────────────── */
qs_result_t qs_chunker_write_java(qs_arena_t *a, qs_chunk_plan_t *plan,
                                    qs_diag_engine_t *diag) {
    /* Java uses @argfile — write a list of source files per chunk */
    for (qs_size_t ci = 0; ci < plan->chunks.len; ci++) {
        qs_chunk_t *ch = &plan->chunks.data[ci];
        if (!ch->needs_rebuild) continue;
        FILE *f = fopen(ch->generated_path, "w");
        if (!f) return QS_ERROR_CHUNK;
        for (qs_size_t fi = 0; fi < ch->members.len; fi++)
            fprintf(f, "%.*s\n",
                (int)ch->members.data[fi].len, (const char*)ch->members.data[fi].ptr);
        fclose(f);
    }
    return QS_OK;
}

/* ── Write unity file for Python ─────────────────────────────────────────── */
qs_result_t qs_chunker_write_python(qs_arena_t *a, qs_chunk_plan_t *plan,
                                      qs_diag_engine_t *diag) {
    for (qs_size_t ci = 0; ci < plan->chunks.len; ci++) {
        qs_chunk_t *ch = &plan->chunks.data[ci];
        if (!ch->needs_rebuild) continue;
        FILE *f = fopen(ch->generated_path, "w");
        if (!f) return QS_ERROR_CHUNK;
        fprintf(f, "# qs_build Python chunk %llu manifest\n", (unsigned long long)ci);
        for (qs_size_t fi = 0; fi < ch->members.len; fi++)
            fprintf(f, "%.*s\n",
                (int)ch->members.data[fi].len, (const char*)ch->members.data[fi].ptr);
        fclose(f);
    }
    return QS_OK;
}

/* ── Write unity file for Go ─────────────────────────────────────────────── */
qs_result_t qs_chunker_write_go(qs_arena_t *a, qs_chunk_plan_t *plan,
                                  qs_diag_engine_t *diag) {
    /* Go handles compilation per-package; chunks are package groups */
    for (qs_size_t ci = 0; ci < plan->chunks.len; ci++) {
        qs_chunk_t *ch = &plan->chunks.data[ci];
        if (!ch->needs_rebuild) continue;
        FILE *f = fopen(ch->generated_path, "w");
        if (!f) return QS_ERROR_CHUNK;
        fprintf(f, "// qs_build Go chunk %llu package list\n", (unsigned long long)ci);
        for (qs_size_t fi = 0; fi < ch->members.len; fi++)
            fprintf(f, "%.*s\n",
                (int)ch->members.data[fi].len, (const char*)ch->members.data[fi].ptr);
        fclose(f);
    }
    return QS_OK;
}

/* ── Write unity file for Ruby ───────────────────────────────────────────── */
qs_result_t qs_chunker_write_ruby(qs_arena_t *a, qs_chunk_plan_t *plan,
                                    qs_diag_engine_t *diag) {
    for (qs_size_t ci = 0; ci < plan->chunks.len; ci++) {
        qs_chunk_t *ch = &plan->chunks.data[ci];
        if (!ch->needs_rebuild) continue;
        FILE *f = fopen(ch->generated_path, "w");
        if (!f) return QS_ERROR_CHUNK;
        fprintf(f, "# qs_build Ruby unity loader chunk %llu\n", (unsigned long long)ci);
        fprintf(f, "# frozen_string_literal: true\n\n");
        for (qs_size_t fi = 0; fi < ch->members.len; fi++)
            fprintf(f, "require_relative '%.*s'\n",
                (int)ch->members.data[fi].len, (const char*)ch->members.data[fi].ptr);
        fclose(f);
    }
    return QS_OK;
}

/* ── Write unity file for Rust ───────────────────────────────────────────── */
qs_result_t qs_chunker_write_rust(qs_arena_t *a, qs_chunk_plan_t *plan,
                                    qs_diag_engine_t *diag) {
    for (qs_size_t ci = 0; ci < plan->chunks.len; ci++) {
        qs_chunk_t *ch = &plan->chunks.data[ci];
        if (!ch->needs_rebuild) continue;
        FILE *f = fopen(ch->generated_path, "w");
        if (!f) return QS_ERROR_CHUNK;
        fprintf(f, "// qs_build Rust unity chunk %llu — do not edit\n",
                (unsigned long long)ci);
        fprintf(f, "#![allow(unused, dead_code, non_snake_case)]\n\n");
        for (qs_size_t fi = 0; fi < ch->members.len; fi++) {
            qs_str_t stem = qs_path_stem(ch->members.data[fi]);
            fprintf(f, "#[path = \"%.*s\"]\n",
                (int)ch->members.data[fi].len, (const char*)ch->members.data[fi].ptr);
            fprintf(f, "pub mod chunk_%llu_%llu;\n",
                (unsigned long long)ci, (unsigned long long)fi);
        }
        fclose(f);
    }
    return QS_OK;
}

/* ── Write unity file for Zig ────────────────────────────────────────────── */
qs_result_t qs_chunker_write_zig(qs_arena_t *a, qs_chunk_plan_t *plan,
                                   qs_diag_engine_t *diag) {
    for (qs_size_t ci = 0; ci < plan->chunks.len; ci++) {
        qs_chunk_t *ch = &plan->chunks.data[ci];
        if (!ch->needs_rebuild) continue;
        FILE *f = fopen(ch->generated_path, "w");
        if (!f) return QS_ERROR_CHUNK;
        fprintf(f, "// qs_build Zig unity chunk %llu — do not edit\n\n",
                (unsigned long long)ci);
        for (qs_size_t fi = 0; fi < ch->members.len; fi++) {
            qs_str_t stem = qs_path_stem(ch->members.data[fi]);
            fprintf(f, "pub const chunk_%llu_%llu = @import(\"%.*s\");\n",
                (unsigned long long)ci, (unsigned long long)fi,
                (int)ch->members.data[fi].len, (const char*)ch->members.data[fi].ptr);
        }
        fclose(f);
    }
    return QS_OK;
}

void qs_chunker_print_plan(const qs_chunk_plan_t *plan) {
    fprintf(stderr, "[chunker] %llu sources → %llu chunks of %llu (lang=%d)\n",
        (unsigned long long)plan->total_sources,
        (unsigned long long)plan->chunks.len,
        (unsigned long long)plan->chunk_size,
        (int)plan->lang);
    for (qs_size_t i = 0; i < plan->chunks.len; i++) {
        const qs_chunk_t *ch = &plan->chunks.data[i];
        fprintf(stderr, "  chunk[%llu]: %llu files, rebuild=%s, fp=%016llx\n",
            (unsigned long long)ch->index,
            (unsigned long long)ch->members.len,
            ch->needs_rebuild ? "yes" : "no",
            (unsigned long long)ch->fingerprint);
    }
}
