/**
 * romextract_pdfont.c -- Catalog universality pivot Step 3b
 * (2026-05-03), repaired for consumption under c3849 Wave 3
 * (2026-06-10).
 *
 * Per-asset font emitter. Walks the six struct-font ROM segments
 * (fonttahoma / fontnumeric / fonthandelgothic{xs,sm,md,lg}) and
 * emits one .pdfont ZIP compound per face at
 * data/<romid>/fonts/<id>.pdfont.
 *
 * The JPN glyph banks (fontjpn / fontjpnsingle / fontjpnmulti) are
 * raw codepoint-indexed pixel arrays with no kerning/char-table
 * layout; they have no valid struct-font .pdfont shape and are
 * deliberately NOT in the face table (lang.c keeps them on the
 * segment path). bankgothic / zurich / ocramd / ocralg have no ROM
 * segments in the port at all (romdata.c ROMSEG_LIST).
 *
 * Per c3812 typed-archive repair:
 *   _meta/manifest.json  envelope + face metadata + provenance
 *   _meta/*.json         shared inventory/provenance/validation/source handles
 *   font.ini             modder-facing descriptor
 *   glyphs.pgm           decoded 8-bit grayscale glyph atlas
 *   font.metrics.json    glyph metrics, atlas coordinates, and kerning
 *   _meta/*.sha256       public-file SHA-256 sidecars
 *
 * Catalog ID convention (feedback_human_readable_ids):
 *   base:font_<facename>   e.g. base:font_handelgothicsm
 *
 * IMPORTANT (c3849 Wave 3): segs/font*.bin on disk is the
 * POST-preprocess PC-native segment image (romdataInitSegment runs
 * preprocessFont before romExtractAllSegments dumps seg->data), per
 * port/src/preprocess/segfonts.c:
 *   - little-endian s32 kerning[13*13] at offset 0
 *   - struct fontchar[num_chars] at PD_ALIGN(676, sizeof(uintptr_t))
 *     = offsetof(struct font, chars) = 680 (16-byte records on x86_64)
 *   - fontchar.pixeldata holds a buffer-relative offset to CI4 glyph
 *     pixels with a fixed 8-byte row stride
 * The exports are losslessly recompiled at runtime by
 * port/src/fontcatalog.c (textLoadFont consumes them catalog-first).
 *
 * Server build (PD_SERVER): returns 0 immediately; font segments
 * are not loaded server-side and the disk paths are not produced.
 */

#include <stddef.h>
#include <stdint.h>
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
#include "asset_archive_writer.h"
#include "modarchive.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "system.h"

#define PDFONT_OUT_DIR "fonts"
/* v2 (c3849 Wave 3): v1 misparsed the PC-native segment as raw N64
 * big-endian bytes AND resolved segments by face name (always missed),
 * so every v1 archive on disk is absent or garbage. The kind bump plus
 * the manifest pd_schema_version=2 early-skip gate below force stale
 * installs to regenerate. */
#define PDFONT_FAST_CACHE_KIND "pdfont_metrics_json_v3_glyphwidthplus1"
#define PDFONT_SCHEMA_VERSION 2

#define PDFONT_KERNING_DIM 13
#define PDFONT_GLYPH_ROW_BYTES 8
#define PDFONT_ATLAS_COLUMNS 16

/* The parse below reads the build's own struct fontchar straight out of
 * the post-preprocess segment image; pin the 16-byte record contract. */
_Static_assert(sizeof(struct fontchar) == 16,
	"PC-native font segment parse expects 16-byte fontchar records");

/* Canonical {face, segname} table (c3849 Wave 3). Pass B dumps segments
 * under their ROMSEG_LIST names (segs/fonthandelgothicsm.bin), NOT face
 * names, so each face carries its segment name explicitly. Faces whose
 * segment is not present in the current build are skipped silently
 * (segment lookup returns 0). bankgothic/zurich/ocramd/ocralg have no
 * segments; fontjpn/fontjpnsingle/fontjpnmulti are raw glyph banks with
 * the wrong shape -- all deliberately absent. */
static const struct {
	const char *face;
	const char *segname;
} k_FontFaces[] = {
	{ "tahoma",         "fonttahoma"         },
	{ "numeric",        "fontnumeric"        },
	{ "handelgothicxs", "fonthandelgothicxs" },
	{ "handelgothicsm", "fonthandelgothicsm" },
	{ "handelgothicmd", "fonthandelgothicmd" },
	{ "handelgothiclg", "fonthandelgothiclg" },
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

/* Schema gate for the early-skip (pdlang precedent): an existing archive
 * only counts as current when its manifest carries the v2 schema marker,
 * so pre-repair installs regenerate instead of fossilizing garbage. */
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

static void s_fontCharDisplay(u32 index, char *out, size_t out_n)
{
	if (!out || out_n == 0) return;
	if (index == '\\') {
		snprintf(out, out_n, "\\\\");
	} else if (index >= 0x21 && index <= 0x7e) {
		snprintf(out, out_n, "%c", (char)index);
	} else if (index <= 0xff) {
		snprintf(out, out_n, "\\x%02x", (unsigned)index);
	} else {
		/* JPN builds carry u16 glyph indices. */
		snprintf(out, out_n, "\\x%04x", (unsigned)index);
	}
}

static s32 s_appendJsonString(char *dst, u32 dst_cap, const char *src)
{
	u32 w = 0;
	if (!dst || dst_cap < 3) return -1;
	dst[w++] = '"';
	for (const unsigned char *p = (const unsigned char *)src;
			p && *p && w + 2u < dst_cap; p++) {
		if (*p == '"' || *p == '\\') {
			if (w + 2u >= dst_cap) return -1;
			dst[w++] = '\\';
			dst[w++] = (char)*p;
		} else if (*p == '\n') {
			if (w + 2u >= dst_cap) return -1;
			dst[w++] = '\\';
			dst[w++] = 'n';
		} else if (*p == '\t') {
			if (w + 2u >= dst_cap) return -1;
			dst[w++] = '\\';
			dst[w++] = 't';
		} else if (*p >= 0x20 && *p < 0x7f) {
			dst[w++] = (char)*p;
		} else {
			static const char hex[] = "0123456789abcdef";
			if (w + 6u >= dst_cap) return -1;
			dst[w++] = '\\';
			dst[w++] = 'u';
			dst[w++] = '0';
			dst[w++] = '0';
			dst[w++] = hex[(*p >> 4) & 0x0f];
			dst[w++] = hex[*p & 0x0f];
		}
	}
	if (w + 1u >= dst_cap) return -1;
	dst[w++] = '"';
	if (w < dst_cap) dst[w] = '\0';
	return (s32)w;
}

static s32 s_buildFontExports(const char *face, const u8 *src, u32 src_size,
                              u8 **out_pgm, u32 *out_pgm_size,
                              char **out_metrics_json,
                              u32 *out_metrics_json_size,
                              s32 *out_char_count)
{
	if (!src || src_size == 0 || !out_pgm || !out_pgm_size ||
	    !out_metrics_json || !out_metrics_json_size ||
	    !out_char_count) {
		return -1;
	}

	*out_pgm = NULL;
	*out_pgm_size = 0;
	*out_metrics_json = NULL;
	*out_metrics_json_size = 0;
	*out_char_count = 0;

	s32 num_chars = s_fontNumChars(face);

	/* POST-preprocess PC-native segment layout (segfonts.c): the .bin on
	 * disk was dumped AFTER preprocessFont ran, so it is little-endian
	 * native data, not raw N64 big-endian. Kerning s32[169] at offset 0;
	 * struct fontchar records at PD_ALIGN(676, sizeof(uintptr_t)) =
	 * offsetof(struct font, chars) = 680; fontchar.pixeldata holds a
	 * buffer-relative offset to CI4 pixels (8-byte row stride). */
	u32 char_offset = (u32)offsetof(struct font, chars);
	u32 char_table_bytes = (u32)num_chars * (u32)sizeof(struct fontchar);
	if (src_size < char_offset + char_table_bytes) {
		return -1;
	}

	const s32 *src_kerning = (const s32 *)(const void *)src;
	const struct fontchar *src_chars =
		(const struct fontchar *)(const void *)(src + char_offset);

	u8 max_width = 1;
	u8 max_height = 1;
	for (s32 i = 0; i < num_chars; i++) {
		const struct fontchar *ch = &src_chars[i];
		if (ch->width > max_width) max_width = ch->width;
		if (ch->height > max_height) max_height = ch->height;
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

	u32 metrics_cap = 1024u + (u32)num_chars * 192u +
		PDFONT_KERNING_DIM * PDFONT_KERNING_DIM * 72u;
	char *metrics_json = (char *)malloc(metrics_cap);
	if (!metrics_json) {
		free(pgm);
		return -1;
	}
	u32 metrics_len = 0;
	int metrics_header_len = snprintf(metrics_json + metrics_len,
		metrics_cap - metrics_len,
		"{\n"
		"  \"pd_kind\": \"font_metrics\",\n"
		"  \"pd_schema_version\": %d,\n"
		"  \"glyphs_file\": \"glyphs.pgm\",\n"
		"  \"atlas\": {\n"
		"    \"format\": \"pgm_p5_grayscale\",\n"
		"    \"columns\": %u,\n"
		"    \"cell_width\": %u,\n"
		"    \"cell_height\": %u,\n"
		"    \"width\": %u,\n"
		"    \"height\": %u\n"
		"  },\n"
		"  \"glyphs\": [\n",
		PDFONT_SCHEMA_VERSION,
		(unsigned)PDFONT_ATLAS_COLUMNS,
		(unsigned)cell_w,
		(unsigned)cell_h,
		(unsigned)atlas_w,
		(unsigned)atlas_h);
	if (metrics_header_len <= 0 ||
			(u32)metrics_header_len >= metrics_cap - metrics_len) {
		free(metrics_json);
		free(pgm);
		return -1;
	}
	metrics_len += (u32)metrics_header_len;

	for (s32 i = 0; i < num_chars; i++) {
		const struct fontchar *ch = &src_chars[i];
		u32 index = (u32)ch->index;
		s8 baseline = ch->baseline;
		u8 height = ch->height;
		u8 width = ch->width;
		u32 kerning_index = (u32)ch->kerningindex;
		/* Buffer-relative offset written by preprocessFont (0 = none). */
		u32 pixel_offset = (u32)(uintptr_t)ch->pixeldata;
		u32 atlas_x = ((u32)i % PDFONT_ATLAS_COLUMNS) * cell_w + 1u;
		u32 atlas_y = ((u32)i / PDFONT_ATLAS_COLUMNS) * cell_h + 1u;

		if (pixel_offset > 0 &&
		    pixel_offset + (u32)height * PDFONT_GLYPH_ROW_BYTES <= src_size) {
			const u8 *glyph = src + pixel_offset;
			/* x <= width (not < width): the runtime text renderer samples
			 * width+1 texels (game_1531a0.c gDPSetTextureImage + the
			 * (width+1)<<6 texcoord), so a glyph whose ink reaches column
			 * `width` -- overhang past the advance, common in tight fonts
			 * like tahoma where 'O'/'D'/'B'/'R' lose their right edge --
			 * would otherwise drop its rightmost column. Capped at the
			 * 16-col (8-byte CI4) row stride; the +1 fits the cell padding. */
			for (u32 y = 0; y < height; y++) {
				for (u32 x = 0; x <= width && x < 16u; x++) {
					u8 packed = glyph[y * PDFONT_GLYPH_ROW_BYTES + x / 2u];
					u8 nibble = (x & 1u) ? (packed & 0x0f) : (packed >> 4);
					pgm[pgm_header_len + (atlas_y + y) * atlas_w + atlas_x + x] =
						(u8)(nibble * 17u);
				}
			}
		}

		char display[12];
		s_fontCharDisplay(index, display, sizeof(display));
		int wrote = snprintf(metrics_json + metrics_len,
			metrics_cap - metrics_len,
			"    { \"slot\": %d, \"index\": %u, \"index_hex\": \"0x%02x\", \"char\": ",
			i, (unsigned)index, (unsigned)index);
		if (wrote <= 0 || (u32)wrote >= metrics_cap - metrics_len) {
			free(metrics_json);
			free(pgm);
			return -1;
		}
		metrics_len += (u32)wrote;
		s32 string_wrote = s_appendJsonString(metrics_json + metrics_len,
			metrics_cap - metrics_len, display);
		if (string_wrote <= 0) {
			free(metrics_json);
			free(pgm);
			return -1;
		}
		metrics_len += (u32)string_wrote;
		wrote = snprintf(metrics_json + metrics_len, metrics_cap - metrics_len,
			", \"baseline\": %d, \"height\": %u, \"width\": %u, \"kerning_index\": %u, \"atlas_x\": %u, \"atlas_y\": %u }%s\n",
			(int)baseline,
			(unsigned)height, (unsigned)width, (unsigned)kerning_index,
			(unsigned)atlas_x, (unsigned)atlas_y,
			(i + 1 < num_chars) ? "," : "");
		if (wrote <= 0 || (u32)wrote >= metrics_cap - metrics_len) {
			free(metrics_json);
			free(pgm);
			return -1;
		}
		metrics_len += (u32)wrote;
	}

	int wrote_header = snprintf(metrics_json + metrics_len,
		metrics_cap - metrics_len,
		"  ],\n"
		"  \"kerning\": [\n");
	if (wrote_header <= 0 || (u32)wrote_header >= metrics_cap - metrics_len) {
		free(metrics_json);
		free(pgm);
		return -1;
	}
	metrics_len += (u32)wrote_header;
	for (u32 prev = 0; prev < PDFONT_KERNING_DIM; prev++) {
		for (u32 cur = 0; cur < PDFONT_KERNING_DIM; cur++) {
			u32 index = prev * PDFONT_KERNING_DIM + cur;
			s32 adjust = src_kerning[index];
			int wrote = snprintf(metrics_json + metrics_len,
				metrics_cap - metrics_len,
				"    { \"previous\": %u, \"current\": %u, \"adjust\": %d }%s\n",
				(unsigned)prev, (unsigned)cur, (int)adjust,
				(index + 1u < PDFONT_KERNING_DIM * PDFONT_KERNING_DIM) ? "," : "");
			if (wrote <= 0 || (u32)wrote >= metrics_cap - metrics_len) {
				free(metrics_json);
				free(pgm);
				return -1;
			}
			metrics_len += (u32)wrote;
		}
	}
	int wrote_footer = snprintf(metrics_json + metrics_len,
		metrics_cap - metrics_len,
		"  ]\n"
		"}\n");
	if (wrote_footer <= 0 || (u32)wrote_footer >= metrics_cap - metrics_len) {
		free(metrics_json);
		free(pgm);
		return -1;
	}
	metrics_len += (u32)wrote_footer;

	*out_pgm = pgm;
	*out_pgm_size = pgm_size;
	*out_metrics_json = metrics_json;
	*out_metrics_json_size = metrics_len;
	*out_char_count = num_chars;
	return 0;
}

/* Emit one .pdfont ZIP for a given {face, segname} pair. Returns 1
 * written, 0 skipped (segment not in this build, or already on disk),
 * -1 failed. */
static s32 s_emitOneFont(const char *face, const char *segname,
                          const char *out_dir, s32 force_rewrite)
{
	if (!face || !face[0] || !segname || !segname[0]) return 0;

	/* Resolve the on-disk POST-preprocess segment path (Pass B wrote
	 * this under the ROMSEG_LIST segment name, e.g.
	 * segs/fonthandelgothicsm.bin). If the segment is not present in
	 * this build's romid, skip the face silently. */
	char src_rel[FS_MAXPATH];
	if (romExtractSegmentRelPath(segname, src_rel, sizeof(src_rel)) <= 0) {
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
	    s_existingArchiveHasEntry(dst_rel, "_meta/manifest.json") &&
	    s_existingArchiveHasEntry(dst_rel, "glyphs.pgm") &&
	    s_existingArchiveHasEntry(dst_rel, "font.metrics.json") &&
	    s_existingArchiveEntryContains(dst_rel, "_meta/manifest.json",
		    "\"pd_schema_version\": 2")) return 0;

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
	char *metrics_json = NULL;
	u32 metrics_json_size = 0;
	s32 char_count = 0;
	if (s_buildFontExports(face, src_bytes, src_bytes_size,
	                       &glyphs_pgm, &glyphs_pgm_size,
	                       &metrics_json, &metrics_json_size,
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
		"  \"pd_schema_version\": %d,\n"
		"  \"id\": \"%s\",\n"
		"  \"face\": \"%s\",\n"
		"  \"format\": \"bitmap_ci4_atlas\",\n"
		"  \"glyphs\": \"glyphs.pgm\",\n"
		"  \"metrics\": \"font.metrics.json\",\n"
		"  \"source_data_size\": %u,\n"
		"  \"character_count\": %d,\n"
		"  \"source_segment\": \"%s\"\n"
		"}\n",
		PDFONT_SCHEMA_VERSION, catalog_id, face, (unsigned)src_size,
		char_count, segname);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		free(metrics_json);
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
		"pd_schema_version = %d\n"
		"font_format = bitmap_ci4_atlas\n"
		"font_file = glyphs.pgm\n"
		"metrics_file = font.metrics.json\n"
		"source_data_size = %u\n"
		"character_count = %d\n"
		"source_segment = %s\n",
		catalog_id, face, PDFONT_SCHEMA_VERSION, (unsigned)src_size,
		char_count, segname);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		free(metrics_json);
		free(glyphs_pgm);
		sysMemFree(src_bytes);
		sysLoudFailf("EXTRACT.PDFONT",
			"font.ini snprintf truncated for face=\"%s\"", face);
		return -1;
	}

	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		free(metrics_json);
		free(glyphs_pgm);
		sysMemFree(src_bytes);
		sysLoudFailf("EXTRACT.PDFONT",
			"fsFullPath empty for \"%s\"", dst_rel);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		free(metrics_json);
		free(glyphs_pgm);
		sysMemFree(src_bytes);
		sysLoudFailf("EXTRACT.PDFONT",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}

	asset_archive_writer_t asset_writer;
	if (assetArchiveWriterInit(&asset_writer, aw, "font", catalog_id) !=
			MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDFONT",
			"assetArchiveWriterInit failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		goto fail;
	}
	assetArchiveWriterSetProvenance(&asset_writer, "romextract_pdfont",
		src_rel, -1, segname);

	if (assetArchiveWriterAddDescriptor(&asset_writer, "font.ini",
			ini_buf, (u32)ini_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDFONT",
			"AddFileMem font.ini failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		goto fail;
	}
	if (assetArchiveWriterAddManifestJson(&asset_writer,
			manifest_buf, (u32)manifest_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDFONT",
			"AddFileMem _meta/manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		goto fail;
	}
	if (assetArchiveWriterAddPublicMem(&asset_writer, "glyphs.pgm",
			glyphs_pgm, glyphs_pgm_size, "glyphs") != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDFONT",
			"AddFileMem glyphs.pgm failed for face=\"%s\" -> \"%s\"",
			face, dst_full);
		modArchiveAbort(aw);
		goto fail;
	}
	if (assetArchiveWriterAddPublicMem(&asset_writer, "font.metrics.json",
			metrics_json, metrics_json_size, "metrics") != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDFONT",
			"AddFileMem font.metrics.json failed for face=\"%s\" -> \"%s\"",
			face, dst_full);
		modArchiveAbort(aw);
		goto fail;
	}

	if (assetArchiveWriterFinishMetadata(&asset_writer) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDFONT",
			"assetArchiveWriterFinishMetadata failed for \"%s\"",
			dst_full);
		modArchiveAbort(aw);
		goto fail;
	}

	if (modArchiveFinish(aw) != 0) {
		free(metrics_json);
		free(glyphs_pgm);
		sysMemFree(src_bytes);
		sysLoudFailf("EXTRACT.PDFONT",
			"modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}

	free(metrics_json);
	free(glyphs_pgm);
	sysMemFree(src_bytes);
	return 1;

fail:
	free(metrics_json);
	free(glyphs_pgm);
	sysMemFree(src_bytes);
	return -1;
}

#if !defined(PD_SERVER)
/* c3849 Wave 3: pre-v2 builds misparsed the raw fontjpnsingle glyph bank
 * into a garbage .pdfont (the only face/segment name that matched). The
 * JPN glyph banks have no valid struct-font shape, so any stale archive
 * is removed instead of regenerated -- otherwise the walker keeps
 * registering a junk ASSET_FONT row. */
static void s_removeStaleJpnArchives(const char *out_dir)
{
	static const char *const k_Stale[] = {
		"base_font_fontjpn",
		"base_font_fontjpnsingle",
		"base_font_fontjpnmulti",
	};
	for (size_t i = 0; i < sizeof(k_Stale) / sizeof(k_Stale[0]); i++) {
		char rel[FS_MAXPATH];
		snprintf(rel, sizeof(rel), "%s/%s.pdfont", out_dir, k_Stale[i]);
		if (fsFileSize(rel) <= 0) continue;
		char full_buf[FS_MAXPATH + 1];
		const char *full = fsFullPath(rel, full_buf, sizeof(full_buf));
		if (full && full[0] && remove(full) == 0) {
			sysLogPrintf(LOG_NOTE,
				"romextract pdfont: removed stale pre-v2 archive \"%s\"",
				rel);
		} else {
			sysLogPrintf(LOG_WARNING,
				"romextract pdfont: could not remove stale archive \"%s\"",
				rel);
		}
	}
}

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

	s32 r = s_emitOneFont(k_FontFaces[i].face, k_FontFaces[i].segname,
		c->fonts_dir, c->force_rewrite);
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

	/* Before the fast-cache fingerprint: drop garbage v1 JPN archives so
	 * they neither register nor pollute the v2 stamp. */
	s_removeStaleJpnArchives(fonts_dir);

	if (romExtractPdFastCacheCanSkip(PDFONT_FAST_CACHE_KIND, fonts_dir,
			".pdfont", force_rewrite)) {
		bootProgressUpdate((s32)K_FONT_FACE_COUNT, (s32)K_FONT_FACE_COUNT);
		sysLogPrintf(LOG_NOTE,
			"romextract pdfont: written=0 skipped=%zu failed=0 "
			"total=%zu (out=%s, fast-cache)",
			K_FONT_FACE_COUNT, K_FONT_FACE_COUNT, fonts_dir);
		return 0;
	}

	/* In-place cache-kind bump (B-943): force a one-time per-file rewrite when
	 * the stored kind differs from the current one (no-op on clean install or
	 * unchanged kind). See romExtractPdFastCacheKindMismatch. */
	s32 effective_force = force_rewrite |
		romExtractPdFastCacheKindMismatch(PDFONT_FAST_CACHE_KIND, fonts_dir);

	pdfont_fanout_ctx_t fctx;
	memset(&fctx, 0, sizeof(fctx));
	fctx.fonts_dir     = fonts_dir;
	fctx.force_rewrite = effective_force;
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

	if (failed == 0) {
		romExtractPdFastCacheWrite(PDFONT_FAST_CACHE_KIND, fonts_dir, ".pdfont");
	}

	return written;
#endif
}
