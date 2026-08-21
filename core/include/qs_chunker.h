/* qs_chunker.h — unity-build chunk management for all supported languages
 *
 * Chunking strategy:
 *   - Auto-enabled when a module has >= QS_CHUNK_THRESHOLD (150) source files.
 *   - Source files are grouped into "chunks" of QS_DEFAULT_CHUNK_SIZE (50) files.
 *   - For C/C++: a _chunk_N.c / _chunk_N.cpp is generated that #includes each member.
 *   - For C#:    a _chunk_N.cs is generated that is a partial class amalgam.
 *   - For Java:  javac is invoked per-chunk with a shared classpath.
 *   - For Python: per-chunk compile units are used for Cython extension modules.
 *   - For Go:    go build already handles packages; chunking groups packages.
 *   - For Ruby:  per-chunk require chains are generated for mruby embedding.
 *   - For Rust:  crate feature groups are used as logical chunks.
 *   - For Zig:   Zig's build graph handles this natively via @import groups.
 *
 * Incremental: each chunk has a fingerprint (xxhash of member file mtimes +
 * compiler flags). Only chunks whose fingerprint changed are recompiled.
 */
#ifndef QS_CHUNKER_H
#define QS_CHUNKER_H
#include "qs_types.h"
#include "qs_arena.h"
#include "qs_manifest.h"
#include "qs_diag.h"

typedef struct {
    qs_u64       index;         /* chunk number, 0-based */
    qs_str_vec_t members;       /* source file paths in this chunk */
    char        *generated_path;/* path to generated unity file (_chunk_N.ext) */
    qs_u64       fingerprint;   /* xxhash of member mtimes + flags */
    qs_bool_t    needs_rebuild; /* false = cached object still valid */
    char        *object_path;   /* output .o/.obj path */
} qs_chunk_t;

QS_VEC_DECL(qs_chunk_t, qs_chunk_vec)

typedef struct {
    qs_chunk_vec_t chunks;
    qs_size_t      total_sources;
    qs_size_t      chunk_size;
    qs_lang_t      lang;
    char          *out_dir;
} qs_chunk_plan_t;

/* Decide whether chunking should be enabled (checks threshold). */
qs_bool_t   qs_chunker_should_enable(const qs_manifest_t *m);

/* Build a chunk plan from the manifest's source list. */
qs_result_t qs_chunker_plan(qs_arena_t *a, const qs_manifest_t *m,
                              qs_chunk_plan_t *out, qs_diag_engine_t *diag);

/* Write generated unity files to disk. */
qs_result_t qs_chunker_write_c(qs_arena_t *a, qs_chunk_plan_t *plan,
                                 qs_diag_engine_t *diag);
qs_result_t qs_chunker_write_cpp(qs_arena_t *a, qs_chunk_plan_t *plan,
                                   qs_diag_engine_t *diag);
qs_result_t qs_chunker_write_csharp(qs_arena_t *a, qs_chunk_plan_t *plan,
                                      qs_diag_engine_t *diag);
qs_result_t qs_chunker_write_java(qs_arena_t *a, qs_chunk_plan_t *plan,
                                    qs_diag_engine_t *diag);
qs_result_t qs_chunker_write_python(qs_arena_t *a, qs_chunk_plan_t *plan,
                                      qs_diag_engine_t *diag);
qs_result_t qs_chunker_write_go(qs_arena_t *a, qs_chunk_plan_t *plan,
                                  qs_diag_engine_t *diag);
qs_result_t qs_chunker_write_ruby(qs_arena_t *a, qs_chunk_plan_t *plan,
                                    qs_diag_engine_t *diag);
qs_result_t qs_chunker_write_rust(qs_arena_t *a, qs_chunk_plan_t *plan,
                                    qs_diag_engine_t *diag);
qs_result_t qs_chunker_write_zig(qs_arena_t *a, qs_chunk_plan_t *plan,
                                   qs_diag_engine_t *diag);

/* Fingerprint all chunks in the plan and mark which need rebuild. */
qs_result_t qs_chunker_fingerprint(qs_arena_t *a, qs_chunk_plan_t *plan,
                                    const char *cache_dir);

void        qs_chunker_print_plan(const qs_chunk_plan_t *plan);
#endif /* QS_CHUNKER_H */
