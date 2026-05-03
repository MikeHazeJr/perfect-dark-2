/**
 * romextract_parity_pdarena.c -- Catalog universality pivot Step 2
 * parity check (2026-05-03). Per Mike's Q-5 ruling: parity check
 * active during Steps 1-4, retires at Step 5 with .pdbase deletion.
 *
 * Two-pronged: validates the .pdarena JSON envelope + key fields,
 * AND validates that the matching .pdscenario ZIP exists with
 * non-zero size for arenas with a resolvable stagetable entry.
 *
 * Verifies for each registered arena:
 *   1. data/<romid>/arenas/<id>.pdarena exists, well-formed envelope.
 *   2. arena_index + stagenum + name_langid match loader pool.
 *   3. If arena.stagenum has a stagetable entry, the corresponding
 *      data/<romid>/scenarios/<scenario_id>.pdscenario exists
 *      with non-zero size (no internal ZIP inspection -- that's
 *      the universal loader's responsibility at Step 4).
 *
 * Random meta arenas (STAGE_MP_RANDOM_MULTI / SOLO) do not require a
 * .pdscenario file. The arena's `scenario` field carries null in that
 * case and the parity check tolerates the absence.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <PR/ultratypes.h>

#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "catalog_mgr_arenas.h"
#include "loader_pdbase.h"
#include "romextract_pd.h"
#include "system.h"
#include "game/stagetable.h"

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

s32 romExtractParityCheckPdarena(void)
{
	if (!loaderPdbaseArenasActive()) {
		return 0;
	}

	s32 failures = 0;
	s32 arenas_checked = 0;
	s32 scenarios_expected = 0;
	s32 scenarios_present = 0;

	for (s32 i = 0; i < CATALOG_MGR_ARENA_COUNT; i++) {
		const arena_data_t *a = loaderPdbaseGetArena(i);
		if (!a) continue;
		if (a->catalog_id[0] == '\0') continue;

		/* --- .pdarena check --- */
		char arena_filename[128];
		size_t k;
		for (k = 0; k + 1 < sizeof(arena_filename) && a->catalog_id[k]; k++) {
			arena_filename[k] = (a->catalog_id[k] == ':') ? '_' : a->catalog_id[k];
		}
		arena_filename[k] = '\0';

		char arena_relpath[FS_MAXPATH];
		snprintf(arena_relpath, sizeof(arena_relpath), "%s/arenas/%s.pdarena",
			fsDataDir(), arena_filename);

		u32 size = 0;
		char *src = (char *)fsFileLoad(arena_relpath, &size);
		if (!src || size == 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: arena_index=%d \"%s\": "
				"pdarena file missing or empty (\"%s\")",
				i, a->catalog_id, arena_relpath);
			failures++;
			if (src) sysMemFree(src);
			continue;
		}

		char kind_buf[32] = {0};
		if (!s_parseStringOrInt(src, "pd_kind", kind_buf, sizeof(kind_buf))
		    || strcmp(kind_buf, "arena") != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: arena_index=%d \"%s\": "
				"pd_kind != \"arena\" (got \"%s\")",
				i, a->catalog_id, kind_buf);
			failures++;
			sysMemFree(src);
			continue;
		}

		char id_buf[64] = {0};
		if (!s_parseStringOrInt(src, "id", id_buf, sizeof(id_buf))
		    || strcmp(id_buf, a->catalog_id) != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: arena_index=%d "
				"id mismatch: expected=\"%s\" got=\"%s\"",
				i, a->catalog_id, id_buf);
			failures++;
		}

		long long ai = -1;
		if (!s_parseInt(src, "arena_index", &ai) || ai != i) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: arena_index mismatch: "
				"expected=%d got=%lld for \"%s\"",
				i, ai, a->catalog_id);
			failures++;
		}

		long long sn = -1;
		if (!s_parseInt(src, "stagenum", &sn) ||
		    (s32)sn != (s32)a->stagenum) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: arena_index=%d \"%s\": "
				"stagenum mismatch: expected=%d got=%lld",
				i, a->catalog_id, (s32)a->stagenum, sn);
			failures++;
		}

		long long lid = -1;
		if (!s_parseInt(src, "name_langid", &lid) ||
		    (s32)lid != a->name_langid) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: arena_index=%d \"%s\": "
				"name_langid mismatch: expected=%d got=%lld",
				i, a->catalog_id, a->name_langid, lid);
			failures++;
		}

		sysMemFree(src);
		arenas_checked++;

		/* --- .pdscenario check (only when arena has a real stagetable
		 *     entry; random meta arenas skip cleanly). --- */
		s32 stage_idx = stageGetIndex(a->stagenum);
		if (stage_idx < 0) continue;
		scenarios_expected++;

		char scenario_filename[128];
		snprintf(scenario_filename, sizeof(scenario_filename),
			"base_scenario_%s", a->slug);

		char scenario_relpath[FS_MAXPATH];
		snprintf(scenario_relpath, sizeof(scenario_relpath),
			"%s/scenarios/%s.pdscenario",
			fsDataDir(), scenario_filename);

		s32 sz = fsFileSize(scenario_relpath);
		if (sz <= 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: arena_index=%d \"%s\": "
				"pdscenario missing (\"%s\")",
				i, a->catalog_id, scenario_relpath);
			failures++;
		} else {
			scenarios_present++;
		}
	}

	if (failures == 0) {
		sysLogPrintf(LOG_NOTE,
			"LOADER.UNIVERSAL.PARITY: PASS (%d arenas, %d/%d scenarios checked)",
			arenas_checked, scenarios_present, scenarios_expected);
	} else {
		sysLogPrintf(LOG_WARNING,
			"LOADER.UNIVERSAL.PARITY: %d failures across %d arenas + %d scenarios",
			failures, arenas_checked, scenarios_expected);
	}

	return failures;
}
