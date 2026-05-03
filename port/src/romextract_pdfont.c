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
 * Per universality-pivot-schemas.md Section 2.12:
 *   manifest.json        envelope + face metadata + provenance
 *   data.bin             raw font segment bytes (pre-preprocessFont)
 *   data.bin.sha256      outer-file SHA-256 sidecar
 *
 * Catalog ID convention (feedback_human_readable_ids):
 *   base:font_<facename>   e.g. base:font_handelgothicsm
 *
 * The byte payload is the RAW segment as extracted to disk by
 * Pass A (data/<romid>/segs/<facename>.bin). The runtime preprocess
 * pass (port/src/preprocess/segfonts.c::preprocessFont) byte-swaps
 * + repoints the in-memory buffer; the Step 4 universal loader runs
 * the same preprocess at load time. Emitting raw keeps the .pdfont
 * shape consistent with .pdmesh (also raw + post-load preprocess).
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

	if (!force_rewrite && fsFileSize(dst_rel) > 0) return 0;

	/* Manifest. Carries segment name as provenance so the loader can
	 * round-trip the catalog ID on the universality switch. */
	u32 src_size = (u32)fsFileSize(src_rel);

	char manifest_buf[768];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"font\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"face\": \"%s\",\n"
		"  \"data\": \"data.bin\",\n"
		"  \"data_size\": %u,\n"
		"  \"source_segment\": \"%s\"\n"
		"}\n",
		catalog_id, face, (unsigned)src_size, face);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDFONT",
			"manifest.json snprintf truncated for face=\"%s\"", face);
		return -1;
	}

	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		sysLoudFailf("EXTRACT.PDFONT",
			"fsFullPath empty for \"%s\"", dst_rel);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDFONT",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "manifest.json",
	                          manifest_buf, (u32)manifest_len) != 0) {
		sysLoudFailf("EXTRACT.PDFONT",
			"AddFileMem manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		return -1;
	}

	char src_full_buf[FS_MAXPATH + 1];
	const char *src_full = fsFullPath(src_rel, src_full_buf, sizeof(src_full_buf));
	if (!src_full || !src_full[0]) {
		sysLoudFailf("EXTRACT.PDFONT",
			"fsFullPath empty for source \"%s\"", src_rel);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveAddFileDisk(aw, "data.bin", src_full) != 0) {
		sysLoudFailf("EXTRACT.PDFONT",
			"AddFileDisk data.bin failed for face=\"%s\" -> \"%s\"",
			face, dst_full);
		modArchiveAbort(aw);
		return -1;
	}

	/* SHA-256 sidecar (matches .pdmesh / .pdsfx pattern). */
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
				"romextract pdfont: sidecar write failed for \"%s\"",
				dst_full);
		}
	}
	if (src_bytes) sysMemFree(src_bytes);

	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDFONT",
			"modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}

	return 1;
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
