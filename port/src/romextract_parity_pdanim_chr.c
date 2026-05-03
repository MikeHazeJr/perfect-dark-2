/**
 * romextract_parity_pdanim_chr.c -- Catalog universality pivot
 * Step 3a parity check (2026-05-03). Per Mike's Q-5 ruling: parity
 * stays active through the migration period and retires at Step 5
 * with the .pdbase deletion.
 *
 * Strategy: structural integrity on the emitted .pdanim ZIP compounds.
 * After romExtractAllPdanimChr writes per-asset character animations,
 * this function re-reads each one and verifies:
 *   1. ZIP archive opens (atomic-temp-rename landed cleanly).
 *   2. manifest.json present with envelope + chr-anim category.
 *   3. id matches the expected "base:..." catalog ID.
 *   4. source_index matches the lump table slot.
 *   5. frame_count + bytes_per_frame + header_len round-trip.
 *   6. frames.bin entry exists with correct uncompressed size
 *      (= headerlen + numframes * bytesperframe).
 *
 * Failures emit LOADER.UNIVERSAL.PARITY_FAIL with anim_idx, field,
 * and observed-vs-expected values. Returns the count of failed
 * animations (0 = pass).
 *
 * Full field-by-field round-trip parity is the responsibility of
 * Step 4 with the universal directory walker; this is the structural
 * counterpart to romExtractParityCheckPdwpn.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <PR/ultratypes.h>

#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "loader_pdbase_enums.h"
#include "modarchive.h"
#include "romdata.h"
#include "romextract_pd.h"
#include "system.h"

#define PDANIM_CHR_TABLE_TAIL_BYTES 0x38a0

/* Tiny "find string then read scalar" parser for the manifest.json
 * envelope. Mirrors the helper in romextract_parity_pdwpn.c. */
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

static s32 s_findAnimSegment(const u8 **outData, u32 *outSize)
{
	*outData = NULL;
	*outSize = 0;

	s32 nseg = romdataSegmentCount();
	for (s32 i = 0; i < nseg; i++) {
		const char *name = romdataSegmentGetName(i);
		if (name && strcmp(name, "animations") == 0) {
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

static void s_buildCatalogId(s32 anim_idx, char *out, size_t out_n)
{
	const char *sym = loaderPdbaseNameForAnimEnum(anim_idx);
	if (sym && sym[0]) {
		char lowered[96];
		size_t i;
		for (i = 0; sym[i] && i + 1 < sizeof(lowered); i++) {
			lowered[i] = (char)tolower((unsigned char)sym[i]);
		}
		lowered[i] = '\0';
		snprintf(out, out_n, "base:%s", lowered);
	} else {
		snprintf(out, out_n, "base:anim_chr_%04x", (unsigned)anim_idx);
	}
}

s32 romExtractParityCheckPdanimChr(void)
{
	const u8 *seg_data = NULL;
	u32 seg_size = 0;

	if (!s_findAnimSegment(&seg_data, &seg_size)) {
		/* No segment populated -> no extraction happened -> parity is
		 * vacuously satisfied. Same contract as the parity check on
		 * server builds for the .pdwpn / .pdhead / etc. paths. */
		return 0;
	}

	if (seg_size <= PDANIM_CHR_TABLE_TAIL_BYTES) {
		sysLogPrintf(LOG_WARNING,
			"LOADER.UNIVERSAL.PARITY: pdanim_chr segment size=%u too "
			"small for table tail (0x%x)",
			(unsigned)seg_size, PDANIM_CHR_TABLE_TAIL_BYTES);
		return 0;
	}

	const u8 *table_base =
		seg_data + (seg_size - PDANIM_CHR_TABLE_TAIL_BYTES);
	const u32 *table_u32 = (const u32 *)table_base;
	const u32 anim_count = table_u32[0];
	const struct animtableentry *entries =
		(const struct animtableentry *)&table_u32[1];

	const u32 max_entries =
		(PDANIM_CHR_TABLE_TAIL_BYTES - sizeof(u32))
		 / sizeof(struct animtableentry);
	if (anim_count > max_entries) {
		sysLogPrintf(LOG_WARNING,
			"LOADER.UNIVERSAL.PARITY: pdanim_chr count=%u exceeds "
			"capacity=%u; skipping parity",
			(unsigned)anim_count, (unsigned)max_entries);
		return 0;
	}

	s32 failures = 0;
	s32 checked = 0;
	s32 skipped = 0;

	for (s32 i = 0; (u32)i < anim_count; i++) {
		const struct animtableentry *e = &entries[i];

		/* Empty slot or mod-override marker: emitter skipped these,
		 * parity also skips. */
		if (e->data == 0xffffffff) { skipped++; continue; }
		if (e->numframes == 0 && e->headerlen == 0) { skipped++; continue; }

		char catalog_id[128];
		s_buildCatalogId(i, catalog_id, sizeof(catalog_id));

		char filename[128];
		size_t k;
		for (k = 0; k + 1 < sizeof(filename) && catalog_id[k]; k++) {
			filename[k] = (catalog_id[k] == ':') ? '_' : catalog_id[k];
		}
		filename[k] = '\0';

		char relpath[FS_MAXPATH];
		snprintf(relpath, sizeof(relpath), "%s/animations/%s.pdanim",
			fsDataDir(), filename);

		const char *full = fsFullPath(relpath);
		if (!full || !full[0]) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdanim_chr anim_idx=%d "
				"\"%s\": fsFullPath empty for \"%s\"",
				i, catalog_id, relpath);
			failures++;
			continue;
		}

		mod_archive_t *arc = modArchiveOpen(full);
		if (!arc) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdanim_chr anim_idx=%d "
				"\"%s\": modArchiveOpen failed (\"%s\")",
				i, catalog_id, relpath);
			failures++;
			continue;
		}

		s32 manifest_idx = modArchiveFindEntry(arc, "manifest.json");
		if (manifest_idx < 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdanim_chr anim_idx=%d "
				"\"%s\": missing manifest.json",
				i, catalog_id);
			failures++;
			modArchiveClose(arc);
			continue;
		}

		u32 manifest_size = 0;
		char *manifest = (char *)modArchiveExtractAlloc(arc, manifest_idx, &manifest_size);
		if (!manifest) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdanim_chr anim_idx=%d "
				"\"%s\": ExtractAlloc(manifest.json) failed",
				i, catalog_id);
			failures++;
			modArchiveClose(arc);
			continue;
		}

		/* Envelope: pd_kind. */
		char buf[128];
		s32 row_failures = 0;
		if (!s_parseStr(manifest, "pd_kind", buf, sizeof(buf))
		 || strcmp(buf, "animation") != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdanim_chr anim_idx=%d "
				"\"%s\": pd_kind != \"animation\" (got \"%s\")",
				i, catalog_id, buf);
			row_failures++;
		}

		if (!s_parseStr(manifest, "category", buf, sizeof(buf))
		 || strcmp(buf, "character_animation") != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdanim_chr anim_idx=%d "
				"\"%s\": category != \"character_animation\" (got \"%s\")",
				i, catalog_id, buf);
			row_failures++;
		}

		if (!s_parseStr(manifest, "id", buf, sizeof(buf))
		 || strcmp(buf, catalog_id) != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdanim_chr anim_idx=%d "
				"id mismatch: expected=\"%s\" got=\"%s\"",
				i, catalog_id, buf);
			row_failures++;
		}

		long long iv = -1;
		if (!s_parseInt(manifest, "source_index", &iv) || iv != i) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdanim_chr \"%s\": "
				"source_index mismatch: expected=%d got=%lld",
				catalog_id, i, iv);
			row_failures++;
		}

		if (!s_parseInt(manifest, "frame_count", &iv) || (u16)iv != e->numframes) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdanim_chr anim_idx=%d "
				"\"%s\": frame_count mismatch: expected=%u got=%lld",
				i, catalog_id, (unsigned)e->numframes, iv);
			row_failures++;
		}

		if (!s_parseInt(manifest, "bytes_per_frame", &iv) || (u16)iv != e->bytesperframe) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdanim_chr anim_idx=%d "
				"\"%s\": bytes_per_frame mismatch: expected=%u got=%lld",
				i, catalog_id, (unsigned)e->bytesperframe, iv);
			row_failures++;
		}

		if (!s_parseInt(manifest, "header_len", &iv) || (u16)iv != e->headerlen) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdanim_chr anim_idx=%d "
				"\"%s\": header_len mismatch: expected=%u got=%lld",
				i, catalog_id, (unsigned)e->headerlen, iv);
			row_failures++;
		}

		free(manifest);

		/* Verify frames.bin exists and has the expected size. */
		s32 frames_idx = modArchiveFindEntry(arc, "frames.bin");
		if (frames_idx < 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdanim_chr anim_idx=%d "
				"\"%s\": missing frames.bin",
				i, catalog_id);
			row_failures++;
		} else {
			u32 frames_size = modArchiveGetEntrySize(arc, frames_idx);
			u32 expected = (u32)e->headerlen
				+ (u32)e->numframes * (u32)e->bytesperframe;
			if (frames_size != expected) {
				sysLogPrintf(LOG_WARNING,
					"LOADER.UNIVERSAL.PARITY_FAIL: pdanim_chr anim_idx=%d "
					"\"%s\": frames.bin size mismatch: expected=%u got=%u",
					i, catalog_id, (unsigned)expected,
					(unsigned)frames_size);
				row_failures++;
			}
		}

		modArchiveClose(arc);

		if (row_failures > 0) failures += row_failures;
		checked++;
	}

	if (failures == 0) {
		sysLogPrintf(LOG_NOTE,
			"LOADER.UNIVERSAL.PARITY: pdanim_chr PASS (checked=%d "
			"skipped=%d total=%u)",
			checked, skipped, (unsigned)anim_count);
	} else {
		sysLogPrintf(LOG_WARNING,
			"LOADER.UNIVERSAL.PARITY: pdanim_chr %d failures across "
			"%d checked (skipped=%d total=%u)",
			failures, checked, skipped, (unsigned)anim_count);
	}

	return failures;
}
