/*
 * logging/logger.c — Structured build logger.
 * Writes timestamped, levelled log entries to stderr and optionally to a
 * log file in <out_dir>/qs_build.log. Used for build telemetry, timing,
 * and post-mortem analysis. Separate from the diagnostic engine (which
 * is for user-facing compiler errors).
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

typedef enum { LOG_DEBUG=0, LOG_INFO, LOG_WARN, LOG_ERROR } log_level_t;

typedef struct {
    FILE      *file;        /* log file; may be NULL */
    log_level_t min_level;
    qs_bool_t   color;
    qs_bool_t   timestamps;
} qs_logger_t;

static qs_logger_t g_log = { NULL, LOG_INFO, QS_TRUE, QS_TRUE };

void qs_log_init(const char *log_path, log_level_t min_level,
                  qs_bool_t color, qs_bool_t timestamps) {
    g_log.min_level  = min_level;
    g_log.color      = color;
    g_log.timestamps = timestamps;
    if (log_path) g_log.file = fopen(log_path,"a");
}

void qs_log_close(void) {
    if (g_log.file) { fclose(g_log.file); g_log.file=NULL; }
}

static const char *level_str(log_level_t l) {
    switch(l){
        case LOG_DEBUG: return "DBG";
        case LOG_INFO:  return "INF";
        case LOG_WARN:  return "WRN";
        case LOG_ERROR: return "ERR";
        default:        return "???";
    }
}
static const char *level_colour(log_level_t l) {
    switch(l){
        case LOG_DEBUG: return "\033[90m";
        case LOG_INFO:  return "\033[36m";
        case LOG_WARN:  return "\033[33m";
        case LOG_ERROR: return "\033[31m";
        default:        return "";
    }
}

static void emit(log_level_t level, const char *fmt, va_list ap) {
    if (level < g_log.min_level) return;
    char ts[32]="";
    if (g_log.timestamps) {
        time_t t=time(NULL);
        struct tm *tm=localtime(&t);
        strftime(ts,sizeof(ts),"%H:%M:%S",tm);
    }
    char msg[4096];
    vsnprintf(msg,sizeof(msg),fmt,ap);

    /* stderr */
    if (g_log.timestamps)
        fprintf(stderr,"%s%s [%s]%s %s\n",
            g_log.color?level_colour(level):"",
            ts,level_str(level),
            g_log.color?"\033[0m":"",
            msg);
    else
        fprintf(stderr,"%s[%s]%s %s\n",
            g_log.color?level_colour(level):"",
            level_str(level),
            g_log.color?"\033[0m":"",
            msg);

    /* log file (no colour) */
    if (g_log.file)
        fprintf(g_log.file,"%s [%s] %s\n",ts,level_str(level),msg);
}

void qs_log_debug(const char *fmt,...){va_list a;va_start(a,fmt);emit(LOG_DEBUG,fmt,a);va_end(a);}
void qs_log_info (const char *fmt,...){va_list a;va_start(a,fmt);emit(LOG_INFO, fmt,a);va_end(a);}
void qs_log_warn (const char *fmt,...){va_list a;va_start(a,fmt);emit(LOG_WARN, fmt,a);va_end(a);}
void qs_log_error(const char *fmt,...){va_list a;va_start(a,fmt);emit(LOG_ERROR,fmt,a);va_end(a);}
