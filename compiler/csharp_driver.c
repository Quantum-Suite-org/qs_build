/*
 * compiler/csharp_driver.c — C# compilation driver via dotnet CLI.
 * Supports exe, dll, nupkg output. Uses response files for large source lists.
 * PCH equivalent: dotnet build with a prebuilt reference cache project.
 * Chunking: response file (@file) batches sources per chunk.
 */
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_manifest.h"
#include "../core/include/qs_toolchain.h"
#include "../core/include/qs_diag.h"
#include "../core/include/qs_process.h"
#include "../core/include/qs_fs.h"
#include "../core/include/qs_path.h"
#include <string.h>
#include <stdio.h>

#define ARGV_MAX 256
static int cs_ac; static const char *cs_av[ARGV_MAX];
#define PUSH(s) do{if(cs_ac<ARGV_MAX-1){cs_av[cs_ac++]=(s);cs_av[cs_ac]=NULL;}}while(0)
#define PUSHF(fmt,...) PUSH(qs_arena_sprintf(a,fmt,__VA_ARGS__))

/* Generate a minimal .csproj for this manifest */
static char *gen_csproj(qs_arena_t *a, const qs_manifest_t *m, const char *out_dir) {
    char *csproj = qs_arena_sprintf(a, "%s/%.*s.csproj",
        out_dir, (int)m->name.len, (const char*)m->name.ptr);
    FILE *f = fopen(csproj, "w"); if (!f) return NULL;
    const char *sdk = QS_STR_IS_EMPTY(m->dotnet_sdk)?"net8.0":(const char*)m->dotnet_sdk.ptr;
    /* Map output type */
    const char *out_type = "Exe";
    if (m->output_type==QS_OUT_DLL||m->output_type==QS_OUT_LIB||
        m->output_type==QS_OUT_NUPKG) out_type="Library";
    fprintf(f,"<Project Sdk=\"Microsoft.NET.Sdk\">\n");
    fprintf(f,"  <PropertyGroup>\n");
    fprintf(f,"    <OutputType>%s</OutputType>\n", out_type);
    fprintf(f,"    <TargetFramework>%s</TargetFramework>\n", sdk);
    fprintf(f,"    <Nullable>enable</Nullable>\n");
    fprintf(f,"    <ImplicitUsings>enable</ImplicitUsings>\n");
    fprintf(f,"    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n");
    fprintf(f,"    <Optimize>true</Optimize>\n");
    fprintf(f,"    <AssemblyName>%.*s</AssemblyName>\n",
        (int)m->name.len,(const char*)m->name.ptr);
    if (m->enable_lto) fprintf(f,"    <PublishReadyToRun>true</PublishReadyToRun>\n");
    fprintf(f,"  </PropertyGroup>\n");
    /* Defines */
    if (m->defines.len) {
        fprintf(f,"  <PropertyGroup><DefineConstants>");
        for (qs_size_t i=0;i<m->defines.len;i++) {
            if(i)fprintf(f,";");
            fprintf(f,"%.*s",(int)m->defines.data[i].len,(const char*)m->defines.data[i].ptr);
        }
        fprintf(f,"</DefineConstants></PropertyGroup>\n");
    }
    /* Explicit source includes */
    fprintf(f,"  <ItemGroup>\n");
    for (qs_size_t i=0;i<m->sources.len;i++)
        fprintf(f,"    <Compile Include=\"%.*s\"/>\n",
            (int)m->sources.data[i].len,(const char*)m->sources.data[i].ptr);
    fprintf(f,"  </ItemGroup>\n");
    fprintf(f,"</Project>\n");
    fclose(f);
    return csproj;
}

qs_result_t qs_csharp_compile_all(qs_arena_t *a, const qs_manifest_t *m,
                                    const qs_toolchain_t *tc,
                                    const char *out_dir,
                                    qs_diag_engine_t *diag,
                                    qs_bool_t verbose, qs_bool_t dry_run,
                                    char **output_path_out) {
    qs_fs_mkdir_p(out_dir);
    char *csproj = gen_csproj(a, m, out_dir);
    if (!csproj) return QS_ERROR_IO;

    cs_ac = 0;
    PUSH(tc->executable); PUSH("build"); PUSH(csproj);
    PUSHF("--output=%s/bin", out_dir);
    PUSH("--nologo");
    PUSH("-p:GenerateFullPaths=true");  /* for diag parser */
    if (verbose) PUSH("-v:normal"); else PUSH("-v:quiet");

    if (verbose) qs_proc_print_argv(cs_av);
    if (dry_run) { *output_path_out = NULL; return QS_OK; }

    qs_proc_result_t pr;
    qs_result_t r = qs_proc_run(a, cs_av, NULL, 600000000ULL, &pr);
    if (r != QS_OK) return r;

    /* dotnet writes errors to stdout */
    const char *err_text = pr.stdout_text[0] ? pr.stdout_text : pr.stderr_text;
    qs_diag_ingest_tool_output(diag, a, QS_STR("dotnet"), err_text, pr.exit_code);

    if (pr.exit_code == 0) {
        const char *ext = m->output_type==QS_OUT_DLL||m->output_type==QS_OUT_LIB?".dll":".exe";
        *output_path_out = qs_arena_sprintf(a, "%s/bin/%.*s%s",
            out_dir,(int)m->name.len,(const char*)m->name.ptr,ext);
    }
    return pr.exit_code == 0 ? QS_OK : QS_ERROR_COMPILE;
}

/* Pack as NuGet after build */
qs_result_t qs_csharp_pack(qs_arena_t *a, const qs_manifest_t *m,
                             const qs_toolchain_t *tc, const char *out_dir,
                             qs_diag_engine_t *diag, qs_bool_t verbose) {
    char *csproj = qs_arena_sprintf(a, "%s/%.*s.csproj",
        out_dir,(int)m->name.len,(const char*)m->name.ptr);
    const char *argv[] = { tc->executable,"pack",csproj,
        qs_arena_sprintf(a,"--output=%s/pkg",out_dir),"--nologo",NULL };
    if (verbose) qs_proc_print_argv(argv);
    qs_proc_result_t pr;
    qs_result_t r = qs_proc_run(a, argv, NULL, 120000000ULL, &pr);
    if (r != QS_OK) return r;
    qs_diag_ingest_tool_output(diag, a, QS_STR("dotnet"), pr.stdout_text, pr.exit_code);
    return pr.exit_code == 0 ? QS_OK : QS_ERROR_COMPILE;
}
