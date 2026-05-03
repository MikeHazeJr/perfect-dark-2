/* Boot Thread Pool (Engine Phase 2)
 *
 * SDL-backed worker pool used by the boot orchestrator to run catalog
 * + extract + verify off the main thread.  See port/include/boot_pool.h
 * for topology + lifecycle contract.
 *
 * Implementation: ring-buffer job queue, single mutex, two condition
 * variables (work-available + idle).  Workers are spawned by Init and
 * exit cleanly when Shutdown sets the stop flag and broadcasts.
 *
 * Physical-core detection (Mike Q1, 2026-05-03): SDL_GetCPUCount returns
 * logical CPUs, which on hyperthreaded x86 doubles the physical count.
 * On Windows we query GetLogicalProcessorInformation to count cores
 * exactly; on other platforms we fall back to SDL's count (no PC-port
 * targets currently exist outside Windows, so the fallback is dormant).
 */

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#endif

#include <PR/ultratypes.h>

#include "boot_pool.h"
#include "config.h"
#include "system.h"

#define BOOT_POOL_QUEUE_CAP 256
#define BOOT_POOL_WORKER_CAP 16
#define BOOT_POOL_WORKER_MIN 1

typedef struct {
    boot_pool_job_fn fn;
    void            *arg;
} boot_job_t;

static struct {
    int            initialized;
    SDL_mutex     *mutex;
    SDL_cond      *cv_work;
    SDL_cond      *cv_idle;

    boot_job_t     queue[BOOT_POOL_QUEUE_CAP];
    int            head;
    int            tail;
    int            count;

    int            in_flight;       /* jobs popped but not yet completed */
    int            stop;
    int            worker_count;
    SDL_Thread    *workers[BOOT_POOL_WORKER_CAP];
} s_Pool;

/* pd.ini override (default 0 = auto-detect). */
static s32 s_CfgWorkerOverride = 0;

static int s_detectPhysicalCores(void)
{
#if defined(_WIN32)
    DWORD bytes = 0;
    GetLogicalProcessorInformation(NULL, &bytes);
    if (bytes == 0) {
        return SDL_GetCPUCount();
    }

    SYSTEM_LOGICAL_PROCESSOR_INFORMATION *buf =
        (SYSTEM_LOGICAL_PROCESSOR_INFORMATION *)malloc((size_t)bytes);
    if (!buf) {
        return SDL_GetCPUCount();
    }

    if (!GetLogicalProcessorInformation(buf, &bytes)) {
        free(buf);
        return SDL_GetCPUCount();
    }

    int cores = 0;
    DWORD count = bytes / (DWORD)sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    for (DWORD i = 0; i < count; i++) {
        if (buf[i].Relationship == RelationProcessorCore) {
            cores++;
        }
    }
    free(buf);

    if (cores < 1) {
        cores = SDL_GetCPUCount();
    }
    return cores;
#else
    return SDL_GetCPUCount();
#endif
}

static int s_workerLoop(void *arg)
{
    (void)arg;

    for (;;) {
        SDL_LockMutex(s_Pool.mutex);

        while (!s_Pool.stop && s_Pool.count == 0) {
            SDL_CondWait(s_Pool.cv_work, s_Pool.mutex);
        }

        if (s_Pool.stop && s_Pool.count == 0) {
            SDL_UnlockMutex(s_Pool.mutex);
            return 0;
        }

        boot_job_t job = s_Pool.queue[s_Pool.head];
        s_Pool.head = (s_Pool.head + 1) % BOOT_POOL_QUEUE_CAP;
        s_Pool.count--;
        s_Pool.in_flight++;
        SDL_UnlockMutex(s_Pool.mutex);

        if (job.fn) {
            job.fn(job.arg);
        }

        SDL_LockMutex(s_Pool.mutex);
        s_Pool.in_flight--;
        if (s_Pool.count == 0 && s_Pool.in_flight == 0) {
            SDL_CondBroadcast(s_Pool.cv_idle);
        }
        SDL_UnlockMutex(s_Pool.mutex);
    }
}

void bootPoolInit(void)
{
    if (s_Pool.initialized) {
        return;
    }

    /* Lazy register pd.ini key so configLoad picks up overrides on
     * subsequent boots.  Range [0, 32]: 0 = auto-detect, otherwise
     * caller's choice clamped at use site. */
    static int s_ConfigRegistered = 0;
    if (!s_ConfigRegistered) {
        configRegisterInt("Boot.WorkerThreads", &s_CfgWorkerOverride, 0, 32);
        s_ConfigRegistered = 1;
    }

    memset(&s_Pool, 0, sizeof(s_Pool));
    s_Pool.mutex = SDL_CreateMutex();
    s_Pool.cv_work = SDL_CreateCond();
    s_Pool.cv_idle = SDL_CreateCond();

    int desired;
    if (s_CfgWorkerOverride > 0) {
        desired = s_CfgWorkerOverride;
    } else {
        int cores = s_detectPhysicalCores();
        /* N - 2: reserve one for main thread, one for manager.  Mike Q1. */
        desired = cores - 2;
    }

    if (desired < BOOT_POOL_WORKER_MIN) {
        desired = BOOT_POOL_WORKER_MIN;
    }
    if (desired > BOOT_POOL_WORKER_CAP) {
        desired = BOOT_POOL_WORKER_CAP;
    }

    s_Pool.worker_count = desired;
    s_Pool.initialized = 1;

    for (int i = 0; i < s_Pool.worker_count; i++) {
        char name[32];
        snprintf(name, sizeof(name), "boot_worker_%d", i);
        s_Pool.workers[i] = SDL_CreateThread(s_workerLoop, name, NULL);
        if (!s_Pool.workers[i]) {
            sysLogPrintf(LOG_ERROR,
                "BOOT_POOL: SDL_CreateThread failed for worker %d: %s",
                i, SDL_GetError());
        }
    }

    sysLogPrintf(LOG_NOTE,
        "BOOT_POOL: spawned %d worker thread(s) (override=%d, "
        "physical_cores_detected=%d)",
        s_Pool.worker_count, s_CfgWorkerOverride, s_detectPhysicalCores());
}

void bootPoolShutdown(void)
{
    if (!s_Pool.initialized) {
        return;
    }

    SDL_LockMutex(s_Pool.mutex);
    s_Pool.stop = 1;
    SDL_CondBroadcast(s_Pool.cv_work);
    SDL_UnlockMutex(s_Pool.mutex);

    for (int i = 0; i < s_Pool.worker_count; i++) {
        if (s_Pool.workers[i]) {
            SDL_WaitThread(s_Pool.workers[i], NULL);
            s_Pool.workers[i] = NULL;
        }
    }

    SDL_DestroyCond(s_Pool.cv_work);
    SDL_DestroyCond(s_Pool.cv_idle);
    SDL_DestroyMutex(s_Pool.mutex);
    s_Pool.cv_work = NULL;
    s_Pool.cv_idle = NULL;
    s_Pool.mutex = NULL;
    s_Pool.initialized = 0;
}

int bootPoolGetWorkerCount(void)
{
    return s_Pool.initialized ? s_Pool.worker_count : 0;
}

int bootPoolGetThreadCount(void)
{
    /* +1 for the main thread implicit "manager" role; in Phase 2 the
     * dispatch happens on the main thread before entering the render
     * loop, so we don't spawn a separate manager thread.  Phase 3+ may
     * promote the manager to a real thread when fan-out begins. */
    return s_Pool.initialized ? (s_Pool.worker_count + 1) : 0;
}

void bootPoolEnqueue(boot_pool_job_fn fn, void *arg)
{
    if (!s_Pool.initialized || !fn) {
        return;
    }

    SDL_LockMutex(s_Pool.mutex);
    if (s_Pool.count >= BOOT_POOL_QUEUE_CAP) {
        SDL_UnlockMutex(s_Pool.mutex);
        sysLogPrintf(LOG_ERROR,
            "BOOT_POOL: queue full (cap=%d); dropping job", BOOT_POOL_QUEUE_CAP);
        return;
    }

    s_Pool.queue[s_Pool.tail].fn = fn;
    s_Pool.queue[s_Pool.tail].arg = arg;
    s_Pool.tail = (s_Pool.tail + 1) % BOOT_POOL_QUEUE_CAP;
    s_Pool.count++;
    SDL_CondSignal(s_Pool.cv_work);
    SDL_UnlockMutex(s_Pool.mutex);
}

void bootPoolWaitIdle(void)
{
    if (!s_Pool.initialized) {
        return;
    }

    SDL_LockMutex(s_Pool.mutex);
    while (s_Pool.count > 0 || s_Pool.in_flight > 0) {
        SDL_CondWait(s_Pool.cv_idle, s_Pool.mutex);
    }
    SDL_UnlockMutex(s_Pool.mutex);
}

/* Engine Phase 4: per-index range fan-out.  Generic version of the
 * Phase 3 verify-pass batch.  Manager runs inline so the caller blocks
 * until every index has been processed; cv_done signals the manager
 * once every worker has decremented workers_remaining.
 *
 * Local batch struct (one per call) so concurrent calls from different
 * orchestrator phases stay isolated.
 */
typedef struct {
    SDL_mutex          *mutex;
    SDL_cond           *cv_done;
    int                 next_index;
    int                 end_index;
    int                 workers_remaining;
    boot_pool_range_fn  fn;
    void               *user_ctx;
} boot_range_batch_t;

static void s_rangeWorker(void *arg)
{
    boot_range_batch_t *b = (boot_range_batch_t *)arg;

    for (;;) {
        int idx;
        SDL_LockMutex(b->mutex);
        if (b->next_index >= b->end_index) {
            SDL_UnlockMutex(b->mutex);
            break;
        }
        idx = b->next_index++;
        SDL_UnlockMutex(b->mutex);

        b->fn(idx, b->user_ctx);
    }

    SDL_LockMutex(b->mutex);
    b->workers_remaining--;
    if (b->workers_remaining == 0) {
        SDL_CondSignal(b->cv_done);
    }
    SDL_UnlockMutex(b->mutex);
}

void bootPoolForRangeBlocking(int begin, int end,
                               boot_pool_range_fn fn, void *ctx)
{
    if (begin >= end || fn == NULL) {
        return;
    }

    /* worker_count = boot pool's thread count (cores - 2 by default).
     * Floor 1 so minimal-hardware paths still execute correctly: with
     * worker_count == 1 we dispatch zero parallel workers and the
     * manager runs everything inline on the calling thread. */
    int worker_count = bootPoolGetWorkerCount();
    if (worker_count < 1) {
        worker_count = 1;
    }

    /* Cap workers at the range size: spawning more workers than items
     * just creates idle workers that immediately exit. */
    int span = end - begin;
    if (worker_count > span) {
        worker_count = span;
    }
    if (worker_count < 1) {
        worker_count = 1;
    }

    boot_range_batch_t b;
    b.mutex             = SDL_CreateMutex();
    b.cv_done           = SDL_CreateCond();
    b.next_index        = begin;
    b.end_index         = end;
    b.workers_remaining = worker_count;
    b.fn                = fn;
    b.user_ctx          = ctx;

    /* Dispatch (worker_count - 1) parallel workers; manager runs the
     * last slot inline.  See romExtractVerifyAll for the rationale.
     *
     * Pool not initialised (e.g. tests build without pool init): fall
     * through to the inline run below; the call still completes
     * synchronously on the caller's thread. */
    for (int w = 0; w < worker_count - 1; w++) {
        bootPoolEnqueue(s_rangeWorker, &b);
    }

    s_rangeWorker(&b);

    SDL_LockMutex(b.mutex);
    while (b.workers_remaining > 0) {
        SDL_CondWait(b.cv_done, b.mutex);
    }
    SDL_UnlockMutex(b.mutex);

    SDL_DestroyMutex(b.mutex);
    SDL_DestroyCond(b.cv_done);
}
