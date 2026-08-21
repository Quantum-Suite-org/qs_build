/*
 * parser/ast.c — Abstract Syntax Tree nodes for build.qs manifests.
 * The parser (manifest/parser.c) produces AST nodes; downstream passes
 * (validator, code generator, IDE tooling) consume them.
 * Nodes are arena-allocated; no manual frees needed.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_vec.h"
#include <string.h>
#include <stdio.h>

typedef enum {
    AST_MODULE=0,    /* top-level module { } declaration */
    AST_FIELD,       /* key = value ; */
    AST_STRING_LIT,  /* "hello" */
    AST_NUMBER_LIT,  /* 42 */
    AST_BOOL_LIT,    /* true | false */
    AST_STRING_LIST, /* [ "a", "b" ] */
} ast_kind_t;

typedef struct ast_node ast_node_t;
struct ast_node {
    ast_kind_t    kind;
    qs_str_t      text;      /* raw token text for leaves */
    qs_u64        line;
    ast_node_t   *parent;
    ast_node_t  **children;
    qs_size_t     child_count;
    qs_size_t     child_cap;
    qs_arena_t   *arena;
};

ast_node_t *ast_node_new(qs_arena_t *a, ast_kind_t kind, qs_str_t text, qs_u64 line) {
    ast_node_t *n = QS_ARENA_NEW_Z(a, ast_node_t);
    n->kind       = kind;
    n->text       = text;
    n->line       = line;
    n->arena      = a;
    return n;
}

qs_result_t ast_node_add_child(ast_node_t *parent, ast_node_t *child) {
    if (parent->child_count >= parent->child_cap) {
        qs_size_t ncap = parent->child_cap ? parent->child_cap*2 : 4;
        ast_node_t **nc = (ast_node_t**)qs_arena_realloc(
            parent->arena, parent->children,
            sizeof(ast_node_t*)*parent->child_cap,
            sizeof(ast_node_t*)*ncap, _Alignof(ast_node_t*));
        if (!nc) return QS_ERROR_OOM;
        parent->children  = nc;
        parent->child_cap = ncap;
    }
    parent->children[parent->child_count++] = child;
    child->parent = parent;
    return QS_OK;
}

static void print_indent(int depth) {
    for(int i=0;i<depth*2;i++) fputc(' ',stderr);
}

void ast_dump(const ast_node_t *n, int depth) {
    static const char *KINDS[]={"MODULE","FIELD","STRING","NUMBER","BOOL","LIST"};
    print_indent(depth);
    fprintf(stderr,"%s",KINDS[n->kind<6?n->kind:0]);
    if (n->text.len)
        fprintf(stderr," '%.*s'",(int)n->text.len,(const char*)n->text.ptr);
    fprintf(stderr,"  (line %llu)\n",(unsigned long long)n->line);
    for (qs_size_t i=0;i<n->child_count;i++)
        ast_dump(n->children[i],depth+1);
}
