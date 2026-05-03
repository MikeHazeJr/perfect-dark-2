/**
 * romextract_parity_pdsong.c -- Catalog universality pivot Step 3
 * audio half parity check for .pdsong (2026-05-03).
 *
 * Re-walks the seqtable in the disk-migrated "sequences" segment,
 * re-opens each emitted .pdsong ZIP, parses manifest.json, and
 * verifies envelope + key scalar fields round-trip the source
 * seqtableentry. Failures emit LOADER.UNIVERSAL.PARITY_FAIL.
 *
 * Same Q-5 structural-integrity contract as the other Step 3
 * parity checks.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <PR/ultratypes.h>

#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "modarchive.h"
#include "romdata.h"
#include "romextract_pd.h"
#include "system.h"

#define PDSONG_OUT_DIR "audio/music"

static s32 s_findSegment(const char *name, const u8 **outData, u32 *outSize)
{
	*outData = NULL;
	*outSize = 0;
	if (!name) return 0;

	s32 nseg = romdataSegmentCount();
	for (s32 i = 0; i < nseg; i++) {
		const char *seg_name = romdataSegmentGetName(i);
		if (seg_name && strcmp(seg_name, name) == 0) {
			const u8 *data = romdataSegmentGetData(i);
			u32 size = romdataSegmentGetSize(i);
			if (!data || size == 0) return 0;
			*outData = data;
			*outSize = size;
			return 1;
		}
	}
	return 0;
}

static const char *s_findKey(const char *src, const char *key)
{
	char pattern[128];
	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	const char *p = strstr(src, pattern);
	if (!p) return NULL;
	p += strlen(pattern);
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
	if (*p != ':') return NULL;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
	return p;
}

static s32 s_parseInt(const char *src, const char *key, long long *out_val)
{
	const char *p = s_findKey(src, key);
	if (!p) return 0;
	*out_val = strtoll(p, NULL, 10);
	return 1;
}

static s32 s_parseStr(const char *src, const char *key, char *out, size_t n)
{
	const char *p = s_findKey(src, key);
	if (!p || *p != '"') return 0;
	p++;
	size_t i = 0;
	while (*p && *p != '"' && i + 1 < n) out[i++] = *p++;
	out[i] = '\0';
	return 1;
}

s32 romExtractParityCheckPdsong(void)
{
	const u8 *seg_data = NULL;
	u32 seg_size = 0;

	if (!s_findSegment("sequences", &seg_data, &seg_size)) return 0;
	if (seg_size < sizeof(u16)) return 0;

	const struct seqtable *table = (const struct seqtable *)seg_data;
	u16 count = table->count;

	const u32 hdr_bytes = (u32)((const u8 *)&table->entries[0] - seg_data);
	const u32 max_by_size =
		(seg_size - hdr_bytes) / sizeof(struct seqtableentry);
	if ((u32)count > max_by_size) return 0;

	s32 failures = 0;
	s32 checked = 0;
	s32 skipped = 0;

	for (s32 i = 0; i < count; i++) {
		const struct seqtableentry *e = &table->entries[i];
		if (e->binlen == 0) { skipped++; continue; }

		char catalog_id[64];
		snprintf(catalog_id, sizeof(catalog_id),
			"base:song_%04x", (unsigned)i);

		char filename_slug[64];
		size_t k, j = 0;
		for (k = 0; catalog_id[k] && j + 1 < sizeof(filename_slug); k++) {
			filename_slug[j++] = (catalog_id[k] == ':') ? '_' : catalog_id[k];
		}
		filename_slug[j] = '\0';

		char relpath[FS_MAXPATH];
		snprintf(relpath, sizeof(relpath), "%s/%s/%s.pdsong",
			fsDataDir(), PDSONG_OUT_DIR, filename_slug);

		const char *full = fsFullPath(relpath);
		if (!full || !full[0]) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdsong slot=%d "
				"\"%s\": fsFullPath empty for \"%s\"",
				i, catalog_id, relpath);
			failures++;
			continue;
		}

		mod_archive_t *arc = modArchiveOpen(full);
		if (!arc) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdsong slot=%d "
				"\"%s\": modArchiveOpen failed (\"%s\")",
				i, catalog_id, relpath);
			failures++;
			continue;
		}

		s32 manifest_idx = modArchiveFindEntry(arc, "manifest.json");
		if (manifest_idx < 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdsong slot=%d "
				"\"%s\": missing manifest.json", i, catalog_id);
			failures++;
			modArchiveClose(arc);
			continue;
		}

		u32 manifest_size = 0;
		char *manifest = (char *)modArchiveExtractAlloc(arc, manifest_idx, &manifest_size);
		if (!manifest) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdsong slot=%d "
				"\"%s\": ExtractAlloc(manifest.json) failed",
				i, catalog_id);
			failures++;
			modArchiveClose(arc);
			continue;
		}

		s32 row_failures = 0;
		char buf[128];

		if (!s_parseStr(manifest, "pd_kind", buf, sizeof(buf))
		 || strcmp(buf, "song") != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdsong slot=%d "
				"\"%s\": pd_kind != \"song\" (got \"%s\")",
				i, catalog_id, buf);
			row_failures++;
		}

		if (!s_parseStr(manifest, "id", buf, sizeof(buf))
		 || strcmp(buf, catalog_id) != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdsong slot=%d "
				"id mismatch: expected=\"%s\" got=\"%s\"",
				i, catalog_id, buf);
			row_failures++;
		}

		long long iv = -1;
		if (!s_parseInt(manifest, "source_index", &iv) || iv != i) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdsong \"%s\": "
				"source_index mismatch: expected=%d got=%lld",
				catalog_id, i, iv);
			row_failures++;
		}

		if (!s_parseInt(manifest, "binlen", &iv) || (u32)iv != e->binlen) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdsong slot=%d "
				"\"%s\": binlen mismatch: expected=%u got=%lld",
				i, catalog_id, (unsigned)e->binlen, iv);
			row_failures++;
		}

		if (!s_parseInt(manifest, "ziplen", &iv) || (u32)iv != e->ziplen) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdsong slot=%d "
				"\"%s\": ziplen mismatch: expected=%u got=%lld",
				i, catalog_id, (unsigned)e->ziplen, iv);
			row_failures++;
		}

		free(manifest);

		s32 data_idx = modArchiveFindEntry(arc, "data.bin");
		if (data_idx < 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdsong slot=%d "
				"\"%s\": missing data.bin",
				i, catalog_id);
			row_failures++;
		} else {
			u32 data_size = modArchiveGetEntrySize(arc, data_idx);
			u32 expected = (e->ziplen > 0) ? (u32)e->ziplen : (u32)e->binlen;
			if (data_size != expected) {
				sysLogPrintf(LOG_WARNING,
					"LOADER.UNIVERSAL.PARITY_FAIL: pdsong slot=%d "
					"\"%s\": data.bin size mismatch: expected=%u got=%u",
					i, catalog_id, (unsigned)expected,
					(unsigned)data_size);
				row_failures++;
			}
		}

		modArchiveClose(arc);

		if (row_failures > 0) failures += row_failures;
		checked++;
	}

	if (failures == 0) {
		sysLogPrintf(LOG_NOTE,
			"LOADER.UNIVERSAL.PARITY: pdsong PASS (checked=%d "
			"skipped=%d total=%u)",
			checked, skipped, (unsigned)count);
	} else {
		sysLogPrintf(LOG_WARNING,
			"LOADER.UNIVERSAL.PARITY: pdsong %d failures across "
			"%d checked (skipped=%d total=%u)",
			failures, checked, skipped, (unsigned)count);
	}

	return failures;
}
