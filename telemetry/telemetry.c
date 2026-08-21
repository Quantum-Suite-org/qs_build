/*
 * telemetry/telemetry.c — Optional anonymous build telemetry.
 * Collects: build duration, language, output type, platform, success/fail,
 * source file count, cache hit rate. Never collects source paths or content.
 * Disabled by default; enabled via QS_BUILD_TELEMETRY_URL env var.
 * Sends a single HTTP POST with a JSON payload. No third-party libs.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_cache.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "qs_process.h"

typedef struct {
    qs_bool_t enabled;
    char     *endpoint;    /* HTTP POST endpoint URL */
    char     *session_id;  /* random hex ID per build session */
} qs_telemetry_t;

static char *random_hex16(qs_arena_t *a) {
    /* Use time + pointer as entropy source (good enough for session IDs) */
    qs_u64 v=(qs_u64)time(NULL)^(qs_u64)(qs_size_t)a;
    char *s=qs_arena_alloc(a,17,1);
    static const char H[]="0123456789abcdef";
    for(int i=15;i>=0;i--){s[i]=H[v&0xF];v>>=4;} s[16]='\0';
    return s;
}

void qs_telemetry_init(qs_arena_t *a, qs_telemetry_t *t) {
    memset(t,0,sizeof(*t));
    const char *url=getenv("QS_BUILD_TELEMETRY_URL");
    if (!url) { t->enabled=QS_FALSE; return; }
    t->enabled   =QS_TRUE;
    t->endpoint  =qs_arena_strdup(a,url);
    t->session_id=random_hex16(a);
}

void qs_telemetry_report(qs_arena_t *a, qs_telemetry_t *t,
                           const qs_manifest_t *m, const qs_cache_t *cache,
                           qs_u64 build_time_us, qs_bool_t success) {
    if (!t->enabled) return;
    /* Build JSON payload — no PII, no source paths */
    qs_u64 hits=cache?cache->hits:0, misses=cache?cache->misses:0;
    double hit_rate=(hits+misses)>0
        ?(double)hits/(double)(hits+misses)*100.0:0.0;
    char payload[2048];
    snprintf(payload,sizeof(payload),
        "{\"session\":\"%s\","
        "\"lang\":\"%s\","
        "\"output_type\":\"%s\","
        "\"sources\":%llu,"
        "\"build_time_us\":%llu,"
        "\"cache_hit_rate\":%.1f,"
        "\"success\":%s,"
        "\"pch\":%s,"
        "\"chunks\":%s,"
        "\"version\":\"1.0.0-alpha\"}",
        t->session_id,
        qs_lang_name(m->language),
        qs_output_type_str(m->output_type),
        (unsigned long long)m->sources.len,
        (unsigned long long)build_time_us,
        hit_rate,
        success?"true":"false",
        m->enable_pch?"true":"false",
        m->enable_chunks?"true":"false");

    /* Send via process: curl or wget if available */
    char *curl=qs_proc_find_in_path(a,"curl");
    if (!curl) { curl=qs_proc_find_in_path(a,"wget"); }
    if (!curl) return;

    const char *argv[16]; int ac=0;
    if (strstr(curl,"curl")) {
        argv[ac++]=curl;
        argv[ac++]="-s"; argv[ac++]="-X"; argv[ac++]="POST";
        argv[ac++]="-H"; argv[ac++]="Content-Type: application/json";
        argv[ac++]="-d"; argv[ac++]=payload;
        argv[ac++]=t->endpoint;
        argv[ac]=NULL;
    } else {
        argv[ac++]=curl; argv[ac++]="-q";
        argv[ac++]="--post-data"; argv[ac++]=payload;
        argv[ac++]=t->endpoint; argv[ac]=NULL;
    }
    /* Fire-and-forget: don't block the build on telemetry */
    qs_proc_result_t pr;
    qs_proc_run(a,argv,NULL,3000000ULL,&pr);
}
