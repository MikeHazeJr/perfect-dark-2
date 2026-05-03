/**
 * romextract_pdlang.c -- Catalog universality pivot Step 3b
 * (2026-05-03).
 *
 * Per-asset language string-table emitter. Walks the g_LangFiles[]
 * bank-to-file mapping in src/game/lang.c and emits one .pdlang ZIP
 * compound per (bank, locale) at data/<romid>/lang/<id>.pdlang.
 *
 * Per universality-pivot-schemas.md Section 2.13:
 *   manifest.json         envelope + locale + bank metadata
 *   data.bin              raw lang file bytes (pre-preprocessLangFile)
 *   data.bin.sha256       outer-file SHA-256 sidecar
 *
 * Catalog ID convention (feedback_human_readable_ids):
 *   base:lang_<bank>_<locale>   e.g. base:lang_gun_en, base:lang_propobj_en
 *
 * Bank name derivation: parses the FILE_L<NAME><LOC> enum string
 * via loaderEnumNameForFileEnum on g_LangFiles[bank] (the English
 * canonical entry, which has the form FILE_L<NAME>E). Stripping
 * "FILE_L" prefix and the trailing locale char yields the bank
 * name (e.g. FILE_LGUNE -> "gun").
 *
 * Locale scope (NTSC primary target, this build):
 *   NTSC final  -> 1 locale (English) per bank, 68 .pdlang files.
 *   PAL final   -> up to 5 locales (E/G/F/S/I) per bank (~340 files).
 *                  langGetFileNumOffset() picks the active locale at
 *                  runtime; emitter walks all available locale slots.
 *   JPN final   -> Japanese single locale.
 *
 * For Step 3b initial ship, emit only the canonical English bank
 * (locale="en") per the build's langGetFileId(bank) result. The
 * remaining PAL/JPN locales fold in as a curation pass at Step 5
 * cleanup (or a follow-up worktree); the catalog ID stays stable
 * across that follow-up because it includes the locale suffix.
 *
 * The byte payload is the RAW pre-preprocess file as extracted to
 * disk by Pass A (data/<romid>/files/<sanitized>.bin). The runtime
 * preprocessLangFile (port/src/preprocess/filelang.c) byte-swaps
 * the offset table and rewrites string offsets in place; the
 * Step 4 universal loader runs the same preprocess at load time.
 *
 * Server build (PD_SERVER): returns 0 immediately; lang banks are
 * not loaded server-side.
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
#include "loader_enum_reverse.h"
#include "modarchive.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "sha256.h"
#include "system.h"

#define PDLANG_OUT_DIR "lang"

/* Walk range. g_LangFiles[] is sized 69 in src/game/lang.c (bank 0
 * is a sentinel zero entry, banks 1..68 carry real files). The
 * LANGBANK_* enum tops out at 0x44 = 68. */
#define PDLANG_BANK_MAX 68

#if !defined(PD_SERVER)

extern u16 g_LangFiles[];

/* Strip "FILE_L" prefix and trailing locale-char suffix from a
 * symbolic FILE_* enum string, write lowercase result to out. The
 * canonical English-tier entries are FILE_L<NAME>E (e.g.
 * FILE_LAMEE -> ame). Unknown shapes fall through unchanged. */
static s32 s_extractBankName(const char *file_sym, char *out, size_t out_n)
{
	if (!file_sym || !file_sym[0] || out_n == 0) {
		if (out_n) out[0] = '\0';
		return 0;
	}

	const char *body = file_sym;
	if (strncmp(body, "FILE_L", 6) == 0) {
		body = file_sym + 6;
	} else if (strncmp(body, "FILE_", 5) == 0) {
		body = file_sym + 5;
	}

	/* Body is now like "AMEE" or "GUNE". Drop the trailing locale
	 * letter (E/J/P/G/F/S/I) if it's exactly one upper-case char
	 * after a body of >= 2 chars. */
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
	return (s32)copy_len;
}

/* Emit one .pdlang ZIP for (bank, locale). Returns 1 written, 0
 * skipped (file not on disk for this build), -1 failed. */
static s32 s_emitOneLang(s32 bank, const char *locale_tag,
                         u16 file_id, const char *out_dir,
                         s32 force_rewrite)
{
	if (file_id == 0) return 0;

	char src_rel[FS_MAXPATH];
	if (romExtractRelPathForFilenum((s32)file_id, src_rel, sizeof(src_rel)) <= 0) {
		return 0;
	}
	if (fsFileSize(src_rel) <= 0) {
		/* File not extracted for this build; skip silently. Lang banks
		 * are sometimes ROM-region-specific. */
		return 0;
	}

	const char *file_sym = loaderEnumNameForFileEnum((s32)file_id);
	char bank_name[64];
	if (!s_extractBankName(file_sym, bank_name, sizeof(bank_name))
	    || bank_name[0] == '\0') {
		/* No symbolic name; fall back to hex bank index (Q-4 Bucket 2). */
		snprintf(bank_name, sizeof(bank_name), "%02x", (unsigned)bank);
	}

	char catalog_id[128];
	snprintf(catalog_id, sizeof(catalog_id),
		"base:lang_%s_%s", bank_name, locale_tag);

	char filename_slug[128];
	{
		size_t i, j = 0;
		for (i = 0; catalog_id[i] && j + 1 < sizeof(filename_slug); i++) {
			filename_slug[j++] = (catalog_id[i] == ':') ? '_' : catalog_id[i];
		}
		filename_slug[j] = '\0';
	}

	char dst_rel[FS_MAXPATH];
	snprintf(dst_rel, sizeof(dst_rel),
		"%s/%s.pdlang", out_dir, filename_slug);

	if (!force_rewrite && fsFileSize(dst_rel) > 0) return 0;

	u32 src_size = (u32)fsFileSize(src_rel);

	/* Stage category from bank ID range. Banks 0x01..0x25 (1..37) are
	 * stage lang banks (level dialog); 0x26+ are system banks
	 * (gun, mp_menu, options, etc). Schema 2.13 enumerates
	 * "stage" / "mp_ui" / "system". */
	const char *category;
	if (bank <= 0x25) category = "stage";
	else if (bank == 0x26 /*GUN*/ || bank == 0x29 /*PROPOBJ*/
	      || bank == 0x2a /*MPWEAPONS*/) category = "system";
	else category = "mp_ui";

	char manifest_buf[1024];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"lang\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"locale\": \"%s\",\n"
		"  \"category\": \"%s\",\n"
		"  \"data\": \"data.bin\",\n"
		"  \"data_size\": %u,\n"
		"  \"source_bank\": %d,\n"
		"  \"source_filenum\": %u,\n"
		"  \"source_symbol\": \"%s\"\n"
		"}\n",
		catalog_id, locale_tag, category,
		(unsigned)src_size, bank, (unsigned)file_id,
		file_sym ? file_sym : "");
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDLANG",
			"manifest.json snprintf truncated for bank=%d", bank);
		return -1;
	}

	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		sysLoudFailf("EXTRACT.PDLANG",
			"fsFullPath empty for \"%s\"", dst_rel);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDLANG",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "manifest.json",
	                          manifest_buf, (u32)manifest_len) != 0) {
		sysLoudFailf("EXTRACT.PDLANG",
			"AddFileMem manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		return -1;
	}

	char src_full_buf[FS_MAXPATH + 1];
	const char *src_full = fsFullPath(src_rel, src_full_buf, sizeof(src_full_buf));
	if (!src_full || !src_full[0]) {
		sysLoudFailf("EXTRACT.PDLANG",
			"fsFullPath empty for source \"%s\"", src_rel);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveAddFileDisk(aw, "data.bin", src_full) != 0) {
		sysLoudFailf("EXTRACT.PDLANG",
			"AddFileDisk data.bin failed for bank=%d -> \"%s\"",
			bank, dst_full);
		modArchiveAbort(aw);
		return -1;
	}

	u32 src_bytes_size = 0;
	void *src_bytes = fsFileLoad(src_rel, &src_bytes_size);
	if (src_bytes && src_bytes_size > 0) {
		u8 digest[SHA256_DIGEST_SIZE];
		sha256Hash(src_bytes, (size_t)src_bytes_size, digest);
		char hex[SHA256_HEX_SIZE + 1];
		sha256ToHex(digest, hex);
		hex[SHA256_HEX_SIZE] = '\0';
		char sidecar[SHA256_HEX_SIZE + 2];
		snprintf(sidecar, sizeof(sidecar), "%s\n", hex);
		if (modArchiveAddFileMem(aw, "data.bin.sha256",
		                          sidecar, (u32)strlen(sidecar)) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdlang: sidecar write failed for \"%s\"",
				dst_full);
		}
	}
	if (src_bytes) sysMemFree(src_bytes);

	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDLANG",
			"modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}

	return 1;
}

#endif /* !PD_SERVER */

s32 romExtractAllPdlang(s32 force_rewrite)
{
#if defined(PD_SERVER)
	(void)force_rewrite;
	return 0;
#else
	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDLANG", "fsDataDirEnsure failed");
		return -1;
	}

	char dataDirBuf[FS_MAXPATH + 1];
	char lang_dir[FS_MAXPATH];
	snprintf(lang_dir, sizeof(lang_dir),
		"%s/%s", fsDataDir(dataDirBuf, sizeof(dataDirBuf)), PDLANG_OUT_DIR);
	if (!fsCreateDir(lang_dir)) {
		sysLoudFailf("EXTRACT.PDLANG",
			"fsCreateDir(\"%s\") failed", lang_dir);
		return -1;
	}

	s32 written = 0;
	s32 skipped = 0;
	s32 failed = 0;

	/* For Step 3b initial ship, emit canonical English locale only.
	 * PAL/JPN extension lands as Step 5 cleanup or a follow-up
	 * worktree. The catalog ID includes the locale suffix so the
	 * follow-up's IDs don't collide with these. */
	const char *locale_tag = "en";

	for (s32 bank = 1; bank <= PDLANG_BANK_MAX; bank++) {
		u16 file_id = g_LangFiles[bank];
		s32 r = s_emitOneLang(bank, locale_tag, file_id,
		                      lang_dir, force_rewrite);
		if (r > 0)        written++;
		else if (r == 0)  skipped++;
		else              failed++;
	}

	sysLogPrintf(LOG_NOTE,
		"romextract pdlang: written=%d skipped=%d failed=%d "
		"banks=%d locale=%s (out=%s)",
		written, skipped, failed, PDLANG_BANK_MAX, locale_tag, lang_dir);

	return written;
#endif
}
