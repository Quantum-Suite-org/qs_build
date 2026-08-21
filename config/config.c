/*
 * config/config.c — Global qs_build configuration.
 * Reads (in priority order):
 *   1. Command-line flags (highest priority)
 *   2. Environment variables (QS_BUILD_*)
 *   3. <workspace>/.qsbuildrc   (TOML-lite key=value file)
 *   4. Built-in defaults        (lowest priority)
 *
 * Merges all sources into a single qs_config_t struct that the
 * pipeline uses throughout the build.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_str.h"
#include "../core/include/qs_cli.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

typedef struct {
    /* Parallelism */
    qs_u64    jobs;              /* 0 = auto */
    /* Paths */
    char     *cache_dir;        /* default: <out>/.qs_cache */
    char     *tmp_dir;          /* default: system temp */
    char     *log_file;         /* NULL = no file log */
    /* Behavior */
    qs_bool_t color;
    qs_bool_t verbose;
    qs_bool_t timestamps;
    qs_bool_t show_commands;
    qs_bool_t incremental;
    qs_u64    error_limit;
    qs_u64    cache_max_gb;     /* max cache size in GB */
    /* Remote cache */
    char     *remote_cache_url;
    char     *remote_cache_token;
    /* Forge:Visuals integration */
    qs_bool_t emit_reflect_json; /* emit reflection manifest for Forge:Visuals */
    qs_bool_t emit_build_info;   /* embed build metadata */
    char     *telemetry_path;    /* JSON timing file; NULL = disabled */
} qs_config_t;

static void config_defaults(qs_config_t *c) {
    memset(c, 0, sizeof(*c));
    c->jobs          = 0;        /* auto */
    c->color         = QS_TRUE;
    c->incremental   = QS_TRUE;
    c->timestamps    = QS_TRUE;
    c->cache_max_gb  = 4;
    c->emit_build_info = QS_TRUE;
}

static void config_from_env(qs_arena_t *a, qs_config_t *c) {
    const char *v;
    if ((v=getenv("QS_BUILD_JOBS"))&&*v)        c->jobs=strtoul(v,NULL,10);
    if ((v=getenv("QS_BUILD_NO_COLOR"))&&*v)    c->color=QS_FALSE;
    if ((v=getenv("QS_BUILD_VERBOSE"))&&*v)     c->verbose=QS_TRUE;
    if ((v=getenv("QS_BUILD_NO_CACHE"))&&*v)    c->incremental=QS_FALSE;
    if ((v=getenv("QS_BUILD_CACHE_DIR"))&&*v)   c->cache_dir=qs_arena_strdup(a,v);
    if ((v=getenv("QS_BUILD_LOG"))&&*v)         c->log_file=qs_arena_strdup(a,v);
    if ((v=getenv("QS_REMOTE_CACHE_URL"))&&*v)  c->remote_cache_url=qs_arena_strdup(a,v);
    if ((v=getenv("QS_REMOTE_CACHE_TOKEN"))&&*v)c->remote_cache_token=qs_arena_strdup(a,v);
    if ((v=getenv("QS_BUILD_TELEMETRY"))&&*v)   c->telemetry_path=qs_arena_strdup(a,v);
}

/* Parse a simple key=value .qsbuildrc file */
static void config_from_file(qs_arena_t *a, qs_config_t *c, const char *path) {
    char *text=NULL; qs_size_t tlen=0;
    if (qs_fs_read_file(a,path,&text,&tlen)!=QS_OK) return;
    char *p=text;
    while(*p) {
        while(*p==' '||*p=='\t') p++;
        if (*p=='#'||*p=='\n'||*p=='\r') { while(*p&&*p!='\n') p++; continue; }
        char *key=p; while(*p&&*p!='='&&*p!='\n') p++;
        if (*p!='=') { while(*p&&*p!='\n') p++; continue; }
        qs_size_t kl=(qs_size_t)(p-key);
        p++; /* skip = */
        char *val=p; while(*p&&*p!='\n'&&*p!='\r') p++;
        qs_size_t vl=(qs_size_t)(p-val);
        /* Trim */
        while(kl&&isspace((unsigned char)key[kl-1])) kl--;
        while(vl&&isspace((unsigned char)val[vl-1])) vl--;
        /* Match known keys */
        char k[64],v2[256];
        if(kl<63){memcpy(k,key,kl);k[kl]='\0';}else k[0]='\0';
        if(vl<255){memcpy(v2,val,vl);v2[vl]='\0';}else v2[0]='\0';
        if(!strcmp(k,"jobs"))          c->jobs=strtoul(v2,NULL,10);
        else if(!strcmp(k,"color"))    c->color=strcmp(v2,"false")!=0?QS_TRUE:QS_FALSE;
        else if(!strcmp(k,"verbose"))  c->verbose=strcmp(v2,"true")==0;
        else if(!strcmp(k,"incremental")) c->incremental=strcmp(v2,"false")!=0?QS_TRUE:QS_FALSE;
        else if(!strcmp(k,"log_file")) c->log_file=qs_arena_strdup(a,v2);
        else if(!strcmp(k,"cache_max_gb")) c->cache_max_gb=strtoul(v2,NULL,10);
        else if(!strcmp(k,"telemetry")) c->telemetry_path=qs_arena_strdup(a,v2);
    }
}

void config_from_cli(qs_config_t *c, const qs_cli_args_t *args) {
    if (args->jobs)         c->jobs          = args->jobs;
    if (!args->color)       c->color         = QS_FALSE;
    if (args->verbose)      c->verbose       = QS_TRUE;
    if (args->show_commands)c->show_commands = QS_TRUE;
    if (!args->incremental) c->incremental   = QS_FALSE;
    if (args->error_limit)  c->error_limit   = args->error_limit;
}

qs_result_t qs_config_load(qs_arena_t *a, const qs_cli_args_t *args,
                             const char *workspace_dir, qs_config_t *out) {
    config_defaults(out);
    config_from_env(a, out);
    char *rcpath = qs_arena_sprintf(a, "%s/.qsbuildrc",
        workspace_dir ? workspace_dir : ".");
    if (qs_fs_exists(rcpath)) config_from_file(a, out, rcpath);
    config_from_cli(out, args);
    return QS_OK;
}

void qs_config_print(const qs_config_t *c) {
    fprintf(stderr,"[config]\n");
    fprintf(stderr,"  jobs=%llu  color=%s  verbose=%s  incremental=%s\n",
        (unsigned long long)c->jobs,
        c->color?"yes":"no", c->verbose?"yes":"no", c->incremental?"yes":"no");
    fprintf(stderr,"  cache_max_gb=%llu  error_limit=%llu\n",
        (unsigned long long)c->cache_max_gb,(unsigned long long)c->error_limit);
    if (c->remote_cache_url) fprintf(stderr,"  remote_cache=%s\n",c->remote_cache_url);
    if (c->log_file)         fprintf(stderr,"  log_file=%s\n",c->log_file);
}
