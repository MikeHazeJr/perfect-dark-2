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
 * opcodes -- ZIP compound, category="weapon_animation"). This file
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
 * Total per-anim span = headerlen + numframes * bytesperframe. The extractor
 * emits those bytes as TSV rows so the base character animation archive stays
 * editable/openable rather than exposing an authored frames.bin blob.
 *
 * Companion docs:
 *   context/designs/catalog/universality-pivot-schemas.md   schemas
 *   context/audits/catalog-universality-pivot-plan-2026-05-02.md  plan
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
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

static s32 s_existingArchiveHasAnimPayloads(const char *relpath)
{
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return 0;
	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;
	s32 ok = modArchiveFindEntry(arc, "animation.ini") >= 0
	      && modArchiveFindEntry(arc, "manifest.json") >= 0
	      && modArchiveFindEntry(arc, "header.tsv") >= 0
	      && modArchiveFindEntry(arc, "frames.tsv") >= 0;
	modArchiveClose(arc);
	return ok;
}

typedef struct {
	char *data;
	u32   len;
	u32   cap;
} pdanim_textbuf_t;

static void s_textbufFree(pdanim_textbuf_t *b)
{
	if (b->data) free(b->data);
	memset(b, 0, sizeof(*b));
}

static s32 s_textbufReserve(pdanim_textbuf_t *b, u32 extra)
{
	if (extra > 0xffffffffu - b->len) return -1;
	u32 need = b->len + extra + 1;
	if (need <= b->cap) return 0;
	u32 cap = b->cap ? b->cap : 1024;
	while (cap < need) {
		if (cap > 0x80000000u) return -1;
		cap *= 2;
	}
	char *p = (char *)realloc(b->data, cap);
	if (!p) return -1;
	b->data = p;
	b->cap = cap;
	return 0;
}

static s32 s_textbufAppend(pdanim_textbuf_t *b, const char *s)
{
	u32 n = (u32)strlen(s);
	if (s_textbufReserve(b, n) != 0) return -1;
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
	return 0;
}

static s32 s_textbufAppendf(pdanim_textbuf_t *b, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	va_list ap2;
	va_copy(ap2, ap);
	int need = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (need < 0) {
		va_end(ap2);
		return -1;
	}
	if (s_textbufReserve(b, (u32)need) != 0) {
		va_end(ap2);
		return -1;
	}
	int wrote = vsnprintf(b->data + b->len, (size_t)need + 1, fmt, ap2);
	va_end(ap2);
	if (wrote != need) return -1;
	b->len += (u32)need;
	return 0;
}

static s32 s_buildHeaderTsv(const u8 *data, u32 header_len, pdanim_textbuf_t *out)
{
	if (s_textbufAppend(out, "offset\tbyte_hex\tbyte_dec\n") != 0) return -1;
	for (u32 i = 0; i < header_len; i++) {
		if (s_textbufAppendf(out, "%u\t%02x\t%u\n",
		                     (unsigned)i, (unsigned)data[i],
		                     (unsigned)data[i]) != 0) {
			return -1;
		}
	}
	return 0;
}

static s32 s_buildFramesTsv(const u8 *frames, u32 frame_count,
                            u32 bytes_per_frame, pdanim_textbuf_t *out)
{
	if (s_textbufAppend(out, "frame") != 0) return -1;
	for (u32 b = 0; b < bytes_per_frame; b++) {
		if (s_textbufAppendf(out, "\tb%02u", (unsigned)b) != 0) return -1;
	}
	if (s_textbufAppend(out, "\n") != 0) return -1;
	for (u32 f = 0; f < frame_count; f++) {
		if (s_textbufAppendf(out, "%u", (unsigned)f) != 0) return -1;
		const u8 *row = frames + f * bytes_per_frame;
		for (u32 b = 0; b < bytes_per_frame; b++) {
			if (s_textbufAppendf(out, "\t%02x", (unsigned)row[b]) != 0) return -1;
		}
		if (s_textbufAppend(out, "\n") != 0) return -1;
	}
	return 0;
}

static s32 s_addShaSidecar(mod_archive_writer_t *aw, const char *name,
                           const void *data, u32 len)
{
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(data, (size_t)len, digest);
	char hex[SHA256_HEX_SIZE + 1];
	sha256ToHex(digest, hex);
	hex[SHA256_HEX_SIZE] = '\0';
	char sidecar[SHA256_HEX_SIZE + 2];
	snprintf(sidecar, sizeof(sidecar), "%s\n", hex);
	return modArchiveAddFileMem(aw, name, sidecar, (u32)strlen(sidecar));
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

	if (!force_rewrite && fsFileSize(dst_rel) > 0 &&
	    s_existingArchiveHasAnimPayloads(dst_rel)) return 0;

	pdanim_textbuf_t header_tsv;
	pdanim_textbuf_t frames_tsv;
	memset(&header_tsv, 0, sizeof(header_tsv));
	memset(&frames_tsv, 0, sizeof(frames_tsv));
	if (s_buildHeaderTsv(seg_data + entry->data,
	                     (u32)entry->headerlen, &header_tsv) != 0 ||
	    s_buildFramesTsv(seg_data + entry->data + entry->headerlen,
	                     (u32)entry->numframes,
	                     (u32)entry->bytesperframe, &frames_tsv) != 0) {
		s_textbufFree(&header_tsv);
		s_textbufFree(&frames_tsv);
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"failed to build TSV payloads for anim_idx=%d", anim_idx);
		return -1;
	}

	/* Build manifest.json text in memory. Schema fields per Section 2.6
	 * + provenance hints (source_offset, source_index) for the parity
	 * check and Step 4 round-trip. */
	const char *sym = loaderEnumNameForAnimEnum(anim_idx);
	char manifest_buf[1152];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"animation\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"category\": \"character_animation\",\n"
		"  \"header\": \"header.tsv\",\n"
		"  \"frames\": \"frames.tsv\",\n"
		"  \"frame_count\": %u,\n"
		"  \"bytes_per_frame\": %u,\n"
		"  \"header_len\": %u,\n"
		"  \"header_size\": %u,\n"
		"  \"frames_size\": %u,\n"
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
		(unsigned)header_tsv.len,
		(unsigned)frames_tsv.len,
		(unsigned)entry->framelen,
		(unsigned)entry->flags,
		anim_idx,
		(unsigned)entry->data,
		sym ? sym : "");
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		s_textbufFree(&header_tsv);
		s_textbufFree(&frames_tsv);
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"manifest.json snprintf truncated for anim_idx=%d", anim_idx);
		return -1;
	}

	char ini_buf[1024];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[animation]\n"
		"catalog_id = %s\n"
		"category = character_animation\n"
		"header_file = header.tsv\n"
		"frames_file = frames.tsv\n"
		"frame_count = %u\n"
		"bytes_per_frame = %u\n"
		"header_len = %u\n"
		"header_size = %u\n"
		"frames_size = %u\n"
		"framelen = %u\n"
		"flags = %u\n"
		"source_index = %d\n"
		"source_offset = %u\n"
		"source_symbol = %s\n",
		catalog_id,
		(unsigned)entry->numframes,
		(unsigned)entry->bytesperframe,
		(unsigned)entry->headerlen,
		(unsigned)header_tsv.len,
		(unsigned)frames_tsv.len,
		(unsigned)entry->framelen,
		(unsigned)entry->flags,
		anim_idx,
		(unsigned)entry->data,
		sym ? sym : "");
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		s_textbufFree(&header_tsv);
		s_textbufFree(&frames_tsv);
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"animation.ini snprintf truncated for anim_idx=%d", anim_idx);
		return -1;
	}

	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		s_textbufFree(&header_tsv);
		s_textbufFree(&frames_tsv);
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"fsFullPath failed for \"%s\"", dst_rel);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		s_textbufFree(&header_tsv);
		s_textbufFree(&frames_tsv);
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "animation.ini", ini_buf, (u32)ini_len) != 0) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"AddFileMem animation.ini failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&header_tsv);
		s_textbufFree(&frames_tsv);
		return -1;
	}
	if (modArchiveAddFileMem(aw, "manifest.json",
	                          manifest_buf, (u32)manifest_len) != 0) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"AddFileMem manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_textbufFree(&header_tsv);
		s_textbufFree(&frames_tsv);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "header.tsv",
	                          header_tsv.data, header_tsv.len) != 0) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"AddFileMem header.tsv failed for \"%s\" "
			"(anim_idx=%d len=%u)",
			dst_full, anim_idx, (unsigned)header_tsv.len);
		modArchiveAbort(aw);
		s_textbufFree(&header_tsv);
		s_textbufFree(&frames_tsv);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "frames.tsv",
	                          frames_tsv.data, frames_tsv.len) != 0) {
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"AddFileMem frames.tsv failed for \"%s\" "
			"(anim_idx=%d len=%u)",
			dst_full, anim_idx, (unsigned)frames_tsv.len);
		modArchiveAbort(aw);
		s_textbufFree(&header_tsv);
		s_textbufFree(&frames_tsv);
		return -1;
	}

	if (s_addShaSidecar(aw, "header.tsv.sha256",
	                    header_tsv.data, header_tsv.len) != 0 ||
	    s_addShaSidecar(aw, "frames.tsv.sha256",
	                    frames_tsv.data, frames_tsv.len) != 0) {
		sysLogPrintf(LOG_WARNING,
			"romextract pdanim_chr: sidecar write failed for \"%s\" "
			"(anim_idx=%d)", dst_full, anim_idx);
	}

	if (modArchiveFinish(aw) != 0) {
		s_textbufFree(&header_tsv);
		s_textbufFree(&frames_tsv);
		sysLoudFailf("EXTRACT.PDANIM_CHR",
			"modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}

	s_textbufFree(&header_tsv);
	s_textbufFree(&frames_tsv);
	return 1;
}

/* Engine Phase 4: file-scope fan-out context for chr animations. */
typedef struct {
	const struct animtableentry *entries;
	const u8                    *seg_data;
	u32                          seg_size;
	const char                  *anims_dir;
	s32                          force_rewrite;
	s32                          count;
	SDL_atomic_t                 written;
	SDL_atomic_t                 skipped;
	SDL_atomic_t                 failed;
	SDL_atomic_t                 named;
	SDL_atomic_t                 processed;
} pdanim_chr_fanout_ctx_t;

static void s_pdanimChrWork(int i, void *user)
{
	pdanim_chr_fanout_ctx_t *c = (pdanim_chr_fanout_ctx_t *)user;
	if (i < 0 || i >= c->count) return;

	s32 r = s_emitOneChrAnim(i, &c->entries[i], c->seg_data, c->seg_size,
	                         c->anims_dir, c->force_rewrite);
	if (r > 0)       SDL_AtomicAdd(&c->written, 1);
	else if (r == 0) SDL_AtomicAdd(&c->skipped, 1);
	else             SDL_AtomicAdd(&c->failed,  1);

	if (loaderEnumNameForAnimEnum(i)) SDL_AtomicAdd(&c->named, 1);

	int done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if ((done & 0x3f) == 0 || done == c->count) {
		bootProgressUpdate(done, c->count);
	}
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

	char dataDirBuf[FS_MAXPATH + 1];
	char anims_dir[FS_MAXPATH];
	snprintf(anims_dir, sizeof(anims_dir), "%s/animations",
		fsDataDir(dataDirBuf, sizeof(dataDirBuf)));
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

	pdanim_chr_fanout_ctx_t cctx;
	memset(&cctx, 0, sizeof(cctx));
	cctx.entries       = entries;
	cctx.seg_data      = seg_data;
	cctx.seg_size      = seg_size;
	cctx.anims_dir     = anims_dir;
	cctx.force_rewrite = force_rewrite;
	cctx.count         = (s32)anim_count;
	SDL_AtomicSet(&cctx.written,   0);
	SDL_AtomicSet(&cctx.skipped,   0);
	SDL_AtomicSet(&cctx.failed,    0);
	SDL_AtomicSet(&cctx.named,     0);
	SDL_AtomicSet(&cctx.processed, 0);

	bootProgressUpdate(0, (s32)anim_count);
	bootPoolForRangeBlocking(0, (int)anim_count, s_pdanimChrWork, &cctx);
	bootProgressUpdate((s32)anim_count, (s32)anim_count);

	s32 written = SDL_AtomicGet(&cctx.written);
	s32 skipped = SDL_AtomicGet(&cctx.skipped);
	s32 failed  = SDL_AtomicGet(&cctx.failed);
	s32 named   = SDL_AtomicGet(&cctx.named);

	sysLogPrintf(LOG_NOTE,
		"romextract pdanim_chr: written=%d skipped=%d failed=%d "
		"total=%u named=%d (anim_chr lump emitted to %s)",
		written, skipped, failed, (unsigned)anim_count, named, anims_dir);

	return written;
}
