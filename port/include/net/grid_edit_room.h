#ifndef PD_GRID_EDIT_ROOM_H
#define PD_GRID_EDIT_ROOM_H

#include "assetcatalog.h"
#include "net/grid_edit_snapshot_wire.h"
#include "net/grid_edit_wire.h"
#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grid_edit_room grid_edit_room_t;

typedef struct grid_edit_room_invite {
	grid_edit_wire_identity_t identity;
	char arena_id[CATALOG_ID_LEN];
	char arena_sha256[SHA256_HEX_SIZE];
	u64 baseline_revision;
	u64 host_actor;
} grid_edit_room_invite_t;

typedef struct grid_edit_room_state {
	grid_edit_room_invite_t invite;
	s32 is_host;
	s32 ready;
	s32 can_edit;
	s32 pending_invite;
	s32 pending_command;
	s32 creator_detached;
	s32 pending_resume;
	s32 snapshot_ready;
	u64 local_actor;
	u64 peer_actor;
	u64 next_sequence;
	u64 revision;
	s32 enabled;
	s32 brightness;
	/* An inverse is offered only to the author of the latest changing commit. */
	s32 can_undo_ambient;
	grid_edit_ambient_light_t previous_ambient;
	s32 can_undo_spawn;
	grid_edit_move_spawn_t previous_spawn;
	s32 can_undo_decoration;
	grid_edit_move_decoration_t previous_decoration;
} grid_edit_room_state_t;

typedef struct grid_edit_room_resume_tail {
	size_t count;
	grid_edit_delta_t deltas[GRID_EDIT_DELTA_CAPACITY];
} grid_edit_room_resume_tail_t;

/* Open one exact native local arena from a saved source or pin. The caller
 * supplies the host's authoritative room and actor IDs and resolves the
 * current pin to a physical local .pdarena path. An authenticated resume can
 * share a verified snapshot and host deltas before the guest editing grant. */
grid_edit_room_t *gridEditRoomHostOpen(const grid_edit_wire_identity_t *identity,
	u64 host_actor, const char *arena_id, const char *arena_path, const char *arena_sha256,
	char *err, size_t cap);
s32 gridEditRoomHostInvite(grid_edit_room_t *room, u64 peer_actor, u64 peer_connect_generation,
	grid_edit_room_invite_t *out);
/* Revoke the exact departing creator and preserve the committed host draft. */
s32 gridEditRoomHostDetachPeer(grid_edit_room_t *room, u64 peer_actor, u64 peer_connect_generation);
s32 gridEditRoomHostCancelInvite(grid_edit_room_t *room, u64 peer_actor, u64 peer_connect_generation);
s32 gridEditRoomHostGrant(grid_edit_room_t *room, u64 peer_actor, u64 peer_connect_generation,
	const grid_edit_room_invite_t *confirmation);

/* Reserve a newer generation and creator ID for the retained host draft.
 * The authenticated connection can be reused after a voluntary departure.
 * The caller owns the returned bytes and frees them with free(). Send them
 * only after HostConfirmResume verifies the exact-source confirmation. The
 * host can then edit and retain up to GRID_EDIT_DELTA_CAPACITY commits during
 * catch-up. Canceling the invitation preserves committed edits. An exact ACK
 * returns the ordered tail for the caller to send before the grant. */
void *gridEditRoomHostPrepareResume(grid_edit_room_t *room, const grid_edit_wire_identity_t *fresh_identity,
	u64 peer_actor, u64 peer_connect_generation, grid_edit_room_invite_t *out_invite,
	grid_edit_snapshot_offer_t *out_offer, char *err, size_t cap);
s32 gridEditRoomHostConfirmResume(grid_edit_room_t *room, u64 peer_actor, u64 peer_connect_generation,
	const grid_edit_room_invite_t *confirmation);
s32 gridEditRoomHostGrantResume(grid_edit_room_t *room, u64 peer_actor, u64 peer_connect_generation,
	const grid_edit_room_invite_t *confirmation, const grid_edit_snapshot_offer_t *ack,
	grid_edit_room_resume_tail_t *out_tail);

/* The guest resolves the invited arena ID through local saved sources/pins and
 * opens it at the exact invited SHA before replying to the invitation. Only a
 * grant received from the bound host enables edits. */
grid_edit_room_t *gridEditRoomGuestOpen(const grid_edit_room_invite_t *invite,
	u64 host_connect_generation, const char *local_arena_path, char *err, size_t cap);
/* Open from borrowed, previously consented match bytes. The room owns its parsed draft. */
grid_edit_room_t *gridEditRoomGuestOpenBytes(const grid_edit_room_invite_t *invite,
	u64 host_connect_generation, const void *arena_bytes, u32 arena_size, char *err, size_t cap);
/* Mark an accepted exact-source invitation as requiring a snapshot. Install
 * only the verified offer on the bound host connection. A failed install
 * preserves the native baseline. The guest sends the ACK only after install. */
s32 gridEditRoomGuestRequireResume(grid_edit_room_t *room, const grid_edit_wire_identity_t *identity,
	u64 host_connect_generation);
s32 gridEditRoomGuestInstallResume(grid_edit_room_t *room, u64 host_connect_generation,
	const grid_edit_snapshot_offer_t *offer, const void *bytes, u32 size, char *err, size_t cap);
s32 gridEditRoomGuestGrant(grid_edit_room_t *room, const grid_edit_room_invite_t *invite,
	u64 host_connect_generation, u64 granted_actor_id);

/* The local host passes its actor and generation zero. A remote sender's
 * actor and nonzero connection generation come from the authenticated route.
 * Only GRID_EDIT_APPLIED fills out_delta. The room admits one remote creator. */
grid_edit_result_t gridEditRoomHostApply(grid_edit_room_t *room, const grid_edit_wire_identity_t *identity,
	u64 actor, u64 connect_generation, const grid_edit_command_t *command,
	grid_edit_receipt_t *receipt, grid_edit_delta_t *out_delta, char *err, size_t cap);

/* Update from the host's monotonic clock when polling the room and before
 * applying a command. Clock rollback and non-host calls are refused. */
s32 gridEditRoomHostAdvanceClock(grid_edit_room_t *room, u64 now_ms);

/* An editor can claim or renew its focused resource.
 * NULL clears focus. The caller checks the exact room generation first and
 * supplies host-monotonic time independently of any received message. */
grid_edit_lease_result_t gridEditRoomHostFocusResource(grid_edit_room_t *room, u64 actor,
	u64 connect_generation, const grid_edit_resource_t *resource_or_null, u64 now_ms);

/* One pending guest edit. Commit is observed on the authoritative delta path.
 * A correlated rejection clears the pending edit without consuming sequence.
 * A local send failure can clear the pending edit through the same API with
 * GRID_EDIT_RESOURCE_EXHAUSTED after the caller verifies no bytes were sent. */
s32 gridEditRoomGuestBuildAmbient(grid_edit_room_t *room, s32 enabled, s32 brightness,
	grid_edit_command_t *out);
s32 gridEditRoomGuestBuildMoveSpawn(grid_edit_room_t *room, const grid_edit_move_spawn_t *move,
	grid_edit_command_t *out);
s32 gridEditRoomGuestBuildMoveDecoration(grid_edit_room_t *room, const grid_edit_move_decoration_t *move,
	grid_edit_command_t *out);
s32 gridEditRoomGuestReceiveReceipt(grid_edit_room_t *room, const grid_edit_wire_identity_t *identity,
	u64 host_connect_generation, u64 sequence, u64 expected_revision, grid_edit_result_t result);
s32 gridEditRoomGuestApplyDelta(grid_edit_room_t *room, const grid_edit_wire_identity_t *identity,
	u64 host_connect_generation, const grid_edit_delta_t *delta, char *err, size_t cap);

/* A host-only snapshot for saving the exact committed draft locally. */
forge_level_doc_t *gridEditRoomHostCloneDocument(const grid_edit_room_t *room, u64 *revision);

/* The host can query the current poses while editable. A guest can query after the grant. */
s32 gridEditRoomSpawnCount(const grid_edit_room_t *room);
s32 gridEditRoomGetSpawnPose(const grid_edit_room_t *room, s32 index, grid_edit_move_spawn_t *out);
s32 gridEditRoomDecorationCount(const grid_edit_room_t *room);
s32 gridEditRoomGetDecorationPose(const grid_edit_room_t *room, s32 index, grid_edit_move_decoration_t *out);

/* Only the ready host can read or change these independent stored flags. */
s32 gridEditRoomSetVisibility(grid_edit_room_t *room, s32 players_see_creators, s32 creators_see_match);
s32 gridEditRoomGetVisibility(const grid_edit_room_t *room, s32 *players_see_creators, s32 *creators_see_match);

s32 gridEditRoomGetState(const grid_edit_room_t *room, grid_edit_room_state_t *out);
void gridEditRoomFree(grid_edit_room_t *room);

#ifdef __cplusplus
}
#endif

#endif
