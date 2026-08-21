/*
 * diag_parsers.c — Per-language stderr parsers.
 *
 * Each parser takes raw stderr text from a compiler/tool and extracts
 * structured qs_diag_t records. The goal is:
 *   - Never lose information from the raw line
 *   - Parse as much as possible (file, line, col, message, code)
 *   - Fall back to a single raw-line diagnostic if format is unrecognised
 *
 * Supported parsers:
 *   clang / GCC    — "file:line:col: severity: message"
 *   MSVC (cl.exe)  — "file(line,col): error C####: message"
 *   javac          — "file:line: error/warning: message"
 *   dotnet/csc     — "file(line,col): error CS####: message"
 *   go             — "file:line:col: message" + build errors
 *   python         — "  File "file", line N" + exception line
 *   rustc          — JSON mode (--error-format=json) + text fallback
 *   zig            — "file:line:col: error: message" / note: ...
 *   ruby           — "file:line:in `method': message (ExcClass)"
 */
#include "qs_diag.h"
#include "qs_arena.h"
#include "qs_str.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* ── Line iterator ───────────────────────────────────────────────────────── */
typedef struct { const char *p; const char *end; } line_iter_t;

static void li_init(line_iter_t *it, const char *text) {
    it->p=text; it->end=text+strlen(text);
}
static qs_bool_t li_next(line_iter_t *it, const char **line_start, qs_size_t *line_len) {
    if (it->p>=it->end) return QS_FALSE;
    *line_start=it->p;
    const char *nl=(const char*)memchr(it->p,'\n',(size_t)(it->end-it->p));
    if (nl) { *line_len=(qs_size_t)(nl-it->p); it->p=nl+1; }
    else    { *line_len=(qs_size_t)(it->end-it->p); it->p=it->end; }
    return QS_TRUE;
}

/* ── Number parsing ──────────────────────────────────────────────────────── */
static qs_u64 parse_u64(const char *s, qs_size_t len) {
    qs_u64 v=0;
    for (qs_size_t i=0;i<len&&s[i]>='0'&&s[i]<='9';i++) v=v*10+(qs_u64)(s[i]-'0');
    return v;
}

/* ── Severity from string ────────────────────────────────────────────────── */
static qs_severity_t parse_sev(const char *s, qs_size_t len) {
    if (len>=5 && strncmp(s,"error",5)==0)   return QS_SEV_ERROR;
    if (len>=7 && strncmp(s,"warning",7)==0) return QS_SEV_WARNING;
    if (len>=4 && strncmp(s,"note",4)==0)    return QS_SEV_NOTE;
    if (len>=5 && strncmp(s,"fatal",5)==0)   return QS_SEV_FATAL;
    if (len>=4 && strncmp(s,"hint",4)==0)    return QS_SEV_HINT;
    return QS_SEV_NOTE;
}

/* ── Build a minimal diag from a raw line ────────────────────────────────── */
static qs_diag_t raw_diag(qs_arena_t *a, const char *line, qs_size_t len,
                            qs_severity_t sev, qs_str_t tool) {
    qs_diag_t d; memset(&d,0,sizeof(d));
    d.sev=sev; d.tool=tool;
    d.raw=d.message=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line,len),len};
    return d;
}

/* ══════════════════════════════════════════════════════════════════════════
 * clang / GCC: file:line:col: severity: message
 *              file:line:col: note: ...
 * ══════════════════════════════════════════════════════════════════════════ */
qs_size_t qs_diag_parse_clang(qs_arena_t *a, const char *text, qs_str_t tool,
                                qs_diag_t *out, qs_size_t cap) {
    qs_size_t count=0;
    line_iter_t it; li_init(&it,text);
    const char *line; qs_size_t llen;
    while (li_next(&it,&line,&llen) && count<cap) {
        if (!llen) continue;
        /* Try to parse "file:line:col: severity: message" */
        /* Find first colon that is NOT part of a Windows drive letter */
        qs_size_t ci=0;
#ifdef _WIN32
        ci = (llen>2&&line[1]==':')?2:0; /* skip drive letter */
#endif
        while (ci<llen && line[ci]!=':') ci++;
        if (ci>=llen) { /* no colon — bare message */
            if (count<cap) out[count++]=raw_diag(a,line,llen,QS_SEV_NOTE,tool);
            continue;
        }
        qs_size_t file_end=ci;
        /* Expect digit for line number */
        qs_size_t li_start=ci+1, li_end=li_start;
        while (li_end<llen && isdigit((unsigned char)line[li_end])) li_end++;
        if (li_end==li_start || line[li_end]!=':') {
            out[count++]=raw_diag(a,line,llen,QS_SEV_NOTE,tool); continue;
        }
        qs_u64 lineno=parse_u64(line+li_start,li_end-li_start);
        /* col */
        qs_size_t co_start=li_end+1, co_end=co_start;
        while (co_end<llen && isdigit((unsigned char)line[co_end])) co_end++;
        qs_u64 colno=0;
        if (co_end>co_start && line[co_end]==':') { colno=parse_u64(line+co_start,co_end-co_start); ci=co_end; }
        else ci=li_end;
        /* severity keyword */
        qs_size_t sev_start=ci+1;
        while (sev_start<llen&&line[sev_start]==' ') sev_start++;
        qs_size_t sev_end=sev_start;
        while (sev_end<llen&&line[sev_end]!=':') sev_end++;
        if (sev_end>=llen) { out[count++]=raw_diag(a,line,llen,QS_SEV_NOTE,tool); continue; }
        qs_severity_t sev=parse_sev(line+sev_start,sev_end-sev_start);
        /* message: everything after "severity: " */
        qs_size_t msg_start=sev_end+1;
        while (msg_start<llen&&line[msg_start]==' ') msg_start++;

        qs_diag_t d; memset(&d,0,sizeof(d));
        d.sev=sev; d.tool=tool;
        d.raw=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line,llen),llen};
        d.loc.file=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line,file_end),file_end};
        d.loc.line=lineno; d.loc.col=colno;
        d.message=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line+msg_start,llen-msg_start),llen-msg_start};
        /* clang includes [-Wfoo] or [-Werror,-Wfoo] at end — extract as code */
        qs_size_t ml=llen-msg_start;
        if (ml>0 && ((const char*)d.message.ptr)[ml-1]==']') {
            for (qs_size_t k=ml;k>0;k--) {
                if (((const char*)d.message.ptr)[k-1]=='[') {
                    d.code=(qs_str_t){d.message.ptr+k, ml-k-1};
                    d.message.len=k>2?k-2:0; /* trim trailing space+[ */
                    break;
                }
            }
        }
        out[count++]=d;
    }
    return count;
}

/* ══════════════════════════════════════════════════════════════════════════
 * GCC — same format as clang; delegate
 * ══════════════════════════════════════════════════════════════════════════ */
qs_size_t qs_diag_parse_gcc(qs_arena_t *a, const char *text, qs_str_t tool,
                              qs_diag_t *out, qs_size_t cap) {
    return qs_diag_parse_clang(a,text,tool,out,cap);
}

/* ══════════════════════════════════════════════════════════════════════════
 * MSVC: file(line,col): error|warning C####: message
 * ══════════════════════════════════════════════════════════════════════════ */
qs_size_t qs_diag_parse_msvc(qs_arena_t *a, const char *text, qs_str_t tool,
                               qs_diag_t *out, qs_size_t cap) {
    qs_size_t count=0;
    line_iter_t it; li_init(&it,text);
    const char *line; qs_size_t llen;
    while (li_next(&it,&line,&llen) && count<cap) {
        if (!llen) continue;
        /* find '(' */
        qs_size_t pi=0; while(pi<llen&&line[pi]!='(') pi++;
        if (pi>=llen) { out[count++]=raw_diag(a,line,llen,QS_SEV_NOTE,tool); continue; }
        qs_size_t file_end=pi;
        /* parse line number */
        qs_size_t ln_s=pi+1, ln_e=ln_s;
        while(ln_e<llen&&isdigit((unsigned char)line[ln_e])) ln_e++;
        qs_u64 lineno=parse_u64(line+ln_s,ln_e-ln_s);
        /* optional ,col */
        qs_u64 colno=0;
        if (ln_e<llen&&line[ln_e]==',') {
            qs_size_t co_s=ln_e+1,co_e=co_s;
            while(co_e<llen&&isdigit((unsigned char)line[co_e])) co_e++;
            colno=parse_u64(line+co_s,co_e-co_s);
            ln_e=co_e;
        }
        /* expect "): error C####:" */
        qs_size_t after=ln_e;
        while(after<llen&&line[after]!=':') after++;
        if (after>=llen) { out[count++]=raw_diag(a,line,llen,QS_SEV_NOTE,tool); continue; }
        qs_size_t kw_s=after+1;
        while(kw_s<llen&&line[kw_s]==' ') kw_s++;
        qs_size_t kw_e=kw_s;
        while(kw_e<llen&&line[kw_e]!=' '&&line[kw_e]!=':') kw_e++;
        qs_severity_t sev=parse_sev(line+kw_s,kw_e-kw_s);
        /* code like C4100 or LNK2001 */
        qs_size_t code_s=kw_e;
        while(code_s<llen&&(line[code_s]==' '||line[code_s]==':')) code_s++;
        qs_size_t code_e=code_s;
        while(code_e<llen&&line[code_e]!=':'&&line[code_e]!=' ') code_e++;
        qs_size_t msg_s=code_e;
        while(msg_s<llen&&(line[msg_s]==':'||line[msg_s]==' ')) msg_s++;

        qs_diag_t d; memset(&d,0,sizeof(d));
        d.sev=sev; d.tool=tool;
        d.raw=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line,llen),llen};
        d.loc.file=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line,file_end),file_end};
        d.loc.line=lineno; d.loc.col=colno;
        d.code=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line+code_s,code_e-code_s),code_e-code_s};
        d.message=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line+msg_s,llen-msg_s),llen-msg_s};
        out[count++]=d;
    }
    return count;
}

/* ══════════════════════════════════════════════════════════════════════════
 * javac: file.java:##: error|warning: message
 *        symbol line / location line are notes
 * ══════════════════════════════════════════════════════════════════════════ */
qs_size_t qs_diag_parse_javac(qs_arena_t *a, const char *text,
                                qs_diag_t *out, qs_size_t cap) {
    qs_str_t tool=QS_STR("javac");
    qs_size_t count=0;
    line_iter_t it; li_init(&it,text);
    const char *line; qs_size_t llen;
    while (li_next(&it,&line,&llen) && count<cap) {
        if (!llen) continue;
        /* find last colon before a digit run to locate line number */
        qs_size_t ci=0; while(ci<llen&&line[ci]!=':') ci++;
        if (ci>=llen) { out[count++]=raw_diag(a,line,llen,QS_SEV_NOTE,tool); continue; }
        qs_size_t file_end=ci;
        qs_size_t ln_s=ci+1, ln_e=ln_s;
        while(ln_e<llen&&isdigit((unsigned char)line[ln_e])) ln_e++;
        if (ln_e==ln_s||line[ln_e]!=':') { out[count++]=raw_diag(a,line,llen,QS_SEV_NOTE,tool); continue; }
        qs_u64 lineno=parse_u64(line+ln_s,ln_e-ln_s);
        qs_size_t kw_s=ln_e+1; while(kw_s<llen&&line[kw_s]==' ') kw_s++;
        qs_size_t kw_e=kw_s; while(kw_e<llen&&line[kw_e]!=':') kw_e++;
        qs_severity_t sev=parse_sev(line+kw_s,kw_e-kw_s);
        qs_size_t msg_s=kw_e+1; while(msg_s<llen&&line[msg_s]==' ') msg_s++;

        qs_diag_t d; memset(&d,0,sizeof(d));
        d.sev=sev; d.tool=tool;
        d.raw=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line,llen),llen};
        d.loc.file=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line,file_end),file_end};
        d.loc.line=lineno;
        d.message=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line+msg_s,llen-msg_s),llen-msg_s};
        out[count++]=d;
    }
    return count;
}

/* ══════════════════════════════════════════════════════════════════════════
 * dotnet/csc: file(line,col): error|warning CS####: message
 * ══════════════════════════════════════════════════════════════════════════ */
qs_size_t qs_diag_parse_dotnet(qs_arena_t *a, const char *text,
                                 qs_diag_t *out, qs_size_t cap) {
    /* dotnet output is MSVC-like for source errors */
    qs_str_t tool=QS_STR("dotnet");
    qs_size_t count=qs_diag_parse_msvc(a,text,tool,out,cap);
    /* Fix up tool name for entries that came from MSBuild prefix lines */
    return count;
}

/* ══════════════════════════════════════════════════════════════════════════
 * go build: ./file.go:line:col: message   (no severity keyword)
 *           can also be:  # package/path → treated as note
 * ══════════════════════════════════════════════════════════════════════════ */
qs_size_t qs_diag_parse_go(qs_arena_t *a, const char *text,
                             qs_diag_t *out, qs_size_t cap) {
    qs_str_t tool=QS_STR("go");
    qs_size_t count=0;
    line_iter_t it; li_init(&it,text);
    const char *line; qs_size_t llen;
    while (li_next(&it,&line,&llen) && count<cap) {
        if (!llen) continue;
        if (line[0]=='#') { out[count++]=raw_diag(a,line,llen,QS_SEV_NOTE,tool); continue; }
        /* file:line:col: message */
        qs_size_t ci=0; while(ci<llen&&line[ci]!=':') ci++;
        if (ci>=llen) { out[count++]=raw_diag(a,line,llen,QS_SEV_ERROR,tool); continue; }
        qs_size_t file_end=ci;
        qs_size_t ln_s=ci+1,ln_e=ln_s;
        while(ln_e<llen&&isdigit((unsigned char)line[ln_e])) ln_e++;
        qs_u64 lineno=0, colno=0;
        if (ln_e>ln_s) { lineno=parse_u64(line+ln_s,ln_e-ln_s); ci=ln_e; }
        if (ci<llen&&line[ci]==':') {
            qs_size_t co_s=ci+1,co_e=co_s;
            while(co_e<llen&&isdigit((unsigned char)line[co_e])) co_e++;
            if (co_e>co_s) { colno=parse_u64(line+co_s,co_e-co_s); ci=co_e; }
        }
        qs_size_t msg_s=ci; while(msg_s<llen&&(line[msg_s]==':'||line[msg_s]==' ')) msg_s++;

        qs_diag_t d; memset(&d,0,sizeof(d));
        d.sev=QS_SEV_ERROR; d.tool=tool;
        d.raw=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line,llen),llen};
        d.loc.file=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line,file_end),file_end};
        d.loc.line=lineno; d.loc.col=colno;
        d.message=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line+msg_s,llen-msg_s),llen-msg_s};
        out[count++]=d;
    }
    return count;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Python: traceback lines then exception
 *   File "file.py", line ##, in function
 *   ExceptionType: message
 * ══════════════════════════════════════════════════════════════════════════ */
qs_size_t qs_diag_parse_python(qs_arena_t *a, const char *text,
                                 qs_diag_t *out, qs_size_t cap) {
    qs_str_t tool=QS_STR("python");
    qs_size_t count=0;
    line_iter_t it; li_init(&it,text);
    const char *line; qs_size_t llen;
    const char *pending_file=NULL; qs_size_t pf_len=0; qs_u64 pending_line=0;
    while (li_next(&it,&line,&llen) && count<cap) {
        if (!llen) continue;
        /* "  File "foo.py", line 42, in bar" */
        if (llen>8 && strncmp(line,"  File \"",8)==0) {
            const char *fp=line+8;
            const char *fend=(const char*)memchr(fp,'"',(size_t)(llen-8));
            if (fend) {
                pending_file=fp; pf_len=(qs_size_t)(fend-fp);
                const char *lp=strstr(fend,", line ");
                if (lp) {
                    lp+=7; qs_size_t le=0;
                    while(isdigit((unsigned char)lp[le])) le++;
                    pending_line=parse_u64(lp,le);
                }
            }
            continue;
        }
        /* SyntaxError: message (or any ExcClass: msg) — attach location */
        const char *colon=(const char*)memchr(line,':',(size_t)llen);
        if (colon && colon>line && isupper((unsigned char)line[0]) && !isspace((unsigned char)line[0])) {
            qs_size_t exc_len=(qs_size_t)(colon-line);
            qs_bool_t looks_like_exc=QS_TRUE;
            for (qs_size_t k=0;k<exc_len;k++) if (!isalnum((unsigned char)line[k])&&line[k]!='_') { looks_like_exc=QS_FALSE; break; }
            if (looks_like_exc) {
                qs_diag_t d; memset(&d,0,sizeof(d));
                d.sev=QS_SEV_ERROR; d.tool=tool;
                d.raw=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line,llen),llen};
                d.code=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line,exc_len),exc_len};
                qs_size_t ms=(qs_size_t)(colon-line)+1; while(ms<llen&&line[ms]==' ') ms++;
                d.message=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line+ms,llen-ms),llen-ms};
                if (pending_file) {
                    d.loc.file=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,pending_file,pf_len),pf_len};
                    d.loc.line=pending_line;
                    pending_file=NULL;
                }
                out[count++]=d; continue;
            }
        }
        /* SyntaxError pointer line (^) or code line — skip */
    }
    return count;
}

/* ══════════════════════════════════════════════════════════════════════════
 * rustc: emits JSON with --error-format=json. Each line is one JSON object.
 * Fallback: plain text "error[E####]: message\n  --> file:line:col"
 * ══════════════════════════════════════════════════════════════════════════ */
static qs_bool_t json_str_field(const char *json, qs_size_t jlen,
                                  const char *key, char *out, qs_size_t out_cap) {
    /* Tiny inline JSON string extractor — no deps. Only handles flat fields. */
    qs_size_t kl=strlen(key);
    for (qs_size_t i=0;i+kl+4<jlen;i++) {
        if (json[i]!='"') continue;
        if (strncmp(json+i+1,key,kl)==0 && json[i+1+kl]=='"') {
            qs_size_t vs=i+kl+2;
            while(vs<jlen&&(json[vs]==':'||json[vs]==' '||json[vs]=='\t')) vs++;
            if (vs>=jlen||json[vs]!='"') continue;
            vs++;
            qs_size_t ve=vs;
            while(ve<jlen&&json[ve]!='"') ve++;
            qs_size_t cp=ve-vs<out_cap-1?ve-vs:out_cap-1;
            memcpy(out,json+vs,cp); out[cp]='\0';
            return QS_TRUE;
        }
    }
    return QS_FALSE;
}

qs_size_t qs_diag_parse_rustc(qs_arena_t *a, const char *text,
                                qs_diag_t *out, qs_size_t cap) {
    qs_str_t tool=QS_STR("rustc");
    qs_size_t count=0;
    line_iter_t it; li_init(&it,text);
    const char *line; qs_size_t llen;
    while (li_next(&it,&line,&llen) && count<cap) {
        if (!llen) continue;
        /* JSON mode: lines start with '{' */
        if (line[0]=='{') {
            char level[32]="", msg[1024]="", code[16]="";
            char file[512]=""; char lineno[16]="", colno[16]="";
            json_str_field(line,llen,"level",level,sizeof(level));
            json_str_field(line,llen,"message",msg,sizeof(msg));
            json_str_field(line,llen,"code",code,sizeof(code));
            json_str_field(line,llen,"file_name",file,sizeof(file));
            json_str_field(line,llen,"line_start",lineno,sizeof(lineno));
            json_str_field(line,llen,"column_start",colno,sizeof(colno));
            qs_diag_t d; memset(&d,0,sizeof(d));
            d.sev=parse_sev(level,strlen(level)); d.tool=tool;
            d.raw=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line,llen),llen};
            d.code=qs_str_from_cstr(qs_arena_strdup(a,code));
            d.message=qs_str_from_cstr(qs_arena_strdup(a,msg));
            d.loc.file=qs_str_from_cstr(qs_arena_strdup(a,file));
            d.loc.line=parse_u64(lineno,strlen(lineno));
            d.loc.col =parse_u64(colno,strlen(colno));
            out[count++]=d; continue;
        }
        /* Text fallback: "error[E####]: message" */
        if (strncmp(line,"error",5)==0||strncmp(line,"warning",7)==0||strncmp(line,"note",4)==0) {
            qs_severity_t sev=QS_SEV_NOTE;
            qs_size_t ki=0;
            if (strncmp(line,"error",5)==0)   { sev=QS_SEV_ERROR;   ki=5; }
            if (strncmp(line,"warning",7)==0) { sev=QS_SEV_WARNING; ki=7; }
            /* extract [E####] */
            char code[16]="";
            if (ki<llen&&line[ki]=='[') {
                qs_size_t ce=ki+1; while(ce<llen&&line[ce]!=']') ce++;
                qs_size_t cl=ce-ki-1<15?ce-ki-1:15;
                memcpy(code,line+ki+1,cl); code[cl]='\0';
                ki=ce+1;
            }
            while(ki<llen&&(line[ki]==':'||line[ki]==' ')) ki++;
            qs_diag_t d; memset(&d,0,sizeof(d));
            d.sev=sev; d.tool=tool;
            d.raw=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line,llen),llen};
            d.code=qs_str_from_cstr(qs_arena_strdup(a,code));
            d.message=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line+ki,llen-ki),llen-ki};
            out[count++]=d; continue;
        }
        /* "  --> file:line:col" location line */
        if (llen>4&&strncmp(line,"  -->",5)==0) {
            /* attach to last diag */
            if (count>0) {
                const char *fp=line+5; while(*fp==' ') fp++;
                qs_size_t fl=0; while(fl<(qs_size_t)(llen-(fp-line))&&fp[fl]!=':') fl++;
                out[count-1].loc.file=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,fp,fl),fl};
                if (fl<(qs_size_t)(llen-(fp-line))&&fp[fl]==':') {
                    qs_size_t ln_s=fl+1,ln_e=ln_s;
                    while(ln_e<(qs_size_t)(llen-(fp-line))&&isdigit((unsigned char)fp[ln_e])) ln_e++;
                    out[count-1].loc.line=parse_u64(fp+ln_s,ln_e-ln_s);
                }
            }
            continue;
        }
    }
    return count;
}

/* ══════════════════════════════════════════════════════════════════════════
 * zig: src/foo.zig:12:5: error: expected ';', found '}'
 *      note:...
 * ══════════════════════════════════════════════════════════════════════════ */
qs_size_t qs_diag_parse_zig(qs_arena_t *a, const char *text,
                              qs_diag_t *out, qs_size_t cap) {
    qs_str_t tool=QS_STR("zig");
    /* Zig format is identical to clang */
    return qs_diag_parse_clang(a,text,tool,out,cap);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Ruby: file.rb:line:in `method': message (ExceptionClass)
 * ══════════════════════════════════════════════════════════════════════════ */
qs_size_t qs_diag_parse_ruby(qs_arena_t *a, const char *text,
                               qs_diag_t *out, qs_size_t cap) {
    qs_str_t tool=QS_STR("ruby");
    qs_size_t count=0;
    line_iter_t it; li_init(&it,text);
    const char *line; qs_size_t llen;
    while (li_next(&it,&line,&llen) && count<cap) {
        if (!llen) continue;
        /* file:line:in `method': message (ExcClass) */
        qs_size_t ci=0; while(ci<llen&&line[ci]!=':') ci++;
        qs_size_t file_end=ci;
        qs_size_t ln_s=ci+1,ln_e=ln_s;
        while(ln_e<llen&&isdigit((unsigned char)line[ln_e])) ln_e++;
        qs_u64 lineno=parse_u64(line+ln_s,ln_e-ln_s);
        qs_size_t msg_s=ln_e; while(msg_s<llen&&(line[msg_s]==':'||line[msg_s]==' ')) msg_s++;

        qs_diag_t d; memset(&d,0,sizeof(d));
        d.sev=QS_SEV_ERROR; d.tool=tool;
        d.raw=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line,llen),llen};
        d.loc.file=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line,file_end),file_end};
        d.loc.line=lineno;
        /* Extract (ExcClass) from end */
        if (llen>0&&line[llen-1]==')') {
            qs_size_t ep=llen-1; while(ep>0&&line[ep]!='(') ep--;
            if (ep>0) {
                d.code=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line+ep+1,llen-ep-2),llen-ep-2};
                d.message=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line+msg_s,ep-msg_s>0?ep-msg_s-1:0),ep-msg_s>0?ep-msg_s-1:0};
            }
        } else {
            d.message=(qs_str_t){(const qs_u8*)qs_arena_memdup(a,line+msg_s,llen-msg_s),llen-msg_s};
        }
        out[count++]=d;
    }
    return count;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Unified ingest: detect tool, parse, emit into engine
 * ══════════════════════════════════════════════════════════════════════════ */
qs_result_t qs_diag_ingest_tool_output(qs_diag_engine_t *e, qs_arena_t *scratch,
                                         qs_str_t tool, const char *stderr_text,
                                         qs_i64 exit_code) {
    if (!stderr_text||!*stderr_text) {
        if (exit_code!=0) {
            qs_diag_emit_simple(e,QS_SEV_ERROR,
                qs_arena_str_to_cstr(scratch,tool),"QSB-E000",
                QS_LOC_UNKNOWN,"tool exited with non-zero status");
        }
        return QS_OK;
    }
    #define MAX_PARSED 4096
    qs_diag_t *parsed=(qs_diag_t*)qs_arena_alloc(scratch,sizeof(qs_diag_t)*MAX_PARSED,_Alignof(qs_diag_t));
    if (!parsed) return QS_ERROR_OOM;
    qs_size_t n=0;
    if      (qs_str_eq_cstr(tool,"clang")||qs_str_eq_cstr(tool,"clang++"))
        n=qs_diag_parse_clang(scratch,stderr_text,tool,parsed,MAX_PARSED);
    else if (qs_str_eq_cstr(tool,"cl")||qs_str_eq_cstr(tool,"cl.exe"))
        n=qs_diag_parse_msvc(scratch,stderr_text,tool,parsed,MAX_PARSED);
    else if (qs_str_eq_cstr(tool,"gcc")||qs_str_eq_cstr(tool,"g++"))
        n=qs_diag_parse_gcc(scratch,stderr_text,tool,parsed,MAX_PARSED);
    else if (qs_str_eq_cstr(tool,"javac"))
        n=qs_diag_parse_javac(scratch,stderr_text,parsed,MAX_PARSED);
    else if (qs_str_eq_cstr(tool,"dotnet")||qs_str_eq_cstr(tool,"csc"))
        n=qs_diag_parse_dotnet(scratch,stderr_text,parsed,MAX_PARSED);
    else if (qs_str_eq_cstr(tool,"go"))
        n=qs_diag_parse_go(scratch,stderr_text,parsed,MAX_PARSED);
    else if (qs_str_eq_cstr(tool,"python")||qs_str_eq_cstr(tool,"python3"))
        n=qs_diag_parse_python(scratch,stderr_text,parsed,MAX_PARSED);
    else if (qs_str_eq_cstr(tool,"rustc"))
        n=qs_diag_parse_rustc(scratch,stderr_text,parsed,MAX_PARSED);
    else if (qs_str_eq_cstr(tool,"zig"))
        n=qs_diag_parse_zig(scratch,stderr_text,parsed,MAX_PARSED);
    else if (qs_str_eq_cstr(tool,"ruby")||qs_str_eq_cstr(tool,"gem"))
        n=qs_diag_parse_ruby(scratch,stderr_text,parsed,MAX_PARSED);
    else {
        /* Unknown tool: emit as raw note lines */
        line_iter_t it; li_init(&it,stderr_text);
        const char *line; qs_size_t llen;
        while (li_next(&it,&line,&llen)&&n<MAX_PARSED)
            if (llen) parsed[n++]=raw_diag(scratch,(char*)line,llen,QS_SEV_NOTE,tool);
    }
    if (n==0 && exit_code!=0) {
        /* Parser found nothing but tool failed — emit whole stderr as error */
        qs_diag_t d; memset(&d,0,sizeof(d));
        d.sev=QS_SEV_ERROR; d.tool=tool;
        d.message=qs_str_from_cstr(qs_arena_strdup(scratch,stderr_text));
        d.raw=d.message;
        qs_diag_emit(e,d);
    }
    for (qs_size_t i=0;i<n;i++) qs_diag_emit(e,parsed[i]);
    return QS_OK;
}
