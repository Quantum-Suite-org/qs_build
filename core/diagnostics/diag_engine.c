/*
 * diag_engine.c — Core diagnostic engine: collect, format, emit.
 * All language-driver parsers call qs_diag_emit() to submit structured
 * diagnostics. The engine pretty-prints with source carets, colour, JSON,
 * and a final error/warning summary — like clang -fdiagnostics-color but
 * across every language we support.
 */
#include "qs_diag.h"
#include "qs_arena.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

void qs_diag_engine_init(qs_diag_engine_t *e, qs_arena_t *arena) {
    memset(e,0,sizeof(*e));
    e->arena=arena;
    qs_diag_vec_init(&e->diags,arena);
    e->color=QS_TRUE;
    e->show_caret=QS_TRUE;
    e->error_limit=0;
}

void qs_diag_emit(qs_diag_engine_t *e, qs_diag_t d) {
    if (e->limit_reached) return;
    e->counts[(int)d.sev]++;
    qs_diag_vec_push(&e->diags,d);
    if (e->on_emit) e->on_emit(&d,e->on_emit_ctx);
    if (e->error_limit>0 && e->counts[QS_SEV_ERROR]+e->counts[QS_SEV_FATAL]>=e->error_limit)
        e->limit_reached=QS_TRUE;
}

void qs_diag_emit_simple(qs_diag_engine_t *e, qs_severity_t sev,
                          const char *tool, const char *code,
                          qs_loc_t loc, const char *message) {
    qs_diag_t d; memset(&d,0,sizeof(d));
    d.sev=sev;
    d.tool=qs_str_from_cstr(tool);
    d.code=qs_str_from_cstr(code?code:"");
    d.loc=loc;
    d.message=qs_str_from_cstr(message);
    qs_diag_emit(e,d);
}

/* Print one diagnostic to stderr with full context. */
static void print_one(const qs_diag_t *d, qs_bool_t color, qs_bool_t show_caret,
                        qs_bool_t show_raw) {
    const char *sev_str = qs_severity_str(d->sev);
    const char *col     = color ? qs_severity_colour(d->sev) : "";
    const char *bold    = color ? QS_COLOUR_BOLD  : "";
    const char *reset   = color ? QS_COLOUR_RESET : "";

    /* ── Header line: file:line:col: severity: message ──────────────────── */
    if (!QS_STR_IS_EMPTY(d->loc.file)) {
        fprintf(stderr,"%s%.*s",bold,(int)d->loc.file.len,(const char*)d->loc.file.ptr);
        if (d->loc.line)   fprintf(stderr,":%llu",(unsigned long long)d->loc.line);
        if (d->loc.col)    fprintf(stderr,":%llu",(unsigned long long)d->loc.col);
        fprintf(stderr,": ");
    }
    fprintf(stderr,"%s%s%s%s: ",col,bold,sev_str,reset);
    if (!QS_STR_IS_EMPTY(d->code))
        fprintf(stderr,"[%.*s] ",(int)d->code.len,(const char*)d->code.ptr);
    fprintf(stderr,"%.*s\n",(int)d->message.len,(const char*)d->message.ptr);

    /* ── Tool attribution ────────────────────────────────────────────────── */
    if (!QS_STR_IS_EMPTY(d->tool))
        fprintf(stderr,"    %s(from %.*s)%s\n",color?"\033[90m":"",
            (int)d->tool.len,(const char*)d->tool.ptr,reset);

    /* ── Source line + caret ─────────────────────────────────────────────── */
    if (show_caret && !QS_STR_IS_EMPTY(d->loc.src_line)) {
        fprintf(stderr,"  %.*s\n",(int)d->loc.src_line.len,(const char*)d->loc.src_line.ptr);
        if (d->loc.col>0) {
            qs_u64 spaces = d->loc.col>1?d->loc.col-1:0;
            fprintf(stderr,"  ");
            for (qs_u64 i=0;i<spaces;i++) fputc(' ',stderr);
            fprintf(stderr,"%s^",col);
            qs_u64 span=d->loc.span>1?d->loc.span-1:0;
            for (qs_u64 i=0;i<span;i++) fputc('~',stderr);
            fprintf(stderr,"%s\n",reset);
        }
    }

    /* ── Note ────────────────────────────────────────────────────────────── */
    if (!QS_STR_IS_EMPTY(d->note))
        fprintf(stderr,"    %snote:%s %.*s\n",color?"\033[36m":"",reset,
            (int)d->note.len,(const char*)d->note.ptr);

    /* ── Hint ────────────────────────────────────────────────────────────── */
    if (!QS_STR_IS_EMPTY(d->hint))
        fprintf(stderr,"    %shint:%s %.*s\n",color?"\033[32m":"",reset,
            (int)d->hint.len,(const char*)d->hint.ptr);

    /* ── Doc URL ─────────────────────────────────────────────────────────── */
    if (!QS_STR_IS_EMPTY(d->url))
        fprintf(stderr,"    %sdocs:%s %.*s\n",color?"\033[90m":"",reset,
            (int)d->url.len,(const char*)d->url.ptr);

    /* ── Raw stderr line ─────────────────────────────────────────────────── */
    if (show_raw && !QS_STR_IS_EMPTY(d->raw))
        fprintf(stderr,"    %sraw: %.*s%s\n",color?"\033[90m":"",
            (int)d->raw.len,(const char*)d->raw.ptr,reset);

    /* ── Related locations ───────────────────────────────────────────────── */
    for (qs_size_t i=0;i<d->related_count;i++) {
        const qs_loc_t *r=&d->related[i];
        if (!QS_STR_IS_EMPTY(r->file))
            fprintf(stderr,"    %snote:%s see %.*s:%llu:%llu\n",
                color?"\033[36m":"",reset,
                (int)r->file.len,(const char*)r->file.ptr,
                (unsigned long long)r->line,(unsigned long long)r->col);
    }
}

void qs_diag_print_all(const qs_diag_engine_t *e) {
    for (qs_size_t i=0;i<e->diags.len;i++)
        print_one(&e->diags.data[i],e->color,e->show_caret,e->show_raw);
    if (e->limit_reached)
        fprintf(stderr,"\n%stoo many errors; stopping after %llu%s\n",
            e->color?"\033[31m":"",
            (unsigned long long)e->error_limit,
            e->color?QS_COLOUR_RESET:"");
}

void qs_diag_print_summary(const qs_diag_engine_t *e) {
    qs_u64 errs  = e->counts[QS_SEV_ERROR]+e->counts[QS_SEV_FATAL];
    qs_u64 warns = e->counts[QS_SEV_WARNING];
    if (!errs && !warns) { fprintf(stderr,"build succeeded\n"); return; }
    fprintf(stderr,"\n%s%llu error(s)%s, %s%llu warning(s)%s\n",
        e->color?"\033[31m":"", (unsigned long long)errs,  e->color?QS_COLOUR_RESET:"",
        e->color?"\033[33m":"", (unsigned long long)warns, e->color?QS_COLOUR_RESET:"");
}

void qs_diag_print_json(const qs_diag_engine_t *e) {
    fprintf(stdout,"[\n");
    for (qs_size_t i=0;i<e->diags.len;i++) {
        const qs_diag_t *d=&e->diags.data[i];
        fprintf(stdout,"  {\n");
        fprintf(stdout,"    \"severity\":\"%s\",\n",qs_severity_str(d->sev));
        fprintf(stdout,"    \"code\":\"%.*s\",\n",(int)d->code.len,(const char*)d->code.ptr);
        fprintf(stdout,"    \"tool\":\"%.*s\",\n",(int)d->tool.len,(const char*)d->tool.ptr);
        fprintf(stdout,"    \"message\":\"%.*s\",\n",(int)d->message.len,(const char*)d->message.ptr);
        fprintf(stdout,"    \"file\":\"%.*s\",\n",(int)d->loc.file.len,(const char*)d->loc.file.ptr);
        fprintf(stdout,"    \"line\":%llu,\n",(unsigned long long)d->loc.line);
        fprintf(stdout,"    \"column\":%llu,\n",(unsigned long long)d->loc.col);
        fprintf(stdout,"    \"note\":\"%.*s\",\n",(int)d->note.len,(const char*)d->note.ptr);
        fprintf(stdout,"    \"hint\":\"%.*s\"\n",(int)d->hint.len,(const char*)d->hint.ptr);
        fprintf(stdout,"  }%s\n",i+1<e->diags.len?",":"");
    }
    fprintf(stdout,"]\n");
}

qs_bool_t qs_diag_has_errors(const qs_diag_engine_t *e) {
    return (e->counts[QS_SEV_ERROR]+e->counts[QS_SEV_FATAL])>0?QS_TRUE:QS_FALSE;
}
qs_u64 qs_diag_error_count(const qs_diag_engine_t *e) {
    return e->counts[QS_SEV_ERROR]+e->counts[QS_SEV_FATAL];
}
qs_u64 qs_diag_warning_count(const qs_diag_engine_t *e) {
    return e->counts[QS_SEV_WARNING];
}
void qs_diag_clear(qs_diag_engine_t *e) {
    qs_diag_vec_clear(&e->diags);
    memset(e->counts,0,sizeof(e->counts));
    e->limit_reached=QS_FALSE;
}
