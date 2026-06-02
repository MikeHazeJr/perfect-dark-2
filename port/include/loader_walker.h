/**
 * loader_walker.h -- Catalog universality pivot Step 4 (2026-05-03).
 *
 * Universal directory walker. Replaces hardcoded per-asset .pd<ext> envelope
 * scans as the primary catalog row registration path. Walks
 * data/<romid>/<class>/*.pd<ext> for the public typed archive families and
 * registers a catalog row per file via the existing assetCatalogRegister* API
 * using the human-readable catalog ID carried in each file's envelope.
 *
 * Original universality kinds:
 *   weapon mesh animation head body arena scenario
 *   sfx voice song ui font lang
 *
 * c3844 metadata-family extension:
 *   character skin prop vehicle mission gamemode botprofile hud
 *   effect material theme
 *
 * Boot order: must run AFTER assetCatalogRegisterBaseGame (so the in-binary
 * catalog rows exist as a baseline) AND AFTER the romextract_pd*.c emitters
 * (so the per-asset compounds have been written on first boot). Walker
 * registrations are last-write-wins, so they overlay the in-binary catalog
 * with the disk-derived rows -- the universality switch.
 *
 * the per-asset parser positioning (per Step 4 directive): loaderWalkerLoadAll +
 * loaderPoolFinalize remain in the boot path as the parity-bounded
 * fallback responsible for populating the heavyweight loader_pool
 * (s_Weapons[] full records, s_HeadsPool[], s_BodiesPool[], s_ArenasPool[]).
 * Step 5 retires the the legacy aggregate tier entirely once pool population also moves
 * onto a per-asset path; until then the walker handles row registration and
 * loader_pool fills directly from per-asset content.
 *
 * Server build: weapons / heads / bodies / arenas / etc. catalog rows
 * register identically server-side (server already participates in the
 * catalog for stage / map / mode / mod-distribution semantics). The walker
 * is unconditional; per-kind callbacks early-return if the file cannot be
 * parsed.
 *
 * Companion docs:
 *   context/designs/catalog/universality-pivot-schemas.md
 *   context/audits/catalog-universality-pivot-plan-2026-05-02.md
 */
#ifndef PD_LOADER_WALKER_H
#define PD_LOADER_WALKER_H

#include <PR/ultratypes.h>
#include "loader_walker_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Aggregate per-kind result counters from a full walker pass. */
typedef struct {
    s32 weapons_registered;
    s32 heads_registered;
    s32 bodies_registered;
    s32 arenas_registered;
    s32 meshes_registered;
    s32 animations_registered;
    s32 sfx_registered;
    s32 voices_registered;
    s32 songs_registered;
    s32 scenarios_registered;
    s32 uis_registered;
    s32 fonts_registered;
    s32 langs_registered;
    s32 metadata_registered;

    s32 total_files_scanned;
    s32 total_envelope_failures;
    s32 total_register_failures;
} loader_walker_result_t;

/* Walk every `data/<romid>/<class>/*.pd<ext>` for public typed archive
 * families and register a catalog row per file.  Dependency-ordered so
 * cross-references (e.g. arena -> scenario, weapon -> mesh) resolve
 * cleanly during a single pass.
 *
 * Idempotent: catalog rows are last-write-wins, so re-runs simply
 * refresh.  Returns 0 always; per-kind detail in *out (zeroed if NULL).
 */
s32 loaderWalkerLoadAll(loader_walker_result_t *out);

/* Returns 1 once at least one walker registration has succeeded.
 * Catalog managers + loader_pool fallback gates may consult this to decide
 * whether to defer to the walker output or re-register from in-binary
 * tables. Cleared only on full process restart. */
s32 loaderWalkerIsActive(void);

/* Per-kind walker entry points (internal, exposed for the dispatch
 * layer in loader_walker.c).  Each populates the per-kind result struct
 * for aggregation into loader_walker_result_t.  Call loaderWalkerScanKind
 * in their bodies. */
void loaderWalkerScanWeapons(const char *tier_dir, loader_walker_kind_result_t *out);
void loaderWalkerScanHeads(const char *tier_dir, loader_walker_kind_result_t *out);
void loaderWalkerScanBodies(const char *tier_dir, loader_walker_kind_result_t *out);
void loaderWalkerScanArenas(const char *tier_dir, loader_walker_kind_result_t *out);
void loaderWalkerScanMeshes(const char *tier_dir, loader_walker_kind_result_t *out);
void loaderWalkerScanAnimations(const char *tier_dir, loader_walker_kind_result_t *out);
void loaderWalkerScanSfx(const char *tier_dir, loader_walker_kind_result_t *out);
void loaderWalkerScanVoices(const char *tier_dir, loader_walker_kind_result_t *out);
void loaderWalkerScanSongs(const char *tier_dir, loader_walker_kind_result_t *out);
void loaderWalkerScanScenarios(const char *tier_dir, loader_walker_kind_result_t *out);
void loaderWalkerScanUi(const char *tier_dir, loader_walker_kind_result_t *out);
void loaderWalkerScanFonts(const char *tier_dir, loader_walker_kind_result_t *out);
void loaderWalkerScanLangs(const char *tier_dir, loader_walker_kind_result_t *out);

typedef struct {
    s32 entries_scanned;
    s32 entries_registered;
    s32 envelope_failures;
    s32 register_failures;
} loader_walker_metadata_result_t;

void loaderWalkerScanMetadataFamilies(const char *tier_dir,
    loader_walker_metadata_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PD_LOADER_WALKER_H */
