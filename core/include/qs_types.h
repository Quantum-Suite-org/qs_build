/* qs_types.h — canonical primitive types for qs_build (C11, zero deps)
 * Section 15 rule: all sizes/counts/indices are qs_u64. Never bare int/long/size_t. */
#ifndef QS_TYPES_H
#define QS_TYPES_H
#include <stdint.h>
#include <stddef.h>

typedef unsigned long long qs_u64;
typedef long long          qs_i64;
typedef unsigned int       qs_u32;
typedef int                qs_i32;
typedef unsigned short     qs_u16;
typedef short              qs_i16;
typedef unsigned char      qs_u8;
typedef signed char        qs_i8;
typedef float              qs_f32;
typedef double             qs_f64;

typedef qs_u64 qs_size_t;
typedef qs_u64 qs_index_t;
typedef qs_u64 qs_offset_t;
typedef qs_u64 qs_handle_t;
typedef qs_u64 qs_hash_t;
typedef qs_u64 qs_flags_t;
typedef qs_u64 qs_time_us_t;

typedef enum { QS_FALSE = 0, QS_TRUE = 1 } qs_bool_t;

typedef struct { const qs_u8 *ptr; qs_size_t len; } qs_str_t;
#define QS_STR(lit)     ((qs_str_t){ (const qs_u8 *)(lit), sizeof(lit)-1 })
#define QS_STR_EMPTY    ((qs_str_t){ (const qs_u8 *)0, 0 })
#define QS_STR_IS_EMPTY(s) ((s).len == 0)

typedef struct { qs_u8 *ptr; qs_size_t len; qs_size_t cap; } qs_buf_t;

typedef enum {
    QS_OK=0, QS_ERROR_OOM=1, QS_ERROR_INVALID_ARG=2, QS_ERROR_NOT_FOUND=3,
    QS_ERROR_IO=4, QS_ERROR_CORRUPTED=5, QS_ERROR_UNSUPPORTED=6,
    QS_ERROR_TOOLCHAIN=7, QS_ERROR_COMPILE=8, QS_ERROR_LINK=9,
    QS_ERROR_CYCLE=10, QS_ERROR_MANIFEST=11, QS_ERROR_PROCESS=12,
    QS_ERROR_TIMEOUT=13, QS_ERROR_PERMISSION=14, QS_ERROR_EXISTS=15,
    QS_ERROR_PCH=16, QS_ERROR_CHUNK=17, QS_ERROR_CACHE=18,
    QS_ERROR_INTERNAL=255
} qs_result_t;

static inline const char *qs_result_str(qs_result_t r) {
    switch(r) {
        case QS_OK:                 return "ok";
        case QS_ERROR_OOM:          return "out of memory";
        case QS_ERROR_INVALID_ARG:  return "invalid argument";
        case QS_ERROR_NOT_FOUND:    return "not found";
        case QS_ERROR_IO:           return "i/o error";
        case QS_ERROR_CORRUPTED:    return "data corrupted";
        case QS_ERROR_UNSUPPORTED:  return "unsupported";
        case QS_ERROR_TOOLCHAIN:    return "toolchain error";
        case QS_ERROR_COMPILE:      return "compilation error";
        case QS_ERROR_LINK:         return "link error";
        case QS_ERROR_CYCLE:        return "dependency cycle";
        case QS_ERROR_MANIFEST:     return "manifest error";
        case QS_ERROR_PROCESS:      return "process error";
        case QS_ERROR_TIMEOUT:      return "timeout";
        case QS_ERROR_PERMISSION:   return "permission denied";
        case QS_ERROR_EXISTS:       return "already exists";
        case QS_ERROR_PCH:          return "pch error";
        case QS_ERROR_CHUNK:        return "chunk error";
        case QS_ERROR_CACHE:        return "cache error";
        default:                    return "internal error";
    }
}

#define QS_U64_MAX         ((qs_u64)0xFFFFFFFFFFFFFFFFULL)
#define QS_INVALID_INDEX   QS_U64_MAX
#define QS_INVALID_HANDLE  ((qs_handle_t)0)
#define QS_ARRAY_LEN(a)    ((qs_size_t)(sizeof(a)/sizeof((a)[0])))
#define QS_UNUSED(x)       ((void)(x))
#define QS_MIN(a,b)        ((a)<(b)?(a):(b))
#define QS_MAX(a,b)        ((a)>(b)?(a):(b))
#define QS_CLAMP(v,lo,hi)  ((v)<(lo)?(lo):(v)>(hi)?(hi):(v))
#define QS_ALIGN_UP(x,a)   (((x)+(a)-1)&~((a)-1))
#define QS_KB(n)  ((qs_size_t)(n)*1024ULL)
#define QS_MB(n)  ((qs_size_t)(n)*1024ULL*1024ULL)
#define QS_GB(n)  ((qs_size_t)(n)*1024ULL*1024ULL*1024ULL)

/* PCH threshold: auto-enable precompiled headers when a folder has >= this many source files */
#define QS_PCH_THRESHOLD    30
/* Chunking threshold: auto-enable unity builds when a module has >= this many source files */
#define QS_CHUNK_THRESHOLD  150
/* Default files per unity chunk */
#define QS_DEFAULT_CHUNK_SIZE 50

#ifdef __GNUC__
#  define QS_LIKELY(x)    __builtin_expect(!!(x),1)
#  define QS_UNLIKELY(x)  __builtin_expect(!!(x),0)
#  define QS_NORETURN     __attribute__((noreturn))
#  define QS_PRINTF(f,a)  __attribute__((format(printf,f,a)))
#  define QS_NODISCARD    __attribute__((warn_unused_result))
#  define QS_INLINE       static inline __attribute__((always_inline))
#else
#  define QS_LIKELY(x)    (x)
#  define QS_UNLIKELY(x)  (x)
#  define QS_NORETURN     __declspec(noreturn)
#  define QS_PRINTF(f,a)
#  define QS_NODISCARD    _Check_return_
#  define QS_INLINE       static __forceinline
#endif

#if defined(_WIN32)
#  define QS_API __declspec(dllexport)
#else
#  define QS_API __attribute__((visibility("default")))
#endif

_Static_assert(sizeof(qs_u64)==8,"qs_u64 must be 8 bytes");
_Static_assert(sizeof(qs_i64)==8,"qs_i64 must be 8 bytes");
#endif /* QS_TYPES_H */
