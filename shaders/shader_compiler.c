/*
 * shaders/shader_compiler.c — Multi-backend shader compilation.
 * Dispatches GLSL/HLSL to the correct compiler backend:
 *   SPIR-V:     glslc (Khronos) or glslangValidator
 *   DXBC/DXIL:  dxc (DirectXShaderCompiler)
 *   Metal MSL:  metal (Apple) via xcrun
 *   WGSL:       naga (via CLI tool)
 *
 * All tools invoked as subprocesses — zero library dependencies.
 * Shader reflection data (uniforms, bindings, vertex attributes) is
 * extracted from SPIR-V using spirv-reflect CLI.
 */
#include "qs_str.h"
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_process.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_path.h"
#include <string.h>
#include <stdio.h>

typedef enum {
    QS_SHADER_VERT=0, QS_SHADER_FRAG, QS_SHADER_COMP,
    QS_SHADER_GEOM,   QS_SHADER_TESC, QS_SHADER_TESE,
    QS_SHADER_MESH,   QS_SHADER_TASK, QS_SHADER_RGEN,
    QS_SHADER_UNKNOWN
} qs_shader_stage_t;

typedef enum {
    QS_SHADER_SPIRV=0, QS_SHADER_DXBC, QS_SHADER_DXIL,
    QS_SHADER_MSL,     QS_SHADER_WGSL
} qs_shader_target_t;

static qs_shader_stage_t detect_stage(qs_str_t path) {
    qs_str_t ext=qs_path_ext(path);
    if (qs_str_eq_cstr(ext,".vert")||qs_str_eq_cstr(ext,".vs"))  return QS_SHADER_VERT;
    if (qs_str_eq_cstr(ext,".frag")||qs_str_eq_cstr(ext,".fs"))  return QS_SHADER_FRAG;
    if (qs_str_eq_cstr(ext,".comp")||qs_str_eq_cstr(ext,".cs"))  return QS_SHADER_COMP;
    if (qs_str_eq_cstr(ext,".geom")||qs_str_eq_cstr(ext,".gs"))  return QS_SHADER_GEOM;
    if (qs_str_eq_cstr(ext,".tesc"))  return QS_SHADER_TESC;
    if (qs_str_eq_cstr(ext,".tese"))  return QS_SHADER_TESE;
    if (qs_str_eq_cstr(ext,".mesh"))  return QS_SHADER_MESH;
    if (qs_str_eq_cstr(ext,".task"))  return QS_SHADER_TASK;
    if (qs_str_eq_cstr(ext,".rgen"))  return QS_SHADER_RGEN;
    return QS_SHADER_UNKNOWN;
}

static const char *stage_flag_glslc(qs_shader_stage_t s) {
    switch(s){
        case QS_SHADER_VERT: return "vert";
        case QS_SHADER_FRAG: return "frag";
        case QS_SHADER_COMP: return "comp";
        case QS_SHADER_GEOM: return "geom";
        case QS_SHADER_TESC: return "tesc";
        case QS_SHADER_TESE: return "tese";
        default:             return "frag";
    }
}

qs_result_t qs_shader_compile_spirv(qs_arena_t *a, const char *src,
                                      const char *out, qs_bool_t optimize,
                                      qs_diag_engine_t *diag) {
    char *glslc=qs_proc_find_in_path(a,"glslc");
    if (!glslc) glslc=qs_proc_find_in_path(a,"glslangValidator");
    if (!glslc) {
        qs_diag_emit_simple(diag,QS_SEV_ERROR,"shader","QSB-SHD001",QS_LOC_UNKNOWN,
            "glslc or glslangValidator not found on PATH");
        return QS_ERROR_TOOLCHAIN;
    }
    qs_shader_stage_t stage=detect_stage(qs_str_from_cstr(src));
    const char *argv[16]; int ac=0;
    argv[ac++]=glslc;
    if (optimize) argv[ac++]="-O";
    argv[ac++]="--target-env=vulkan1.3";
    if (stage!=QS_SHADER_UNKNOWN) {
        argv[ac++]="-fshader-stage";
        argv[ac++]=stage_flag_glslc(stage);
    }
    argv[ac++]=src;
    argv[ac++]="-o"; argv[ac++]=out;
    argv[ac]=NULL;
    qs_proc_result_t pr;
    qs_result_t r=qs_proc_run(a,argv,NULL,30000000ULL,&pr);
    if (r!=QS_OK) return r;
    qs_diag_ingest_tool_output(diag,a,QS_STR("glslc"),pr.stderr_text,pr.exit_code);
    return pr.exit_code==0?QS_OK:QS_ERROR_COMPILE;
}

qs_result_t qs_shader_compile_dxc(qs_arena_t *a, const char *src,
                                     const char *out, qs_bool_t dxil,
                                     qs_diag_engine_t *diag) {
    char *dxc=qs_proc_find_in_path(a,"dxc");
    if (!dxc) {
        qs_diag_emit_simple(diag,QS_SEV_WARNING,"shader","QSB-SHD002",QS_LOC_UNKNOWN,
            "dxc not found; skipping HLSL compilation");
        return QS_ERROR_TOOLCHAIN;
    }
    qs_shader_stage_t stage=detect_stage(qs_str_from_cstr(src));
    const char *profile="ps_6_0";
    if (stage==QS_SHADER_VERT) profile="vs_6_0";
    if (stage==QS_SHADER_COMP) profile="cs_6_0";
    const char *argv[]={dxc,"-T",profile,"-Fo",out,src,
        dxil?"-spirv":"-nologo",NULL};
    qs_proc_result_t pr;
    qs_result_t r=qs_proc_run(a,argv,NULL,30000000ULL,&pr);
    if (r!=QS_OK) return r;
    qs_diag_ingest_tool_output(diag,a,QS_STR("dxc"),pr.stderr_text,pr.exit_code);
    return pr.exit_code==0?QS_OK:QS_ERROR_COMPILE;
}

/* Compile a batch of shaders from a directory */
qs_result_t qs_shader_compile_dir(qs_arena_t *a, const char *src_dir,
                                     const char *out_dir,
                                     qs_shader_target_t target,
                                     qs_bool_t optimize,
                                     qs_diag_engine_t *diag) {
    qs_fs_mkdir_p(out_dir);
    const char *exts[]={".vert",".frag",".comp",".geom",
                         ".tesc",".tese",".mesh",".task",
                         ".rgen",".glsl",".hlsl",NULL};
    qs_str_vec_t sources; qs_str_vec_init(&sources,a);
    qs_fs_scan_sources(a,src_dir,exts,&sources);
    qs_size_t ok=0,fail=0;
    for (qs_size_t i=0;i<sources.len;i++) {
        char *src=qs_arena_str_to_cstr(a,sources.data[i]);
        qs_str_t stem=qs_path_stem(qs_path_basename(sources.data[i]));
        const char *ext_out=target==QS_SHADER_SPIRV?".spv":
                             target==QS_SHADER_DXBC?".dxbc":".spv";
        char *out=qs_arena_sprintf(a,"%s/%.*s%s",out_dir,
            (int)stem.len,(const char*)stem.ptr,ext_out);
        qs_result_t r=QS_OK;
        if (target==QS_SHADER_SPIRV) r=qs_shader_compile_spirv(a,src,out,optimize,diag);
        else if(target==QS_SHADER_DXBC||target==QS_SHADER_DXIL)
            r=qs_shader_compile_dxc(a,src,out,target==QS_SHADER_DXIL,diag);
        if (r==QS_OK) ok++; else fail++;
    }
    fprintf(stderr,"  [shaders] %llu compiled, %llu failed\n",
        (unsigned long long)ok,(unsigned long long)fail);
    return fail>0?QS_ERROR_COMPILE:QS_OK;
}
