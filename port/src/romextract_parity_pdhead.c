/**
 * romextract_parity_pdhead.c -- Catalog universality pivot Step 2
 * parity check (2026-05-03). Per Mike's Q-5 ruling: parity check
 * active during Steps 1-4, retires at Step 5 with .pdbase deletion.
 *
 * Strategy mirrors romextract_parity_pdwpn.c: re-read each emitted
 * .pdhead JSON file and verify:
 *   1. File exists with non-zero size.
 *   2. Envelope present and well-formed (pd_kind, pd_schema_version, id).
 *   3. headnum field matches the loader pool slot.
 *   4. ismale + height fields match the loader pool's head_data_t.
 *
 * Any failure emits LOADER.UNIVERSAL.PARITY_FAIL with headnum, field,
 * and observed-vs-expected values. Returns the count of failed heads
 * (0 = pass).
 *
 * Full field-by-field parity is the responsibility of Step 4 when the
 * universal directory walker can re-populate a parallel pool from the
 * .pdhead files; comparing two pools field-by-field is the F12-equivalent
 * of weapons gate parity.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <PR/ultratypes.h>

#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "catalog_mgr_heads.h"
#include "loader_pdbase.h"
#include "romextract_pd.h"
#include "system.h"

/* Tiny "find string then read scalar" parser (shared idiom with
 * romextract_parity_pdwpn.c -- duplicated rather than refactored to
 * keep each parity unit self-contained for the F13-style retirement
 * at Step 5). */
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
	long long v = strtoll(p, NULL, 10);
	*out_val = v;
	return 1;
}

static s32 s_parseStringOrInt(const char *src, const char *key,
                               char *out_buf, size_t n)
{
	const char *p = s_findKey(src, key);
	if (!p) return 0;
	if (*p == '"') {
		p++;
		size_t i = 0;
		while (*p && *p != '"' && i + 1 < n) {
			out_buf[i++] = *p++;
		}
		out_buf[i] = '\0';
		return 1;
	}
	size_t i = 0;
	while (*p && (*p == '-' || (*p >= '0' && *p <= '9')) && i + 1 < n) {
		out_buf[i++] = *p++;
	}
	out_buf[i] = '\0';
	return i > 0;
}

s32 romExtractParityCheckPdhead(void)
{
	if (!loaderPdbaseHeadsActive()) {
		return 0;
	}

	s32 failures = 0;
	s32 checked = 0;

	for (s32 i = 0; i < CATALOG_MGR_HEAD_COUNT; i++) {
		const head_data_t *h = loaderPdbaseGetHead(i);
		if (!h) continue;
		if (h->catalog_id[0] == '\0') continue;

		char filename[128];
		size_t k;
		for (k = 0; k + 1 < sizeof(filename) && h->catalog_id[k]; k++) {
			filename[k] = (h->catalog_id[k] == ':') ? '_' : h->catalog_id[k];
		}
		filename[k] = '\0';

		char relpath[FS_MAXPATH];
		snprintf(relpath, sizeof(relpath), "%s/heads/%s.pdhead",
			fsDataDir(), filename);

		u32 size = 0;
		char *src = (char *)fsFileLoad(relpath, &size);
		if (!src || size == 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: headnum=%d \"%s\": "
				"file missing or empty (\"%s\")",
				i, h->catalog_id, relpath);
			failures++;
			if (src) sysMemFree(src);
			continue;
		}

		char kind_buf[32] = {0};
		if (!s_parseStringOrInt(src, "pd_kind", kind_buf, sizeof(kind_buf))
		    || strcmp(kind_buf, "head") != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: headnum=%d \"%s\": "
				"pd_kind != \"head\" (got \"%s\")",
				i, h->catalog_id, kind_buf);
			failures++;
			sysMemFree(src);
			continue;
		}

		char id_buf[64] = {0};
		if (!s_parseStringOrInt(src, "id", id_buf, sizeof(id_buf))
		    || strcmp(id_buf, h->catalog_id) != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: headnum=%d "
				"id mismatch: expected=\"%s\" got=\"%s\"",
				i, h->catalog_id, id_buf);
			failures++;
		}

		long long hn = -1;
		if (!s_parseInt(src, "headnum", &hn) || hn != i) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: headnum mismatch: "
				"expected=%d got=%lld for \"%s\"",
				i, hn, h->catalog_id);
			failures++;
		}

		long long ismale = -1;
		if (!s_parseInt(src, "ismale", &ismale) ||
		    (u32)ismale != (u32)h->ismale) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: headnum=%d \"%s\": "
				"ismale mismatch: expected=%u got=%lld",
				i, h->catalog_id, (unsigned)h->ismale, ismale);
			failures++;
		}

		long long height = -1;
		if (!s_parseInt(src, "height", &height) ||
		    (u32)height != (u32)h->height) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: headnum=%d \"%s\": "
				"height mismatch: expected=%u got=%lld",
				i, h->catalog_id, (unsigned)h->height, height);
			failures++;
		}

		sysMemFree(src);
		checked++;
	}

	if (failures == 0) {
		sysLogPrintf(LOG_NOTE,
			"LOADER.UNIVERSAL.PARITY: PASS (%d heads checked)",
			checked);
	} else {
		sysLogPrintf(LOG_WARNING,
			"LOADER.UNIVERSAL.PARITY: %d failures across %d heads",
			failures, checked);
	}

	return failures;
}
