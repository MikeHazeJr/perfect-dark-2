#ifndef PD_GRID_MATCH_RUNTIME_H
#define PD_GRID_MATCH_RUNTIME_H

#include "assetcatalog_scanner.h"
#include "fs.h"
#include "grid_match_cache.h"
#include "net/grid_match_result.h"
#include "net/grid_match_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grid_match_runtime_identity {
	grid_match_offer_t offer;
	char arena_id[CATALOG_ID_LEN];
	char scenario_id[CATALOG_ID_LEN];
	u8 arena_sha256[SHA256_DIGEST_SIZE];
	s32 stage;
} grid_match_runtime_identity_t;

typedef struct grid_match_private_candidate {
	grid_match_offer_t offer;
	grid_match_offer_t arena_offer;
	grid_match_runtime_identity_t outgoing;
	grid_match_cache_t *cache;
	grid_match_cache_t *outgoing_cache;
	fs_stage_archive_set_t *incoming_archives;
	fs_stage_archive_set_t *outgoing_archives;
	char arena_path[FS_MAXPATH + 1];
	asset_catalog_grid_arena_file_policy_e arena_file_policy;
	char arena_id[CATALOG_ID_LEN];
	char scenario_id[CATALOG_ID_LEN];
	char scenario_member[FS_MAXPATH + 1];
	u8 arena_sha256[SHA256_DIGEST_SIZE];
	size_t arena_package_index;
	size_t entry_count;
	size_t package_count;
	u32 catalog_generation;
} grid_match_private_candidate_t;

typedef struct grid_match_reconnect_package {
	grid_match_source_root_t owner;
	u8 digest[SHA256_DIGEST_SIZE];
	u32 size;
	u16 index;
	void *bytes;
} grid_match_reconnect_package_t;

typedef struct grid_match_reconnect_sources {
	size_t count;
	grid_match_reconnect_package_t *packages;
} grid_match_reconnect_sources_t;

typedef struct grid_match_session_file {
	grid_match_source_root_t owner;
	u16 package_index;
	char path[FS_MAXPATH + 1];
} grid_match_session_file_t;

typedef struct grid_match_session_sources {
	asset_catalog_grid_source_set_t *lease;
	grid_match_session_file_t *files;
	size_t count;
} grid_match_session_sources_t;

/* Resolve the descriptor of the exact typed source used by this catalog row. */
s32 gridMatchCatalogArchiveDescriptor(const asset_entry_t *entry, char *out, size_t capacity);

/* Check the pinned native structure of a staged original Variant before its
 * Mod is enabled. The caller retains the exact package bytes throughout. */
s32 gridMatchRuntimePreflightSavedArena(const char *locator, const void *bytes, u32 size,
	const char *arena_id, const char *expected_sha, char *err, size_t err_cap);

/* Validate a private offline Level File draft under the 128 MiB local budget.
 * No packages, catalog rows, or stages are published. */
s32 gridMatchRuntimePreflightLocalArena(const char *locator, const void *bytes, u32 size,
	const char *arena_id, const char *expected_sha, char *err, size_t err_cap);

/* Preflight the exact staged two-room extension and its static walking routes. */
s32 gridMatchRuntimePreflightRoomExtension(const char *locator, const void *bytes, u32 size,
	const char *arena_id, const char *expected_sha, char *err, size_t err_cap);

/* Main-thread handoff for an arena and its complete staged archive set.
 * Catalog rows and their local archive revisions must already match. A
 * successful prepare owns *cache and sets it to NULL. A refusal preserves
 * both the caller's cache and any previously prepared or active match. */
s32 gridMatchRuntimePrepare(const grid_match_sources_t *plan, const grid_match_offer_t *offer,
	grid_match_cache_t **cache, const char *arena_id, s32 stage, char *err, size_t err_cap);

/* Borrowed until the corresponding pending/active state changes. */
const grid_match_runtime_identity_t *gridMatchRuntimePending(void);
const grid_match_runtime_identity_t *gridMatchRuntimeActive(void);
/* Only known, independent pads in the resident exact host stage are eligible. */
s32 gridMatchRuntimeDecorationPadIndependent(s32 pad);

/* Resolve an eligible spawn weapon in both local weapon domains. */
s32 gridMatchResolveCatalogSpawnWeapon(const char *id, s32 *mp_slot, s32 *weapon);

/* Resolve a selected weapon only when its typed source is in this exact offer. */
s32 gridMatchRuntimeResolveSpawnWeapon(const grid_match_offer_t *offer, const char *id, s32 *weapon);

/* Verify the selected local weapon after the match manifest loads its definition. */
s32 gridMatchRuntimeValidateActiveSpawnWeapon(const char *id, s32 weapon, char *err, size_t err_cap);

/* Match the presence of a sealed Fiesta pool to the exact pending or active offer. */
s32 gridMatchRuntimeFiestaMatches(const grid_match_offer_t *offer, s32 fiesta);

/* Recheck the native definitions after the active manifest has loaded. An
 * active match without a Fiesta pool succeeds. */
s32 gridMatchRuntimeValidateActiveFiesta(char *err, size_t err_cap);

/* Active local MP weapon slots after resolving their admitted typed sources.
 * Returns the count and optionally lends the array until the active stage ends.
 * An absent or unusable pool returns zero and clears the optional output. */
s32 gridMatchRuntimeFiestaSlots(const u8 **slots);
s32 gridMatchRuntimeFiestaActive(void);
const char *gridMatchRuntimeFiestaWeaponId(s32 weapon, s32 *out_slot);

/* Observe a confirmed native Fiesta grant in a managed smoke run. */
void gridMatchRuntimeTraceFiestaGrant(const char *actor, s32 mp_slot, s32 weapon);

/* Copies the sealed active plan without copying or releasing archive bytes. */
s32 gridMatchRuntimeCopyActiveSources(grid_match_sources_t *out, char *err, size_t err_cap);

/* Bind the private host's sealed gameplay closure to its exact pending
 * match. Success transfers ownership and clears sources. */
s32 gridMatchRuntimeOwnPrivateGameplaySources(const grid_match_offer_t *pending_offer,
	grid_match_sources_t *sources, char *err, size_t err_cap);

/* Compose the detached arena review and exact active gameplay closure into
 * ready private bytes and detached stage archive tables. The caller binds
 * the exact pinned revision path, reviewed arena ID and SHA to the authorized
 * Room, review and private epoch, and passes a zeroed or owned candidate.
 * Success replaces out. Failure preserves it. Neither mount is published here. */
s32 gridMatchRuntimeComposePrivateCandidate(const grid_match_offer_t *active_offer,
	const grid_match_cache_t *arena_cache, const grid_match_offer_t *arena_offer,
	const char *revision_path, const char *arena_id, const u8 arena_sha256[SHA256_DIGEST_SIZE],
	grid_match_private_candidate_t *out, char *err, size_t err_cap);
void gridMatchRuntimeReleasePrivateCandidate(grid_match_private_candidate_t *candidate);
struct grid_edit_room;
typedef struct grid_match_private_layout grid_match_private_layout_t;
s32 gridMatchRuntimePreparePrivateLayout(const grid_match_private_candidate_t *candidate,
	const grid_match_offer_t *arena_offer, s32 room, s32 extension,
	const struct grid_edit_room *draft_room, u64 draft_revision, grid_match_private_layout_t **out,
	char *err, size_t err_cap);
s32 gridMatchRuntimeCommitPrivateLayout(grid_match_private_layout_t *layout, u64 previous_generation,
	u64 next_generation, char *err, size_t err_cap);
void gridMatchRuntimeReleasePrivateLayout(grid_match_private_layout_t *layout);
u64 gridMatchRuntimePrivateLayoutGeneration(void);

s32 gridMatchRuntimePrivateCandidateCurrent(const grid_match_private_candidate_t *candidate,
	const grid_match_offer_t *arena_offer);

/* Copy only exact active lease packages. The reconnect caller owns both this
 * set and its authenticated match/epoch scope. Empty means fully installed. */
s32 gridMatchRuntimeRetainReconnectSources(grid_match_reconnect_sources_t *out,
	char *err, size_t err_cap);
void gridMatchRuntimeFreeReconnectSources(grid_match_reconnect_sources_t *sources);

/* Borrow exact raw package bytes until the pending state changes. */
const void *gridMatchRuntimeReadPendingPackage(const grid_match_offer_t *offer,
	size_t package_index, u32 *out_size);
const grid_match_source_package_t *gridMatchRuntimePendingPackage(const grid_match_offer_t *offer,
	size_t package_index);

/* The pending match takes ownership of the complete set on success. */
s32 gridMatchRuntimeOwnSourceSet(grid_match_session_sources_t *sources, char *err, size_t err_cap);
s32 gridMatchRuntimeRetireDetachedSourceSet(grid_match_session_sources_t *sources, char *err, size_t err_cap);
s32 gridMatchRuntimeRetrySourceSetRetirement(char *err, size_t err_cap);

/* Transfer one admitted arena lease and its private file after prepare.
 * Clears the caller's lease on success and preserves it on refusal. */
s32 gridMatchRuntimeOwnArenaLease(asset_catalog_arena_lease_t *lease,
	char *err, size_t err_cap);

/* Retry a refused lease retirement or file removal after detachment. */
s32 gridMatchRuntimeRetryArenaLeaseRetirement(char *err, size_t err_cap);

/* Transfer a detached native arena lease to the runtime's cleanup owner.
 * Returns 1 after immediate retirement. On 0, a cleared lease means
 * cleanup is owned for retry, and an unchanged lease remains caller-owned. */
s32 gridMatchRuntimeRetireDetachedArenaLease(asset_catalog_arena_lease_t *lease, char *err, size_t err_cap);

/* Transfers the exact native weapon lease and its file to the pending stage. */
s32 gridMatchRuntimeOwnWeaponLease(asset_catalog_grid_weapon_lease_t **lease,
	size_t package_index, char *err, size_t err_cap);
s32 gridMatchRuntimeRetryWeaponLeaseRetirement(char *err, size_t err_cap);
/* After world and UI teardown, player cleanup, and manifest release. */
s32 gridMatchRuntimeReleaseWeaponStageRefs(char *err, size_t err_cap);
void gridMatchRuntimePollLeaseRetirement(u32 now_ms);
s32 gridMatchRuntimeIsIdle(void);
s32 gridMatchRuntimeRetireDetachedWeaponLease(asset_catalog_grid_weapon_lease_t **lease,
	char *err, size_t err_cap);

/* Publish only after stopping the outgoing world and before the intended
 * stage consumes its assets. A refusal leaves the pending set intact. */
s32 gridMatchRuntimePublishStage(s32 stage, const grid_match_offer_t *offer,
	char *err, size_t err_cap);
/* Recheck exact pending catalog and source bindings without publishing them.
 * The host uses this while the Room start can still be rolled back. */
s32 gridMatchRuntimeValidatePendingStage(s32 stage, const grid_match_offer_t *offer,
	char *err, size_t err_cap);
void gridMatchRuntimeCancelPending(void);

/* Drops the filesystem bindings before freeing their archive bytes. Call
 * after the active stage stops, including when a partial load is torn down. */
void gridMatchRuntimeDropActive(void);

/* Before stage-end commits, the client can retain the exact arena package
 * from its verified active stage cache, whether installed or temporarily
 * leased. Pass a terminal result whose authority and score bytes have been
 * verified. The copy outlives the stage and any private file. */
s32 gridMatchRuntimeRememberReturnedArena(const grid_match_result_t *result);

/* Borrow the arena only for the exact accepted result of the current room.
 * The caller enforces current room membership and clears the snapshot when
 * that scope ends. The bytes last until replacement or explicit forget. */
const void *gridMatchRuntimeReadReturnedArena(const grid_match_result_t *result, u32 *size);
const grid_match_sources_t *gridMatchRuntimeReturnedSources(const grid_match_result_t *result);
const void *gridMatchRuntimeReadReturnedSource(const grid_match_result_t *result,
	size_t package_index, u32 *size);
void gridMatchRuntimeForgetReturnedArena(void);

#ifdef __cplusplus
}
#endif

#endif
