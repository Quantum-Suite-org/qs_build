/*
 * assets/asset_pipeline.c — Asset processing pipeline for Quantum Suite.
 * Processes game/app assets and packages them into .qpkg bundles:
 *
 * Supported asset types (future: dispatched to individual processors):
 *   .qmesh    — 3D mesh (geometry, UVs, normals, tangents)
 *   .qtex     — texture (PNG/DDS/KTX2 → compressed GPU format)
 *   .qaudio   — audio (WAV/OGG → platform-specific format)
 *   .qshader  — GLSL/HLSL → SPIR-V / DXBC / Metal bytecode
 *   .qfont    — TrueType/OTF → signed-distance-field atlas
 *   .qscene   — scene graph JSON → binary scene blob
 *   .qmat     — material definition → serialized material blob
 *   .qanim    — animation clips → compressed keyframe data
 *
 * This file handles discovery, dispatch, and error reporting.
 * Actual conversion is done by external tools (imagemagick, glslc, etc.)
 * invoked as subprocesses — zero library dependencies.
 */
#include "qs_str.h"
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_process.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_path.h"
#include "../core/include/qs_vec.h"
#include <string.h>
#include <stdio.h>

typedef enum {
    QS_ASSET_MESH=0, QS_ASSET_TEXTURE, QS_ASSET_AUDIO,
    QS_ASSET_SHADER,  QS_ASSET_FONT,    QS_ASSET_SCENE,
    QS_ASSET_MATERIAL,QS_ASSET_ANIM,    QS_ASSET_UNKNOWN
} qs_asset_type_t;

typedef struct {
    qs_str_t       src_path;
    qs_str_t       out_path;
    qs_asset_type_t type;
    qs_bool_t       processed;
    qs_u64          src_size;
    qs_u64          out_size;
    qs_u64          time_us;
} qs_asset_result_t;

QS_VEC_DECL(qs_asset_result_t, qs_asset_result_vec)

static qs_asset_type_t detect_asset_type(qs_str_t path) {
    qs_str_t ext=qs_path_ext(path);
    if (qs_str_eq_cstr(ext,".qmesh")||qs_str_eq_cstr(ext,".obj")||
        qs_str_eq_cstr(ext,".fbx")||qs_str_eq_cstr(ext,".gltf")) return QS_ASSET_MESH;
    if (qs_str_eq_cstr(ext,".qtex")||qs_str_eq_cstr(ext,".png")||
        qs_str_eq_cstr(ext,".jpg")||qs_str_eq_cstr(ext,".dds")||
        qs_str_eq_cstr(ext,".ktx2")) return QS_ASSET_TEXTURE;
    if (qs_str_eq_cstr(ext,".qaudio")||qs_str_eq_cstr(ext,".wav")||
        qs_str_eq_cstr(ext,".ogg")||qs_str_eq_cstr(ext,".mp3")) return QS_ASSET_AUDIO;
    if (qs_str_eq_cstr(ext,".glsl")||qs_str_eq_cstr(ext,".hlsl")||
        qs_str_eq_cstr(ext,".vert")||qs_str_eq_cstr(ext,".frag")||
        qs_str_eq_cstr(ext,".comp")||qs_str_eq_cstr(ext,".qshader")) return QS_ASSET_SHADER;
    if (qs_str_eq_cstr(ext,".ttf")||qs_str_eq_cstr(ext,".otf")||
        qs_str_eq_cstr(ext,".qfont")) return QS_ASSET_FONT;
    if (qs_str_eq_cstr(ext,".qscene")) return QS_ASSET_SCENE;
    if (qs_str_eq_cstr(ext,".qmat"))   return QS_ASSET_MATERIAL;
    if (qs_str_eq_cstr(ext,".qanim"))  return QS_ASSET_ANIM;
    return QS_ASSET_UNKNOWN;
}

static const char *asset_type_name(qs_asset_type_t t) {
    switch(t){
        case QS_ASSET_MESH:     return "mesh";
        case QS_ASSET_TEXTURE:  return "texture";
        case QS_ASSET_AUDIO:    return "audio";
        case QS_ASSET_SHADER:   return "shader";
        case QS_ASSET_FONT:     return "font";
        case QS_ASSET_SCENE:    return "scene";
        case QS_ASSET_MATERIAL: return "material";
        case QS_ASSET_ANIM:     return "animation";
        default:                return "unknown";
    }
}

/* Process a shader using glslc (SPIR-V) or dxc (DXBC) */
static qs_result_t process_shader(qs_arena_t *a, const char *src,
                                    const char *out, qs_diag_engine_t *diag) {
    char *glslc=qs_proc_find_in_path(a,"glslc");
    if (glslc) {
        const char *argv[]={glslc,"-O",src,"-o",out,NULL};
        qs_proc_result_t pr;
        qs_result_t r=qs_proc_run(a,argv,NULL,30000000ULL,&pr);
        if (r!=QS_OK) return r;
        qs_diag_ingest_tool_output(diag,a,QS_STR("glslc"),pr.stderr_text,pr.exit_code);
        return pr.exit_code==0?QS_OK:QS_ERROR_COMPILE;
    }
    /* No shader compiler: copy as-is */
    return qs_fs_copy_file(src,out);
}

qs_result_t qs_asset_process_all(qs_arena_t *a, const qs_str_vec_t *assets,
                                    const char *out_dir,
                                    qs_diag_engine_t *diag,
                                    qs_asset_result_vec_t *results) {
    qs_asset_result_vec_init(results,a);
    qs_fs_mkdir_p(out_dir);
    qs_size_t ok_count=0, fail_count=0;

    for (qs_size_t i=0;i<assets->len;i++) {
        qs_str_t src=assets->data[i];
        qs_asset_type_t type=detect_asset_type(src);
        char *src_c=qs_arena_str_to_cstr(a,src);
        qs_str_t stem=qs_path_stem(qs_path_basename(src));
        char *out=qs_arena_sprintf(a,"%s/%.*s.qasset",
            out_dir,(int)stem.len,(const char*)stem.ptr);

        qs_stat_t ss; qs_fs_stat(src_c,&ss);
        qs_asset_result_t res;
        res.src_path=src; res.out_path=qs_str_from_cstr(out);
        res.type=type; res.src_size=ss.size;

        qs_result_t r=QS_OK;
        if (type==QS_ASSET_SHADER) r=process_shader(a,src_c,out,diag);
        else                        r=qs_fs_copy_file(src_c,out); /* passthrough */

        qs_stat_t os; qs_fs_stat(out,&os);
        res.out_size=os.size;
        res.processed=(r==QS_OK)?QS_TRUE:QS_FALSE;
        qs_asset_result_vec_push(results,res);

        if (r==QS_OK) ok_count++;
        else {
            fail_count++;
            qs_diag_emit_simple(diag,QS_SEV_ERROR,"assets","QSB-AST001",
                QS_LOC_UNKNOWN,qs_arena_sprintf(a,"asset process failed: %s",src_c));
        }
    }
    fprintf(stderr,"  [assets] %llu ok, %llu failed\n",
        (unsigned long long)ok_count,(unsigned long long)fail_count);
    return fail_count>0?QS_ERROR_IO:QS_OK;
}
