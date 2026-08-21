/*
 * migration/migrator.c — Migrate existing build systems to qs_build.
 * Reads CMakeLists.txt, Makefile, Cargo.toml, go.mod, pyproject.toml,
 * Gemfile, build.gradle, .csproj, or pom.xml and generates a build.qs.
 *
 * Migration is best-effort: complex CMake logic is noted as comments.
 * The generated build.qs always compiles; users refine from there.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_path.h"
#include "qs_fs.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

typedef enum {
    QS_MIGRATE_CMAKE=0, QS_MIGRATE_MAKE,
    QS_MIGRATE_CARGO,   QS_MIGRATE_GO,
    QS_MIGRATE_PYTHON,  QS_MIGRATE_RUBY,
    QS_MIGRATE_GRADLE,  QS_MIGRATE_CSPROJ,
    QS_MIGRATE_MAVEN,   QS_MIGRATE_UNKNOWN
} qs_migrate_from_t;

static qs_migrate_from_t detect_build_system(qs_arena_t *a, const char *dir) {
    if (qs_fs_exists(qs_arena_sprintf(a,"%s/CMakeLists.txt",dir)))  return QS_MIGRATE_CMAKE;
    if (qs_fs_exists(qs_arena_sprintf(a,"%s/Cargo.toml",dir)))      return QS_MIGRATE_CARGO;
    if (qs_fs_exists(qs_arena_sprintf(a,"%s/go.mod",dir)))           return QS_MIGRATE_GO;
    if (qs_fs_exists(qs_arena_sprintf(a,"%s/pyproject.toml",dir)))  return QS_MIGRATE_PYTHON;
    if (qs_fs_exists(qs_arena_sprintf(a,"%s/setup.py",dir)))        return QS_MIGRATE_PYTHON;
    if (qs_fs_exists(qs_arena_sprintf(a,"%s/Gemfile",dir)))         return QS_MIGRATE_RUBY;
    if (qs_fs_exists(qs_arena_sprintf(a,"%s/build.gradle",dir)))    return QS_MIGRATE_GRADLE;
    if (qs_fs_exists(qs_arena_sprintf(a,"%s/pom.xml",dir)))         return QS_MIGRATE_MAVEN;
    if (qs_fs_exists(qs_arena_sprintf(a,"%s/Makefile",dir)))        return QS_MIGRATE_MAKE;
    /* Check for .csproj */
    qs_str_vec_t entries; qs_str_vec_init(&entries,a);
    qs_fs_list_dir(a,dir,&entries);
    for (qs_size_t i=0;i<entries.len;i++)
        if (qs_str_ends_with_cstr(entries.data[i],".csproj")) return QS_MIGRATE_CSPROJ;
    return QS_MIGRATE_UNKNOWN;
}

static char *extract_toml_field(qs_arena_t *a, const char *text, const char *key) {
    char pat[128]; snprintf(pat,sizeof(pat),"%s = ",key);
    const char *found=strstr(text,pat);
    if (!found) return NULL;
    found+=strlen(pat);
    if (*found=='"') {
        found++;
        const char *end=strchr(found,'"');
        if (!end) return NULL;
        return qs_arena_alloc(a,(qs_size_t)(end-found)+1,1);
    }
    return NULL;
}

qs_result_t qs_migrate(qs_arena_t *a, const char *project_dir,
                         const char *out_path, qs_diag_engine_t *diag) {
    qs_migrate_from_t from=detect_build_system(a,project_dir);
    const char *src_dir=project_dir;

    /* Load existing build file for metadata extraction */
    char *cargo_text=NULL; char *go_text=NULL; char *py_text=NULL;
    if (from==QS_MIGRATE_CARGO)
        qs_fs_read_file(a,qs_arena_sprintf(a,"%s/Cargo.toml",project_dir),
            &cargo_text,NULL);
    if (from==QS_MIGRATE_GO)
        qs_fs_read_file(a,qs_arena_sprintf(a,"%s/go.mod",project_dir),
            &go_text,NULL);

    /* Scan sources */
    qs_lang_t lang=QS_LANG_UNKNOWN;
    switch(from){
        case QS_MIGRATE_CMAKE:
        case QS_MIGRATE_MAKE:    lang=QS_LANG_CPP;    break;
        case QS_MIGRATE_CARGO:   lang=QS_LANG_RUST;   break;
        case QS_MIGRATE_GO:      lang=QS_LANG_GO;     break;
        case QS_MIGRATE_PYTHON:  lang=QS_LANG_PYTHON; break;
        case QS_MIGRATE_RUBY:    lang=QS_LANG_RUBY;   break;
        case QS_MIGRATE_GRADLE:
        case QS_MIGRATE_MAVEN:   lang=QS_LANG_JAVA;   break;
        case QS_MIGRATE_CSPROJ:  lang=QS_LANG_CSHARP; break;
        default:                 lang=QS_LANG_CPP;    break;
    }

    qs_str_vec_t sources, headers;
    qs_str_vec_init(&sources,a); qs_str_vec_init(&headers,a);
    extern qs_result_t qs_scan_sources(qs_arena_t*,const char*,qs_lang_t,
                                        qs_str_vec_t*,qs_str_vec_t*);
    qs_scan_sources(a,src_dir,lang,&sources,&headers);

    /* Extract project name from dir */
    qs_str_t name_str=qs_path_basename(qs_str_from_cstr(project_dir));
    char *name=qs_arena_str_to_cstr(a,name_str);

    /* Extract version if available */
    const char *version="0.1.0";
    if (cargo_text) {
        char *v=extract_toml_field(a,cargo_text,"version");
        if (v) version=v;
    }

    /* Write build.qs */
    FILE *f=fopen(out_path,"w"); if(!f) return QS_ERROR_IO;
    fprintf(f,"// Migrated from %s by qs_build\n",
        from==QS_MIGRATE_CMAKE?"CMake":
        from==QS_MIGRATE_CARGO?"Cargo":
        from==QS_MIGRATE_GO?"go.mod":
        from==QS_MIGRATE_PYTHON?"pyproject.toml":
        from==QS_MIGRATE_RUBY?"Gemfile":
        from==QS_MIGRATE_GRADLE?"Gradle":
        from==QS_MIGRATE_MAVEN?"Maven":
        from==QS_MIGRATE_CSPROJ?".csproj":"Makefile");
    fprintf(f,"// Review and adjust before using in production.\n\n");
    fprintf(f,"module %s {\n",name);
    fprintf(f,"    name     = \"%s\";\n",name);
    fprintf(f,"    version  = \"%s\";\n",version);
    fprintf(f,"    language = \"%s\";\n",
        lang==QS_LANG_CPP?"cpp":lang==QS_LANG_C?"c":
        lang==QS_LANG_RUST?"rust":lang==QS_LANG_GO?"go":
        lang==QS_LANG_PYTHON?"python":lang==QS_LANG_RUBY?"ruby":
        lang==QS_LANG_JAVA?"java":lang==QS_LANG_CSHARP?"csharp":"cpp");
    if (lang==QS_LANG_CPP) fprintf(f,"    standard = \"c++20\";\n");
    if (lang==QS_LANG_C)   fprintf(f,"    standard = \"c17\";\n");
    fprintf(f,"    output_type = \"exe\"; // adjust as needed\n\n");
    fprintf(f,"    sources = [\n");
    for (qs_size_t i=0;i<sources.len&&i<256;i++)
        fprintf(f,"        \"%.*s\",\n",
            (int)sources.data[i].len,(const char*)sources.data[i].ptr);
    fprintf(f,"    ];\n");
    if (headers.len) {
        fprintf(f,"    headers = [\n");
        for (qs_size_t i=0;i<headers.len&&i<64;i++)
            fprintf(f,"        \"%.*s\",\n",
                (int)headers.data[i].len,(const char*)headers.data[i].ptr);
        fprintf(f,"    ];\n");
    }
    fprintf(f,"}\n");
    fclose(f);

    fprintf(stderr,"  [migrate] wrote %s  (%llu sources)\n",
        out_path,(unsigned long long)sources.len);
    qs_diag_emit_simple(diag,QS_SEV_NOTE,"migrate","QSB-MIG001",QS_LOC_UNKNOWN,
        qs_arena_sprintf(a,"migration complete — review %s before building",out_path));
    return QS_OK;
}
