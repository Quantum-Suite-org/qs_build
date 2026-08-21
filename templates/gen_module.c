/*
 * templates/gen_module.c — Generates scaffold files for a new qs_build module.
 *
 * Invoked via: qs_build new <name> [--lang cpp] [--types exe]
 *
 * Produces:
 *   <name>/build.qs          — manifest
 *   <name>/src/main.cpp      — hello-world entry point
 *   <name>/include/<name>.h  — public header stub
 *   <name>/tests/test_main.cpp — test entry point
 *   <name>/.gitignore        — standard ignores
 *   <name>/README.md         — project readme stub
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_manifest.h"
#include <stdio.h>
#include <string.h>

qs_result_t qs_template_new_module(qs_arena_t *a, const char *name,
                                     qs_lang_t lang, qs_output_type_t out_type) {
    /* Create directory structure */
    qs_fs_mkdir_p(qs_arena_sprintf(a,"%s/src",     name));
    qs_fs_mkdir_p(qs_arena_sprintf(a,"%s/include", name));
    qs_fs_mkdir_p(qs_arena_sprintf(a,"%s/tests",   name));

    const char *lang_str  = qs_lang_name(lang);
    const char *lang_id   = lang==QS_LANG_CPP?"cpp":
                             lang==QS_LANG_C?"c":
                             lang==QS_LANG_CSHARP?"csharp":
                             lang==QS_LANG_JAVA?"java":
                             lang==QS_LANG_PYTHON?"python":
                             lang==QS_LANG_GO?"go":
                             lang==QS_LANG_RUBY?"ruby":
                             lang==QS_LANG_ZIG?"zig":"rust";
    const char *out_str   = qs_output_type_str(out_type);

    /* ── build.qs ────────────────────────────────────────────────────────── */
    FILE *f; char *path;
    path=qs_arena_sprintf(a,"%s/build.qs",name);
    f=fopen(path,"w"); if(!f) return QS_ERROR_IO;
    fprintf(f,"module %s {\n",name);
    fprintf(f,"    name     = \"%s\";\n",name);
    fprintf(f,"    version  = \"0.1.0\";\n");
    fprintf(f,"    language = \"%s\";\n",lang_id);
    if (lang==QS_LANG_CPP)    fprintf(f,"    standard = \"c++20\";\n");
    else if (lang==QS_LANG_C) fprintf(f,"    standard = \"c17\";\n");
    fprintf(f,"    output_type = \"%s\";\n",out_str);
    fprintf(f,"\n");
    if (lang==QS_LANG_CPP||lang==QS_LANG_C) {
        fprintf(f,"    sources      = [ \"src/main.%s\" ];\n",lang==QS_LANG_CPP?"cpp":"c");
        fprintf(f,"    headers      = [ \"include/%s.h\" ];\n",name);
        fprintf(f,"    include_dirs = [ \"include\" ];\n");
        fprintf(f,"    tests        = [ \"tests/test_main.%s\" ];\n",lang==QS_LANG_CPP?"cpp":"c");
    } else if (lang==QS_LANG_PYTHON) {
        fprintf(f,"    sources      = [ \"src/__init__.py\" ];\n");
    } else if (lang==QS_LANG_GO) {
        fprintf(f,"    go_module    = \"github.com/your-org/%s\";\n",name);
    }
    fprintf(f,"\n    enable_lto  = false;\n");
    fprintf(f,"    // chunk_size = 50;   // uncomment for large projects\n");
    fprintf(f,"    // pch_header = \"include/pch.h\";\n");
    fprintf(f,"}\n");
    fclose(f);

    /* ── src/main.* ──────────────────────────────────────────────────────── */
    if (lang==QS_LANG_CPP) {
        path=qs_arena_sprintf(a,"%s/src/main.cpp",name);
        f=fopen(path,"w"); if(f) {
            fprintf(f,"#include \"%s.h\"\n#include <cstdio>\n\n",name);
            fprintf(f,"int main() {\n");
            fprintf(f,"    std::printf(\"Hello from %s!\\n\");\n",name);
            fprintf(f,"    return 0;\n}\n");
            fclose(f);
        }
        path=qs_arena_sprintf(a,"%s/include/%s.h",name,name);
        f=fopen(path,"w"); if(f) {
            char guard[128]; snprintf(guard,sizeof(guard),"%s_H",name);
            for(char*p=guard;*p;p++) if(*p>='a'&&*p<='z') *p-=32;
            fprintf(f,"#ifndef %s\n#define %s\n\n",guard,guard);
            fprintf(f,"// %s public API\n\n",name);
            fprintf(f,"#endif /* %s */\n",guard);
            fclose(f);
        }
        path=qs_arena_sprintf(a,"%s/tests/test_main.cpp",name);
        f=fopen(path,"w"); if(f) {
            fprintf(f,"#include \"%s.h\"\n#include <cassert>\n#include <cstdio>\n\n",name);
            fprintf(f,"int main() {\n");
            fprintf(f,"    // TAP output\n");
            fprintf(f,"    std::printf(\"1..1\\n\");\n");
            fprintf(f,"    std::printf(\"ok 1 - placeholder test\\n\");\n");
            fprintf(f,"    return 0;\n}\n");
            fclose(f);
        }
    } else if (lang==QS_LANG_PYTHON) {
        path=qs_arena_sprintf(a,"%s/src/__init__.py",name);
        f=fopen(path,"w"); if(f) {
            fprintf(f,"\"\"\" %s — built by qs_build \"\"\"\n\n",name);
            fprintf(f,"def hello():\n    print(f'Hello from %s!')\n\nif __name__ == '__main__':\n    hello()\n",name);
            fclose(f);
        }
    } else if (lang==QS_LANG_GO) {
        path=qs_arena_sprintf(a,"%s/src/main.go",name);
        f=fopen(path,"w"); if(f) {
            fprintf(f,"package main\n\nimport \"fmt\"\n\nfunc main() {\n");
            fprintf(f,"\tfmt.Println(\"Hello from %s!\")\n}\n",name);
            fclose(f);
        }
    } else if (lang==QS_LANG_ZIG) {
        path=qs_arena_sprintf(a,"%s/src/main.zig",name);
        f=fopen(path,"w"); if(f) {
            fprintf(f,"const std = @import(\"std\");\n\n");
            fprintf(f,"pub fn main() void {\n");
            fprintf(f,"    std.debug.print(\"Hello from %s!\\n\", .{});\n}\n",name);
            fclose(f);
        }
    } else if (lang==QS_LANG_RUST) {
        qs_fs_mkdir_p(qs_arena_sprintf(a,"%s/src",name));
        path=qs_arena_sprintf(a,"%s/src/main.rs",name);
        f=fopen(path,"w"); if(f) {
            fprintf(f,"fn main() {\n    println!(\"Hello from %s!\");\n}\n",name);
            fclose(f);
        }
    }

    /* ── .gitignore ──────────────────────────────────────────────────────── */
    path=qs_arena_sprintf(a,"%s/.gitignore",name);
    f=fopen(path,"w"); if(f) {
        fprintf(f,"out/\n.qs_cache/\n.qs_pch/\n.qs_chunks/\n");
        fprintf(f,"*.o\n*.obj\n*.a\n*.lib\n*.so\n*.dll\n*.dylib\n");
        fprintf(f,"*.exe\n*.pdb\n*.d\n*.flags\n");
        if (lang==QS_LANG_PYTHON) fprintf(f,"__pycache__/\n*.pyc\ndist/\n*.egg-info/\n");
        if (lang==QS_LANG_GO)     fprintf(f,"vendor/\n");
        if (lang==QS_LANG_RUST)   fprintf(f,"target/\n");
        if (lang==QS_LANG_ZIG)    fprintf(f,".zig-cache/\nzig-out/\n");
        fclose(f);
    }

    /* ── README.md ───────────────────────────────────────────────────────── */
    path=qs_arena_sprintf(a,"%s/README.md",name);
    f=fopen(path,"w"); if(f) {
        fprintf(f,"# %s\n\nBuilt with [qs_build](https://quantum-suite.dev).\n\n",name);
        fprintf(f,"## Building\n\n```bash\nqs_build\n```\n\n");
        fprintf(f,"## Output types\n\n```bash\n");
        fprintf(f,"qs_build --types exe\nqs_build --types dll\nqs_build --types lib\n```\n");
        fclose(f);
    }

    fprintf(stderr,"  \033[1;32m✓\033[0m  scaffolded %s (%s, %s)\n",
        name,lang_str,out_str);
    return QS_OK;
}
