/**
 * modarchive_bench.h -- Priority M / B-238 / M-1.7.
 *
 * Single entry point. Called early in main(). Exits the process when
 * the bench flag is set; returns silently otherwise.
 */
#ifndef _IN_MODARCHIVE_BENCH_H
#define _IN_MODARCHIVE_BENCH_H

#ifdef __cplusplus
extern "C" {
#endif

void modArchiveRunBenchmark(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MODARCHIVE_BENCH_H */
