/* qs_jobs.h — simple fixed-size thread pool for parallel compilation */
#ifndef QS_JOBS_H
#define QS_JOBS_H
#include "qs_types.h"

typedef struct qs_job_queue qs_job_queue_t;
typedef void (*qs_job_fn_t)(void *arg, qs_u64 thread_index);

typedef struct {
    qs_job_fn_t fn;
    void       *arg;
    const char *label;  /* for progress display */
} qs_job_t;

typedef struct {
    qs_job_queue_t *queue;
    qs_u64          thread_count;
    qs_u64          completed;
    qs_u64          total;
    qs_bool_t       any_failed;
} qs_pool_t;

/* thread_count=0 → auto-detect nproc */
qs_result_t qs_pool_init(qs_pool_t *p, qs_u64 thread_count);
qs_result_t qs_pool_submit(qs_pool_t *p, qs_job_t job);
qs_result_t qs_pool_wait(qs_pool_t *p);   /* block until all jobs done */
void        qs_pool_destroy(qs_pool_t *p);
void        qs_pool_print_progress(const qs_pool_t *p);
qs_u64      qs_cpu_count(void);
#endif /* QS_JOBS_H */
