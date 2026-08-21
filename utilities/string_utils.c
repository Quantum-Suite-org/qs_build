/*
 * utilities/string_utils.c — Extra string utilities for qs_build.
 * Supplements core/util/str.c with higher-level operations:
 * word-wrap for help text, table formatting for diagnostic summaries,
 * glob matching for source filters, and shell quoting.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_str.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* Word-wrap `text` at `width` columns, indent continuation lines by `indent` spaces */
void qs_word_wrap(const char *text, int width, int indent, FILE *out) {
    int col=0;
    const char *p=text;
    while (*p) {
        if (*p=='\n') { fputc('\n',out); col=0; p++; continue; }
        /* Find end of next word */
        const char *ws=p;
        while(*ws&&!isspace((unsigned char)*ws)) ws++;
        int wlen=(int)(ws-p);
        if (col>0 && col+1+wlen>width) {
            fputc('\n',out);
            for(int i=0;i<indent;i++) fputc(' ',out);
            col=indent;
        } else if (col>indent) {
            fputc(' ',out); col++;
        }
        fwrite(p,1,(size_t)wlen,out); col+=wlen;
        p=ws;
        while(*p==' '||*p=='\t') p++;
    }
    fputc('\n',out);
}

/* Print a two-column table: left-pad right column to `right_col` */
void qs_print_table_row(const char *left, const char *right, int right_col) {
    int ll=(int)strlen(left);
    fprintf(stderr,"  %s",left);
    for (int i=ll;i<right_col-2;i++) fputc(' ',stderr);
    fprintf(stderr,"%s\n",right);
}

/* Glob match: supports * (any substring) and ? (any single char) */
qs_bool_t qs_glob_match(const char *pattern, const char *str) {
    if (!*pattern) return !*str?QS_TRUE:QS_FALSE;
    if (*pattern=='*') {
        while(*pattern=='*') pattern++;
        if (!*pattern) return QS_TRUE;
        while(*str) {
            if (qs_glob_match(pattern,str)) return QS_TRUE;
            str++;
        }
        return QS_FALSE;
    }
    if (*pattern=='?'||*pattern==*str)
        return qs_glob_match(pattern+1,str+1);
    return QS_FALSE;
}

/* Shell-quote a string (single-quote style for POSIX) */
char *qs_shell_quote(qs_arena_t *a, const char *s) {
    qs_size_t len=strlen(s);
    /* Worst case: every char is a single-quote → 4x expansion */
    char *out=qs_arena_alloc(a,len*4+3,1);
    char *p=out; *p++='\'';
    for(qs_size_t i=0;i<len;i++){
        if(s[i]=='\''){*p++='\'';;*p++='\\';*p++='\'';*p++='\'';}
        else *p++=s[i];
    }
    *p++='\''; *p='\0';
    return out;
}

/* Levenshtein distance for typo suggestions in CLI flag names */
static int levenshtein(const char *a, int la, const char *b, int lb) {
    if(la==0) return lb; if(lb==0) return la;
    int dp[128][128]; /* max 127 chars each */
    if(la>127) la=127; if(lb>127) lb=127;
    for(int i=0;i<=la;i++) dp[i][0]=i;
    for(int j=0;j<=lb;j++) dp[0][j]=j;
    for(int i=1;i<=la;i++)
        for(int j=1;j<=lb;j++){
            int cost=a[i-1]!=b[j-1];
            int del=dp[i-1][j]+1, ins=dp[i][j-1]+1, sub=dp[i-1][j-1]+cost;
            dp[i][j]=del<ins?(del<sub?del:sub):(ins<sub?ins:sub);
        }
    return dp[la][lb];
}

/* Suggest the closest known flag to an unknown one */
const char *qs_suggest_flag(const char *unknown, const char *const *known, qs_size_t n) {
    int best_dist=999; const char *best=NULL;
    int ul=(int)strlen(unknown);
    for(qs_size_t i=0;i<n;i++){
        int kl=(int)strlen(known[i]);
        int d=levenshtein(unknown,ul,known[i],kl);
        if(d<best_dist){best_dist=d;best=known[i];}
    }
    return best_dist<=3?best:NULL; /* only suggest if very close */
}
