#ifndef PD_GRID_EDIT_SESSION_H
#define PD_GRID_EDIT_SESSION_H

#include "forge_level_doc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GRID_EDIT_MAX_MEMBERS 32
#define GRID_EDIT_SPAWN_ID_CAP 32
#define GRID_EDIT_DELTA_CAPACITY 64
#define GRID_EDIT_MAX_LEASES 32
#define GRID_EDIT_LEASE_MS 3000u

typedef struct grid_edit_session grid_edit_session_t;

typedef enum grid_edit_role {
	GRID_EDIT_ROLE_NONE = 0,
	GRID_EDIT_ROLE_CREATOR,
	GRID_EDIT_ROLE_PLAYER
} grid_edit_role_t;

typedef struct grid_edit_capabilities {
	grid_edit_role_t role;
	s32 can_edit;
	s32 see_creators;
	s32 see_match;
} grid_edit_capabilities_t;

typedef enum grid_edit_operation {
	GRID_EDIT_MOVE_SPAWN = 1,
	GRID_EDIT_SET_AMBIENT_LIGHT,
	GRID_EDIT_MOVE_DECORATION
} grid_edit_operation_t;

typedef struct grid_edit_move_spawn {
	char spawn_id[GRID_EDIT_SPAWN_ID_CAP];
	f32 position[3];
	f32 look[3];
} grid_edit_move_spawn_t;

typedef struct grid_edit_ambient_light {
	s32 enabled;
	s32 brightness;
} grid_edit_ambient_light_t;

typedef struct grid_edit_move_decoration {
	char record_id[GRID_EDIT_SPAWN_ID_CAP];
	f32 x, z;
} grid_edit_move_decoration_t;

typedef struct grid_edit_command {
	u64 sequence;
	u64 expected_revision;
	grid_edit_operation_t operation;
	union {
		grid_edit_move_spawn_t move_spawn;
		grid_edit_ambient_light_t ambient_light;
		grid_edit_move_decoration_t move_decoration;
	} data;
} grid_edit_command_t;

typedef enum grid_edit_result {
	GRID_EDIT_APPLIED = 1,
	GRID_EDIT_DUPLICATE,
	GRID_EDIT_DENIED = -1,
	GRID_EDIT_INVALID = -2,
	GRID_EDIT_STALE = -3,
	GRID_EDIT_OUT_OF_ORDER = -4,
	GRID_EDIT_SEQUENCE_REUSED = -5,
	GRID_EDIT_RESOURCE_EXHAUSTED = -6,
	GRID_EDIT_RESOURCE_LEASED = -7
} grid_edit_result_t;

typedef struct grid_edit_receipt {
	/* The command's committed revision for APPLIED or DUPLICATE. The current
	 * revision for STALE. Zero for other outcomes. */
	u64 revision;
} grid_edit_receipt_t;

typedef struct grid_edit_delta {
	u64 revision;
	u64 author_id;
	grid_edit_command_t command;
} grid_edit_delta_t;

typedef enum grid_edit_tail_result {
	GRID_EDIT_TAIL_READY = 1,
	GRID_EDIT_TAIL_SNAPSHOT_REQUIRED,
	GRID_EDIT_TAIL_DENIED = -1,
	GRID_EDIT_TAIL_INVALID_REVISION = -2,
	GRID_EDIT_TAIL_BUFFER_TOO_SMALL = -3
} grid_edit_tail_result_t;

typedef enum grid_edit_resource_kind {
	GRID_EDIT_RESOURCE_SPAWN = 1,
	GRID_EDIT_RESOURCE_AMBIENT_LIGHT,
	GRID_EDIT_RESOURCE_DECORATION
} grid_edit_resource_kind_t;

typedef struct grid_edit_resource {
	grid_edit_resource_kind_t kind;
	union {
		char spawn_id[GRID_EDIT_SPAWN_ID_CAP]; /* GRID_EDIT_RESOURCE_SPAWN */
		char record_id[GRID_EDIT_SPAWN_ID_CAP]; /* GRID_EDIT_RESOURCE_DECORATION */
	};
} grid_edit_resource_t;

typedef enum grid_edit_lease_result {
	GRID_EDIT_LEASE_GRANTED = 1,
	GRID_EDIT_LEASE_DENIED = -1,
	GRID_EDIT_LEASE_INVALID_RESOURCE = -2,
	GRID_EDIT_LEASE_HELD = -3,
	GRID_EDIT_LEASE_FULL = -4,
	GRID_EDIT_LEASE_CLOCK_UNAVAILABLE = -5
} grid_edit_lease_result_t;

/* One draft per session. The host starts as a creator and both cross-role
 * visibility flags start off. The opened document is borrowed for this call
 * and cloned as revision 1. Free the session with gridEditSessionFree.
 *
 * Run these calls serially on the host. The caller derives actor IDs from
 * authenticated connections or the trusted local host. IDs are nonzero and
 * never assigned again in the same session after removal. A reconnect with a
 * new ID needs a fresh host grant. Actor IDs are never taken from commands. */
grid_edit_session_t *gridEditSessionCreate(u64 host_id, const forge_level_doc_t *opened);
/* Construct a replica from a verified native snapshot. The host's sequence
 * starts after host_sequence, while any subsequently admitted creator starts
 * at 1. The snapshot has no older delta tail or duplicate receipts. */
grid_edit_session_t *gridEditSessionCreateAtRevision(u64 host_id, const forge_level_doc_t *opened,
	u64 revision, u64 host_sequence);
void gridEditSessionFree(grid_edit_session_t *session);

/* Only the host may set a member's creator/player role, remove a guest or
 * set either disclosure policy. Updating a member preserves their sequence.
 * The member limit includes the host. Return 1 on success and 0 on refusal. */
s32 gridEditSessionSetMember(grid_edit_session_t *session, u64 actor_id, u64 member_id,
	grid_edit_role_t role);
s32 gridEditSessionRemoveMember(grid_edit_session_t *session, u64 actor_id, u64 member_id);
s32 gridEditSessionSetVisibility(grid_edit_session_t *session, u64 actor_id,
	s32 players_see_creators, s32 creators_see_match);
/* The host can read the stored disclosure flags independently of role. */
s32 gridEditSessionGetVisibility(const grid_edit_session_t *session, u64 actor_id,
	s32 *players_see_creators, s32 *creators_see_match);

/* Use these permissions before disclosing creator presence or match data.
 * Each role sees its own side. A nonmember has no capabilities. Draft
 * snapshots and commands are restricted to creators even when players can
 * see creator presence. */
grid_edit_capabilities_t gridEditSessionCapabilities(const grid_edit_session_t *session,
	u64 viewer_id);

/* Creator-only poses read directly from the committed native document. */
s32 gridEditSessionSpawnCount(const grid_edit_session_t *session, u64 viewer_id);
s32 gridEditSessionGetSpawnPose(const grid_edit_session_t *session, u64 viewer_id,
	s32 index, grid_edit_move_spawn_t *out);
s32 gridEditSessionFindSpawnPose(const grid_edit_session_t *session, u64 viewer_id,
	const char *spawn_id, grid_edit_move_spawn_t *out);
s32 gridEditSessionDecorationCount(const grid_edit_session_t *session, u64 viewer_id);
s32 gridEditSessionGetDecorationPose(const grid_edit_session_t *session, u64 viewer_id,
	s32 index, grid_edit_move_decoration_t *out);
s32 gridEditSessionFindDecorationPose(const grid_edit_session_t *session, u64 viewer_id,
	const char *record_id, grid_edit_move_decoration_t *out);

s32 gridEditMoveSpawnValid(const grid_edit_move_spawn_t *move);
s32 gridEditMoveDecorationValid(const grid_edit_move_decoration_t *move);
s32 gridEditCommandMatches(const grid_edit_command_t *a, const grid_edit_command_t *b);

/* Sequences start at 1 per member and advance by one per successful commit.
 * An exact retry of that member's latest commit returns the original receipt.
 * Earlier sequences and conflicting payloads cannot commit. A rejected
 * command leaves both the draft and the sequence unchanged. */
grid_edit_result_t gridEditSessionApply(grid_edit_session_t *session, u64 author_id,
	const grid_edit_command_t *command, grid_edit_receipt_t *receipt, char *err, size_t err_cap);

/* A creator receives all commits strictly after after_revision. Pass the
 * revision of a verified local draft. Zero or a revision older than the
 * retained history requires a full snapshot. A future revision is invalid.
 * Optional count and latest_revision are zero on denial. BUFFER_TOO_SMALL
 * reports the required count and writes no deltas, so a NULL out may size a
 * later call. The creator gets the latest revision on all authorized reads.
 * Deltas carry typed data for callers to encode independently. */
grid_edit_tail_result_t gridEditSessionReadDeltas(const grid_edit_session_t *session, u64 viewer_id,
	u64 after_revision, grid_edit_delta_t *out, size_t capacity, size_t *count, u64 *latest_revision);

/* The host advances time from its monotonic clock before handling lease
 * traffic or edits. Returns 0 for a non-host actor or a backwards clock.
 * Leases require at least one clock update. */
s32 gridEditSessionAdvanceClock(grid_edit_session_t *session, u64 actor_id, u64 now_ms);

/* Creators may lease an existing spawn, decoration or ambient-light setting for
 * GRID_EDIT_LEASE_MS. A creator may renew their lease or release it early.
 * Leases end on expiry, role loss or removal. Edits need no lease on an
 * unclaimed resource. */
grid_edit_lease_result_t gridEditSessionAcquireLease(grid_edit_session_t *session, u64 author_id,
	const grid_edit_resource_t *resource);
s32 gridEditSessionReleaseLease(grid_edit_session_t *session, u64 author_id,
	const grid_edit_resource_t *resource);

/* A canonical wire resource key. The session verifies that it exists. */
s32 gridEditResourceValid(const grid_edit_resource_t *resource);

/* A creator focuses at most one resource. Changing targets releases its old
 * lease before attempting to claim the new one. NULL clears focus. */
grid_edit_lease_result_t gridEditSessionFocusResource(grid_edit_session_t *session, u64 author_id,
	const grid_edit_resource_t *resource_or_null);

/* A creator receives an owned snapshot of the committed native document.
 * Free it with forgeLevelDocFree. The optional revision is zero on refusal
 * or allocation failure. */
forge_level_doc_t *gridEditSessionCloneDocument(const grid_edit_session_t *session,
	u64 viewer_id, u64 *revision);

/* Swaps owned documents at the same revision, returning the previous document
 * to the caller. Both the session and replacement are required. This preserves
 * authority, including member sequence and delta history. */
forge_level_doc_t *gridEditSessionSwapDocument(grid_edit_session_t *session, forge_level_doc_t *document);

/* Takes ownership of a verified canonical snapshot at the current revision. */
void gridEditSessionAdoptSnapshotDocument(grid_edit_session_t *session, forge_level_doc_t *document);

#ifdef __cplusplus
}
#endif

#endif
