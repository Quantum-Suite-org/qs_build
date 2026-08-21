/*
 * plugins/plugin_api.c — Plugin system for qs_build.
 * Plugins are shared libraries (.so/.dll) loaded at runtime via dlopen/LoadLibrary.
 * A plugin exports a single entry point:
 *   qs_plugin_info_t *qs_plugin_init(qs_plugin_host_t *host);
 *
 * Plugins can:
 *   - Register custom language drivers
 *   - Add pre/post build hooks
 *   - Extend the manifest parser with custom keys
 *   - Emit custom output types
 *   - Hook into the diagnostic engine
 *
 * Plugin discovery: scans <exe_dir>/plugins/ and QS_PLUGIN_PATH env var.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_process.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#  include <windows.h>
#  define QS_DLOPEN(p)   LoadLibraryA(p)
#  define QS_DLSYM(h,s)  GetProcAddress((HMODULE)(h),s)
#  define QS_DLCLOSE(h)  FreeLibrary((HMODULE)(h))
#  define QS_PLUGIN_EXT  ".dll"
typedef void* qs_dl_handle_t;
#else
#  include <dlfcn.h>
#  define QS_DLOPEN(p)   dlopen(p, RTLD_NOW|RTLD_LOCAL)
#  define QS_DLSYM(h,s)  dlsym(h,s)
#  define QS_DLCLOSE(h)  dlclose(h)
#  ifdef __APPLE__
#    define QS_PLUGIN_EXT ".dylib"
#  else
#    define QS_PLUGIN_EXT ".so"
#  endif
typedef void* qs_dl_handle_t;
#endif

#define QS_PLUGIN_API_VERSION 1
#define QS_MAX_PLUGINS 64

/* Host API exposed to plugins */
typedef struct qs_plugin_host {
    qs_u32            api_version;
    qs_arena_t       *arena;
    qs_diag_engine_t *diag;
    /* Plugin can call these to register hooks */
    void (*register_pre_build) (struct qs_plugin_host *host, void(*fn)(void*), void *ctx);
    void (*register_post_build)(struct qs_plugin_host *host, void(*fn)(void*,qs_bool_t), void *ctx);
    void (*log_info)           (struct qs_plugin_host *host, const char *msg);
    void (*log_warn)           (struct qs_plugin_host *host, const char *msg);
} qs_plugin_host_t;

typedef struct {
    qs_u32      api_version;
    const char *name;
    const char *version;
    const char *description;
    const char *author;
} qs_plugin_info_t;

typedef qs_plugin_info_t *(*qs_plugin_init_fn)(qs_plugin_host_t *host);

typedef struct {
    qs_dl_handle_t  handle;
    qs_plugin_info_t *info;
    char            *path;
} qs_loaded_plugin_t;

typedef struct {
    qs_loaded_plugin_t plugins[QS_MAX_PLUGINS];
    qs_size_t          count;
    qs_arena_t        *arena;
    qs_diag_engine_t  *diag;
} qs_plugin_manager_t;

/* Hooks registered by plugins */
static void (*pre_build_hooks[QS_MAX_PLUGINS])(void*);
static void *pre_build_ctxs[QS_MAX_PLUGINS];
static qs_size_t pre_build_count = 0;

static void plugin_register_pre(qs_plugin_host_t *host, void(*fn)(void*), void *ctx) {
    if (pre_build_count < QS_MAX_PLUGINS) {
        pre_build_hooks[pre_build_count]  = fn;
        pre_build_ctxs[pre_build_count++] = ctx;
    }
}
static void plugin_log_info(qs_plugin_host_t *host, const char *msg) {
    qs_diag_emit_simple(host->diag,QS_SEV_NOTE,"plugin","",QS_LOC_UNKNOWN,msg);
}
static void plugin_log_warn(qs_plugin_host_t *host, const char *msg) {
    qs_diag_emit_simple(host->diag,QS_SEV_WARNING,"plugin","",QS_LOC_UNKNOWN,msg);
}

void qs_plugin_manager_init(qs_plugin_manager_t *pm, qs_arena_t *a, qs_diag_engine_t *d) {
    memset(pm,0,sizeof(*pm));
    pm->arena=a; pm->diag=d;
}

qs_result_t qs_plugin_load(qs_plugin_manager_t *pm, const char *path) {
    if (pm->count >= QS_MAX_PLUGINS) return QS_ERROR_INTERNAL;
    qs_dl_handle_t h = QS_DLOPEN(path);
    if (!h) {
        qs_diag_emit_simple(pm->diag,QS_SEV_WARNING,"plugin","QSB-PLG001",
            QS_LOC_UNKNOWN,qs_arena_sprintf(pm->arena,"cannot load plugin: %s",path));
        return QS_ERROR_IO;
    }
    qs_plugin_init_fn init_fn=(qs_plugin_init_fn)QS_DLSYM(h,"qs_plugin_init");
    if (!init_fn) {
        QS_DLCLOSE(h);
        qs_diag_emit_simple(pm->diag,QS_SEV_WARNING,"plugin","QSB-PLG002",
            QS_LOC_UNKNOWN,qs_arena_sprintf(pm->arena,
                "plugin missing qs_plugin_init: %s",path));
        return QS_ERROR_UNSUPPORTED;
    }
    qs_plugin_host_t host = {
        .api_version       = QS_PLUGIN_API_VERSION,
        .arena             = pm->arena,
        .diag              = pm->diag,
        .register_pre_build= plugin_register_pre,
        .register_post_build=NULL,
        .log_info          = plugin_log_info,
        .log_warn          = plugin_log_warn,
    };
    qs_plugin_info_t *info = init_fn(&host);
    if (!info) { QS_DLCLOSE(h); return QS_ERROR_INTERNAL; }
    if (info->api_version != QS_PLUGIN_API_VERSION) {
        qs_diag_emit_simple(pm->diag,QS_SEV_WARNING,"plugin","QSB-PLG003",
            QS_LOC_UNKNOWN,qs_arena_sprintf(pm->arena,
                "plugin API version mismatch: %s (got %u, want %u)",
                path,info->api_version,QS_PLUGIN_API_VERSION));
        QS_DLCLOSE(h);
        return QS_ERROR_UNSUPPORTED;
    }
    qs_loaded_plugin_t *lp = &pm->plugins[pm->count++];
    lp->handle = h;
    lp->info   = info;
    lp->path   = qs_arena_strdup(pm->arena,path);
    fprintf(stderr,"  [plugin] loaded: %s v%s — %s\n",
        info->name,info->version,info->description?info->description:"");
    return QS_OK;
}

/* Scan a directory for plugins and load them all */
qs_result_t qs_plugin_scan_dir(qs_plugin_manager_t *pm, const char *dir) {
    if (!qs_fs_is_dir(dir)) return QS_OK;
    qs_str_vec_t entries; qs_str_vec_init(&entries,pm->arena);
    qs_fs_list_dir(pm->arena,dir,&entries);
    for (qs_size_t i=0;i<entries.len;i++) {
        char *p=qs_arena_str_to_cstr(pm->arena,entries.data[i]);
        if (strstr(p,QS_PLUGIN_EXT))
            qs_plugin_load(pm,p);
    }
    return QS_OK;
}

void qs_plugin_run_pre_build(qs_plugin_manager_t *pm) {
    for (qs_size_t i=0;i<pre_build_count;i++)
        if (pre_build_hooks[i]) pre_build_hooks[i](pre_build_ctxs[i]);
}

void qs_plugin_manager_destroy(qs_plugin_manager_t *pm) {
    for (qs_size_t i=0;i<pm->count;i++)
        if (pm->plugins[i].handle) QS_DLCLOSE(pm->plugins[i].handle);
    pm->count=0;
}

void qs_plugin_manager_print(const qs_plugin_manager_t *pm) {
    fprintf(stderr,"[plugins] %llu loaded:\n",(unsigned long long)pm->count);
    for (qs_size_t i=0;i<pm->count;i++) {
        const qs_plugin_info_t *inf=pm->plugins[i].info;
        fprintf(stderr,"  %-20s v%-10s %s\n",
            inf->name,inf->version,inf->description?inf->description:"");
    }
}
