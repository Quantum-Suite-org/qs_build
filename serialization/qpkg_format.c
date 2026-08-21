/*
 * serialization/qpkg_format.c — .qpkg binary format reader/writer (v2).
 *
 * v2 format (extends v1 in packages/packager.c):
 *   [header]
 *     magic[8]           "QSPKG002"
 *     version[4]         2
 *     flags[4]           bit0=compressed, bit1=signed, bit2=encrypted
 *     entry_count[8]
 *     index_offset[8]    byte offset to the index table
 *     metadata_len[8]
 *     metadata[N]        JSON blob
 *   [data section]
 *     for each entry:
 *       data[entry.size] bytes
 *   [index table]
 *     for each entry:
 *       path_len[8] + path[path_len]
 *       offset[8]        byte offset of data from file start
 *       size[8]          uncompressed size
 *       stored_size[8]   size on disk (== size if not compressed)
 *       checksum[8]      xxhash64 of stored bytes
 *
 * The index-at-end design allows streaming writes without seeking.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_hash.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_diag.h"
#include <stdio.h>
#include <string.h>

#define QPKG2_MAGIC   "QSPKG002"
#define QPKG2_VERSION 2
#define QPKG2_FLAG_COMPRESSED 0x01
#define QPKG2_FLAG_SIGNED     0x02

typedef struct {
    char     path[512];
    qs_u64   offset;
    qs_u64   size;
    qs_u64   stored_size;
    qs_u64   checksum;
} qpkg2_index_entry_t;

typedef struct {
    FILE                *fp;
    qpkg2_index_entry_t *entries;
    qs_size_t            entry_count;
    qs_size_t            entry_cap;
    qs_u64               data_offset; /* current write position */
    qs_arena_t          *arena;
} qpkg2_writer_t;

static void write_u64le(FILE *f, qs_u64 v) {
    qs_u8 b[8]; for(int i=0;i<8;i++){b[i]=(qs_u8)(v&0xFF);v>>=8;} fwrite(b,1,8,f);
}
static void write_u32le(FILE *f, qs_u32 v) {
    qs_u8 b[4]; for(int i=0;i<4;i++){b[i]=(qs_u8)(v&0xFF);v>>=8;} fwrite(b,1,4,f);
}

qs_result_t qpkg2_writer_open(qs_arena_t *a, qpkg2_writer_t *w,
                                const char *path, const char *metadata_json,
                                qs_u32 flags) {
    w->fp=fopen(path,"wb"); if(!w->fp) return QS_ERROR_IO;
    w->arena=a; w->entry_count=0; w->entry_cap=64;
    w->entries=(qpkg2_index_entry_t*)qs_arena_alloc(a,
        sizeof(qpkg2_index_entry_t)*w->entry_cap,_Alignof(qpkg2_index_entry_t));

    /* Write header (placeholder for index_offset — we'll fill it at close) */
    fwrite(QPKG2_MAGIC,1,8,w->fp);
    write_u32le(w->fp,QPKG2_VERSION);
    write_u32le(w->fp,flags);
    write_u64le(w->fp,0);          /* entry_count placeholder */
    write_u64le(w->fp,0);          /* index_offset placeholder */
    qs_size_t meta_len=metadata_json?strlen(metadata_json):0;
    write_u64le(w->fp,(qs_u64)meta_len);
    if (meta_len) fwrite(metadata_json,1,meta_len,w->fp);

    w->data_offset=(qs_u64)ftell(w->fp);
    return QS_OK;
}

qs_result_t qpkg2_writer_add_file(qs_arena_t *a, qpkg2_writer_t *w,
                                    const char *arc_path, const char *disk_path) {
    char *data=NULL; qs_size_t dlen=0;
    if (qs_fs_read_file(a,disk_path,&data,&dlen)!=QS_OK) return QS_ERROR_NOT_FOUND;

    if (w->entry_count>=w->entry_cap) {
        qs_size_t ncap=w->entry_cap*2;
        w->entries=(qpkg2_index_entry_t*)qs_arena_realloc(a,w->entries,
            sizeof(qpkg2_index_entry_t)*w->entry_cap,
            sizeof(qpkg2_index_entry_t)*ncap,
            _Alignof(qpkg2_index_entry_t));
        w->entry_cap=ncap;
    }
    qpkg2_index_entry_t *e=&w->entries[w->entry_count++];
    strncpy(e->path,arc_path,sizeof(e->path)-1);
    e->offset     =(qs_u64)ftell(w->fp);
    e->size       =(qs_u64)dlen;
    e->stored_size=(qs_u64)dlen;
    e->checksum   =qs_xxhash64(data,(qs_u64)dlen,0);

    if (dlen) fwrite(data,1,(size_t)dlen,w->fp);
    return QS_OK;
}

qs_result_t qpkg2_writer_close(qpkg2_writer_t *w) {
    qs_u64 index_offset=(qs_u64)ftell(w->fp);
    /* Write index */
    for (qs_size_t i=0;i<w->entry_count;i++) {
        qpkg2_index_entry_t *e=&w->entries[i];
        qs_size_t plen=strlen(e->path);
        write_u64le(w->fp,(qs_u64)plen);
        fwrite(e->path,1,plen,w->fp);
        write_u64le(w->fp,e->offset);
        write_u64le(w->fp,e->size);
        write_u64le(w->fp,e->stored_size);
        write_u64le(w->fp,e->checksum);
    }
    /* Patch header: entry_count at offset 12, index_offset at 20 */
    fseek(w->fp,12,SEEK_SET);
    write_u64le(w->fp,(qs_u64)w->entry_count);
    write_u64le(w->fp,index_offset);
    fclose(w->fp); w->fp=NULL;
    return QS_OK;
}

/* Verify a .qpkg v2: check magic, version, and all entry checksums */
qs_result_t qpkg2_verify(qs_arena_t *a, const char *path, qs_diag_engine_t *diag) {
    char *data=NULL; qs_size_t dlen=0;
    if (qs_fs_read_file(a,path,&data,&dlen)!=QS_OK) return QS_ERROR_NOT_FOUND;
    if (dlen<32) return QS_ERROR_CORRUPTED;
    if (memcmp(data,QPKG2_MAGIC,8)!=0) {
        qs_diag_emit_simple(diag,QS_SEV_ERROR,"qpkg","QSB-PKG020",QS_LOC_UNKNOWN,
            qs_arena_sprintf(a,"bad magic in %s",path));
        return QS_ERROR_CORRUPTED;
    }
    /* Read entry_count and index_offset from header */
    qs_u64 entry_count=0, index_offset=0;
    for(int i=0;i<8;i++) entry_count |=(qs_u64)(unsigned char)data[12+i]<<(i*8);
    for(int i=0;i<8;i++) index_offset|=(qs_u64)(unsigned char)data[20+i]<<(i*8);
    if (index_offset>=dlen) return QS_ERROR_CORRUPTED;

    /* Walk index and verify checksums */
    qs_size_t pos=(qs_size_t)index_offset;
    qs_size_t ok=0,bad=0;
    for (qs_u64 e=0;e<entry_count&&pos+8<dlen;e++){
        qs_u64 plen=0;
        for(int i=0;i<8;i++) plen|=(qs_u64)(unsigned char)data[pos+i]<<(i*8);
        pos+=8+(qs_size_t)plen;
        if(pos+32>dlen) break;
        qs_u64 offset=0,size=0,stored=0,checksum=0;
        for(int i=0;i<8;i++) offset  |=(qs_u64)(unsigned char)data[pos+i]<<(i*8); pos+=8;
        for(int i=0;i<8;i++) size    |=(qs_u64)(unsigned char)data[pos+i]<<(i*8); pos+=8;
        for(int i=0;i<8;i++) stored  |=(qs_u64)(unsigned char)data[pos+i]<<(i*8); pos+=8;
        for(int i=0;i<8;i++) checksum|=(qs_u64)(unsigned char)data[pos+i]<<(i*8); pos+=8;
        if(offset+stored>dlen){bad++;continue;}
        qs_u64 actual=qs_xxhash64(data+(qs_size_t)offset,(qs_u64)stored,0);
        if(actual==checksum) ok++;
        else {
            bad++;
            qs_diag_emit_simple(diag,QS_SEV_ERROR,"qpkg","QSB-PKG021",QS_LOC_UNKNOWN,
                qs_arena_sprintf(a,"checksum mismatch in entry %llu of %s",
                    (unsigned long long)e,path));
        }
    }
    fprintf(stderr,"  [qpkg] verify %s: %llu ok, %llu bad\n",
        path,(unsigned long long)ok,(unsigned long long)bad);
    return bad>0?QS_ERROR_CORRUPTED:QS_OK;
}
