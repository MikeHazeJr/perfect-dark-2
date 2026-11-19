#ifndef PD_GRID_EDIT_ROOM_NET_H
#define PD_GRID_EDIT_ROOM_NET_H

#include "net/grid_edit_room.h"
#include "net/grid_edit_playtest_view.h"
#include "net/grid_edit_room_wire.h"
#include "grid_match_sources.h"

#ifdef __cplusplus
extern "C" {
#endif

struct netclient;
struct _ENetEvent;

s32 netGridEditCanInvite(u32 client_id);
s32 netGridEditInvite(u32 client_id, const char *arena_id, const char *exact_sha_or_null);
s32 netGridEditPendingInvite(grid_edit_room_invite_t *out);
s32 netGridEditGetState(grid_edit_room_state_t *out);
s32 netGridEditReply(s32 accept);
s32 netGridEditSetAmbient(s32 enabled, s32 brightness);
s32 netGridEditSpawnCount(void);
s32 netGridEditGetSpawnPose(s32 index, grid_edit_move_spawn_t *out);
s32 netGridEditMoveSpawn(const grid_edit_move_spawn_t *move);
s32 netGridEditDecorationCount(void);
s32 netGridEditGetDecorationPose(s32 index, grid_edit_move_decoration_t *out);
s32 netGridEditMoveDecoration(const grid_edit_move_decoration_t *move);
/* Placement selected from the creator's exact current live projection. */
s32 netGridEditMoveSpawnOnPlan(const grid_edit_move_spawn_t *move, const grid_playtest_target_t *target);
s32 netGridEditMoveDecorationOnPlan(const grid_edit_move_decoration_t *move, const grid_playtest_target_t *target);
s32 netGridEditSaveVariant(const char *name, char *arena_id, size_t capacity);
s32 netGridEditCanSaveLocalByorVariant(void);
/* Returns success once the archive is written. netGridEditError reports any
 * later pin or library registration issue. */
s32 netGridEditSaveLocalByorVariant(const char *name, char *arena_id, size_t capacity);
typedef struct net_grid_edit_variant_review {
	grid_edit_wire_identity_t identity;
	u64 room_lifetime;
	u64 revision;
	char parent_sha256[SHA256_HEX_SIZE];
} net_grid_edit_variant_review_t;
s32 netGridEditCanReviewVariantSources(void);
s32 netGridEditVariantReviewMatches(const net_grid_edit_variant_review_t *key);
s32 netGridEditReviewVariantSources(net_grid_edit_variant_review_t *key,
	forge_level_doc_t **out_doc, grid_match_sources_t *out);
s32 netGridEditSaveSelectedVariant(const net_grid_edit_variant_review_t *key, const char *name,
	const grid_match_sources_t *reviewed, const u16 *omitted_packages, size_t omitted_count,
	const forge_level_placement_fallback_t *fallbacks, size_t fallback_count, char *arena_id, size_t capacity);
s32 netGridEditCanStageRoomExtension(void);
s32 netGridEditStageRoomExtension(const char *name, s32 room, s32 extra_length,
	char *arena_id, size_t capacity);
typedef struct grid_edit_staged_variant_info {
	char arena_id[CATALOG_ID_LEN];
	char arena_sha256[SHA256_HEX_SIZE];
	u64 draft_revision;
	u64 current_revision;
	u64 applied_layout_generation;
} grid_edit_staged_variant_info_t;
s32 netGridEditGetStagedVariant(grid_edit_staged_variant_info_t *out);
#define GRID_EDIT_REVIEW_PREPARATION_ISSUE_MAX 192

typedef struct grid_edit_variant_review_info {
	grid_edit_variant_review_t variant;
	grid_edit_variant_review_reply_e reply;
	u8 includes_dependencies;
	/* An exact local cache is held for this current prepared review. */
	u8 sources_prepared;
	u8 match_sources_prepared;
	char match_preparation_issue[GRID_EDIT_REVIEW_PREPARATION_ISSUE_MAX];
	u8 transfer_offered;
	u8 transfer_available;
	u8 transfer_active;
	u8 apply_pending;
	u32 arena_bytes;
	/* Bytes queued by the host or assembled by the creator. */
	u32 transferred_bytes;
} grid_edit_variant_review_info_t;
s32 netGridEditCanOfferStagedReview(void);
s32 netGridEditOfferStagedReview(void);
s32 netGridEditCanOfferStagedReviewTransfer(void);
s32 netGridEditOfferStagedReviewTransfer(void);
s32 netGridEditAcceptStagedReviewTransfer(void);
s32 netGridEditCancelStagedReview(void);
s32 netGridEditGetStagedReview(grid_edit_variant_review_info_t *out);
s32 netGridEditReplyToStagedReview(s32 acknowledge);
s32 netGridEditCanApplyStagedLayout(void);
s32 netGridEditApplyStagedLayout(void);
void netGridEditClose(void);
const char *netGridEditError(void);

/* Invoked by the network event loop, before ordinary packet dispatch. */
s32 netGridEditReceive(const struct _ENetEvent *event, struct netclient *client, s32 is_client);
void netGridEditPoll(void);
void netGridEditPumpSnapshot(void);
void netGridEditMatchStarting(u8 room_id);
s32 netGridEditPrivateLaunchMatches(u8 room_id, const char *arena_id);
s32 netGridEditPrivatePlaytestRoom(u8 room_id);
s32 netGridEditPrivateResumeRoomOpen(u8 room_id);
s32 netGridEditPrivateResumeCanJoin(u8 room_id, const struct netclient *client);
void netGridEditPrivateResumeAdmitted(u8 room_id, const struct netclient *client);
s32 netGridEditCanInviteToResumeRoom(u32 client_id);
s32 netGridEditInviteToResumeRoom(u32 client_id);
void netGridEditPrivatePlaytestFailed(u8 room_id);
s32 netGridEditStartPrivatePlaytest(s32 bots, u8 difficulty);
s32 netGridEditGetPlaytestVisibility(s32 *players, s32 *creators);
s32 netGridEditSetPlaytestVisibility(s32 players, s32 creators);
s32 netGridEditGetPlaytestView(grid_playtest_view_t *out);
/* Current creator-visible host receipt. This does not prove a local install. */
s32 netGridEditGetObservedLayout(grid_edit_room_control_t *out);
void netGridEditSetCreatorCursor(const f32 *position_or_null);
void netGridEditSetResourceFocus(const grid_edit_resource_t *resource_or_null);
s32 netGridEditGetCreatorCursor(grid_playtest_view_t *out, u16 *cursor_x, u16 *cursor_z);
s32 netGridEditHasSavedVariant(const char *arena_id);
const char *netGridEditSavedVariantId(void);
s32 netGridEditStartPrivateVariantPlaytest(const char *arena_id, s32 bots, u8 difficulty);
void netGridEditPrivateLayoutTick(void);
void netGridEditPrivatePlaytestTick(void);
void netGridEditPrivatePlaytestStageStopped(void);
s32 netGridEditPrivatePlaytestAmbient(s32 *out);
/* Reports the displayed override state only for the current private round. */
s32 netGridEditPrivatePlaytestAmbientState(s32 *enabled, s32 *brightness);
void netGridEditClientReset(const struct netclient *client);
void netGridEditNetworkReset(void);
s32 netGridEditHostOwnsRoom(void);

#ifdef __cplusplus
}
#endif

#endif
