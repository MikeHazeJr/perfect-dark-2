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
 *   _meta/manifest.json   compatibility envelope + locale + bank metadata
 *   strings.json          editable indexed text source table
 *   _meta/*.json          shared inventory/provenance/validation/source handles
 *   _meta/*.sha256        public-file SHA-256 sidecars
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
 * Locale scope: all seven active-name-table variants when their native codec
 * is Latin-1. JPN-final's packed Japanese J source/glyph codec remains open.
 *
 * The emitted JSON is decoded from the RAW pre-preprocess file as
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
#include "lib/rzip.h"
#include "asset_archive_writer.h"
#include "modarchive.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "lang_source.h"
#include "sha256.h"
#include "system.h"
#include "versioninfo.h"

#define PDLANG_OUT_DIR "lang"
#define PDLANG_EXTRACT_VERSION "strings_json_rzip_v3_regional_source_hash"
#define PDLANG_FAST_CACHE_KIND "pdlang_strings_json_rzip_v3_regional_source_hash"

/* Walk range. g_LangFiles[] is sized 69 in src/game/lang.c (bank 0
 * is a sentinel zero entry, banks 1..68 carry real files). The
 * LANGBANK_* enum tops out at 0x44 = 68. */
#define PDLANG_BANK_MAX 68
#define PDLANG_LOCALE_COUNT 7

#if !defined(PD_SERVER)

extern u16 g_LangFiles[];

typedef struct {
	const char *locale;
	const char *source_suffix;
} pdlang_locale_t;

static const pdlang_locale_t s_locales[PDLANG_LOCALE_COUNT] = {
	{ "en", "E" }, { "ja", "J" }, { "en-GB", "P" }, { "fr", "_str_f" },
	{ "de", "_str_g" }, { "it", "_str_i" }, { "es", "_str_s" }
};

static s32 s_extractBankName(const char *file_sym, char *out, size_t out_n);

/* Resolve through the active ROM file-name table. Enum adjacency is not
 * sufficient evidence that a regional file belongs to this ROM. */
static s32 s_resolveLocaleFile(s32 bank, s32 locale_index, char *bank_name,
	size_t bank_name_size)
{
	const s32 english = g_LangFiles[bank];
	const char *canonical = romdataFileGetName(english);
	const char *symbol = loaderEnumNameForFileEnum(english);
	char stem[128];
	char candidate[160];
	s32 found = -1;
	size_t len;

	if (!canonical || !(len = strlen(canonical)) || canonical[len - 1] != 'E'
			|| len >= sizeof(stem)) return -2;
	if (!s_extractBankName(symbol, bank_name, bank_name_size)) return -2;
	memcpy(stem, canonical, len - 1);
	stem[len - 1] = '\0';
	if (locale_index == 0) return english;
	for (s32 spelling = 0; spelling < 2; spelling++) {
		/* The active table may name compiled _str_*Z files with or without
		 * the trailing Z. Ambiguous aliases must not select by order. */
		if (spelling && s_locales[locale_index].source_suffix[0] != '_') break;
		snprintf(candidate, sizeof(candidate), "%s%s%s", stem,
			s_locales[locale_index].source_suffix, spelling ? "Z" : "");
		s32 id = romdataFileGetNumForName(candidate);
		if (id < 0) continue;
		if (found >= 0 && found != id) return -2;
		found = id;
	}
	return found;
}

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

static s32 s_existingArchiveEntryContains(const char *relpath, const char *entry,
	const char *needle)
{
	if (!needle || !needle[0]) return 1;

	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return 0;

	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;

	s32 idx = modArchiveFindEntry(arc, entry);
	if (idx < 0) {
		modArchiveClose(arc);
		return 0;
	}

	u32 size = 0;
	void *bytes = modArchiveExtractAlloc(arc, idx, &size);
	s32 found = 0;
	if (bytes) {
		const char *hay = (const char *)bytes;
		size_t nlen = strlen(needle);
		for (u32 i = 0; i + nlen <= size; i++) {
			if (memcmp(hay + i, needle, nlen) == 0) {
				found = 1;
				break;
			}
		}
		free(bytes);
	}

	modArchiveClose(arc);
	return found;
}

static s32 s_prepareLangSource(const u8 *src, u32 src_size,
                               u8 **out_bytes, u32 *out_size,
                               s32 *out_was_rzip)
{
	*out_bytes = NULL;
	*out_size = 0;
	*out_was_rzip = 0;

	if (!src || src_size == 0) {
		return 0;
	}

	if (src_size >= 5 && rzipIs1173((void *)src)) {
		u32 inflated_size =
			((u32)src[2] << 16) | ((u32)src[3] << 8) | (u32)src[4];
		if (inflated_size == 0) {
			return 0;
		}

		u8 *inflated = (u8 *)calloc(1, inflated_size + 16u);
		if (!inflated) {
			return 0;
		}

		s32 actual_size = rzipInflate((void *)src, inflated, NULL);
		if (actual_size <= 0 || (u32)actual_size > inflated_size) {
			free(inflated);
			return 0;
		}

		*out_bytes = inflated;
		*out_size = (u32)actual_size;
		*out_was_rzip = 1;
		return 1;
	}

	*out_bytes = (u8 *)src;
	*out_size = src_size;
	return 1;
}

static s32 s_buildStringsJson(const u8 *src, u32 src_size, const char *locale,
                              char **out_text, u32 *out_size,
                              u32 *out_count)
{
	const char *error = NULL;
	s32 ok = langSourceExportNative(src, src_size, -1,
		langSourceEncodingForLocale(VERSION_ROMID, locale),
		out_text, out_size, out_count, &error);
	if (!ok) {
		sysLogPrintf(LOG_WARNING, "EXTRACT.PDLANG: native source rejected: %s",
			error ? error : "invalid language source");
	}
	return ok;
}

/* Emit one .pdlang ZIP for (bank, locale). Returns 1 written, 0
 * skipped (file not on disk for this build), -1 failed. */
static s32 s_emitOneLang(s32 bank, const char *bank_name, const char *locale_tag,
                         u16 file_id, const char *out_dir,
                         s32 force_rewrite)
{
	if (file_id == 0) return -1;

	char src_rel[FS_MAXPATH];
	if (romExtractRelPathForFilenum((s32)file_id, src_rel, sizeof(src_rel)) <= 0) {
		return -1;
	}
	if (fsFileSize(src_rel) <= 0) {
		sysLoudFailf("EXTRACT.PDLANG", "mapped bank=%d locale=%s has no extracted source %s",
			bank, locale_tag, src_rel);
		return -1;
	}

	const char *file_sym = loaderEnumNameForFileEnum((s32)file_id);

	char catalog_id[128];
	const char *id_locale = strcmp(locale_tag, "en-GB") == 0 ? "en_gb" : locale_tag;
	snprintf(catalog_id, sizeof(catalog_id),
		"base:lang_%s_%s", bank_name, id_locale);

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

	u32 src_size = (u32)fsFileSize(src_rel);
	u32 src_bytes_size = 0;
	void *src_bytes = fsFileLoad(src_rel, &src_bytes_size);
	if (!src_bytes || src_bytes_size == 0) {
		sysLoudFailf("EXTRACT.PDLANG",
			"fsFileLoad failed for bank=%d source \"%s\"", bank, src_rel);
		if (src_bytes) sysMemFree(src_bytes);
		return -1;
	}
	u8 source_digest[SHA256_DIGEST_SIZE];
	char source_hash[SHA256_HEX_SIZE];
	sha256Hash(src_bytes, src_bytes_size, source_digest);
	sha256ToHex(source_digest, source_hash);
	char hash_field[SHA256_HEX_SIZE + 32];
	snprintf(hash_field, sizeof(hash_field), "source_sha256 = %s", source_hash);
	if (!force_rewrite && fsFileSize(dst_rel) > 0 &&
	    s_existingArchiveHasEntry(dst_rel, "lang.ini") &&
	    s_existingArchiveHasEntry(dst_rel, "_meta/manifest.json") &&
	    s_existingArchiveHasEntry(dst_rel, "strings.json") &&
	    s_existingArchiveEntryContains(dst_rel, "lang.ini",
		    "extract_version = " PDLANG_EXTRACT_VERSION) &&
	    s_existingArchiveEntryContains(dst_rel, "lang.ini", hash_field)) {
		sysMemFree(src_bytes);
		return 0;
	}

	u8 *lang_source = NULL;
	u32 lang_source_size = 0;
	s32 lang_source_was_rzip = 0;
	if (!s_prepareLangSource((const u8 *)src_bytes, src_bytes_size,
	                         &lang_source, &lang_source_size,
	                         &lang_source_was_rzip)) {
		sysLoudFailf("EXTRACT.PDLANG",
			"could not prepare lang source for bank=%d source \"%s\"",
			bank, src_rel);
		sysMemFree(src_bytes);
		return -1;
	}

	char *json_text = NULL;
	u32 json_size = 0;
	u32 string_count = 0;
	if (!s_buildStringsJson(lang_source, lang_source_size, locale_tag,
	                        &json_text, &json_size, &string_count)) {
		sysLoudFailf("EXTRACT.PDLANG",
			"could not build strings.json for bank=%d source \"%s\"",
			bank, src_rel);
		if (lang_source_was_rzip) free(lang_source);
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
		"  \"data\": \"strings.json\",\n"
		"  \"data_size\": %u,\n"
		"  \"source_data_size\": %u,\n"
		"  \"decoded_source_size\": %u,\n"
		"  \"string_count\": %u,\n"
		"  \"extract_version\": \"%s\",\n"
		"  \"source_bank\": %d,\n"
		"  \"source_filenum\": %u,\n"
		"  \"source_sha256\": \"%s\",\n"
		"  \"source_symbol\": \"%s\"\n"
		"}\n",
		catalog_id, locale_tag, category,
		(unsigned)json_size,
		(unsigned)src_size,
		(unsigned)lang_source_size,
		(unsigned)string_count,
		PDLANG_EXTRACT_VERSION,
		bank, (unsigned)file_id, source_hash,
		file_sym ? file_sym : "");
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDLANG",
			"manifest.json snprintf truncated for bank=%d", bank);
		if (lang_source_was_rzip) free(lang_source);
		free(json_text);
		sysMemFree(src_bytes);
		return -1;
	}

	char ini_buf[768];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[lang]\n"
		"catalog_id = %s\n"
		"locale = %s\n"
		"category = %s\n"
		"strings_file = strings.json\n"
		"data_size = %u\n"
		"source_data_size = %u\n"
		"decoded_source_size = %u\n"
		"string_count = %u\n"
		"extract_version = %s\n"
		"source_bank = %d\n"
		"source_sha256 = %s\n"
		"source_symbol = %s\n",
		catalog_id, locale_tag, category,
		(unsigned)json_size,
		(unsigned)src_size,
		(unsigned)lang_source_size,
		(unsigned)string_count,
		PDLANG_EXTRACT_VERSION,
		bank, source_hash,
		file_sym ? file_sym : "");
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		sysLoudFailf("EXTRACT.PDLANG",
			"lang.ini snprintf truncated for bank=%d", bank);
		if (lang_source_was_rzip) free(lang_source);
		free(json_text);
		sysMemFree(src_bytes);
		return -1;
	}

	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		sysLoudFailf("EXTRACT.PDLANG",
			"fsFullPath empty for \"%s\"", dst_rel);
		if (lang_source_was_rzip) free(lang_source);
		free(json_text);
		sysMemFree(src_bytes);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDLANG",
			"modArchiveBegin failed for \"%s\"", dst_full);
		if (lang_source_was_rzip) free(lang_source);
		free(json_text);
		sysMemFree(src_bytes);
		return -1;
	}

	asset_archive_writer_t asset_writer;
	if (assetArchiveWriterInit(&asset_writer, aw, "lang", catalog_id) !=
			MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDLANG",
			"assetArchiveWriterInit failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		if (lang_source_was_rzip) free(lang_source);
		free(json_text);
		sysMemFree(src_bytes);
		return -1;
	}
	assetArchiveWriterSetProvenance(&asset_writer, "romextract_pdlang",
		src_rel, (s32)file_id, file_sym ? file_sym : "");

	if (assetArchiveWriterAddDescriptor(&asset_writer, "lang.ini",
			ini_buf, (u32)ini_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDLANG",
			"AddFileMem lang.ini failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		if (lang_source_was_rzip) free(lang_source);
		free(json_text);
		sysMemFree(src_bytes);
		return -1;
	}
	if (assetArchiveWriterAddManifestJson(&asset_writer,
			manifest_buf, (u32)manifest_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDLANG",
			"AddFileMem _meta/manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		if (lang_source_was_rzip) free(lang_source);
		free(json_text);
		sysMemFree(src_bytes);
		return -1;
	}

	if (assetArchiveWriterAddPublicMem(&asset_writer, "strings.json",
			json_text, json_size, "strings") != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDLANG",
			"AddFileMem strings.json failed for bank=%d -> \"%s\"",
			bank, dst_full);
		modArchiveAbort(aw);
		if (lang_source_was_rzip) free(lang_source);
		free(json_text);
		sysMemFree(src_bytes);
		return -1;
	}

	if (assetArchiveWriterFinishMetadata(&asset_writer) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDLANG",
			"assetArchiveWriterFinishMetadata failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		if (lang_source_was_rzip) free(lang_source);
		free(json_text);
		sysMemFree(src_bytes);
		return -1;
	}

	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDLANG",
			"modArchiveFinish failed for \"%s\"", dst_full);
		if (lang_source_was_rzip) free(lang_source);
		free(json_text);
		sysMemFree(src_bytes);
		return -1;
	}

	if (lang_source_was_rzip) free(lang_source);
	free(json_text);
	sysMemFree(src_bytes);
	return 1;
}

/* Engine Phase 4: per-lang fan-out context. */
typedef struct {
	const char  *lang_dir;
	s32          force_rewrite;
	s32          count;
	SDL_atomic_t written;
	SDL_atomic_t skipped;
	SDL_atomic_t failed;
	SDL_atomic_t unsupported_japanese;
	SDL_atomic_t processed;
} pdlang_fanout_ctx_t;

static void s_pdlangWork(int idx, void *user)
{
	pdlang_fanout_ctx_t *c = (pdlang_fanout_ctx_t *)user;
	if (idx < 0 || idx >= c->count) return;

	s32 bank = idx / PDLANG_LOCALE_COUNT + 1;
	s32 locale_index = idx % PDLANG_LOCALE_COUNT;
	char bank_name[64] = {0};
	s32 file_id = s_resolveLocaleFile(bank, locale_index, bank_name,
		sizeof(bank_name));
	s32 unsupported = file_id >= 0 && langSourceEncodingForLocale(VERSION_ROMID,
		s_locales[locale_index].locale) != LANG_SOURCE_LATIN1;
	s32 r = unsupported || file_id == -1 ? 0 : file_id < 0 ? -1 :
		s_emitOneLang(bank, bank_name, s_locales[locale_index].locale,
			(u16)file_id, c->lang_dir, c->force_rewrite);
	if (unsupported) SDL_AtomicAdd(&c->unsupported_japanese, 1);
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

	/* Source hashes must be checked on each pass; a directory-only output
	 * fingerprint cannot detect a changed extracted input. */

	/* In-place cache-kind bump (B-943): force a one-time per-file rewrite when
	 * the stored kind differs from the current one (no-op on clean install or
	 * unchanged kind). See romExtractPdFastCacheKindMismatch. */
	s32 effective_force = force_rewrite |
		romExtractPdFastCacheKindMismatch(PDLANG_FAST_CACHE_KIND, lang_dir);

	pdlang_fanout_ctx_t lctx;
	memset(&lctx, 0, sizeof(lctx));
	lctx.lang_dir      = lang_dir;
	lctx.force_rewrite = effective_force;
	lctx.count         = PDLANG_BANK_MAX * PDLANG_LOCALE_COUNT;
	SDL_AtomicSet(&lctx.written,   0);
	SDL_AtomicSet(&lctx.skipped,   0);
	SDL_AtomicSet(&lctx.failed,    0);
	SDL_AtomicSet(&lctx.unsupported_japanese, 0);
	SDL_AtomicSet(&lctx.processed, 0);

	bootProgressUpdate(0, lctx.count);
	bootPoolForRangeBlocking(0, lctx.count, s_pdlangWork, &lctx);
	bootProgressUpdate(lctx.count, lctx.count);

	s32 written = SDL_AtomicGet(&lctx.written);
	s32 skipped = SDL_AtomicGet(&lctx.skipped);
	s32 failed  = SDL_AtomicGet(&lctx.failed);
	s32 unsupported_japanese = SDL_AtomicGet(&lctx.unsupported_japanese);

	sysLogPrintf(LOG_NOTE,
		"romextract pdlang: written=%d skipped=%d failed=%d "
		"banks=%d source_locales=%d unsupported_japanese=%d (out=%s)",
		written, skipped, failed, PDLANG_BANK_MAX, PDLANG_LOCALE_COUNT,
		unsupported_japanese, lang_dir);

	if (failed == 0) {
		romExtractPdFastCacheWrite(PDLANG_FAST_CACHE_KIND, lang_dir, ".pdlang");
	}

	return failed ? -1 : written;
#endif
}
