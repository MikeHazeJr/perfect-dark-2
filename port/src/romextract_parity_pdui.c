/**
 * romextract_parity_pdui.c -- Catalog universality pivot Step 3b part 2
 * parity check (2026-05-03). Per Mike's Q-5 ruling parity stays active
 * through Steps 1-4 and retires at Step 5.
 *
 * Re-walks the canonical 14-texture .pdui table, re-opens each emitted
 * .pdui ZIP at data/<romid>/ui/<slug>.pdui, parses manifest.json, and
 * verifies envelope + key scalar fields round-trip the source. Failures
 * emit LOADER.UNIVERSAL.PARITY_FAIL with the texture catalog ID +
 * field detail.
 *
 * Skip semantics: missing .pdui files are treated as skip (not failure)
 * because the .pdui emitter is deferred to the render-loop on first
 * launch (g_TexGeneralConfigs not ready at boot main.c block). Parity
 * runs cleanly at boot before the .pdui files exist, then fires for
 * real on subsequent boots once the ZIPs are on disk.
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

#define PDUI_OUT_DIR "ui"

/* Canonical .pdui table mirror. Must stay in sync with k_PduiEntries[]
 * in port/fast3d/pdgui_theme.cpp. The mirror is intentional: the parity
 * check must NOT depend on the C++ side that produces the data; if the
 * two diverge, parity itself catches the drift. */
struct PduiParityEntry {
	const char *catalog_id;
	const char *file_slug;
	int         tex_index;
};

static const struct PduiParityEntry k_PduiParityEntries[] = {
	{ "base:ui_noise_sm",    "ui_noise_sm",     0 },
	{ "base:ui_particles",   "ui_particles",    1 },
	{ "base:ui_noise_lg",    "ui_noise_lg",     2 },
	{ "base:ui_grad_bar",    "ui_grad_bar",     3 },
	{ "base:ui_mirror_tile", "ui_mirror_tile",  4 },
	{ "base:ui_bg_haze",     "ui_bg_haze",      6 },
	{ "base:ui_dot_tile",    "ui_dot_tile",     7 },
	{ "base:ui_nuke",        "ui_nuke",        10 },
	{ "base:ui_bg_alt",      "ui_bg_alt",      11 },
	{ "base:ui_icon_a",      "ui_icon_a",      34 },
	{ "base:ui_icon_b",      "ui_icon_b",      35 },
	{ "base:ui_icon_c",      "ui_icon_c",      36 },
	{ "base:ui_deco",        "ui_deco",        37 },
	{ "base:ui_stars",       "ui_stars",       38 },
};

#define K_PDUI_PARITY_ENTRY_COUNT (sizeof(k_PduiParityEntries) / sizeof(k_PduiParityEntries[0]))

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

s32 romExtractParityCheckPdui(void)
{
#if defined(PD_SERVER)
	return 0;
#else
	s32 failures = 0;
	s32 checked = 0;
	s32 skipped = 0;

	for (size_t i = 0; i < K_PDUI_PARITY_ENTRY_COUNT; i++) {
		const struct PduiParityEntry *e = &k_PduiParityEntries[i];

		char relpath[FS_MAXPATH];
		snprintf(relpath, sizeof(relpath),
			"%s/%s/%s.pdui", fsDataDir(), PDUI_OUT_DIR, e->file_slug);

		s32 fsz = fsFileSize(relpath);
		if (fsz <= 0) {
			/* .pdui not yet emitted (first boot before render-loop trigger,
			 * or the texture's ROM extract failed). Treat as skip. */
			skipped++;
			continue;
		}

		const char *full = fsFullPath(relpath);
		if (!full || !full[0]) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdui id=\"%s\": "
				"fsFullPath empty for \"%s\"", e->catalog_id, relpath);
			failures++;
			continue;
		}

		mod_archive_t *arc = modArchiveOpen(full);
		if (!arc) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdui id=\"%s\": "
				"modArchiveOpen failed (\"%s\")", e->catalog_id, relpath);
			failures++;
			continue;
		}

		s32 manifest_idx = modArchiveFindEntry(arc, "manifest.json");
		if (manifest_idx < 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdui id=\"%s\": "
				"missing manifest.json", e->catalog_id);
			failures++;
			modArchiveClose(arc);
			continue;
		}

		u32 manifest_size = 0;
		char *manifest = (char *)modArchiveExtractAlloc(arc, manifest_idx, &manifest_size);
		if (!manifest) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdui id=\"%s\": "
				"ExtractAlloc(manifest.json) failed", e->catalog_id);
			failures++;
			modArchiveClose(arc);
			continue;
		}

		s32 row_failures = 0;
		char buf[128];

		if (!s_parseStr(manifest, "pd_kind", buf, sizeof(buf))
		 || strcmp(buf, "ui") != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdui id=\"%s\": "
				"pd_kind != \"ui\" (got \"%s\")", e->catalog_id, buf);
			row_failures++;
		}

		if (!s_parseStr(manifest, "id", buf, sizeof(buf))
		 || strcmp(buf, e->catalog_id) != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdui id=\"%s\": "
				"id mismatch: expected=\"%s\" got=\"%s\"",
				e->catalog_id, e->catalog_id, buf);
			row_failures++;
		}

		long long iv = -1;
		if (!s_parseInt(manifest, "texture_count", &iv) || iv != 1) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdui id=\"%s\": "
				"texture_count != 1 (got %lld)", e->catalog_id, iv);
			row_failures++;
		}

		iv = -1;
		if (!s_parseInt(manifest, "source_index", &iv) || iv != e->tex_index) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdui id=\"%s\": "
				"source_index mismatch: expected=%d got=%lld",
				e->catalog_id, e->tex_index, iv);
			row_failures++;
		}

		free(manifest);

		s32 tga_idx = modArchiveFindEntry(arc, "texture.tga");
		if (tga_idx < 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdui id=\"%s\": "
				"missing texture.tga", e->catalog_id);
			row_failures++;
		} else {
			u32 tga_size = modArchiveGetEntrySize(arc, tga_idx);
			if (tga_size < 18) {
				sysLogPrintf(LOG_WARNING,
					"LOADER.UNIVERSAL.PARITY_FAIL: pdui id=\"%s\": "
					"texture.tga truncated (size=%u)",
					e->catalog_id, (unsigned)tga_size);
				row_failures++;
			}
		}

		modArchiveClose(arc);

		if (row_failures > 0) failures += row_failures;
		checked++;
	}

	if (failures == 0) {
		sysLogPrintf(LOG_NOTE,
			"LOADER.UNIVERSAL.PARITY: pdui PASS (checked=%d skipped=%d "
			"total=%zu)", checked, skipped, K_PDUI_PARITY_ENTRY_COUNT);
	} else {
		sysLogPrintf(LOG_WARNING,
			"LOADER.UNIVERSAL.PARITY: pdui %d failures across %d "
			"checked (skipped=%d total=%zu)",
			failures, checked, skipped, K_PDUI_PARITY_ENTRY_COUNT);
	}

	return failures;
#endif
}
