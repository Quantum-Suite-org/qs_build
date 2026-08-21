/*
 * jobs/pool.c — Fixed-size thread pool for parallel compilation.
 * POSIX pthreads on Linux/macOS, Win32 threads on Windows.
 * Workers pull jobs from a shared queue and execute them.
 * qs_pool_wait() blocks until all submitted jobs complete.
 */
#include "../core/include/qs_jobs.h"
#include "../core/include/qs_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  define QS_MUTEX        CRITICAL_SECTION
#  define QS_COND         CONDITION_VARIABLE
#  define mutex_init(m)   InitializeCriticalSection(m)
#  define mutex_lock(m)   EnterCriticalSection(m)
#  define mutex_unlock(m) LeaveCriticalSection(m)
#  define mutex_destroy(m) DeleteCriticalSection(m)
#  define cond_init(c)    InitializeConditionVariable(c)
#  define cond_wait(c,m)  SleepConditionVariableCS(c,m,INFINITE)
#  define cond_signal(c)  WakeConditionVariable(c)
#  define cond_broadcast(c) WakeAllConditionVariable(c)
#  define cond_destroy(c) ((void)0)
typedef HANDLE qs_thread_t;
static DWORD WINAPI thread_func(LPVOID arg);
static qs_thread_t thread_create(LPTHREAD_START_ROUTINE fn, void *arg) {
    return CreateThread(NULL,0,fn,arg,0,NULL);
}
static void thread_join(qs_thread_t t) { WaitForSingleObject(t,INFINITE); CloseHandle(t); }
#else
#  include <pthread.h>
#  include <unistd.h>
#  define QS_MUTEX        pthread_mutex_t
#  define QS_COND         pthread_cond_t
#  define mutex_init(m)   pthread_mutex_init(m,NULL)
#  define mutex_lock(m)   pthread_mutex_lock(m)
#  define mutex_unlock(m) pthread_mutex_unlock(m)
#  define mutex_destroy(m) pthread_mutex_destroy(m)
#  define cond_init(c)    pthread_cond_init(c,NULL)
#  define cond_wait(c,m)  pthread_cond_wait(c,m)
#  define cond_signal(c)  pthread_cond_signal(c)
#  define cond_broadcast(c) pthread_cond_broadcast(c)
#  define cond_destroy(c) pthread_cond_destroy(c)
typedef pthread_t qs_thread_t;
static void *thread_func(void *arg);
static qs_thread_t thread_create(void*(*fn)(void*), void *arg) {
    qs_thread_t t; pthread_create(&t,NULL,fn,arg); return t;
}
static void thread_join(qs_thread_t t) { pthread_join(t,NULL); }
#endif

#define MAX_QUEUE 65536
#define MAX_THREADS 256

struct qs_job_queue {
    qs_job_t   jobs[MAX_QUEUE];
    qs_u64     head, tail, count;
    QS_MUTEX   lock;
    QS_COND    not_empty;
    QS_COND    not_full;
    QS_COND    all_done;
    qs_u64     in_flight;  /* jobs dispatched but not yet finished */
    qs_bool_t  shutdown;
    qs_bool_t  any_failed;
};

typedef struct { struct qs_job_queue *q; qs_u64 idx; } worker_arg_t;

#ifdef _WIN32
static DWORD WINAPI thread_func(LPVOID varg) {
#else
static void *thread_func(void *varg) {
#endif
    worker_arg_t *wa=(worker_arg_t*)varg;
    struct qs_job_queue *q=wa->q;
    qs_u64 idx=wa->idx;
    free(wa);

    for(;;){
        mutex_lock(&q->lock);
        while(!q->count && !q->shutdown) cond_wait(&q->not_empty,&q->lock);
        if (q->shutdown && !q->count){ mutex_unlock(&q->lock); break; }
        qs_job_t job=q->jobs[q->head % MAX_QUEUE];
        q->head++; q->count++;  /* count tracks capacity; in_flight tracks live */
        /* Fix: count is items in queue */
        q->count--;
        q->in_flight++;
        mutex_unlock(&q->lock);
        cond_signal(&q->not_full);

        /* Execute */
        job.fn(job.arg, idx);

        mutex_lock(&q->lock);
        q->in_flight--;
        if (!q->count && !q->in_flight) cond_broadcast(&q->all_done);
        mutex_unlock(&q->lock);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

qs_u64 qs_cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO si; GetSystemInfo(&si); return (qs_u64)si.dwNumberOfProcessors;
#elif defined(__linux__)
    long n=sysconf(_SC_NPROCESSORS_ONLN); return n>0?(qs_u64)n:1;
#elif defined(__APPLE__)
    long n=sysconf(_SC_NPROCESSORS_ONLN); return n>0?(qs_u64)n:1;
#else
    return 4;
#endif
}

qs_result_t qs_pool_init(qs_pool_t *p, qs_u64 thread_count) {
    if (!thread_count) thread_count=qs_cpu_count();
    if (thread_count>MAX_THREADS) thread_count=MAX_THREADS;
    p->thread_count=thread_count;
    p->completed=p->total=0;
    p->any_failed=QS_FALSE;

    struct qs_job_queue *q=(struct qs_job_queue*)calloc(1,sizeof(*q));
    if (!q) return QS_ERROR_OOM;
    mutex_init(&q->lock);
    cond_init(&q->not_empty);
    cond_init(&q->not_full);
    cond_init(&q->all_done);
    q->shutdown=QS_FALSE;
    p->queue=q;

    /* Spawn workers */
    for (qs_u64 i=0;i<thread_count;i++){
        worker_arg_t *wa=(worker_arg_t*)malloc(sizeof(*wa));
        wa->q=q; wa->idx=i;
#ifdef _WIN32
        thread_create(thread_func,wa);
#else
        thread_create(thread_func,wa);
#endif
    }
    return QS_OK;
}

qs_result_t qs_pool_submit(qs_pool_t *p, qs_job_t job) {
    struct qs_job_queue *q=p->queue;
    mutex_lock(&q->lock);
    while (q->count>=MAX_QUEUE && !q->shutdown)
        cond_wait(&q->not_full,&q->lock);
    if (q->shutdown){ mutex_unlock(&q->lock); return QS_ERROR_INTERNAL; }
    q->jobs[q->tail % MAX_QUEUE]=job;
    q->tail++; q->count++;
    p->total++;
    mutex_unlock(&q->lock);
    cond_signal(&q->not_empty);
    return QS_OK;
}

qs_result_t qs_pool_wait(qs_pool_t *p) {
    struct qs_job_queue *q=p->queue;
    mutex_lock(&q->lock);
    while (q->count||q->in_flight) cond_wait(&q->all_done,&q->lock);
    mutex_unlock(&q->lock);
    return p->any_failed?QS_ERROR_COMPILE:QS_OK;
}

void qs_pool_destroy(qs_pool_t *p) {
    struct qs_job_queue *q=p->queue;
    mutex_lock(&q->lock); q->shutdown=QS_TRUE; mutex_unlock(&q->lock);
    cond_broadcast(&q->not_empty);
    /* Threads will exit; we don't track handles here for simplicity */
}

void qs_pool_print_progress(const qs_pool_t *p) {
    qs_u64 done=p->completed, tot=p->total;
    if (!tot) return;
    int pct=(int)((double)done/(double)tot*100.0);
    fprintf(stderr,"\r  \033[36m[%3d%%]\033[0m  %llu / %llu jobs",
        pct,(unsigned long long)done,(unsigned long long)tot);
    fflush(stderr);
}
