/**
 * romextract_pdhead.c -- Catalog universality pivot Step 2 (2026-05-03).
 *
 * Walks the loader_pool head pool and emits one zip-openable .pdhead
 * archive per registered head at data/<romid>/heads/<id>.pdhead.
 *
 * Schema lock-down: context/designs/catalog/universality-pivot-schemas.md
 * Section 2.2 (.pdhead).
 *
 * Cross-reference convention for Step 2: the `mesh` field preserves the
 * original FILE_* enum string (resolved via reverse lookup against
 * loader_enum_reverse.c). Step 4 (universal loader) will swap this to
 * a catalog ID once the directory walker is minting the universal
 * mapping. The Step 2 emit format is therefore intermediate and
 * identical to the per-record envelope content; this is intentional so the
 * parity check at Step 2 is clean.
 *
 * Server build: emitter early-returns 0 (loader not active server-side).
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
#include "modarchive.h"
#include "romextract_pd.h"
#include "system.h"
#include "headdata_authored.h"

/* Convert a catalog ID like "base:head_carrington" to a filename slug
 * "base_head_carrington". Caller buffer must hold at least 64 bytes. */
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

static void s_meshCatalogId(u16 filenum, const char *hint_suffix,
                            char *out, size_t n)
{
	if (!out || n == 0) return;
	const char *sym = loaderEnumNameForFileEnum(filenum);
	if (!sym) {
		snprintf(out, n, "base:rom_g_%04x", (unsigned)filenum);
		return;
	}
	const char *body = sym;
	if (strncmp(sym, "FILE_G", 6) == 0) body = sym + 6;
	else if (strncmp(sym, "FILE_", 5) == 0) body = sym + 5;

	char lowered[96];
	size_t i;
	for (i = 0; i + 1 < sizeof(lowered) && body[i]; i++) {
		lowered[i] = (char)tolower((unsigned char)body[i]);
	}
	lowered[i] = '\0';

	if (hint_suffix && hint_suffix[0]) snprintf(out, n, "base:%s_%s", lowered, hint_suffix);
	else snprintf(out, n, "base:%s", lowered);
}

static void s_meshArchiveRelPath(u16 filenum, const char *hint_suffix,
                                 char *out, size_t out_size)
{
	if (!out || out_size == 0) return;
	out[0] = '\0';
	if (filenum == 0) return;
	char mesh_id[128];
	char mesh_file[128];
	char data_dir[FS_MAXPATH + 1];
	s_meshCatalogId(filenum, hint_suffix, mesh_id, sizeof(mesh_id));
	s_idToFilename(mesh_id, mesh_file, sizeof(mesh_file));
	snprintf(out, out_size, "%s/meshes/%s.pdmesh",
		fsDataDir(data_dir, sizeof(data_dir)), mesh_file);
}

static s32 s_addRequiredArchiveFile(mod_archive_writer_t *aw, const char *inner,
                                    const char *relpath, const char *dst_full)
{
	if (!relpath || !relpath[0] || fsFileSize(relpath) <= 0) {
		sysLoudFailf("EXTRACT.PDHEAD",
			"required dependency %s missing for \"%s\" (rel=\"%s\")",
			inner ? inner : "(null)", dst_full ? dst_full : "(null)",
			relpath ? relpath : "(null)");
		return -1;
	}
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return -1;
	if (modArchiveAddFileDisk(aw, inner, full) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDHEAD",
			"AddFileDisk %s failed for \"%s\"", inner, dst_full);
		return -1;
	}
	return 0;
}

/* Emit one zip-openable .pdhead archive. Returns 1 written, 0 skipped,
 * -1 failed. */
static s32 s_emitOneHead(const head_authored_record_t *h,
                          const char *out_dir, s32 force_rewrite)
{
	const char *catalog_id = h->catalog_id;
	if (!catalog_id || !catalog_id[0]) return 0;

	char filename[128];
	s_idToFilename(catalog_id, filename, sizeof(filename));

	char relpath[FS_MAXPATH];
	snprintf(relpath, sizeof(relpath), "%s/%s.pdhead", out_dir, filename);

	if (!force_rewrite && fsFileSize(relpath) > 0 &&
	    s_existingZipArchive(relpath) &&
	    (h->filenum == 0 || s_existingArchiveHasEntry(relpath, "mesh.pdmesh"))) {
		return 0;
	}

	char mesh_rel[FS_MAXPATH];
	s_meshArchiveRelPath(h->filenum, NULL, mesh_rel, sizeof(mesh_rel));

	const char *type_str = loaderEnumNameForHeadbodyType(h->type);
	const char *file_str = loaderEnumNameForFileEnum(h->filenum);

	char manifest_buf[1024];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"head\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"headnum\": %d,\n"
		"  \"ismale\": %u,\n"
		"  \"unk00_01\": %u,\n",
		catalog_id, (s32)h->headnum, (unsigned)h->ismale,
		(unsigned)h->unk00_01);
	if (type_str) {
		manifest_len += snprintf(manifest_buf + manifest_len,
			sizeof(manifest_buf) - manifest_len,
			"  \"type\": \"%s\",\n", type_str);
	} else {
		manifest_len += snprintf(manifest_buf + manifest_len,
			sizeof(manifest_buf) - manifest_len,
			"  \"type\": %u,\n", (unsigned)h->type);
	}
	manifest_len += snprintf(manifest_buf + manifest_len,
		sizeof(manifest_buf) - manifest_len,
		"  \"height\": %u,\n",
		(unsigned)h->height);
	if (file_str) {
		manifest_len += snprintf(manifest_buf + manifest_len,
			sizeof(manifest_buf) - manifest_len,
			"  \"mesh\": \"%s\",\n", file_str);
	} else {
		manifest_len += snprintf(manifest_buf + manifest_len,
			sizeof(manifest_buf) - manifest_len,
			"  \"mesh\": %u,\n", (unsigned)h->filenum);
	}
	manifest_len += snprintf(manifest_buf + manifest_len,
		sizeof(manifest_buf) - manifest_len,
		h->filenum ? "  \"mesh_archive\": \"mesh.pdmesh\",\n"
		           : "  \"mesh_archive\": null,\n");
	manifest_len += snprintf(manifest_buf + manifest_len,
		sizeof(manifest_buf) - manifest_len,
		"  \"scale\": %.7g,\n"
		"  \"animscale\": %.7g\n"
		"}\n",
		(double)h->scale, (double)h->animscale);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDHEAD",
			"manifest snprintf truncated for \"%s\"", catalog_id);
		return -1;
	}

	char ini_buf[1024];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[head]\n"
		"catalog_id = %s\n"
		"headnum = %d\n"
		"ismale = %u\n"
		"unk00_01 = %u\n"
		"type = %s\n"
		"height = %u\n"
		"mesh = %s\n"
		"mesh_archive = %s\n"
		"scale = %.7g\n"
		"animscale = %.7g\n",
		catalog_id, (s32)h->headnum, (unsigned)h->ismale,
		(unsigned)h->unk00_01, type_str ? type_str : "",
		(unsigned)h->height, file_str ? file_str : "",
		h->filenum ? "mesh.pdmesh" : "",
		(double)h->scale, (double)h->animscale);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		sysLoudFailf("EXTRACT.PDHEAD",
			"head.ini snprintf truncated for \"%s\"", catalog_id);
		return -1;
	}

	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) {
		sysLoudFailf("EXTRACT.PDHEAD",
			"fsFullPath failed for \"%s\"", relpath);
		return -1;
	}
	mod_archive_writer_t *aw = modArchiveBegin(full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDHEAD",
			"modArchiveBegin failed for \"%s\"", full);
		return -1;
	}
	if (modArchiveAddFileMem(aw, "head.ini", ini_buf, (u32)ini_len) != 0) {
		sysLoudFailf("EXTRACT.PDHEAD",
			"AddFileMem head.ini failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveAddFileMem(aw, "manifest.json",
	                          manifest_buf, (u32)manifest_len) != 0) {
		sysLoudFailf("EXTRACT.PDHEAD",
			"AddFileMem manifest.json failed for \"%s\"", full);
		modArchiveAbort(aw);
		return -1;
	}
	if (h->filenum != 0 &&
	    s_addRequiredArchiveFile(aw, "mesh.pdmesh", mesh_rel, full) != 0) {
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDHEAD",
			"modArchiveFinish failed for \"%s\"", full);
		return -1;
	}
	return 1;
}

/* Engine Phase 4: per-head fan-out context. */
typedef struct {
	const char  *heads_dir;
	s32          force_rewrite;
	s32          count;
	SDL_atomic_t written;
	SDL_atomic_t skipped;
	SDL_atomic_t failed;
	SDL_atomic_t processed;
} pdhead_fanout_ctx_t;

static void s_pdheadWork(int i, void *user)
{
	pdhead_fanout_ctx_t *c = (pdhead_fanout_ctx_t *)user;
	if (i < 0 || i >= c->count) return;

	s32 r = s_emitOneHead(&g_HeadData[i], c->heads_dir, c->force_rewrite);
	if (r > 0)       SDL_AtomicAdd(&c->written, 1);
	else if (r == 0) SDL_AtomicAdd(&c->skipped, 1);
	else             SDL_AtomicAdd(&c->failed,  1);

	int done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if ((done & 0x07) == 0 || done == c->count) {
		bootProgressUpdate(done, c->count);
	}
}

s32 romExtractAllPdhead(s32 force_rewrite)
{
	/* BYOR completion (2026-05-03): walks g_HeadData[] from the
	 * authoring source-of-truth (port/src/headdata_authored.c). */

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDHEAD",
			"fsDataDirEnsure failed; cannot create output dir");
		return -1;
	}

	char dataDirBuf[FS_MAXPATH + 1];
	char heads_dir[FS_MAXPATH];
	snprintf(heads_dir, sizeof(heads_dir), "%s/heads",
		fsDataDir(dataDirBuf, sizeof(dataDirBuf)));
	if (!fsCreateDir(heads_dir)) {
		sysLoudFailf("EXTRACT.PDHEAD",
			"fsCreateDir(\"%s\") failed", heads_dir);
		return -1;
	}

	if (romExtractPdFastCacheCanSkip("pdhead", heads_dir,
			".pdhead", force_rewrite)) {
		bootProgressUpdate(g_HeadDataCount, g_HeadDataCount);
		sysLogPrintf(LOG_NOTE,
			"romextract pdhead: written=0 skipped=%d failed=0 total=%d (fast-cache)",
			g_HeadDataCount, g_HeadDataCount);
		return 0;
	}

	pdhead_fanout_ctx_t hctx;
	memset(&hctx, 0, sizeof(hctx));
	hctx.heads_dir     = heads_dir;
	hctx.force_rewrite = force_rewrite;
	hctx.count         = g_HeadDataCount;
	SDL_AtomicSet(&hctx.written,   0);
	SDL_AtomicSet(&hctx.skipped,   0);
	SDL_AtomicSet(&hctx.failed,    0);
	SDL_AtomicSet(&hctx.processed, 0);

	bootProgressUpdate(0, g_HeadDataCount);
	bootPoolForRangeBlocking(0, g_HeadDataCount, s_pdheadWork, &hctx);
	bootProgressUpdate(g_HeadDataCount, g_HeadDataCount);

	s32 written = SDL_AtomicGet(&hctx.written);
	s32 skipped = SDL_AtomicGet(&hctx.skipped);
	s32 failed  = SDL_AtomicGet(&hctx.failed);

	sysLogPrintf(LOG_NOTE,
		"romextract pdhead: written=%d skipped=%d failed=%d total=%d",
		written, skipped, failed, g_HeadDataCount);

	if (failed == 0) {
		romExtractPdFastCacheWrite("pdhead", heads_dir, ".pdhead");
	}

	return written;
}
