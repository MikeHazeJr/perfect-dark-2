/**
 * romextract_pdarena.c -- Catalog universality pivot Step 2 (2026-05-03).
 *
 * Walks the loader_pool arena pool and emits TWO files per registered
 * arena per Mike's Q-1 unified-scenario directive:
 *
 *   1. data/<romid>/arenas/<id>.pdarena
 *      JSON metadata document carrying arena_index / slug / category /
 *      stagenum / requirefeature / name_langid / load_mode + a
 *      `scenario` catalog ID reference to the .pdscenario below.
 *      Schema: universality-pivot-schemas.md Section 2.4.
 *
 *   2. data/<romid>/scenarios/<scenario_id>.pdscenario
 *      ZIP compound bundling geometry / tiles / pads / setup / mpsetup
 *      binaries plus a manifest.json envelope. UNIFIED per Q-1 (one
 *      file per stage; modder-friendly atomic distribution).
 *      Schema: universality-pivot-schemas.md Section 2.10.
 *
 * Boot order: must run AFTER stageTableInit (g_Stages populated) AND
 * loaderPoolFinalize (arena pool populated) AND
 * romExtractAllFiles (per-stage .bin files extracted to disk).
 *
 * Cross-reference convention for Step 2: the .pdarena `scenario` field
 * carries the catalog ID `base:scenario_<arena_slug>` matching the
 * .pdscenario this emitter produces. Arenas without a resolvable
 * stagetable entry (Random meta arenas with token stagenums
 * STAGE_MP_RANDOM_MULTI=0x02 / STAGE_MP_RANDOM_SOLO=0x03) get a
 * `scenario: null` and skip the .pdscenario emit.
 *
 * ARENA_LOADMODE_CANVAS invariant: preserved per-arena via the
 * load_mode field in the .pdarena JSON. The 14 Solo Missions arenas
 * still get .pdscenario emits because their stage files exist on disk
 * (they are rendered as static canvases at runtime by the menu UI but
 * the asset bytes are real).
 *
 * Server build: emitter early-returns 0 (arena loader not active
 * server-side; g_RomFile is freed; no stage files extracted).
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
#include "romextract.h"
#include "romextract_pd.h"
#include "sha256.h"
#include "system.h"
#include "game/stagetable.h"
#include "arenadata_authored.h"

/* Convert "base:arena_mp_skedar" -> "base_arena_mp_skedar". */
static void s_idToFilename(const char *id, char *out, size_t n)
{
	if (!id || !out || n == 0) { if (out && n) out[0] = '\0'; return; }
	size_t i;
	for (i = 0; i + 1 < n && id[i]; i++) {
		out[i] = (id[i] == ':') ? '_' : id[i];
	}
	out[i] = '\0';
}

/* Build the scenario catalog ID for an arena slug. The arena's
 * `scenario` field carries this string; the .pdscenario file lives at
 * data/<romid>/scenarios/<filename_slug>.pdscenario. */
static void s_scenarioCatalogIdFromSlug(const char *slug, char *out, size_t n)
{
	if (!slug || !out || n == 0) { if (out && n) out[0] = '\0'; return; }
	snprintf(out, n, "base:scenario_%s", slug);
}

/* Map an arena's category to the scenario `kind` string per Section
 * 2.10 of the schema. "Solo Missions" -> "solo"; everything else -> "mp"
 * (Dark / Classic / Bonus / Random). The schema also defines
 * "firingrange" and "coop" but those map to stages outside the arena
 * pool (firing range is the boot training stage; coop is a per-game
 * mode flag, not a per-arena attribute). */
static const char *s_kindFromCategory(const char *category)
{
	if (!category || !category[0]) return "mp";
	if (strcmp(category, "Solo Missions") == 0) return "solo";
	return "mp";
}

/* Compute SHA-256 of an on-disk file and add it as `<inner_name>.sha256`
 * inside the ZIP. Best-effort: missing source files just skip the sidecar. */
static void s_addSidecar(mod_archive_writer_t *aw, const char *src_rel,
                          const char *inner_name)
{
	u32 size = 0;
	void *bytes = fsFileLoad(src_rel, &size);
	if (!bytes) return;
	if (size > 0) {
		u8 digest[SHA256_DIGEST_SIZE];
		sha256Hash(bytes, (size_t)size, digest);
		char hex[SHA256_HEX_SIZE + 1];
		sha256ToHex(digest, hex);
		hex[SHA256_HEX_SIZE] = '\0';
		char sidecar[SHA256_HEX_SIZE + 2];
		snprintf(sidecar, sizeof(sidecar), "%s\n", hex);
		char sidecar_name[64];
		snprintf(sidecar_name, sizeof(sidecar_name), "%s.sha256", inner_name);
		(void)modArchiveAddFileMem(aw, sidecar_name,
			sidecar, (u32)strlen(sidecar));
	}
	sysMemFree(bytes);
}

/* Add a per-stage binary file from data/<romid>/files/<sanitized>.bin
 * into the open ZIP writer as <inner_name>. Returns 1 if added, 0 if
 * filenum is 0 / source missing, -1 on hard error. */
static s32 s_addStageBin(mod_archive_writer_t *aw, u16 filenum,
                          const char *inner_name)
{
	if (filenum == 0) return 0;

	char src_rel[FS_MAXPATH];
	if (romExtractRelPathForFilenum((s32)filenum, src_rel, sizeof(src_rel)) <= 0) {
		return 0;
	}
	if (fsFileSize(src_rel) <= 0) {
		sysLogPrintf(LOG_NOTE,
			"romextract pdarena: stage source missing for filenum=0x%04x (rel=\"%s\")",
			(unsigned)filenum, src_rel);
		return 0;
	}

	char src_full_buf[FS_MAXPATH + 1];
	const char *src_full = fsFullPath(src_rel, src_full_buf, sizeof(src_full_buf));
	if (!src_full || !src_full[0]) return -1;
	if (modArchiveAddFileDisk(aw, inner_name, src_full) != 0) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"AddFileDisk %s failed for \"%s\"", inner_name, src_full);
		return -1;
	}
	s_addSidecar(aw, src_rel, inner_name);
	return 1;
}

/* Emit one .pdarena JSON. Returns 1 written, 0 skipped, -1 failed. */
static s32 s_emitOnePdarena(const arena_authored_record_t *a, s32 arena_index,
                             const char *out_dir, s32 force_rewrite)
{
	const char *catalog_id = a->catalog_id;
	if (!catalog_id || !catalog_id[0]) return 0;

	char filename[128];
	s_idToFilename(catalog_id, filename, sizeof(filename));

	char relpath[FS_MAXPATH];
	snprintf(relpath, sizeof(relpath), "%s/%s.pdarena", out_dir, filename);

	if (!force_rewrite && fsFileSize(relpath) > 0) return 0;

	FILE *fp = fsFileOpenWrite(relpath);
	if (!fp) {
		sysLoudFailf("EXTRACT.PDARENA",
			"fsFileOpenWrite failed for \"%s\"", relpath);
		return -1;
	}

	char scenario_id[96];
	s32 stage_idx = stageGetIndex(a->stagenum);
	if (stage_idx >= 0) {
		s_scenarioCatalogIdFromSlug(a->slug, scenario_id, sizeof(scenario_id));
	} else {
		scenario_id[0] = '\0';
	}

	const char *load_mode_str = loaderEnumNameForArenaLoadMode(a->load_mode);

	fputs("{\n", fp);
	fputs("  \"pd_kind\": \"arena\",\n", fp);
	fputs("  \"pd_schema_version\": 1,\n", fp);
	fprintf(fp, "  \"id\": \"%s\",\n", catalog_id);
	fprintf(fp, "  \"arena_index\": %d,\n", arena_index);
	fprintf(fp, "  \"slug\": \"%s\",\n", a->slug);
	fprintf(fp, "  \"category\": \"%s\",\n", a->category);
	fprintf(fp, "  \"stagenum\": %d,\n", (s32)a->stagenum);
	fprintf(fp, "  \"requirefeature\": %u,\n", (unsigned)a->requirefeature);
	fprintf(fp, "  \"name_langid\": %d,\n", a->name_langid);
	if (load_mode_str) fprintf(fp, "  \"load_mode\": \"%s\",\n", load_mode_str);
	else               fprintf(fp, "  \"load_mode\": %u,\n", (unsigned)a->load_mode);
	if (scenario_id[0]) fprintf(fp, "  \"scenario\": \"%s\"\n", scenario_id);
	else                fputs("  \"scenario\": null\n", fp);
	fputs("}\n", fp);
	fclose(fp);
	return 1;
}

/* Emit one .pdscenario ZIP. Returns 1 written, 0 skipped, -1 failed. */
static s32 s_emitOnePdscenario(const arena_authored_record_t *a,
                                const char *out_dir, s32 force_rewrite)
{
	s32 stage_idx = stageGetIndex(a->stagenum);
	if (stage_idx < 0) {
		/* Arena's stagenum has no stagetable entry. Random meta arenas
		 * (STAGE_MP_RANDOM_MULTI / SOLO) hit this branch. The .pdarena's
		 * `scenario` field is null and no .pdscenario is emitted. */
		return 0;
	}

	const struct stagetableentry *st = stageGetEntry(stage_idx);
	if (!st) return 0;

	char scenario_id[96];
	s_scenarioCatalogIdFromSlug(a->slug, scenario_id, sizeof(scenario_id));

	char filename[128];
	s_idToFilename(scenario_id, filename, sizeof(filename));

	char dst_rel[FS_MAXPATH];
	snprintf(dst_rel, sizeof(dst_rel), "%s/%s.pdscenario", out_dir, filename);

	if (!force_rewrite && fsFileSize(dst_rel) > 0) return 0;

	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"fsFullPath failed for \"%s\"", dst_rel);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}

	/* Add binary payloads. Each helper returns 1 if added, 0 if absent
	 * (filenum 0 or source missing on disk), -1 on hard error. */
	s32 has_geometry = s_addStageBin(aw, st->bgfileid,     "geometry.bin");
	s32 has_tiles    = s_addStageBin(aw, st->tilefileid,   "tiles.bin");
	s32 has_pads     = s_addStageBin(aw, st->padsfileid,   "pads.bin");
	s32 has_setup    = s_addStageBin(aw, st->setupfileid,  "setup.bin");
	s32 has_mpsetup  = s_addStageBin(aw, st->mpsetupfileid, "mpsetup.bin");

	if (has_geometry < 0 || has_tiles < 0 || has_pads < 0 ||
	    has_setup < 0 || has_mpsetup < 0) {
		modArchiveAbort(aw);
		return -1;
	}

	/* Build manifest.json */
	const char *kind = s_kindFromCategory(a->category);

	char manifest_buf[1024];
	int n = 0;
	n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		"{\n"
		"  \"pd_kind\": \"scenario\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"kind\": \"%s\",\n"
		"  \"stagenum\": %d,\n"
		"  \"display_name\": \"%s\"",
		scenario_id, kind, (s32)a->stagenum, a->slug);

	if (has_geometry > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"geometry\": \"geometry.bin\"");
	if (has_tiles > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"tiles\": \"tiles.bin\"");
	if (has_pads > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"pads\": \"pads.bin\"");
	if (has_setup > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"setup\": \"setup.bin\"");
	if (has_mpsetup > 0) n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n,
		",\n  \"mpsetup\": \"mpsetup.bin\"");

	n += snprintf(manifest_buf + n, sizeof(manifest_buf) - n, "\n}\n");

	if (n <= 0 || (size_t)n >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"manifest snprintf truncated for \"%s\"", scenario_id);
		modArchiveAbort(aw);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "manifest.json",
	                          manifest_buf, (u32)n) != 0) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"AddFileMem manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		return -1;
	}

	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDSCENARIO",
			"modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}
	return 1;
}

/* Engine Phase 4: file-scope fan-out context for the dual-emit pdarena
 * + pdscenario walk. */
typedef struct {
	const char  *arenas_dir;
	const char  *scenarios_dir;
	s32          force_rewrite;
	s32          count;
	SDL_atomic_t arenas_written;
	SDL_atomic_t arenas_skipped;
	SDL_atomic_t arenas_failed;
	SDL_atomic_t scenarios_written;
	SDL_atomic_t scenarios_skipped;
	SDL_atomic_t scenarios_failed;
	SDL_atomic_t processed;
} pdarena_fanout_ctx_t;

static void s_pdarenaWork(int i, void *user)
{
	pdarena_fanout_ctx_t *c = (pdarena_fanout_ctx_t *)user;
	if (i < 0 || i >= c->count) return;

	const arena_authored_record_t *a = &g_ArenaData[i];

	s32 r1 = s_emitOnePdarena(a, i, c->arenas_dir, c->force_rewrite);
	if (r1 > 0)       SDL_AtomicAdd(&c->arenas_written, 1);
	else if (r1 == 0) SDL_AtomicAdd(&c->arenas_skipped, 1);
	else              SDL_AtomicAdd(&c->arenas_failed,  1);

	s32 r2 = s_emitOnePdscenario(a, c->scenarios_dir, c->force_rewrite);
	if (r2 > 0)       SDL_AtomicAdd(&c->scenarios_written, 1);
	else if (r2 == 0) SDL_AtomicAdd(&c->scenarios_skipped, 1);
	else              SDL_AtomicAdd(&c->scenarios_failed,  1);

	int done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if ((done & 0x07) == 0 || done == c->count) {
		bootProgressUpdate(done, c->count);
	}
}

s32 romExtractAllPdarena(s32 force_rewrite)
{
	/* BYOR completion (2026-05-03): walks g_ArenaData[] from the
	 * authoring source-of-truth (port/src/arenadata_authored.c). */

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDARENA",
			"fsDataDirEnsure failed; cannot create output dir");
		return -1;
	}

	char dataDirBuf[FS_MAXPATH + 1];
	const char *dataDir = fsDataDir(dataDirBuf, sizeof(dataDirBuf));

	char arenas_dir[FS_MAXPATH];
	snprintf(arenas_dir, sizeof(arenas_dir), "%s/arenas", dataDir);
	if (!fsCreateDir(arenas_dir)) {
		sysLoudFailf("EXTRACT.PDARENA",
			"fsCreateDir(\"%s\") failed", arenas_dir);
		return -1;
	}

	char scenarios_dir[FS_MAXPATH];
	snprintf(scenarios_dir, sizeof(scenarios_dir), "%s/scenarios", dataDir);
	if (!fsCreateDir(scenarios_dir)) {
		sysLoudFailf("EXTRACT.PDARENA",
			"fsCreateDir(\"%s\") failed", scenarios_dir);
		return -1;
	}

	pdarena_fanout_ctx_t actx;
	memset(&actx, 0, sizeof(actx));
	actx.arenas_dir    = arenas_dir;
	actx.scenarios_dir = scenarios_dir;
	actx.force_rewrite = force_rewrite;
	actx.count         = g_ArenaDataCount;
	SDL_AtomicSet(&actx.arenas_written,    0);
	SDL_AtomicSet(&actx.arenas_skipped,    0);
	SDL_AtomicSet(&actx.arenas_failed,     0);
	SDL_AtomicSet(&actx.scenarios_written, 0);
	SDL_AtomicSet(&actx.scenarios_skipped, 0);
	SDL_AtomicSet(&actx.scenarios_failed,  0);
	SDL_AtomicSet(&actx.processed,         0);

	bootProgressUpdate(0, g_ArenaDataCount);
	bootPoolForRangeBlocking(0, g_ArenaDataCount, s_pdarenaWork, &actx);
	bootProgressUpdate(g_ArenaDataCount, g_ArenaDataCount);

	s32 arenas_written    = SDL_AtomicGet(&actx.arenas_written);
	s32 arenas_skipped    = SDL_AtomicGet(&actx.arenas_skipped);
	s32 arenas_failed     = SDL_AtomicGet(&actx.arenas_failed);
	s32 scenarios_written = SDL_AtomicGet(&actx.scenarios_written);
	s32 scenarios_skipped = SDL_AtomicGet(&actx.scenarios_skipped);
	s32 scenarios_failed  = SDL_AtomicGet(&actx.scenarios_failed);

	sysLogPrintf(LOG_NOTE,
		"romextract pdarena: arenas written=%d skipped=%d failed=%d total=%d",
		arenas_written, arenas_skipped, arenas_failed, g_ArenaDataCount);
	sysLogPrintf(LOG_NOTE,
		"romextract pdscenario: written=%d skipped=%d failed=%d",
		scenarios_written, scenarios_skipped, scenarios_failed);

	return arenas_written + scenarios_written;
}
