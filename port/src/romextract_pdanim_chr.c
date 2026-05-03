/**
 * romextract_pdanim_chr.c -- Catalog universality pivot Step 3a (2026-05-03).
 *
 * Walks the chr-animation table embedded in the "animations" ROM
 * segment (data/<romid>/segs/animations.bin on disk; in-memory pointer
 * via _animationsTableRomStart / _animationsTableRomEnd) and emits
 * one .pdanim ZIP compound per registered chr animation at
 * data/<romid>/animations/<id>.pdanim.
 *
 * Companion to romextract_pdanim.c (Step 1, weapon-animation gunscript
 * opcodes -- plain JSON, category="weapon_animation"). This file
 * handles category="character_animation" -- ZIP compound per the
 * universality-pivot-schemas.md Section 2.6 lock-down.
 *
 * Per Mike's Q-3 ruling (2026-05-02): "DO NOT DEFER beyond the scope
 * of the catalog work. Catalog is not complete unless it is COMPLETE."
 * Step 3a closes that ruling so chr animations become per-asset files
 * alongside the Step 1 weapon-anim files.
 *
 * Per-anim byte layout in the lump (validated against
 * src/lib/anim.c::animLoadFrame, line 312 area):
 *   header bytes : entry.data .. entry.data + entry.headerlen
 *   frame data   : entry.data + entry.headerlen ..
 *                  entry.data + entry.headerlen + numframes * bytesperframe
 * Total per-anim span = headerlen + numframes * bytesperframe.
 *
 * Companion docs:
 *   context/designs/catalog/universality-pivot-schemas.md   schemas
 *   context/audits/catalog-universality-pivot-plan-2026-05-02.md  plan
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

/* The chr animation table sits at the tail of the "animations" segment.
 * Byte length is fixed at 0x38a0 across ROM versions; first u32 is the
 * count (already byte-swapped by preprocessAnimations), followed by
 * `count` struct animtableentry records (also byte-swapped). */
#define PDANIM_CHR_TABLE_TAIL_BYTES 0x38a0

/* Locate the "animations" segment buffer + size by name. Returns 1 on
 * success with *outData / *outSize populated, 0 on miss (segment not
 * populated; e.g. server build with no ROM loaded). */
static s32 s_findAnimSegment(const u8 **outData, u32 *outSize)
{
	*outData = NULL;
	*outSize = 0;

	s32 nseg = romdataSegmentCount();
	for (s32 i = 0; i < nseg; i++) {
		const char *name = romdataSegmentGetName(i);
		if (name && strcmp(name, "animations") == 0) {
			const u8 *data = romdataSegmentGetData(i);
			u32 size = romdataSegmentGetSize(i);
			if (!data || size == 0) return 0;
			*outData = data;
			*outSize = size;
			return 1;
		}
	}
	return 0;
}

/* Lowercase a string into out_buf. Stops at NUL or out_n - 1. */
static void s_lowercaseInto(const char *src, char *out, size_t out_n)
{
	size_t i;
	for (i = 0; src && src[i] && i + 1 < out_n; i++) {
		out[i] = (char)tolower((unsigned char)src[i]);
	}
	out[i] = '\0';
}

/* Build the catalog ID for a chr animation index. If the loader's
 * enum reverse-lookup has a symbolic name (e.g. "ANIM_HEROHIT"), emit
 * "base:anim_herohit". Otherwise fall back to "base:anim_chr_<NNNN>"
 * with the index in 4-digit hex (Q-4 Bucket 2: has consumers, name
 * obscure -> generated stable ID).
 *
 * Returns 1 if a symbolic name was found, 0 if generated. */
static s32 s_buildCatalogId(s32 anim_idx, char *out, size_t out_n)
{
	const char *sym = loaderEnumNameForAnimEnum(anim_idx);
	if (sym && sym[0]) {
		char lowered[96];
		s_lowercaseInto(sym, lowered, sizeof(lowered));
		snprintf(out, out_n, "base:%s", lowered);
		return 1;
	}
	snprintf(out, out_n, "base:anim_chr_%04x", (unsigned)anim_idx);
	return 0;
}

/* Emit one .pdanim ZIP compound. Returns 1 written, 0 skipped, -1 failed. */
static s32 s_emitOneChrAnim(s32 anim_idx,
                             const struct animtableentry *entry,
                             const u8 *seg_data, u32 seg_size,
                             const char *out_dir, s32 force_rewrite)
{
	/* mod-override marker: preprocessAnimations sets entry->data to
	 * 0xffffffff if a mod has hooked the slot via
	 * modAnimationLoadDescriptor. Mod scan runs LATER than this
	 * emitter in the boot path, so we should never see this state
	 * at extract time -- defensive bail with a warning. */
	if (entry->data == 0xffffffff) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdanim_chr: anim_idx=%d marked mod-override "
			"(data=0xffffffff); skipping -- boot order regression?",
			anim_idx);
		return 0;
	}

	/* Empty slot: numframes==0 AND headerlen==0 means the table entry
	 * is unused. Some legacy ROMs reserve table space for animations
	 * that never got authored. Skip silently. */
	if (entry->numframes == 0 && entry->headerlen == 0) {
		return 0;
	}

	/* Compute the per-anim byte span and validate it fits inside the
	 * frame-data region of the segment (everything before the table
	 * tail). If not, the table is corrupt or our layout assumption
	 * is wrong -- LOUDFAIL with diagnostic detail. */
	const u32 frame_region_size = (seg_size > PDANIM_CHR_TABLE_TAIL_BYTES)
		? (seg_size - PDANIM_CHR_TABLE_TAIL_BYTES) : 0;
	const u32 anim_bytes = (u32)entry->headerlen
	                     + (u32)entry->numframes * (u32)entry->bytesperframe;

	if (entry->data + anim_bytes > frame_region_size) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"anim_idx=%d byte range overflow: data=0x%x + "
			"len=0x%x > frame_region=0x%x (numframes=%u "
			"bytesperframe=%u headerlen=%u)",
			anim_idx, (unsigned)entry->data, (unsigned)anim_bytes,
			(unsigned)frame_region_size, (unsigned)entry->numframes,
			(unsigned)entry->bytesperframe, (unsigned)entry->headerlen);
		return -1;
	}

	char catalog_id[128];
	(void)s_buildCatalogId(anim_idx, catalog_id, sizeof(catalog_id));

	/* Filename: catalog_id with ':' -> '_'. */
	char filename_slug[128];
	{
		size_t i, j = 0;
		for (i = 0; catalog_id[i] && j + 1 < sizeof(filename_slug); i++) {
			filename_slug[j++] = (catalog_id[i] == ':') ? '_' : catalog_id[i];
		}
		filename_slug[j] = '\0';
	}

	char dst_rel[FS_MAXPATH];
	snprintf(dst_rel, sizeof(dst_rel), "%s/%s.pdanim", out_dir, filename_slug);

	if (!force_rewrite && fsFileSize(dst_rel) > 0) return 0;

	/* Build manifest.json text in memory. Schema fields per Section 2.6
	 * + provenance hints (source_offset, source_index) for the parity
	 * check and Step 4 round-trip. */
	const char *sym = loaderEnumNameForAnimEnum(anim_idx);
	char manifest_buf[1024];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"animation\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"category\": \"character_animation\",\n"
		"  \"frames\": \"frames.bin\",\n"
		"  \"frame_count\": %u,\n"
		"  \"bytes_per_frame\": %u,\n"
		"  \"header_len\": %u,\n"
		"  \"framelen\": %u,\n"
		"  \"flags\": %u,\n"
		"  \"source_index\": %d,\n"
		"  \"source_offset\": %u,\n"
		"  \"source_symbol\": \"%s\"\n"
		"}\n",
		catalog_id,
		(unsigned)entry->numframes,
		(unsigned)entry->bytesperframe,
		(unsigned)entry->headerlen,
		(unsigned)entry->framelen,
		(unsigned)entry->flags,
		anim_idx,
		(unsigned)entry->data,
		sym ? sym : "");
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"manifest.json snprintf truncated for anim_idx=%d", anim_idx);
		return -1;
	}

	const char *dst_full = fsFullPath(dst_rel);
	if (!dst_full || !dst_full[0]) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"fsFullPath failed for \"%s\"", dst_rel);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "manifest.json",
	                          manifest_buf, (u32)manifest_len) != 0) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"AddFileMem manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		return -1;
	}

	/* Frame payload: contiguous bytes [data, data + anim_bytes) from
	 * the segment buffer.  Empty-payload anims (anim_bytes == 0) still
	 * round-trip a zero-length entry so the catalog row exists. */
	if (modArchiveAddFileMem(aw, "frames.bin",
	                          (const char *)(seg_data + entry->data),
	                          anim_bytes) != 0) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"AddFileMem frames.bin failed for \"%s\" "
			"(anim_idx=%d len=%u)",
			dst_full, anim_idx, (unsigned)anim_bytes);
		modArchiveAbort(aw);
		return -1;
	}

	/* SHA-256 sidecar over the frame payload (matches .pdmesh pattern). */
	if (anim_bytes > 0) {
		u8 digest[SHA256_DIGEST_SIZE];
		sha256Hash(seg_data + entry->data, (size_t)anim_bytes, digest);
		char hex[SHA256_HEX_SIZE + 1];
		sha256ToHex(digest, hex);
		hex[SHA256_HEX_SIZE] = '\0';
		char sidecar[SHA256_HEX_SIZE + 2];
		snprintf(sidecar, sizeof(sidecar), "%s\n", hex);
		if (modArchiveAddFileMem(aw, "frames.bin.sha256",
		                          sidecar, (u32)strlen(sidecar)) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdanim_chr: sidecar write failed for "
				"\"%s\" (anim_idx=%d)",
				dst_full, anim_idx);
		}
	}

	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}

	return 1;
}

s32 romExtractAllPdanimChr(s32 force_rewrite)
{
	const u8 *seg_data = NULL;
	u32 seg_size = 0;

	if (!s_findAnimSegment(&seg_data, &seg_size)) {
		sysLogPrintf(LOG_NOTE,
			"romextract pdanim_chr: \"animations\" segment not loaded "
			"(server build or pre-romdata-init); skipping");
		return 0;
	}

	if (seg_size <= PDANIM_CHR_TABLE_TAIL_BYTES) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"animations segment too small (size=%u, expected > 0x%x)",
			(unsigned)seg_size, PDANIM_CHR_TABLE_TAIL_BYTES);
		return -1;
	}

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDANIM_CHR", "fsDataDirEnsure failed");
		return -1;
	}

	char anims_dir[FS_MAXPATH];
	snprintf(anims_dir, sizeof(anims_dir), "%s/animations", fsDataDir());
	if (!fsCreateDir(anims_dir)) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"fsCreateDir(\"%s\") failed", anims_dir);
		return -1;
	}

	/* Parse the table tail. preprocessAnimations already byte-swapped
	 * the count + entry fields, so direct native-endian read is safe. */
	const u8 *table_base = seg_data + (seg_size - PDANIM_CHR_TABLE_TAIL_BYTES);
	const u32 *table_u32 = (const u32 *)table_base;
	const u32 anim_count = table_u32[0];
	const struct animtableentry *entries =
		(const struct animtableentry *)&table_u32[1];

	/* Sanity: 1208 entries is the upper bound the 0x38a0-byte tail can
	 * hold (including the u32 count prefix at offset 0). Table sizes
	 * less than this are normal across ROM versions. */
	const u32 max_entries =
		(PDANIM_CHR_TABLE_TAIL_BYTES - sizeof(u32)) / sizeof(struct animtableentry);
	if (anim_count > max_entries) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"anim_count=%u exceeds table capacity=%u "
			"(table_tail=0x%x bytes)",
			(unsigned)anim_count, (unsigned)max_entries,
			PDANIM_CHR_TABLE_TAIL_BYTES);
		return -1;
	}

	s32 written = 0;
	s32 skipped = 0;
	s32 failed = 0;
	s32 named = 0;

	for (s32 i = 0; (u32)i < anim_count; i++) {
		s32 r = s_emitOneChrAnim(i, &entries[i], seg_data, seg_size,
			anims_dir, force_rewrite);
		if (r > 0)      written++;
		else if (r == 0) skipped++;
		else             failed++;

		if (loaderEnumNameForAnimEnum(i)) named++;
	}

	sysLogPrintf(LOG_NOTE,
		"romextract pdanim_chr: written=%d skipped=%d failed=%d "
		"total=%u named=%d (anim_chr lump emitted to %s)",
		written, skipped, failed, (unsigned)anim_count, named, anims_dir);

	return written;
}
