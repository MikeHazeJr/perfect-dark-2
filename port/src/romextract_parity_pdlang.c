/**
 * romextract_parity_pdlang.c -- Catalog universality pivot Step 3b
 * parity check for .pdlang (2026-05-03). Re-walks g_LangFiles[1..68]
 * + emits English-locale rows in lockstep with the emitter, re-opens
 * each emitted .pdlang ZIP, parses manifest.json, asserts envelope +
 * key scalar fields round-trip the source. Failures emit
 * LOADER.UNIVERSAL.PARITY_FAIL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <PR/ultratypes.h>

#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "loader_pdbase_enums.h"
#include "modarchive.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "system.h"

#define PDLANG_OUT_DIR "lang"
#define PDLANG_BANK_MAX 68

#if !defined(PD_SERVER)

extern u16 g_LangFiles[];

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

/* Mirror of the emitter's bank-name extractor. Kept private here
 * (parity has its own iteration shape; emitter's helper is static
 * to its file). */
static void s_extractBankName(const char *file_sym, char *out, size_t out_n)
{
	if (out_n == 0) return;
	out[0] = '\0';
	if (!file_sym || !file_sym[0]) return;

	const char *body = file_sym;
	if (strncmp(body, "FILE_L", 6) == 0) body = file_sym + 6;
	else if (strncmp(body, "FILE_", 5) == 0) body = file_sym + 5;

	size_t body_len = strlen(body);
	size_t copy_len = body_len;
	if (body_len >= 2) {
		char tail = body[body_len - 1];
		if (tail == 'E' || tail == 'J' || tail == 'P' ||
		    tail == 'G' || tail == 'F' || tail == 'S' || tail == 'I') {
			copy_len = body_len - 1;
		}
	}
	if (copy_len + 1 > out_n) copy_len = out_n - 1;
	for (size_t i = 0; i < copy_len; i++) {
		out[i] = (char)tolower((unsigned char)body[i]);
	}
	out[copy_len] = '\0';
}

#endif /* !PD_SERVER */

s32 romExtractParityCheckPdlang(void)
{
#if defined(PD_SERVER)
	return 0;
#else
	s32 failures = 0;
	s32 checked = 0;
	s32 skipped = 0;

	const char *locale_tag = "en";

	for (s32 bank = 1; bank <= PDLANG_BANK_MAX; bank++) {
		u16 file_id = g_LangFiles[bank];
		if (file_id == 0) { skipped++; continue; }

		char src_rel[FS_MAXPATH];
		if (romExtractRelPathForFilenum((s32)file_id, src_rel, sizeof(src_rel)) <= 0) {
			skipped++;
			continue;
		}
		s32 src_size = (s32)fsFileSize(src_rel);
		if (src_size <= 0) { skipped++; continue; }

		const char *file_sym = loaderPdbaseNameForFileEnum((s32)file_id);
		char bank_name[64];
		s_extractBankName(file_sym, bank_name, sizeof(bank_name));
		if (bank_name[0] == '\0') {
			snprintf(bank_name, sizeof(bank_name), "%02x", (unsigned)bank);
		}

		char catalog_id[128];
		snprintf(catalog_id, sizeof(catalog_id),
			"base:lang_%s_%s", bank_name, locale_tag);

		char filename_slug[128];
		size_t k, j = 0;
		for (k = 0; catalog_id[k] && j + 1 < sizeof(filename_slug); k++) {
			filename_slug[j++] = (catalog_id[k] == ':') ? '_' : catalog_id[k];
		}
		filename_slug[j] = '\0';

		char relpath[FS_MAXPATH];
		snprintf(relpath, sizeof(relpath),
			"%s/%s/%s.pdlang", fsDataDir(), PDLANG_OUT_DIR, filename_slug);

		const char *full = fsFullPath(relpath);
		if (!full || !full[0]) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdlang bank=%d \"%s\": "
				"fsFullPath empty for \"%s\"", bank, catalog_id, relpath);
			failures++;
			continue;
		}

		mod_archive_t *arc = modArchiveOpen(full);
		if (!arc) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdlang bank=%d \"%s\": "
				"modArchiveOpen failed", bank, catalog_id);
			failures++;
			continue;
		}

		s32 manifest_idx = modArchiveFindEntry(arc, "manifest.json");
		if (manifest_idx < 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdlang bank=%d \"%s\": "
				"missing manifest.json", bank, catalog_id);
			failures++;
			modArchiveClose(arc);
			continue;
		}

		u32 manifest_size = 0;
		char *manifest = (char *)modArchiveExtractAlloc(arc, manifest_idx, &manifest_size);
		if (!manifest) {
			failures++;
			modArchiveClose(arc);
			continue;
		}

		s32 row_failures = 0;
		char buf[128];

		if (!s_parseStr(manifest, "pd_kind", buf, sizeof(buf))
		 || strcmp(buf, "lang") != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdlang bank=%d \"%s\": "
				"pd_kind != \"lang\" (got \"%s\")", bank, catalog_id, buf);
			row_failures++;
		}

		if (!s_parseStr(manifest, "id", buf, sizeof(buf))
		 || strcmp(buf, catalog_id) != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdlang bank=%d "
				"id mismatch: expected=\"%s\" got=\"%s\"",
				bank, catalog_id, buf);
			row_failures++;
		}

		if (!s_parseStr(manifest, "locale", buf, sizeof(buf))
		 || strcmp(buf, locale_tag) != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdlang bank=%d \"%s\": "
				"locale mismatch: expected=\"%s\" got=\"%s\"",
				bank, catalog_id, locale_tag, buf);
			row_failures++;
		}

		long long iv = -1;
		if (!s_parseInt(manifest, "source_bank", &iv) || iv != bank) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdlang \"%s\": "
				"source_bank mismatch: expected=%d got=%lld",
				catalog_id, bank, iv);
			row_failures++;
		}

		if (!s_parseInt(manifest, "source_filenum", &iv) || (u32)iv != file_id) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdlang \"%s\": "
				"source_filenum mismatch: expected=%u got=%lld",
				catalog_id, (unsigned)file_id, iv);
			row_failures++;
		}

		if (!s_parseInt(manifest, "data_size", &iv) || iv != src_size) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdlang \"%s\": "
				"data_size mismatch: expected=%d got=%lld",
				catalog_id, src_size, iv);
			row_failures++;
		}

		free(manifest);

		s32 data_idx = modArchiveFindEntry(arc, "data.bin");
		if (data_idx < 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: pdlang \"%s\": "
				"missing data.bin", catalog_id);
			row_failures++;
		} else {
			u32 data_size = modArchiveGetEntrySize(arc, data_idx);
			if ((s32)data_size != src_size) {
				sysLogPrintf(LOG_WARNING,
					"LOADER.UNIVERSAL.PARITY_FAIL: pdlang \"%s\": "
					"data.bin size mismatch: expected=%d got=%u",
					catalog_id, src_size, (unsigned)data_size);
				row_failures++;
			}
		}

		modArchiveClose(arc);

		if (row_failures > 0) failures += row_failures;
		checked++;
	}

	if (failures == 0) {
		sysLogPrintf(LOG_NOTE,
			"LOADER.UNIVERSAL.PARITY: pdlang PASS (checked=%d skipped=%d "
			"banks=%d)", checked, skipped, PDLANG_BANK_MAX);
	} else {
		sysLogPrintf(LOG_WARNING,
			"LOADER.UNIVERSAL.PARITY: pdlang %d failures across %d "
			"checked (skipped=%d banks=%d)",
			failures, checked, skipped, PDLANG_BANK_MAX);
	}

	return failures;
#endif
}
