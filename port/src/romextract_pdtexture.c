/**
 * romextract_pdtexture.c -- c3849 Wave 4 (Slice A): standalone base-texture
 * public-source emitter, split out of romextract_pdmeta.c (c3843).
 *
 * Emits one .pdtexture ZIP per bundled ASSET_TEXTURE catalog row at
 * data/<romid>/textures/<slug>.pdtexture:
 *   texture.ini          modder-facing descriptor (catalog_id + texture_file)
 *   texture.png          decoded RGBA PNG (1x1 transparent for empty slots)
 *   _meta/manifest.json  envelope + provenance + texture_file parity (B-854)
 *
 * Decode reuses romExtractDecodeTextureImages (romextract_pdarena.c), which
 * inflates through the game's own texdecompress.c paths from the user's
 * extracted textureslist/texturesdata segments (BYOR: nothing pre-shipped).
 *
 * Wave 4 hardening over the pdmeta-era emitter: romExtractAllPdmeta WROTE
 * the textures fast-cache stamp but never read it, so every warm boot paid
 * fsFileSize + two ZIP opens per texture (~7,000 opens for 3,503 NTSC
 * rows). romExtractAllPdtexture now early-outs through
 * romExtractPdFastCacheCanSkip like every other family emitter; the
 * per-entry member checks remain the stale-stamp fallback.
 *
 * Catalog binding stays REGISTRATION-time (assetcatalog_base_extended.c):
 * each base ASSET_TEXTURE row's FileProvider primary points at
 * data/<romid>/textures/<slug>.pdtexture::texture.png and texLoad's public
 * image-source intercept (mod_texture_source.c) consumes it as RGBA32.
 * Emit-on-demand is NOT viable: texLoad sysFatalErrors when a bound archive
 * is missing, so safety rests on emit-before-walker boot order (B-325).
 *
 * Server build (PD_SERVER): returns 0 immediately; no ROM is loaded and the
 * disk paths are not produced.
 */

#include <PR/ultratypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_archive_writer.h"
#include "assetcatalog.h"
#include "assetcatalog_slug.h"
#include "boot_progress.h"
#include "constants.h"
#include "fs.h"
#include "modarchive.h"
#include "romextract_pd.h"
#include "system.h"

/* Stamp kind must stay under 64 chars (romextract_pd_cache.c reads it with
 * %63s); bump the label to invalidate and re-emit the whole family. */
#define ROMEXTRACT_PDTEXTURE_FAST_CACHE_KIND \
	"pdtexture_png_v1_decoded_rom_rgba_manifest_texture_file"

static const u8 k_Transparent1x1Png[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
	0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
	0x89, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41,
	0x54, 0x78, 0xda, 0x63, 0x60, 0x00, 0x02, 0x00,
	0x00, 0x05, 0x00, 0x01, 0xe9, 0xfa, 0xdc, 0xd8,
	0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
	0xae, 0x42, 0x60, 0x82,
};

/* Slice B: archive name and bound primary path both come from the shared
 * catalogIdToFilenameSlug (assetcatalog_slug.h) -- the per-file private
 * slug copy is gone, so emitter/binding drift is structurally impossible. */
static void s_archiveRelPath(const char *dir, const char *id,
	const char *ext, char *out, size_t out_n)
{
	if (!out || out_n == 0) return;
	out[0] = '\0';
	char slug[CATALOG_ID_LEN];
	catalogIdToFilenameSlug(id, slug, sizeof(slug));
	snprintf(out, out_n, "%s/%s%s", dir, slug, ext);
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

static s32 s_openWriter(const char *relpath, const char *family,
	const char *catalog_id, const char *tool, const char *source,
	s32 source_index, mod_archive_writer_t **out_aw,
	asset_archive_writer_t *out_writer)
{
	if (out_aw) *out_aw = NULL;
	if (!relpath || !family || !catalog_id || !out_aw || !out_writer) {
		return -1;
	}

	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) {
		sysLoudFailf("EXTRACT.PDTEXTURE",
			"fsFullPath failed for \"%s\"", relpath);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDTEXTURE",
			"modArchiveBegin failed for \"%s\"", full);
		return -1;
	}

	if (assetArchiveWriterInit(out_writer, aw, family, catalog_id) !=
			MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDTEXTURE",
			"assetArchiveWriterInit failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}

	assetArchiveWriterSetProvenance(out_writer, tool, source,
		source_index, catalog_id);
	*out_aw = aw;
	return 0;
}

static s32 s_finishWriter(mod_archive_writer_t *aw,
	asset_archive_writer_t *writer, const char *relpath)
{
	if (assetArchiveWriterFinishMetadata(writer) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDTEXTURE",
			"assetArchiveWriterFinishMetadata failed for \"%s\"", relpath);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDTEXTURE",
			"modArchiveFinish failed for \"%s\"", relpath);
		return -1;
	}
	return 1;
}

static s32 s_emitTexture(const asset_entry_t *e, const char *out_dir,
	s32 force_rewrite)
{
	char relpath[FS_MAXPATH];
	s_archiveRelPath(out_dir, e->id, ".pdtexture", relpath, sizeof(relpath));
	if (!force_rewrite && fsFileSize(relpath) > 0 &&
			s_existingArchiveHasEntry(relpath, "texture.ini") &&
			s_existingArchiveHasEntry(relpath, "texture.png")) {
		return 0;
	}

	u8 *tga = NULL;
	u8 *png = NULL;
	u32 tga_size = 0;
	u32 png_size = 0;
	u32 width = 0;
	u32 height = 0;
	s32 empty_rom_slot = 0;
	if (romExtractDecodeTextureImages((u16)e->ext.texture.texture_id,
			&tga, &tga_size, &png, &png_size, &width, &height, NULL) != 0 ||
			!png || png_size == 0) {
		if (tga) { free(tga); tga = NULL; }
		if (png) { free(png); png = NULL; }
		empty_rom_slot =
			romExtractTextureSlotIsEmpty((u16)e->ext.texture.texture_id);
		if (!empty_rom_slot) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdtexture: failed to decode %s (texture_id=%d)",
				e->id, e->ext.texture.texture_id);
			return -1;
		}
		png = (u8 *)malloc(sizeof(k_Transparent1x1Png));
		if (!png) {
			return -1;
		}
		memcpy(png, k_Transparent1x1Png, sizeof(k_Transparent1x1Png));
		png_size = (u32)sizeof(k_Transparent1x1Png);
		width = 1;
		height = 1;
		sysLogPrintf(LOG_NOTE,
			"romextract pdtexture: emitted transparent source for empty ROM slot %s",
			e->id);
	}

	char ini[1024];
	int ini_len = snprintf(ini, sizeof(ini),
		"[texture]\n"
		"catalog_id = %s\n"
		"name = %s\n"
		"width = %u\n"
		"height = %u\n"
		"texture_file = texture.png\n",
		e->id, e->id, (unsigned)width, (unsigned)height);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini)) {
		free(tga);
		free(png);
		return -1;
	}

	char manifest[1024];
	int manifest_len = snprintf(manifest, sizeof(manifest),
		"{\n"
		"  \"pd_kind\": \"texture\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"texture_file\": \"texture.png\",\n"
		"  \"source_state\": \"%s\",\n"
		"  \"size\": { \"width\": %u, \"height\": %u }\n"
		"}\n",
		e->id,
		empty_rom_slot ? "empty_rom_slot" : "decoded_rom_texture",
		(unsigned)width, (unsigned)height);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest)) {
		free(tga);
		free(png);
		return -1;
	}

	mod_archive_writer_t *aw;
	asset_archive_writer_t writer;
	if (s_openWriter(relpath, "texture", e->id, "romextract_pdtexture",
			"textureslist/texturesdata", e->ext.texture.texture_id,
			&aw, &writer) != 0) {
		free(tga);
		free(png);
		return -1;
	}
	if (assetArchiveWriterAddDescriptor(&writer, "texture.ini",
			ini, (u32)ini_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddManifestJson(&writer,
			manifest, (u32)manifest_len) != MODARCHIVE_OK ||
			assetArchiveWriterAddPublicMem(&writer, "texture.png",
			png, png_size, "texture") != MODARCHIVE_OK) {
		modArchiveAbort(aw);
		free(tga);
		free(png);
		return -1;
	}
	free(tga);
	free(png);
	return s_finishWriter(aw, &writer, relpath);
}

/* c3849 Wave 4 Slice B: boot bind verification (Gate-5 style, mirrors
 * B-908). texLoad's public-source path sysFatalErrors at FIRST USE when a
 * bound .pdtexture archive is missing (mod_texture_source.c), with zero
 * boot-time detection -- a miss here is boot-completes-fine-then-fatal
 * mid-game. This pass surfaces every miss loudly at boot instead.
 *
 * Cheap by design: split each bundled ASSET_TEXTURE row's bound primary
 * path at "::" and fsFileSize only the archive half (a VFS central-dir
 * lookup or stat -- never a ZIP open), so it runs on BOTH the emit path
 * and the fast-cache skip path (~3,500 stats, negligible). */
static void s_verifyTextureBinds(void)
{
	s32 total = assetCatalogGetCount();
	s32 checked = 0;
	s32 missing = 0;

	for (s32 i = 0; i < total; i++) {
		const asset_entry_t *e = assetCatalogGetByIndex(i);
		if (!e || !e->occupied || !e->bundled || e->type != ASSET_TEXTURE) {
			continue;
		}
		checked++;

		const char *path = fileProviderPath(e->source.primary);
		if (!path || !path[0]) {
			missing++;
			sysLogPrintf(LOG_WARNING,
				"TEXTURE.BIND: %s has no FileProvider primary bound",
				e->id);
			continue;
		}

		char archive[FS_MAXPATH + 1];
		strncpy(archive, path, sizeof(archive) - 1);
		archive[sizeof(archive) - 1] = '\0';
		char *sep = strstr(archive, "::");
		if (sep) {
			*sep = '\0';
		}

		if (fsFileSize(archive) <= 0) {
			missing++;
			sysLogPrintf(LOG_WARNING,
				"TEXTURE.BIND: %s missing archive \"%s\" (bound primary \"%s\")",
				e->id, archive, path);
		}
	}

	if (missing > 0) {
		sysLogPrintf(LOG_WARNING,
			"TEXTURE.BIND: %d missing of %d -- first texLoad of any "
			"missing texture is fatal (mod_texture_source.c)",
			missing, checked);
	} else {
		sysLogPrintf(LOG_NOTE, "TEXTURE.BIND: 0 missing of %d", checked);
	}
}

s32 romExtractAllPdtexture(s32 force_rewrite)
{
#if defined(PD_SERVER)
	(void)force_rewrite;
	return 0;
#else
	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDTEXTURE", "fsDataDirEnsure failed");
		return -1;
	}

	char data_dir_buf[FS_MAXPATH + 1];
	const char *data_dir = fsDataDir(data_dir_buf, sizeof(data_dir_buf));
	char textures_dir[FS_MAXPATH];
	snprintf(textures_dir, sizeof(textures_dir), "%s/textures", data_dir);
	if (!fsCreateDir(textures_dir)) {
		sysLoudFailf("EXTRACT.PDTEXTURE",
			"fsCreateDir(\"%s\") failed", textures_dir);
		return -1;
	}

	/* Bundled ASSET_TEXTURE count comes from the catalog, never a
	 * hardcoded 3503 -- JPN registers 3511 (NUM_TEXTURES is per-romid). */
	s32 total = assetCatalogGetCount();
	s32 tex_total = 0;
	for (s32 i = 0; i < total; i++) {
		const asset_entry_t *e = assetCatalogGetByIndex(i);
		if (e && e->occupied && e->bundled && e->type == ASSET_TEXTURE) {
			tex_total++;
		}
	}

	/* Wave 4 dead-stamp fix: the whole-family early-out pdmeta never had.
	 * On a valid stamp this replaces ~2 ZIP opens + 1 stat per texture
	 * with one directory scan. */
	if (romExtractPdFastCacheCanSkip(ROMEXTRACT_PDTEXTURE_FAST_CACHE_KIND,
			textures_dir, ".pdtexture", force_rewrite)) {
		bootProgressUpdate(tex_total, tex_total);
		sysLogPrintf(LOG_NOTE,
			"romextract pdtexture: written=0 skipped=%d failed=0 "
			"total=%d (out=%s, fast-cache)",
			tex_total, tex_total, textures_dir);
		/* The skip path still verifies binds -- catching a deleted or
		 * renamed archive before first texLoad is the point of the pass,
		 * and the stamp's dir fingerprint is a heuristic, not a proof. */
		s_verifyTextureBinds();
		return 0;
	}

	/* In-place cache-kind bump (B-943): force a one-time per-file rewrite when
	 * the stored kind differs from the current one (no-op on clean install or
	 * unchanged kind). See romExtractPdFastCacheKindMismatch. */
	s32 effective_force = force_rewrite |
		romExtractPdFastCacheKindMismatch(ROMEXTRACT_PDTEXTURE_FAST_CACHE_KIND,
			textures_dir);

	s32 written = 0;
	s32 skipped = 0;
	s32 failed = 0;
	s32 processed = 0;

	bootProgressUpdate(0, tex_total);
	for (s32 i = 0; i < total; i++) {
		const asset_entry_t *e = assetCatalogGetByIndex(i);
		if (!e || !e->occupied || !e->bundled || e->type != ASSET_TEXTURE) {
			continue;
		}
		s32 r = s_emitTexture(e, textures_dir, effective_force);
		if (r > 0) written++;
		else if (r == 0) skipped++;
		else failed++;
		processed++;
		if ((processed & 0x1f) == 0) {
			bootProgressUpdate(processed, tex_total);
		}
	}
	bootProgressUpdate(tex_total, tex_total);

	sysLogPrintf(LOG_NOTE,
		"romextract pdtexture: written=%d skipped=%d failed=%d "
		"total=%d (out=%s)",
		written, skipped, failed, tex_total, textures_dir);

	if (failed == 0) {
		romExtractPdFastCacheWrite(ROMEXTRACT_PDTEXTURE_FAST_CACHE_KIND,
			textures_dir, ".pdtexture");
	}

	s_verifyTextureBinds();

	return failed ? -1 : written;
#endif
}
