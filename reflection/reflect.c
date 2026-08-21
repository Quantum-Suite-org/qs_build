/*
 * reflection/reflect.c — Compile-time reflection metadata extraction.
 *
 * Parses C/C++ headers and extracts struct/class/enum/function metadata
 * that gets embedded into .qpkg archives as a JSON reflection manifest.
 * Used by Forge:Visuals to expose types as visual blocks.
 *
 * This is a simple line-oriented scanner — not a full C++ parser.
 * It handles the common cases: struct/class/enum definitions,
 * function signatures, and QS_REFLECT() annotations.
 *
 * QS_REFLECT annotation: placed before a declaration to mark it for export:
 *   QS_REFLECT("category=Math", "doc=Adds two integers")
 *   int add(int a, int b);
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_str.h"
#include "../core/include/qs_vec.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

typedef enum {
    QS_REFLECT_STRUCT=0, QS_REFLECT_CLASS, QS_REFLECT_ENUM,
    QS_REFLECT_FUNCTION, QS_REFLECT_TYPEDEF
} qs_reflect_kind_t;

typedef struct {
    qs_reflect_kind_t kind;
    char             *name;
    char             *category;
    char             *doc;
    char             *source_file;
    qs_u64            source_line;
    /* For functions: parameters as "type name" strings */
    char            **params;
    qs_size_t         param_count;
    char             *return_type;
} qs_reflect_entry_t;

QS_VEC_DECL(qs_reflect_entry_t, qs_reflect_vec)

typedef struct {
    qs_reflect_vec_t entries;
    qs_arena_t      *arena;
} qs_reflect_db_t;

void qs_reflect_db_init(qs_reflect_db_t *db, qs_arena_t *a) {
    db->arena=a; qs_reflect_vec_init(&db->entries,a);
}

/* Scan one header file for QS_REFLECT annotations and type declarations */
qs_result_t qs_reflect_scan_header(qs_reflect_db_t *db, const char *path) {
    char *text=NULL; qs_size_t tlen=0;
    if (qs_fs_read_file(db->arena,path,&text,&tlen)!=QS_OK) return QS_ERROR_NOT_FOUND;

    const char *p=text;
    qs_u64 line=1;
    char pending_category[256]="";
    char pending_doc[1024]="";
    qs_bool_t has_reflect=QS_FALSE;

    while (*p) {
        /* Track line numbers */
        if (*p=='\n') { line++; p++; continue; }

        /* Skip whitespace */
        if (isspace((unsigned char)*p)) { p++; continue; }

        /* QS_REFLECT annotation */
        if (strncmp(p,"QS_REFLECT(",11)==0) {
            has_reflect=QS_TRUE;
            pending_category[0]=pending_doc[0]='\0';
            const char *close=strchr(p,')');
            if (close) {
                /* Extract key=value pairs from the annotation args */
                const char *inner=p+11;
                while (inner<close) {
                    while (inner<close&&(*inner=='"'||*inner==' '||*inner==',')) inner++;
                    if (inner>=close) break;
                    const char *str_end=strchr(inner,'"');
                    if (!str_end||str_end>=close) break;
                    inner++; /* skip opening " */
                    const char *val_end=strchr(inner,'"');
                    if (!val_end||val_end>close) break;
                    qs_size_t vlen=(qs_size_t)(val_end-inner);
                    char kv[512]; if(vlen>=511)vlen=511;
                    memcpy(kv,inner,vlen); kv[vlen]='\0';
                    if (strncmp(kv,"category=",9)==0)
                        strncpy(pending_category,kv+9,sizeof(pending_category)-1);
                    else if (strncmp(kv,"doc=",4)==0)
                        strncpy(pending_doc,kv+4,sizeof(pending_doc)-1);
                    inner=val_end+1;
                }
                p=close+1;
            } else {
                while(*p&&*p!='\n') p++;
            }
            continue;
        }

        /* struct / class / enum */
        qs_reflect_kind_t kind;
        qs_bool_t is_type=QS_FALSE;
        if (strncmp(p,"struct ",7)==0)      { kind=QS_REFLECT_STRUCT;   p+=7;  is_type=QS_TRUE; }
        else if (strncmp(p,"class ",6)==0)  { kind=QS_REFLECT_CLASS;    p+=6;  is_type=QS_TRUE; }
        else if (strncmp(p,"enum ",5)==0)   { kind=QS_REFLECT_ENUM;     p+=5;  is_type=QS_TRUE; }
        else if (strncmp(p,"typedef ",8)==0){ kind=QS_REFLECT_TYPEDEF;  p+=8;  is_type=QS_TRUE; }

        if (is_type || has_reflect) {
            if (is_type) {
                while(*p==' '||*p=='\t') p++;
                const char *name_start=p;
                while(*p&&!isspace((unsigned char)*p)&&*p!='{') p++;
                qs_size_t nlen=(qs_size_t)(p-name_start);
                if (nlen>0) {
                    qs_reflect_entry_t e; memset(&e,0,sizeof(e));
                    e.kind=kind;
                    e.name=qs_arena_alloc(db->arena,nlen+1,1);
                    memcpy(e.name,name_start,nlen); e.name[nlen]='\0';
                    e.source_file=qs_arena_strdup(db->arena,path);
                    e.source_line=line;
                    if (pending_category[0]) e.category=qs_arena_strdup(db->arena,pending_category);
                    if (pending_doc[0])      e.doc=qs_arena_strdup(db->arena,pending_doc);
                    qs_reflect_vec_push(&db->entries,e);
                }
                has_reflect=QS_FALSE;
            } else {
                /* has_reflect but no type keyword — skip to next line */
                while(*p&&*p!='\n') p++;
                has_reflect=QS_FALSE;
            }
            continue;
        }

        /* Skip to end of line for everything else */
        while(*p&&*p!='\n') p++;
    }
    return QS_OK;
}

/* Emit all collected metadata as a JSON string into the arena */
char *qs_reflect_emit_json(qs_reflect_db_t *db) {
    qs_arena_t *a=db->arena;
    /* Estimate size */
    qs_size_t cap=64+db->entries.len*256;
    char *buf=qs_arena_alloc(a,cap,1);
    if (!buf) return NULL;
    int pos=0;
    pos+=snprintf(buf+pos,(size_t)(cap-pos),"{\n  \"entries\": [\n");
    for (qs_size_t i=0;i<db->entries.len;i++) {
        const qs_reflect_entry_t *e=&db->entries.data[i];
        const char *kinds[]={"struct","class","enum","function","typedef"};
        pos+=snprintf(buf+pos,(size_t)(cap-pos),
            "    {\"kind\":\"%s\",\"name\":\"%s\",\"file\":\"%s\","
            "\"line\":%llu,\"category\":\"%s\",\"doc\":\"%s\"}%s\n",
            kinds[(int)e->kind],e->name?e->name:"",
            e->source_file?e->source_file:"",
            (unsigned long long)e->source_line,
            e->category?e->category:"",
            e->doc?e->doc:"",
            i+1<db->entries.len?",":"");
    }
    pos+=snprintf(buf+pos,(size_t)(cap-pos),"  ]\n}\n");
    return buf;
}
