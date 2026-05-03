#ifndef BOOT_POOL_H
#define BOOT_POOL_H

/* Boot Thread Pool (Engine Phase 2)
 *
 * SDL-backed worker pool created at boot to run catalog / extract / verify
 * work off the main thread.  Topology per Mike's Q1 (2026-05-03):
 *
 *   - Main thread       UI / SDL pump / overlay render (NEVER queued).
 *   - Manager thread    Coordinates the pool.  Phase 2: also runs the boot
 *                       orchestrator job (single-worker enqueue model).
 *                       Phase 3+: dispatches per-file work to workers and
 *                       aggregates progress.
 *   - Worker threads    Pull jobs from the queue and execute.  Count
 *                       scales to (physicalCores - 2).  Floor 1, cap 16.
 *
 * `bootPoolInit()` detects physical cores and spawns the threads.  Use
 * `bootPoolEnqueue()` to schedule work and `bootPoolWaitIdle()` to block
 * until the queue drains and all in-flight jobs complete.  Phase 2
 * enqueues a single job that runs the boot sequence serially on the
 * pool; Phase 3 will fan out the verify pass per file.
 *
 * `pd.ini` key Boot.WorkerThreads exposes the worker count for diagnostics
 * (default 0 = auto-detect).
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*boot_pool_job_fn)(void *arg);

/* Spawn the pool.  Idempotent.  Reads pd.ini Boot.WorkerThreads override
 * if set (clamped to [1, 16]); otherwise auto-detects physical cores. */
void bootPoolInit(void);

/* Drain the queue, signal stop, and join all threads.  Idempotent. */
void bootPoolShutdown(void);

/* Diagnostics: number of worker threads + manager thread. */
int  bootPoolGetWorkerCount(void);
int  bootPoolGetThreadCount(void);

/* Enqueue a job.  Caller retains ownership of `arg` (job is responsible
 * for freeing if needed). */
void bootPoolEnqueue(boot_pool_job_fn fn, void *arg);

/* Block the calling thread until the queue is empty and all in-flight
 * jobs have completed.  Safe to call from the main thread; safe to call
 * from a worker (in which case the caller must not be one of the jobs
 * we're waiting on, or it deadlocks). */
void bootPoolWaitIdle(void);

/* Engine Phase 4 (2026-05-03): per-index range fan-out.
 *
 * Fan out [begin, end) across the boot pool.  The caller's thread
 * participates as a worker (manager-runs-inline pattern from the Phase 3
 * verify pass), so the call is synchronous: it returns when every index
 * has been processed.
 *
 * Workers pull indices from a shared mutex-protected cursor; one mutex
 * acquisition per pulled index, no contention inside the callback.
 *
 * Contract:
 *   - `fn(i, ctx)` is invoked exactly once per i in [begin, end).
 *   - `ctx` is shared across all worker invocations; the callback owns
 *     synchronization for any shared writes (atomics, mutex, etc.).
 *   - Order is unspecified; callbacks run concurrently across workers.
 *   - MUST be called from the catalog-work manager thread (which is
 *     itself a pool worker) or from the main thread when the pool is
 *     idle.  Calling from another active pool worker would deadlock.
 *   - When the pool has only 1 worker (minimal hardware), execution
 *     stays serial on the calling thread and remains correct.
 *   - `begin >= end` or `fn == NULL` is a no-op.
 *
 * Pattern reference: port/src/romextract.c::romExtractVerifyAll (Phase 3
 * verify pass) for shared-cursor + manager-inline. */
typedef void (*boot_pool_range_fn)(int index, void *ctx);
void bootPoolForRangeBlocking(int begin, int end,
                               boot_pool_range_fn fn, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_POOL_H */
