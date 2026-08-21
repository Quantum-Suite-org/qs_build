/*
 * scheduler/scheduler.c — Dependency-aware job scheduler.
 * Builds a DAG from manifest dependencies, topologically sorts it,
 * then dispatches modules in parallel respecting dependency order.
 * Uses the thread pool (jobs/pool.c) for parallel execution.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_jobs.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_vec.h"
#include <string.h>
#include <stdio.h>

#define MAX_NODES 1024

typedef struct {
    const char  *name;
    qs_size_t    dep_indices[MAX_NODES];
    qs_size_t    dep_count;
    qs_bool_t    done;
    qs_bool_t    in_progress;
    qs_bool_t    failed;
} sched_node_t;

typedef struct {
    sched_node_t  nodes[MAX_NODES];
    qs_size_t     count;
    qs_arena_t   *arena;
    qs_diag_engine_t *diag;
} qs_scheduler_t;

void qs_scheduler_init(qs_scheduler_t *s, qs_arena_t *a, qs_diag_engine_t *d) {
    memset(s, 0, sizeof(*s));
    s->arena = a;
    s->diag  = d;
}

qs_result_t qs_scheduler_add(qs_scheduler_t *s, const char *name,
                               const char *const *deps, qs_size_t dep_count) {
    if (s->count >= MAX_NODES) return QS_ERROR_INTERNAL;
    sched_node_t *n = &s->nodes[s->count++];
    n->name      = name;
    n->dep_count = 0;
    n->done      = QS_FALSE;
    n->failed    = QS_FALSE;
    /* Resolve dep names to indices */
    for (qs_size_t d = 0; d < dep_count && n->dep_count < MAX_NODES; d++) {
        for (qs_size_t i = 0; i < s->count - 1; i++) {
            if (strcmp(s->nodes[i].name, deps[d]) == 0) {
                n->dep_indices[n->dep_count++] = i;
                break;
            }
        }
    }
    return QS_OK;
}

/* Returns true if all dependencies of node i are done */
static qs_bool_t deps_satisfied(const qs_scheduler_t *s, qs_size_t i) {
    const sched_node_t *n = &s->nodes[i];
    for (qs_size_t d = 0; d < n->dep_count; d++) {
        if (!s->nodes[n->dep_indices[d]].done) return QS_FALSE;
    }
    return QS_TRUE;
}

/* Returns true if any dependency of node i has failed */
static qs_bool_t dep_failed(const qs_scheduler_t *s, qs_size_t i) {
    const sched_node_t *n = &s->nodes[i];
    for (qs_size_t d = 0; d < n->dep_count; d++) {
        if (s->nodes[n->dep_indices[d]].failed) return QS_TRUE;
    }
    return QS_FALSE;
}

/* Topological sort via Kahn's algorithm; detects cycles */
qs_result_t qs_scheduler_toposort(qs_scheduler_t *s, qs_size_t *order_out) {
    qs_size_t in_degree[MAX_NODES] = {0};
    qs_size_t queue[MAX_NODES];
    qs_size_t qhead = 0, qtail = 0, sorted = 0;

    /* Compute in-degrees */
    for (qs_size_t i = 0; i < s->count; i++)
        for (qs_size_t d = 0; d < s->nodes[i].dep_count; d++)
            in_degree[i]++;

    /* Enqueue nodes with no dependencies */
    for (qs_size_t i = 0; i < s->count; i++)
        if (!in_degree[i]) queue[qtail++] = i;

    while (qhead < qtail) {
        qs_size_t cur = queue[qhead++];
        order_out[sorted++] = cur;
        /* Reduce in-degree for nodes that depend on cur */
        for (qs_size_t i = 0; i < s->count; i++) {
            for (qs_size_t d = 0; d < s->nodes[i].dep_count; d++) {
                if (s->nodes[i].dep_indices[d] == cur) {
                    if (--in_degree[i] == 0)
                        queue[qtail++] = i;
                }
            }
        }
    }

    if (sorted != s->count) {
        qs_diag_emit_simple(s->diag, QS_SEV_FATAL, "scheduler", "QSB-SCH001",
            QS_LOC_UNKNOWN, "circular dependency detected in module graph");
        return QS_ERROR_CYCLE;
    }
    return QS_OK;
}

void qs_scheduler_print(const qs_scheduler_t *s) {
    fprintf(stderr, "[scheduler] %llu modules:\n", (unsigned long long)s->count);
    for (qs_size_t i = 0; i < s->count; i++) {
        const sched_node_t *n = &s->nodes[i];
        fprintf(stderr, "  %s  (deps=%llu)\n",
            n->name, (unsigned long long)n->dep_count);
    }
}
