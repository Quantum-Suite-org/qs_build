/*
 * manifest/parser.c — Parse build.qs manifest files into qs_manifest_t.
 *
 * build.qs format (same as the existing skeleton in qs_build/core):
 *
 *   module <name> {
 *       name     = "...";
 *       version  = "...";
 *       language = "cpp";    // c | cpp | csharp | java | python | go | ruby | zig | rust
 *       standard = "c++20";
 *       sources  = [ "src/foo.cpp", "src/bar.cpp" ];
 *       headers  = [ "include/foo.h" ];
 *       defines  = [ "NDEBUG", "MY_MACRO=1" ];
 *       include_dirs = [ "include", "third_party/include" ];
 *       dependencies = [ "qs_core", "qs_math" ];
 *       output_type = "exe";  // exe|dll|lib|app|qpkg|wheel|jar|gem|nupkg|gobin
 *       enable_lto    = true;
 *       chunk_size    = 50;
 *       pch_header    = "include/pch.h";  // optional; auto-generated if omitted
 *       tests         = [ "tests/test_main.cpp" ];
 *       // Language-specific extras
 *       dotnet_sdk    = "net8.0";
 *       java_version  = "21";
 *       python_version= "3.12";
 *       go_module     = "github.com/org/module";
 *       publish       = false;
 *   }
 *
 * Parser: hand-rolled recursive descent, zero dependencies.
 * All strings are interned into the arena.
 */
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_str.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_path.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ── Lexer state ─────────────────────────────────────────────────────────── */
typedef struct {
    const char *src;
    qs_size_t   len;
    qs_size_t   pos;
    qs_size_t   line;
    qs_size_t   col;
    const char *path;
    qs_diag_engine_t *diag;
    qs_arena_t  *arena;
} mlex_t;

typedef enum {
    MT_EOF=0, MT_IDENT, MT_STRING, MT_NUMBER, MT_BOOL,
    MT_EQ, MT_LBRACE, MT_RBRACE, MT_LBRACKET, MT_RBRACKET,
    MT_COMMA, MT_SEMI, MT_COLON
} mtoken_kind_t;

typedef struct {
    mtoken_kind_t kind;
    const char   *text;
    qs_size_t     len;
    qs_size_t     line;
} mtoken_t;

static void mlex_init(mlex_t *l, const char *src, qs_size_t len,
                       const char *path, qs_arena_t *a, qs_diag_engine_t *d) {
    l->src=src; l->len=len; l->pos=0; l->line=1; l->col=1;
    l->path=path; l->arena=a; l->diag=d;
}

static void skip_ws_comments(mlex_t *l) {
    while (l->pos < l->len) {
        char c = l->src[l->pos];
        if (c == ' '||c=='\t'||c=='\r') { l->pos++; l->col++; continue; }
        if (c == '\n') { l->pos++; l->line++; l->col=1; continue; }
        if (c == '/' && l->pos+1 < l->len) {
            if (l->src[l->pos+1]=='/') {
                while (l->pos<l->len && l->src[l->pos]!='\n') l->pos++;
                continue;
            }
            if (l->src[l->pos+1]=='*') {
                l->pos+=2;
                while (l->pos+1<l->len && !(l->src[l->pos]=='*'&&l->src[l->pos+1]=='/')) {
                    if (l->src[l->pos]=='\n'){l->line++;l->col=1;}
                    l->pos++;
                }
                if (l->pos+1<l->len) l->pos+=2;
                continue;
            }
        }
        break;
    }
}

static mtoken_t mlex_next(mlex_t *l) {
    skip_ws_comments(l);
    mtoken_t t; memset(&t,0,sizeof(t)); t.line=l->line;
    if (l->pos>=l->len) { t.kind=MT_EOF; return t; }
    char c=l->src[l->pos];
    /* Single-char tokens */
    if (c=='='){ t.kind=MT_EQ;       t.text=l->src+l->pos; t.len=1; l->pos++; return t; }
    if (c=='{'){ t.kind=MT_LBRACE;   t.text=l->src+l->pos; t.len=1; l->pos++; return t; }
    if (c=='}'){ t.kind=MT_RBRACE;   t.text=l->src+l->pos; t.len=1; l->pos++; return t; }
    if (c=='['){ t.kind=MT_LBRACKET; t.text=l->src+l->pos; t.len=1; l->pos++; return t; }
    if (c==']'){ t.kind=MT_RBRACKET; t.text=l->src+l->pos; t.len=1; l->pos++; return t; }
    if (c==','){ t.kind=MT_COMMA;    t.text=l->src+l->pos; t.len=1; l->pos++; return t; }
    if (c==';'){ t.kind=MT_SEMI;     t.text=l->src+l->pos; t.len=1; l->pos++; return t; }
    if (c==':'){ t.kind=MT_COLON;    t.text=l->src+l->pos; t.len=1; l->pos++; return t; }
    /* String literal */
    if (c=='"') {
        l->pos++;
        const char *start=l->src+l->pos;
        while (l->pos<l->len && l->src[l->pos]!='"' && l->src[l->pos]!='\n') l->pos++;
        t.kind=MT_STRING; t.text=start; t.len=(qs_size_t)(l->src+l->pos-start);
        if (l->pos<l->len) l->pos++; /* consume closing " */
        return t;
    }
    /* Identifier or keyword */
    if (isalpha((unsigned char)c)||c=='_') {
        const char *start=l->src+l->pos;
        while (l->pos<l->len&&(isalnum((unsigned char)l->src[l->pos])||l->src[l->pos]=='_')) l->pos++;
        t.kind=MT_IDENT; t.text=start; t.len=(qs_size_t)(l->src+l->pos-start);
        /* Distinguish bool literals */
        if ((t.len==4&&strncmp(t.text,"true",4)==0)||(t.len==5&&strncmp(t.text,"false",5)==0))
            t.kind=MT_BOOL;
        return t;
    }
    /* Number */
    if (isdigit((unsigned char)c)) {
        const char *start=l->src+l->pos;
        while (l->pos<l->len&&isdigit((unsigned char)l->src[l->pos])) l->pos++;
        t.kind=MT_NUMBER; t.text=start; t.len=(qs_size_t)(l->src+l->pos-start);
        return t;
    }
    /* Unknown — skip */
    l->pos++;
    return mlex_next(l);
}

/* ── String list parsing ─────────────────────────────────────────────────── */
static qs_result_t parse_string_list(mlex_t *l, qs_str_vec_t *out) {
    mtoken_t t = mlex_next(l);
    if (t.kind != MT_LBRACKET) return QS_ERROR_MANIFEST;
    for (;;) {
        skip_ws_comments(l);
        t = mlex_next(l);
        if (t.kind == MT_RBRACKET) break;
        if (t.kind == MT_COMMA) continue;
        if (t.kind == MT_STRING) {
            char *s = qs_arena_alloc(l->arena, t.len+1, 1);
            memcpy(s, t.text, t.len); s[t.len]='\0';
            qs_str_vec_push(out, qs_str_from_cstr(s));
        }
    }
    return QS_OK;
}

/* ── Language name → enum ────────────────────────────────────────────────── */
static qs_lang_t lang_from_str(const char *s, qs_size_t len) {
    if (strncmp(s,"c",len)==0 && len==1) return QS_LANG_C;
    if (strncmp(s,"cpp",len)==0||strncmp(s,"c++",len)==0) return QS_LANG_CPP;
    if (strncmp(s,"csharp",len)==0||strncmp(s,"cs",len)==0) return QS_LANG_CSHARP;
    if (strncmp(s,"java",len)==0) return QS_LANG_JAVA;
    if (strncmp(s,"python",len)==0||strncmp(s,"py",len)==0) return QS_LANG_PYTHON;
    if (strncmp(s,"go",len)==0) return QS_LANG_GO;
    if (strncmp(s,"ruby",len)==0||strncmp(s,"rb",len)==0) return QS_LANG_RUBY;
    if (strncmp(s,"zig",len)==0) return QS_LANG_ZIG;
    if (strncmp(s,"rust",len)==0||strncmp(s,"rs",len)==0) return QS_LANG_RUST;
    return QS_LANG_UNKNOWN;
}

/* ── Main parse entry ────────────────────────────────────────────────────── */
qs_result_t qs_manifest_parse(qs_arena_t *a, const char *path,
                                qs_manifest_t *out, qs_diag_engine_t *diag) {
    char *src = NULL; qs_size_t slen = 0;
    qs_result_t r = qs_fs_read_file(a, path, &src, &slen);
    if (r != QS_OK) {
        qs_diag_emit_simple(diag, QS_SEV_FATAL, "manifest", "QSB-MAN001",
            QS_LOC_UNKNOWN, qs_arena_sprintf(a, "cannot open manifest: %s", path));
        return r;
    }

    memset(out, 0, sizeof(*out));
    out->manifest_path = qs_str_from_cstr(path);
    out->output_type   = QS_OUT_EXE;
    qs_str_vec_init(&out->sources,      a);
    qs_str_vec_init(&out->headers,      a);
    qs_str_vec_init(&out->modules,      a);
    qs_str_vec_init(&out->dependencies, a);
    qs_str_vec_init(&out->tests,        a);
    qs_str_vec_init(&out->defines,      a);
    qs_str_vec_init(&out->include_dirs, a);
    qs_str_vec_init(&out->lib_dirs,     a);
    qs_str_vec_init(&out->link_libs,    a);
    qs_str_vec_init(&out->extra_flags,  a);

    mlex_t lex; mlex_init(&lex, src, slen, path, a, diag);
    mtoken_t t;

    /* Expect: module <name> { ... } */
    t = mlex_next(&lex);
    if (t.kind == MT_IDENT && strncmp(t.text,"module",t.len)==0) {
        t = mlex_next(&lex);
        if (t.kind == MT_IDENT) {
            char *nm = qs_arena_alloc(a, t.len+1, 1);
            memcpy(nm,t.text,t.len); nm[t.len]='\0';
            out->name = qs_str_from_cstr(nm);
        }
        t = mlex_next(&lex); /* consume '{' */
    }

    /* Parse key = value; pairs inside { } */
    for (;;) {
        t = mlex_next(&lex);
        if (t.kind == MT_EOF || t.kind == MT_RBRACE) break;
        if (t.kind != MT_IDENT) continue;

        char key[64]; qs_size_t kl = t.len < 63 ? t.len : 63;
        memcpy(key, t.text, kl); key[kl] = '\0';

        mtoken_t eq = mlex_next(&lex); /* '=' */
        if (eq.kind != MT_EQ) continue;

        mtoken_t val = mlex_next(&lex);

        #define KEY(k) (strcmp(key,(k))==0)
        #define STRVAL() qs_arena_alloc(a,val.len+1,1); \
                          do { char *_s=(char*)qs_arena_alloc(a,val.len+1,1); \
                               memcpy(_s,val.text,val.len);_s[val.len]='\0'; } while(0)

        if (KEY("name") && val.kind==MT_STRING) {
            char *s=qs_arena_alloc(a,val.len+1,1); memcpy(s,val.text,val.len); s[val.len]='\0';
            out->name=qs_str_from_cstr(s);
        } else if (KEY("version") && val.kind==MT_STRING) {
            char *s=qs_arena_alloc(a,val.len+1,1); memcpy(s,val.text,val.len); s[val.len]='\0';
            out->version=qs_str_from_cstr(s);
        } else if (KEY("language") && val.kind==MT_STRING) {
            out->language=lang_from_str(val.text,val.len);
        } else if (KEY("standard") && val.kind==MT_STRING) {
            char *s=qs_arena_alloc(a,val.len+1,1); memcpy(s,val.text,val.len); s[val.len]='\0';
            out->standard=qs_str_from_cstr(s);
        } else if (KEY("sources")      && val.kind==MT_LBRACKET) {
            lex.pos--; /* put '[' back */ parse_string_list(&lex,&out->sources);
        } else if (KEY("headers")      && val.kind==MT_LBRACKET) {
            lex.pos--; parse_string_list(&lex,&out->headers);
        } else if (KEY("modules")      && val.kind==MT_LBRACKET) {
            lex.pos--; parse_string_list(&lex,&out->modules);
        } else if (KEY("dependencies") && val.kind==MT_LBRACKET) {
            lex.pos--; parse_string_list(&lex,&out->dependencies);
        } else if (KEY("tests")        && val.kind==MT_LBRACKET) {
            lex.pos--; parse_string_list(&lex,&out->tests);
        } else if (KEY("defines")      && val.kind==MT_LBRACKET) {
            lex.pos--; parse_string_list(&lex,&out->defines);
        } else if (KEY("include_dirs") && val.kind==MT_LBRACKET) {
            lex.pos--; parse_string_list(&lex,&out->include_dirs);
        } else if (KEY("lib_dirs")     && val.kind==MT_LBRACKET) {
            lex.pos--; parse_string_list(&lex,&out->lib_dirs);
        } else if (KEY("link_libs")    && val.kind==MT_LBRACKET) {
            lex.pos--; parse_string_list(&lex,&out->link_libs);
        } else if (KEY("extra_flags")  && val.kind==MT_LBRACKET) {
            lex.pos--; parse_string_list(&lex,&out->extra_flags);
        } else if (KEY("output_type")  && val.kind==MT_STRING) {
            /* val.text is NOT null-terminated — copy to arena first */
            char *ot_s=qs_arena_alloc(a,val.len+1,1);
            memcpy(ot_s,val.text,val.len); ot_s[val.len]='\0';
            qs_output_type_parse(ot_s, &out->output_type);
        } else if (KEY("enable_lto")   && val.kind==MT_BOOL) {
            out->enable_lto = (val.text[0]=='t')?QS_TRUE:QS_FALSE;
        } else if (KEY("enable_modules")&& val.kind==MT_BOOL) {
            out->enable_modules=(val.text[0]=='t')?QS_TRUE:QS_FALSE;
        } else if (KEY("enable_pch")   && val.kind==MT_BOOL) {
            out->enable_pch=(val.text[0]=='t')?QS_TRUE:QS_FALSE;
        } else if (KEY("chunk_size")   && val.kind==MT_NUMBER) {
            out->chunk_size=0; for(qs_size_t i=0;i<val.len;i++) out->chunk_size=out->chunk_size*10+(val.text[i]-'0');
        } else if (KEY("dotnet_sdk")   && val.kind==MT_STRING) {
            char *s=qs_arena_alloc(a,val.len+1,1); memcpy(s,val.text,val.len); s[val.len]='\0'; out->dotnet_sdk=qs_str_from_cstr(s);
        } else if (KEY("java_version") && val.kind==MT_STRING) {
            char *s=qs_arena_alloc(a,val.len+1,1); memcpy(s,val.text,val.len); s[val.len]='\0'; out->java_version=qs_str_from_cstr(s);
        } else if (KEY("python_version")&&val.kind==MT_STRING) {
            char *s=qs_arena_alloc(a,val.len+1,1); memcpy(s,val.text,val.len); s[val.len]='\0'; out->python_version=qs_str_from_cstr(s);
        } else if (KEY("go_module")    && val.kind==MT_STRING) {
            char *s=qs_arena_alloc(a,val.len+1,1); memcpy(s,val.text,val.len); s[val.len]='\0'; out->go_module=qs_str_from_cstr(s);
        } else if (KEY("publish")      && val.kind==MT_BOOL) {
            out->publish=(val.text[0]=='t')?QS_TRUE:QS_FALSE;
        } else if (KEY("pch_header")   && val.kind==MT_STRING) {
            char *s=qs_arena_alloc(a,val.len+1,1); memcpy(s,val.text,val.len); s[val.len]='\0'; out->pch_header=qs_str_from_cstr(s);
        }
        /* Consume optional semicolon */
        skip_ws_comments(&lex);
        if (lex.pos < lex.len && lex.src[lex.pos]==';') lex.pos++;
    }
    return QS_OK;
}

void qs_manifest_apply_auto_rules(qs_manifest_t *m) {
    if (!m->enable_pch    && m->sources.len >= QS_PCH_THRESHOLD)
        m->enable_pch    = QS_TRUE;
    if (!m->enable_chunks && m->sources.len >= QS_CHUNK_THRESHOLD)
        m->enable_chunks = QS_TRUE;
    if (!m->chunk_size)
        m->chunk_size = QS_DEFAULT_CHUNK_SIZE;
}

qs_result_t qs_manifest_validate(qs_manifest_t *m, qs_diag_engine_t *diag) {
    if (QS_STR_IS_EMPTY(m->name))
        qs_diag_emit_simple(diag,QS_SEV_ERROR,"manifest","QSB-MAN010",QS_LOC_UNKNOWN,"manifest missing 'name'");
    if (m->language==QS_LANG_UNKNOWN)
        qs_diag_emit_simple(diag,QS_SEV_WARNING,"manifest","QSB-MAN011",QS_LOC_UNKNOWN,"manifest missing 'language'; defaulting to C");
    if (!m->sources.len && m->language!=QS_LANG_GO)
        qs_diag_emit_simple(diag,QS_SEV_WARNING,"manifest","QSB-MAN012",QS_LOC_UNKNOWN,"manifest has no sources listed");
    return qs_diag_has_errors(diag)?QS_ERROR_MANIFEST:QS_OK;
}

qs_result_t qs_output_type_parse(const char *s, qs_output_type_t *out) {
    if (strcmp(s,"exe")==0||strcmp(s,"executable")==0) { *out=QS_OUT_EXE; return QS_OK; }
    if (strcmp(s,"dll")==0||strcmp(s,"sharedlib")==0)  { *out=QS_OUT_DLL; return QS_OK; }
    if (strcmp(s,"lib")==0||strcmp(s,"staticlib")==0)  { *out=QS_OUT_LIB; return QS_OK; }
    if (strcmp(s,"app")==0)                            { *out=QS_OUT_APP; return QS_OK; }
    if (strcmp(s,"qpkg")==0)                           { *out=QS_OUT_QPKG; return QS_OK; }
    if (strcmp(s,"cdylib")==0)                         { *out=QS_OUT_CDYLIB; return QS_OK; }
    if (strcmp(s,"wheel")==0)                          { *out=QS_OUT_WHEEL; return QS_OK; }
    if (strcmp(s,"jar")==0)                            { *out=QS_OUT_JAR; return QS_OK; }
    if (strcmp(s,"gem")==0)                            { *out=QS_OUT_GEM; return QS_OK; }
    if (strcmp(s,"nupkg")==0)                          { *out=QS_OUT_NUPKG; return QS_OK; }
    if (strcmp(s,"gobin")==0)                          { *out=QS_OUT_GOBIN; return QS_OK; }
    if (strcmp(s,"object")==0||strcmp(s,"obj")==0)     { *out=QS_OUT_OBJECT; return QS_OK; }
    return QS_ERROR_INVALID_ARG;
}

void qs_manifest_print(const qs_manifest_t *m) {
    fprintf(stderr,"[manifest] name=%.*s lang=%s output=%s sources=%llu\n",
        (int)m->name.len,(const char*)m->name.ptr,
        qs_lang_name(m->language),
        qs_output_type_str(m->output_type),
        (unsigned long long)m->sources.len);
    fprintf(stderr,"  pch=%s chunks=%s chunk_size=%llu lto=%s\n",
        m->enable_pch?"yes":"auto",
        m->enable_chunks?"yes":"auto",
        (unsigned long long)m->chunk_size,
        m->enable_lto?"yes":"no");
}
