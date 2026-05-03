/**
 * romextract_pdsong.c -- Catalog universality pivot Step 3 audio
 * half (2026-05-03).
 *
 * Music-track emitter. Walks the byte-swapped struct seqtable at
 * the head of the disk-migrated "sequences" segment and emits one
 * .pdsong ZIP compound per entry at data/<romid>/audio/music/.
 *
 * Per-asset ZIP layout per universality-pivot-schemas.md Section 2.9:
 *   manifest.json     envelope + sequence metadata + provenance
 *   data.bin          raw sequence bytes sliced from sequences segment
 *   data.bin.sha256   outer-file SHA-256 sidecar
 *
 * Catalog ID convention: base:song_<NNNN> where NNNN is the seqtable
 * slot index in 4-digit hex (Q-4 Bucket 2). The 43 catalog-registered
 * music tracks (s_BaseMusicTracks[] in assetcatalog_base_extended.c)
 * use slug names like "track_dark_combat" but those map to MUSIC_*
 * enum values, NOT to seqtable slot indices directly. The MUSIC_* ->
 * seqtable mapping lives in the music subsystem and is curated at
 * Step 5 cleanup (or in a follow-up worktree); the slot-based ID
 * stays stable across that follow-up so refs survive.
 *
 * Per-entry layout in the sequences segment:
 *   offset 0x00     u16 count                  (byte-swapped)
 *   offset 0x02     padding to 8-byte stride   (preprocessSequences leaves
 *                                               the original 4-byte alignment;
 *                                               the entries[] array starts
 *                                               at offset 4 because the table
 *                                               is dma'd as a 16-byte header
 *                                               then a count-driven payload)
 *   offset 0x04     struct seqtableentry[count]
 *
 * Each seqtableentry is { u32 romaddr; u16 binlen; u16 ziplen }.
 * romaddr at extract time is segment-relative (the live runtime adds
 * _sequencesSegmentRomStart); binlen is the uncompressed sequence
 * stream length, ziplen the on-disk compressed length. We slice
 * `binlen` bytes from offset `romaddr` as data.bin -- the runtime
 * decoder feeds the same slice.
 *
 * Server build: returns 0 immediately (sequences segment not
 * populated; nothing to extract).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>

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

#define PDSONG_OUT_DIR "audio/music"

/* Locate a named segment buffer + size. Returns 1 on success, 0 on
 * miss. Mirrors the segment-lookup helpers in romextract_pdsfx.c
 * and romextract_pdanim_chr.c. */
static s32 s_findSegment(const char *name, const u8 **outData, u32 *outSize)
{
	*outData = NULL;
	*outSize = 0;
	if (!name) return 0;

	s32 nseg = romdataSegmentCount();
	for (s32 i = 0; i < nseg; i++) {
		const char *seg_name = romdataSegmentGetName(i);
		if (seg_name && strcmp(seg_name, name) == 0) {
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

/* Catalog ID for a song slot. base:song_<NNNN> hex form. */
static void s_buildSongCatalogId(s32 slot_idx, char *out, size_t out_n)
{
	snprintf(out, out_n, "base:song_%04x", (unsigned)slot_idx);
}

/* Filename slug: catalog ID with ':' -> '_'. */
static void s_filenameSlug(const char *catalog_id, char *out, size_t out_n)
{
	size_t i, j = 0;
	for (i = 0; catalog_id[i] && j + 1 < out_n; i++) {
		out[j++] = (catalog_id[i] == ':') ? '_' : catalog_id[i];
	}
	out[j] = '\0';
}

/* Emit one .pdsong ZIP. Returns 1 written, 0 skipped, -1 failed. */
static s32 s_emitOneSong(s32 slot_idx,
                         const struct seqtableentry *entry,
                         const u8 *seg_data, u32 seg_size,
                         const char *out_dir, s32 force_rewrite)
{
	if (entry->binlen == 0) {
		/* Empty slot: zero-length sequence. Skip silently. */
		return 0;
	}

	if ((u32)entry->romaddr + (u32)entry->binlen > seg_size) {
		sysLoudFailf("EXTRACT.PDSONG",
			"slot=%d range 0x%x..0x%x past segment size 0x%x "
			"(binlen=%u ziplen=%u)",
			slot_idx,
			(unsigned)entry->romaddr,
			(unsigned)entry->romaddr + entry->binlen,
			(unsigned)seg_size,
			(unsigned)entry->binlen,
			(unsigned)entry->ziplen);
		return -1;
	}

	char catalog_id[64];
	s_buildSongCatalogId(slot_idx, catalog_id, sizeof(catalog_id));

	char filename_slug[64];
	s_filenameSlug(catalog_id, filename_slug, sizeof(filename_slug));

	char dst_rel[FS_MAXPATH];
	snprintf(dst_rel, sizeof(dst_rel),
		"%s/%s.pdsong", out_dir, filename_slug);

	if (!force_rewrite && fsFileSize(dst_rel) > 0) return 0;

	/* Detect whether the slice is zlib-compressed (ziplen > 0 means
	 * the runtime needs zip-decompression before play). The schema's
	 * format string distinguishes uncompressed (loose ALSEQ) from
	 * zlib-wrapped. Either way the byte slice carries the on-disk
	 * form; the loader decompresses if needed.
	 *
	 * NOTE: ziplen == 0 in practice means binlen == ziplen and the
	 * data is uncompressed. ziplen > 0 means zlib payload.
	 *
	 * We expose both lengths in the manifest for round-trip parity. */
	const char *fmt_str = (entry->ziplen > 0) ? "ALSEQ_ZIP" : "ALSEQ";

	/* The slice we extract is whichever length is on disk. PD writes
	 * the larger of binlen and ziplen, but both should bound the
	 * actual byte range. We use ziplen if non-zero (compressed), else
	 * binlen (uncompressed). */
	u32 slice_len = (entry->ziplen > 0) ? (u32)entry->ziplen
	                                     : (u32)entry->binlen;

	if ((u32)entry->romaddr + slice_len > seg_size) {
		sysLoudFailf("EXTRACT.PDSONG",
			"slot=%d slice len=%u past segment (romaddr=0x%x size=0x%x)",
			slot_idx, (unsigned)slice_len,
			(unsigned)entry->romaddr, (unsigned)seg_size);
		return -1;
	}

	char manifest_buf[1024];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"song\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"format\": \"%s\",\n"
		"  \"data\": \"data.bin\",\n"
		"  \"data_size\": %u,\n"
		"  \"binlen\": %u,\n"
		"  \"ziplen\": %u,\n"
		"  \"loops\": false,\n"
		"  \"source_index\": %d,\n"
		"  \"source_offset\": %u\n"
		"}\n",
		catalog_id, fmt_str,
		(unsigned)slice_len,
		(unsigned)entry->binlen,
		(unsigned)entry->ziplen,
		slot_idx,
		(unsigned)entry->romaddr);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDSONG",
			"manifest.json snprintf truncated for slot=%d", slot_idx);
		return -1;
	}

	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		sysLoudFailf("EXTRACT.PDSONG",
			"fsFullPath empty for \"%s\"", dst_rel);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDSONG",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "manifest.json",
	                          manifest_buf, (u32)manifest_len) != 0) {
		sysLoudFailf("EXTRACT.PDSONG",
			"AddFileMem manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "data.bin",
	                          (const char *)(seg_data + entry->romaddr),
	                          slice_len) != 0) {
		sysLoudFailf("EXTRACT.PDSONG",
			"AddFileMem data.bin failed for \"%s\" "
			"(slot=%d len=%u)",
			dst_full, slot_idx, (unsigned)slice_len);
		modArchiveAbort(aw);
		return -1;
	}

	{
		u8 digest[SHA256_DIGEST_SIZE];
		sha256Hash(seg_data + entry->romaddr, (size_t)slice_len, digest);
		char hex[SHA256_HEX_SIZE + 1];
		sha256ToHex(digest, hex);
		hex[SHA256_HEX_SIZE] = '\0';
		char sidecar[SHA256_HEX_SIZE + 2];
		snprintf(sidecar, sizeof(sidecar), "%s\n", hex);
		if (modArchiveAddFileMem(aw, "data.bin.sha256",
		                          sidecar, (u32)strlen(sidecar)) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract pdsong: sidecar write failed for \"%s\" "
				"(slot=%d)", dst_full, slot_idx);
		}
	}

	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDSONG",
			"modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}

	return 1;
}

s32 romExtractAllPdsong(s32 force_rewrite)
{
	const u8 *seg_data = NULL;
	u32 seg_size = 0;

	if (!s_findSegment("sequences", &seg_data, &seg_size)) {
		sysLogPrintf(LOG_NOTE,
			"romextract pdsong: \"sequences\" segment not loaded "
			"(server build or pre-romdata-init); skipping");
		return 0;
	}

	/* The segment starts with `struct seqtable` { u16 count; entries[1] }.
	 * preprocessSequences byte-swaps count + each entry's three fields
	 * to native at romdataInit time, so direct read is safe. The
	 * runtime DMAs the first 16 bytes then re-DMAs to get the count;
	 * we read the buffer directly here. */
	if (seg_size < sizeof(u16)) {
		sysLoudFailf("EXTRACT.PDSONG",
			"sequences segment too small (size=%u)",
			(unsigned)seg_size);
		return -1;
	}

	const struct seqtable *table = (const struct seqtable *)seg_data;
	u16 count = table->count;

	/* Sanity: bound count by what could possibly fit in the segment.
	 * struct seqtable's entries[] starts at offset sizeof(u16) (no
	 * struct padding to round up; the runtime allocates count *
	 * sizeof(seqtableentry) + 4 per src/lib/snd.c::sndLoad). */
	const u32 hdr_bytes = (u32)((const u8 *)&table->entries[0] - seg_data);
	const u32 max_by_size = (seg_size - hdr_bytes) / sizeof(struct seqtableentry);
	if ((u32)count > max_by_size) {
		sysLoudFailf("EXTRACT.PDSONG",
			"seqtable count=%u exceeds segment-derived cap %u "
			"(seg_size=%u hdr_bytes=%u)",
			(unsigned)count, (unsigned)max_by_size,
			(unsigned)seg_size, (unsigned)hdr_bytes);
		return -1;
	}

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDSONG", "fsDataDirEnsure failed");
		return -1;
	}

	char dataDirBuf[FS_MAXPATH + 1];
	char out_dir[FS_MAXPATH];
	snprintf(out_dir, sizeof(out_dir), "%s/%s",
		fsDataDir(dataDirBuf, sizeof(dataDirBuf)), PDSONG_OUT_DIR);
	if (!fsCreateDir(out_dir)) {
		sysLoudFailf("EXTRACT.PDSONG",
			"fsCreateDir(\"%s\") failed", out_dir);
		return -1;
	}

	s32 written = 0;
	s32 skipped = 0;
	s32 failed  = 0;

	for (s32 i = 0; i < count; i++) {
		s32 r = s_emitOneSong(i, &table->entries[i],
		                      seg_data, seg_size,
		                      out_dir, force_rewrite);
		if (r > 0)        written++;
		else if (r == 0)  skipped++;
		else              failed++;
	}

	sysLogPrintf(LOG_NOTE,
		"romextract pdsong: written=%d skipped=%d failed=%d "
		"total=%u (out=%s)",
		written, skipped, failed, (unsigned)count, out_dir);

	return written;
}
