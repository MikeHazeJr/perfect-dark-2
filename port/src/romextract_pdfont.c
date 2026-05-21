/**
 * romextract_pdfont.c -- Catalog universality pivot Step 3b
 * (2026-05-03).
 *
 * Per-asset font emitter. Walks the font ROM segments
 * (bankgothic / zurich / tahoma / numeric / handelgothic{xs,sm,md,lg}
 * / ocra{md,lg} for NTSC; fontjpn / fontjpnsingle for JPN) and
 * emits one .pdfont ZIP compound per face at
 * data/<romid>/fonts/<id>.pdfont.
 *
 * Per c3812 typed-archive repair:
 *   manifest.json        envelope + face metadata + provenance
 *   font.ini             modder-facing descriptor
 *   glyphs.pgm           decoded 8-bit grayscale glyph atlas
 *   metrics.tsv          glyph metrics and atlas coordinates
 *   kerning.tsv          13x13 kerning table
 *
 * Catalog ID convention (feedback_human_readable_ids):
 *   base:font_<facename>   e.g. base:font_handelgothicsm
 *
 * The ROM segment stores a 13x13 kerning table, per-glyph metrics,
 * and CI4 glyph pixels. Base extraction now exposes those as editable
 * TSV plus a standard PGM bitmap atlas instead of a raw data.bin.
 *
 * Server build (PD_SERVER): returns 0 immediately; font segments
 * are not loaded server-side and the disk paths are not produced.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <PR/ultratypes.h>

#include "boot_pool.h"
#include "boot_progress.h"
#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "modarchive.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "sha256.h"
#include "system.h"

#define PDFONT_OUT_DIR "fonts"

#define PDFONT_KERNING_DIM 13
#define PDFONT_RAW_CHAR_SIZE 12
#define PDFONT_GLYPH_ROW_BYTES 8
#define PDFONT_ATLAS_COLUMNS 16

/* Canonical face list. Driven by ld/pd.ld FONT(name) macros for the
 * NTSC / PAL / JPN segments. The .pdfont emitter walks this table;
 * faces whose segment is not present in the current build (e.g. JPN
 * faces in NTSC) are skipped silently (segment lookup returns 0). */
static const char *const k_FontFaces[] = {
	"bankgothic",
	"zurich",
	"tahoma",
	"numeric",
	"handelgothicxs",
	"handelgothicsm",
	"handelgothicmd",
	"handelgothiclg",
	"ocramd",
	"ocralg",
	/* JPN-only faces; harmless on NTSC/PAL where the segment lookup
	 * misses and the entry is skipped. */
	"fontjpn",
	"fontjpnsingle",
};

#define K_FONT_FACE_COUNT (sizeof(k_FontFaces) / sizeof(k_FontFaces[0]))

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
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static s32 s_fontNumChars(const char *face)
{
#if VERSION == VERSION_PAL_FINAL
	if (face &&
	    (!strcmp(face, "handelgothicsm") ||
	     !strcmp(face, "handelgothicxs") ||
	     !strcmp(face, "handelgothicmd"))) {
		return 135;
	}
#endif
	(void)face;
	return 94;
}

static void s_fontCharDisplay(u8 index, char *out, size_t out_n)
{
	if (!out || out_n == 0) return;
	if (index == '\\') {
		snprintf(out, out_n, "\\\\");
	} else if (index >= 0x21 && index <= 0x7e) {
		snprintf(out, out_n, "%c", (char)index);
	} else {
		snprintf(out, out_n, "\\x%02x", (unsigned)index);
	}
}

static void s_addMemSidecar(mod_archive_writer_t *aw, const char *inner_name,
                             const void *bytes, u32 size)
{
	if (!aw || !inner_name || !bytes || size == 0) return;
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(bytes, (size_t)size, digest);
	char hex[SHA256_HEX_SIZE + 1];
	sha256ToHex(digest, hex);
	hex[SHA256_HEX_SIZE] = '\0';
	char sidecar[SHA256_HEX_SIZE + 2];
	snprintf(sidecar, sizeof(sidecar), "%s\n", hex);
	char sidecar_name[96];
	snprintf(sidecar_name, sizeof(sidecar_name), "%s.sha256", inner_name);
	(void)modArchiveAddFileMem(aw, sidecar_name,
		sidecar, (u32)strlen(sidecar));
}

static s32 s_buildFontExports(const char *face, const u8 *src, u32 src_size,
                              u8 **out_pgm, u32 *out_pgm_size,
                              char **out_metrics, u32 *out_metrics_size,
                              char **out_kerning, u32 *out_kerning_size,
                              s32 *out_char_count)
{
	if (!src || src_size == 0 || !out_pgm || !out_pgm_size ||
	    !out_metrics || !out_metrics_size || !out_kerning ||
	    !out_kerning_size || !out_char_count) {
		return -1;
	}

	*out_pgm = NULL;
	*out_pgm_size = 0;
	*out_metrics = NULL;
	*out_metrics_size = 0;
	*out_kerning = NULL;
	*out_kerning_size = 0;
	*out_char_count = 0;

	s32 num_chars = s_fontNumChars(face);
	u32 char_offset = ((PDFONT_KERNING_DIM * PDFONT_KERNING_DIM * 4u) + 3u) & ~3u;
	u32 char_table_bytes = (u32)num_chars * PDFONT_RAW_CHAR_SIZE;
	if (src_size < char_offset + char_table_bytes) {
		return -1;
	}

	u8 max_width = 1;
	u8 max_height = 1;
	for (s32 i = 0; i < num_chars; i++) {
		const u8 *ch = src + char_offset + (u32)i * PDFONT_RAW_CHAR_SIZE;
		if (ch[3] > max_width) max_width = ch[3];
		if (ch[2] > max_height) max_height = ch[2];
	}

	u32 cell_w = (u32)max_width + 2u;
	u32 cell_h = (u32)max_height + 2u;
	u32 rows = ((u32)num_chars + PDFONT_ATLAS_COLUMNS - 1u) / PDFONT_ATLAS_COLUMNS;
	u32 atlas_w = PDFONT_ATLAS_COLUMNS * cell_w;
	u32 atlas_h = rows * cell_h;
	u32 pixel_count = atlas_w * atlas_h;

	char pgm_header[64];
	int pgm_header_len = snprintf(pgm_header, sizeof(pgm_header),
		"P5\n%u %u\n255\n", (unsigned)atlas_w, (unsigned)atlas_h);
	if (pgm_header_len <= 0 || (size_t)pgm_header_len >= sizeof(pgm_header)) {
		return -1;
	}

	u32 pgm_size = (u32)pgm_header_len + pixel_count;
	u8 *pgm = (u8 *)malloc(pgm_size);
	if (!pgm) return -1;
	memcpy(pgm, pgm_header, (size_t)pgm_header_len);
	memset(pgm + pgm_header_len, 0, pixel_count);

	u32 metrics_cap = 128u + (u32)num_chars * 96u;
	char *metrics = (char *)malloc(metrics_cap);
	if (!metrics) {
		free(pgm);
		return -1;
	}
	u32 metrics_len = 0;
	metrics_len += snprintf(metrics + metrics_len, metrics_cap - metrics_len,
		"slot\tindex_hex\tchar\tbaseline\theight\twidth\tkerning_index\tatlas_x\tatlas_y\n");

	for (s32 i = 0; i < num_chars; i++) {
		const u8 *ch = src + char_offset + (u32)i * PDFONT_RAW_CHAR_SIZE;
		u8 index = ch[0];
		s8 baseline = (s8)ch[1];
		u8 height = ch[2];
		u8 width = ch[3];
		u32 kerning_index = s_readBe32(ch + 4);
		u32 pixel_offset = s_readBe32(ch + 8);
		u32 atlas_x = ((u32)i % PDFONT_ATLAS_COLUMNS) * cell_w + 1u;
		u32 atlas_y = ((u32)i / PDFONT_ATLAS_COLUMNS) * cell_h + 1u;

		if (pixel_offset > 0 &&
		    pixel_offset + (u32)height * PDFONT_GLYPH_ROW_BYTES <= src_size) {
			const u8 *glyph = src + pixel_offset;
			for (u32 y = 0; y < height; y++) {
				for (u32 x = 0; x < width && x < 16u; x++) {
					u8 packed = glyph[y * PDFONT_GLYPH_ROW_BYTES + x / 2u];
					u8 nibble = (x & 1u) ? (packed & 0x0f) : (packed >> 4);
					pgm[pgm_header_len + (atlas_y + y) * atlas_w + atlas_x + x] =
						(u8)(nibble * 17u);
				}
			}
		}

		char display[12];
		s_fontCharDisplay(index, display, sizeof(display));
		int wrote = snprintf(metrics + metrics_len, metrics_cap - metrics_len,
			"%d\t0x%02x\t%s\t%d\t%u\t%u\t%u\t%u\t%u\n",
			i, (unsigned)index, display, (int)baseline,
			(unsigned)height, (unsigned)width, (unsigned)kerning_index,
			(unsigned)atlas_x, (unsigned)atlas_y);
		if (wrote <= 0 || (u32)wrote >= metrics_cap - metrics_len) {
			free(metrics);
			free(pgm);
			return -1;
		}
		metrics_len += (u32)wrote;
	}

	u32 kerning_cap = 64u + PDFONT_KERNING_DIM * PDFONT_KERNING_DIM * 24u;
	char *kerning = (char *)malloc(kerning_cap);
	if (!kerning) {
		free(metrics);
		free(pgm);
		return -1;
	}
	u32 kerning_len = 0;
	kerning_len += snprintf(kerning + kerning_len, kerning_cap - kerning_len,
		"previous\tcurrent\tadjust\n");
	for (u32 prev = 0; prev < PDFONT_KERNING_DIM; prev++) {
		for (u32 cur = 0; cur < PDFONT_KERNING_DIM; cur++) {
			u32 pos = (prev * PDFONT_KERNING_DIM + cur) * 4u;
			s32 adjust = (s32)s_readBe32(src + pos);
			int wrote = snprintf(kerning + kerning_len, kerning_cap - kerning_len,
				"%u\t%u\t%d\n", (unsigned)prev, (unsigned)cur, (int)adjust);
			if (wrote <= 0 || (u32)wrote >= kerning_cap - kerning_len) {
				free(kerning);
				free(metrics);
				free(pgm);
				return -1;
			}
			kerning_len += (u32)wrote;
		}
	}

	*out_pgm = pgm;
	*out_pgm_size = pgm_size;
	*out_metrics = metrics;
	*out_metrics_size = metrics_len;
	*out_kerning = kerning;
	*out_kerning_size = kerning_len;
	*out_char_count = num_chars;
	return 0;
}

/* Emit one .pdfont ZIP for a given face name. Returns 1 written, 0
 * skipped (face not in this build, or already on disk), -1 failed. */
static s32 s_emitOneFont(const char *face, const char *out_dir,
                          s32 force_rewrite)
{
	if (!face || !face[0]) return 0;

	/* Resolve the on-disk RAW segment path (Pass A wrote this). If
	 * the segment is not present in this build's romid, skip the
	 * face silently. */
	char src_rel[FS_MAXPATH];
	if (romExtractSegmentRelPath(face, src_rel, sizeof(src_rel)) <= 0) {
		return 0;
	}
	if (fsFileSize(src_rel) <= 0) {
		/* Not present for this version; not a hard error. */
		return 0;
	}

	char catalog_id[96];
	snprintf(catalog_id, sizeof(catalog_id), "base:font_%s", face);

	char filename_slug[96];
	{
		size_t i, j = 0;
		for (i = 0; catalog_id[i] && j + 1 < sizeof(filename_slug); i++) {
			filename_slug[j++] = (catalog_id[i] == ':') ? '_' : catalog_id[i];
		}
		filename_slug[j] = '\0';
	}

	char dst_rel[FS_MAXPATH];
	snprintf(dst_rel, sizeof(dst_rel),
		"%s/%s.pdfont", out_dir, filename_slug);

	if (!force_rewrite && fsFileSize(dst_rel) > 0 &&
	    s_existingArchiveHasEntry(dst_rel, "font.ini") &&
	    s_existingArchiveHasEntry(dst_rel, "glyphs.pgm") &&
	    s_existingArchiveHasEntry(dst_rel, "metrics.tsv")) return 0;

	/* Manifest. Carries segment name as provenance so the loader can
	 * round-trip the catalog ID on the universality switch. */
	u32 src_size = (u32)fsFileSize(src_rel);

	u32 src_bytes_size = 0;
	u8 *src_bytes = (u8 *)fsFileLoad(src_rel, &src_bytes_size);
	if (!src_bytes || src_bytes_size == 0) {
		if (src_bytes) sysMemFree(src_bytes);
		sysLoudFailf("EXTRACT.PDFONT",
			"source load failed for face=\"%s\"", face);
		return -1;
	}

	u8 *glyphs_pgm = NULL;
	u32 glyphs_pgm_size = 0;
	char *metrics_tsv = NULL;
	u32 metrics_tsv_size = 0;
	char *kerning_tsv = NULL;
	u32 kerning_tsv_size = 0;
	s32 char_count = 0;
	if (s_buildFontExports(face, src_bytes, src_bytes_size,
	                       &glyphs_pgm, &glyphs_pgm_size,
	                       &metrics_tsv, &metrics_tsv_size,
	                       &kerning_tsv, &kerning_tsv_size,
	                       &char_count) != 0) {
		sysMemFree(src_bytes);
		sysLoudFailf("EXTRACT.PDFONT",
			"could not build bitmap exports for face=\"%s\"", face);
		return -1;
	}

	char manifest_buf[768];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"font\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"face\": \"%s\",\n"
		"  \"format\": \"bitmap_ci4_atlas\",\n"
		"  \"glyphs\": \"glyphs.pgm\",\n"
		"  \"metrics\": \"metrics.tsv\",\n"
		"  \"kerning\": \"kerning.tsv\",\n"
		"  \"source_data_size\": %u,\n"
		"  \"character_count\": %d,\n"
		"  \"source_segment\": \"%s\"\n"
		"}\n",
		catalog_id, face, (unsigned)src_size, char_count, face);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		free(kerning_tsv);
		free(metrics_tsv);
		free(glyphs_pgm);
		sysMemFree(src_bytes);
		sysLoudFailf("EXTRACT.PDFONT",
			"manifest.json snprintf truncated for face=\"%s\"", face);
		return -1;
	}

	char ini_buf[768];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[font]\n"
		"catalog_id = %s\n"
		"face = %s\n"
		"font_format = bitmap_ci4_atlas\n"
		"font_file = glyphs.pgm\n"
		"metrics_file = metrics.tsv\n"
		"kerning_file = kerning.tsv\n"
		"source_data_size = %u\n"
		"character_count = %d\n"
		"source_segment = %s\n",
		catalog_id, face, (unsigned)src_size, char_count, face);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		free(kerning_tsv);
		free(metrics_tsv);
		free(glyphs_pgm);
		sysMemFree(src_bytes);
		sysLoudFailf("EXTRACT.PDFONT",
			"font.ini snprintf truncated for face=\"%s\"", face);
		return -1;
	}

	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		free(kerning_tsv);
		free(metrics_tsv);
		free(glyphs_pgm);
		sysMemFree(src_bytes);
		sysLoudFailf("EXTRACT.PDFONT",
			"fsFullPath empty for \"%s\"", dst_rel);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		free(kerning_tsv);
		free(metrics_tsv);
		free(glyphs_pgm);
		sysMemFree(src_bytes);
		sysLoudFailf("EXTRACT.PDFONT",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "font.ini", ini_buf, (u32)ini_len) != 0) {
		sysLoudFailf("EXTRACT.PDFONT",
			"AddFileMem font.ini failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		goto fail;
	}
	if (modArchiveAddFileMem(aw, "manifest.json",
	                          manifest_buf, (u32)manifest_len) != 0) {
		sysLoudFailf("EXTRACT.PDFONT",
			"AddFileMem manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		goto fail;
	}
	if (modArchiveAddFileMem(aw, "glyphs.pgm",
	                         glyphs_pgm, glyphs_pgm_size) != 0) {
		sysLoudFailf("EXTRACT.PDFONT",
			"AddFileMem glyphs.pgm failed for face=\"%s\" -> \"%s\"",
			face, dst_full);
		modArchiveAbort(aw);
		goto fail;
	}
	if (modArchiveAddFileMem(aw, "metrics.tsv",
	                         metrics_tsv, metrics_tsv_size) != 0) {
		sysLoudFailf("EXTRACT.PDFONT",
			"AddFileMem metrics.tsv failed for face=\"%s\" -> \"%s\"",
			face, dst_full);
		modArchiveAbort(aw);
		goto fail;
	}
	if (modArchiveAddFileMem(aw, "kerning.tsv",
	                         kerning_tsv, kerning_tsv_size) != 0) {
		sysLoudFailf("EXTRACT.PDFONT",
			"AddFileMem kerning.tsv failed for face=\"%s\" -> \"%s\"",
			face, dst_full);
		modArchiveAbort(aw);
		goto fail;
	}

	s_addMemSidecar(aw, "glyphs.pgm", glyphs_pgm, glyphs_pgm_size);
	s_addMemSidecar(aw, "metrics.tsv", metrics_tsv, metrics_tsv_size);
	s_addMemSidecar(aw, "kerning.tsv", kerning_tsv, kerning_tsv_size);

	if (modArchiveFinish(aw) != 0) {
		free(kerning_tsv);
		free(metrics_tsv);
		free(glyphs_pgm);
		sysMemFree(src_bytes);
		sysLoudFailf("EXTRACT.PDFONT",
			"modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}

	free(kerning_tsv);
	free(metrics_tsv);
	free(glyphs_pgm);
	sysMemFree(src_bytes);
	return 1;

fail:
	free(kerning_tsv);
	free(metrics_tsv);
	free(glyphs_pgm);
	sysMemFree(src_bytes);
	return -1;
}

#if !defined(PD_SERVER)
/* Engine Phase 4: per-font fan-out. */
typedef struct {
	const char  *fonts_dir;
	s32          force_rewrite;
	s32          count;
	SDL_atomic_t written;
	SDL_atomic_t skipped;
	SDL_atomic_t failed;
	SDL_atomic_t processed;
} pdfont_fanout_ctx_t;

static void s_pdfontWork(int i, void *user)
{
	pdfont_fanout_ctx_t *c = (pdfont_fanout_ctx_t *)user;
	if (i < 0 || i >= c->count) return;

	s32 r = s_emitOneFont(k_FontFaces[i], c->fonts_dir, c->force_rewrite);
	if (r > 0)       SDL_AtomicAdd(&c->written, 1);
	else if (r == 0) SDL_AtomicAdd(&c->skipped, 1);
	else             SDL_AtomicAdd(&c->failed,  1);

	int done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if (done == c->count) bootProgressUpdate(done, c->count);
}
#endif

s32 romExtractAllPdfont(s32 force_rewrite)
{
#if defined(PD_SERVER)
	(void)force_rewrite;
	return 0;
#else
	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDFONT", "fsDataDirEnsure failed");
		return -1;
	}

	char dataDirBuf[FS_MAXPATH + 1];
	char fonts_dir[FS_MAXPATH];
	snprintf(fonts_dir, sizeof(fonts_dir),
		"%s/%s", fsDataDir(dataDirBuf, sizeof(dataDirBuf)), PDFONT_OUT_DIR);
	if (!fsCreateDir(fonts_dir)) {
		sysLoudFailf("EXTRACT.PDFONT",
			"fsCreateDir(\"%s\") failed", fonts_dir);
		return -1;
	}

	pdfont_fanout_ctx_t fctx;
	memset(&fctx, 0, sizeof(fctx));
	fctx.fonts_dir     = fonts_dir;
	fctx.force_rewrite = force_rewrite;
	fctx.count         = (s32)K_FONT_FACE_COUNT;
	SDL_AtomicSet(&fctx.written,   0);
	SDL_AtomicSet(&fctx.skipped,   0);
	SDL_AtomicSet(&fctx.failed,    0);
	SDL_AtomicSet(&fctx.processed, 0);

	bootProgressUpdate(0, (s32)K_FONT_FACE_COUNT);
	bootPoolForRangeBlocking(0, (int)K_FONT_FACE_COUNT, s_pdfontWork, &fctx);
	bootProgressUpdate((s32)K_FONT_FACE_COUNT, (s32)K_FONT_FACE_COUNT);

	s32 written = SDL_AtomicGet(&fctx.written);
	s32 skipped = SDL_AtomicGet(&fctx.skipped);
	s32 failed  = SDL_AtomicGet(&fctx.failed);

	sysLogPrintf(LOG_NOTE,
		"romextract pdfont: written=%d skipped=%d failed=%d "
		"total=%zu (out=%s)",
		written, skipped, failed, K_FONT_FACE_COUNT, fonts_dir);

	return written;
#endif
}
