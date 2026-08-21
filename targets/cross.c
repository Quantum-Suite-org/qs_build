/*
 * targets/cross.c — Cross-compilation target triple parsing and mapping.
 *
 * Parses standard LLVM/GNU target triples:
 *   <arch>-<vendor>-<os>[-<env>]
 * e.g.: x86_64-unknown-linux-gnu
 *       aarch64-apple-darwin
 *       x86_64-pc-windows-msvc
 *       wasm32-unknown-unknown
 *       riscv64gc-unknown-linux-gnu
 *
 * Maps to qs_build's per-language cross flags.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_str.h"
#include <string.h>
#include "qs_vec.h"
#include "qs_str.h"
#include <stdio.h>

typedef enum {
    QS_ARCH_X86_64=0, QS_ARCH_X86, QS_ARCH_AARCH64, QS_ARCH_ARM,
    QS_ARCH_RISCV64, QS_ARCH_WASM32, QS_ARCH_WASM64, QS_ARCH_UNKNOWN
} qs_arch_t;

typedef enum {
    QS_OS_LINUX=0, QS_OS_MACOS, QS_OS_WINDOWS, QS_OS_WASI,
    QS_OS_FREEBSD, QS_OS_ANDROID, QS_OS_UNKNOWN
} qs_os_t;

typedef enum {
    QS_ENV_GNU=0, QS_ENV_MSVC, QS_ENV_MUSL, QS_ENV_GNUEABIHF,
    QS_ENV_ANDROID, QS_ENV_UNKNOWN
} qs_env_t;

typedef struct {
    char       raw[128];    /* original triple string */
    qs_arch_t  arch;
    qs_os_t    os;
    qs_env_t   env;
    qs_bool_t  is_cross;   /* false = native host */
} qs_target_t;

qs_result_t qs_target_parse(const char *triple, qs_target_t *out) {
    memset(out,0,sizeof(*out));
    if (!triple||!*triple) { out->is_cross=QS_FALSE; return QS_OK; }
    strncpy(out->raw,triple,sizeof(out->raw)-1);
    out->is_cross=QS_TRUE;

    /* Arch */
    if (strstr(triple,"x86_64")||strstr(triple,"amd64"))  out->arch=QS_ARCH_X86_64;
    else if (strstr(triple,"i686")||strstr(triple,"i386")) out->arch=QS_ARCH_X86;
    else if (strstr(triple,"aarch64")||strstr(triple,"arm64")) out->arch=QS_ARCH_AARCH64;
    else if (strstr(triple,"arm"))   out->arch=QS_ARCH_ARM;
    else if (strstr(triple,"riscv64")) out->arch=QS_ARCH_RISCV64;
    else if (strstr(triple,"wasm32")) out->arch=QS_ARCH_WASM32;
    else if (strstr(triple,"wasm64")) out->arch=QS_ARCH_WASM64;
    else out->arch=QS_ARCH_UNKNOWN;

    /* OS */
    if (strstr(triple,"linux"))   out->os=QS_OS_LINUX;
    else if (strstr(triple,"darwin")||strstr(triple,"macos")) out->os=QS_OS_MACOS;
    else if (strstr(triple,"windows")) out->os=QS_OS_WINDOWS;
    else if (strstr(triple,"wasi"))    out->os=QS_OS_WASI;
    else if (strstr(triple,"freebsd")) out->os=QS_OS_FREEBSD;
    else if (strstr(triple,"android")) out->os=QS_OS_ANDROID;
    else out->os=QS_OS_UNKNOWN;

    /* Env */
    if (strstr(triple,"msvc"))       out->env=QS_ENV_MSVC;
    else if (strstr(triple,"musl"))  out->env=QS_ENV_MUSL;
    else if (strstr(triple,"eabihf"))out->env=QS_ENV_GNUEABIHF;
    else if (strstr(triple,"android"))out->env=QS_ENV_ANDROID;
    else                              out->env=QS_ENV_GNU;
    return QS_OK;
}

/* Build clang --target= and sysroot flags for cross-compilation */
void qs_target_clang_flags(qs_arena_t *a, const qs_target_t *t,
                             const char *sysroot, qs_str_vec_t *flags) {
    if (!t->is_cross) return;
    qs_str_vec_push(flags, qs_str_from_cstr(qs_arena_sprintf(a,"--target=%s",t->raw)));
    if (sysroot && *sysroot)
        qs_str_vec_push(flags, qs_str_from_cstr(qs_arena_sprintf(a,"--sysroot=%s",sysroot)));
}

/* Build Go GOOS/GOARCH env entries */
void qs_target_go_env(qs_arena_t *a, const qs_target_t *t,
                       char **goos_out, char **goarch_out) {
    if (!t->is_cross) { *goos_out=*goarch_out=NULL; return; }
    switch(t->os) {
        case QS_OS_LINUX:   *goos_out=qs_arena_strdup(a,"GOOS=linux");   break;
        case QS_OS_MACOS:   *goos_out=qs_arena_strdup(a,"GOOS=darwin");  break;
        case QS_OS_WINDOWS: *goos_out=qs_arena_strdup(a,"GOOS=windows"); break;
        case QS_OS_FREEBSD: *goos_out=qs_arena_strdup(a,"GOOS=freebsd"); break;
        default:            *goos_out=NULL; break;
    }
    switch(t->arch) {
        case QS_ARCH_X86_64:  *goarch_out=qs_arena_strdup(a,"GOARCH=amd64");  break;
        case QS_ARCH_AARCH64: *goarch_out=qs_arena_strdup(a,"GOARCH=arm64");  break;
        case QS_ARCH_X86:     *goarch_out=qs_arena_strdup(a,"GOARCH=386");    break;
        case QS_ARCH_RISCV64: *goarch_out=qs_arena_strdup(a,"GOARCH=riscv64");break;
        default:              *goarch_out=NULL; break;
    }
}

void qs_target_print(const qs_target_t *t) {
    if (!t->is_cross) { fprintf(stderr,"  target: native host\n"); return; }
    fprintf(stderr,"  target: %s  (arch=%d os=%d env=%d)\n",
        t->raw,(int)t->arch,(int)t->os,(int)t->env);
}
