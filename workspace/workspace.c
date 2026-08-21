/*
 * workspace/workspace.c — Multi-module workspace management.
 * A workspace is a directory containing multiple modules (each with a build.qs).
 * The workspace manager discovers all manifests, resolves inter-module
 * dependencies, determines build order, and drives the pipeline per module.
 *
 * Workspace discovery: finds all build.qs files recursively, up to
 * QS_WORKSPACE_MAX_DEPTH (5) levels deep, skipping common non-source dirs.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_vec.h"
#include "../core/include/qs_path.h"
#include <string.h>
#include <stdio.h>

#define QS_WORKSPACE_MAX_DEPTH 5

QS_VEC_DECL(qs_manifest_t, qs_manifest_vec)

typedef struct {
    char              *root;          /* workspace root directory */
    qs_manifest_vec_t  modules;       /* all discovered modules */
    qs_str_vec_t       manifest_paths;/* paths to build.qs files */
    qs_arena_t        *arena;
    qs_diag_engine_t  *diag;
} qs_workspace_t;

void qs_workspace_init(qs_workspace_t *ws, qs_arena_t *a,
                        qs_diag_engine_t *d, const char *root) {
    memset(ws, 0, sizeof(*ws));
    ws->root  = qs_arena_strdup(a, root);
    ws->arena = a;
    ws->diag  = d;
    qs_manifest_vec_init(&ws->modules, a);
    qs_str_vec_init(&ws->manifest_paths, a);
}

/* Recursively find all build.qs files */
static void find_manifests(qs_arena_t *a, const char *dir, int depth,
                             qs_str_vec_t *out) {
    if (depth > QS_WORKSPACE_MAX_DEPTH) return;
    qs_str_vec_t entries; qs_str_vec_init(&entries, a);
    if (qs_fs_list_dir(a, dir, &entries) != QS_OK) return;
    for (qs_size_t i = 0; i < entries.len; i++) {
        char *p = qs_arena_str_to_cstr(a, entries.data[i]);
        qs_str_t base = qs_path_basename(qs_str_from_cstr(p));
        /* Skip hidden and known non-source dirs */
        if (base.len>0&&((const char*)base.ptr)[0]=='.') continue;
        static const char *SKIP[]={"out","target","node_modules","vendor",NULL};
        qs_bool_t skip=QS_FALSE;
        for(int s=0;SKIP[s];s++) if(qs_str_eq_cstr(base,SKIP[s])){skip=QS_TRUE;break;}
        if(skip) continue;
        if (qs_fs_is_dir(p)) {
            find_manifests(a, p, depth+1, out);
        } else if (qs_str_eq_cstr(base, "build.qs")) {
            qs_str_vec_push(out, qs_str_from_cstr(qs_arena_strdup(a,p)));
        }
    }
}

qs_result_t qs_workspace_discover(qs_workspace_t *ws) {
    find_manifests(ws->arena, ws->root, 0, &ws->manifest_paths);
    fprintf(stderr, "[workspace] found %llu module(s) in %s\n",
        (unsigned long long)ws->manifest_paths.len, ws->root);

    for (qs_size_t i = 0; i < ws->manifest_paths.len; i++) {
        char *mp = qs_arena_str_to_cstr(ws->arena, ws->manifest_paths.data[i]);
        qs_manifest_t m;
        qs_result_t r = qs_manifest_parse(ws->arena, mp, &m, ws->diag);
        if (r != QS_OK) continue;
        qs_manifest_apply_auto_rules(&m);
        qs_manifest_vec_push(&ws->modules, m);
    }
    return QS_OK;
}

void qs_workspace_print(const qs_workspace_t *ws) {
    fprintf(stderr,"[workspace] %llu modules:\n",
        (unsigned long long)ws->modules.len);
    for (qs_size_t i=0;i<ws->modules.len;i++) {
        const qs_manifest_t *m=&ws->modules.data[i];
        fprintf(stderr,"  %.*s  (%s, %llu src)\n",
            (int)m->name.len,(const char*)m->name.ptr,
            qs_lang_name(m->language),(unsigned long long)m->sources.len);
    }
}
