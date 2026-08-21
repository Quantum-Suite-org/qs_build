/*
 * profiles/profiles.c — Build profile management.
 * A profile is a named set of compiler flags and settings.
 * Built-in profiles: debug, release, relwithdebinfo, minsizerel, profiling.
 * Custom profiles can be defined in build.qs.
 *
 * profile debug        → -O0 -g3 -DDEBUG -fsanitize=address,undefined
 * profile release      → -O3 -DNDEBUG -flto (when supported)
 * profile relwithdebinfo → -O2 -g -DNDEBUG
 * profile minsizerel   → -Os -DNDEBUG
 * profile profiling    → -O2 -g -pg -DNDEBUG
 */
#include "qs_str.h"
#include "../core/include/qs_types.h"
#include "../core/include/qs_arena.h"
#include "../core/include/qs_toolchain.h"
#include "../core/include/qs_vec.h"
#include <string.h>
#include <stdio.h>

typedef enum {
    QS_PROFILE_DEBUG=0,
    QS_PROFILE_RELEASE,
    QS_PROFILE_RELWITHDEBINFO,
    QS_PROFILE_MINSIZEREL,
    QS_PROFILE_PROFILING,
    QS_PROFILE_CUSTOM
} qs_profile_kind_t;

typedef struct {
    qs_profile_kind_t kind;
    const char       *name;
    qs_str_vec_t      c_flags;
    qs_str_vec_t      cpp_flags;
    qs_str_vec_t      link_flags;
    qs_str_vec_t      defines;
} qs_profile_t;

static void push(qs_str_vec_t *v, const char *s) {
    qs_str_vec_push(v, qs_str_from_cstr((char*)s));
}

void qs_profile_init_debug(qs_arena_t *a, const qs_toolchain_t *tc,
                             qs_profile_t *out) {
    out->kind = QS_PROFILE_DEBUG;
    out->name = "debug";
    qs_str_vec_init(&out->c_flags,    a);
    qs_str_vec_init(&out->cpp_flags,  a);
    qs_str_vec_init(&out->link_flags, a);
    qs_str_vec_init(&out->defines,    a);

    if (tc->is_msvc) {
        push(&out->c_flags,   "/Od");
        push(&out->c_flags,   "/Zi");
        push(&out->c_flags,   "/RTC1");
        push(&out->c_flags,   "/MDd");
        push(&out->defines,   "_DEBUG");
    } else {
        push(&out->c_flags,   "-O0");
        push(&out->c_flags,   "-g3");
        push(&out->c_flags,   "-fno-omit-frame-pointer");
        if (strstr(tc->name,"clang")||strstr(tc->name,"gcc")) {
            push(&out->c_flags, "-fsanitize=address,undefined");
            push(&out->link_flags, "-fsanitize=address,undefined");
        }
        push(&out->defines,   "DEBUG");
        push(&out->defines,   "_DEBUG");
    }
}

void qs_profile_init_release(qs_arena_t *a, const qs_toolchain_t *tc,
                               qs_profile_t *out) {
    out->kind = QS_PROFILE_RELEASE;
    out->name = "release";
    qs_str_vec_init(&out->c_flags,    a);
    qs_str_vec_init(&out->cpp_flags,  a);
    qs_str_vec_init(&out->link_flags, a);
    qs_str_vec_init(&out->defines,    a);

    if (tc->is_msvc) {
        push(&out->c_flags,   "/O2");
        push(&out->c_flags,   "/GL");
        push(&out->c_flags,   "/MD");
        push(&out->link_flags,"/LTCG");
        push(&out->link_flags,"/OPT:REF");
        push(&out->link_flags,"/OPT:ICF");
    } else {
        push(&out->c_flags,   "-O3");
        push(&out->c_flags,   "-fomit-frame-pointer");
        push(&out->c_flags,   "-march=native");
        if (tc->supports_lto) {
            push(&out->c_flags,    "-flto");
            push(&out->link_flags, "-flto");
        }
        push(&out->link_flags, "-Wl,--gc-sections");
    }
    push(&out->defines, "NDEBUG");
    push(&out->defines, "QS_RELEASE");
}

void qs_profile_init_profiling(qs_arena_t *a, const qs_toolchain_t *tc,
                                 qs_profile_t *out) {
    out->kind = QS_PROFILE_PROFILING;
    out->name = "profiling";
    qs_str_vec_init(&out->c_flags,    a);
    qs_str_vec_init(&out->cpp_flags,  a);
    qs_str_vec_init(&out->link_flags, a);
    qs_str_vec_init(&out->defines,    a);

    if (!tc->is_msvc) {
        push(&out->c_flags,    "-O2");
        push(&out->c_flags,    "-g");
        push(&out->c_flags,    "-pg");
        push(&out->link_flags, "-pg");
    } else {
        push(&out->c_flags,    "/O2");
        push(&out->c_flags,    "/Zi");
    }
    push(&out->defines, "NDEBUG");
    push(&out->defines, "QS_PROFILING");
}

/* Apply profile flags on top of an argv array */
void qs_profile_apply(const qs_profile_t *prof, qs_str_vec_t *flags) {
    for (qs_size_t i = 0; i < prof->c_flags.len; i++)
        qs_str_vec_push(flags, prof->c_flags.data[i]);
    for (qs_size_t i = 0; i < prof->defines.len; i++) {
        /* Prepend -D */
        char *def = qs_arena_sprintf(flags->arena, "-D%.*s",
            (int)prof->defines.data[i].len,
            (const char*)prof->defines.data[i].ptr);
        qs_str_vec_push(flags, qs_str_from_cstr(def));
    }
}

const char *qs_profile_name(qs_profile_kind_t k) {
    switch(k) {
        case QS_PROFILE_DEBUG:          return "debug";
        case QS_PROFILE_RELEASE:        return "release";
        case QS_PROFILE_RELWITHDEBINFO: return "relwithdebinfo";
        case QS_PROFILE_MINSIZEREL:     return "minsizerel";
        case QS_PROFILE_PROFILING:      return "profiling";
        default:                        return "custom";
    }
}
