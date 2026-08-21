/* qs_path.h — UTF-8 path manipulation (zero deps, C11) */
#ifndef QS_PATH_H
#define QS_PATH_H
#include "qs_types.h"
#include "qs_arena.h"
#include "qs_str.h"

typedef enum {
    QS_LANG_UNKNOWN=0, QS_LANG_C, QS_LANG_CPP, QS_LANG_CSHARP, QS_LANG_JAVA,
    QS_LANG_PYTHON, QS_LANG_GO, QS_LANG_RUBY, QS_LANG_ZIG, QS_LANG_RUST,
    QS_LANG_HEADER_C, QS_LANG_HEADER_CPP
} qs_lang_t;

qs_str_t  qs_path_normalise(qs_arena_t *a, qs_str_t p);
qs_str_t  qs_path_dir(qs_arena_t *a, qs_str_t p);
qs_str_t  qs_path_basename(qs_str_t p);
qs_str_t  qs_path_stem(qs_str_t p);
qs_str_t  qs_path_ext(qs_str_t p);
qs_str_t  qs_path_join(qs_arena_t *a, qs_str_t dir, qs_str_t file);
qs_str_t  qs_path_join_arr(qs_arena_t *a, const qs_str_t *parts, qs_size_t n);
qs_str_t  qs_path_replace_ext(qs_arena_t *a, qs_str_t p, qs_str_t new_ext);
qs_bool_t qs_path_is_absolute(qs_str_t p);
qs_bool_t qs_path_has_ext(qs_str_t p, const char *const *exts);
qs_lang_t qs_path_detect_lang(qs_str_t p);
const char *qs_lang_name(qs_lang_t l);
const char *qs_lang_std_default(qs_lang_t l);
#endif /* QS_PATH_H */
