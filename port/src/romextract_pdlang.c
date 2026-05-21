/**
 * romextract_pdlang.c -- Catalog universality pivot Step 3b
 * (2026-05-03).
 *
 * Per-asset language string-table emitter. Walks the g_LangFiles[]
 * bank-to-file mapping in src/game/lang.c and emits one .pdlang ZIP
 * compound per (bank, locale) at data/<romid>/lang/<id>.pdlang.
 *
 * Per-asset ZIP layout:
 *   lang.ini              editable language descriptor
 *   manifest.json         compatibility envelope + locale + bank metadata
 *   strings.tsv           escaped index<TAB>text source table
 *   strings.tsv.sha256    outer-file SHA-256 sidecar
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
 * The emitted TSV is decoded from the RAW pre-preprocess file as
 * extracted to disk by Pass A. The raw file starts with a big-endian
 * string offset table followed by null-terminated string bytes.
 *
 * Server build (PD_SERVER): returns 0 immediately; lang banks are
 * not loaded server-side.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <SDL.h>
#include <PR/ultratypes.h>

#include "boot_pool.h"
#include "boot_progress.h"
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

static s32 s_existingArchiveHasEntry(const char *relpath, const char *entry)
{
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return 0;
	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;
	s32 has_entry = modArchiveFindEntry(arc, entry) >= 0;
	modArchiveClose(arc);
	return has_entry;
}

static u32 s_readBe32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static u32 s_nextNonZeroOffset(const u8 *src, u32 offset, u32 len)
{
	for (u32 pos = offset; pos + 4u <= len; pos += 4u) {
		u32 value = s_readBe32(src + pos);
		if (value != 0) return value;
	}
	return 0;
}

static u32 s_langStringCount(const u8 *src, u32 len)
{
	if (!src || len < 4) return 0;
	u32 table_len = s_nextNonZeroOffset(src, 0, len);
	if (table_len != 0 && table_len <= len && (table_len % 4u) == 0) {
		return table_len / 4u;
	}
	return 1;
}

static u32 s_escapeTsvText(char *dst, u32 dst_cap, const u8 *src, u32 len)
{
	u32 w = 0;
	for (u32 i = 0; i < len && w + 1u < dst_cap; i++) {
		u8 ch = src[i];
		if (ch == '\\') {
			if (w + 2u >= dst_cap) break;
			dst[w++] = '\\';
			dst[w++] = '\\';
		} else if (ch == '\n') {
			if (w + 2u >= dst_cap) break;
			dst[w++] = '\\';
			dst[w++] = 'n';
		} else if (ch == '\t') {
			if (w + 2u >= dst_cap) break;
			dst[w++] = '\\';
			dst[w++] = 't';
		} else if (ch >= 0x20 && ch < 0x7f) {
			dst[w++] = (char)ch;
		} else {
			static const char hex[] = "0123456789abcdef";
			if (w + 4u >= dst_cap) break;
			dst[w++] = '\\';
			dst[w++] = 'x';
			dst[w++] = hex[(ch >> 4) & 0x0f];
			dst[w++] = hex[ch & 0x0f];
		}
	}
	if (dst_cap) dst[w] = '\0';
	return w;
}

static s32 s_buildStringsTsv(const u8 *src, u32 src_size,
                             char **out_text, u32 *out_size,
                             u32 *out_count)
{
	*out_text = NULL;
	*out_size = 0;
	*out_count = 0;

	u32 count = s_langStringCount(src, src_size);
	if (count == 0 || count > 512) return 0;

	u32 cap = src_size * 4u + count * 18u + 32u;
	char *tsv = (char *)malloc(cap);
	if (!tsv) return 0;
	u32 w = 0;

	for (u32 i = 0; i < count && w + 16u < cap; i++) {
		u32 offset = (i * 4u + 4u <= src_size) ? s_readBe32(src + i * 4u) : 0;
		u32 end = 0;
		if (offset != 0 && offset < src_size) {
			end = s_nextNonZeroOffset(src, (i + 1u) * 4u, count * 4u);
			if (end == 0 || end > src_size) end = src_size;
			if (end < offset) end = offset;
			while (end > offset && src[end - 1u] == 0) end--;
		} else {
			offset = 0;
			end = 0;
		}

		int n = snprintf(tsv + w, cap - w, "%u\t", (unsigned)i);
		if (n <= 0 || (u32)n >= cap - w) {
			free(tsv);
			return 0;
		}
		w += (u32)n;
		if (end > offset) {
			w += s_escapeTsvText(tsv + w, cap - w, src + offset, end - offset);
		}
		if (w + 1u >= cap) {
			free(tsv);
			return 0;
		}
		tsv[w++] = '\n';
	}
	tsv[w] = '\0';

	*out_text = tsv;
	*out_size = w;
	*out_count = count;
	return 1;
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

	if (!force_rewrite && fsFileSize(dst_rel) > 0 &&
	    s_existingArchiveHasEntry(dst_rel, "lang.ini") &&
	    s_existingArchiveHasEntry(dst_rel, "strings.tsv")) return 0;

	u32 src_size = (u32)fsFileSize(src_rel);
	u32 src_bytes_size = 0;
	void *src_bytes = fsFileLoad(src_rel, &src_bytes_size);
	if (!src_bytes || src_bytes_size == 0) {
		sysLoudFailf("EXTRACT.PDLANG",
			"fsFileLoad failed for bank=%d source \"%s\"", bank, src_rel);
		if (src_bytes) sysMemFree(src_bytes);
		return -1;
	}

	char *tsv_text = NULL;
	u32 tsv_size = 0;
	u32 string_count = 0;
	if (!s_buildStringsTsv((const u8 *)src_bytes, src_bytes_size,
	                       &tsv_text, &tsv_size, &string_count)) {
		sysLoudFailf("EXTRACT.PDLANG",
			"could not build strings.tsv for bank=%d source \"%s\"",
			bank, src_rel);
		sysMemFree(src_bytes);
		return -1;
	}

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
		"  \"data\": \"strings.tsv\",\n"
		"  \"data_size\": %u,\n"
		"  \"source_data_size\": %u,\n"
		"  \"string_count\": %u,\n"
		"  \"source_bank\": %d,\n"
		"  \"source_filenum\": %u,\n"
		"  \"source_symbol\": \"%s\"\n"
		"}\n",
		catalog_id, locale_tag, category,
		(unsigned)tsv_size,
		(unsigned)src_size,
		(unsigned)string_count,
		bank, (unsigned)file_id,
		file_sym ? file_sym : "");
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDLANG",
			"manifest.json snprintf truncated for bank=%d", bank);
		free(tsv_text);
		sysMemFree(src_bytes);
		return -1;
	}

	char ini_buf[768];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[lang]\n"
		"catalog_id = %s\n"
		"locale = %s\n"
		"category = %s\n"
		"strings_file = strings.tsv\n"
		"data_size = %u\n"
		"source_data_size = %u\n"
		"string_count = %u\n"
		"source_bank = %d\n"
		"source_filenum = %u\n"
		"source_symbol = %s\n",
		catalog_id, locale_tag, category,
		(unsigned)tsv_size,
		(unsigned)src_size,
		(unsigned)string_count,
		bank, (unsigned)file_id,
		file_sym ? file_sym : "");
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		sysLoudFailf("EXTRACT.PDLANG",
			"lang.ini snprintf truncated for bank=%d", bank);
		free(tsv_text);
		sysMemFree(src_bytes);
		return -1;
	}

	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		sysLoudFailf("EXTRACT.PDLANG",
			"fsFullPath empty for \"%s\"", dst_rel);
		free(tsv_text);
		sysMemFree(src_bytes);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDLANG",
			"modArchiveBegin failed for \"%s\"", dst_full);
		free(tsv_text);
		sysMemFree(src_bytes);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "lang.ini", ini_buf, (u32)ini_len) != 0) {
		sysLoudFailf("EXTRACT.PDLANG",
			"AddFileMem lang.ini failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		free(tsv_text);
		sysMemFree(src_bytes);
		return -1;
	}
	if (modArchiveAddFileMem(aw, "manifest.json",
	                          manifest_buf, (u32)manifest_len) != 0) {
		sysLoudFailf("EXTRACT.PDLANG",
			"AddFileMem manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		free(tsv_text);
		sysMemFree(src_bytes);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "strings.tsv", tsv_text, tsv_size) != 0) {
		sysLoudFailf("EXTRACT.PDLANG",
			"AddFileMem strings.tsv failed for bank=%d -> \"%s\"",
			bank, dst_full);
		modArchiveAbort(aw);
		free(tsv_text);
		sysMemFree(src_bytes);
		return -1;
	}

	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash((const u8 *)tsv_text, (size_t)tsv_size, digest);
	char hex[SHA256_HEX_SIZE + 1];
	sha256ToHex(digest, hex);
	hex[SHA256_HEX_SIZE] = '\0';
	char sidecar[SHA256_HEX_SIZE + 2];
	snprintf(sidecar, sizeof(sidecar), "%s\n", hex);
	if (modArchiveAddFileMem(aw, "strings.tsv.sha256",
	                          sidecar, (u32)strlen(sidecar)) != 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdlang: sidecar write failed for \"%s\"",
			dst_full);
	}

	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDLANG",
			"modArchiveFinish failed for \"%s\"", dst_full);
		free(tsv_text);
		sysMemFree(src_bytes);
		return -1;
	}

	free(tsv_text);
	sysMemFree(src_bytes);
	return 1;
}

/* Engine Phase 4: per-lang fan-out context. */
typedef struct {
	const char  *locale_tag;
	const char  *lang_dir;
	s32          force_rewrite;
	s32          count;
	SDL_atomic_t written;
	SDL_atomic_t skipped;
	SDL_atomic_t failed;
	SDL_atomic_t processed;
} pdlang_fanout_ctx_t;

static void s_pdlangWork(int idx, void *user)
{
	pdlang_fanout_ctx_t *c = (pdlang_fanout_ctx_t *)user;
	if (idx < 0 || idx >= c->count) return;

	/* Bank index is 1-based: idx 0 -> bank 1. */
	s32 bank = idx + 1;
	u16 file_id = g_LangFiles[bank];
	s32 r = s_emitOneLang(bank, c->locale_tag, file_id,
	                      c->lang_dir, c->force_rewrite);
	if (r > 0)       SDL_AtomicAdd(&c->written, 1);
	else if (r == 0) SDL_AtomicAdd(&c->skipped, 1);
	else             SDL_AtomicAdd(&c->failed,  1);

	int done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if ((done & 0x07) == 0 || done == c->count) {
		bootProgressUpdate(done, c->count);
	}
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

	/* For Step 3b initial ship, emit canonical English locale only.
	 * PAL/JPN extension lands as Step 5 cleanup or a follow-up
	 * worktree. The catalog ID includes the locale suffix so the
	 * follow-up's IDs don't collide with these. */
	const char *locale_tag = "en";

	pdlang_fanout_ctx_t lctx;
	memset(&lctx, 0, sizeof(lctx));
	lctx.locale_tag    = locale_tag;
	lctx.lang_dir      = lang_dir;
	lctx.force_rewrite = force_rewrite;
	lctx.count         = PDLANG_BANK_MAX;
	SDL_AtomicSet(&lctx.written,   0);
	SDL_AtomicSet(&lctx.skipped,   0);
	SDL_AtomicSet(&lctx.failed,    0);
	SDL_AtomicSet(&lctx.processed, 0);

	bootProgressUpdate(0, PDLANG_BANK_MAX);
	bootPoolForRangeBlocking(0, PDLANG_BANK_MAX, s_pdlangWork, &lctx);
	bootProgressUpdate(PDLANG_BANK_MAX, PDLANG_BANK_MAX);

	s32 written = SDL_AtomicGet(&lctx.written);
	s32 skipped = SDL_AtomicGet(&lctx.skipped);
	s32 failed  = SDL_AtomicGet(&lctx.failed);

	sysLogPrintf(LOG_NOTE,
		"romextract pdlang: written=%d skipped=%d failed=%d "
		"banks=%d locale=%s (out=%s)",
		written, skipped, failed, PDLANG_BANK_MAX, locale_tag, lang_dir);

	return written;
#endif
}
