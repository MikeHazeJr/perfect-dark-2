#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/net.h"
#include "net/netenet.h"
#include "net/netbuf.h"
#include "net/netmsg.h"
#include "net/matchsetup.h"
#include "net/grid_edit_room_net.h"
#include "net/grid_edit_room_wire.h"
#include "net/grid_edit_snapshot_wire.h"
#include "net/grid_match_transfer.h"
#include "bss.h"
#include "data.h"
#include "game/bg.h"
#include "game/mplayer/participant.h"
#include "game/prop.h"
#include "game/propobj.h"
#include "game/setuputils.h"
#include "forge_level_io.h"
#include "forge_revision_store.h"
#include "grid_archive_policy.h"
#include "grid_match_cache.h"
#include "grid_match_package_io.h"
#include "grid_match_runtime.h"
#include "fs.h"
#include "lib/collision.h"
#include "lib/main.h"
#include "mod_author_runtime.h"
#include "modmgr.h"
#include "room.h"
#include "scenario_source_runtime.h"
#include "types.h"
#include "smoke_harness.h"
#include "system.h"

extern s32 netLobbyRequestStartWithSims(u8 gamemode, const char *stage_id, u8 difficulty,
	u8 anti_client, u8 bots, u8 sim_type, u8 time_limit, u32 options, u8 scenario,
	u8 score_limit, u16 team_score_limit, u8 weapon_set);

#define GRID_EDIT_INVITE_TIMEOUT_MS 60000u
#define GRID_EDIT_REVIEW_TIMEOUT_MS 30000u
#define GRID_EDIT_REVIEW_TRANSFER_TIMEOUT_MS 300000u
#define GRID_EDIT_LAYOUT_TIMEOUT_MS 5000u
#define GRID_EDIT_REVIEW_TRANSFER_CHUNKS_PER_FRAME 4u
#define GRID_EDIT_REVIEW_TRANSFER_MAX_QUEUED_COMMANDS 128u
#define GRID_VIEW_INTERVAL_MS 250u
#define GRID_VIEW_EXPIRY_MS 1000u
#define GRID_PLAYTEST_MAX_PENDING_DECORATIONS 16
#define GRID_EDIT_SNAPSHOT_CHUNKS_PER_FRAME 2u
#define GRID_EDIT_SNAPSHOT_MAX_QUEUED_COMMANDS 96u

static grid_edit_room_t *s_EditRoom;
static grid_edit_room_invite_t s_EditInvite;
static u64 s_EditGeneration;
static u64 s_EditActor = 1;
static u64 s_EditGuestActor;
static u64 s_EditConnection;
static u32 s_EditTarget = NET_NULL_CLIENT;
static u32 s_EditDeadline;
static s32 s_EditOfferPending;
static char s_EditError[256];
static grid_archive_origin_e s_EditSourceOrigin = GRID_ARCHIVE_INVALID;
static forge_revision_t s_EditSavedVariant;
typedef struct grid_edit_staged_variant {
	forge_revision_t pin;
	grid_edit_room_invite_t source;
	u64 draft_revision;
	u8 edited_room;
	u16 extension;
	char mod_id[MOD_AUTHOR_ID_MAX];
	char archive_path[MOD_AUTHOR_PATH_MAX];
} grid_edit_staged_variant_t;
static grid_edit_staged_variant_t s_EditStagedVariant;
static struct {
	grid_edit_room_control_t offer;
	grid_match_cache_t *sources;
	grid_match_private_candidate_t candidate;
	grid_match_transfer_t *transfer;
	u64 connection;
	u64 actor;
	u32 deadline;
	u32 catalog_generation;
	u32 candidate_checked_at;
	char candidate_issue[GRID_EDIT_REVIEW_PREPARATION_ISSUE_MAX];
	u32 transfer_offset;
	u16 transfer_package;
	u8 transfer_active;
	grid_edit_variant_review_reply_e reply;
} s_EditReview;
static u64 s_EditReviewSerial;
static u64 s_EditLastReviewSeen;
static s32 s_EditReviewSupported;
static u8 s_EditReviewCapabilities;

typedef enum grid_edit_layout_phase {
	EDIT_LAYOUT_NONE = 0,
	EDIT_LAYOUT_WAITING_REPLY,
	EDIT_LAYOUT_READY,
	EDIT_LAYOUT_OUTCOME
} grid_edit_layout_phase_t;

static struct {
	grid_edit_layout_phase_t phase;
	grid_edit_room_control_t intent;
	grid_match_private_layout_t *layout;
	grid_match_cache_t *ready_sources; /* creator's immutable READY promise */
	u64 connection;
	u64 actor;
	u32 deadline;
} s_EditLayout;
/* The managed fixture holds a decoded READY from the real creator connection. */
static struct {
	grid_edit_room_control_t reply;
	u64 connection;
	u64 actor;
	s32 timed_out;
} s_EditSmokeLayoutReply;

static u64 s_EditNextLayoutGeneration;
static u64 s_EditCreatorLayoutGeneration;
static u64 s_EditCreatorProposalGeneration;
static u32 s_EditCreatorLayoutEpoch;
static grid_edit_room_control_t s_EditCreatorLayoutReceipt;

static struct {
	u64 connection;
	u8 room_id;
	u8 host_id;
	u8 client_id;
	u8 owns_mask;
} s_EditResumeRoomInvite;

typedef enum grid_edit_resume_phase {
	EDIT_RESUME_NONE = 0,
	EDIT_RESUME_INVITED,
	EDIT_RESUME_SENDING,
	EDIT_RESUME_WAITING_ACK,
	EDIT_RESUME_RECEIVING,
	EDIT_RESUME_WAITING_GRANT
} grid_edit_resume_phase_t;

static struct {
	grid_edit_resume_phase_t phase;
	grid_edit_snapshot_offer_t offer;
	grid_edit_room_invite_t confirmation;
	u8 *bytes;
	u32 offset;
	u32 live_epoch;
	u64 live_generation;
} s_EditResume;


typedef enum grid_edit_playtest_phase {
	EDIT_PLAYTEST_NONE = 0,
	EDIT_PLAYTEST_REQUESTED,
	EDIT_PLAYTEST_LOADING,
	EDIT_PLAYTEST_ACTIVE
} grid_edit_playtest_phase_t;

static struct {
	grid_edit_playtest_phase_t phase;
	grid_edit_wire_identity_t draft;
	char arena_id[CATALOG_ID_LEN];
	char arena_sha256[SHA256_HEX_SIZE];
	u32 stage_epoch;
	u64 participant_mask;
	u8 gameplay_fingerprint[SHA256_DIGEST_SIZE];
	s32 gameplay_fingerprint_ready;
	u64 displayed_revision;
	s32 ambient_enabled;
	s32 brightness;
	grid_edit_room_control_t layout_receipt;
} s_EditPlaytest;

static struct {
	grid_playtest_view_t frame;
	f32 cursor_x, cursor_z;
	u32 sampled_at, received_at, cursor_received_at, cursor_sent_at;
	u64 cursor_token, announced_layout_generation;
	u8 frame_sent, cursor_visible, cursor_sent, cursor_granted;
	u8 cursor_policy_dirty;
} s_EditPresentation;

typedef struct grid_playtest_decoration_move {
	grid_edit_move_decoration_t pose;
	u32 epoch;
	u64 prop_generation;
	u64 draft_revision;
} grid_playtest_decoration_move_t;

static grid_playtest_decoration_move_t s_EditDecorationMoves[GRID_PLAYTEST_MAX_PENDING_DECORATIONS];
static grid_edit_resource_t s_LocalEditFocus;
static u64 s_LastEditFocusSent;

static void editFinishLayout(grid_edit_layout_apply_result_e outcome, const char *reason);
static void editReceiveLayoutControl(s32 is_client, const grid_edit_room_control_t *control);

static void editReleaseReviewSources(const char *reason)
{
	if (s_EditLayout.layout) {
		editFinishLayout(GRID_EDIT_LAYOUT_APPLY_ABORTED, "The prepared Variant is no longer available.");
	}
	gridMatchRuntimeReleasePrivateCandidate(&s_EditReview.candidate);
	if (!s_EditReview.sources) return;
	gridMatchCacheReset(&s_EditReview.sources);
	sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW.PREPARED: released serial=%llu reason=%s",
		(unsigned long long)s_EditReview.offer.review.offer_serial, reason);
}

static void editReleaseReviewTransfer(const char *reason)
{
	if (!s_EditReview.transfer_active) return;
	gridMatchTransferReset(&s_EditReview.transfer);
	s_EditReview.transfer_active = 0;
	sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW.TRANSFER: released serial=%llu reason=%s",
		(unsigned long long)s_EditReview.offer.review.offer_serial, reason);
}

static void editClearReview(const char *reason)
{
	editReleaseReviewTransfer(reason);
	editReleaseReviewSources(reason);
	memset(&s_EditReview, 0, sizeof(s_EditReview));
}

static void editClearSmokeLayoutReply(void)
{
	memset(&s_EditSmokeLayoutReply, 0, sizeof(s_EditSmokeLayoutReply));
}

static void editClearLayout(void)
{
	if (s_EditLayout.layout) gridMatchRuntimeReleasePrivateLayout(s_EditLayout.layout);
	gridMatchCacheReset(&s_EditLayout.ready_sources);
	memset(&s_EditLayout, 0, sizeof(s_EditLayout));
}

static void editClearResume(void)
{
	free(s_EditResume.bytes);
	memset(&s_EditResume, 0, sizeof(s_EditResume));
}

static void editClearResumeRoomInvite(void)
{
	if (!s_EditResumeRoomInvite.connection) return;
	hub_room_t *room = roomGetById(s_EditResumeRoomInvite.room_id);
	if (s_EditResumeRoomInvite.owns_mask && room
			&& room->creator_client_id == s_EditResumeRoomInvite.host_id) {
		room->invited_client_mask &= ~(1u << s_EditResumeRoomInvite.client_id);
	}
	memset(&s_EditResumeRoomInvite, 0, sizeof(s_EditResumeRoomInvite));
	netRoomListMarkDirty();
}

static s32 editError(const char *reason)
{
	snprintf(s_EditError, sizeof(s_EditError), "%s", reason);
	return 0;
}

const char *netGridEditError(void)
{
	return s_EditError;
}

static void netGridEditReset(void)
{
	if (s_EditRoom && netGridEditPrivatePlaytestRoom(s_EditInvite.identity.room_id)) {
		netRoomListMarkDirty();
	}
	editClearResumeRoomInvite();
	editClearResume();
	editClearSmokeLayoutReply();
	memset(&s_EditPresentation, 0, sizeof(s_EditPresentation));
	memset(s_EditDecorationMoves, 0, sizeof(s_EditDecorationMoves));
	memset(&s_LocalEditFocus, 0, sizeof(s_LocalEditFocus));
	s_LastEditFocusSent = 0;
	if (s_EditPlaytest.phase == EDIT_PLAYTEST_REQUESTED) {
		memset(&s_EditPlaytest, 0, sizeof(s_EditPlaytest));
	}
	s_EditSourceOrigin = GRID_ARCHIVE_INVALID;
	memset(&s_EditSavedVariant, 0, sizeof(s_EditSavedVariant));
	memset(&s_EditStagedVariant, 0, sizeof(s_EditStagedVariant));
	editClearLayout();
	editClearReview("reset");
	s_EditCreatorLayoutGeneration = 0;
	s_EditCreatorProposalGeneration = 0;
	s_EditCreatorLayoutEpoch = 0;
	memset(&s_EditCreatorLayoutReceipt, 0, sizeof(s_EditCreatorLayoutReceipt));
	s_EditReviewSupported = 0;
	s_EditReviewCapabilities = 0;
	s_EditLastReviewSeen = 0;
	gridEditRoomFree(s_EditRoom);
	s_EditRoom = NULL;
	memset(&s_EditInvite, 0, sizeof(s_EditInvite));
	s_EditTarget = NET_NULL_CLIENT;
	s_EditConnection = 0;
	s_EditGuestActor = 0;
	s_EditOfferPending = 0;
	s_EditDeadline = 0;
	s_EditError[0] = '\0';
}

void netGridEditNetworkReset(void)
{
	memset(&s_EditPlaytest, 0, sizeof(s_EditPlaytest));
	netGridEditReset();
}

static u64 editConnection(const struct netclient *client)
{
	return client && client->peer ? (u64)client->peer->connectID + 1 : 0;
}

static s32 editAuthenticatedLobbyPeer(const struct netclient *client)
{
	return client && editConnection(client) && client->state == CLSTATE_LOBBY
		&& !(client->flags & CLFLAG_SPECTATOR)
		&& netPeerHasGridCapability(client);
}

static s32 editLobbyPeer(const struct netclient *client, u8 room_id)
{
	return room_id != 0xff && editAuthenticatedLobbyPeer(client) && client->room_id == room_id;
}

static s32 editHostOwnsCurrentRoom(void)
{
	if (netGridEditHostOwnsRoom()) return 1;
	if (!g_NetLocalClient || !netGridEditPrivatePlaytestRoom(g_NetLocalClient->room_id)) return 0;
	const hub_room_t *room = roomGetById(g_NetLocalClient->room_id);
	return room && room->creator_client_id == g_NetLocalClient->id;
}

static struct netclient *editHostTarget(u32 client_id)
{
	if (!editHostOwnsCurrentRoom() || client_id >= NET_MAX_CLIENTS) return NULL;
	struct netclient *client = &g_NetClients[client_id];
	const u8 room_id = g_NetLocalClient->room_id;
	if (client == g_NetLocalClient || !editLobbyPeer(client, room_id)) return NULL;
	const hub_room_t *room = roomGetById(room_id);
	for (u8 i = 0; room && i < room->client_count; i++) {
		if (room->clients[i] == client_id) return client;
	}
	return NULL;
}

static struct netclient *editBoundPeer(void)
{
	const u8 room_id = s_EditInvite.identity.room_id;
	struct netclient *client = NULL;
	if (g_NetMode == NETMODE_SERVER) {
		client = editHostTarget(s_EditTarget);
	} else if (g_NetMode == NETMODE_CLIENT) {
		client = g_NetLocalClient;
	}
	return editLobbyPeer(client, room_id) && editConnection(client) == s_EditConnection
		? client : NULL;
}

static s32 editSend(struct netclient *peer, struct netbuf *wire)
{
	const u32 size = wire->wp;
	return peer && size && !wire->error && netSend(peer, wire, 1, NETCHAN_CONTROL) == size;
}

static s32 editSendControl(struct netclient *peer, u8 opcode,
		const grid_edit_room_control_t *control)
{
	u8 data[1 + GRID_EDIT_ROOM_CONTROL_WIRE_MAX];
	struct netbuf wire = { .data = data, .size = sizeof(data) };
	netbufWriteU8(&wire, opcode);
	return gridEditRoomControlWrite(&wire, control)
		&& editSend(peer, &wire);
}

static u64 editHostCursorToken(void)
{
	return (s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_APPLY)
		? s_EditPlaytest.layout_receipt.apply_layout_generation : 0;
}

static s32 editSendCursorPolicy(struct netclient *peer, u8 allowed)
{
	u8 data[1 + 1 + sizeof(u64) + 1 + GRID_PLAYTEST_CURSOR_LAYOUT_PREFIX_SIZE];
	struct netbuf wire = { .data = data, .size = sizeof(data) };
	const u64 token = editHostCursorToken();
	netbufWriteU8(&wire, SVC_GRID_EDIT_CURSOR_POLICY);
	netbufWriteU8(&wire, s_EditInvite.identity.room_id);
	netbufWriteU64(&wire, s_EditInvite.identity.generation);
	netbufWriteU8(&wire, allowed);
	if (token) {
		netbufWriteU8(&wire, GRID_PLAYTEST_CURSOR_LAYOUT_VERSION);
		netbufWriteU64(&wire, token);
	}
	return editSend(peer, &wire);
}

static s32 editSendPlaytestView(struct netclient *peer, const grid_playtest_view_t *frame)
{
	u8 data[1 + GRID_PLAYTEST_WORLD_VIEW_WIRE_MAX];
	struct netbuf wire = { .data = data, .size = sizeof(data) };
	netbufWriteU8(&wire, SVC_GRID_EDIT_VIEW);
	const s32 world_view = frame->has_world_bounds && (s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_INTERACTIVE_VIEW);
	const s32 encoded = world_view
		? gridEditPlaytestWorldViewWrite(&wire, &s_EditInvite.identity, frame)
		: gridEditPlaytestViewWrite(&wire, &s_EditInvite.identity, frame);
	return encoded && editSend(peer, &wire);
}

static void editClearRemotePlaytestView(void)
{
	if (!s_EditPresentation.frame_sent) return;
	grid_playtest_view_t clear = {
		.epoch = s_EditPresentation.frame.epoch,
		.serial = ++s_EditPresentation.frame.serial,
	};
	if (editSendPlaytestView(editBoundPeer(), &clear)) s_EditPresentation.frame_sent = 0;
}

s32 netGridEditGetPlaytestVisibility(s32 *players, s32 *creators)
{
	return editHostOwnsCurrentRoom() && gridEditRoomGetVisibility(s_EditRoom, players, creators);
}

s32 netGridEditSetPlaytestVisibility(s32 players, s32 creators)
{
	if (!editHostOwnsCurrentRoom() || !gridEditRoomSetVisibility(s_EditRoom, players, creators)) return 0;
	if (!creators) editClearRemotePlaytestView();
	if (!players) s_EditPresentation.cursor_visible = 0;
	struct netclient *peer = editBoundPeer();
	if (peer) {
		s_EditPresentation.cursor_policy_dirty = s_EditLayout.phase == EDIT_LAYOUT_OUTCOME
			|| (!editSendCursorPolicy(peer, players != 0) && editHostCursorToken() != 0);
	}
	s_EditPresentation.sampled_at = 0;
	return 1;
}

s32 netGridEditGetPlaytestView(grid_playtest_view_t *out)
{
	grid_edit_room_state_t state;
	if (!out || g_NetMode != NETMODE_CLIENT || !editBoundPeer()
			|| !gridEditRoomGetState(s_EditRoom, &state) || !state.ready
			|| !s_EditPresentation.frame.actor_count
			|| (u32)(SDL_GetTicks() - s_EditPresentation.received_at) >= GRID_VIEW_EXPIRY_MS) return 0;
	*out = s_EditPresentation.frame;
	return 1;
}

s32 netGridEditGetObservedLayout(grid_edit_room_control_t *out)
{
	grid_playtest_view_t view;
	if (!out || !s_EditCreatorLayoutReceipt.apply_layout_generation || !netGridEditGetPlaytestView(&view)
			|| view.epoch != s_EditCreatorLayoutReceipt.review.stage_epoch) return 0;
	*out = s_EditCreatorLayoutReceipt;
	return 1;
}

void netGridEditSetCreatorCursor(const f32 *position)
{
	grid_edit_room_state_t state;
	struct netclient *peer = g_NetMode == NETMODE_CLIENT ? editBoundPeer() : NULL;
	if (!peer || !s_EditPresentation.cursor_granted
			|| !gridEditRoomGetState(s_EditRoom, &state) || !state.ready) return;
	const u8 visible = position && __builtin_isfinite(position[0]) && __builtin_isfinite(position[2]);
	const u32 now = SDL_GetTicks();
	if ((!visible && !s_EditPresentation.cursor_sent)
			|| (visible && s_EditPresentation.cursor_sent
				&& (u32)(now - s_EditPresentation.cursor_sent_at) < GRID_VIEW_INTERVAL_MS)) return;
	u8 data[1 + GRID_PLAYTEST_CURSOR_LAYOUT_WIRE_MAX];
	struct netbuf wire = { .data = data, .size = sizeof(data) };
	netbufWriteU8(&wire, CLC_GRID_EDIT_CURSOR);
	const f32 x = visible ? position[0] : 0.0f;
	const f32 z = visible ? position[2] : 0.0f;
	const s32 encoded = s_EditPresentation.cursor_token
		? gridEditPlaytestLayoutCursorWrite(&wire, &s_EditInvite.identity, s_EditPresentation.cursor_token, visible, x, z)
		: gridEditPlaytestCursorWrite(&wire, &s_EditInvite.identity, visible, x, z);
	if (encoded && editSend(peer, &wire)) {
		s_EditPresentation.cursor_sent = visible;
		s_EditPresentation.cursor_sent_at = now;
	}
}

void netGridEditSetResourceFocus(const grid_edit_resource_t *resource)
{
	const grid_edit_resource_t clear = {0};
	const grid_edit_resource_t *desired = resource ? resource : &clear;
	const u64 now = SDL_GetTicks64();
	const s32 same = desired->kind == s_LocalEditFocus.kind
		&& (desired->kind == GRID_EDIT_RESOURCE_AMBIENT_LIGHT || !desired->kind
			|| strncmp(desired->record_id, s_LocalEditFocus.record_id, GRID_EDIT_SPAWN_ID_CAP) == 0);
	if (same && (!resource || now - s_LastEditFocusSent < GRID_EDIT_LEASE_MS / 3)) return;
	grid_edit_room_state_t draft;
	struct netclient *peer = editBoundPeer();
	if (!peer || !gridEditRoomGetState(s_EditRoom, &draft) || !draft.ready) return;

	if (draft.is_host) {
		if (gridEditRoomHostFocusResource(s_EditRoom, draft.local_actor, 0, resource, now) == GRID_EDIT_LEASE_HELD) {
			editError("The other creator is editing this item.");
		}
	} else {
		u8 data[1 + GRID_EDIT_FOCUS_WIRE_MAX];
		struct netbuf wire = { .data = data, .size = sizeof(data) };
		netbufWriteU8(&wire, CLC_GRID_EDIT_FOCUS);
		if (!gridEditFocusResourceWrite(&wire, &s_EditInvite.identity, resource) || !editSend(peer, &wire)) return;
	}
	s_LocalEditFocus = *desired;
	s_LastEditFocusSent = now;
}

static s32 editSameIdentity(const grid_edit_wire_identity_t *identity)
{
	return s_EditConnection && identity->room_id == s_EditInvite.identity.room_id
		&& identity->generation == s_EditInvite.identity.generation;
}

static s32 editSendClose(struct netclient *peer)
{
	if (!editLobbyPeer(peer, s_EditInvite.identity.room_id) || editConnection(peer) != s_EditConnection) return 0;
	const grid_edit_room_control_t closed = {
		.kind = GRID_EDIT_ROOM_CONTROL_CLOSE,
		.invite.identity = s_EditInvite.identity,
	};
	const u8 opcode = g_NetMode == NETMODE_SERVER ? SVC_GRID_EDIT_CONTROL : CLC_GRID_EDIT_CONTROL;
	return editSendControl(peer, opcode, &closed);
}

static void editClosePeer(struct netclient *peer)
{
	editSendClose(peer);
	netGridEditReset();
}

static void editCloseBound(void)
{
	editClosePeer(editBoundPeer());
}

void netGridEditClose(void)
{
	editCloseBound();
}

static s32 editHostRoomStillOwned(void)
{
	return editHostOwnsCurrentRoom() && g_NetLocalClient->room_id == s_EditInvite.identity.room_id;
}

static s32 editKeepDraftAfterPeerDeparture(void)
{
	grid_edit_room_state_t state;
	if (!editHostRoomStillOwned() || !gridEditRoomGetState(s_EditRoom, &state)) return 0;
	s32 kept = state.pending_resume
		? gridEditRoomHostCancelInvite(s_EditRoom, s_EditGuestActor, s_EditConnection)
		: gridEditRoomHostDetachPeer(s_EditRoom, s_EditGuestActor, s_EditConnection);
	if (!kept) return 0;
	editClearResume();
	editClearSmokeLayoutReply();
	s_EditTarget = NET_NULL_CLIENT;
	s_EditConnection = 0;
	s_EditGuestActor = 0;
	editClearLayout();
	editClearReview("creator-detached");
	s_EditReviewSupported = 0;
	s_EditReviewCapabilities = 0;
	s_EditDeadline = 0;
	memset(&s_EditPresentation, 0, sizeof(s_EditPresentation));
	s_EditError[0] = '\0';
	netRoomListMarkDirty();
	sysLogPrintf(LOG_NOTE, "NET.GRID.EDIT: host kept the committed draft after creator left");
	return 1;
}

static void editEndCreator(struct netclient *peer, const char *reason)
{
	editSendClose(peer);
	if (!editKeepDraftAfterPeerDeparture()) netGridEditReset();
	editError(reason);
}

s32 netGridEditPrivatePlaytestRoom(u8 room_id)
{
	return g_NetMode == NETMODE_SERVER && !g_NetDedicated && room_id != 0xff
		&& s_EditPlaytest.phase >= EDIT_PLAYTEST_LOADING && s_EditPlaytest.draft.room_id == room_id;
}

s32 netGridEditPrivateLaunchMatches(u8 room_id, const char *arena_id)
{
	return netGridEditHostOwnsRoom() && arena_id && s_EditPlaytest.phase == EDIT_PLAYTEST_REQUESTED
		&& s_EditPlaytest.draft.room_id == room_id && strcmp(s_EditPlaytest.arena_id, arena_id) == 0
		&& s_EditPlaytest.draft.generation == s_EditInvite.identity.generation && editBoundPeer();
}

void netGridEditPrivatePlaytestFailed(u8 room_id)
{
	if (s_EditPlaytest.phase != EDIT_PLAYTEST_NONE && s_EditPlaytest.draft.room_id == room_id) {
		editClearResumeRoomInvite();
		editClearRemotePlaytestView();
		s_EditPresentation.cursor_visible = 0;
		memset(s_EditDecorationMoves, 0, sizeof(s_EditDecorationMoves));
		memset(&s_EditPlaytest, 0, sizeof(s_EditPlaytest));
		netRoomListMarkDirty();
	}
}

static s32 editPlaytestArenaMatches(const grid_match_runtime_identity_t *arena)
{
	if (!arena || arena->offer.room_id != s_EditPlaytest.draft.room_id
			|| strcmp(arena->arena_id, s_EditPlaytest.arena_id) != 0) return 0;
	char digest[SHA256_HEX_SIZE];
	sha256ToHex(arena->arena_sha256, digest);
	return strcmp(digest, s_EditPlaytest.arena_sha256) == 0;
}

static s32 editActivePlaytestWorld(u8 room_id)
{
	return s_EditPlaytest.phase == EDIT_PLAYTEST_ACTIVE
		&& netGridEditPrivatePlaytestRoom(room_id) && g_NetMatchRoomId == room_id
		&& g_NetStageEpoch && s_EditPlaytest.stage_epoch == g_NetStageEpoch
		&& g_NetLocalClient && g_NetLocalClient->room_id == room_id
		&& g_NetLocalClient->state == CLSTATE_GAME && g_NetLocalClient->stage_ready
		&& editHostOwnsCurrentRoom() && editPlaytestArenaMatches(gridMatchRuntimeActive());
}

s32 netGridEditPrivateResumeRoomOpen(u8 room_id)
{
	grid_edit_room_state_t draft;
	if (s_EditSourceOrigin != GRID_ARCHIVE_ORIGINAL || !editActivePlaytestWorld(room_id)
			|| !gridEditRoomGetState(s_EditRoom, &draft) || !draft.is_host || draft.ready
			|| !draft.creator_detached || draft.invite.identity.room_id != room_id) return 0;
	if (draft.pending_resume) {
		return s_EditResume.live_epoch == g_NetStageEpoch
			&& s_EditResume.live_generation == s_EditPlaytest.draft.generation;
	}
	return !s_EditConnection && !draft.pending_invite
		&& draft.invite.identity.generation >= s_EditPlaytest.draft.generation;
}

static s32 editRoomInviteMatches(u8 room_id, const struct netclient *client)
{
	return client && s_EditResumeRoomInvite.connection
		&& room_id == s_EditResumeRoomInvite.room_id
		&& client->id == s_EditResumeRoomInvite.client_id
		&& editConnection(client) == s_EditResumeRoomInvite.connection;
}

s32 netGridEditPrivateResumeCanJoin(u8 room_id, const struct netclient *client)
{
	if (!client || client->id >= NET_MAX_CLIENTS || client != &g_NetClients[client->id]
			|| !editAuthenticatedLobbyPeer(client)
			|| !netGridEditPrivateResumeRoomOpen(room_id)) return 0;
	const hub_room_t *room = roomGetById(room_id);
	return room && (room->access != ROOM_ACCESS_INVITE || editRoomInviteMatches(room_id, client));
}

void netGridEditPrivateResumeAdmitted(u8 room_id, const struct netclient *client)
{
	if (editRoomInviteMatches(room_id, client)) editClearResumeRoomInvite();
}

static struct netclient *editResumeRoomInviteTarget(u32 client_id)
{
	if (s_EditConnection || !g_NetLocalClient || client_id >= NET_MAX_CLIENTS) return NULL;
	const u8 room_id = g_NetLocalClient->room_id;
	if (!netGridEditPrivateResumeRoomOpen(room_id)) return NULL;
	const hub_room_t *room = roomGetById(room_id);
	struct netclient *client = &g_NetClients[client_id];
	if (!room || room->access != ROOM_ACCESS_INVITE || client->id != client_id
			|| client->room_id == room_id || !editAuthenticatedLobbyPeer(client)
			|| editRoomInviteMatches(room_id, client)
			|| !roomCanJoin(room, client_id)) return NULL;
	return client;
}

s32 netGridEditCanInviteToResumeRoom(u32 client_id)
{
	return editResumeRoomInviteTarget(client_id) != NULL;
}

s32 netGridEditInviteToResumeRoom(u32 client_id)
{
	struct netclient *client = editResumeRoomInviteTarget(client_id);
	if (!client) return editError("Choose a connected Grid player outside this Room.");
	const u8 room_id = g_NetLocalClient->room_id;
	hub_room_t *room = roomGetById(room_id);
	const s32 owns_mask = !(room->invited_client_mask & (1u << client_id));
	const room_result_t result = roomInviteClientDuringPrivatePlaytest(
		room, (u8)g_NetLocalClient->id, (u8)client_id);
	if (result != ROOM_RESULT_OK) return editError(roomResultMessage(result));
	editClearResumeRoomInvite();
	s_EditResumeRoomInvite.connection = editConnection(client);
	s_EditResumeRoomInvite.room_id = room_id;
	s_EditResumeRoomInvite.host_id = g_NetLocalClient->id;
	s_EditResumeRoomInvite.client_id = client->id;
	s_EditResumeRoomInvite.owns_mask = owns_mask;
	s_EditError[0] = '\0';
	netRoomListMarkDirty();
	sysLogPrintf(LOG_NOTE, "NET.GRID.EDIT: invited lobby client=%u to rejoin room=%u",
		client->id, (unsigned)room_id);
	return 1;
}

void netGridEditMatchStarting(u8 room_id)
{
	if (netGridEditPrivateLaunchMatches(room_id, s_EditPlaytest.arena_id)) {
		if (editPlaytestArenaMatches(gridMatchRuntimePending())) {
			s_EditPlaytest.phase = EDIT_PLAYTEST_LOADING;
			sysLogPrintf(LOG_NOTE, "GRID.PLAYTEST: prepared room=%u arena=%s",
				room_id, s_EditPlaytest.arena_id);
		}
		return;
	}
	if ((s_EditConnection || s_EditRoom) && s_EditInvite.identity.room_id == room_id) editCloseBound();
}

static s32 editStagedVariantMatchesDraft(const grid_edit_room_state_t *draft)
{
	const grid_edit_room_invite_t *source = &s_EditStagedVariant.source;
	return s_EditStagedVariant.pin.path[0] && draft->is_host
		&& draft->invite.identity.room_id == source->identity.room_id
		&& strcmp(draft->invite.arena_id, source->arena_id) == 0
		&& strcmp(draft->invite.arena_sha256, source->arena_sha256) == 0;
}

static s32 editStagedLayoutApplied(void)
{
	if (!s_EditPlaytest.layout_receipt.apply_layout_generation) return 0;
	char arena_sha[SHA256_HEX_SIZE];
	sha256ToHex(s_EditPlaytest.layout_receipt.review.arena_sha256, arena_sha);
	return strcmp(arena_sha, s_EditStagedVariant.pin.digest) == 0;
}

s32 netGridEditGetStagedVariant(grid_edit_staged_variant_info_t *out)
{
	grid_edit_room_state_t draft;
	if (!out || !editHostRoomStillOwned() || !gridEditRoomGetState(s_EditRoom, &draft)
			|| !editStagedVariantMatchesDraft(&draft)) return 0;
	snprintf(out->arena_id, sizeof(out->arena_id), "%s", s_EditStagedVariant.pin.arena_id);
	snprintf(out->arena_sha256, sizeof(out->arena_sha256), "%s", s_EditStagedVariant.pin.digest);
	out->draft_revision = s_EditStagedVariant.draft_revision;
	out->current_revision = draft.revision;
	out->applied_layout_generation = editStagedLayoutApplied()
		? s_EditPlaytest.layout_receipt.apply_layout_generation : 0;
	return 1;
}

static s32 editReviewDraft(grid_edit_room_state_t *draft)
{
	return editBoundPeer() && s_EditResume.phase == EDIT_RESUME_NONE
		&& gridEditRoomGetState(s_EditRoom, draft) && draft->ready;
}

static s32 editReviewHostScope(grid_edit_room_state_t *draft)
{
	s32 players, creators;
	return g_NetMode == NETMODE_SERVER && s_EditReviewSupported && editReviewDraft(draft)
		&& editStagedVariantMatchesDraft(draft) && draft->revision == s_EditStagedVariant.draft_revision
		&& !editStagedLayoutApplied()
		&& editActivePlaytestWorld(draft->invite.identity.room_id)
		&& gridEditRoomGetVisibility(s_EditRoom, &players, &creators) && creators;
}

static s32 editTransferReview(grid_edit_room_control_kind_e kind)
{
	return kind == GRID_EDIT_ROOM_CONTROL_REVIEW_TRANSFER_OFFER;
}

static s32 editPreparedReview(grid_edit_room_control_kind_e kind)
{
	return kind == GRID_EDIT_ROOM_CONTROL_REVIEW_PREPARED_OFFER || editTransferReview(kind);
}

static s32 editReviewTransferSupported(void)
{
	const u8 caps = GRID_EDIT_REVIEW_CAP_DEPS_V1 | GRID_EDIT_REVIEW_CAP_PREPARED
		| GRID_EDIT_REVIEW_CAP_TRANSFER;
	return (s_EditReviewCapabilities & caps) == caps;
}

static s32 editReviewHasDependencies(grid_edit_room_control_kind_e kind)
{
	return kind == GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_OFFER || editPreparedReview(kind);
}

static s32 editIsReviewOffer(grid_edit_room_control_kind_e kind)
{
	return kind == GRID_EDIT_ROOM_CONTROL_REVIEW_OFFER || editReviewHasDependencies(kind);
}

static grid_match_offer_t editReviewSourcesKey(const grid_edit_room_control_t *offer)
{
	grid_match_offer_t key = { .room_id = offer->invite.identity.room_id, .generation = offer->review.offer_serial };
	sha256ToHex(offer->review_deps_sha256, key.manifest_digest);
	return key;
}

static s32 editReviewSourcesReady(const grid_edit_room_control_t *offer, const grid_match_cache_t *sources)
{
	if (!sources || !editPreparedReview(offer->kind)) return 0;
	const grid_match_offer_t key = editReviewSourcesKey(offer);
	return gridMatchCacheReady(sources, &key);
}

/* Choose the original staged arena's own archive from the ready cache. Its
 * embedded scenario travels inside this package; other packages stay local. */
static s32 editSelectReviewTransferArena(const grid_edit_room_control_t *offer,
		const grid_match_cache_t *sources, u16 *package, u32 *bytes)
{
	const grid_match_offer_t key = editReviewSourcesKey(offer);
	for (u16 i = 0; i < MANIFEST_MAX_ENTRIES; ++i) {
		const grid_match_source_package_t *entry = gridMatchCachePackage(sources, &key, i);
		if (!entry) return 0;
		if (entry->owner.type != ASSET_ARENA || strcmp(entry->owner.id, offer->review.arena_id) != 0) continue;
		u32 size = 0;
		const void *arena = gridMatchCacheRead(sources, &key, i, &size);
		if (!arena || !size || size != entry->size || size > NET_DISTRIB_MAX_COMP || entry->bundled
				|| strcmp(entry->suffix, ".pdarena") != 0
				|| memcmp(entry->digest, offer->review.arena_sha256, SHA256_DIGEST_SIZE) != 0
				|| gridArchiveOrigin(entry->suffix, arena, size) != GRID_ARCHIVE_ORIGINAL) return 0;
		*package = i;
		*bytes = size;
		return 1;
	}
	return 0;
}

static void editLogReviewSourcesRetained(const char *role)
{
	char digest[SHA256_HEX_SIZE];
	sha256ToHex(s_EditReview.offer.review_deps_sha256, digest);
	sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW.PREPARED: retained role=%s serial=%llu sha256=%s",
		role, (unsigned long long)s_EditReview.offer.review.offer_serial, digest);
}

static s32 editReviewExpired(void)
{
	return (s32)(SDL_GetTicks() - s_EditReview.deadline) >= 0;
}

static s32 editCandidateGameplayCurrent(u8 room_id)
{
	u8 gameplay[SHA256_DIGEST_SIZE];
	return s_EditPlaytest.gameplay_fingerprint_ready
		&& s_EditPlaytest.participant_mask == mpParticipantsEncodeActiveMask()
		&& netmsgGridPrivateGameplayFingerprint(room_id, gameplay)
		&& memcmp(gameplay, s_EditPlaytest.gameplay_fingerprint, sizeof(gameplay)) == 0;
}

static s32 editCandidateSourcesCurrent(void)
{
	const grid_edit_room_control_t *offer = &s_EditReview.offer;
	if (!s_EditReview.candidate.cache || offer->review.stage_epoch != g_NetStageEpoch) return 0;
	const grid_match_offer_t arena = editReviewSourcesKey(offer);
	return editActivePlaytestWorld(arena.room_id)
		&& gridMatchRuntimePrivateCandidateCurrent(&s_EditReview.candidate, &arena);
}

static void editCheckPreparedCandidate(void)
{
	if (!s_EditReview.candidate.cache) return;
	const u32 now = SDL_GetTicks();
	if (now - s_EditReview.candidate_checked_at < GRID_VIEW_INTERVAL_MS) return;
	s_EditReview.candidate_checked_at = now;
	if (editCandidateSourcesCurrent() && editCandidateGameplayCurrent(s_EditReview.offer.invite.identity.room_id)) return;
	const char *reason = assetCatalogPreviewGridArenaLeaseStage(s_EditReview.candidate.scenario_id) < 0
		? "No native stage slot is currently available for this arena."
		: "The private playtest or its catalog sources changed after the review.";
	snprintf(s_EditReview.candidate_issue, sizeof(s_EditReview.candidate_issue), "%s", reason);
	if (s_EditLayout.layout) editFinishLayout(GRID_EDIT_LAYOUT_APPLY_ABORTED, reason);
	gridMatchRuntimeReleasePrivateCandidate(&s_EditReview.candidate);
	sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW.CANDIDATE: invalidated serial=%llu",
		(unsigned long long)s_EditReview.offer.review.offer_serial);
}

static s32 editReviewMatchesStaged(const grid_edit_variant_review_t *review)
{
	char sha[SHA256_HEX_SIZE];
	sha256ToHex(review->arena_sha256, sha);
	return review->edited_room == s_EditStagedVariant.edited_room
		&& review->extension == s_EditStagedVariant.extension
		&& strcmp(review->arena_id, s_EditStagedVariant.pin.arena_id) == 0
		&& strcmp(sha, s_EditStagedVariant.pin.digest) == 0;
}

static s32 editReviewMatchesCurrent(void)
{
	const grid_edit_room_control_t *offer = &s_EditReview.offer;
	grid_edit_room_state_t draft;
	if (!editIsReviewOffer(offer->kind) || !editSameIdentity(&offer->invite.identity)
			|| s_EditReview.connection != s_EditConnection) return 0;
	if (editTransferReview(offer->kind) && !editReviewTransferSupported()) return 0;
	if (editReviewHasDependencies(offer->kind)
			&& (g_NetMode == NETMODE_SERVER || s_EditReview.transfer_active
				|| s_EditReview.reply == GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED)
			&& s_EditReview.catalog_generation != assetCatalogGetGeneration()) return 0;
	if (g_NetMode == NETMODE_SERVER) {
		return editReviewHostScope(&draft) && draft.peer_actor == s_EditReview.actor
			&& editReviewMatchesStaged(&offer->review)
			&& offer->review.stage_epoch == g_NetStageEpoch && offer->review.draft_revision == draft.revision;
	}
	return g_NetMode == NETMODE_CLIENT && editReviewDraft(&draft) && draft.local_actor == s_EditReview.actor
		&& offer->review.draft_revision == draft.revision && offer->review.stage_epoch >= s_EditPresentation.frame.epoch;
}

static void editCancelReview(const char *reason)
{
	const grid_edit_room_control_kind_e kind = s_EditReview.offer.kind;
	if (editIsReviewOffer(kind)
			&& (g_NetMode == NETMODE_SERVER || (g_NetMode == NETMODE_CLIENT && editPreparedReview(kind)))
			&& s_EditReview.connection == s_EditConnection && editSameIdentity(&s_EditReview.offer.invite.identity)) {
		grid_edit_room_control_t cancelled = s_EditReview.offer;
		cancelled.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_CANCEL;
		cancelled.review_arena_size = 0;
		memset(cancelled.review_deps_sha256, 0, sizeof(cancelled.review_deps_sha256));
		const u8 opcode = g_NetMode == NETMODE_SERVER ? SVC_GRID_EDIT_CONTROL : CLC_GRID_EDIT_CONTROL;
		editSendControl(editBoundPeer(), opcode, &cancelled);
	}
	editClearReview(reason);
}

static s32 editReadReviewDigest(const char *hex, u8 digest[SHA256_DIGEST_SIZE])
{
	static const char digits[] = "0123456789abcdef";
	if (strlen(hex) != SHA256_HEX_SIZE - 1) return 0;
	for (size_t i = 0; i < SHA256_DIGEST_SIZE; ++i) {
		const char *hi = strchr(digits, hex[2 * i]);
		const char *lo = strchr(digits, hex[2 * i + 1]);
		if (!hi || !lo) return 0;
		digest[i] = (u8)((hi - digits) * 16 + (lo - digits));
	}
	return strspn(hex, "0") != SHA256_HEX_SIZE - 1;
}

static s32 editReviewCanSend(void)
{
	return !s_EditReview.offer.kind || (editReviewHasDependencies(s_EditReview.offer.kind)
		&& (s_EditReview.reply == GRID_EDIT_VARIANT_REVIEW_ACKNOWLEDGED
			|| s_EditReview.reply == GRID_EDIT_VARIANT_REVIEW_SOURCE_VERIFIED));
}

s32 netGridEditCanOfferStagedReview(void)
{
	grid_edit_room_state_t draft;
	return editReviewCanSend() && editReviewHostScope(&draft);
}

s32 netGridEditCanOfferStagedReviewTransfer(void)
{
	return editReviewTransferSupported() && netGridEditCanOfferStagedReview();
}

static s32 editOfferStagedReview(s32 allow_transfer)
{
	grid_edit_room_state_t draft;
	if (!editReviewCanSend() || !editReviewHostScope(&draft) || s_EditReviewSerial == UINT64_MAX) {
		return editError("Review requires the invited creator and an unchanged staged draft in the active playtest.");
	}
	if (allow_transfer && !editReviewTransferSupported()) {
		return editError("This creator does not support temporary arena transfer for review.");
	}
	const s32 try_deps = (s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_DEPS_V1) != 0;
	const u64 connection = s_EditConnection;
	const u64 peer_actor = draft.peer_actor;
	const u32 catalog_generation = assetCatalogGetGeneration();
	grid_edit_room_control_t offer = { .kind = GRID_EDIT_ROOM_CONTROL_REVIEW_OFFER };
	offer.invite.identity = draft.invite.identity;
	offer.review.offer_serial = s_EditReviewSerial + 1;
	offer.review.draft_revision = s_EditStagedVariant.draft_revision;
	offer.review.stage_epoch = g_NetStageEpoch;
	offer.review.edited_room = s_EditStagedVariant.edited_room;
	offer.review.extension = s_EditStagedVariant.extension;
	snprintf(offer.review.arena_id, sizeof(offer.review.arena_id), "%s", s_EditStagedVariant.pin.arena_id);
	if (!editReadReviewDigest(s_EditStagedVariant.pin.digest, offer.review.arena_sha256)
			|| !editReadReviewDigest(draft.invite.arena_sha256, offer.review.parent_sha256)) {
		return editError("The staged Variant has no exact revision to review.");
	}

	grid_match_offer_t sources_key = {0};
	grid_match_cache_t *sources = NULL;
	u16 transfer_package = 0;
	if (try_deps) {
		char reason[sizeof(s_EditError)] = {0};
		if (forgeLevelPrepareStagedArenaSources(s_EditStagedVariant.pin.path, offer.review.arena_id,
				s_EditStagedVariant.pin.digest, offer.invite.identity.room_id, offer.review.offer_serial,
				&sources_key, &sources, reason, sizeof(reason))
				&& editReadReviewDigest(sources_key.manifest_digest, offer.review_deps_sha256)) {
			offer.kind = (s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_PREPARED)
				? GRID_EDIT_ROOM_CONTROL_REVIEW_PREPARED_OFFER : GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_OFFER;
		} else {
			memset(offer.review_deps_sha256, 0, sizeof(offer.review_deps_sha256));
			sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW.DEPS: unavailable serial=%llu reason='%s'",
				(unsigned long long)offer.review.offer_serial, reason[0] ? reason : "invalid source dependency digest");
		}
		if (allow_transfer) {
			if (!editPreparedReview(offer.kind)
					|| !editSelectReviewTransferArena(&offer, sources, &transfer_package, &offer.review_arena_size)) {
				gridMatchCacheReset(&sources);
				return editError(reason[0] ? reason
					: "Only the exact staged original arena with verified dependencies can be shared for review.");
			}
			offer.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_TRANSFER_OFFER;
		}
		/* Verification may have taken long enough to lose the review scope. */
		if (!editReviewCanSend() || !editReviewHostScope(&draft) || !editSameIdentity(&offer.invite.identity)
				|| connection != s_EditConnection || peer_actor != draft.peer_actor
				|| offer.review.draft_revision != draft.revision || offer.review.stage_epoch != g_NetStageEpoch
				|| !editReviewMatchesStaged(&offer.review) || s_EditReviewSerial != offer.review.offer_serial - 1
				|| (editReviewHasDependencies(offer.kind) && catalog_generation != assetCatalogGetGeneration())
				|| (editPreparedReview(offer.kind) && !editReviewSourcesReady(&offer, sources))
				|| (allow_transfer && !editReviewTransferSupported())) {
			gridMatchCacheReset(&sources);
			return editError("This staged Variant review is no longer current.");
		}
	}
	if (!editPreparedReview(offer.kind)) gridMatchCacheReset(&sources);
	if (!editSendControl(editBoundPeer(), SVC_GRID_EDIT_CONTROL, &offer)) {
		gridMatchCacheReset(&sources);
		return editError("Could not send the staged Variant for review.");
	}
	editClearReview("replaced");
	s_EditReviewSerial = offer.review.offer_serial;
	s_EditReview.offer = offer;
	s_EditReview.sources = sources;
	s_EditReview.transfer_package = transfer_package;
	s_EditReview.connection = connection;
	s_EditReview.actor = peer_actor;
	s_EditReview.deadline = SDL_GetTicks() + GRID_EDIT_REVIEW_TIMEOUT_MS;
	s_EditReview.catalog_generation = catalog_generation;
	s_EditError[0] = '\0';
	sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW: offered creator=%u serial=%llu epoch=%u revision=%llu arena=%s",
		s_EditTarget, (unsigned long long)offer.review.offer_serial, (unsigned)offer.review.stage_epoch,
		(unsigned long long)offer.review.draft_revision, offer.review.arena_id);
	if (editReviewHasDependencies(offer.kind)) {
		sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW.DEPS: offered serial=%llu sha256=%s",
			(unsigned long long)offer.review.offer_serial, sources_key.manifest_digest);
	}
	if (sources) editLogReviewSourcesRetained("host");
	if (editTransferReview(offer.kind)) {
		sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW.TRANSFER: offered creator=%u serial=%llu arena=%s bytes=%u",
			s_EditTarget, (unsigned long long)offer.review.offer_serial, offer.review.arena_id,
			offer.review_arena_size);
	}
	return 1;
}

s32 netGridEditOfferStagedReview(void)
{
	return editOfferStagedReview(0);
}

s32 netGridEditOfferStagedReviewTransfer(void)
{
	return editOfferStagedReview(1);
}

static s32 editReviewTransferAvailable(void)
{
	return g_NetMode == NETMODE_CLIENT && editTransferReview(s_EditReview.offer.kind)
		&& !s_EditReview.reply && !s_EditReview.transfer_active
		&& !assetCatalogResolveAny(s_EditReview.offer.review.arena_id);
}

s32 netGridEditGetStagedReview(grid_edit_variant_review_info_t *out)
{
	if (!out || !editReviewMatchesCurrent()
			|| (editPreparedReview(s_EditReview.offer.kind) && editReviewExpired())) return 0;
	out->variant = s_EditReview.offer.review;
	out->reply = s_EditReview.reply;
	out->includes_dependencies = editReviewHasDependencies(s_EditReview.offer.kind);
	out->sources_prepared = s_EditReview.reply == GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED
		&& editReviewSourcesReady(&s_EditReview.offer, s_EditReview.sources);
	out->match_sources_prepared = g_NetMode == NETMODE_SERVER && out->sources_prepared
		&& editCandidateSourcesCurrent()
		&& s_EditPlaytest.participant_mask == mpParticipantsEncodeActiveMask();
	const s32 show_issue = g_NetMode == NETMODE_SERVER && out->sources_prepared && !out->match_sources_prepared;
	snprintf(out->match_preparation_issue, sizeof(out->match_preparation_issue), "%s",
		show_issue ? s_EditReview.candidate_issue : "");
	out->transfer_offered = editTransferReview(s_EditReview.offer.kind);
	out->transfer_available = editReviewTransferAvailable();
	out->transfer_active = s_EditReview.transfer_active;
	out->apply_pending = s_EditLayout.phase != EDIT_LAYOUT_NONE;
	out->arena_bytes = s_EditReview.offer.review_arena_size;
	out->transferred_bytes = s_EditReview.transfer_offset;
	return 1;
}

static s32 editLayoutHostReady(void)
{
	const u8 caps = GRID_EDIT_REVIEW_CAP_DEPS_V1 | GRID_EDIT_REVIEW_CAP_PREPARED | GRID_EDIT_REVIEW_CAP_APPLY;
	const u64 generation = s_EditPlaytest.layout_receipt.apply_layout_generation;
	if ((s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_APPLY_BASELINE)
			&& generation && generation != s_EditPresentation.announced_layout_generation) return 0;
	for (size_t i = 0; i < ARRAYCOUNT(s_EditDecorationMoves); ++i) {
		if (g_NetStageEpoch && s_EditDecorationMoves[i].epoch == g_NetStageEpoch) return 0;
	}
	return g_NetMode == NETMODE_SERVER && (s_EditReviewCapabilities & caps) == caps
		&& !g_MainIsEndscreen && !mainStageChangePending()
		&& s_EditReview.reply == GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED && !editReviewExpired()
		&& editReviewMatchesCurrent() && editReviewSourcesReady(&s_EditReview.offer, s_EditReview.sources)
		&& editCandidateSourcesCurrent() && editCandidateGameplayCurrent(s_EditInvite.identity.room_id);
}

s32 netGridEditCanApplyStagedLayout(void)
{
	return s_EditLayout.phase == EDIT_LAYOUT_NONE && s_EditNextLayoutGeneration < UINT64_MAX
		&& gridMatchRuntimePrivateLayoutGeneration() <= s_EditNextLayoutGeneration && editLayoutHostReady();
}

static s32 editLayoutMatchesReview(const grid_edit_room_control_t *intent)
{
	return editPreparedReview(s_EditReview.offer.kind) && editSameIdentity(&intent->invite.identity)
		&& gridEditVariantReviewMatches(&intent->review, &s_EditReview.offer.review)
		&& memcmp(intent->review_deps_sha256, s_EditReview.offer.review_deps_sha256, SHA256_DIGEST_SIZE) == 0;
}

static s32 editLayoutMatchesCandidate(const grid_edit_room_control_t *intent)
{
	u8 manifest[SHA256_DIGEST_SIZE];
	return editLayoutMatchesReview(intent)
		&& editReadReviewDigest(s_EditReview.candidate.offer.manifest_digest, manifest)
		&& memcmp(manifest, intent->apply_manifest_sha256, sizeof(manifest)) == 0
		&& intent->apply_previous_layout_generation == gridMatchRuntimePrivateLayoutGeneration();
}

static void editLogLayout(const char *event, const char *side, const grid_edit_room_control_t *intent, u64 actor)
{
	char digest[SHA256_HEX_SIZE];
	char arena_sha[SHA256_HEX_SIZE];
	sha256ToHex(intent->apply_manifest_sha256, digest);
	sha256ToHex(intent->review.arena_sha256, arena_sha);
	sysLogPrintf(LOG_NOTE,
		"NET.GRID.LAYOUT: event=%s side=%s room=%u epoch=%u previous=%llu next=%llu serial=%llu actor=%llu result=%u sha256=%s arena=%s arena_sha256=%s draft_generation=%llu",
		event, side, (unsigned)intent->invite.identity.room_id, (unsigned)intent->review.stage_epoch,
		(unsigned long long)intent->apply_previous_layout_generation, (unsigned long long)intent->apply_layout_generation,
		(unsigned long long)intent->review.offer_serial, (unsigned long long)actor, (unsigned)intent->apply_result,
		digest, intent->review.arena_id, arena_sha, (unsigned long long)intent->invite.identity.generation);
}

static s32 editLayoutReceiptMatchesDraft(const grid_edit_room_control_t *receipt, const grid_edit_room_state_t *draft)
{
	u8 parent[SHA256_DIGEST_SIZE];
	return receipt->review.draft_revision <= draft->revision
		&& strcmp(receipt->review.arena_id, draft->invite.arena_id) != 0
		&& editReadReviewDigest(draft->invite.arena_sha256, parent)
		&& memcmp(receipt->review.parent_sha256, parent, sizeof(parent)) == 0;
}

/* The original layout tuple survives creator detachment. Rebind only its
 * control identity after a fresh grant, ahead of that actor's projection. */
static s32 editEnsureLayoutBaseline(struct netclient *peer)
{
	const u8 caps = GRID_EDIT_REVIEW_CAP_APPLY | GRID_EDIT_REVIEW_CAP_APPLY_BASELINE;
	const grid_edit_room_control_t *receipt = &s_EditPlaytest.layout_receipt;
	const u64 generation = receipt->apply_layout_generation;
	if ((s_EditReviewCapabilities & caps) != caps || !generation
			|| s_EditPresentation.announced_layout_generation == generation) return 1;
	grid_edit_room_state_t draft;
	if (peer != editBoundPeer() || !editReviewDraft(&draft) || !draft.is_host
			|| draft.peer_actor != s_EditGuestActor || !editActivePlaytestWorld(draft.invite.identity.room_id)
			|| draft.invite.identity.generation != s_EditPlaytest.draft.generation
			|| receipt->invite.identity.room_id != draft.invite.identity.room_id
			|| receipt->review.stage_epoch != g_NetStageEpoch
			|| generation != gridMatchRuntimePrivateLayoutGeneration()
			|| !editLayoutReceiptMatchesDraft(receipt, &draft)) return 0;
	grid_edit_room_control_t baseline = *receipt;
	baseline.invite.identity = draft.invite.identity;
	if (!editSendControl(peer, SVC_GRID_EDIT_CONTROL, &baseline)) return 0;
	s_EditPresentation.announced_layout_generation = generation;
	editLogLayout("baseline", "host", &baseline, draft.peer_actor);
	return 1;
}

static void editReplaySmokeLayoutReady(void)
{
	if (!s_EditSmokeLayoutReply.timed_out || !editBoundPeer()
			|| s_EditSmokeLayoutReply.connection != s_EditConnection || s_EditSmokeLayoutReply.actor != s_EditGuestActor) return;
	const grid_edit_room_control_t held = s_EditSmokeLayoutReply.reply;
	const grid_edit_room_control_t *current = &s_EditLayout.intent;
	if (held.review.stage_epoch != current->review.stage_epoch
			|| held.review.offer_serial != current->review.offer_serial
			|| held.apply_layout_generation >= current->apply_layout_generation) return;
	editClearSmokeLayoutReply();
	editReceiveLayoutControl(0, &held);
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LAYOUT.RETRY: replayed epoch=%u serial=%llu stale=%llu waiting=%llu ignored=%d",
		held.review.stage_epoch, (unsigned long long)held.review.offer_serial,
		(unsigned long long)held.apply_layout_generation, (unsigned long long)current->apply_layout_generation,
		s_EditLayout.phase == EDIT_LAYOUT_WAITING_REPLY);
}

s32 netGridEditApplyStagedLayout(void)
{
	if (!netGridEditCanApplyStagedLayout()) {
		return editError("Apply requires a current prepared Variant and an invited creator who supports live layout changes.");
	}
	grid_edit_room_control_t intent = s_EditReview.offer;
	const grid_match_offer_t arena = editReviewSourcesKey(&intent);
	intent.kind = GRID_EDIT_ROOM_CONTROL_APPLY_PREPARE;
	intent.review_arena_size = 0;
	intent.review_reply = 0;
	intent.apply_previous_layout_generation = gridMatchRuntimePrivateLayoutGeneration();
	intent.apply_layout_generation = s_EditNextLayoutGeneration + 1;
	if (!editReadReviewDigest(s_EditReview.candidate.offer.manifest_digest, intent.apply_manifest_sha256)) {
		return editError("The prepared layout has no exact match source identity.");
	}
	grid_match_private_layout_t *layout = NULL;
	char err[sizeof(s_EditError)] = {0};
	if (!gridMatchRuntimePreparePrivateLayout(&s_EditReview.candidate, &arena, intent.review.edited_room,
			intent.review.extension, s_EditRoom, intent.review.draft_revision, &layout, err, sizeof(err)) || !layout) {
		if (layout) gridMatchRuntimeReleasePrivateLayout(layout);
		return editError(err[0] ? err : "Could not prepare the native layout for this playtest.");
	}
	if (!netGridEditCanApplyStagedLayout() || !editLayoutMatchesCandidate(&intent)) {
		gridMatchRuntimeReleasePrivateLayout(layout);
		return editError("The staged Variant changed during layout preparation.");
	}
	if (!editSendControl(editBoundPeer(), SVC_GRID_EDIT_CONTROL, &intent)) {
		gridMatchRuntimeReleasePrivateLayout(layout);
		return editError("Could not send the live layout intent to the invited creator.");
	}
	s_EditNextLayoutGeneration = intent.apply_layout_generation;
	s_EditLayout.phase = EDIT_LAYOUT_WAITING_REPLY;
	s_EditLayout.layout = layout;
	s_EditLayout.intent = intent;
	s_EditLayout.connection = s_EditConnection;
	s_EditLayout.actor = s_EditReview.actor;
	s_EditLayout.deadline = SDL_GetTicks() + GRID_EDIT_LAYOUT_TIMEOUT_MS;
	s_EditError[0] = '\0';
	editLogLayout("prepared", "host", &intent, s_EditLayout.actor);
	editReplaySmokeLayoutReady();
	return 1;
}

static void editFinishLayout(grid_edit_layout_apply_result_e outcome, const char *reason)
{
	if (s_EditLayout.layout) gridMatchRuntimeReleasePrivateLayout(s_EditLayout.layout);
	s_EditLayout.layout = NULL;
	s_EditLayout.intent.kind = GRID_EDIT_ROOM_CONTROL_APPLY_OUTCOME;
	s_EditLayout.intent.apply_result = outcome;
	s_EditLayout.phase = EDIT_LAYOUT_OUTCOME;
	s_EditLayout.deadline = SDL_GetTicks() + GRID_EDIT_LAYOUT_TIMEOUT_MS;
	if (reason) editError(reason);
	editLogLayout("outcome", "host", &s_EditLayout.intent, s_EditLayout.actor);
}

static void editClearLayoutView(void)
{
	s_EditPresentation.frame.room_count = 0;
	s_EditPresentation.frame.actor_count = 0;
	s_EditPresentation.sampled_at = 0;
	s_EditPresentation.received_at = 0;
}

static void editClearLayoutProjection(void)
{
	editClearLayoutView();
	s_EditPresentation.cursor_visible = 0;
	s_EditPresentation.cursor_sent = 0;
	s_EditPresentation.cursor_granted = 0;
	s_EditPresentation.cursor_token = 0;
	s_EditPresentation.cursor_received_at = 0;
	s_EditPresentation.cursor_sent_at = 0;
}

static void editSendLayoutOutcome(void)
{
	if (s_EditLayout.phase != EDIT_LAYOUT_OUTCOME) return;
	struct netclient *peer = editBoundPeer();
	if (!peer || s_EditLayout.connection != s_EditConnection || s_EditLayout.actor != s_EditGuestActor) {
		editClearLayout();
	} else if (editSendControl(peer, SVC_GRID_EDIT_CONTROL, &s_EditLayout.intent)) {
		if (s_EditLayout.intent.apply_result == GRID_EDIT_LAYOUT_APPLY_COMMITTED) {
			s_EditPresentation.announced_layout_generation = s_EditLayout.intent.apply_layout_generation;
		}
		editClearLayout();
	} else if ((s32)(SDL_GetTicks() - s_EditLayout.deadline) >= 0 && editSendClose(peer)) {
		/* Keep the terminal job while both reliable sends fail. Once CLOSE
		 * queues, it clears the old grant before a fresh invite can arrive. */
		if (!editKeepDraftAfterPeerDeparture()) netGridEditReset();
		editError("The layout result could not be delivered. Rejoin Shared Grid to continue.");
	}
}

void netGridEditPrivateLayoutTick(void)
{
	if (g_NetMode != NETMODE_SERVER) return;
	if (s_EditLayout.phase == EDIT_LAYOUT_WAITING_REPLY || s_EditLayout.phase == EDIT_LAYOUT_READY) {
		const grid_edit_room_control_t *intent = &s_EditLayout.intent;
		const u32 now = SDL_GetTicks();
		const s32 expired = (s32)(now - s_EditLayout.deadline) >= 0;
		if (expired && s_EditLayout.phase == EDIT_LAYOUT_WAITING_REPLY
				&& s_EditSmokeLayoutReply.connection == s_EditConnection
				&& s_EditSmokeLayoutReply.actor == s_EditGuestActor
				&& gridEditLayoutApplyMatches(intent, &s_EditSmokeLayoutReply.reply)) {
			s_EditSmokeLayoutReply.timed_out = 1;
			sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LAYOUT.RETRY: deadline epoch=%u serial=%llu next=%llu elapsed_ms=%u",
				intent->review.stage_epoch, (unsigned long long)intent->review.offer_serial,
				(unsigned long long)intent->apply_layout_generation,
				(unsigned)(now - (s_EditLayout.deadline - GRID_EDIT_LAYOUT_TIMEOUT_MS)));
		}
		if (expired || !editLayoutHostReady()
				|| !editLayoutMatchesCandidate(intent) || s_EditLayout.connection != s_EditConnection
				|| s_EditLayout.actor != s_EditGuestActor) {
			editFinishLayout(GRID_EDIT_LAYOUT_APPLY_ABORTED, "The creator or exact staged Variant is no longer ready.");
		} else if (s_EditLayout.phase == EDIT_LAYOUT_READY) {
			char err[sizeof(s_EditError)] = {0};
			if (!gridMatchRuntimeCommitPrivateLayout(s_EditLayout.layout, intent->apply_previous_layout_generation,
					intent->apply_layout_generation, err, sizeof(err))) {
				editFinishLayout(GRID_EDIT_LAYOUT_APPLY_ABORTED, err[0] ? err : "The actors could not use the staged layout safely.");
			} else {
				s_EditPlaytest.layout_receipt = *intent;
				s_EditPlaytest.layout_receipt.kind = GRID_EDIT_ROOM_CONTROL_APPLY_BASELINE;
				editClearRemotePlaytestView();
				editClearLayoutProjection();
				s_EditPresentation.cursor_policy_dirty = 1;
				memset(s_EditDecorationMoves, 0, sizeof(s_EditDecorationMoves));
				s_EditError[0] = '\0';
				editFinishLayout(GRID_EDIT_LAYOUT_APPLY_COMMITTED, NULL);
				editClearReview("layout-committed");
			}
		}
	}
	editSendLayoutOutcome();
}

s32 netGridEditAcceptStagedReviewTransfer(void)
{
	if (!editReviewTransferAvailable() || !editReviewMatchesCurrent() || editReviewExpired()) {
		return editError("This review can receive an arena only while the exact arena is absent from the local catalog.");
	}

	const grid_match_offer_t key = editReviewSourcesKey(&s_EditReview.offer);
	grid_match_transfer_t *transfer = NULL;
	if (!gridMatchTransferBegin(&transfer, &key, 0, s_EditReview.offer.review_arena_size)) {
		return editError("Could not prepare to receive this arena for review.");
	}
	grid_edit_room_control_t request = s_EditReview.offer;
	request.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_TRANSFER_REQUEST;
	if (!editSendControl(editBoundPeer(), CLC_GRID_EDIT_CONTROL, &request)) {
		gridMatchTransferReset(&transfer);
		return editError("Could not request this arena for review.");
	}
	s_EditReview.transfer = transfer;
	s_EditReview.transfer_active = 1;
	s_EditReview.catalog_generation = assetCatalogGetGeneration();
	s_EditReview.deadline = SDL_GetTicks() + GRID_EDIT_REVIEW_TRANSFER_TIMEOUT_MS;
	s_EditError[0] = '\0';
	sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW.TRANSFER: consented role=creator serial=%llu bytes=%u",
		(unsigned long long)request.review.offer_serial, request.review_arena_size);
	return 1;
}

s32 netGridEditCancelStagedReview(void)
{
	if (!editTransferReview(s_EditReview.offer.kind) || !editReviewMatchesCurrent()) return 0;
	editCancelReview("cancelled");
	s_EditError[0] = '\0';
	return 1;
}

static s32 editFindReviewedLocalArena(const grid_edit_variant_review_t *review, char root[FS_MAXPATH + 1])
{
	const asset_entry_t *arena = assetCatalogResolve(review->arena_id);
	char descriptor[FS_MAXPATH + 1], chain[FS_MAXPATH + 1];
	return arena && arena->type == ASSET_ARENA && !arena->temporary
		&& gridMatchCatalogArchiveDescriptor(arena, descriptor, sizeof(descriptor))
		&& gridMatchPackageLocation(descriptor, root, FS_MAXPATH + 1, chain, sizeof(chain), NULL)
		&& !chain[0];
}

static s32 editVerifyReviewedLocalArena(const char *root, const char *arena_id, const char *expected_sha)
{
	u32 size = 0;
	void *bytes = gridMatchPackageLoad(root, &size);
	const s32 verified = bytes && gridMatchRuntimePreflightRoomExtension(root, bytes, size,
		arena_id, expected_sha, NULL, 0);
	free(bytes);
	return verified;
}

static grid_edit_variant_review_reply_e editReviewLocalResult(const grid_edit_room_control_t *offer,
	u8 proof[SHA256_DIGEST_SIZE], grid_match_cache_t **retained)
{
	char root[FS_MAXPATH + 1];
	if (!editFindReviewedLocalArena(&offer->review, root)) return GRID_EDIT_VARIANT_REVIEW_ACKNOWLEDGED;
	char arena_sha[SHA256_HEX_SIZE];
	sha256ToHex(offer->review.arena_sha256, arena_sha);
	if (editReviewHasDependencies(offer->kind)) {
		grid_match_offer_t key = {0};
		grid_match_cache_t *sources = NULL;
		if (forgeLevelPrepareStagedArenaSources(root, offer->review.arena_id, arena_sha,
				offer->invite.identity.room_id, offer->review.offer_serial, &key, &sources, NULL, 0)) {
			u8 computed[SHA256_DIGEST_SIZE];
			const s32 prepared = editPreparedReview(offer->kind);
			if (editReadReviewDigest(key.manifest_digest, computed)
					&& memcmp(computed, offer->review_deps_sha256, sizeof(computed)) == 0
					&& (!prepared || editReviewSourcesReady(offer, sources))) {
				if (prepared) *retained = sources;
				else gridMatchCacheReset(&sources);
				memcpy(proof, computed, sizeof(computed));
				return GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED;
			}
			gridMatchCacheReset(&sources);
			return GRID_EDIT_VARIANT_REVIEW_SOURCE_VERIFIED;
		}
		if (editReviewExpired()) return GRID_EDIT_VARIANT_REVIEW_ACKNOWLEDGED;
	}
	return editVerifyReviewedLocalArena(root, offer->review.arena_id, arena_sha)
		? GRID_EDIT_VARIANT_REVIEW_SOURCE_VERIFIED : GRID_EDIT_VARIANT_REVIEW_ACKNOWLEDGED;
}

s32 netGridEditReplyToStagedReview(s32 acknowledge)
{
	if (g_NetMode != NETMODE_CLIENT || !editReviewMatchesCurrent() || s_EditReview.reply
			|| s_EditReview.transfer_active || editReviewExpired()) {
		return editError("This staged Variant review is no longer pending.");
	}
	const grid_edit_room_control_t offer = s_EditReview.offer;
	const u32 catalog_generation = assetCatalogGetGeneration();
	grid_match_cache_t *sources = NULL;
	grid_edit_room_control_t reply = offer;
	reply.kind = editReviewHasDependencies(offer.kind)
		? GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_REPLY : GRID_EDIT_ROOM_CONTROL_REVIEW_REPLY;
	reply.review_arena_size = 0;
	memset(reply.review_deps_sha256, 0, sizeof(reply.review_deps_sha256));
	reply.review_reply = acknowledge ? editReviewLocalResult(&offer, reply.review_deps_sha256, &sources)
		: GRID_EDIT_VARIANT_REVIEW_DECLINED;
	if (editReviewExpired() || !editReviewMatchesCurrent()
			|| s_EditReview.reply || s_EditReview.offer.kind != offer.kind
			|| !gridEditVariantReviewMatches(&s_EditReview.offer.review, &offer.review)
			|| (reply.review_reply == GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED
				&& catalog_generation != assetCatalogGetGeneration())) {
		gridMatchCacheReset(&sources);
		return editError("This staged Variant review expired before verification finished.");
	}
	if (!editSendControl(editBoundPeer(), CLC_GRID_EDIT_CONTROL, &reply)) {
		gridMatchCacheReset(&sources);
		return editError("Could not reply to the staged Variant review.");
	}
	s_EditReview.reply = reply.review_reply;
	s_EditReview.catalog_generation = catalog_generation;
	s_EditReview.sources = sources;
	s_EditError[0] = '\0';
	if (reply.review_reply == GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED) {
		char digest[SHA256_HEX_SIZE];
		sha256ToHex(reply.review_deps_sha256, digest);
		sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW.DEPS: local verified serial=%llu sha256=%s",
			(unsigned long long)reply.review.offer_serial, digest);
	}
	if (sources) editLogReviewSourcesRetained("creator");
	return 1;
}

s32 netGridEditHasSavedVariant(const char *arena_id)
{
	if (!arena_id) return 0;
	if (s_EditSavedVariant.path[0] && strcmp(arena_id, s_EditSavedVariant.arena_id) == 0) return 1;
	grid_edit_staged_variant_info_t staged;
	return netGridEditGetStagedVariant(&staged) && strcmp(arena_id, staged.arena_id) == 0;
}

const char *netGridEditSavedVariantId(void)
{
	return s_EditSavedVariant.arena_id;
}

static s32 editStagedVariantMatchesPin(const char *path, const char *expected_sha)
{
	u32 size = 0;
	void *bytes = gridMatchPackageLoad(path, &size);
	if (!bytes) return 0;
	u8 digest[SHA256_DIGEST_SIZE];
	char sha[SHA256_HEX_SIZE];
	sha256Hash(bytes, size, digest);
	sha256ToHex(digest, sha);
	free(bytes);
	return strcmp(sha, expected_sha) == 0;
}

static s32 editPreflightVariantFile(const char *path, const char *arena_id, const char *expected_sha,
	char measured_sha[SHA256_HEX_SIZE])
{
	u32 size = 0;
	void *bytes = gridMatchPackageLoad(path, &size);
	if (!bytes) return editError("The staged arena archive could not be read.");
	if (!expected_sha) {
		u8 digest[SHA256_DIGEST_SIZE];
		sha256Hash(bytes, size, digest);
		sha256ToHex(digest, measured_sha);
		expected_sha = measured_sha;
	}
	const s32 verified = gridMatchRuntimePreflightRoomExtension(path, bytes, size, arena_id, expected_sha,
		s_EditError, sizeof(s_EditError));
	free(bytes);
	return verified;
}

static s32 editEnableStagedVariant(const char *arena_id)
{
	if (!gridMatchRuntimeIsIdle() || mainGridRoomReturnPending()) {
		return editError("Wait for the previous playtest to finish returning to the Room.");
	}
	const char *sha = s_EditStagedVariant.pin.digest;
	if (!editPreflightVariantFile(s_EditStagedVariant.archive_path, arena_id, sha, NULL)) return 0;
	if (!modAuthorRuntimeEnable(s_EditStagedVariant.mod_id, NULL, s_EditError, sizeof(s_EditError))) return 0;
	const asset_entry_t *arena = assetCatalogResolve(arena_id);
	char descriptor[FS_MAXPATH + 1], root[FS_MAXPATH + 1], chain[FS_MAXPATH + 1];
	const char *member = NULL;
	if (!arena || arena->type != ASSET_ARENA
			|| !gridMatchCatalogArchiveDescriptor(arena, descriptor, sizeof(descriptor))
			|| !gridMatchPackageLocation(descriptor, root, sizeof(root), chain, sizeof(chain), &member)
			|| chain[0] || !member || strcmp(member, "arena.ini") != 0 || !editStagedVariantMatchesPin(root, sha)) {
		return editError("The enabled arena does not match the staged revision.");
	}
	return 1;
}

static s32 editStartPrivatePlaytest(const char *variant_id, s32 bots, u8 difficulty)
{
	grid_edit_room_state_t draft;
	if (!netGridEditHostOwnsRoom() || !editBoundPeer()
			|| s_EditPlaytest.phase != EDIT_PLAYTEST_NONE
			|| !gridEditRoomGetState(s_EditRoom, &draft) || !draft.is_host || !draft.ready) {
		return editError("Open a Shared Draft with an invited creator before starting a private playtest.");
	}
	if (bots < 1 || bots > matchConfigMaxBotsForHumans(1)
			|| strcmp(g_MatchConfig.scenario_id, "base:combat") != 0) {
		return editError("Choose Combat and add at least one bot before starting the private playtest.");
	}
	const forge_revision_t *variant = NULL;
	if (variant_id) {
		if (editStagedVariantMatchesDraft(&draft) && strcmp(variant_id, s_EditStagedVariant.pin.arena_id) == 0) {
			if (!editEnableStagedVariant(variant_id)) return 0;
			variant = &s_EditStagedVariant.pin;
		} else if (s_EditSavedVariant.path[0] && strcmp(variant_id, s_EditSavedVariant.arena_id) == 0) {
			variant = &s_EditSavedVariant;
		} else {
			return editError("Save the shared draft as a Variant before playing it.");
		}
	}
	const char *arena_id = variant ? variant->arena_id : draft.invite.arena_id;
	const char *arena_sha = variant ? variant->digest : draft.invite.arena_sha256;
	s_EditPlaytest.phase = EDIT_PLAYTEST_REQUESTED;
	s_EditPlaytest.draft = draft.invite.identity;
	snprintf(s_EditPlaytest.arena_id, sizeof(s_EditPlaytest.arena_id), "%s", arena_id);
	snprintf(s_EditPlaytest.arena_sha256, sizeof(s_EditPlaytest.arena_sha256), "%s", arena_sha);

	const s32 result = netLobbyRequestStartWithSims(NETGAMEMODE_MP, arena_id, 0, NET_NULL_CLIENT,
		(u8)bots, difficulty, g_MatchConfig.timelimit, matchConfigGetUserOptions(), MPSCENARIO_COMBAT,
		g_MatchConfig.scorelimit, g_MatchConfig.teamscorelimit,
		(u8)(g_MatchConfig.weaponSetIndex >= 0 ? g_MatchConfig.weaponSetIndex : 0xff));
	if (result != 0 || s_EditPlaytest.phase != EDIT_PLAYTEST_LOADING) {
		netGridEditPrivatePlaytestFailed(draft.invite.identity.room_id);
		return editError("The exact pinned arena could not start a private bot playtest.");
	}
	s_EditError[0] = '\0';
	return 1;
}

s32 netGridEditStartPrivatePlaytest(s32 bots, u8 difficulty)
{
	return editStartPrivatePlaytest(NULL, bots, difficulty);
}

s32 netGridEditStartPrivateVariantPlaytest(const char *arena_id, s32 bots, u8 difficulty)
{
	if (!arena_id || !arena_id[0]) return editError("Choose the saved Variant to playtest.");
	return editStartPrivatePlaytest(arena_id, bots, difficulty);
}

static struct defaultobj *editPlaytestDecoration(const char *record_id, s32 *protected_pad)
{
	const grid_match_runtime_identity_t *arena = gridMatchRuntimeActive();
	u32 index;
	if (!arena || arena->stage != g_Vars.stagenum || arena->offer.room_id != s_EditPlaytest.draft.room_id
			|| !scenarioSourceGridDecorationCommand(arena->scenario_id, g_StageSetup.props,
				record_id, &index)) return NULL;
	struct defaultobj *obj = setupGetObjByCmdIndex(index);
	if (obj && !gridMatchRuntimeDecorationPadIndependent(obj->pad)) {
		if (protected_pad) *protected_pad = 1;
		return NULL;
	}
	return obj;
}

static grid_edit_result_t editPlanDecorationMove(const grid_edit_command_t *command,
		s32 require_live, grid_playtest_decoration_move_t *move, s32 *out_index)
{
	*out_index = -1;
	if (command->operation != GRID_EDIT_MOVE_DECORATION || s_EditPlaytest.phase != EDIT_PLAYTEST_ACTIVE
			|| g_NetStageEpoch != s_EditPlaytest.stage_epoch) return GRID_EDIT_APPLIED;
	grid_edit_room_state_t draft;
	if (!gridEditRoomGetState(s_EditRoom, &draft) || command->expected_revision != draft.revision) {
		/* The authority supplies the regular stale decision and detailed receipt. */
		return GRID_EDIT_APPLIED;
	}

	const grid_edit_move_decoration_t *pose = &command->data.move_decoration;
	if (!gridEditMoveDecorationValid(pose)) return GRID_EDIT_APPLIED;
	s32 protected_pad = 0;
	struct defaultobj *obj = editPlaytestDecoration(pose->record_id, &protected_pad);
	if (protected_pad) {
		editError("A live decoration needs an independent pad outside the round's starts and routes.");
		return GRID_EDIT_INVALID;
	}
	if (!obj) {
		if (!require_live) return GRID_EDIT_APPLIED;
		editError("That decoration is unavailable in the running playtest.");
		return GRID_EDIT_INVALID;
	}
	const u64 lifecycle = obj->prop ? propGetLifecycleGeneration(obj->prop) : 0;
	if (!objGridDecorationMove(obj, lifecycle, pose->x, pose->z, 0)) {
		editError("A live decoration must stay on the same static floor in its current room.");
		return GRID_EDIT_INVALID;
	}
	for (s32 i = 0; i < GRID_PLAYTEST_MAX_PENDING_DECORATIONS; i++) {
		const grid_playtest_decoration_move_t *queued = &s_EditDecorationMoves[i];
		if (queued->epoch == g_NetStageEpoch && strcmp(pose->record_id, queued->pose.record_id) == 0) {
			*out_index = i;
			break;
		}
		if (*out_index < 0 && !queued->epoch) *out_index = i;
	}
	if (*out_index < 0) {
		editError("The private playtest has too many decoration edits waiting for the current tick.");
		return GRID_EDIT_RESOURCE_EXHAUSTED;
	}
	*move = (grid_playtest_decoration_move_t){
		.pose = *pose, .epoch = g_NetStageEpoch, .prop_generation = lifecycle,
	};
	return GRID_EDIT_APPLIED;
}

static void editApplyPlaytestDecorations(void)
{
	for (s32 i = 0; i < GRID_PLAYTEST_MAX_PENDING_DECORATIONS; i++) {
		grid_playtest_decoration_move_t *move = &s_EditDecorationMoves[i];
		if (!move->epoch) continue;
		if (move->epoch == g_NetStageEpoch) {
			struct defaultobj *obj = editPlaytestDecoration(move->pose.record_id, NULL);
			const s32 applied = objGridDecorationMove(obj, move->prop_generation, move->pose.x, move->pose.z, 1);
			sysLogPrintf(applied ? LOG_NOTE : LOG_WARNING,
				"GRID.PLAYTEST.DECORATION: epoch=%u draft_revision=%llu record=%s x=%.1f z=%.1f applied=%d",
				move->epoch, (unsigned long long)move->draft_revision, move->pose.record_id,
				move->pose.x, move->pose.z, applied);
		}
		memset(move, 0, sizeof(*move));
	}
}

static u16 editViewCoordinate(f32 value, f32 minimum, f32 maximum)
{
	if (value <= minimum) return 0;
	if (value >= maximum) return UINT16_MAX;
	return (u16)((value - minimum) / (maximum - minimum) * UINT16_MAX);
}

static s32 editViewRoomBounds(const struct room *room)
{
	return !(room->flags & ROOMFLAG_RENDERALWAYS)
		&& __builtin_isfinite(room->bbmin[0]) && __builtin_isfinite(room->bbmin[2])
		&& __builtin_isfinite(room->bbmax[0]) && __builtin_isfinite(room->bbmax[2])
		&& room->bbmax[0] > room->bbmin[0] && room->bbmax[2] > room->bbmin[2];
}

static s32 editSamplePlaytestView(grid_playtest_view_t *frame)
{
	const struct room *rooms[GRID_PLAYTEST_VIEW_MAX_ROOMS];
	f32 min_x = 0, min_z = 0, max_x = 0, max_z = 0;
	for (s32 i = 1; g_Rooms && i < g_Vars.roomcount; i++) {
		const struct room *room = &g_Rooms[i];
		if (!editViewRoomBounds(room)) continue;
		if (!frame->room_count || room->bbmin[0] < min_x) min_x = room->bbmin[0];
		if (!frame->room_count || room->bbmin[2] < min_z) min_z = room->bbmin[2];
		if (!frame->room_count || room->bbmax[0] > max_x) max_x = room->bbmax[0];
		if (!frame->room_count || room->bbmax[2] > max_z) max_z = room->bbmax[2];
		if (frame->room_count < GRID_PLAYTEST_VIEW_MAX_ROOMS) rooms[frame->room_count++] = room;
	}
	if (!frame->room_count || !__builtin_isfinite(max_x - min_x) || !__builtin_isfinite(max_z - min_z)) return 0;
	for (u8 i = 0; i < frame->room_count; i++) {
		const struct room *room = rooms[i];
		frame->rooms[i] = (grid_playtest_view_room_t){
			.min_x = editViewCoordinate(room->bbmin[0], min_x, max_x),
			.min_z = editViewCoordinate(room->bbmin[2], min_z, max_z),
			.max_x = editViewCoordinate(room->bbmax[0], min_x, max_x),
			.max_z = editViewCoordinate(room->bbmax[2], min_z, max_z),
		};
	}
	for (s32 i = 0; i < MAX_MPCHRS && frame->actor_count < GRID_PLAYTEST_VIEW_MAX_ACTORS; i++) {
		const struct chrdata *chr = g_MpAllChrPtrs[i];
		const struct prop *prop = chr ? chr->prop : NULL;
		if (!prop || !__builtin_isfinite(prop->pos.x) || !__builtin_isfinite(prop->pos.z)) continue;
		frame->actors[frame->actor_count++] = (grid_playtest_view_actor_t){
			.x = editViewCoordinate(prop->pos.x, min_x, max_x),
			.z = editViewCoordinate(prop->pos.z, min_z, max_z),
			.bot = chr->aibot != NULL,
		};
	}
	frame->has_world_bounds = 1;
	frame->layout_generation = gridMatchRuntimePrivateLayoutGeneration();
	frame->min_x = min_x;
	frame->min_z = min_z;
	frame->max_x = max_x;
	frame->max_z = max_z;
	return frame->actor_count != 0;
}

s32 netGridEditGetCreatorCursor(grid_playtest_view_t *out, u16 *cursor_x, u16 *cursor_z)
{
	s32 players, creators;
	if (!out || !cursor_x || !cursor_z || !g_NetStageEpoch
			|| s_EditPlaytest.phase != EDIT_PLAYTEST_ACTIVE
			|| s_EditPresentation.frame.epoch != g_NetStageEpoch
			|| !s_EditPresentation.frame.actor_count
			|| !editBoundPeer() || !netGridEditGetPlaytestVisibility(&players, &creators) || !players
			|| !s_EditPresentation.cursor_visible
			|| (u32)(SDL_GetTicks() - s_EditPresentation.cursor_received_at) >= GRID_VIEW_EXPIRY_MS) return 0;
	const grid_playtest_view_t *frame = &s_EditPresentation.frame;
	if (s_EditPresentation.cursor_x < frame->min_x || s_EditPresentation.cursor_x > frame->max_x
			|| s_EditPresentation.cursor_z < frame->min_z || s_EditPresentation.cursor_z > frame->max_z) return 0;
	*cursor_x = editViewCoordinate(s_EditPresentation.cursor_x, frame->min_x, frame->max_x);
	*cursor_z = editViewCoordinate(s_EditPresentation.cursor_z, frame->min_z, frame->max_z);
	*out = *frame;
	return 1;
}

static void editPlaytestPresentationTick(void)
{
	s32 players, creators;
	if (s_EditLayout.phase == EDIT_LAYOUT_OUTCOME
			|| !netGridEditGetPlaytestVisibility(&players, &creators)) return;
	struct netclient *peer = editBoundPeer();
	if (!peer) return;
	if (!creators) editClearRemotePlaytestView();
	const s32 creators_ready = creators && editEnsureLayoutBaseline(peer);
	if (s_EditPresentation.cursor_policy_dirty && editSendCursorPolicy(peer, players != 0)) {
		s_EditPresentation.cursor_policy_dirty = 0;
	}
	if (!players && !creators) return;
	const u32 now = SDL_GetTicks();
	if ((u32)(now - s_EditPresentation.sampled_at) < GRID_VIEW_INTERVAL_MS) return;
	s_EditPresentation.sampled_at = now;
	grid_playtest_view_t frame = {
		.epoch = g_NetStageEpoch,
		.serial = s_EditPresentation.frame.serial + 1,
	};
	if (!frame.serial || !editSamplePlaytestView(&frame)) return;
	s_EditPresentation.frame = frame;
	if (creators_ready && enet_list_size(&peer->peer->outgoingCommands) < GRID_EDIT_SNAPSHOT_MAX_QUEUED_COMMANDS
			&& editSendPlaytestView(peer, &frame)) {
		s_EditPresentation.frame_sent = 1;
	}
}

void netGridEditPrivatePlaytestTick(void)
{
	if (!netGridEditPrivatePlaytestRoom(g_NetMatchRoomId) || !g_NetLocalClient
			|| g_NetLocalClient->state != CLSTATE_GAME || !g_NetLocalClient->stage_ready
			|| !g_NetStageEpoch) return;
	if (s_EditPlaytest.phase == EDIT_PLAYTEST_LOADING) {
		if (!editPlaytestArenaMatches(gridMatchRuntimeActive())) return;
		s_EditPlaytest.stage_epoch = g_NetStageEpoch;
		s_EditPlaytest.phase = EDIT_PLAYTEST_ACTIVE;
		s_EditPlaytest.participant_mask = mpParticipantsEncodeActiveMask();
		s_EditPlaytest.gameplay_fingerprint_ready = netmsgGridPrivateGameplayFingerprint(
			s_EditPlaytest.draft.room_id, s_EditPlaytest.gameplay_fingerprint);
		if (netGridEditPrivateResumeRoomOpen(g_NetMatchRoomId)) netRoomListMarkDirty();
		sysLogPrintf(LOG_NOTE, "GRID.PLAYTEST: active room=%u epoch=%u arena=%s",
			s_EditPlaytest.draft.room_id, s_EditPlaytest.stage_epoch, s_EditPlaytest.arena_id);
	}
	if (s_EditPlaytest.stage_epoch != g_NetStageEpoch) return;
	editApplyPlaytestDecorations();
	editPlaytestPresentationTick();
	grid_edit_room_state_t draft;
	if (!gridEditRoomGetState(s_EditRoom, &draft) || !draft.is_host
			|| draft.invite.identity.room_id != s_EditPlaytest.draft.room_id
			|| draft.revision == s_EditPlaytest.displayed_revision) return;
	if (draft.invite.identity.generation != s_EditPlaytest.draft.generation
			&& !(draft.pending_resume && draft.can_edit && editSameIdentity(&draft.invite.identity)
				&& s_EditResume.live_epoch == g_NetStageEpoch
				&& s_EditResume.live_generation == s_EditPlaytest.draft.generation)) return;
	s_EditPlaytest.displayed_revision = draft.revision;
	s_EditPlaytest.ambient_enabled = draft.enabled;
	s_EditPlaytest.brightness = draft.brightness;
	sysLogPrintf(LOG_NOTE, "GRID.PLAYTEST: applied visual ambient epoch=%u revision=%llu enabled=%d brightness=%d",
		s_EditPlaytest.stage_epoch, (unsigned long long)draft.revision, draft.enabled, draft.brightness);
}

s32 netGridEditPrivatePlaytestAmbientState(s32 *enabled, s32 *brightness)
{
	if (!enabled || !brightness || !netGridEditPrivatePlaytestRoom(g_NetMatchRoomId)
			|| s_EditPlaytest.phase != EDIT_PLAYTEST_ACTIVE || !s_EditPlaytest.displayed_revision
			|| s_EditPlaytest.stage_epoch != g_NetStageEpoch) return 0;
	*enabled = s_EditPlaytest.ambient_enabled;
	*brightness = s_EditPlaytest.brightness;
	return 1;
}

s32 netGridEditPrivatePlaytestAmbient(s32 *out)
{
	s32 enabled, brightness;
	if (!out || !netGridEditPrivatePlaytestAmbientState(&enabled, &brightness) || !enabled) return 0;
	*out = brightness;
	return 1;
}

void netGridEditPrivatePlaytestStageStopped(void)
{
	if (s_EditPlaytest.phase == EDIT_PLAYTEST_ACTIVE) {
		editClearSmokeLayoutReply();
		if (s_EditLayout.phase == EDIT_LAYOUT_WAITING_REPLY || s_EditLayout.phase == EDIT_LAYOUT_READY) {
			editFinishLayout(GRID_EDIT_LAYOUT_APPLY_ABORTED, "The private playtest ended before the layout could apply.");
		}
		editSendLayoutOutcome();
		editCancelReview("stage-stopped");
		editClearResumeRoomInvite();
		editClearRemotePlaytestView();
		s_EditPresentation.cursor_visible = 0;
		memset(s_EditDecorationMoves, 0, sizeof(s_EditDecorationMoves));
		sysLogPrintf(LOG_NOTE, "GRID.PLAYTEST: retired local stage room=%u epoch=%u",
			s_EditPlaytest.draft.room_id, s_EditPlaytest.stage_epoch);
		const u64 token = editHostCursorToken();
		memset(&s_EditPlaytest, 0, sizeof(s_EditPlaytest));
		if (token) {
			s32 players, creators;
			struct netclient *peer = editBoundPeer();
			if (peer && gridEditRoomGetVisibility(s_EditRoom, &players, &creators)) {
				s_EditPresentation.cursor_policy_dirty = s_EditLayout.phase == EDIT_LAYOUT_OUTCOME
					|| !editSendCursorPolicy(peer, players != 0);
			}
		}
		netRoomListMarkDirty();
	}
}

void netGridEditClientReset(const struct netclient *client)
{
	if (s_EditResumeRoomInvite.connection && (client == g_NetLocalClient
			|| client == &g_NetClients[s_EditResumeRoomInvite.client_id])) {
		editClearResumeRoomInvite();
	}
	if (!s_EditConnection && !s_EditRoom) return;
	if (g_NetMode == NETMODE_SERVER && client == g_NetLocalClient && s_EditTarget < NET_MAX_CLIENTS) {
		/* Membership may have moved before the successful room transaction
		 * updates this client's room ID. Only the recorded guest can receive
		 * the identity-only close through the old connection. */
		editClosePeer(&g_NetClients[s_EditTarget]);
	} else if (g_NetMode == NETMODE_SERVER && s_EditConnection && s_EditTarget < NET_MAX_CLIENTS
			&& client == &g_NetClients[s_EditTarget] && editKeepDraftAfterPeerDeparture()) {
		return;
	} else if (client == g_NetLocalClient || (g_NetMode == NETMODE_SERVER
			&& s_EditTarget < NET_MAX_CLIENTS && client == &g_NetClients[s_EditTarget])) {
		netGridEditReset();
		editError("Shared Grid ended when a creator disconnected or left the room.");
	}
}

void netGridEditPoll(void)
{
	if (g_NetMode == NETMODE_SERVER && s_EditRoom) {
		gridEditRoomHostAdvanceClock(s_EditRoom, SDL_GetTicks64());
		/* Terminal controls must also settle after returning to the Room. */
		editSendLayoutOutcome();
	}
	if (s_EditResumeRoomInvite.connection
			&& !netGridEditPrivateResumeRoomOpen(s_EditResumeRoomInvite.room_id)) {
		editClearResumeRoomInvite();
	}
	if (s_EditPlaytest.phase == EDIT_PLAYTEST_ACTIVE && !g_NetStageEpoch) {
		editClearRemotePlaytestView();
		s_EditPresentation.cursor_visible = 0;
	}
	if (!s_EditConnection) {
		if (s_EditRoom && !editHostRoomStillOwned()) netGridEditReset();
		return;
	}
	if (!editBoundPeer()) {
		if (editKeepDraftAfterPeerDeparture()) return;
		netGridEditReset();
		editError("Shared Grid ended when the room or connection changed.");
	} else if (s_EditDeadline && (s32)(SDL_GetTicks() - s_EditDeadline) >= 0) {
		editEndCreator(editBoundPeer(), "Shared Grid invitation expired.");
	} else {
		if (s_EditReview.offer.kind) {
			if ((g_NetMode == NETMODE_CLIENT || !s_EditReview.reply || editPreparedReview(s_EditReview.offer.kind))
					&& editReviewExpired()) {
				editCancelReview("expired");
			} else if (!editReviewMatchesCurrent()) {
				editCancelReview("scope-changed");
			} else {
				editCheckPreparedCandidate();
			}
		}
		grid_edit_room_state_t draft;
		if (g_NetMode == NETMODE_CLIENT && editReviewDraft(&draft)) {
			grid_edit_room_control_t support = { .kind = GRID_EDIT_ROOM_CONTROL_REVIEW_SUPPORTED };
			support.invite.identity = draft.invite.identity;
			if (!s_EditReviewSupported) {
				s_EditReviewSupported = editSendControl(editBoundPeer(), CLC_GRID_EDIT_CONTROL, &support);
			}
			if (s_EditReviewSupported && !(s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_DEPS_V1)) {
				support.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_CAPABILITIES;
				support.review_capabilities = GRID_EDIT_REVIEW_CAP_DEPS_V1;
				if (editSendControl(editBoundPeer(), CLC_GRID_EDIT_CONTROL, &support)) {
					s_EditReviewCapabilities |= support.review_capabilities;
				}
			}
			if ((s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_DEPS_V1)
					&& !(s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_PREPARED)) {
				support.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_CAPABILITIES;
				support.review_capabilities = GRID_EDIT_REVIEW_CAP_PREPARED;
				if (editSendControl(editBoundPeer(), CLC_GRID_EDIT_CONTROL, &support)) {
					s_EditReviewCapabilities |= support.review_capabilities;
				}
			}
			if ((s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_PREPARED)
					&& !(s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_TRANSFER)) {
				support.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_CAPABILITIES;
				support.review_capabilities = GRID_EDIT_REVIEW_CAP_TRANSFER;
				if (editSendControl(editBoundPeer(), CLC_GRID_EDIT_CONTROL, &support)) {
					s_EditReviewCapabilities |= support.review_capabilities;
				}
			}
			if ((s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_PREPARED)
					&& !(s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_APPLY)) {
				support.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_CAPABILITIES;
				support.review_capabilities = GRID_EDIT_REVIEW_CAP_APPLY;
				if (editSendControl(editBoundPeer(), CLC_GRID_EDIT_CONTROL, &support)) {
					s_EditReviewCapabilities |= support.review_capabilities;
				}
			}
			if ((s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_APPLY)
					&& !(s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_APPLY_BASELINE)) {
				support.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_CAPABILITIES;
				support.review_capabilities = GRID_EDIT_REVIEW_CAP_APPLY_BASELINE;
				if (editSendControl(editBoundPeer(), CLC_GRID_EDIT_CONTROL, &support)) {
					s_EditReviewCapabilities |= support.review_capabilities;
				}
			}
			if ((s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_APPLY_BASELINE)
					&& !(s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_INTERACTIVE_VIEW)) {
				support.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_CAPABILITIES;
				support.review_capabilities = GRID_EDIT_REVIEW_CAP_INTERACTIVE_VIEW;
				if (editSendControl(editBoundPeer(), CLC_GRID_EDIT_CONTROL, &support)) {
					s_EditReviewCapabilities |= support.review_capabilities;
				}
			}
		}
	}
}

s32 netGridEditCanInvite(u32 client_id)
{
	if (s_EditConnection || !editHostTarget(client_id)) return 0;
	if (!netGridEditHostOwnsRoom()) return netGridEditPrivateResumeRoomOpen(g_NetLocalClient->room_id);
	if (!s_EditRoom) return 1;
	grid_edit_room_state_t state;
	return s_EditSourceOrigin == GRID_ARCHIVE_ORIGINAL
		&& gridEditRoomGetState(s_EditRoom, &state) && state.creator_detached;
}

static grid_archive_origin_e editPinOrigin(const forge_revision_t *pin)
{
	u32 size = 0;
	void *bytes = gridMatchPackageLoad(pin->path, &size);
	if (!bytes) return GRID_ARCHIVE_INVALID;
	u8 digest[SHA256_DIGEST_SIZE];
	char sha[SHA256_HEX_SIZE];
	sha256Hash(bytes, size, digest);
	sha256ToHex(digest, sha);
	const grid_archive_origin_e origin = strcmp(sha, pin->digest) == 0
		? gridArchiveOrigin(pin->path, bytes, size) : GRID_ARCHIVE_INVALID;
	free(bytes);
	return origin;
}

s32 netGridEditInvite(u32 client_id, const char *arena_id, const char *exact_sha_or_null)
{
	struct netclient *target = netGridEditCanInvite(client_id) ? editHostTarget(client_id) : NULL;
	if (!target || !arena_id || !arena_id[0]) return editError("Choose an arena and a connected Grid editor.");
	char directory[FS_MAXPATH + 1];
	forge_revision_t pin;
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	const s32 found = exact_sha_or_null
		? forgeRevisionRead(directory, arena_id, exact_sha_or_null, &pin, s_EditError, sizeof(s_EditError))
		: forgeRevisionReadCurrent(directory, arena_id, &pin, s_EditError, sizeof(s_EditError));
	if (!found) return editError("Pin this arena revision in Level File before inviting.");
	if (s_EditGeneration == UINT64_MAX || s_EditActor == UINT64_MAX) {
		return editError("Shared Grid session IDs are exhausted.");
	}
	const grid_edit_wire_identity_t identity = {
		.room_id = g_NetLocalClient->room_id, .generation = ++s_EditGeneration,
	};
	const u64 actor = ++s_EditActor;
	const u64 connection = editConnection(target);
	if (s_EditRoom) {
		const s32 live_resume = netGridEditPrivateResumeRoomOpen(identity.room_id);
		if (strcmp(pin.arena_id, s_EditInvite.arena_id) != 0
				|| strcmp(pin.digest, s_EditInvite.arena_sha256) != 0
				|| editPinOrigin(&pin) != GRID_ARCHIVE_ORIGINAL) {
			return editError("Resuming a draft requires the exact pinned original arena.");
		}
		s_EditResume.bytes = gridEditRoomHostPrepareResume(s_EditRoom, &identity, actor, connection,
			&s_EditInvite, &s_EditResume.offer, s_EditError, sizeof(s_EditError));
		if (!s_EditResume.bytes) return 0;
		if (live_resume) {
			s_EditResume.live_epoch = s_EditPlaytest.stage_epoch;
			s_EditResume.live_generation = s_EditPlaytest.draft.generation;
		}
		s_EditResume.phase = EDIT_RESUME_INVITED;
	} else {
		s_EditRoom = gridEditRoomHostOpen(&identity, 1, pin.arena_id, pin.path, pin.digest,
			s_EditError, sizeof(s_EditError));
		if (!s_EditRoom) return 0;
		s_EditSourceOrigin = editPinOrigin(&pin);
		if (!gridEditRoomHostInvite(s_EditRoom, actor, connection, &s_EditInvite)) {
			netGridEditReset();
			return editError("Could not prepare the Shared Grid invitation.");
		}
	}
	s_EditTarget = client_id;
	s_EditConnection = connection;
	s_EditGuestActor = actor;
	const grid_edit_room_control_t control = { .kind = GRID_EDIT_ROOM_CONTROL_INVITE, .invite = s_EditInvite };
	if (!editSendControl(target, SVC_GRID_EDIT_CONTROL, &control)) {
		if (!editKeepDraftAfterPeerDeparture()) netGridEditReset();
		return editError("Could not send the Shared Grid invitation.");
	}
	s_EditDeadline = SDL_GetTicks() + GRID_EDIT_INVITE_TIMEOUT_MS;
	s_EditError[0] = '\0';
	sysLogPrintf(LOG_NOTE, "NET.GRID.EDIT: invited client=%u room=%u generation=%llu", client_id,
		identity.room_id, (unsigned long long)identity.generation);
	return 1;
}

s32 netGridEditPendingInvite(grid_edit_room_invite_t *out)
{
	if (!s_EditOfferPending || !out) return 0;
	*out = s_EditInvite;
	return 1;
}

s32 netGridEditGetState(grid_edit_room_state_t *out)
{
	return gridEditRoomGetState(s_EditRoom, out);
}

static grid_edit_room_t *editOpenGuest(void)
{
	const grid_match_result_t *played = netGridLastMatchResult();
	if (played && played->offer.room_id == s_EditInvite.identity.room_id
			&& strcmp(played->arena_id, s_EditInvite.arena_id) == 0) {
		char digest[SHA256_HEX_SIZE];
		sha256ToHex(played->arena_sha256, digest);
		u32 size = 0;
		const void *bytes = strcmp(digest, s_EditInvite.arena_sha256) == 0
			? gridMatchRuntimeReadReturnedArena(played, &size) : NULL;
		if (bytes) {
			grid_edit_room_t *room = gridEditRoomGuestOpenBytes(&s_EditInvite, s_EditConnection,
				bytes, size, s_EditError, sizeof(s_EditError));
			if (room) sysLogPrintf(LOG_NOTE, "NET.GRID.EDIT: opened verified last-played session arena");
			return room;
		}
	}

	char directory[FS_MAXPATH + 1];
	char local_archive[FS_MAXPATH + 1];
	forge_revision_t pin;
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	if (forgeRevisionRead(directory, s_EditInvite.arena_id, s_EditInvite.arena_sha256, &pin, NULL, 0)) {
		snprintf(local_archive, sizeof(local_archive), "%s", pin.path);
	} else if (!modAuthorRuntimeTemplatePath(s_EditInvite.arena_id, local_archive, sizeof(local_archive),
			NULL, 0)) {
		editError("This profile needs the invited arena revision to join Shared Grid.");
		return NULL;
	}
	return gridEditRoomGuestOpen(&s_EditInvite, s_EditConnection, local_archive, s_EditError, sizeof(s_EditError));
}

s32 netGridEditReply(s32 accept)
{
	struct netclient *host = s_EditOfferPending ? editBoundPeer() : NULL;
	if (!host) return editError("The Shared Grid invitation expired or the room changed.");
	if (!accept) {
		editCloseBound();
		return 1;
	}
	s_EditRoom = editOpenGuest();
	if (!s_EditRoom) return 0;
	const grid_edit_room_control_t control = { .kind = GRID_EDIT_ROOM_CONTROL_CONFIRM, .invite = s_EditInvite };
	if (!editSendControl(host, CLC_GRID_EDIT_CONTROL, &control)) {
		editCloseBound();
		return editError("Could not confirm the Shared Grid invitation.");
	}
	s_EditOfferPending = 0;
	s_EditDeadline = SDL_GetTicks() + GRID_EDIT_INVITE_TIMEOUT_MS;
	return 1;
}

static s32 editSendSnapshotOffer(struct netclient *peer, u8 opcode, grid_edit_snapshot_kind_e kind)
{
	u8 data[1 + GRID_EDIT_SNAPSHOT_OFFER_WIRE_SIZE];
	struct netbuf packet = { .data = data, .size = sizeof(data) };
	netbufWriteU8(&packet, opcode);
	return gridEditSnapshotOfferWrite(&packet, kind, &s_EditResume.offer) && editSend(peer, &packet);
}

static void editPumpReviewTransfer(void)
{
	if (g_NetMode != NETMODE_SERVER || !s_EditReview.transfer_active
			|| !editReviewMatchesCurrent() || editReviewExpired()) return;
	const grid_edit_room_control_t *offer = &s_EditReview.offer;
	if (s_EditReview.transfer_offset >= offer->review_arena_size) return;
	struct netclient *peer = editBoundPeer();
	if (!peer || enet_peer_get_state(peer->peer) != ENET_PEER_STATE_CONNECTED) return;

	const grid_match_offer_t key = editReviewSourcesKey(offer);
	u32 size = 0;
	const u8 *arena = gridMatchCacheRead(s_EditReview.sources, &key, s_EditReview.transfer_package, &size);
	if (!arena || size != offer->review_arena_size) {
		editCancelReview("source-missing");
		return;
	}
	for (u32 sent = 0; sent < GRID_EDIT_REVIEW_TRANSFER_CHUNKS_PER_FRAME
			&& s_EditReview.transfer_offset < size; ++sent) {
		/* ENet splits a 16 KiB packet into reliable commands and separately
		 * bounds its own in-flight window. Leave the control channel usable. */
		if (enet_list_size(&peer->peer->outgoingCommands) >= GRID_EDIT_REVIEW_TRANSFER_MAX_QUEUED_COMMANDS) break;
		const u32 offset = s_EditReview.transfer_offset;
		const u32 remaining = size - offset;
		const grid_match_chunk_t chunk = {
			.offer = key,
			.package_index = 0,
			.offset = offset,
			.size = (u16)(remaining < NET_DISTRIB_CHUNK_SIZE ? remaining : NET_DISTRIB_CHUNK_SIZE),
			.data = arena + offset,
		};
		u8 data[1 + GRID_EDIT_REVIEW_CHUNK_WIRE_MAX];
		struct netbuf wire = { .data = data, .size = sizeof(data) };
		netbufWriteU8(&wire, SVC_GRID_EDIT_REVIEW_CHUNK);
		const s32 encoded = gridEditReviewChunkWrite(&wire, offer->invite.identity.generation, &chunk);
		const u32 packet_size = wire.wp;
		if (!encoded || netSend(peer, &wire, 1, NETCHAN_TRANSFER) != packet_size) {
			editCancelReview("send-failed");
			editError("Could not send the arena for review.");
			return;
		}
		s_EditReview.transfer_offset += chunk.size;
	}
}

void netGridEditPumpSnapshot(void)
{
	editPumpReviewTransfer();
	if (s_EditResume.phase != EDIT_RESUME_SENDING) return;
	struct netclient *peer = editBoundPeer();
	if (!peer || !s_EditResume.bytes || s_EditResume.offset >= s_EditResume.offer.size) return;
	for (size_t i = 0; i < GRID_EDIT_SNAPSHOT_CHUNKS_PER_FRAME && s_EditResume.offset < s_EditResume.offer.size; i++) {
		if (enet_list_size(&peer->peer->outgoingCommands) >= GRID_EDIT_SNAPSHOT_MAX_QUEUED_COMMANDS) break;
		const u32 remaining = s_EditResume.offer.size - s_EditResume.offset;
		const grid_edit_snapshot_chunk_t chunk = {
			.identity = s_EditResume.offer.identity,
			.offset = s_EditResume.offset,
			.size = (u16)(remaining < GRID_EDIT_SNAPSHOT_CHUNK_MAX_BYTES ? remaining : GRID_EDIT_SNAPSHOT_CHUNK_MAX_BYTES),
			.bytes = s_EditResume.bytes + s_EditResume.offset,
		};
		u8 data[1 + GRID_EDIT_SNAPSHOT_CHUNK_WIRE_MAX];
		struct netbuf packet = { .data = data, .size = sizeof(data) };
		netbufWriteU8(&packet, SVC_GRID_EDIT_SNAPSHOT);
		if (!gridEditSnapshotChunkWrite(&packet, &chunk) || !editSend(peer, &packet)) {
			editEndCreator(peer, "Could not send the Shared Grid draft snapshot.");
			return;
		}
		s_EditResume.offset += chunk.size;
	}
	if (s_EditResume.offset == s_EditResume.offer.size) {
		free(s_EditResume.bytes);
		s_EditResume.bytes = NULL;
		s_EditResume.phase = EDIT_RESUME_WAITING_ACK;
	}
}

static s32 editSendDelta(struct netclient *peer, const grid_edit_delta_t *delta)
{
	u8 data[1 + GRID_EDIT_DELTA_WIRE_MAX];
	struct netbuf wire = { .data = data, .size = sizeof(data) };
	netbufWriteU8(&wire, SVC_GRID_EDIT_DELTA);
	return gridEditDeltaWrite(&wire, &s_EditInvite.identity, delta)
		&& editSend(peer, &wire);
}

static grid_edit_result_t editValidatePlanTarget(const grid_edit_command_t *command,
		const grid_playtest_target_t *target)
{
	s32 players, creators;
	if (!(s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_INTERACTIVE_VIEW)
			|| !gridEditRoomGetVisibility(s_EditRoom, &players, &creators) || !creators) {
		return GRID_EDIT_DENIED;
	}
	if (!editActivePlaytestWorld(s_EditInvite.identity.room_id)
			|| s_EditLayout.phase == EDIT_LAYOUT_OUTCOME
			|| target->epoch != g_NetStageEpoch
			|| target->layout_generation != gridMatchRuntimePrivateLayoutGeneration()
			|| !gridEditPlaytestTargetMatchesView(target, &s_EditPresentation.frame) || !g_Rooms) {
		return GRID_EDIT_STALE;
	}
	const grid_playtest_view_t *frame = &s_EditPresentation.frame;
	const s32 decoration = command->operation == GRID_EDIT_MOVE_DECORATION;
	const f32 x = decoration ? command->data.move_decoration.x : command->data.move_spawn.position[0];
	const f32 z = decoration ? command->data.move_decoration.z : command->data.move_spawn.position[2];
	if (x < frame->min_x || x > frame->max_x || z < frame->min_z || z > frame->max_z) {
		return GRID_EDIT_INVALID;
	}
	if (decoration) return GRID_EDIT_APPLIED;

	const f32 *position = command->data.move_spawn.position;
	struct coord point = { .x = position[0], .y = position[1], .z = position[2] };
	RoomNum inside[24], above[22];
	bgFindRoomsByPos(&point, inside, above, 20, NULL);
	RoomNum *rooms = inside[0] != -1 ? inside : above;
	f32 floor_y = 0.0f;
	struct coord floor_normal;
	struct prop *floor_prop = NULL;
	const RoomNum room = rooms[0] != -1
		? cdFindFloorRoomYColourNormalPropAtPos(&point, rooms, &floor_y, NULL, &floor_normal, &floor_prop) : -1;
	if (room <= 0 || room >= g_Vars.roomcount || floor_prop || !__builtin_isfinite(floor_y)
			|| !editViewRoomBounds(&g_Rooms[room])
			|| point.y < g_Rooms[room].bbmin[1] || point.y > g_Rooms[room].bbmax[1]) {
		editError("Choose a player start above a native floor in the playtest.");
		return GRID_EDIT_INVALID;
	}
	return GRID_EDIT_APPLIED;
}

static s32 editApplyHost(struct netclient *peer, u64 actor, u64 connection,
		const grid_edit_command_t *command, const grid_playtest_target_t *target)
{
	grid_edit_room_state_t state;
	const s32 defer_delta = !connection && s_EditResume.phase != EDIT_RESUME_NONE
		&& gridEditRoomGetState(s_EditRoom, &state) && state.is_host && state.pending_resume;
	grid_edit_delta_t delta;
	grid_edit_receipt_t receipt;
	grid_playtest_decoration_move_t move;
	s32 move_index = -1;
	gridEditRoomHostAdvanceClock(s_EditRoom, SDL_GetTicks64());
	grid_edit_result_t result = target ? editValidatePlanTarget(command, target) : GRID_EDIT_APPLIED;
	if (result == GRID_EDIT_APPLIED) result = editPlanDecorationMove(command, target != NULL, &move, &move_index);
	if (result == GRID_EDIT_APPLIED) {
		result = gridEditRoomHostApply(s_EditRoom, &s_EditInvite.identity,
			actor, connection, command, &receipt, &delta, s_EditError, sizeof(s_EditError));
	}
	if (result == GRID_EDIT_APPLIED) {
		if (move_index >= 0) {
			move.draft_revision = delta.revision;
			s_EditDecorationMoves[move_index] = move;
		}
		if (!defer_delta && !editSendDelta(peer, &delta)) {
			editEndCreator(peer, "Connection send failed. The Room kept your draft.");
			return 0;
		}
		sysLogPrintf(LOG_NOTE, "NET.GRID.EDIT: committed revision=%llu author=%llu",
			(unsigned long long)delta.revision, (unsigned long long)delta.author_id);
		return 1;
	}
	if (connection && result < 0) {
		const grid_edit_room_control_t refusal = {
			.kind = GRID_EDIT_ROOM_CONTROL_REFUSAL,
			.invite.identity = s_EditInvite.identity,
			.requested_sequence = command->sequence,
			.expected_revision = command->expected_revision,
			.result = result,
		};
		if (!editSendControl(peer, SVC_GRID_EDIT_CONTROL, &refusal)) {
			editEndCreator(peer, "Could not send the edit result. Invite the creator again after the connection recovers.");
		}
	}
	return 0;
}

static s32 editSubmitCommand(const grid_edit_command_t *operation, const grid_playtest_target_t *target)
{
	grid_edit_room_state_t state;
	struct netclient *peer = editBoundPeer();
	if (!peer || !gridEditRoomGetState(s_EditRoom, &state) || !state.can_edit) {
		return editError("Shared Grid must be ready before applying edits.");
	}
	grid_playtest_view_t frame;
	if (target && (state.is_host || !netGridEditGetPlaytestView(&frame) || !gridEditPlaytestTargetMatchesView(target, &frame))) {
		return editError("The live plan changed. Choose the position again on the current plan.");
	}
	grid_edit_command_t command = *operation;
	if (state.is_host) {
		command.sequence = state.next_sequence;
		command.expected_revision = state.revision;
		return editApplyHost(peer, state.local_actor, 0, &command, NULL);
	}
	s32 built;
	if (operation->operation == GRID_EDIT_MOVE_SPAWN) {
		built = gridEditRoomGuestBuildMoveSpawn(s_EditRoom, &operation->data.move_spawn, &command);
	} else if (operation->operation == GRID_EDIT_MOVE_DECORATION) {
		built = gridEditRoomGuestBuildMoveDecoration(s_EditRoom, &operation->data.move_decoration, &command);
	} else {
		const grid_edit_ambient_light_t *ambient = &operation->data.ambient_light;
		built = gridEditRoomGuestBuildAmbient(s_EditRoom, ambient->enabled, ambient->brightness, &command);
	}
	if (!built) {
		return editError("The previous Shared Grid edit is pending or the value is invalid.");
	}
	u8 data[1 + GRID_PLAYTEST_TARGET_COMMAND_WIRE_MAX];
	struct netbuf wire = { .data = data, .size = sizeof(data) };
	netbufWriteU8(&wire, CLC_GRID_EDIT_COMMAND);
	const s32 encoded = target ? gridEditPlaytestTargetCommandWrite(&wire, &s_EditInvite.identity, target, &command)
		: gridEditCommandWrite(&wire, &s_EditInvite.identity, &command);
	if (!encoded || !editSend(peer, &wire)) {
		gridEditRoomGuestReceiveReceipt(s_EditRoom, &s_EditInvite.identity, s_EditConnection,
			command.sequence, command.expected_revision, GRID_EDIT_RESOURCE_EXHAUSTED);
		return editError("Could not send the Shared Grid edit.");
	}
	s_EditError[0] = '\0';
	return 1;
}

s32 netGridEditSetAmbient(s32 enabled, s32 brightness)
{
	const grid_edit_command_t command = {
		.operation = GRID_EDIT_SET_AMBIENT_LIGHT,
		.data.ambient_light = { .enabled = enabled, .brightness = brightness },
	};
	return editSubmitCommand(&command, NULL);
}

s32 netGridEditSpawnCount(void)
{
	return gridEditRoomSpawnCount(s_EditRoom);
}

s32 netGridEditGetSpawnPose(s32 index, grid_edit_move_spawn_t *out)
{
	return gridEditRoomGetSpawnPose(s_EditRoom, index, out);
}

s32 netGridEditMoveSpawn(const grid_edit_move_spawn_t *move)
{
	return netGridEditMoveSpawnOnPlan(move, NULL);
}

s32 netGridEditMoveSpawnOnPlan(const grid_edit_move_spawn_t *move, const grid_playtest_target_t *target)
{
	if (!move) return editError("Select a spawn to move.");
	const grid_edit_command_t command = {
		.operation = GRID_EDIT_MOVE_SPAWN,
		.data.move_spawn = *move,
	};
	return editSubmitCommand(&command, target);
}

s32 netGridEditDecorationCount(void)
{
	return gridEditRoomDecorationCount(s_EditRoom);
}

s32 netGridEditGetDecorationPose(s32 index, grid_edit_move_decoration_t *out)
{
	return gridEditRoomGetDecorationPose(s_EditRoom, index, out);
}

s32 netGridEditMoveDecoration(const grid_edit_move_decoration_t *move)
{
	return netGridEditMoveDecorationOnPlan(move, NULL);
}

s32 netGridEditMoveDecorationOnPlan(const grid_edit_move_decoration_t *move, const grid_playtest_target_t *target)
{
	if (!move) return editError("Select a decoration to move.");
	const grid_edit_command_t command = {
		.operation = GRID_EDIT_MOVE_DECORATION,
		.data.move_decoration = *move,
	};
	return editSubmitCommand(&command, target);
}

s32 netGridEditCanStageRoomExtension(void)
{
	grid_edit_room_state_t draft;
	return s_EditSourceOrigin == GRID_ARCHIVE_ORIGINAL
		&& editHostRoomStillOwned() && gridEditRoomGetState(s_EditRoom, &draft)
		&& draft.is_host && !draft.pending_invite && (draft.ready || draft.creator_detached);
}

static s32 editStageVariant(const mod_author_result_t *result, const grid_edit_room_invite_t *source,
	u64 revision, s32 room, s32 extension, const char *directory)
{
	forge_revision_t candidate = {0};
	snprintf(candidate.arena_id, sizeof(candidate.arena_id), "%s", result->catalog_id);
	if (snprintf(candidate.path, sizeof(candidate.path), "%s", result->archive_path)
			>= (s32)sizeof(candidate.path)) return editError("The staged arena archive path is too long.");
	if (!editPreflightVariantFile(candidate.path, candidate.arena_id, NULL, candidate.digest)) return 0;

	forge_revision_t parent;
	if (forgeRevisionReadParent(directory, &candidate, &parent, s_EditError, sizeof(s_EditError))
			!= FORGE_REVISION_PARENT_AVAILABLE
			|| strcmp(parent.arena_id, source->arena_id) != 0 || strcmp(parent.digest, source->arena_sha256) != 0) {
		return editError("The staged arena does not descend from this draft's exact original revision.");
	}

	grid_edit_staged_variant_t staged = {
		.source = *source, .draft_revision = revision, .edited_room = (u8)room, .extension = (u16)extension,
	};
	if (!forgeRevisionPublishLocal(directory, candidate.arena_id, candidate.path, candidate.digest,
			&staged.pin, s_EditError, sizeof(s_EditError))) return 0;
	snprintf(staged.mod_id, sizeof(staged.mod_id), "%s", result->mod_id);
	snprintf(staged.archive_path, sizeof(staged.archive_path), "%s", candidate.path);
	editCancelReview("variant-replaced");
	s_EditStagedVariant = staged;
	return 1;
}

static s32 editSaveVariant(const char *name, s32 room, s32 extra_length, char *arena_id, size_t capacity)
{
	grid_edit_room_state_t state;
	if (!arena_id || capacity < CATALOG_ID_LEN || !name || !name[0]) {
		return editError("Enter a new arena name for the shared variant.");
	}
	if (!editHostRoomStillOwned() || !gridEditRoomGetState(s_EditRoom, &state) || !state.is_host
			|| (!state.creator_detached && (!state.ready || !editBoundPeer()))) {
		return editError("Only the room host can save this shared draft.");
	}
	if (room && !netGridEditCanStageRoomExtension()) {
		return editError("Open a shared original draft to extend a room.");
	}
	u64 revision = 0;
	forge_level_doc_t *document = gridEditRoomHostCloneDocument(s_EditRoom, &revision);
	if (!document) return editError("Could not copy the committed shared draft.");
	mod_author_result_t result;
	const s32 saved = room
		? forgeLevelForkExtendedRevision(state.invite.arena_id, state.invite.arena_sha256,
			document, name, room, extra_length, &result, s_EditError, sizeof(s_EditError))
		: forgeLevelForkEditedRevision(state.invite.arena_id, state.invite.arena_sha256,
			document, name, &result, s_EditError, sizeof(s_EditError));
	forgeLevelDocFree(document);
	if (!saved) return 0;
	sysLogPrintf(LOG_NOTE, "NET.GRID.EDIT: saved variant=%s revision=%llu", result.catalog_id,
		(unsigned long long)revision);

	char directory[FS_MAXPATH + 1];
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	if (room) {
		if (!editStageVariant(&result, &state.invite, revision, room, extra_length, directory)) return 0;
		snprintf(arena_id, capacity, "%s", result.catalog_id);
		sysLogPrintf(LOG_NOTE,
			"GRID.PLAYTEST: staged native variant=%s revision=%llu room=%d extension=%d active_epoch=%u draft_generation=%llu sha256=%s parent=%s",
			result.catalog_id, (unsigned long long)revision, room, extra_length, g_NetStageEpoch,
			(unsigned long long)state.invite.identity.generation, s_EditStagedVariant.pin.digest, state.invite.arena_sha256);
		return 1;
	}

	snprintf(arena_id, capacity, "%s", result.catalog_id);
	forge_revision_t pinned = {0};
	if (forgeRevisionPublishLocal(directory, result.catalog_id, result.archive_path,
			NULL, &pinned, s_EditError, sizeof(s_EditError))) {
		s_EditSavedVariant = pinned;
		sysLogPrintf(LOG_NOTE, "NET.GRID.EDIT: pinned variant=%s revision=%llu sha256=%s",
			result.catalog_id, (unsigned long long)revision, s_EditSavedVariant.digest);
	} else {
		sysLogPrintf(LOG_WARNING, "NET.GRID.EDIT: saved variant could not be pinned: %s", s_EditError);
	}
	return 1;
}

s32 netGridEditSaveVariant(const char *name, char *arena_id, size_t capacity)
{
	return editSaveVariant(name, 0, 0, arena_id, capacity);
}

static s32 editSharedVariantContext(grid_archive_origin_e origin, grid_edit_room_state_t *draft,
	net_grid_edit_variant_review_t *key)
{
	if (s_EditSourceOrigin != origin || !editHostRoomStillOwned() || !gridMatchRuntimeIsIdle()
			|| g_NetStageEpoch || mainStageChangePending() || !gridEditRoomGetState(s_EditRoom, draft)
			|| !draft->is_host || (!draft->creator_detached && (!draft->ready || !editBoundPeer()))) return 0;
	const hub_room_t *room = roomGetById(draft->invite.identity.room_id);
	if (!room || !room->lifetime || !g_NetLocalClient || room->creator_client_id != g_NetLocalClient->id
			|| g_NetLocalClient->state != CLSTATE_LOBBY) return 0;
	*key = (net_grid_edit_variant_review_t){
		.identity = draft->invite.identity, .room_lifetime = room->lifetime, .revision = draft->revision,
	};
	memcpy(key->parent_sha256, draft->invite.arena_sha256, sizeof(key->parent_sha256));
	return 1;
}

static s32 editLocalByorVariantContext(grid_edit_room_state_t *draft, net_grid_edit_variant_review_t *key)
{
	if (!editSharedVariantContext(GRID_ARCHIVE_BYOR, draft, key) || draft->pending_invite
			|| draft->pending_command) return 0;
	const asset_entry_t *arena = assetCatalogResolve(draft->invite.arena_id);
	return arena && arena->type == ASSET_ARENA && arena->enabled && !arena->bundled
		&& !arena->temporary && arena->grid_offer_only;
}

s32 netGridEditCanSaveLocalByorVariant(void)
{
	grid_edit_room_state_t draft;
	net_grid_edit_variant_review_t key;
	return editLocalByorVariantContext(&draft, &key);
}

static void editPinAndEnableSavedVariant(const mod_author_result_t *saved, u64 revision)
{
	char directory[FS_MAXPATH + 1];
	forge_revision_t pin;
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	if (!s_EditError[0] && forgeRevisionPublishLocal(directory, saved->catalog_id, saved->archive_path,
			NULL, &pin, s_EditError, sizeof(s_EditError))
			&& modmgrEnableAuthoredGridVariant(saved->mod_dir, saved->mod_id, pin.arena_id, pin.digest,
				s_EditError, sizeof(s_EditError))) {
		s_EditSavedVariant = pin;
		sysLogPrintf(LOG_NOTE, "NET.GRID.EDIT: pinned variant=%s revision=%llu sha256=%s",
			pin.arena_id, (unsigned long long)revision, pin.digest);
	} else {
		sysLogPrintf(LOG_WARNING, "NET.GRID.EDIT: saved variant is not yet ready for the library: %s", s_EditError);
	}
	sysLogPrintf(LOG_NOTE, "NET.GRID.EDIT: saved variant=%s revision=%llu", saved->catalog_id, (unsigned long long)revision);
}

s32 netGridEditSaveLocalByorVariant(const char *name, char *arena_id, size_t capacity)
{
	grid_edit_room_state_t draft;
	net_grid_edit_variant_review_t key;
	if (!arena_id || capacity < CATALOG_ID_LEN || !name || !name[0] || !editLocalByorVariantContext(&draft, &key)) {
		return editError("Save the committed local BYOR draft from the host's Room after play ends.");
	}
	u64 revision = 0;
	forge_level_doc_t *doc = gridEditRoomHostCloneDocument(s_EditRoom, &revision);
	mod_author_result_t *saved = calloc(1, sizeof(*saved));
	if (!doc || !saved || revision != key.revision) {
		forgeLevelDocFree(doc);
		free(saved);
		return editError("Could not copy the exact shared draft for saving.");
	}
	const s32 published = forgeLevelForkLocalByorRevision(draft.invite.arena_id, key.parent_sha256,
		doc, name, saved, s_EditError, sizeof(s_EditError));
	forgeLevelDocFree(doc);
	if (published) {
		snprintf(arena_id, capacity, "%s", saved->catalog_id);
		editPinAndEnableSavedVariant(saved, revision);
	}
	free(saved);
	return published;
}

s32 netGridEditCanReviewVariantSources(void)
{
	grid_edit_room_state_t draft;
	net_grid_edit_variant_review_t key;
	return editSharedVariantContext(GRID_ARCHIVE_ORIGINAL, &draft, &key);
}

static s32 editSharedVariantReviewSame(const net_grid_edit_variant_review_t *current,
		const net_grid_edit_variant_review_t *reviewed)
{
	return reviewed && current->identity.room_id == reviewed->identity.room_id
		&& current->identity.generation == reviewed->identity.generation
		&& current->room_lifetime == reviewed->room_lifetime && current->revision == reviewed->revision
		&& memcmp(current->parent_sha256, reviewed->parent_sha256, sizeof(current->parent_sha256)) == 0;
}

s32 netGridEditVariantReviewMatches(const net_grid_edit_variant_review_t *key)
{
	grid_edit_room_state_t draft;
	net_grid_edit_variant_review_t current;
	return editSharedVariantContext(GRID_ARCHIVE_ORIGINAL, &draft, &current) && editSharedVariantReviewSame(&current, key);
}

s32 netGridEditReviewVariantSources(net_grid_edit_variant_review_t *key,
		forge_level_doc_t **out_doc, grid_match_sources_t *out)
{
	grid_edit_room_state_t draft;
	net_grid_edit_variant_review_t current;
	if (!key || !out_doc || *out_doc || !out || !editSharedVariantContext(GRID_ARCHIVE_ORIGINAL, &draft, &current)) {
		return editError("Review the committed shared draft from the host's Room.");
	}
	u64 revision = 0;
	forge_level_doc_t *doc = gridEditRoomHostCloneDocument(s_EditRoom, &revision);
	if (!doc || revision != current.revision) {
		forgeLevelDocFree(doc);
		return editError("Could not copy the exact shared draft.");
	}
	grid_match_sources_t plan = {0};
	const s32 ok = forgeLevelReviewSharedVariantSources(draft.invite.arena_id, current.parent_sha256,
		doc, &plan, s_EditError, sizeof(s_EditError));
	if (!ok || !netGridEditVariantReviewMatches(&current)) {
		forgeLevelDocFree(doc);
		gridMatchSourcesFree(&plan);
		return ok ? editError("The shared draft changed while reviewing its sources.") : 0;
	}
	gridMatchSourcesFree(out);
	*out = plan;
	*out_doc = doc;
	*key = current;
	return 1;
}

s32 netGridEditSaveSelectedVariant(const net_grid_edit_variant_review_t *key, const char *name,
		const grid_match_sources_t *reviewed, const u16 *omitted_packages, size_t omitted_count,
		const forge_level_placement_fallback_t *fallbacks, size_t fallback_count, char *arena_id, size_t capacity)
{
	grid_edit_room_state_t draft;
	net_grid_edit_variant_review_t current;
	if (!arena_id || capacity < CATALOG_ID_LEN || !name || !name[0]
			|| !editSharedVariantContext(GRID_ARCHIVE_ORIGINAL, &draft, &current)
			|| !editSharedVariantReviewSame(&current, key)) {
		return editError("The shared draft changed. Review its sources again and enter a new variant name.");
	}
	u64 revision = 0;
	forge_level_doc_t *doc = gridEditRoomHostCloneDocument(s_EditRoom, &revision);
	mod_author_result_t *saved = calloc(1, sizeof(*saved));
	if (!doc || !saved || revision != current.revision) {
		forgeLevelDocFree(doc);
		free(saved);
		return editError("Could not copy the exact shared draft for saving.");
	}
	const s32 published = forgeLevelSaveSharedVariant(draft.invite.arena_id, current.parent_sha256, doc, name,
		reviewed, omitted_packages, omitted_count, fallbacks, fallback_count, saved, s_EditError, sizeof(s_EditError));
	forgeLevelDocFree(doc);
	if (!published) {
		free(saved);
		return 0;
	}
	snprintf(arena_id, capacity, "%s", saved->catalog_id);
	editPinAndEnableSavedVariant(saved, revision);
	free(saved);
	return 1;
}

s32 netGridEditStageRoomExtension(const char *name, s32 room, s32 extra_length,
	char *arena_id, size_t capacity)
{
	if (room != 1 && room != 2) return editError("Select one of the two original rooms.");
	return editSaveVariant(name, room, extra_length, arena_id, capacity);
}

static s32 editSendGrant(struct netclient *client)
{
	const grid_edit_room_control_t grant = {
		.kind = GRID_EDIT_ROOM_CONTROL_GRANT, .invite = s_EditInvite, .granted_actor = s_EditGuestActor,
	};
	if (!editSendControl(client, SVC_GRID_EDIT_CONTROL, &grant)) {
		editEndCreator(client, "Could not send the Shared Grid creator grant.");
		return 0;
	}
	s_EditDeadline = 0;
	s32 players, creators;
	if (gridEditRoomGetVisibility(s_EditRoom, &players, &creators) && players) editSendCursorPolicy(client, 1);
	sysLogPrintf(LOG_NOTE, "NET.GRID.EDIT: granted client=%u", client->id);
	return 1;
}

static void editBeginSnapshotSend(struct netclient *client, const grid_edit_room_invite_t *confirmation)
{
	if (!gridEditRoomHostConfirmResume(s_EditRoom, s_EditGuestActor, s_EditConnection, confirmation)) return;
	s_EditResume.confirmation = *confirmation;
	if (!editSendSnapshotOffer(client, SVC_GRID_EDIT_SNAPSHOT, GRID_EDIT_SNAPSHOT_OFFER)) {
		editEndCreator(client, "Could not offer the Shared Grid draft snapshot.");
		return;
	}
	s_EditDeadline = SDL_GetTicks() + GRID_EDIT_INVITE_TIMEOUT_MS;
	s_EditResume.phase = EDIT_RESUME_SENDING;
}

static void editReceiveSnapshotOffer(struct netclient *host, struct netbuf *payload)
{
	grid_edit_snapshot_offer_t offer;
	if (s_EditResume.phase != EDIT_RESUME_NONE || s_EditOfferPending
			|| !gridEditSnapshotOfferRead(payload, GRID_EDIT_SNAPSHOT_OFFER, &offer)
			|| !editSameIdentity(&offer.identity)
			|| !gridEditRoomGuestRequireResume(s_EditRoom, &offer.identity, s_EditConnection)) return;
	s_EditResume.bytes = malloc(offer.size);
	if (!s_EditResume.bytes) {
		editEndCreator(host, "Could not allocate the Shared Grid draft snapshot.");
		return;
	}
	s_EditResume.offer = offer;
	s_EditResume.phase = EDIT_RESUME_RECEIVING;
	s_EditDeadline = SDL_GetTicks() + GRID_EDIT_INVITE_TIMEOUT_MS;
}

static void editReceiveSnapshotChunk(struct netclient *host, struct netbuf *payload)
{
	grid_edit_snapshot_chunk_t chunk;
	if (s_EditResume.phase != EDIT_RESUME_RECEIVING
			|| !gridEditSnapshotChunkRead(payload, &chunk) || !editSameIdentity(&chunk.identity)) return;
	if (chunk.offset != s_EditResume.offset || chunk.size > s_EditResume.offer.size - s_EditResume.offset) {
		editEndCreator(host, "Shared Grid draft snapshot arrived out of order.");
		return;
	}
	memcpy(s_EditResume.bytes + s_EditResume.offset, chunk.bytes, chunk.size);
	s_EditResume.offset += chunk.size;
	if (s_EditResume.offset != s_EditResume.offer.size) return;
	char reason[256] = {0};
	if (!gridEditRoomGuestInstallResume(s_EditRoom, s_EditConnection, &s_EditResume.offer,
			s_EditResume.bytes, s_EditResume.offset, reason, sizeof(reason))) {
		editEndCreator(host, reason[0] ? reason : "Could not verify the Shared Grid draft snapshot.");
		return;
	}
	free(s_EditResume.bytes);
	s_EditResume.bytes = NULL;
	s_EditResume.phase = EDIT_RESUME_WAITING_GRANT;
	if (!editSendSnapshotOffer(host, CLC_GRID_EDIT_SNAPSHOT, GRID_EDIT_SNAPSHOT_ACK)) {
		editEndCreator(host, "Could not acknowledge the verified Shared Grid draft snapshot.");
		return;
	}
	char digest[SHA256_HEX_SIZE];
	sha256ToHex(s_EditResume.offer.sha256, digest);
	sysLogPrintf(LOG_NOTE, "NET.GRID.EDIT: verified resume snapshot revision=%llu digest=%s",
		(unsigned long long)s_EditResume.offer.revision, digest);
}

static void editReceiveSnapshot(struct netclient *peer, s32 is_client, struct netbuf *payload)
{
	if (!s_EditRoom || peer != editBoundPeer()) return;
	const u8 kind = payload->data[payload->rp];
	if (is_client) {
		if (kind == GRID_EDIT_SNAPSHOT_OFFER) editReceiveSnapshotOffer(peer, payload);
		else if (kind == GRID_EDIT_SNAPSHOT_CHUNK) editReceiveSnapshotChunk(peer, payload);
		return;
	}
	grid_edit_snapshot_offer_t ack;
	if (kind != GRID_EDIT_SNAPSHOT_ACK || s_EditResume.phase != EDIT_RESUME_WAITING_ACK
			|| !gridEditSnapshotOfferRead(payload, GRID_EDIT_SNAPSHOT_ACK, &ack)
			|| !gridEditSnapshotOfferMatches(&s_EditResume.offer, &ack)) return;
	grid_edit_room_resume_tail_t tail;
	if (!gridEditRoomHostGrantResume(s_EditRoom, s_EditGuestActor, s_EditConnection,
			&s_EditResume.confirmation, &ack, &tail)) {
		editEndCreator(peer, "Could not verify the edits made during Shared Grid resume. The Room kept your draft.");
		return;
	}
	const s32 live_resume = s_EditResume.live_epoch && s_EditResume.live_epoch == g_NetStageEpoch
		&& s_EditResume.live_generation == s_EditPlaytest.draft.generation
		&& editActivePlaytestWorld(ack.identity.room_id);
	/* Core has advanced the retained host draft even if a later send fails. */
	if (live_resume) s_EditPlaytest.draft = ack.identity;
	/* Deltas and the grant use the same reliable control channel. The guest
	 * remains a pending creator while it receives the committed edits. */
	for (size_t i = 0; i < tail.count; ++i) {
		if (!editSendDelta(peer, &tail.deltas[i])) {
			editEndCreator(peer, "Could not send the Shared Grid resume edits. The Room kept your draft.");
			return;
		}
	}
	const u64 revision = tail.count ? tail.deltas[tail.count - 1].revision : ack.revision;
	if (editSendGrant(peer)) {
		if (live_resume) {
			sysLogPrintf(LOG_NOTE, "GRID.PLAYTEST: resumed draft generation=%llu revision=%llu epoch=%u",
				(unsigned long long)ack.identity.generation, (unsigned long long)revision,
				s_EditPlaytest.stage_epoch);
		}
		editClearResumeRoomInvite();
		netRoomListMarkDirty();
		char digest[SHA256_HEX_SIZE];
		sha256ToHex(ack.sha256, digest);
		sysLogPrintf(LOG_NOTE, "NET.GRID.EDIT: resumed creator=%u revision=%llu digest=%s tail=%u",
			peer->id, (unsigned long long)revision, digest, (unsigned)tail.count);
		editClearResume();
	}
}

static void editReceiveReviewTransferRequest(const grid_edit_room_control_t *request)
{
	const grid_edit_room_control_t *offer = &s_EditReview.offer;
	if (!editTransferReview(offer->kind) || s_EditReview.reply || s_EditReview.transfer_active
			|| !editReviewMatchesCurrent() || editReviewExpired() || !editReviewSourcesReady(offer, s_EditReview.sources)
			|| request->review_arena_size != offer->review_arena_size
			|| !gridEditVariantReviewMatches(&offer->review, &request->review)
			|| memcmp(request->review_deps_sha256, offer->review_deps_sha256, SHA256_DIGEST_SIZE) != 0) return;
	s_EditReview.transfer_active = 1;
	s_EditReview.deadline = SDL_GetTicks() + GRID_EDIT_REVIEW_TRANSFER_TIMEOUT_MS;
	sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW.TRANSFER: consented role=host serial=%llu bytes=%u",
		(unsigned long long)offer->review.offer_serial, offer->review_arena_size);
}

static void editReceiveReviewArenaChunk(struct netclient *client, struct netbuf *payload)
{
	if (!s_EditReview.transfer_active || !s_EditReview.transfer || s_EditReview.reply
			|| client != editBoundPeer() || !editReviewMatchesCurrent() || editReviewExpired()) return;
	const grid_edit_room_control_t offer = s_EditReview.offer;
	const grid_match_offer_t key = editReviewSourcesKey(&offer);
	u64 draft_generation;
	grid_match_chunk_t chunk;
	if (!gridEditReviewChunkRead(payload, &draft_generation, &chunk)
			|| draft_generation != offer.invite.identity.generation || chunk.offer.room_id != key.room_id
			|| chunk.offer.generation != key.generation || strcmp(chunk.offer.manifest_digest, key.manifest_digest) != 0) return;
	if (!gridMatchTransferAppend(s_EditReview.transfer, &chunk)) {
		editCancelReview("invalid-chunk");
		editError("The arena transfer did not match this review.");
		return;
	}
	if (chunk.offset == s_EditReview.transfer_offset) s_EditReview.transfer_offset += chunk.size;
	u32 size = 0;
	const void *bytes = gridMatchTransferComplete(s_EditReview.transfer, &key, 0, &size);
	if (!bytes) return;

	char arena_sha[SHA256_HEX_SIZE];
	char reason[sizeof(s_EditError)] = {0};
	grid_match_offer_t measured = {0};
	grid_match_cache_t *sources = NULL;
	sha256ToHex(offer.review.arena_sha256, arena_sha);
	const s32 prepared = forgeLevelPrepareReceivedStagedArenaSources(bytes, size,
		offer.review.arena_id, arena_sha, s_EditInvite.arena_id, s_EditInvite.arena_sha256,
		key.room_id, key.generation, &measured, &sources, reason, sizeof(reason));
	if (!editReviewMatchesCurrent() || editReviewExpired()) {
		gridMatchCacheReset(&sources);
		editCancelReview("scope-changed");
		editError("The arena review expired or changed during verification.");
		return;
	}
	if (!prepared || measured.room_id != key.room_id || measured.generation != key.generation
			|| strcmp(measured.manifest_digest, key.manifest_digest) != 0
			|| !editReviewSourcesReady(&offer, sources)) {
		gridMatchCacheReset(&sources);
		editCancelReview("package-unverified");
		editError(reason[0] ? reason : "The arena did not match the review's exact source packages.");
		return;
	}
	grid_edit_room_control_t reply = offer;
	reply.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_REPLY;
	reply.review_arena_size = 0;
	reply.review_reply = GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED;
	if (!editReadReviewDigest(measured.manifest_digest, reply.review_deps_sha256)
			|| !editSendControl(client, CLC_GRID_EDIT_CONTROL, &reply)) {
		gridMatchCacheReset(&sources);
		editCancelReview("reply-failed");
		editError("Could not confirm the arena review.");
		return;
	}

	s_EditReview.sources = sources;
	s_EditReview.reply = reply.review_reply;
	s_EditReview.deadline = SDL_GetTicks() + GRID_EDIT_REVIEW_TIMEOUT_MS;
	s_EditError[0] = '\0';
	sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW.TRANSFER: verified serial=%llu arena=%s bytes=%u sha256=%s",
		(unsigned long long)key.generation, offer.review.arena_id, size, measured.manifest_digest);
	editLogReviewSourcesRetained("creator");
	editReleaseReviewTransfer("verified");
}

static void editPreparePrivateCandidateSources(void)
{
	const grid_match_runtime_identity_t *active = gridMatchRuntimeActive();
	const grid_edit_room_control_t *review = &s_EditReview.offer;
	const grid_match_offer_t arena = editReviewSourcesKey(review);
	char *reason = s_EditReview.candidate_issue;
	const size_t reason_cap = sizeof(s_EditReview.candidate_issue);
	reason[0] = '\0';
	if (!editReviewMatchesCurrent() || !editPlaytestArenaMatches(active)
			|| !editCandidateGameplayCurrent(arena.room_id)) {
		snprintf(reason, reason_cap, "The live private roster or match settings no longer match.");
	} else if (gridMatchRuntimeComposePrivateCandidate(&active->offer, s_EditReview.sources, &arena,
			s_EditStagedVariant.pin.path, review->review.arena_id, review->review.arena_sha256, &s_EditReview.candidate,
			reason, reason_cap)) {
		const grid_match_private_candidate_t *candidate = &s_EditReview.candidate;
		s_EditReview.candidate_checked_at = SDL_GetTicks();
		sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW.CANDIDATE: prepared serial=%llu epoch=%u entries=%u packages=%u sha256=%s",
			(unsigned long long)arena.generation, (unsigned)g_NetStageEpoch,
			(unsigned)candidate->entry_count, (unsigned)candidate->package_count, candidate->offer.manifest_digest);
		return;
	}
	if (!reason[0]) snprintf(reason, reason_cap, "The exact match source plan could not be built.");
	sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW.CANDIDATE: unavailable serial=%llu reason='%s'",
		(unsigned long long)arena.generation, reason);
}

static void editReceiveVariantReview(s32 is_client, const grid_edit_room_control_t *control)
{
	grid_edit_room_state_t draft;
	if (!editReviewDraft(&draft)) return;
	if (is_client ? (draft.is_host || !s_EditReviewSupported) : (!draft.is_host || draft.peer_actor != s_EditGuestActor)) return;
	const grid_edit_variant_review_t *review = &control->review;
	const s32 prepared = editPreparedReview(s_EditReview.offer.kind);
	if (control->kind == GRID_EDIT_ROOM_CONTROL_REVIEW_CANCEL) {
		const u64 actor = is_client ? draft.local_actor : draft.peer_actor;
		if ((is_client ? editIsReviewOffer(s_EditReview.offer.kind) : prepared)
				&& s_EditReview.connection == s_EditConnection && s_EditReview.actor == actor
				&& editSameIdentity(&s_EditReview.offer.invite.identity)
				&& gridEditVariantReviewMatches(&s_EditReview.offer.review, review)) {
			editClearReview("peer-cancel");
		}
		return;
	}
	if (!is_client) {
		const s32 deps = editReviewHasDependencies(s_EditReview.offer.kind);
		const grid_edit_room_control_kind_e expected_reply = deps
			? GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_REPLY : GRID_EDIT_ROOM_CONTROL_REVIEW_REPLY;
		if (control->kind == GRID_EDIT_ROOM_CONTROL_REVIEW_SUPPORTED) {
			s_EditReviewSupported = 1;
		} else if (control->kind == GRID_EDIT_ROOM_CONTROL_REVIEW_CAPABILITIES && s_EditReviewSupported) {
			const u64 previous_token = editHostCursorToken();
			s_EditReviewCapabilities |= control->review_capabilities;
			if (previous_token != editHostCursorToken()) s_EditPresentation.cursor_policy_dirty = 1;
		} else if (control->kind == GRID_EDIT_ROOM_CONTROL_REVIEW_TRANSFER_REQUEST) {
			editReceiveReviewTransferRequest(control);
		} else if (control->kind == expected_reply && !s_EditReview.reply
				&& editReviewMatchesCurrent() && !editReviewExpired()
				&& gridEditVariantReviewMatches(&s_EditReview.offer.review, review)) {
			if (control->review_reply == GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED) {
				if (!deps || !(s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_DEPS_V1)
						|| memcmp(control->review_deps_sha256, s_EditReview.offer.review_deps_sha256,
							SHA256_DIGEST_SIZE) != 0
						|| (prepared && !editReviewSourcesReady(&s_EditReview.offer, s_EditReview.sources))
						|| (s_EditReview.transfer_active
							&& s_EditReview.transfer_offset != s_EditReview.offer.review_arena_size)) return;
				if (s_EditReview.transfer_active) {
					s_EditReview.deadline = SDL_GetTicks() + GRID_EDIT_REVIEW_TIMEOUT_MS;
					editReleaseReviewTransfer("verified");
				}
				char digest[SHA256_HEX_SIZE];
				sha256ToHex(control->review_deps_sha256, digest);
				sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW.DEPS: matched creator=%u serial=%llu sha256=%s",
					s_EditTarget, (unsigned long long)review->offer_serial, digest);
				if (prepared) {
					sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW.PREPARED: matched creator=%u serial=%llu sha256=%s",
						s_EditTarget, (unsigned long long)review->offer_serial, digest);
					editPreparePrivateCandidateSources();
				}
			} else if (prepared) {
				if (s_EditReview.transfer_active) {
					s_EditReview.deadline = SDL_GetTicks() + GRID_EDIT_REVIEW_TIMEOUT_MS;
					editReleaseReviewTransfer(control->review_reply == GRID_EDIT_VARIANT_REVIEW_DECLINED
						? "declined" : "downgraded");
				}
				editReleaseReviewSources(control->review_reply == GRID_EDIT_VARIANT_REVIEW_DECLINED
					? "declined" : "downgraded");
			}
			s_EditReview.reply = control->review_reply;
			sysLogPrintf(LOG_NOTE, "NET.GRID.REVIEW: creator=%u replied serial=%llu result=%u",
				s_EditTarget, (unsigned long long)review->offer_serial, (unsigned)control->review_reply);
		}
		return;
	}
	if (!editIsReviewOffer(control->kind)
			|| (editReviewHasDependencies(control->kind) && !(s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_DEPS_V1))
			|| (editPreparedReview(control->kind) && !(s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_PREPARED))
			|| (editTransferReview(control->kind) && !editReviewTransferSupported())
			|| review->offer_serial <= s_EditLastReviewSeen || review->draft_revision != draft.revision
			|| review->stage_epoch < s_EditPresentation.frame.epoch) return;
	char parent[SHA256_HEX_SIZE];
	sha256ToHex(review->parent_sha256, parent);
	if (strcmp(parent, draft.invite.arena_sha256) != 0 || strcmp(review->arena_id, draft.invite.arena_id) == 0) return;
	editClearReview("replaced");
	s_EditLastReviewSeen = review->offer_serial;
	s_EditReview.offer = *control;
	s_EditReview.connection = s_EditConnection;
	s_EditReview.actor = draft.local_actor;
	s_EditReview.deadline = SDL_GetTicks() + GRID_EDIT_REVIEW_TIMEOUT_MS;
}

static void editStoreObservedLayout(const grid_edit_room_control_t *control)
{
	s_EditCreatorLayoutReceipt = *control;
	s_EditCreatorLayoutReceipt.kind = GRID_EDIT_ROOM_CONTROL_APPLY_BASELINE;
	s_EditCreatorLayoutReceipt.apply_result = 0;
	s_EditCreatorLayoutEpoch = control->review.stage_epoch;
	s_EditCreatorLayoutGeneration = control->apply_layout_generation;
	if (s_EditCreatorProposalGeneration < s_EditCreatorLayoutGeneration) {
		s_EditCreatorProposalGeneration = s_EditCreatorLayoutGeneration;
	}
}

static void editReceiveLayoutControl(s32 is_client, const grid_edit_room_control_t *control)
{
	grid_edit_room_state_t draft;
	if (!(s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_APPLY) || !editReviewDraft(&draft)
			|| (is_client ? draft.is_host : (!draft.is_host || draft.peer_actor != s_EditGuestActor))) return;
	const u64 actor = is_client ? draft.local_actor : draft.peer_actor;
	if (!is_client) {
		if (control->kind != GRID_EDIT_ROOM_CONTROL_APPLY_REPLY || s_EditLayout.phase != EDIT_LAYOUT_WAITING_REPLY
				|| s_EditLayout.connection != s_EditConnection || s_EditLayout.actor != actor
				|| !gridEditLayoutApplyMatches(&s_EditLayout.intent, control)) return;
		if (control->apply_result == GRID_EDIT_LAYOUT_APPLY_READY
				&& s_EditLayout.intent.apply_layout_generation == 1 && smokeHarnessIsActive()
				&& sysArgCheck("--debug-grid-private-layout-retry")) {
			if (!s_EditSmokeLayoutReply.connection) {
				s_EditSmokeLayoutReply.reply = *control;
				s_EditSmokeLayoutReply.connection = s_EditConnection;
				s_EditSmokeLayoutReply.actor = actor;
				editLogLayout("held", "host", control, actor);
			}
			return;
		}
		editLogLayout("reply", "host", control, actor);
		if (control->apply_result == GRID_EDIT_LAYOUT_APPLY_READY) {
			s_EditLayout.phase = EDIT_LAYOUT_READY;
		} else {
			editFinishLayout(GRID_EDIT_LAYOUT_APPLY_ABORTED, "The creator could not confirm the prepared layout.");
		}
		return;
	}

	if (control->kind == GRID_EDIT_ROOM_CONTROL_APPLY_BASELINE) {
		if (!(s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_APPLY_BASELINE) || s_EditLayout.phase != EDIT_LAYOUT_NONE
				|| control->review.stage_epoch < s_EditPresentation.frame.epoch
				|| control->review.stage_epoch < s_EditCreatorLayoutEpoch
				|| control->apply_layout_generation < s_EditCreatorLayoutGeneration
				|| control->apply_layout_generation <= s_EditCreatorLayoutReceipt.apply_layout_generation
				|| !editLayoutReceiptMatchesDraft(control, &draft)) return;
		editStoreObservedLayout(control);
		/* This is a host receipt only. Keep cursor permission independent
		 * and never touch the local review cache or its verification state. */
		editClearLayoutView();
		editLogLayout("baseline", "creator", control, actor);
		return;
	}

	if (control->kind == GRID_EDIT_ROOM_CONTROL_APPLY_OUTCOME) {
		if (s_EditLayout.phase != EDIT_LAYOUT_READY || !s_EditLayout.ready_sources || s_EditLayout.connection != s_EditConnection
				|| s_EditLayout.actor != actor || !gridEditLayoutApplyMatches(&s_EditLayout.intent, control)) return;
		if (control->apply_result == GRID_EDIT_LAYOUT_APPLY_COMMITTED) {
			editStoreObservedLayout(control);
			editClearLayoutProjection();
			if (editLayoutMatchesReview(control)) editClearReview("layout-committed");
			s_EditError[0] = '\0';
		}
		editLogLayout("outcome", "creator", control, actor);
		editClearLayout();
		return;
	}

	if (control->kind != GRID_EDIT_ROOM_CONTROL_APPLY_PREPARE || s_EditLayout.phase != EDIT_LAYOUT_NONE
			|| !editLayoutMatchesReview(control) || control->apply_layout_generation <= s_EditCreatorProposalGeneration
			|| control->review.stage_epoch < s_EditCreatorLayoutEpoch) return;
	grid_edit_room_control_t reply = *control;
	reply.kind = GRID_EDIT_ROOM_CONTROL_APPLY_REPLY;
	const grid_match_offer_t sources_key = editReviewSourcesKey(&s_EditReview.offer);
	grid_match_cache_t *ready_sources = NULL;
	reply.apply_result = editReviewMatchesCurrent() && !editReviewExpired()
		&& s_EditReview.reply == GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED
		&& (control->review.stage_epoch > s_EditCreatorLayoutEpoch
			|| control->apply_previous_layout_generation == s_EditCreatorLayoutGeneration)
		&& gridMatchCacheRetainReady(&ready_sources, s_EditReview.sources, &sources_key, NULL, 0)
		? GRID_EDIT_LAYOUT_APPLY_READY : GRID_EDIT_LAYOUT_APPLY_REFUSED;
	if (!editSendControl(editBoundPeer(), CLC_GRID_EDIT_CONTROL, &reply)) {
		gridMatchCacheReset(&ready_sources);
		return;
	}
	s_EditCreatorProposalGeneration = control->apply_layout_generation;
	if (reply.apply_result == GRID_EDIT_LAYOUT_APPLY_READY) {
		s_EditCreatorLayoutEpoch = control->review.stage_epoch;
		s_EditCreatorLayoutGeneration = control->apply_previous_layout_generation;
		s_EditLayout.phase = EDIT_LAYOUT_READY;
		s_EditLayout.intent = *control;
		s_EditLayout.ready_sources = ready_sources;
		s_EditLayout.connection = s_EditConnection;
		s_EditLayout.actor = actor;
	}
	editLogLayout("reply", "creator", &reply, actor);
}

static void editReceiveControl(struct netclient *client, s32 is_client,
		const grid_edit_room_control_t *control)
{
	const grid_edit_room_invite_t *invite = &control->invite;
	if (is_client && control->kind == GRID_EDIT_ROOM_CONTROL_INVITE) {
		if (s_EditConnection || client != g_NetLocalClient || client->room_id != invite->identity.room_id
				|| !editLobbyPeer(client, invite->identity.room_id)) return;
		s_EditInvite = *invite;
		s_EditConnection = editConnection(client);
		s_EditOfferPending = 1;
		s_EditDeadline = SDL_GetTicks() + GRID_EDIT_INVITE_TIMEOUT_MS;
		s_EditError[0] = '\0';
		return;
	}
	if (!editSameIdentity(&invite->identity) || client != editBoundPeer()) return;
	if (control->kind == GRID_EDIT_ROOM_CONTROL_APPLY_PREPARE || control->kind == GRID_EDIT_ROOM_CONTROL_APPLY_REPLY
			|| control->kind == GRID_EDIT_ROOM_CONTROL_APPLY_OUTCOME || control->kind == GRID_EDIT_ROOM_CONTROL_APPLY_BASELINE) {
		editReceiveLayoutControl(is_client, control);
		return;
	}
	if ((control->kind >= GRID_EDIT_ROOM_CONTROL_REVIEW_SUPPORTED
			&& control->kind <= GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_REPLY) || editPreparedReview(control->kind)
			|| control->kind == GRID_EDIT_ROOM_CONTROL_REVIEW_TRANSFER_REQUEST) {
		editReceiveVariantReview(is_client, control);
		return;
	}
	if (control->kind == GRID_EDIT_ROOM_CONTROL_CLOSE) {
		if (!is_client && editKeepDraftAfterPeerDeparture()) return;
		netGridEditReset();
		editError(is_client ? "The host ended Shared Grid." : "The invited creator left Shared Grid.");
	} else if (!is_client && control->kind == GRID_EDIT_ROOM_CONTROL_CONFIRM) {
		if (s_EditResume.phase == EDIT_RESUME_INVITED) {
			editBeginSnapshotSend(client, invite);
		} else if (s_EditResume.phase == EDIT_RESUME_NONE
				&& gridEditRoomHostGrant(s_EditRoom, s_EditGuestActor, s_EditConnection, invite)) {
			editSendGrant(client);
		}
	} else if (is_client && control->kind == GRID_EDIT_ROOM_CONTROL_GRANT
			&& (s_EditResume.phase == EDIT_RESUME_NONE || s_EditResume.phase == EDIT_RESUME_WAITING_GRANT)
			&& gridEditRoomGuestGrant(s_EditRoom, invite, s_EditConnection, control->granted_actor)) {
		editClearResume();
		s_EditDeadline = 0;
		sysLogPrintf(LOG_NOTE, "NET.GRID.EDIT: verified baseline and accepted creator grant");
	} else if (is_client && control->kind == GRID_EDIT_ROOM_CONTROL_REFUSAL
			&& gridEditRoomGuestReceiveReceipt(s_EditRoom, &invite->identity, s_EditConnection,
				control->requested_sequence, control->expected_revision, control->result)) {
		const char *reason = "The host refused this Shared Grid edit.";
		if (control->result == GRID_EDIT_STALE) {
			reason = "The arena changed before that edit. Review the current values and try again.";
		} else if (control->result == GRID_EDIT_RESOURCE_LEASED) {
			reason = "The other creator is editing this item. Try again when they finish.";
		}
		editError(reason);
	}
}

static void editReceivePresentation(u8 opcode, struct netbuf *payload)
{
	grid_edit_room_state_t state;
	if (!gridEditRoomGetState(s_EditRoom, &state) || !state.ready) return;
	grid_edit_wire_identity_t identity;
	if (opcode == SVC_GRID_EDIT_VIEW) {
		grid_playtest_view_t frame;
		const s32 world_view = (s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_INTERACTIVE_VIEW)
			&& gridEditPlaytestWorldViewRead(payload, &identity, &frame);
		if ((!world_view && !gridEditPlaytestViewRead(payload, &identity, &frame)) || !editSameIdentity(&identity)) return;
		if (world_view && (frame.epoch < s_EditCreatorLayoutEpoch
				|| frame.layout_generation != (frame.epoch == s_EditCreatorLayoutEpoch ? s_EditCreatorLayoutGeneration : 0))) return;
		if (frame.epoch < s_EditPresentation.frame.epoch
				|| (frame.epoch == s_EditPresentation.frame.epoch && frame.serial <= s_EditPresentation.frame.serial)) return;
		s_EditPresentation.frame = frame;
		s_EditPresentation.received_at = SDL_GetTicks();
	} else if (opcode == SVC_GRID_EDIT_CURSOR_POLICY) {
		identity.room_id = netbufReadU8(payload);
		identity.generation = netbufReadU64(payload);
		const u8 allowed = netbufReadU8(payload);
		u64 token = 0;
		if (netbufReadLeft(payload) == GRID_PLAYTEST_CURSOR_LAYOUT_PREFIX_SIZE) {
			if (!(s_EditReviewCapabilities & GRID_EDIT_REVIEW_CAP_APPLY)
					|| netbufReadU8(payload) != GRID_PLAYTEST_CURSOR_LAYOUT_VERSION) return;
			token = netbufReadU64(payload);
			if (!token) return;
		}
		if (payload->error || allowed > 1 || netbufReadLeft(payload) || !editSameIdentity(&identity)) return;
		if (!allowed || token != s_EditPresentation.cursor_token) s_EditPresentation.cursor_sent = 0;
		s_EditPresentation.cursor_token = token;
		s_EditPresentation.cursor_granted = allowed;
	} else {
		s32 players, creators;
		u8 visible;
		f32 x, z;
		if (!g_NetStageEpoch || s_EditPlaytest.phase != EDIT_PLAYTEST_ACTIVE
				|| s_EditPlaytest.stage_epoch != g_NetStageEpoch || s_EditLayout.phase == EDIT_LAYOUT_OUTCOME
				|| !gridEditRoomGetVisibility(s_EditRoom, &players, &creators) || !players) return;
		const u64 expected = editHostCursorToken();
		u64 token = 0;
		const s32 decoded = expected
			? gridEditPlaytestLayoutCursorRead(payload, &identity, &token, &visible, &x, &z)
			: gridEditPlaytestCursorRead(payload, &identity, &visible, &x, &z);
		if (!decoded || token != expected || !editSameIdentity(&identity)) return;
		s_EditPresentation.cursor_visible = visible;
		s_EditPresentation.cursor_x = x;
		s_EditPresentation.cursor_z = z;
		s_EditPresentation.cursor_received_at = SDL_GetTicks();
	}
}

s32 netGridEditReceive(const ENetEvent *event, struct netclient *client, s32 is_client)
{
	const u8 opcode = event->packet->data[0];
	if (opcode == SVC_GRID_EDIT_REVIEW_CHUNK) {
		if (is_client && event->channelID == NETCHAN_TRANSFER
				&& (event->packet->flags & ENET_PACKET_FLAG_RELIABLE)
				&& client->peer == event->peer && client == editBoundPeer()
				&& event->packet->dataLength > 1 && event->packet->dataLength <= 1 + GRID_EDIT_REVIEW_CHUNK_WIRE_MAX) {
			struct netbuf payload;
			netbufStartReadData(&payload, event->packet->data + 1, (u32)event->packet->dataLength - 1);
			editReceiveReviewArenaChunk(client, &payload);
		}
		return 1;
	}
	const u8 control_opcode = is_client ? SVC_GRID_EDIT_CONTROL : CLC_GRID_EDIT_CONTROL;
	const u8 edit_opcode = is_client ? SVC_GRID_EDIT_DELTA : CLC_GRID_EDIT_COMMAND;
	const u8 snapshot_opcode = is_client ? SVC_GRID_EDIT_SNAPSHOT : CLC_GRID_EDIT_SNAPSHOT;
	const s32 presentation = is_client
		? opcode == SVC_GRID_EDIT_VIEW || opcode == SVC_GRID_EDIT_CURSOR_POLICY
		: opcode == CLC_GRID_EDIT_CURSOR;
	const s32 focus = !is_client && opcode == CLC_GRID_EDIT_FOCUS;
	if (opcode != control_opcode && opcode != edit_opcode && opcode != snapshot_opcode && !presentation && !focus) return 0;
	const size_t maximum = focus ? GRID_EDIT_FOCUS_WIRE_MAX : presentation ? GRID_PLAYTEST_WORLD_VIEW_WIRE_MAX
		: opcode == snapshot_opcode ? GRID_EDIT_SNAPSHOT_CHUNK_WIRE_MAX
		: opcode == edit_opcode ? (is_client ? GRID_EDIT_DELTA_WIRE_MAX : GRID_PLAYTEST_TARGET_COMMAND_WIRE_MAX)
		: GRID_EDIT_ROOM_CONTROL_WIRE_MAX;
	if (smokeHarnessIsActive() && sysArgCheck("--debug-grid-private-draft")) {
		sysLogPrintf(LOG_NOTE, "NET.GRID.EDIT.WIRE: received opcode=0x%02x", opcode);
	}
	if (event->channelID != NETCHAN_CONTROL || !(event->packet->flags & ENET_PACKET_FLAG_RELIABLE)
			|| client->peer != event->peer || !editLobbyPeer(client, client->room_id)) return 1;
	const s32 resume_delta = is_client && opcode == SVC_GRID_EDIT_DELTA && s_EditRoom && client == editBoundPeer()
		&& (s_EditResume.phase == EDIT_RESUME_RECEIVING || s_EditResume.phase == EDIT_RESUME_WAITING_GRANT);
	if (event->packet->dataLength <= 1 || event->packet->dataLength > 1 + maximum) {
		if (resume_delta) editEndCreator(client, "Shared Grid received a malformed resume edit.");
		return 1;
	}
	struct netbuf payload;
	netbufStartReadData(&payload, event->packet->data + 1, (u32)event->packet->dataLength - 1);
	if (opcode == snapshot_opcode) {
		editReceiveSnapshot(client, is_client, &payload);
		return 1;
	}
	if (opcode == control_opcode) {
		grid_edit_room_control_t decoded;
		if (gridEditRoomControlRead(&payload, &decoded)) editReceiveControl(client, is_client, &decoded);
		return 1;
	}
	if (!s_EditRoom || client != editBoundPeer()) return 1;
	if (presentation) {
		editReceivePresentation(opcode, &payload);
		return 1;
	}
	grid_edit_wire_identity_t identity;
	if (focus) {
		grid_edit_resource_t resource;
		s32 has_resource;
		if (gridEditFocusResourceRead(&payload, &identity, &resource, &has_resource) && editSameIdentity(&identity)) {
			gridEditRoomHostFocusResource(s_EditRoom, s_EditGuestActor, s_EditConnection,
				has_resource ? &resource : NULL, SDL_GetTicks64());
		}
		return 1;
	}
	if (is_client) {
		grid_edit_delta_t delta;
		if (!gridEditDeltaRead(&payload, &identity, &delta)) {
			if (resume_delta) editEndCreator(client, "Shared Grid received a malformed resume edit.");
			return 1;
		}
		if (!editSameIdentity(&identity)) return 1;
		if (resume_delta && s_EditResume.phase != EDIT_RESUME_WAITING_GRANT) {
			editEndCreator(client, "Shared Grid received a resume edit before verifying its snapshot.");
			return 1;
		}
		if (gridEditRoomGuestApplyDelta(s_EditRoom, &identity, s_EditConnection, &delta,
				s_EditError, sizeof(s_EditError))) {
			sysLogPrintf(LOG_NOTE, "NET.GRID.EDIT: observed revision=%llu author=%llu",
				(unsigned long long)delta.revision, (unsigned long long)delta.author_id);
		} else if (resume_delta) {
			editEndCreator(client, "The Shared Grid resume edit did not match the verified draft.");
		} else {
			editCloseBound();
			editError("Shared Grid ended because the verified draft could not advance.");
		}
	} else {
		grid_edit_command_t command;
		grid_playtest_target_t target;
		const s32 stamped = payload.data[0] == GRID_PLAYTEST_TARGET_COMMAND_MARKER;
		const s32 decoded = stamped ? gridEditPlaytestTargetCommandRead(&payload, &identity, &target, &command)
			: gridEditCommandRead(&payload, &identity, &command);
		if (decoded && editSameIdentity(&identity)) {
			editApplyHost(client, s_EditGuestActor, s_EditConnection, &command, stamped ? &target : NULL);
		}
	}
	return 1;
}
