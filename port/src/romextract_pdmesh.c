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
#include "weapondata_authored.h"
#include "headdata_authored.h"
#include "bodydata_authored.h"

/* Track filenums already emitted to avoid duplicate work when multiple
 * weapons / heads / bodies share a mesh. Cap covers ~86 weapons * 2
 * (hi + lo) + 84 head meshes + 68 body meshes + 68 hand meshes plus
 * dedup headroom. */
#define ROMEXTRACT_PDMESH_SEEN_CAP 512
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

/* Emit one .pdmesh compound. Returns 1 written, 0 skipped, -1 failed.
 *
 * Engine Phase 4: dedup is now done UPSTREAM by the collect-phase in
 * romExtractAllPdmesh; this function no longer touches s_SeenFilenums.
 * Callers from parallel workers can invoke this safely on disjoint
 * (filenum, hint) tuples. */
static s32 s_emitOneMesh(u16 filenum, const char *hint_suffix,
                          const char *out_dir, s32 force_rewrite)
{
	if (filenum == 0) return 0;

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
	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
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

	char src_full_buf[FS_MAXPATH + 1];
	const char *src_full = fsFullPath(src_rel, src_full_buf, sizeof(src_full_buf));
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

/* Engine Phase 4: collect-then-emit pattern.  Phase 1 walks the
 * authoring tables single-threaded, dedups by filenum into a work
 * queue.  Phase 2 fans the unique (filenum, hint) tuples across the
 * boot pool.
 *
 * Dedup happens upstream so each worker writes a unique slug; ZIP
 * writes are independent and thread-safe. */

typedef struct {
	u16  filenum;
	char hint[8];   /* "hi", "lo", "hand", or "" */
} pdmesh_work_t;

typedef struct {
	const pdmesh_work_t *jobs;
	s32                  count;
	const char          *out_dir;
	s32                  force_rewrite;
	SDL_atomic_t         written;
	SDL_atomic_t         skipped;
	SDL_atomic_t         failed;
	SDL_atomic_t         processed;
} pdmesh_fanout_ctx_t;

static void s_pdmeshWork(int idx, void *user)
{
	pdmesh_fanout_ctx_t *c = (pdmesh_fanout_ctx_t *)user;
	if (idx < 0 || idx >= c->count) return;

	const pdmesh_work_t *j = &c->jobs[idx];
	const char *hint = j->hint[0] ? j->hint : NULL;
	s32 r = s_emitOneMesh(j->filenum, hint, c->out_dir, c->force_rewrite);
	if (r > 0)       SDL_AtomicAdd(&c->written, 1);
	else if (r == 0) SDL_AtomicAdd(&c->skipped, 1);
	else             SDL_AtomicAdd(&c->failed,  1);

	int done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if ((done & 0x0f) == 0 || done == c->count) {
		bootProgressUpdate(done, c->count);
	}
}

static s32 s_pdmeshAddWork(pdmesh_work_t *jobs, s32 *job_count, s32 cap,
                            u16 filenum, const char *hint)
{
	if (filenum == 0) return 0;
	/* Dedup: linear scan; the cap is ~512 so this stays cheap. */
	for (s32 i = 0; i < *job_count; i++) {
		if (jobs[i].filenum == filenum) return 0;
	}
	if (*job_count >= cap) return 0;
	jobs[*job_count].filenum = filenum;
	jobs[*job_count].hint[0] = '\0';
	if (hint) {
		size_t hlen = strlen(hint);
		if (hlen >= sizeof(jobs[*job_count].hint)) {
			hlen = sizeof(jobs[*job_count].hint) - 1;
		}
		memcpy(jobs[*job_count].hint, hint, hlen);
		jobs[*job_count].hint[hlen] = '\0';
	}
	(*job_count)++;
	return 1;
}

s32 romExtractAllPdmesh(s32 force_rewrite)
{
	/* BYOR completion (2026-05-03): walks weapon hi/lo + head mesh +
	 * body mesh + body hand mesh from the authoring source-of-truth.
	 * Per schema 2.5, every .pdwpn / .pdhead / .pdbody / .pdbody hand
	 * field references a .pdmesh entry; this single emitter produces
	 * the full superset so cross-references resolve. */

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDMESH", "fsDataDirEnsure failed");
		return -1;
	}

	char dataDirBuf[FS_MAXPATH + 1];
	char meshes_dir[FS_MAXPATH];
	snprintf(meshes_dir, sizeof(meshes_dir), "%s/meshes",
		fsDataDir(dataDirBuf, sizeof(dataDirBuf)));
	if (!fsCreateDir(meshes_dir)) {
		sysLoudFailf("EXTRACT.PDMESH",
			"fsCreateDir(\"%s\") failed", meshes_dir);
		return -1;
	}

	/* Phase 1: collect unique (filenum, hint) tuples single-threaded.
	 * Cap matches the pre-Phase-4 dedup cap. */
	pdmesh_work_t jobs[ROMEXTRACT_PDMESH_SEEN_CAP];
	s32 job_count = 0;

	for (s32 i = 0; i < g_WeaponDataCount; i++) {
		const struct weapon *wpn = g_WeaponData[i];
		if (!wpn) continue;
		s_pdmeshAddWork(jobs, &job_count, ROMEXTRACT_PDMESH_SEEN_CAP,
		                wpn->hi_model, "hi");
		s_pdmeshAddWork(jobs, &job_count, ROMEXTRACT_PDMESH_SEEN_CAP,
		                wpn->lo_model, "lo");
	}
	for (s32 i = 0; i < g_HeadDataCount; i++) {
		s_pdmeshAddWork(jobs, &job_count, ROMEXTRACT_PDMESH_SEEN_CAP,
		                g_HeadData[i].filenum, NULL);
	}
	for (s32 i = 0; i < g_BodyDataCount; i++) {
		s_pdmeshAddWork(jobs, &job_count, ROMEXTRACT_PDMESH_SEEN_CAP,
		                g_BodyData[i].filenum, NULL);
		if (g_BodyData[i].handfilenum != 0) {
			s_pdmeshAddWork(jobs, &job_count, ROMEXTRACT_PDMESH_SEEN_CAP,
			                g_BodyData[i].handfilenum, "hand");
		}
	}

	/* Phase 2: fan out the per-mesh emit work. */
	pdmesh_fanout_ctx_t mctx;
	memset(&mctx, 0, sizeof(mctx));
	mctx.jobs          = jobs;
	mctx.count         = job_count;
	mctx.out_dir       = meshes_dir;
	mctx.force_rewrite = force_rewrite;
	SDL_AtomicSet(&mctx.written,   0);
	SDL_AtomicSet(&mctx.skipped,   0);
	SDL_AtomicSet(&mctx.failed,    0);
	SDL_AtomicSet(&mctx.processed, 0);

	bootProgressUpdate(0, job_count);
	bootPoolForRangeBlocking(0, job_count, s_pdmeshWork, &mctx);
	bootProgressUpdate(job_count, job_count);

	s32 written = SDL_AtomicGet(&mctx.written);
	s32 skipped = SDL_AtomicGet(&mctx.skipped);
	s32 failed  = SDL_AtomicGet(&mctx.failed);

	/* Maintain the legacy s_SeenCount log field for diff parity. */
	s_SeenCount = job_count;

	sysLogPrintf(LOG_NOTE,
		"romextract pdmesh: written=%d skipped=%d failed=%d unique_filenums=%d",
		written, skipped, failed, s_SeenCount);

	return written;
}
