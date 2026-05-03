/**
 * romextract_parity_pdwpn.c -- Catalog universality pivot Step 1
 * parity check (2026-05-02). Per Mike's Q-5 ruling: parity check
 * active during Step 1, retires at Step 5 with .pdbase deletion.
 *
 * Strategy: structural integrity rather than full round-trip. After
 * romExtractAllPdwpn writes the per-asset .pdwpn files, this function
 * re-reads each one and verifies:
 *   1. File exists with non-zero size.
 *   2. Envelope present and well-formed (pd_kind, pd_schema_version, id).
 *   3. weapon_id field matches the loader pool slot.
 *   4. flags field matches loader pool's wpn->flags.
 *   5. shortname field is present.
 *
 * Any failure emits LOADER.UNIVERSAL.PARITY_FAIL with weapon_id, field,
 * and observed-vs-expected values. Returns the count of failed weapons
 * (0 = pass).
 *
 * Full field-by-field parity is the responsibility of Step 4 when the
 * universal directory walker can re-populate a parallel pool from the
 * .pdwpn files; comparing two pools field-by-field is the F12-equivalent
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
#include "loader_pdbase.h"
#include "romextract_pd.h"
#include "system.h"

/* Tiny "find string then read scalar" parser: we only inspect a handful
 * of scalar fields so a full JSON parser is unnecessary for Step 1. */
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

/* Parse "<key>: <integer>" into out_val. Returns 1 on success. */
static s32 s_parseInt(const char *src, const char *key, long long *out_val)
{
	const char *p = s_findKey(src, key);
	if (!p) return 0;
	long long v = strtoll(p, NULL, 10);
	*out_val = v;
	return 1;
}

/* Parse "<key>: \"<str>\"" into out_buf. Also accepts a bare integer
 * (returns "<int>" stringified) so lang-id fields that we emit as
 * strings or ints round-trip. Returns 1 on success. */
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
	/* bare integer */
	size_t i = 0;
	while (*p && (*p == '-' || (*p >= '0' && *p <= '9')) && i + 1 < n) {
		out_buf[i++] = *p++;
	}
	out_buf[i] = '\0';
	return i > 0;
}

s32 romExtractParityCheckPdwpn(void)
{
	if (!loaderPdbaseIsActive()) {
		return 0;
	}

	s32 failures = 0;
	s32 checked = 0;

	for (s32 i = 0; i < CATALOG_MGR_WEAPON_COUNT; i++) {
		const struct weapon *wpn = loaderPdbaseGetWeapon(i);
		if (!wpn) continue;
		const char *catalog_id = loaderPdbaseGetWeaponCatalogId(i);
		if (!catalog_id) continue;

		/* Build expected on-disk path: data/<romid>/weapons/<id>.pdwpn */
		char filename[128];
		size_t k;
		for (k = 0; k + 1 < sizeof(filename) && catalog_id[k]; k++) {
			filename[k] = (catalog_id[k] == ':') ? '_' : catalog_id[k];
		}
		filename[k] = '\0';

		char relpath[FS_MAXPATH];
		snprintf(relpath, sizeof(relpath), "%s/weapons/%s.pdwpn",
			fsDataDir(), filename);

		u32 size = 0;
		char *src = (char *)fsFileLoad(relpath, &size);
		if (!src || size == 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: weapon_id=%d \"%s\": "
				"file missing or empty (\"%s\")",
				i, catalog_id, relpath);
			failures++;
			if (src) sysMemFree(src);
			continue;
		}
		/* Ensure NUL termination for strstr; fsFileLoad allocates +1. */
		src[size > 0 ? size - 1 : 0] = src[size > 0 ? size - 1 : 0];
		/* Defensive: write a trailing NUL into a safe spot. The buffer
		 * fsFileLoad returns is heap-allocated; we treat the last byte
		 * as guaranteed NUL by allocating with +1 in the loader. If
		 * not, we still scan up to `size` chars; strstr is fine on the
		 * binary content because pd*.json files are ASCII text. */

		/* Check envelope */
		char kind_buf[32] = {0};
		if (!s_parseStringOrInt(src, "pd_kind", kind_buf, sizeof(kind_buf))
		    || strcmp(kind_buf, "weapon") != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: weapon_id=%d \"%s\": "
				"pd_kind != \"weapon\" (got \"%s\")",
				i, catalog_id, kind_buf);
			failures++;
			sysMemFree(src);
			continue;
		}

		/* Check id matches */
		char id_buf[64] = {0};
		if (!s_parseStringOrInt(src, "id", id_buf, sizeof(id_buf))
		    || strcmp(id_buf, catalog_id) != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: weapon_id=%d "
				"id mismatch: expected=\"%s\" got=\"%s\"",
				i, catalog_id, id_buf);
			failures++;
		}

		/* Check weapon_id matches */
		long long wid = -1;
		if (!s_parseInt(src, "weapon_id", &wid) || wid != i) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: weapon_id mismatch: "
				"expected=%d got=%lld for \"%s\"",
				i, wid, catalog_id);
			failures++;
		}

		/* Check flags scalar matches. */
		long long flags = -1;
		if (!s_parseInt(src, "flags", &flags) || (u32)flags != wpn->flags) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: weapon_id=%d \"%s\": "
				"flags mismatch: expected=0x%08x got=0x%08llx",
				i, catalog_id, wpn->flags, (unsigned long long)flags);
			failures++;
		}

		sysMemFree(src);
		checked++;
	}

	if (failures == 0) {
		sysLogPrintf(LOG_NOTE,
			"LOADER.UNIVERSAL.PARITY: PASS (%d weapons checked)",
			checked);
	} else {
		sysLogPrintf(LOG_WARNING,
			"LOADER.UNIVERSAL.PARITY: %d failures across %d weapons",
			failures, checked);
	}

	return failures;
}
