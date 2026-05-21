/**
 * romextract_pdbody.c -- Catalog universality pivot Step 2 (2026-05-03).
 *
 * Walks the loader_pool body pool and emits one zip-openable .pdbody
 * archive per registered body at data/<romid>/bodies/<id>.pdbody.
 *
 * Schema lock-down: context/designs/catalog/universality-pivot-schemas.md
 * Section 2.3 (.pdbody).
 *
 * Cross-reference convention for Step 2: `mesh` and `hand` fields
 * preserve the original FILE_* enum strings (resolved via reverse
 * lookup against loader_enum_reverse.c). Step 4 (universal loader) will
 * swap these to catalog IDs once the directory walker is minting the
 * universal mapping. The Step 2 emit format is therefore intermediate
 * and identical to the per-record envelope content; this is intentional so
 * the parity check at Step 2 is clean.
 *
 * Bodies-only fields preserved per audit: canvaryheight (Skedar's
 * per-chr height variance), handfilenum (first-person hand model;
 * B-275 dependency), unk00_01 == 1 sentinel for integrated-head bodies
 * (Skedar / Dr Caroll / EyeSpy; bodyAllocateModel warning gate at
 * body.c:417).
 *
 * Server build: emitter early-returns 0 (loader not active server-side).
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
#include "loader_enum_reverse.h"
#include "modarchive.h"
#include "romextract_pd.h"
#include "system.h"
#include "bodydata_authored.h"

/* Convert "base:dark_combat" -> "base_dark_combat". */
static void s_idToFilename(const char *id, char *out, size_t n)
{
	if (!id || !out || n == 0) { if (out && n) out[0] = '\0'; return; }
	size_t i;
	for (i = 0; i + 1 < n && id[i]; i++) {
		out[i] = (id[i] == ':') ? '_' : id[i];
	}
	out[i] = '\0';
}

static s32 s_existingZipArchive(const char *relpath)
{
	u32 size = 0;
	void *bytes = fsFileLoad(relpath, &size);
	if (!bytes) return 0;
	s32 is_zip = size >= 2
		&& ((const u8 *)bytes)[0] == 'P'
		&& ((const u8 *)bytes)[1] == 'K';
	sysMemFree(bytes);
	return is_zip;
}

/* Emit one zip-openable .pdbody archive. Returns 1 written, 0 skipped,
 * -1 failed. */
static s32 s_emitOneBody(const body_authored_record_t *b,
                          const char *out_dir, s32 force_rewrite)
{
	const char *catalog_id = b->catalog_id;
	if (!catalog_id || !catalog_id[0]) return 0;

	char filename[128];
	s_idToFilename(catalog_id, filename, sizeof(filename));

	char relpath[FS_MAXPATH];
	snprintf(relpath, sizeof(relpath), "%s/%s.pdbody", out_dir, filename);

	if (!force_rewrite && fsFileSize(relpath) > 0 && s_existingZipArchive(relpath)) {
		return 0;
	}

	const char *type_str = loaderEnumNameForHeadbodyType(b->type);
	const char *mesh_str = loaderEnumNameForFileEnum(b->filenum);
	const char *hand_str = loaderEnumNameForFileEnum(b->handfilenum);

	char manifest_buf[1200];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"body\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"bodynum\": %d,\n"
		"  \"ismale\": %u,\n"
		"  \"unk00_01\": %u,\n"
		"  \"canvaryheight\": %u,\n",
		catalog_id, (s32)b->bodynum, (unsigned)b->ismale,
		(unsigned)b->unk00_01, (unsigned)b->canvaryheight);
	if (type_str) {
		manifest_len += snprintf(manifest_buf + manifest_len,
			sizeof(manifest_buf) - manifest_len,
			"  \"type\": \"%s\",\n", type_str);
	} else {
		manifest_len += snprintf(manifest_buf + manifest_len,
			sizeof(manifest_buf) - manifest_len,
			"  \"type\": %u,\n", (unsigned)b->type);
	}
	manifest_len += snprintf(manifest_buf + manifest_len,
		sizeof(manifest_buf) - manifest_len,
		"  \"height\": %u,\n",
		(unsigned)b->height);
	if (mesh_str) {
		manifest_len += snprintf(manifest_buf + manifest_len,
			sizeof(manifest_buf) - manifest_len,
			"  \"mesh\": \"%s\",\n", mesh_str);
	} else {
		manifest_len += snprintf(manifest_buf + manifest_len,
			sizeof(manifest_buf) - manifest_len,
			"  \"mesh\": %u,\n", (unsigned)b->filenum);
	}
	manifest_len += snprintf(manifest_buf + manifest_len,
		sizeof(manifest_buf) - manifest_len,
		"  \"scale\": %.7g,\n"
		"  \"animscale\": %.7g,\n",
		(double)b->scale, (double)b->animscale);
	if (b->handfilenum != 0) {
		if (hand_str) {
			manifest_len += snprintf(manifest_buf + manifest_len,
				sizeof(manifest_buf) - manifest_len,
				"  \"hand\": \"%s\"\n", hand_str);
		} else {
			manifest_len += snprintf(manifest_buf + manifest_len,
				sizeof(manifest_buf) - manifest_len,
				"  \"hand\": %u\n", (unsigned)b->handfilenum);
		}
	} else {
		manifest_len += snprintf(manifest_buf + manifest_len,
			sizeof(manifest_buf) - manifest_len,
			"  \"hand\": null\n");
	}
	manifest_len += snprintf(manifest_buf + manifest_len,
		sizeof(manifest_buf) - manifest_len, "}\n");
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDBODY",
			"manifest snprintf truncated for \"%s\"", catalog_id);
		return -1;
	}

	char ini_buf[1200];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[body]\n"
		"catalog_id = %s\n"
		"bodynum = %d\n"
		"ismale = %u\n"
		"unk00_01 = %u\n"
		"canvaryheight = %u\n"
		"type = %s\n"
		"height = %u\n"
		"mesh = %s\n"
		"scale = %.7g\n"
		"animscale = %.7g\n",
		catalog_id, (s32)b->bodynum, (unsigned)b->ismale,
		(unsigned)b->unk00_01, (unsigned)b->canvaryheight,
		type_str ? type_str : "", (unsigned)b->height,
		mesh_str ? mesh_str : "", (double)b->scale, (double)b->animscale);
	if (b->handfilenum != 0) {
		ini_len += snprintf(ini_buf + ini_len, sizeof(ini_buf) - ini_len,
			"hand = %s\n", hand_str ? hand_str : "");
	} else {
		ini_len += snprintf(ini_buf + ini_len, sizeof(ini_buf) - ini_len,
			"hand = \n");
	}
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		sysLoudFailf("EXTRACT.PDBODY",
			"body.ini snprintf truncated for \"%s\"", catalog_id);
		return -1;
	}

	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) {
		sysLoudFailf("EXTRACT.PDBODY",
			"fsFullPath failed for \"%s\"", relpath);
		return -1;
	}
	mod_archive_writer_t *aw = modArchiveBegin(full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDBODY",
			"modArchiveBegin failed for \"%s\"", full);
		return -1;
	}
	if (modArchiveAddFileMem(aw, "body.ini", ini_buf, (u32)ini_len) != 0) {
		sysLoudFailf("EXTRACT.PDBODY",
			"AddFileMem body.ini failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveAddFileMem(aw, "manifest.json",
	                          manifest_buf, (u32)manifest_len) != 0) {
		sysLoudFailf("EXTRACT.PDBODY",
			"AddFileMem manifest.json failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDBODY",
			"modArchiveFinish failed for \"%s\"", full);
		return -1;
	}
	return 1;
}

/* Engine Phase 4: per-body fan-out context. */
typedef struct {
	const char  *bodies_dir;
	s32          force_rewrite;
	s32          count;
	SDL_atomic_t written;
	SDL_atomic_t skipped;
	SDL_atomic_t failed;
	SDL_atomic_t processed;
} pdbody_fanout_ctx_t;

static void s_pdbodyWork(int i, void *user)
{
	pdbody_fanout_ctx_t *c = (pdbody_fanout_ctx_t *)user;
	if (i < 0 || i >= c->count) return;

	s32 r = s_emitOneBody(&g_BodyData[i], c->bodies_dir, c->force_rewrite);
	if (r > 0)       SDL_AtomicAdd(&c->written, 1);
	else if (r == 0) SDL_AtomicAdd(&c->skipped, 1);
	else             SDL_AtomicAdd(&c->failed,  1);

	int done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if ((done & 0x07) == 0 || done == c->count) {
		bootProgressUpdate(done, c->count);
	}
}

s32 romExtractAllPdbody(s32 force_rewrite)
{
	/* BYOR completion (2026-05-03): walks g_BodyData[] from the
	 * authoring source-of-truth (port/src/bodydata_authored.c). */

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDBODY",
			"fsDataDirEnsure failed; cannot create output dir");
		return -1;
	}

	char dataDirBuf[FS_MAXPATH + 1];
	char bodies_dir[FS_MAXPATH];
	snprintf(bodies_dir, sizeof(bodies_dir), "%s/bodies",
		fsDataDir(dataDirBuf, sizeof(dataDirBuf)));
	if (!fsCreateDir(bodies_dir)) {
		sysLoudFailf("EXTRACT.PDBODY",
			"fsCreateDir(\"%s\") failed", bodies_dir);
		return -1;
	}

	pdbody_fanout_ctx_t bctx;
	memset(&bctx, 0, sizeof(bctx));
	bctx.bodies_dir    = bodies_dir;
	bctx.force_rewrite = force_rewrite;
	bctx.count         = g_BodyDataCount;
	SDL_AtomicSet(&bctx.written,   0);
	SDL_AtomicSet(&bctx.skipped,   0);
	SDL_AtomicSet(&bctx.failed,    0);
	SDL_AtomicSet(&bctx.processed, 0);

	bootProgressUpdate(0, g_BodyDataCount);
	bootPoolForRangeBlocking(0, g_BodyDataCount, s_pdbodyWork, &bctx);
	bootProgressUpdate(g_BodyDataCount, g_BodyDataCount);

	s32 written = SDL_AtomicGet(&bctx.written);
	s32 skipped = SDL_AtomicGet(&bctx.skipped);
	s32 failed  = SDL_AtomicGet(&bctx.failed);

	sysLogPrintf(LOG_NOTE,
		"romextract pdbody: written=%d skipped=%d failed=%d total=%d",
		written, skipped, failed, g_BodyDataCount);

	return written;
}
