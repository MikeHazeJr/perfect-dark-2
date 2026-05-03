/**
 * romextract_parity_pdbody.c -- Catalog universality pivot Step 2
 * parity check (2026-05-03). Per Mike's Q-5 ruling: parity check
 * active during Steps 1-4, retires at Step 5 with .pdbase deletion.
 *
 * Strategy mirrors romextract_parity_pdhead.c. Re-reads each emitted
 * .pdbody file and verifies envelope + bodynum + ismale + height +
 * canvaryheight (the body-only carryover field that bodies F12
 * ships beyond the heads template).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <PR/ultratypes.h>

#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "catalog_mgr_bodies.h"
#include "loader_pdbase.h"
#include "romextract_pd.h"
#include "system.h"

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

s32 romExtractParityCheckPdbody(void)
{
	if (!loaderPdbaseBodiesActive()) {
		return 0;
	}

	s32 failures = 0;
	s32 checked = 0;

	for (s32 i = 0; i < CATALOG_MGR_BODY_COUNT; i++) {
		const body_data_t *b = loaderPdbaseGetBody(i);
		if (!b) continue;
		if (b->catalog_id[0] == '\0') continue;

		char filename[128];
		size_t k;
		for (k = 0; k + 1 < sizeof(filename) && b->catalog_id[k]; k++) {
			filename[k] = (b->catalog_id[k] == ':') ? '_' : b->catalog_id[k];
		}
		filename[k] = '\0';

		char relpath[FS_MAXPATH];
		snprintf(relpath, sizeof(relpath), "%s/bodies/%s.pdbody",
			fsDataDir(), filename);

		u32 size = 0;
		char *src = (char *)fsFileLoad(relpath, &size);
		if (!src || size == 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: bodynum=%d \"%s\": "
				"file missing or empty (\"%s\")",
				i, b->catalog_id, relpath);
			failures++;
			if (src) sysMemFree(src);
			continue;
		}

		char kind_buf[32] = {0};
		if (!s_parseStringOrInt(src, "pd_kind", kind_buf, sizeof(kind_buf))
		    || strcmp(kind_buf, "body") != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: bodynum=%d \"%s\": "
				"pd_kind != \"body\" (got \"%s\")",
				i, b->catalog_id, kind_buf);
			failures++;
			sysMemFree(src);
			continue;
		}

		char id_buf[64] = {0};
		if (!s_parseStringOrInt(src, "id", id_buf, sizeof(id_buf))
		    || strcmp(id_buf, b->catalog_id) != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: bodynum=%d "
				"id mismatch: expected=\"%s\" got=\"%s\"",
				i, b->catalog_id, id_buf);
			failures++;
		}

		long long bn = -1;
		if (!s_parseInt(src, "bodynum", &bn) || bn != i) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: bodynum mismatch: "
				"expected=%d got=%lld for \"%s\"",
				i, bn, b->catalog_id);
			failures++;
		}

		long long ismale = -1;
		if (!s_parseInt(src, "ismale", &ismale) ||
		    (u32)ismale != (u32)b->ismale) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: bodynum=%d \"%s\": "
				"ismale mismatch: expected=%u got=%lld",
				i, b->catalog_id, (unsigned)b->ismale, ismale);
			failures++;
		}

		long long height = -1;
		if (!s_parseInt(src, "height", &height) ||
		    (u32)height != (u32)b->height) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: bodynum=%d \"%s\": "
				"height mismatch: expected=%u got=%lld",
				i, b->catalog_id, (unsigned)b->height, height);
			failures++;
		}

		long long cvh = -1;
		if (!s_parseInt(src, "canvaryheight", &cvh) ||
		    (u32)cvh != (u32)b->canvaryheight) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: bodynum=%d \"%s\": "
				"canvaryheight mismatch: expected=%u got=%lld",
				i, b->catalog_id, (unsigned)b->canvaryheight, cvh);
			failures++;
		}

		sysMemFree(src);
		checked++;
	}

	if (failures == 0) {
		sysLogPrintf(LOG_NOTE,
			"LOADER.UNIVERSAL.PARITY: PASS (%d bodies checked)",
			checked);
	} else {
		sysLogPrintf(LOG_WARNING,
			"LOADER.UNIVERSAL.PARITY: %d failures across %d bodies",
			failures, checked);
	}

	return failures;
}
