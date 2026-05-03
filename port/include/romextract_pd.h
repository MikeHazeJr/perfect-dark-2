/**
 * romextract_pd.h -- Catalog universality pivot Step 1 (2026-05-02).
 *
 * Per-asset .pd* compound emitters. Walks the loader_pdbase pool
 * (populated by loaderPdbaseScan from base/weapons.pdbase) and writes
 * one .pdwpn JSON file per weapon, one .pdmesh ZIP compound per unique
 * weapon mesh, and one .pdanim JSON file per registered animation.
 *
 * Output paths under data/<romid>/:
 *   weapons/<id>.pdwpn       JSON metadata document
 *   meshes/<id>.pdmesh       ZIP compound (manifest.json + geometry.bin)
 *   animations/<id>.pdanim   JSON metadata document
 *
 * Where <id> is the catalog ID with the colon replaced by underscore.
 * For example "base:falcon2" -> "base_falcon2.pdwpn".
 *
 * Boot order requirement: must run AFTER loaderPdbaseBuildWeaponManager
 * (so loader pools are populated) AND AFTER romExtractAllFiles (so the
 * source .bin files exist on disk for .pdmesh repackaging).  See
 * port/src/main.c wiring near the loaderPdbaseScan block.
 *
 * Server build: each function returns 0 immediately (no client weapon
 * data on the server side; loader is not active).
 *
 * Companion docs:
 *   context/designs/catalog/universality-pivot-schemas.md   schemas
 *   context/audits/catalog-universality-pivot-plan-2026-05-02.md  plan
 */
#ifndef _IN_ROMEXTRACT_PD_H
#define _IN_ROMEXTRACT_PD_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Emit one .pdwpn JSON file for every registered weapon (86 expected).
 * Idempotent: skips files that already exist with non-zero size unless
 * force_rewrite is non-zero.  Per-file failures emit LOUDFAIL.EXTRACT.PDWPN
 * but do not abort the walk.
 *
 * Returns: count of files newly written; -1 on infrastructure failure
 * (data dir creation, etc.).
 */
s32 romExtractAllPdwpn(s32 force_rewrite);

/**
 * Emit one .pdmesh ZIP compound for every unique weapon mesh referenced
 * by hi_model / lo_model fields.  Reads byte payload from the existing
 * data/<romid>/files/<sanitized_rom_name>.bin produced by Pass A.2 of
 * the romextract pipeline; this function does NOT touch g_RomFile (which
 * is freed by romdataReleaseRom before the loader runs).
 *
 * Returns: count of compounds newly written; -1 on infrastructure failure.
 */
s32 romExtractAllPdmesh(s32 force_rewrite);

/**
 * Emit one .pdanim JSON file per registered animation in the loader
 * pool.  Each file carries category="weapon_animation" per
 * universality-pivot-schemas.md Section 2.6.
 *
 * Returns: count of files newly written; -1 on infrastructure failure.
 */
s32 romExtractAllPdanim(s32 force_rewrite);

/**
 * Step 1 parity check (Q-5 ruling).  After emission, re-parses each
 * .pdwpn file's envelope and key scalar fields, compares against the
 * loader pool that produced it, and emits LOADER.UNIVERSAL.PARITY_FAIL
 * on any disagreement.  This is a structural integrity check, not a
 * full round-trip test (full round-trip arrives at Step 4 with the
 * universal loader).
 *
 * Returns: count of weapons that failed parity (0 = pass).
 */
s32 romExtractParityCheckPdwpn(void);

/* ============================================================
 * Catalog universality pivot Step 2 (2026-05-03).
 *
 * Per-asset emitters for the metadata-class kinds beyond weapons:
 *   .pdhead     JSON metadata document (one per registered head)
 *   .pdbody     JSON metadata document (one per registered body)
 *   .pdarena    JSON metadata document (one per registered arena)
 *   .pdscenario ZIP compound (one per arena's playable stage,
 *                              UNIFIED per Q-1 -- bg + tiles + pads
 *                              + setup + mpsetup + manifest in one
 *                              ZIP)
 *
 * Output paths under data/<romid>/:
 *   heads/<id>.pdhead
 *   bodies/<id>.pdbody
 *   arenas/<id>.pdarena
 *   scenarios/<id>.pdscenario
 *
 * Where <id> is the catalog ID with the colon replaced by underscore.
 * For example "base:head_carrington" -> "base_head_carrington.pdhead".
 *
 * Boot order requirement: must run AFTER loaderPdbaseScan +
 * loaderPdbaseBuildHeadManager / BuildBodyManager / BuildArenaManager
 * (so the typed pools are populated) AND AFTER stageTableInit (so
 * g_Stages[] is populated for the .pdscenario emitter to read per-stage
 * file IDs from). Wire alongside Step 1 in port/src/main.c.
 *
 * Server build: each function returns 0 immediately (no character/arena
 * data on the server side; loader is not active).
 * ============================================================ */

/* Step 2: emit one .pdhead JSON file per registered head (152 slots,
 * ~30-40 standalone heads expected based on .pdbase contents).
 * Idempotent: skips files that already exist with non-zero size unless
 * force_rewrite is non-zero. Per-file failures emit
 * LOUDFAIL.EXTRACT.PDHEAD but do not abort the walk.
 *
 * Returns: count of files newly written; -1 on infrastructure failure
 * (data dir creation, etc.). */
s32 romExtractAllPdhead(s32 force_rewrite);

/* Step 2: emit one .pdbody JSON file per registered body (152 slots,
 * 68 expected based on .pdbase: 63 named bodies + 5 SP fallbacks).
 * Idempotent. Returns: count of files newly written; -1 on
 * infrastructure failure. */
s32 romExtractAllPdbody(s32 force_rewrite);

/* Step 2: emit one .pdarena JSON metadata file per registered arena
 * AND one .pdscenario ZIP compound per arena's playable stage.
 *
 * The .pdarena (Section 2.4) carries arena_index / slug / category /
 * stagenum / requirefeature / name_langid / load_mode and a `scenario`
 * catalog ID reference. The .pdscenario (Section 2.10) is the UNIFIED
 * ZIP per Q-1 carrying geometry / tiles / pads / setup / mpsetup
 * binaries plus a manifest.json.
 *
 * 47 arenas total (CATALOG_MGR_ARENA_COUNT). The CANVAS-mode arenas
 * (Solo Missions group) carry a stagenum of 0 and have no playable
 * scene files; for those the .pdarena still emits but the
 * .pdscenario is skipped (the .pdarena's `scenario` field is null).
 *
 * Idempotent. Returns: count of arena files newly written
 * (.pdarena + .pdscenario both count); -1 on infrastructure failure. */
s32 romExtractAllPdarena(s32 force_rewrite);

/* Step 2 parity checks (Q-5 ruling). Re-read each emitted file,
 * verify envelope + key scalar fields against the source loader pool,
 * emit LOADER.UNIVERSAL.PARITY_FAIL on any disagreement. Same
 * structural-integrity-only contract as romExtractParityCheckPdwpn;
 * full field-by-field round-trip arrives at Step 4. */
s32 romExtractParityCheckPdhead(void);
s32 romExtractParityCheckPdbody(void);
s32 romExtractParityCheckPdarena(void);

/* ============================================================
 * Catalog universality pivot Step 3a (2026-05-03).
 *
 * Character-animation emitter. Companion to romExtractAllPdanim
 * (Step 1, weapon-animation gunscript opcodes). Walks the
 * chr-animation lump exposed via _animationsTableRomStart /
 * _animationsTableRomEnd (post-preprocessAnimations, byte-swapped
 * native-endian) and emits one .pdanim ZIP compound per registered
 * chr animation at data/<romid>/animations/<id>.pdanim.
 *
 * Compound layout per universality-pivot-schemas.md Section 2.6:
 *   manifest.json     envelope + animation metadata + provenance
 *   frames.bin        contiguous header + frame bytes
 *                     (length = headerlen + numframes * bytesperframe)
 *   frames.bin.sha256 outer-file SHA-256 sidecar
 *
 * Catalog IDs derive from the loaderPdbaseNameForAnimEnum reverse
 * lookup over k_AnimEnum (port/src/loader_pdbase_enums.c). For named
 * animations (e.g. "ANIM_HEROHIT") the ID is "base:anim_herohit". For
 * unnamed slots (auto-named "ANIM_NNNN" hex) the ID is "base:anim_NNNN".
 *
 * Boot order requirement: must run AFTER romdataInit (which calls
 * preprocessAnimations and remaps _animationsTableRomStart/End to the
 * disk-migrated buffer per romdata.c::romdataReleaseRom, OR points
 * them into g_RomFile pre-Pass-C). EITHER pointer state is fine here:
 * the table bytes are byte-swapped at preprocess time before this
 * emitter sees them.
 *
 * Server build: returns 0 immediately (no segments populated; ROM not
 * loaded; nothing to extract).
 *
 * Per Mike's Q-3 ruling (2026-05-02): "DO NOT DEFER beyond the scope
 * of the catalog work. Catalog is not complete unless it is COMPLETE."
 * Step 3a closes that ruling -- the chr-animation lump becomes
 * per-asset .pdanim files alongside the Step 1 weapon-animation files.
 * ============================================================ */

/* Step 3a: emit one .pdanim ZIP compound per chr animation entry in
 * the segs/animations.bin lump.  Idempotent: skips files that already
 * exist with non-zero size unless force_rewrite is non-zero.  Per-file
 * failures emit LOUDFAIL.EXTRACT.PDANIM_CHR but do not abort the walk.
 *
 * Returns: count of compounds newly written; -1 on infrastructure
 * failure (data dir creation, segment lookup, etc.). */
s32 romExtractAllPdanimChr(s32 force_rewrite);

/* Step 3a parity check (Q-5 ruling). Re-reads each emitted compound's
 * manifest.json envelope + key scalar fields and verifies they round-
 * trip the source table entry. Structural integrity check; full
 * round-trip arrives at Step 4 with the universal loader.
 *
 * Returns: count of chr animations that failed parity (0 = pass). */
s32 romExtractParityCheckPdanimChr(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ROMEXTRACT_PD_H */
