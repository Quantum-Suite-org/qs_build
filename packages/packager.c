/*
 * packages/packager.c — .qpkg archive writer.
 *
 * .qpkg format (custom, zero deps):
 *   Header:  magic[8] + version[4] + entry_count[8] + metadata_len[8]
 *   Metadata: JSON blob (name, version, language, platform, deps)
 *   Entries: for each file:
 *     path_len[8] + path[path_len] + data_len[8] + data[data_len]
 *
 * All integers are little-endian. No compression in v1 (future: zstd).
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_hash.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define QPKG_MAGIC "QSPKG001"  /* 8 bytes */
#define QPKG_VERSION 1

static void write_u64_le(FILE *f, qs_u64 v) {
    qs_u8 b[8];
    for (int i=0;i<8;i++) { b[i]=(qs_u8)(v&0xFF); v>>=8; }
    fwrite(b,1,8,f);
}

static qs_result_t pack_file(qs_arena_t *a, FILE *out,
                               const char *arc_path, const char *disk_path) {
    char *data=NULL; qs_size_t dlen=0;
    qs_result_t r=qs_fs_read_file(a,disk_path,&data,&dlen);
    if (r!=QS_OK) return r;
    qs_size_t plen=strlen(arc_path);
    write_u64_le(out,(qs_u64)plen);
    fwrite(arc_path,1,plen,out);
    write_u64_le(out,(qs_u64)dlen);
    if (dlen) fwrite(data,1,(size_t)dlen,out);
    return QS_OK;
}

qs_result_t qs_qpkg_write(qs_arena_t *a, const qs_manifest_t *m,
                            const char *out_dir, const char *const *file_paths,
                            qs_size_t file_count, qs_diag_engine_t *diag) {
    char *out_path=qs_arena_sprintf(a,"%s/%.*s.qpkg",
        out_dir,(int)m->name.len,(const char*)m->name.ptr);
    FILE *f=fopen(out_path,"wb");
    if (!f) {
        qs_diag_emit_simple(diag,QS_SEV_ERROR,"packager","QSB-PKG001",
            QS_LOC_UNKNOWN,qs_arena_sprintf(a,"cannot create qpkg: %s",out_path));
        return QS_ERROR_IO;
    }

    /* Header */
    fwrite(QPKG_MAGIC,1,8,f);
    write_u64_le(f,(qs_u64)QPKG_VERSION);
    write_u64_le(f,(qs_u64)file_count);

    /* Metadata JSON */
    char meta[4096];
    int mlen=snprintf(meta,sizeof(meta),
        "{\"name\":\"%.*s\",\"version\":\"%.*s\",\"language\":\"%s\","
        "\"output_type\":\"%s\",\"file_count\":%llu}",
        (int)m->name.len,(const char*)m->name.ptr,
        (int)m->version.len,QS_STR_IS_EMPTY(m->version)?(const qs_u8*)"0.1.0":m->version.ptr,
        qs_lang_name(m->language),
        qs_output_type_str(m->output_type),
        (unsigned long long)file_count);
    write_u64_le(f,(qs_u64)mlen);
    fwrite(meta,1,(size_t)mlen,f);

    /* Files */
    qs_size_t packed=0;
    for (qs_size_t i=0;i<file_count;i++) {
        qs_result_t r=pack_file(a,f,file_paths[i],file_paths[i]);
        if (r!=QS_OK)
            qs_diag_emit_simple(diag,QS_SEV_WARNING,"packager","QSB-PKG002",
                QS_LOC_UNKNOWN,qs_arena_sprintf(a,"skipping missing file: %s",file_paths[i]));
        else packed++;
    }
    fclose(f);

    fprintf(stderr,"  \033[1;32m✓\033[0m  %s  (%llu files)\n",
        out_path,(unsigned long long)packed);
    return QS_OK;
}

/* Verify a .qpkg: read header, check magic, count entries */
qs_result_t qs_qpkg_verify(const char *path, qs_diag_engine_t *diag) {
    FILE *f=fopen(path,"rb"); if(!f) return QS_ERROR_NOT_FOUND;
    char magic[8]; fread(magic,1,8,f);
    if (memcmp(magic,QPKG_MAGIC,8)!=0) {
        fclose(f);
        qs_diag_emit_simple(diag,QS_SEV_ERROR,"packager","QSB-PKG010",
            QS_LOC_UNKNOWN,qs_arena_sprintf(NULL,"invalid qpkg magic in: %s",path));
        return QS_ERROR_CORRUPTED;
    }
    fclose(f);
    return QS_OK;
}
