/**
 * romextract_parity_pdfont.c -- Catalog universality pivot Step 3b
 * parity check for .pdfont (2026-05-03). Per Mike's Q-5 ruling
 * parity stays active through Steps 1-4 and retires at Step 5.
 *
 * Re-walks the canonical face table, re-opens each emitted .pdfont
 * ZIP, parses manifest.json, and verifies envelope + key scalar
 * fields round-trip the source segment. Failures emit
 * LOADER.UNIVERSAL.PARITY_FAIL with face name + field detail.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "modarchive.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "system.h"

#define PDFONT_OUT_DIR "fonts"

static const char *const k_FontFaces[] = {
	"bankgothic", "zurich", "tahoma", "numeric",
	"handelgothicxs", "handelgothicsm", "handelgothicmd", "handelgothiclg",
	"ocramd", "ocralg",
	"fontjpn", "fontjpnsingle",
};

#define K_FONT_FACE_COUNT (sizeof(k_FontFaces) / sizeof(k_FontFaces[0]))

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

s32 romExtractParityCheckPdfont(void)
{
#if defined(PD_SERVER)
	return 0;
#else
	s32 failures = 0;
	s32 checked = 0;
	s32 skipped = 0;

	for (size_t i = 0; i < K_FONT_FACE_COUNT; i++) {
		const char *face = k_FontFaces[i];

		char src_rel[FS_MAXPATH];
		if (romExtractSegmentRelPath(face, src_rel, sizeof(src_rel)) <= 0) {
			skipped++;
			continue;
		}
		s32 src_size = (s32)fsFileSize(src_rel);
		if (src_size <= 0) { skipped++; continue; }

		char catalog_id[96];
		snprintf(catalog_id, sizeof(catalog_id), "base:font_%s", face);

		char filename_slug[96];
		size_t k, j = 0;
		for (k = 0; catalog_id[k] && j + 1 < sizeof(filename_slug); k++) {
			filename_slug[j++] = (catalog_id[k] == ':') ? '_' : catalog_id[k];
		}
		filename_slug[j] = '\0';

		char relpath[FS_MAXPATH];
		snprintf(relpath, sizeof(relpath),
			"%s/%s/%s.pdfont", fsDataDir(), PDFONT_OUT_DIR, filename_slug);

		const char *full = fsFullPath(relpath);
		if (!full || !full[0]) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdfont face=\"%s\": "
				"fsFullPath empty for \"%s\"", face, relpath);
			failures++;
			continue;
		}

		mod_archive_t *arc = modArchiveOpen(full);
		if (!arc) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdfont face=\"%s\": "
				"modArchiveOpen failed (\"%s\")", face, relpath);
			failures++;
			continue;
		}

		s32 manifest_idx = modArchiveFindEntry(arc, "manifest.json");
		if (manifest_idx < 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdfont face=\"%s\": "
				"missing manifest.json", face);
			failures++;
			modArchiveClose(arc);
			continue;
		}

		u32 manifest_size = 0;
		char *manifest = (char *)modArchiveExtractAlloc(arc, manifest_idx, &manifest_size);
		if (!manifest) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdfont face=\"%s\": "
				"ExtractAlloc(manifest.json) failed", face);
			failures++;
			modArchiveClose(arc);
			continue;
		}

		s32 row_failures = 0;
		char buf[128];

		if (!s_parseStr(manifest, "pd_kind", buf, sizeof(buf))
		 || strcmp(buf, "font") != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdfont face=\"%s\": "
				"pd_kind != \"font\" (got \"%s\")", face, buf);
			row_failures++;
		}

		if (!s_parseStr(manifest, "id", buf, sizeof(buf))
		 || strcmp(buf, catalog_id) != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdfont face=\"%s\": "
				"id mismatch: expected=\"%s\" got=\"%s\"",
				face, catalog_id, buf);
			row_failures++;
		}

		if (!s_parseStr(manifest, "face", buf, sizeof(buf))
		 || strcmp(buf, face) != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdfont face=\"%s\": "
				"face mismatch: expected=\"%s\" got=\"%s\"",
				face, face, buf);
			row_failures++;
		}

		long long iv = -1;
		if (!s_parseInt(manifest, "data_size", &iv) || iv != src_size) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdfont face=\"%s\": "
				"data_size mismatch: expected=%d got=%lld",
				face, src_size, iv);
			row_failures++;
		}

		free(manifest);

		s32 data_idx = modArchiveFindEntry(arc, "data.bin");
		if (data_idx < 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdfont face=\"%s\": "
				"missing data.bin", face);
			row_failures++;
		} else {
			u32 data_size = modArchiveGetEntrySize(arc, data_idx);
			if ((s32)data_size != src_size) {
				sysLogPrintf(LOG_WARNING,
					"LOADER.UNIVERSAL.PARITY_FAIL: pdfont face=\"%s\": "
					"data.bin size mismatch: expected=%d got=%u",
					face, src_size, (unsigned)data_size);
				row_failures++;
			}
		}

		modArchiveClose(arc);

		if (row_failures > 0) failures += row_failures;
		checked++;
	}

	if (failures == 0) {
		sysLogPrintf(LOG_NOTE,
			"LOADER.UNIVERSAL.PARITY: pdfont PASS (checked=%d skipped=%d "
			"total=%zu)", checked, skipped, K_FONT_FACE_COUNT);
	} else {
		sysLogPrintf(LOG_WARNING,
			"LOADER.UNIVERSAL.PARITY: pdfont %d failures across %d "
			"checked (skipped=%d total=%zu)",
			failures, checked, skipped, K_FONT_FACE_COUNT);
	}

	return failures;
#endif
}
