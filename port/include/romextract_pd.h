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

#ifdef __cplusplus
}
#endif

#endif /* _IN_ROMEXTRACT_PD_H */
