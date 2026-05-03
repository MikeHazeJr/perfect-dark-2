/**
 * romextract_pdmesh.c -- Catalog universality pivot Step 1 (2026-05-02).
 *
 * Walks the unique set of mesh references from the loader_pool weapon
 * pool (hi_model + lo_model fields) and emits one .pdmesh ZIP compound
 * per unique mesh at data/<romid>/meshes/<id>.pdmesh.
 *
 * Compound layout per universality-pivot-schemas.md Section 2.5:
 *   manifest.json       envelope + provenance
 *   geometry.bin        raw model bytes (sourced from existing
 *                       data/<romid>/files/<sanitized_rom_name>.bin
 *                       produced by Pass A.2)
 *   geometry.bin.sha256 outer-file SHA-256 sidecar
 *
 * Reuses port/src/modarchive.c writer subset for ZIP atomic writes.
 *
 * Step 1 cross-reference convention: emits source_filenum_symbol with
 * the FILE_* enum string (provenance hint) and the catalog ID is
 * synthesized from the symbol (e.g. FILE_GFALCON2 -> base:falcon2_hi).
 * Step 4 universal loader can refine the ID minting.
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
#include "catalog_mgr_weapons.h"
#include "loader_pool.h"
#include "loader_enum_reverse.h"
#include "modarchive.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "sha256.h"
#include "system.h"

/* Track filenums already emitted to avoid duplicate work when multiple
 * weapons share a mesh. Cap matches CATALOG_MGR_WEAPON_COUNT * 2 for
 * hi + lo per weapon plus headroom. */
#define ROMEXTRACT_PDMESH_SEEN_CAP 256
static u16 s_SeenFilenums[ROMEXTRACT_PDMESH_SEEN_CAP];
static s32 s_SeenCount;

static s32 s_alreadySeen(u16 filenum)
{
	for (s32 i = 0; i < s_SeenCount; i++) {
		if (s_SeenFilenums[i] == filenum) return 1;
	}
	return 0;
}

static void s_markSeen(u16 filenum)
{
	if (s_SeenCount < ROMEXTRACT_PDMESH_SEEN_CAP) {
		s_SeenFilenums[s_SeenCount++] = filenum;
	}
}

/* Convert "FILE_GFALCON2" -> "base:falcon2_hi" given a hint suffix.
 * Falls back to "base:rom_g_<HEX>" when the symbol is not known. */
static void s_synthCatalogId(u16 filenum, const char *hint_suffix,
                              char *out, size_t n)
{
	const char *sym = loaderEnumNameForFileEnum(filenum);
	if (!sym) {
		snprintf(out, n, "base:rom_g_%04x", (unsigned)filenum);
		return;
	}
	/* Strip "FILE_G" prefix (weapon model files all start with this) and
	 * lowercase the rest, append a discriminator hint when supplied. */
	const char *body = sym;
	if (strncmp(sym, "FILE_G", 6) == 0) body = sym + 6;
	else if (strncmp(sym, "FILE_", 5) == 0) body = sym + 5;

	char lowered[96];
	size_t i;
	for (i = 0; i + 1 < sizeof(lowered) && body[i]; i++) {
		lowered[i] = (char)tolower((unsigned char)body[i]);
	}
	lowered[i] = '\0';

	if (hint_suffix && hint_suffix[0]) {
		snprintf(out, n, "base:%s_%s", lowered, hint_suffix);
	} else {
		snprintf(out, n, "base:%s", lowered);
	}
}

/* Build the on-disk path of the existing extracted .bin for a given
 * filenum. Mirrors romExtractRelPathForFilenum (port/src/romextract.c)
 * but lives in the public API; we rely on it. */
static s32 s_resolveSourceBinPath(u16 filenum, char *out_rel, s32 out_n)
{
	return romExtractRelPathForFilenum((s32)filenum, out_rel, out_n);
}

/* Emit one .pdmesh compound. Returns 1 written, 0 skipped, -1 failed. */
static s32 s_emitOneMesh(u16 filenum, const char *hint_suffix,
                          const char *out_dir, s32 force_rewrite)
{
	if (filenum == 0) return 0;
	if (s_alreadySeen(filenum)) return 0;
	s_markSeen(filenum);

	char src_rel[FS_MAXPATH];
	if (s_resolveSourceBinPath(filenum, src_rel, sizeof(src_rel)) <= 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdmesh: filenum=0x%04x has no extracted .bin path",
			(unsigned)filenum);
		return -1;
	}
	if (fsFileSize(src_rel) <= 0) {
		/* Source bytes not available on disk. Likely the filenum slot
		 * is not present in this ROM region or extraction skipped it.
		 * Not a hard error; the .pdwpn keeps a symbolic FILE_* ref so
		 * Step 4 can resolve through whatever source is available. */
		sysLogPrintf(LOG_NOTE,
			"romextract pdmesh: source missing for filenum=0x%04x (rel=\"%s\")",
			(unsigned)filenum, src_rel);
		return 0;
	}

	char catalog_id[128];
	s_synthCatalogId(filenum, hint_suffix, catalog_id, sizeof(catalog_id));

	char filename_slug[128];
	for (size_t i = 0, j = 0; j + 1 < sizeof(filename_slug); i++) {
		char c = catalog_id[i];
		if (c == '\0') { filename_slug[j] = '\0'; break; }
		filename_slug[j++] = (c == ':') ? '_' : c;
	}
	filename_slug[sizeof(filename_slug) - 1] = '\0';

	char dst_rel[FS_MAXPATH];
	snprintf(dst_rel, sizeof(dst_rel), "%s/%s.pdmesh", out_dir, filename_slug);

	if (!force_rewrite && fsFileSize(dst_rel) > 0) return 0;

	const char *sym_for_provenance = loaderEnumNameForFileEnum(filenum);

	/* Build manifest.json text in memory. */
	char manifest_buf[512];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"mesh\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"source_filenum_symbol\": \"%s\",\n"
		"  \"geometry\": \"geometry.bin\"\n"
		"}\n",
		catalog_id,
		sym_for_provenance ? sym_for_provenance : "");
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDMESH",
			"manifest.json snprintf truncated for filenum=0x%04x",
			(unsigned)filenum);
		return -1;
	}

	/* Open ZIP writer (atomic temp + rename via modArchive). */
	const char *dst_full = fsFullPath(dst_rel);
	if (!dst_full || !dst_full[0]) {
		sysLoudFailf("EXTRACT.PDMESH",
			"fsFullPath failed for \"%s\"", dst_rel);
		return -1;
	}
	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDMESH",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "manifest.json",
	                          manifest_buf, (u32)manifest_len) != 0) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileMem manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		return -1;
	}

	const char *src_full = fsFullPath(src_rel);
	if (!src_full || !src_full[0]) {
		sysLoudFailf("EXTRACT.PDMESH",
			"fsFullPath failed for source \"%s\"", src_rel);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveAddFileDisk(aw, "geometry.bin", src_full) != 0) {
		sysLoudFailf("EXTRACT.PDMESH",
			"AddFileDisk geometry.bin failed for \"%s\" -> \"%s\"",
			src_full, dst_full);
		modArchiveAbort(aw);
		return -1;
	}

	/* Compute SHA-256 of the source bytes for the in-archive sidecar.
	 * Read the file once for hashing; the ZIP writer will read again to
	 * stage the entry. The double-read is acceptable for a one-shot
	 * boot extraction; can be optimized later if it shows up in profiles. */
	u32 src_size = 0;
	void *src_bytes = fsFileLoad(src_rel, &src_size);
	if (src_bytes && src_size > 0) {
		u8 digest[SHA256_DIGEST_SIZE];
		sha256Hash(src_bytes, (size_t)src_size, digest);
		char hex[SHA256_HEX_SIZE + 1];
		sha256ToHex(digest, hex);
		hex[SHA256_HEX_SIZE] = '\0';
		char sidecar[SHA256_HEX_SIZE + 2];
		snprintf(sidecar, sizeof(sidecar), "%s\n", hex);
		if (modArchiveAddFileMem(aw, "geometry.bin.sha256",
		                          sidecar, (u32)strlen(sidecar)) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdmesh: sidecar write failed for \"%s\"",
				dst_full);
		}
	}
	if (src_bytes) sysMemFree(src_bytes);

	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDMESH",
			"modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}

	return 1;
}

s32 romExtractAllPdmesh(s32 force_rewrite)
{
	/* B-318 (2026-05-03): unconditional run with skip-on-existing.
	 * See romextract_pdwpn.c for rationale (gate-removal breaks the
	 * walker deadlock; inner loop tolerates an empty pool). */

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDMESH", "fsDataDirEnsure failed");
		return -1;
	}

	char meshes_dir[FS_MAXPATH];
	snprintf(meshes_dir, sizeof(meshes_dir), "%s/meshes", fsDataDir());
	if (!fsCreateDir(meshes_dir)) {
		sysLoudFailf("EXTRACT.PDMESH",
			"fsCreateDir(\"%s\") failed", meshes_dir);
		return -1;
	}

	s_SeenCount = 0;

	s32 written = 0;
	s32 skipped = 0;
	s32 failed = 0;

	for (s32 i = 0; i < CATALOG_MGR_WEAPON_COUNT; i++) {
		const struct weapon *wpn = loaderPoolGetWeapon(i);
		if (!wpn) continue;

		s32 r;
		r = s_emitOneMesh(wpn->hi_model, "hi", meshes_dir, force_rewrite);
		if (r > 0)      written++;
		else if (r == 0) skipped++;
		else              failed++;

		r = s_emitOneMesh(wpn->lo_model, "lo", meshes_dir, force_rewrite);
		if (r > 0)      written++;
		else if (r == 0) skipped++;
		else              failed++;
	}

	sysLogPrintf(LOG_NOTE,
		"romextract pdmesh: written=%d skipped=%d failed=%d unique_filenums=%d",
		written, skipped, failed, s_SeenCount);

	return written;
}
