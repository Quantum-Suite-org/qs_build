/*
 * linker/linker.c — Unified linker driver for all output types.
 *
 * Dispatches to the correct linker based on toolchain and output type:
 *   - clang/gcc:  clang/g++ -o (letting the compiler driver call lld/ld)
 *   - MSVC:       link.exe
 *   - Zig:        handled entirely by zig build (not called separately)
 *   - Go/Python/Ruby/Java/.NET: packaging handled in their own drivers
 *
 * Output type → linker flags:
 *   exe       → -o name          (POSIX) / /OUT:name.exe (MSVC)
 *   dll       → -shared          / /DLL
 *   lib       → ar rcs           / lib.exe
 *   app       → -o name.app (macOS bundle stub)
 *   cdylib    → -shared -fPIC    / /DLL
 *   object    → no linking (object pass-through)
 */
#include "../core/include/qs_linker.h"
#include "../core/include/qs_process.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_diag.h"
#include <string.h>
#include <stdio.h>

#define ARGV_MAX 1024
static int lk_ac; static const char *lk_av[ARGV_MAX];
#define PUSH(s)      do{if(lk_ac<ARGV_MAX-1){lk_av[lk_ac++]=(s);lk_av[lk_ac]=NULL;}}while(0)
#define PUSHF(f,...) PUSH(qs_arena_sprintf(a,f,__VA_ARGS__))

/* Platform-correct output filename */
char *qs_linker_output_name(qs_arena_t *a, qs_output_type_t t, const char *base) {
    switch(t) {
        case QS_OUT_EXE:
#ifdef _WIN32
            return qs_arena_sprintf(a,"%s.exe",base);
#else
            return qs_arena_strdup(a,base);
#endif
        case QS_OUT_DLL:
        case QS_OUT_CDYLIB:
        case QS_OUT_SHAREDLIB:
#ifdef _WIN32
            return qs_arena_sprintf(a,"%s.dll",base);
#elif defined(__APPLE__)
            return qs_arena_sprintf(a,"lib%s.dylib",base);
#else
            return qs_arena_sprintf(a,"lib%s.so",base);
#endif
        case QS_OUT_LIB:
        case QS_OUT_STATICLIB:
#ifdef _WIN32
            return qs_arena_sprintf(a,"%s.lib",base);
#else
            return qs_arena_sprintf(a,"lib%s.a",base);
#endif
        case QS_OUT_APP:
            return qs_arena_sprintf(a,"%s.app",base);
        case QS_OUT_QPKG:
            return qs_arena_sprintf(a,"%s.qpkg",base);
        default:
            return qs_arena_strdup(a,base);
    }
}

/* Link via ar (static library) */
static qs_result_t link_ar(qs_arena_t *a, const qs_link_config_t *cfg,
                              qs_diag_engine_t *diag) {
    char *ar = qs_proc_find_in_path(a,"ar");
    if (!ar) ar=(char*)"ar";
    lk_ac=0;
    PUSH(ar); PUSH("rcs"); PUSH(cfg->output_path);
    for (qs_size_t i=0;i<cfg->object_files.len;i++)
        PUSH((const char*)cfg->object_files.data[i].ptr);
    qs_proc_result_t pr;
    qs_result_t r=qs_proc_run(a,lk_av,NULL,120000000ULL,&pr);
    if (r!=QS_OK) return r;
    if (pr.exit_code!=0)
        qs_diag_emit_simple(diag,QS_SEV_ERROR,"ar","QSB-LNK001",QS_LOC_UNKNOWN,pr.stderr_text);
    return pr.exit_code==0?QS_OK:QS_ERROR_LINK;
}

/* Link via MSVC lib.exe (static library on Windows) */
static qs_result_t link_msvc_lib(qs_arena_t *a, const qs_link_config_t *cfg,
                                   qs_diag_engine_t *diag) {
    lk_ac=0;
    PUSH("lib.exe"); PUSH("/nologo");
    PUSHF("/OUT:%s",cfg->output_path);
    for (qs_size_t i=0;i<cfg->object_files.len;i++)
        PUSH((const char*)cfg->object_files.data[i].ptr);
    qs_proc_result_t pr;
    qs_result_t r=qs_proc_run(a,lk_av,NULL,120000000ULL,&pr);
    if (r!=QS_OK) return r;
    qs_diag_ingest_tool_output(diag,a,QS_STR("cl"),pr.stderr_text,pr.exit_code);
    return pr.exit_code==0?QS_OK:QS_ERROR_LINK;
}

qs_result_t qs_linker_link(qs_arena_t *a, const qs_link_config_t *cfg,
                              const qs_toolchain_t *tc, qs_diag_engine_t *diag) {
    /* Static libraries use ar/lib.exe, not the compiler driver */
    if (cfg->output_type==QS_OUT_LIB||cfg->output_type==QS_OUT_STATICLIB) {
        return tc->is_msvc ? link_msvc_lib(a,cfg,diag) : link_ar(a,cfg,diag);
    }

    lk_ac=0;

    if (tc->is_msvc) {
        /* MSVC linking must use link.exe, NOT cl.exe.
         * cl.exe with object files tries to re-compile them. */
        char *link_exe = qs_proc_find_in_path(a, "link.exe");
        if (!link_exe) link_exe = qs_proc_find_in_path(a, "link");
        if (!link_exe) link_exe = (char*)"link.exe"; /* last resort */
        PUSH(link_exe);
        PUSH("/nologo");
        if (cfg->output_type==QS_OUT_DLL||cfg->output_type==QS_OUT_CDYLIB||
            cfg->output_type==QS_OUT_SHAREDLIB)
            PUSH("/DLL");
        PUSHF("/OUT:%s",cfg->output_path);
        if (cfg->enable_lto) PUSH("/LTCG");
        if (cfg->strip_debug) PUSH("/RELEASE");
        for (qs_size_t i=0;i<cfg->lib_dirs.len;i++)
            PUSHF("/LIBPATH:%.*s",(int)cfg->lib_dirs.data[i].len,(const char*)cfg->lib_dirs.data[i].ptr);
        for (qs_size_t i=0;i<cfg->link_libs.len;i++)
            PUSHF("%.*s.lib",(int)cfg->link_libs.data[i].len,(const char*)cfg->link_libs.data[i].ptr);
        for (qs_size_t i=0;i<cfg->object_files.len;i++)
            PUSH((const char*)cfg->object_files.data[i].ptr);
        for (qs_size_t i=0;i<cfg->extra_flags.len;i++)
            PUSH((const char*)cfg->extra_flags.data[i].ptr);
        /* Default libs required by the CRT and Win32 subsystem.
         * link.exe /nodefaultlib is NOT set, so MSVC auto-links the CRT,
         * but kernel32 + user32 must be explicit for PE subsystem setup. */
        if (cfg->output_type==QS_OUT_EXE||
            cfg->output_type==QS_OUT_DLL||
            cfg->output_type==QS_OUT_CDYLIB||
            cfg->output_type==QS_OUT_SHAREDLIB) {
            PUSH("kernel32.lib");
            PUSH("user32.lib");
        }
    } else {
        /* GCC/Clang driver */
        switch(cfg->output_type) {
            case QS_OUT_DLL:
            case QS_OUT_CDYLIB:
            case QS_OUT_SHAREDLIB:
                PUSH("-shared"); PUSH("-fPIC"); break;
            case QS_OUT_APP:
#ifdef __APPLE__
                PUSH("-dynamiclib");
#else
                PUSH("-shared");
#endif
                break;
            default: break; /* exe */
        }
        if (cfg->enable_lto) PUSH("-flto");
        if (cfg->strip_debug) PUSH("-s");
        PUSH("-o"); PUSH(cfg->output_path);
        /* Objects first */
        for (qs_size_t i=0;i<cfg->object_files.len;i++)
            PUSH((const char*)cfg->object_files.data[i].ptr);
        for (qs_size_t i=0;i<cfg->lib_dirs.len;i++)
            PUSHF("-L%.*s",(int)cfg->lib_dirs.data[i].len,(const char*)cfg->lib_dirs.data[i].ptr);
        for (qs_size_t i=0;i<cfg->link_libs.len;i++)
            PUSHF("-l%.*s",(int)cfg->link_libs.data[i].len,(const char*)cfg->link_libs.data[i].ptr);
        for (qs_size_t i=0;i<cfg->extra_flags.len;i++)
            PUSH((const char*)cfg->extra_flags.data[i].ptr);
        /* Always link against libm and libpthread on Linux */
#ifdef __linux__
        PUSH("-lm"); PUSH("-lpthread"); PUSH("-ldl");
#endif
    }

    qs_proc_result_t pr;
    qs_result_t r=qs_proc_run(a,lk_av,NULL,300000000ULL,&pr);
    if (r!=QS_OK) return r;

    const char *tool = tc->is_msvc?"link":
                        strstr(tc->name,"clang")?"clang":"gcc";
    const char *err=pr.stderr_text[0]?pr.stderr_text:pr.stdout_text;
    qs_diag_ingest_tool_output(diag,a,qs_str_from_cstr(tool),err,pr.exit_code);
    return pr.exit_code==0?QS_OK:QS_ERROR_LINK;
}

qs_result_t qs_linker_build_argv(qs_arena_t *a, const qs_link_config_t *cfg,
                                    const qs_toolchain_t *tc,
                                    const char ***argv_out, qs_size_t *argc_out) {
    /* Dry-run: just capture the argv without executing */
    qs_diag_engine_t dummy; qs_arena_t dummy_a; qs_arena_init(&dummy_a);
    qs_diag_engine_init(&dummy,&dummy_a);
    lk_ac=0;
    /* Re-use the same logic */
    qs_linker_link(a,cfg,tc,&dummy);
    *argv_out=lk_av;
    *argc_out=(qs_size_t)lk_ac;
    qs_arena_destroy(&dummy_a);
    return QS_OK;
}
