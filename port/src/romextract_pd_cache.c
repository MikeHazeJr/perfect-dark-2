#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

#include <PR/ultratypes.h>

#include "romextract_pd.h"
#include "system.h"

#define PDEXTRACT_CACHE_SCHEMA "pdasset-fast-v1-20260521"
#define PDEXTRACT_CACHE_NAME ".pdextract-cache"
#define PDEXTRACT_CACHE_PATH_LEN 1024

/* Some extractor kinds are long (the pdscenario kind is ~356 chars). The
 * stamp writer emits the full kind, so the readers MUST size their buffer to
 * hold it -- a short buffer truncates the kind, making every kind compare
 * mismatch (silent always-miss for the directory fast-cache, and a false
 * "kind changed" for the in-place re-extract guard, which would re-extract
 * those dirs every boot). 512 covers the current longest kind with headroom.
 * Keep the fscanf width specifiers (PDEXTRACT_CACHE_KIND_SCANF) in sync. */
#define PDEXTRACT_CACHE_KIND_MAX 512
#define PDEXTRACT_CACHE_KIND_SCANF "511"

typedef struct {
	s32 count;
	unsigned long long total_bytes;
	unsigned long long latest_mtime;
} pdextract_dir_fingerprint_t;

static s32 s_hasSuffix(const char *s, const char *suffix)
{
	size_t slen;
	size_t tlen;
	if (s == NULL || suffix == NULL) return 0;
	slen = strlen(s);
	tlen = strlen(suffix);
	if (tlen == 0 || slen < tlen) return 0;
	return strcmp(s + slen - tlen, suffix) == 0;
}

static s32 s_cachePath(const char *abs_dir, char *out, size_t out_len)
{
	if (abs_dir == NULL || out == NULL || out_len == 0) return 0;
	int n = snprintf(out, out_len, "%s/%s", abs_dir, PDEXTRACT_CACHE_NAME);
	if (n <= 0 || (size_t)n >= out_len) {
		out[0] = '\0';
		return 0;
	}
	return 1;
}

static s32 s_scanDir(const char *abs_dir, const char *ext,
                     pdextract_dir_fingerprint_t *fp)
{
	if (abs_dir == NULL || ext == NULL || fp == NULL) return 0;
	memset(fp, 0, sizeof(*fp));

	DIR *dir = opendir(abs_dir);
	if (dir == NULL) {
		return 0;
	}

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.') continue;
		if (!s_hasSuffix(ent->d_name, ext)) continue;

		char path[PDEXTRACT_CACHE_PATH_LEN];
		int n = snprintf(path, sizeof(path), "%s/%s", abs_dir, ent->d_name);
		if (n <= 0 || (size_t)n >= sizeof(path)) {
			closedir(dir);
			return 0;
		}

		struct stat st;
		if (stat(path, &st) != 0) {
			closedir(dir);
			return 0;
		}

		fp->count++;
		fp->total_bytes += (unsigned long long)st.st_size;
		if ((unsigned long long)st.st_mtime > fp->latest_mtime) {
			fp->latest_mtime = (unsigned long long)st.st_mtime;
		}
	}

	closedir(dir);
	return 1;
}

static s32 s_readStamp(const char *path, const char *kind,
                       pdextract_dir_fingerprint_t *fp)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL) return 0;

	char schema[128] = {0};
	char stamp_kind[PDEXTRACT_CACHE_KIND_MAX] = {0};
	pdextract_dir_fingerprint_t disk;
	memset(&disk, 0, sizeof(disk));

	int matched = fscanf(f,
		"schema=%127s\nkind=%" PDEXTRACT_CACHE_KIND_SCANF "s\n"
		"count=%d\ntotal_bytes=%llu\nlatest_mtime=%llu\n",
		schema, stamp_kind, &disk.count, &disk.total_bytes, &disk.latest_mtime);
	fclose(f);

	if (matched != 5) return 0;
	if (strcmp(schema, PDEXTRACT_CACHE_SCHEMA) != 0) return 0;
	if (strcmp(stamp_kind, kind) != 0) return 0;

	*fp = disk;
	return 1;
}

s32 romExtractPdFastCacheCanSkip(const char *kind, const char *abs_dir,
                                 const char *ext, s32 force_rewrite)
{
	if (force_rewrite || kind == NULL || abs_dir == NULL || ext == NULL) {
		return 0;
	}

	char stamp_path[PDEXTRACT_CACHE_PATH_LEN];
	if (!s_cachePath(abs_dir, stamp_path, sizeof(stamp_path))) {
		return 0;
	}

	pdextract_dir_fingerprint_t stamp;
	if (!s_readStamp(stamp_path, kind, &stamp)) {
		return 0;
	}

	pdextract_dir_fingerprint_t live;
	if (!s_scanDir(abs_dir, ext, &live)) {
		return 0;
	}

	if (live.count == stamp.count &&
	    live.total_bytes == stamp.total_bytes &&
	    live.latest_mtime == stamp.latest_mtime &&
	    live.count > 0) {
		sysLogPrintf(LOG_NOTE,
			"romextract %s: fast-cache valid count=%d bytes=%llu",
			kind, live.count, live.total_bytes);
		return 1;
	}

	sysLogPrintf(LOG_NOTE,
		"romextract %s: fast-cache stale "
		"(stamp count=%d bytes=%llu mtime=%llu; live count=%d bytes=%llu mtime=%llu)",
		kind, stamp.count, stamp.total_bytes, stamp.latest_mtime,
		live.count, live.total_bytes, live.latest_mtime);
	return 0;
}

s32 romExtractPdFastCacheKindMismatch(const char *kind, const char *abs_dir)
{
	if (kind == NULL || abs_dir == NULL) {
		return 0;
	}

	char stamp_path[PDEXTRACT_CACHE_PATH_LEN];
	if (!s_cachePath(abs_dir, stamp_path, sizeof(stamp_path))) {
		return 0;
	}

	FILE *f = fopen(stamp_path, "rb");
	if (f == NULL) {
		/* No prior stamp -> clean install, not an in-place kind change. */
		return 0;
	}

	char schema[128] = {0};
	char stamp_kind[PDEXTRACT_CACHE_KIND_MAX] = {0};
	int matched = fscanf(f,
		"schema=%127s\nkind=%" PDEXTRACT_CACHE_KIND_SCANF "s\n",
		schema, stamp_kind);
	fclose(f);

	if (matched != 2) {
		/* Unparseable stamp: let the directory fast-cache treat it as a
		 * miss and the per-file validators decide; don't force a rewrite
		 * off a corrupt marker. */
		return 0;
	}
	if (strcmp(schema, PDEXTRACT_CACHE_SCHEMA) != 0) {
		/* Schema rev is its own re-extraction trigger handled elsewhere;
		 * a kind comparison across schema revisions is meaningless. */
		return 0;
	}

	if (strcmp(stamp_kind, kind) != 0) {
		sysLogPrintf(LOG_NOTE,
			"romextract %s: cache-kind changed in place "
			"(stamp kind=\"%s\"); forcing per-file re-extract",
			kind, stamp_kind);
		return 1;
	}

	return 0;
}

void romExtractPdFastCacheWrite(const char *kind, const char *abs_dir,
                                const char *ext)
{
	if (kind == NULL || abs_dir == NULL || ext == NULL) {
		return;
	}

	pdextract_dir_fingerprint_t fp;
	if (!s_scanDir(abs_dir, ext, &fp) || fp.count <= 0) {
		return;
	}

	char stamp_path[PDEXTRACT_CACHE_PATH_LEN];
	if (!s_cachePath(abs_dir, stamp_path, sizeof(stamp_path))) {
		return;
	}

	FILE *f = fopen(stamp_path, "wb");
	if (f == NULL) {
		sysLogPrintf(LOG_WARNING,
			"romextract %s: could not write fast-cache stamp \"%s\"",
			kind, stamp_path);
		return;
	}

	fprintf(f,
		"schema=%s\nkind=%s\ncount=%d\ntotal_bytes=%llu\nlatest_mtime=%llu\n",
		PDEXTRACT_CACHE_SCHEMA, kind, fp.count,
		fp.total_bytes, fp.latest_mtime);
	fclose(f);
}
