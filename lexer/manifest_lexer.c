/*
 * lexer/manifest_lexer.c — Reusable lexer for build.qs files.
 * Extracted from manifest/parser.c as a standalone module so it can
 * be used by tooling (IDE plugins, validators, formatters).
 * Produces a flat token stream; caller drives the parser.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_vec.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

typedef enum {
    QSL_EOF=0, QSL_IDENT, QSL_STRING, QSL_NUMBER, QSL_BOOL,
    QSL_EQ, QSL_LBRACE, QSL_RBRACE, QSL_LBRACKET, QSL_RBRACKET,
    QSL_COMMA, QSL_SEMI, QSL_COLON, QSL_COMMENT, QSL_UNKNOWN
} qsl_kind_t;

typedef struct {
    qsl_kind_t  kind;
    const char *start;   /* pointer into source */
    qs_size_t   len;
    qs_u64      line;
    qs_u64      col;
} qsl_token_t;

QS_VEC_DECL(qsl_token_t, qsl_token_vec)

typedef struct {
    const char     *src;
    qs_size_t       len;
    qs_size_t       pos;
    qs_u64          line;
    qs_u64          col;
    qsl_token_vec_t tokens;
    qs_arena_t     *arena;
} qsl_lexer_t;

void qsl_lexer_init(qsl_lexer_t *l, qs_arena_t *a, const char *src, qs_size_t len) {
    l->src   = src;
    l->len   = len;
    l->pos   = 0;
    l->line  = 1;
    l->col   = 1;
    l->arena = a;
    qsl_token_vec_init(&l->tokens, a);
}

static void advance(qsl_lexer_t *l) {
    if (l->pos >= l->len) return;
    if (l->src[l->pos] == '\n') { l->line++; l->col = 1; }
    else                          l->col++;
    l->pos++;
}

static void skip_whitespace_comments(qsl_lexer_t *l) {
    while (l->pos < l->len) {
        char c = l->src[l->pos];
        if (c==' '||c=='\t'||c=='\r'||c=='\n') { advance(l); continue; }
        if (c=='/' && l->pos+1<l->len) {
            if (l->src[l->pos+1]=='/') {
                while (l->pos<l->len && l->src[l->pos]!='\n') advance(l);
                continue;
            }
            if (l->src[l->pos+1]=='*') {
                advance(l); advance(l);
                while (l->pos+1<l->len &&
                       !(l->src[l->pos]=='*'&&l->src[l->pos+1]=='/'))
                    advance(l);
                if (l->pos+1<l->len) { advance(l); advance(l); }
                continue;
            }
        }
        break;
    }
}

qs_result_t qsl_tokenize(qsl_lexer_t *l) {
    for (;;) {
        skip_whitespace_comments(l);
        if (l->pos >= l->len) {
            qsl_token_t t = {QSL_EOF, l->src+l->pos, 0, l->line, l->col};
            qsl_token_vec_push(&l->tokens, t);
            break;
        }
        char c = l->src[l->pos];
        qs_u64 tl = l->line, tc = l->col;

        /* Single-char tokens */
        qsl_kind_t sc_kind = QSL_UNKNOWN;
        if (c=='=') sc_kind=QSL_EQ;
        else if(c=='{') sc_kind=QSL_LBRACE;
        else if(c=='}') sc_kind=QSL_RBRACE;
        else if(c=='[') sc_kind=QSL_LBRACKET;
        else if(c==']') sc_kind=QSL_RBRACKET;
        else if(c==',') sc_kind=QSL_COMMA;
        else if(c==';') sc_kind=QSL_SEMI;
        else if(c==':') sc_kind=QSL_COLON;
        if (sc_kind != QSL_UNKNOWN) {
            qsl_token_t t = {sc_kind, l->src+l->pos, 1, tl, tc};
            qsl_token_vec_push(&l->tokens, t);
            advance(l); continue;
        }

        /* String literal */
        if (c == '"') {
            advance(l);
            const char *start = l->src + l->pos;
            while (l->pos<l->len && l->src[l->pos]!='"' && l->src[l->pos]!='\n')
                advance(l);
            qs_size_t slen = (qs_size_t)(l->src + l->pos - start);
            qsl_token_t t = {QSL_STRING, start, slen, tl, tc};
            qsl_token_vec_push(&l->tokens, t);
            if (l->pos<l->len && l->src[l->pos]=='"') advance(l);
            continue;
        }

        /* Identifier / keyword / bool */
        if (isalpha((unsigned char)c) || c=='_') {
            const char *start = l->src + l->pos;
            while (l->pos<l->len && (isalnum((unsigned char)l->src[l->pos])||l->src[l->pos]=='_'))
                advance(l);
            qs_size_t ilen = (qs_size_t)(l->src+l->pos - start);
            qsl_kind_t ik = QSL_IDENT;
            if ((ilen==4&&strncmp(start,"true",4)==0)||
                (ilen==5&&strncmp(start,"false",5)==0)) ik=QSL_BOOL;
            qsl_token_t t = {ik, start, ilen, tl, tc};
            qsl_token_vec_push(&l->tokens, t);
            continue;
        }

        /* Number */
        if (isdigit((unsigned char)c)) {
            const char *start = l->src + l->pos;
            while (l->pos<l->len && isdigit((unsigned char)l->src[l->pos])) advance(l);
            qs_size_t nlen = (qs_size_t)(l->src+l->pos - start);
            qsl_token_t t = {QSL_NUMBER, start, nlen, tl, tc};
            qsl_token_vec_push(&l->tokens, t);
            continue;
        }

        /* Unknown — skip */
        qsl_token_t t = {QSL_UNKNOWN, l->src+l->pos, 1, tl, tc};
        qsl_token_vec_push(&l->tokens, t);
        advance(l);
    }
    return QS_OK;
}

void qsl_dump_tokens(const qsl_lexer_t *l) {
    static const char *NAMES[] = {
        "EOF","IDENT","STRING","NUMBER","BOOL","EQ","LBRACE","RBRACE",
        "LBRACKET","RBRACKET","COMMA","SEMI","COLON","COMMENT","UNKNOWN"
    };
    for (qs_size_t i=0;i<l->tokens.len;i++) {
        const qsl_token_t *t=&l->tokens.data[i];
        fprintf(stderr,"  [%llu:%llu] %-10s '%.*s'\n",
            (unsigned long long)t->line,(unsigned long long)t->col,
            NAMES[t->kind<15?t->kind:14],
            (int)t->len,t->start);
    }
}
