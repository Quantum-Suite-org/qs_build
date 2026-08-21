/*
 * monitoring/monitor.c — Build progress monitor and telemetry.
 * Reports per-phase timing, file counts, cache stats, and peak memory
 * to stderr (human-readable) and optionally to a JSON telemetry file.
 * No external dependencies.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_fs.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    char     name[64];
    qs_u64   start_us;
    qs_u64   end_us;
    qs_u64   items;       /* files compiled, bytes written, etc. */
    qs_bool_t done;
} qs_phase_t;

#define MAX_PHASES 32

typedef struct {
    qs_phase_t  phases[MAX_PHASES];
    qs_size_t   phase_count;
    qs_u64      build_start_us;
    char       *telemetry_path; /* if set, write JSON here at end */
    qs_bool_t   color;
} qs_monitor_t;

static qs_u64 mono_us(void) {
    struct timespec ts;
#if defined(_WIN32)
    return (qs_u64)time(NULL)*1000000ULL;
#else
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return (qs_u64)ts.tv_sec*1000000ULL+(qs_u64)(ts.tv_nsec/1000);
#endif
}

void qs_monitor_init(qs_monitor_t *m, qs_bool_t color, const char *telemetry_path) {
    memset(m,0,sizeof(*m));
    m->build_start_us = mono_us();
    m->color          = color;
    m->telemetry_path = telemetry_path?(char*)telemetry_path:NULL;
}

qs_size_t qs_monitor_phase_begin(qs_monitor_t *m, const char *name) {
    if (m->phase_count >= MAX_PHASES) return MAX_PHASES-1;
    qs_phase_t *p = &m->phases[m->phase_count];
    strncpy(p->name, name, sizeof(p->name)-1);
    p->start_us = mono_us();
    p->done     = QS_FALSE;
    p->items    = 0;
    return m->phase_count++;
}

void qs_monitor_phase_end(qs_monitor_t *m, qs_size_t idx, qs_u64 items) {
    if (idx >= m->phase_count) return;
    qs_phase_t *p = &m->phases[idx];
    p->end_us = mono_us();
    p->items  = items;
    p->done   = QS_TRUE;
    qs_u64 dur = p->end_us - p->start_us;
    const char *clr  = m->color?"\033[36m":"";
    const char *rst  = m->color?"\033[0m":"";
    fprintf(stderr,"  %s%-18s%s  %llu.%03llus",
        clr,p->name,rst,dur/1000000ULL,(dur%1000000ULL)/1000ULL);
    if (items) fprintf(stderr,"  (%llu items)",(unsigned long long)items);
    fprintf(stderr,"\n");
}

void qs_monitor_finish(qs_monitor_t *m) {
    qs_u64 total = mono_us() - m->build_start_us;
    const char *bold = m->color?"\033[1m":"";
    const char *rst  = m->color?"\033[0m":"";
    fprintf(stderr,"\n  %stotal: %llu.%03llus%s\n",
        bold,total/1000000ULL,(total%1000000ULL)/1000ULL,rst);

    if (!m->telemetry_path) return;
    FILE *f = fopen(m->telemetry_path,"w"); if(!f) return;
    fprintf(f,"{\n  \"build_time_us\": %llu,\n  \"phases\": [\n",
        (unsigned long long)total);
    for (qs_size_t i=0;i<m->phase_count;i++) {
        const qs_phase_t *p=&m->phases[i];
        qs_u64 dur=p->end_us-p->start_us;
        fprintf(f,"    {\"name\":\"%s\",\"duration_us\":%llu,\"items\":%llu}%s\n",
            p->name,(unsigned long long)dur,(unsigned long long)p->items,
            i+1<m->phase_count?",":"");
    }
    fprintf(f,"  ]\n}\n");
    fclose(f);
}
