/**
 * forge_level_io.h -- a level's placements in and out of the running game.
 *
 * forge_level_doc.h edits six texts and knows nothing about the game. This
 * is the part that does. It finds the archive a level in the catalog came
 * from, reads the six members out of the scenario inside it, and saves a
 * changed document as a level mod through the mod author, the same save the
 * other in-game creators use (context/designs/modding/forge-mode-scope.md,
 * step F2). Every step is logged as a FORGE.LEVEL.* line.
 */
#ifndef _IN_FORGE_LEVEL_IO_H
#define _IN_FORGE_LEVEL_IO_H

#include <stddef.h>
#include <PR/ultratypes.h>
#include "assetcatalog.h"
#include "forge_level_doc.h"
#include "forge_revision_store.h"
#include "grid_match_cache.h"
#include "grid_match_sources.h"
#include "mod_author.h"
#include "net/grid_match_result.h"
#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where an open level came from, which is what a save starts from. */
typedef struct forge_level_source {
	char arena_id[CATALOG_ID_LEN];
	char archive_path[MOD_AUTHOR_PATH_MAX];      /* .pdarena path or .pdmod::member.pdarena */
	char scenario_member[MOD_AUTHOR_PATH_MAX];   /* its scenario, as the arena names it */
	char archive_sha256[SHA256_HEX_SIZE];        /* exact local recovery source */
} forge_level_source_t;

/**
 * Opens the placements of the level `arena_id`, an arena in the catalog: a
 * base arena or a mod level, including a native arena inside a .pdmod.
 * Returns the document (free it with
 * forgeLevelDocFree) and fills `source`, or returns NULL with the reason in
 * `err` when the arena is unknown, keeps its scenario outside its own
 * archive, or holds files the document refuses.
 */
forge_level_doc_t *forgeLevelOpen(const char *arena_id, forge_level_source_t *source,
		char *err, size_t err_cap);

/* Read the bounded native document members from detached scenario bytes. */
forge_level_doc_t *forgeLevelReadScenarioDocument(const void *scenario, u32 scenario_size,
		char *err, size_t err_cap);

/**
 * Saves the document as the level mod `display_name`, made from the level it
 * was opened from plus what the document changed, and makes it active: it is
 * in the catalog under `result->catalog_id` when this returns. A level whose
 * template is a base arena holds data from the player's own game files and
 * is local only.
 *
 * The archive uses the template's other members and the exact document
 * members. This preserves undo after an earlier in-place save, including
 * when the next save uses a new name. `own_template` records an in-place save.
 *
 * Returns MOD_AUTHOR_OK, or the mod author's error code with the reason in
 * `err`.
 */
s32 forgeLevelSaveAs(forge_level_doc_t *doc, const forge_level_source_t *source,
		const char *display_name, mod_author_result_t *result, char *err, size_t err_cap);

/* Classification to carry forward when a native arena is cloned. */
const char *forgeLevelCloneProvenance(const char *arena_id, const char *archive);

/*
 * The level Forge has open. One at a time, owned here so the editor's panel,
 * the debug flags and later the Modding Hub's Levels tool all work on the
 * same document.
 */

/**
 * A Forge session started on this arena ("" or NULL when the session is not
 * on a level from the catalog). Any document still open is closed: unsaved
 * placements do not follow the player into another session.
 */
void forgeLevelSessionBegin(const char *arena_id);

/* The arena the current Forge session was started on, or "". */
const char *forgeLevelSessionArena(void);

/**
 * Opens the placements of `arena_id` as the session's document, replacing
 * one that is open. 1 on success; 0 with the reason, and no document open.
 */
s32 forgeLevelSessionOpen(const char *arena_id, char *err, size_t err_cap);

/* The open document and where it came from, or NULL when none is open. */
forge_level_doc_t *forgeLevelSessionDoc(void);
const forge_level_source_t *forgeLevelSessionSource(void);

/* A copy of the open document as it was when it was opened (what the stage
 * loaded, when the session is on that level), or NULL when none is open; and
 * a number that changes whenever the open document does: opened, edited,
 * undone, redone or closed. The world follows both while the editor camera
 * flies (forge_level_live.h). */
const forge_level_doc_t *forgeLevelSessionOpenedDoc(void);
u32 forgeLevelSessionGeneration(void);

/* 1 when the current history revision differs from the last successful save or open. */
s32 forgeLevelSessionHasUnsavedChanges(void);

#define FORGE_LEVEL_PROP_SNAP_UNITS 50.0f

typedef struct forge_level_prop_aim {
	f32 position[3];
	f32 look[3];
	s32 has_floor;
	s32 valid;
} forge_level_prop_aim_t;

/* Screen coordinates use the native viewport. NULL aims at the camera reticle.
 * Returns 0 when there is no resident native editor session or view. */
s32 forgeLevelSessionAimProp(const f32 screen[2], forge_level_prop_aim_t *aim);

/* Reraycast for the current view and reserve undo before adding one prop. */
s32 forgeLevelSessionCommitAimedProp(const char *model_catalog_id, u32 expected_generation,
		const f32 screen[2], char *record_id, size_t record_cap, char *err, size_t err_cap);

/* Runs the opened arena's draft through the native stage loader with a
 * temporary scenario source. The editor document and history stay open. */
s32 forgeLevelSessionCanPlayDraft(void);
s32 forgeLevelSessionCanEditCover(void);
s32 forgeLevelSessionMoveCoverToPad(const char *cover_ref, const char *pad_ref,
	char *err, size_t err_cap);
s32 forgeLevelSessionCanEditRoomBrightness(void);
s32 forgeLevelSessionCanConnectPads(void);
s32 forgeLevelSessionConnectPads(const char *first, const char *second,
	char *out_path_id, size_t out_cap, char *err, size_t err_cap);
/* Inspect the exact rebuilt original draft without loading or publishing it.
 * If requested, the digest identifies the private archive that was checked. */
s32 forgeLevelValidateOriginalDraft(forge_level_doc_t *doc, const forge_level_source_t *source,
	char out_sha256[SHA256_HEX_SIZE], char *err, size_t err_cap);
s32 forgeLevelSessionPlayDraft(char *err, size_t err_cap);
s32 forgeLevelDraftReturn(void);
s32 forgeLevelDraftIsPlaying(void);
s32 forgeLevelDraftOwnsStageTransition(void);
void forgeLevelDraftStageLeaving(s32 next_stage);
/* At the real stage boundary: 1 to play, -1 to restore the editor, 0 otherwise. */
s32 forgeLevelDraftStageReady(void);
s32 forgeLevelDraftStageFailed(void);
/* Existing boot smoke probe. Edits the open draft without saving it. */
s32 forgeLevelDebugEditDraft(char *err, size_t err_cap);

/**
 * Saves the open document as the level mod `display_name` (see
 * forgeLevelSaveAs). The document stays open, still as its template plus the
 * edits, so saving again under the same name updates that level.
 */
s32 forgeLevelSessionSaveAs(const char *display_name, mod_author_result_t *result,
		char *err, size_t err_cap);

/* Review the open original's current draft and its exact native source closure.
 * Start with an empty or previously owned plan. Free it with gridMatchSourcesFree.
 * An unsuccessful review leaves the caller's prior plan intact. */
s32 forgeLevelSessionCanReviewVariantSources(void);
s32 forgeLevelSessionReviewVariantSources(grid_match_sources_t *out, char *err, size_t err_cap);

/* Save an independent local variant under a new name. Omitted package indices
 * must be strictly increasing within the reviewed plan. Only placements of
 * those packages may be replaced, and the final draft must no longer require
 * any omitted package. Result is optional and assigned only on success. */
s32 forgeLevelSessionSaveSelectedVariant(const char *display_name,
	const grid_match_sources_t *reviewed, const u16 *omitted_packages, size_t omitted_count,
	const forge_level_placement_fallback_t *fallbacks, size_t fallback_count,
	mod_author_result_t *result, char *err, size_t err_cap);

/* Review the current authenticated played original in the returning Room. The
 * document output starts NULL and the plan starts empty or owned. Both outputs
 * are caller-owned on success and preserved on refusal. No stage is started. */
s32 forgeLevelCanReviewPlayedVariantSources(const grid_match_result_t *result);
s32 forgeLevelReviewPlayedVariantSources(const grid_match_result_t *result,
	forge_level_doc_t **out_doc, grid_match_sources_t *out, char *err, size_t err_cap);
s32 forgeLevelSavePlayedVariant(const grid_match_result_t *result, const char *display_name,
	const grid_match_sources_t *reviewed, const u16 *omitted_packages, size_t omitted_count,
	const forge_level_placement_fallback_t *fallbacks, size_t fallback_count,
	mod_author_result_t *out, char *err, size_t err_cap);

/* Host-side native fork against the exact committed shared draft parent. The
 * Room caller owns authentication and the document/review generation checks.
 * Publishing this fork does not rebuild the catalog or consume Room leases. */
s32 forgeLevelReviewSharedVariantSources(const char *arena_id, const char *parent_sha,
	forge_level_doc_t *doc, grid_match_sources_t *out, char *err, size_t err_cap);
s32 forgeLevelSaveSharedVariant(const char *arena_id, const char *parent_sha, forge_level_doc_t *doc,
	const char *display_name, const grid_match_sources_t *reviewed,
	const u16 *omitted_packages, size_t omitted_count, const forge_level_placement_fallback_t *fallbacks,
	size_t fallback_count, mod_author_result_t *out, char *err, size_t err_cap);

/* Check the selected local arena's dependency plan before preparing its stage.
 * An ordinary arena or a temporary network arena requires no local mount.
 * A mounted variant owns its missing source rows and scratch copies until stage
 * consumers stop. Release stage references, then retire the mount. An unused
 * preparation can be retired directly. A different active lease is preserved. */
s32 forgeLevelMountVariantDependencies(const char *arena_id, char *err, size_t err_cap);
s32 forgeLevelVariantDependenciesStage(void);
/* Borrowed canonical native plan for the mounted stage, through retirement. */
const grid_match_sources_t *forgeLevelVariantDependencyPlanForStage(s32 stage);
s32 forgeLevelVariantDependencyOwnsPackage(const char *arena_id,
	const u8 arena_sha256[SHA256_DIGEST_SIZE], const grid_match_source_package_t *package);
s32 forgeLevelReleaseVariantDependencyStageRefs(char *err, size_t err_cap);
s32 forgeLevelRetireVariantDependencies(char *err, size_t err_cap);

/* The current opened original's display name, borrowed until the catalog
 * changes, or NULL when the session has no matching in-place destination. */
const char *forgeLevelSessionSaveTarget(void);
/* Revalidate the opened original and replace its exact loose archive atomically.
 * Returns MOD_AUTHOR_OK or the mod author's error code. */
s32 forgeLevelSessionSaveOpened(char *err, size_t err_cap);

/* Pin the document's exact last saved archive in this profile. */
s32 forgeLevelSessionPinSavedRevision(forge_revision_t *revision, char *err, size_t err_cap);
/* A local permission for one pinned original arena, held only in memory. */
s32 forgeLevelSessionAllowMatchSharing(char *err, size_t err_cap);
const forge_revision_t *forgeLevelMatchSharePermission(void);
void forgeLevelRevokeMatchSharing(void);
s32 forgeLevelAllowWeaponMatchSharing(const char *weapon_id, char *err, size_t err_cap);
/* Check display eligibility without changing the current grant. */
s32 forgeLevelCanOfferExactWeaponMatchSharing(const grid_match_source_package_t *candidate);
/* Approve the exact weapon package displayed from the pinned arena's sources. */
s32 forgeLevelAllowExactWeaponMatchSharing(const grid_match_source_package_t *candidate,
	char *err, size_t err_cap);
const grid_match_source_package_t *forgeLevelWeaponMatchSharePermission(void);
void forgeLevelRevokeWeaponMatchSharing(void);
/* The host applies this to the exact staged bytes before offering a package. */
s32 forgeLevelMatchPackageShareAllowed(const grid_match_source_package_t *package,
	const void *exact_bytes, u32 size);

/* Fork an authoritative shared draft from a known pinned parent into a new level. */
s32 forgeLevelForkEditedRevision(const char *arena_id, const char *parent_sha,
	forge_level_doc_t *document, const char *name, mod_author_result_t *fork,
	char *err, size_t err_cap);
/* Write the exact local BYOR draft as a new archive without refreshing the catalog. */
s32 forgeLevelForkLocalByorRevision(const char *arena_id, const char *parent_sha,
	forge_level_doc_t *document, const char *name, mod_author_result_t *fork,
	char *err, size_t err_cap);
s32 forgeLevelForkExtendedRevision(const char *arena_id, const char *parent_sha,
		forge_level_doc_t *document, const char *name, s32 room, s32 extra_length,
		mod_author_result_t *fork, char *err, size_t err_cap);

/* Offline: save current edits and a room extension as a new disabled mod
 * based on the exact opened revision's pin. The open Level File stays live. */
s32 forgeLevelSessionCanSaveExtendedVariant(void);
s32 forgeLevelSessionSaveExtendedVariant(const char *name, s32 room, s32 extra_length,
		mod_author_result_t *fork, char *err, size_t err_cap);

s32 forgeLevelSessionForkPinnedRevision(const char *name, mod_author_result_t *fork,
	char *err, size_t err_cap);
s32 forgeLevelSessionForkPreviousRevision(const char *name, mod_author_result_t *fork,
	char *err, size_t err_cap);
/* The verified parent of the opened archive, if it was made by forking. */
forge_revision_parent_e forgeLevelSessionParentRevision(forge_revision_t *revision,
	char *err, size_t err_cap);
s32 forgeLevelSessionForkParentRevision(const char *name, mod_author_result_t *fork,
	char *err, size_t err_cap);
s32 forgeLevelDebugForkPinned(const char *name, char *err, size_t err_cap);

/* Read the pinned revision's placement, stage-rule and registered catalog
 * inputs. This currently inventories those sources locally. */
s32 forgeLevelSessionReviewMatchSources(grid_match_sources_t *plan, char *err, size_t err_cap);
s32 forgeLevelBuildPinnedMatchSources(const char *arena_id,
	const grid_match_source_root_t *match_roots, size_t match_root_count,
	grid_match_sources_t *plan, char *err, size_t err_cap);
/* Capture the selected non-level sources before expanding the arena. */
s32 forgeLevelBuildPrivateGameplaySources(const grid_match_source_root_t *roots, size_t count,
	grid_match_sources_t *plan, char *err, size_t err_cap);
/* Prepare the exact arena's authored source packages from a pin or installed
 * copy, with a caller-owned cache keyed to this Room and review serial.
 * On success takes an empty cache handle and returns the owning cache and its
 * offer. On refusal preserves both outputs. No catalog or stage is published. */
s32 forgeLevelPrepareStagedArenaSources(const char *locator, const char *arena_id,
	const char *expected_sha, u8 room_id, u64 review_serial,
	grid_match_offer_t *out_offer, grid_match_cache_t **out_cache, char *err, size_t err_cap);
/* Prepare a consented original arena directly from bounded received memory.
 * The arena must descend from the exact open parent. Other packages are
 * verified locally. A refusal leaves outputs unchanged; the received arena
 * has no local path, catalog row or installed file. */
s32 forgeLevelPrepareReceivedStagedArenaSources(const void *bytes, u32 size,
	const char *arena_id, const char *expected_sha, const char *parent_id, const char *parent_sha,
	u8 room_id, u64 review_serial, grid_match_offer_t *out_offer, grid_match_cache_t **out_cache,
	char *err, size_t err_cap);
s32 forgeLevelStageLocalMatchSources(const grid_match_sources_t *plan,
	const grid_match_offer_t *offer, grid_match_cache_t **cache, char *err, size_t err_cap);
s32 forgeLevelStageInstalledMatchSources(const grid_match_sources_t *plan,
	const grid_match_offer_t *offer, grid_match_cache_t **cache, char *err, size_t err_cap);
/* Keep locally verified packages staged while an authorized absent arena
 * and/or weapon is considered. The omitted indices are strictly ascending.
 * No bytes for the absent packages are reserved or published. */
s32 forgeLevelStageMatchSourcesExcept(const grid_match_sources_t *plan,
	const grid_match_offer_t *offer, const u16 *absent_packages, size_t absent_count, grid_match_cache_t **cache,
	char *err, size_t err_cap);

/* Private, per-profile placement checkpoints. Restoring is an undoable edit. */
s32 forgeLevelSessionWriteCheckpoint(char *err, size_t err_cap);
s32 forgeLevelSessionRestoreCheckpoint(char *err, size_t err_cap);

/* Closes the open document and drops its edits, and everything Undo and Redo
 * could have brought back. */
void forgeLevelSessionClose(void);

/*
 * Undo and redo for the open document (Forge F3: edits can be undone). The
 * editor brackets each edit with EditBegin and EditEnd: Begin keeps a copy of
 * the document as it is (forgeLevelDocClone); End(1) puts that copy on the
 * undo list, because the edit changed the document, and forgets what Redo
 * could have brought back; End(0) drops it. Undo makes the latest copy the
 * open document again and keeps the one it replaces for Redo; Redo does the
 * reverse. Each logs FORGE.LEVEL.UNDO or FORGE.LEVEL.REDO and returns 1 when
 * it did something. At most FORGE_LEVEL_UNDO_DEPTH steps are kept (the oldest
 * go first); opening another level, closing, and starting a session forget
 * them all. A save does not: an edit made before it can still be undone, and
 * the next save writes the result.
 *
 * Undo and Redo replace the open document, so a caller must not keep using a
 * document pointer it took before calling them.
 */
#define FORGE_LEVEL_UNDO_DEPTH 32
void forgeLevelSessionEditBegin(void);
void forgeLevelSessionEditEnd(s32 changed);
s32 forgeLevelSessionUndo(void);
s32 forgeLevelSessionRedo(void);
s32 forgeLevelSessionUndoCount(void);
s32 forgeLevelSessionRedoCount(void);

/**
 * Starts a Forge session on the arena `arena_id`, the way choosing it in The
 * Grid does: a clean match setup on that level in the standard game mode,
 * then the stage change. Returns 1 when the session was requested.
 */
s32 forgeLevelStartForge(const char *arena_id);

/**
 * --debug-forge-level <arena id> with --debug-forge-level-name <name>: opens
 * that level, adds a player start a short step in front of its first one and
 * another spot for its first weapon pickup a short step in front of that
 * pickup, leaves its first ammo crate out, and saves the result as the level
 * `display_name`. With --debug-forge-level-weapon <weapon id> the copied
 * pickup is given that catalog weapon (`pickup_weapon_id`; NULL or "" leaves
 * it a weapon place like the one it was copied from). With `make_vehicle`
 * (--debug-forge-level-vehicle) it also makes a hoverbike 600 units in front
 * of the level's first start, facing the same way, which gives the vehicle
 * smokes a level with no mission to end the ride (B-1241). With
 * `prop_model_id` (--debug-forge-level-prop <model id>) it also makes a plain
 * prop drawn with that model of the catalog, 420 units in front of the first
 * start. It is what a smoke uses to prove an edited level loads; the Forge
 * editor makes the same open and save calls. Returns 1 when the level was
 * saved and is in the catalog.
 */
s32 forgeLevelDebugEdit(const char *arena_id, const char *display_name, const char *pickup_weapon_id,
		s32 make_vehicle, const char *prop_model_id);

/**
 * The model and the size a pickup of the weapon `weapon_catalog_id` is shown
 * with. For a weapon the match's weapon sets offer, its row in the weapon set
 * table (g_MpWeapons): what one of a level's weapon places becomes when a
 * match gives it that weapon. For any other weapon, the model it is held
 * with, at life size. 1 with the model's catalog id and the size (256 is life
 * size); 0 with the reason when the catalog has no such weapon or the game
 * has no model for it.
 */
s32 forgeLevelPickupLook(const char *weapon_catalog_id, char *out_model_id, size_t model_cap,
		s32 *out_extra_scale, char *err, size_t err_cap);

/**
 * Makes the pickup `record_id` of the open document give the catalog weapon
 * `weapon_catalog_id`, shown with that weapon's model and size
 * (forgeLevelPickupLook, then forgeLevelDocSetPickupWeapon). 1 on success; 0
 * with the reason, and the document unchanged.
 */
s32 forgeLevelSessionGivePickup(const char *record_id, const char *weapon_catalog_id,
		char *err, size_t err_cap);

/**
 * --debug-forge-session-save <name>, inside a Forge session started on a
 * level (--launch-forge-level <arena id>): opens that level's placements,
 * adds a player start at `position` facing `look` (the editor camera's), and
 * saves the result as the level `display_name` while the level is loaded and
 * the editor is up. It is what the editor's Add Start Here and Save As Level
 * buttons do, and it leaves the document open as they do. With `undo_redo`
 * (--debug-forge-session-undo) it undoes the start and redoes it before the
 * save, as the editor's Undo and Redo buttons do. With `live_look`
 * (--debug-forge-session-live, the flat facing of the camera the smoke will
 * look through) it then edits the open document so the world has something
 * to show (forge_level_live.h): the level's first crate or prop is copied 250
 * units along `live_look` from `position` and moved 150 units to the right of
 * the copy, or, in a level with none, an ammo crate is made from nothing 250
 * units along it. Returns 1 when the level was saved and is in the catalog
 * (and the edits, if asked for, were made).
 */
s32 forgeLevelDebugSessionEdit(const char *display_name, const f32 position[3], const f32 look[3],
		s32 undo_redo, const f32 *live_look);

#ifdef __cplusplus
}
#endif

#endif
