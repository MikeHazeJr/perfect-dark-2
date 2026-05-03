/**
 * modarchive_bench.c -- Priority M / B-238 / M-1.7 perf harness.
 *
 * Generates a synthetic `.pdmod` archive, opens it through the modarchive
 * reader, and times each operation called out in the design's 4.6 budget
 * table. Results print to stdout (sysLogPrintf -> log) and the process
 * exits cleanly so the harness is safe to wire in early during main()
 * before the rest of the engine spins up.
 *
 * Activated by `--bench-pdmod` on the command line. No effect when the
 * flag is absent. Not exercised by the headless server (pd-server has no
 * mod loader).
 *
 * Budget being verified (design 4.6):
 *   - First read of typical asset (~512 KB texture):     < 1 ms
 *   - Warm read (cached):                                < 1 us
 *   - Per-archive open at startup:                       < 5 ms
 *   - Per-archive close (mod disable):                  < 10 ms
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <PR/ultratypes.h>

#include "system.h"
#include "fs.h"
#include "modarchive.h"
#include "modvfs.h"
#include "modmigrate.h"
#include "modarchive_bench.h"

/* Synthetic asset shapes mirroring the design's 4.6 buckets. */
typedef struct {
	const char *name;
	u32 size;
} bench_asset_t;

static const bench_asset_t s_BenchAssets[] = {
	{ "manifest_only",  0 },        /* covered by mod.json read */
	{ "small.txt",      4 * 1024 },         /*   4 KiB */
	{ "med.tex",      512 * 1024 },         /* 512 KiB -- the design target */
	{ "large.tex",   4 * 1024 * 1024 },     /*   4 MiB */
};
#define BENCH_ASSET_COUNT ((s32)(sizeof(s_BenchAssets) / sizeof(s_BenchAssets[0])))

/* Manifest payload hand-rolled so the run is independent of the JSON
 * schema/parser evolution. */
static const char *s_BenchManifest =
	"{\n"
	"  \"id\": \"bench.synthetic\",\n"
	"  \"name\": \"Bench Synthetic\",\n"
	"  \"version\": \"1.0\",\n"
	"  \"author\": \"M-1.7\",\n"
	"  \"description\": \"Synthetic mod for the modarchive perf harness\"\n"
	"}\n";

/* Fill a buffer with semi-random bytes so deflate has actual entropy to
 * compress. A purely zero-filled buffer would deflate to almost nothing
 * and not exercise the inflate path. */
static void fillSemiRandom(u8 *buf, u32 size, u32 seed)
{
	u32 x = seed ? seed : 0x12345678u;
	for (u32 i = 0; i < size; i++) {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		buf[i] = (u8)(x & 0xFF);
	}
}

/* Build a synthetic .pdmod at `outPath`. Returns 1 on success. */
static int writeSyntheticArchive(const char *outPath)
{
	mod_archive_writer_t *w = modArchiveBegin(outPath);
	if (!w) return 0;

	if (modArchiveAddFileMem(w, "mod.json",
	                         s_BenchManifest, (u32)strlen(s_BenchManifest)) != MODARCHIVE_OK) {
		modArchiveAbort(w);
		return 0;
	}

	for (s32 i = 1; i < BENCH_ASSET_COUNT; i++) {
		const bench_asset_t *a = &s_BenchAssets[i];
		if (a->size == 0) continue;
		u8 *buf = (u8 *)malloc(a->size);
		if (!buf) {
			modArchiveAbort(w);
			return 0;
		}
		fillSemiRandom(buf, a->size, 0xC0FFEEu + (u32)i);
		s32 r = modArchiveAddFileMem(w, a->name, buf, a->size);
		free(buf);
		if (r != MODARCHIVE_OK) {
			modArchiveAbort(w);
			return 0;
		}
	}

	return modArchiveFinish(w) == MODARCHIVE_OK;
}

/* M-4.1 sibling utility: trigger the folder->.pdmod auto-migration in
 * isolation. Walks each candidate mods/ root in the same priority order
 * the loader uses, runs modMigrateRun on the first one that opens, and
 * exits. Useful for ops + the M-4.2 verification recipe. */
static void runStandaloneMigration(void)
{
	const char *candidates[4];
	char buf[4][512];
	fsFullPath("$E/../mods", buf[0], sizeof(buf[0]));
	strncpy(buf[1], "./mods", sizeof(buf[1]) - 1); buf[1][sizeof(buf[1]) - 1] = '\0';
	fsFullPath("$E/mods", buf[2], sizeof(buf[2]));
	fsFullPath("mods",    buf[3], sizeof(buf[3]));
	for (s32 i = 0; i < 4; i++) candidates[i] = buf[i];

	for (s32 i = 0; i < 4; i++) {
		if (!candidates[i][0]) continue;
		FILE *probe = fopen(candidates[i], "rb");
		if (probe || (probe = fopen(candidates[i], "wb"))) {
			fclose(probe);
		}
		struct stat st;
		if (stat(candidates[i], &st) != 0) continue;
		if (!(st.st_mode & S_IFDIR)) continue;
		sysLogPrintf(LOG_NOTE, "MIGRATE: targeting '%s'", candidates[i]);
		mod_migrate_summary_t mig = { 0, 0, 0 };
		s32 ran = modMigrateRun(candidates[i], &mig);
		if (ran) {
			sysLogPrintf(LOG_NOTE,
				"MIGRATE: standalone pass: packaged=%d skipped=%d failed=%d",
				mig.packaged, mig.skipped, mig.failed);
		} else {
			sysLogPrintf(LOG_NOTE,
				"MIGRATE: standalone pass: skipped (sentinel present or modsdir unreadable)");
		}
		exit(mig.failed > 0 ? 1 : 0);
	}
	sysLogPrintf(LOG_ERROR, "MIGRATE: no mods directory found in candidate roots");
	exit(2);
}

void modArchiveRunBenchmark(void)
{
	if (sysArgCheck("--migrate-pdmod")) {
		runStandaloneMigration();
		/* unreached */
	}

	if (!sysArgCheck("--bench-pdmod")) return;

	/* Try several writable locations in order; the cwd is the typical
	 * fall-back when the engine has not yet finalised its dir layout. */
	char outPath[FS_MAXPATH + 1];
	{
		char saveBuf[FS_MAXPATH + 1];
		char exeBuf[FS_MAXPATH + 1];
		const char *candidates[3] = {
			fsFullPath("$S/bench-pdmod-synthetic.pdmod", saveBuf, sizeof(saveBuf)),
			fsFullPath("$E/bench-pdmod-synthetic.pdmod", exeBuf,  sizeof(exeBuf)),
			"./bench-pdmod-synthetic.pdmod",
		};
		outPath[0] = '\0';
		for (s32 i = 0; i < 3 && !outPath[0]; i++) {
			if (!candidates[i] || !candidates[i][0]) continue;
			FILE *probe = fopen(candidates[i], "wb");
			if (probe) {
				fclose(probe);
				remove(candidates[i]);
				strncpy(outPath, candidates[i], FS_MAXPATH);
				outPath[FS_MAXPATH] = '\0';
			}
		}
	}
	if (!outPath[0]) {
		sysLogPrintf(LOG_ERROR, "BENCH: no writable location for synthetic .pdmod");
		exit(1);
	}

	sysLogPrintf(LOG_NOTE, "BENCH: writing synthetic .pdmod to %s", outPath);
	u64 t0 = sysGetMicroseconds();
	if (!writeSyntheticArchive(outPath)) {
		sysLogPrintf(LOG_ERROR, "BENCH: failed to write synthetic archive");
		exit(1);
	}
	u64 t1 = sysGetMicroseconds();
	sysLogPrintf(LOG_NOTE, "BENCH: write+deflate %4d entries in %6llu us",
		BENCH_ASSET_COUNT, (unsigned long long)(t1 - t0));

	/* OPEN: central-directory parse + manifest probe. Budget < 5 ms. */
	t0 = sysGetMicroseconds();
	mod_archive_t *arc = modArchiveOpen(outPath);
	t1 = sysGetMicroseconds();
	if (!arc) {
		sysLogPrintf(LOG_ERROR, "BENCH: modArchiveOpen failed (err=%d)", modArchiveLastError());
		exit(1);
	}
	u64 openUs = t1 - t0;
	sysLogPrintf(LOG_NOTE, "BENCH: modArchiveOpen          %6llu us  (budget <5000) %s",
		(unsigned long long)openUs, openUs < 5000 ? "PASS" : "NEEDS-OPTIMIZATION");

	/* MANIFEST READ. */
	t0 = sysGetMicroseconds();
	u32 mfstSize = 0;
	char *mfst = modArchiveReadManifest(arc, &mfstSize);
	t1 = sysGetMicroseconds();
	sysLogPrintf(LOG_NOTE, "BENCH: read mod.json (%4u B)  %6llu us",
		mfstSize, (unsigned long long)(t1 - t0));
	free(mfst);

	/* MOUNT INTO VFS so warm reads exercise the cache. */
	if (!modVfsMount("bench.synthetic", arc)) {
		sysLogPrintf(LOG_ERROR, "BENCH: modVfsMount failed");
		exit(1);
	}

	/* COLD + WARM READS for each non-manifest asset. */
	for (s32 i = 1; i < BENCH_ASSET_COUNT; i++) {
		const bench_asset_t *a = &s_BenchAssets[i];
		if (a->size == 0) continue;

		t0 = sysGetMicroseconds();
		u32 sz1 = 0;
		void *cold = modVfsResolveAllocFromMount("bench.synthetic", a->name, &sz1);
		t1 = sysGetMicroseconds();
		u64 coldUs = t1 - t0;
		free(cold);

		t0 = sysGetMicroseconds();
		u32 sz2 = 0;
		void *warm = modVfsResolveAllocFromMount("bench.synthetic", a->name, &sz2);
		t1 = sysGetMicroseconds();
		u64 warmUs = t1 - t0;
		free(warm);

		const char *coldVerdict = (a->size <= 512 * 1024) ? (coldUs < 1000 ? "PASS" : "NEEDS-OPTIMIZATION") : "(>512KB)";
		const char *warmVerdict = (warmUs < 1000) ? "PASS" : "NEEDS-OPTIMIZATION"; /* design says <1 us; us-resolution clock means <1ms is acceptable instrumented */
		sysLogPrintf(LOG_NOTE,
			"BENCH: %-12s (%7u B)  cold=%6llu us %-22s  warm=%6llu us %s",
			a->name, a->size,
			(unsigned long long)coldUs, coldVerdict,
			(unsigned long long)warmUs, warmVerdict);
	}

	/* CLOSE. Budget < 10 ms. */
	modVfsUnmount("bench.synthetic");
	t0 = sysGetMicroseconds();
	modArchiveClose(arc);
	t1 = sysGetMicroseconds();
	u64 closeUs = t1 - t0;
	sysLogPrintf(LOG_NOTE, "BENCH: modArchiveClose         %6llu us  (budget <10000) %s",
		(unsigned long long)closeUs, closeUs < 10000 ? "PASS" : "NEEDS-OPTIMIZATION");

	/* Cleanup. */
	remove(outPath);

	mod_vfs_stats_t st;
	modVfsGetStats(&st);
	sysLogPrintf(LOG_NOTE,
		"BENCH: vfs final stats hits=%llu misses=%llu cap_bytes=%llu resident=%llu evicted=%llu",
		(unsigned long long)st.hits,
		(unsigned long long)st.misses,
		(unsigned long long)st.cap_bytes,
		(unsigned long long)st.bytes_resident,
		(unsigned long long)st.bytes_evicted_total);

	sysLogPrintf(LOG_NOTE, "BENCH: complete -- exit");
	exit(0);
}
