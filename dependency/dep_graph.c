/*
 * dependency/dep_graph.c — Module dependency graph: build, validate, sort.
 * Supports multi-manifest workspaces where modules declare dependencies on
 * sibling modules by name. The graph is built from all workspace manifests,
 * cycle-checked, and topologically sorted so compilation order is correct.
 *
 * Each node = one module (one build.qs). Edges = dependency declarations.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_vec.h"
#include <string.h>
#include <stdio.h>

#define DEP_MAX_NODES 512

typedef struct {
    const qs_manifest_t *manifest;
    qs_size_t             dep_indices[DEP_MAX_NODES];
    qs_size_t             dep_count;
    qs_bool_t             visited;
    qs_bool_t             in_stack;  /* cycle detection */
    qs_bool_t             done;
} dep_node_t;

typedef struct {
    dep_node_t     nodes[DEP_MAX_NODES];
    qs_size_t      count;
    qs_size_t      topo_order[DEP_MAX_NODES];
    qs_size_t      topo_count;
    qs_arena_t    *arena;
    qs_diag_engine_t *diag;
} dep_graph_t;

void dep_graph_init(dep_graph_t *g, qs_arena_t *a, qs_diag_engine_t *d) {
    memset(g, 0, sizeof(*g));
    g->arena = a;
    g->diag  = d;
}

qs_result_t dep_graph_add(dep_graph_t *g, const qs_manifest_t *m) {
    if (g->count >= DEP_MAX_NODES) return QS_ERROR_INTERNAL;
    dep_node_t *n    = &g->nodes[g->count++];
    n->manifest      = m;
    n->dep_count     = 0;
    n->visited = n->in_stack = n->done = QS_FALSE;
    return QS_OK;
}

/* Resolve dep names to indices after all modules added */
qs_result_t dep_graph_resolve(dep_graph_t *g) {
    for (qs_size_t i = 0; i < g->count; i++) {
        dep_node_t *n = &g->nodes[i];
        const qs_manifest_t *m = n->manifest;
        for (qs_size_t d = 0; d < m->dependencies.len; d++) {
            qs_str_t dep_name = m->dependencies.data[d];
            qs_bool_t found   = QS_FALSE;
            for (qs_size_t j = 0; j < g->count; j++) {
                if (j == i) continue;
                if (qs_str_eq(g->nodes[j].manifest->name, dep_name)) {
                    if (n->dep_count < DEP_MAX_NODES)
                        n->dep_indices[n->dep_count++] = j;
                    found = QS_TRUE;
                    break;
                }
            }
            if (!found)
                qs_diag_emit_simple(g->diag, QS_SEV_WARNING, "dep_graph",
                    "QSB-DEP001", QS_LOC_UNKNOWN,
                    qs_arena_sprintf(g->arena,
                        "module '%.*s' depends on '%.*s' which is not in workspace",
                        (int)m->name.len,(const char*)m->name.ptr,
                        (int)dep_name.len,(const char*)dep_name.ptr));
        }
    }
    return QS_OK;
}

/* DFS-based cycle detection + topological sort (simultaneous) */
static qs_result_t dfs(dep_graph_t *g, qs_size_t idx) {
    dep_node_t *n = &g->nodes[idx];
    if (n->done)     return QS_OK;
    if (n->in_stack) {
        qs_diag_emit_simple(g->diag, QS_SEV_FATAL, "dep_graph",
            "QSB-DEP002", QS_LOC_UNKNOWN,
            qs_arena_sprintf(g->arena,
                "circular dependency detected involving module '%.*s'",
                (int)n->manifest->name.len,(const char*)n->manifest->name.ptr));
        return QS_ERROR_CYCLE;
    }
    n->in_stack = QS_TRUE;
    for (qs_size_t d = 0; d < n->dep_count; d++) {
        qs_result_t r = dfs(g, n->dep_indices[d]);
        if (r != QS_OK) return r;
    }
    n->in_stack = QS_FALSE;
    n->done     = QS_TRUE;
    g->topo_order[g->topo_count++] = idx;
    return QS_OK;
}

qs_result_t dep_graph_toposort(dep_graph_t *g) {
    g->topo_count = 0;
    for (qs_size_t i = 0; i < g->count; i++) {
        qs_result_t r = dfs(g, i);
        if (r != QS_OK) return r;
    }
    return QS_OK;
}

void dep_graph_print(const dep_graph_t *g) {
    fprintf(stderr,"[dep_graph] %llu modules (topo order):\n",
        (unsigned long long)g->count);
    for (qs_size_t i = 0; i < g->topo_count; i++) {
        const dep_node_t *n = &g->nodes[g->topo_order[i]];
        fprintf(stderr,"  [%llu] %.*s  (deps=%llu)\n",
            (unsigned long long)i,
            (int)n->manifest->name.len,(const char*)n->manifest->name.ptr,
            (unsigned long long)n->dep_count);
    }
}
