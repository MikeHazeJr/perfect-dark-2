#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "types.h"
#include "platform.h"
#include "data.h"
#include "bss.h"
#include "lib/main.h"
#include "lib/mtx.h"
#include "lib/model.h"
#include "game/mplayer/mplayer.h"
#include "game/mplayer/participant.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/prop.h"
#include "game/propobj.h"
#include "game/player.h"
#include "game/playermgr.h"
#include "game/bondgun.h"
#include "game/game_0b0fd0.h"
#include "game/inv.h"
#include "game/menu.h"
#include "game/setup.h"
#include "game/setuputils.h"
#include "game/stagetable.h"
#include "game/modelmgr.h"
#include "game/propsnd.h"
#include "game/bot.h"
#include "game/title.h"
#include "game/lv.h"
#include "game/pdmode.h"
#include "game/objectives.h"
#include "game/hudmsg.h"
#include "game/lang.h"
#include "system.h"
#include "crashbreadcrumb.h"
#include "romdata.h"
#include "scene_transition.h"
#include "lib/vi.h"
#include "fs.h"
#include "console.h"
#include "net/net.h"
#include "net/netenet.h"
#include "net/netbuf.h"
#include "net/netmsg.h"
#include "net/netlobby.h"
#include "net/netdistrib.h"
#include "net/netmanifest.h"
#include "net/net_manifest_type.h"
#include "net/matchsetup.h"
#include "net/net_client_settings_wire.h"
#include "net/net_cutscene_authority.h"
#include "net/net_reconnect.h"
#include "net/sessioncatalog.h"
#include "room.h"
#include "scenario_save.h"
#include "scenario_source_runtime.h"
#include "assetcatalog.h"
#include "asset_runtime.h"
#include "player_identity.h"
#include "audio.h"
#include "modmusic.h"
#if !defined(PD_SERVER)
#include "modelcatalog.h"
#include "game/mplayer/scenarios.h"
#include "input.h"
#endif
#include "identity.h"
#include "utils.h"
#include "server_admin.h"
#include "server_bans.h"
#include "social.h"
#if !defined(PD_SERVER)
#include "pdgui.h"
#include "pdgui_hud.h"
#include "pdgui_toast.h"
#include "inputctx.h"
#include "menupool.h"
#include "scene.h"
#endif
#include <SDL.h>
#include <stdarg.h>

/* Desync detection and resync constants */
#define NET_DESYNC_THRESHOLD   3   // consecutive desyncs before requesting resync
#define NET_RESYNC_COOLDOWN    300 // minimum frames between resync requests (~5 sec at 60fps)

/* Phase E: Ready gate — timeout before forcing SVC_STAGE_START (30s at 60fps) */
#define READY_GATE_TIMEOUT_TICKS 1800

static s32 netmsgCutsceneAuthorityBeginMatch(u8 room_id,
	const net_cutscene_participant_t *participants, size_t participant_count);
static u32 netmsgReconnectCutsceneAuthorityWrite(struct netbuf *dst);

typedef struct netmsg_stage_start_authority_candidate_s {
	bool valid;
	net_cutscene_participant_t participants[MAX_PLAYERS];
	size_t participant_count;
} netmsg_stage_start_authority_candidate_t;

static netmsg_stage_start_authority_candidate_t
	s_StageStartAuthorityCandidate;

/* Chat rate limiter: max 5 messages per 2-second window per client.
 * Keyed by client index (0..NET_MAX_CLIENTS).
 * CHAT_MSG_MAX_LEN caps per-message size so rate-limited bursts stay small
 * (255B max vs 64KB max without cap — limits amplification factor). */
#define CHAT_RATE_MAX_MSGS   5
#define CHAT_RATE_WINDOW_MS  2000u
#define CHAT_MSG_MAX_LEN     255u

struct chatrate {
	u32 timestamps[CHAT_RATE_MAX_MSGS]; /* circular ring of send times (ms) */
	u32 head;                           /* next slot to overwrite */
};
static struct chatrate s_ChatRate[NET_MAX_CLIENTS + 1];

void netmsgChatRateReset(u32 idx)
{
	if (idx <= (u32)NET_MAX_CLIENTS) {
		memset(&s_ChatRate[idx], 0, sizeof(s_ChatRate[idx]));
	}
}

/* SEC-13: Room mutation rate limiter.
 * One CLC_ROOM_CREATE/JOIN/LEAVE per client per second. Without this a single
 * malicious client can churn join/leave to force the server to walk every
 * room slot and re-broadcast SVC_ROOM_LIST on every other client. */
#define ROOM_RATE_INTERVAL_MS  1000u
static u32 s_RoomMutationLast[NET_MAX_CLIENTS + 1];

static int netmsgRoomRateAllow(struct netclient *cl)
{
	u32 idx = (u32)(cl - g_NetClients);
	if (idx > (u32)NET_MAX_CLIENTS) return 0;
	u32 now = SDL_GetTicks();
	if (s_RoomMutationLast[idx] != 0 && (now - s_RoomMutationLast[idx]) < ROOM_RATE_INTERVAL_MS) {
		return 0;
	}
	s_RoomMutationLast[idx] = now;
	return 1;
}

void netmsgRoomMutationRateReset(u32 idx)
{
	if (idx <= (u32)NET_MAX_CLIENTS) {
		s_RoomMutationLast[idx] = 0;
	}
}

/* S-1 / SEC: ADMIN_SUB_AUTH brute-force mitigation — per-client + hashed-IP
 * buckets (same sliding window as tasks-current: 3 failed auths / 60 s →
 * ADMIN_RESP_RATE_LIMIT + disconnect).  Logs for bad-token spam are capped
 * (one WARNING / 10 s per client).  Optional temp IP ban is intentionally not
 * enabled by default (NAT / shared-IP false positives). */
#define ADMIN_AUTH_FAIL_MAX_ATTEMPTS 3
#define ADMIN_AUTH_FAIL_WINDOW_MS    60000u
#define ADMIN_AUTH_IP_BUCKETS        16
#define ADMIN_AUTH_FAIL_LOG_INTERVAL_MS 10000u

struct adminauthfail {
	u32 t[8];
};

static struct adminauthfail s_AdminAuthFailClient[NET_MAX_CLIENTS + 1];
static struct adminauthfail s_AdminAuthIp[ADMIN_AUTH_IP_BUCKETS];
static u32 s_AdminAuthIpHash[ADMIN_AUTH_IP_BUCKETS];
static u32 s_AdminAuthFailLastLogMs[NET_MAX_CLIENTS + 1];

static u32 adminauth_ip_hash_str(const char *ip)
{
	u32 h = 2166136261u;
	if (!ip) {
		return h;
	}
	for (; *ip; ip++) {
		h ^= (u32)(u8)*ip;
		h *= 16777619u;
	}
	return h;
}

static struct adminauthfail *adminauth_ip_bucket(u32 hash)
{
	u32 i = hash % ADMIN_AUTH_IP_BUCKETS;
	for (u32 probe = 0; probe < ADMIN_AUTH_IP_BUCKETS; probe++) {
		u32 slot = (i + probe) % ADMIN_AUTH_IP_BUCKETS;
		if (s_AdminAuthIpHash[slot] == 0 || s_AdminAuthIpHash[slot] == hash) {
			s_AdminAuthIpHash[slot] = hash;
			return &s_AdminAuthIp[slot];
		}
	}
	u32 slot = hash % ADMIN_AUTH_IP_BUCKETS;
	s_AdminAuthIpHash[slot] = hash;
	return &s_AdminAuthIp[slot];
}

/* Returns 1 if this failure triggers lockout (too many attempts in window). */
static int adminauthfail_on_failure(struct adminauthfail *f, u32 now)
{
	u32 keep[8];
	u32 k = 0;
	for (u32 i = 0; i < 8; i++) {
		if (f->t[i] && now - f->t[i] < ADMIN_AUTH_FAIL_WINDOW_MS) {
			keep[k++] = f->t[i];
		}
	}
	if (k < 8) {
		keep[k++] = now;
	} else {
		keep[7] = now;
	}
	memset(f->t, 0, sizeof(f->t));
	memcpy(f->t, keep, k * sizeof(u32));
	return (k >= ADMIN_AUTH_FAIL_MAX_ATTEMPTS) ? 1 : 0;
}

void netmsgAdminAuthRateReset(u32 idx)
{
	if (idx <= (u32)NET_MAX_CLIENTS) {
		memset(&s_AdminAuthFailClient[idx], 0, sizeof(s_AdminAuthFailClient[idx]));
		s_AdminAuthFailLastLogMs[idx] = 0;
	}
}

/* Extract host IP (no brackets) from netFormatClientAddr for rate limiting. */
/* Append vsnprintf output to payload; sets *trunc if the line did not fully fit. */
static void netmsgAdminPayloadVfmt(char *payload, size_t cap, int *off, int *trunc,
                                    const char *fmt, ...)
{
	if (*trunc) {
		return;
	}
	if (*off < 0 || (size_t)*off >= cap) {
		*trunc = 1;
		return;
	}
	size_t rem = cap - (size_t)*off;
	if (rem <= 1) {
		*trunc = 1;
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(payload + *off, rem, fmt, ap);
	va_end(ap);
	if (n < 0) {
		*trunc = 1;
		return;
	}
	if ((size_t)n >= rem) {
		*trunc = 1;
		*off = (int)cap - 1;
		payload[*off] = '\0';
		return;
	}
	*off += n;
}

static void netmsgAdminExtractPeerIp(struct netclient *cl, char *out, size_t outsz)
{
	out[0] = '\0';
	if (!cl || !cl->peer || outsz == 0) {
		return;
	}
	const char *fmt = netFormatClientAddr(cl);
	if (!fmt) {
		return;
	}
	const char *colon = strrchr(fmt, ':');
	const char *lbracket = strchr(fmt, '[');
	if (lbracket && colon > lbracket) {
		const char *rbracket = strchr(fmt, ']');
		if (rbracket && rbracket > lbracket + 1) {
			size_t n = (size_t)(rbracket - (lbracket + 1));
			if (n >= outsz) {
				n = outsz - 1;
			}
			memcpy(out, lbracket + 1, n);
			out[n] = '\0';
			return;
		}
	}
	if (colon) {
		size_t n = (size_t)(colon - fmt);
		if (n >= outsz) {
			n = outsz - 1;
		}
		memcpy(out, fmt, n);
		out[n] = '\0';
		return;
	}
	strncpy(out, fmt, outsz - 1);
	out[outsz - 1] = '\0';
}

/* SEC-13: Room list broadcast coalescing.
 * Mark dirty here, flush once at end of frame. Stops a burst of room
 * mutations from generating one full SVC_ROOM_LIST per mutation. */
static u8 s_RoomListDirty = 0;

void netRoomListMarkDirty(void)
{
	s_RoomListDirty = 1;
}

void netRoomListFlushIfDirty(void)
{
	if (g_NetMode == NETMODE_SERVER && s_RoomListDirty) {
		s_RoomListDirty = 0;
		netBroadcastRoomList();
	}
}

/* Phase E/F: Server-side per-client readiness tracker.
 * Active while the room is in ROOM_STATE_PREPARING.
 * Bit i in each mask corresponds to g_NetClients[i]. */
static struct {
	s32 active;              /* 1 while waiting for CLC_MANIFEST_STATUS responses */
	u32 expected_mask;       /* clients that were in CLSTATE_LOBBY when manifest broadcast */
	u32 ready_mask;          /* clients that replied MANIFEST_STATUS_READY */
	u32 declined_mask;       /* clients that replied MANIFEST_STATUS_DECLINE */
	u32 deadline_ticks;      /* g_NetTick value at which timeout fires */
	u8  stagenum;            /* saved for mainChangeToStage() when gate fires */
	u8  total_count;         /* popcount(expected_mask) */
	/* L2-1: game mode + difficulty for co-op/anti gate completion */
	u8  game_mode;           /* NETGAMEMODE_MP / COOP / ANTI */
	u8  difficulty;          /* co-op difficulty (unused for MP) */
	/* Phase F: countdown */
	s32 countdown_active;    /* 1 during the 3-second pre-launch countdown */
	u32 countdown_next_tick; /* g_NetTick at which to decrement + re-broadcast */
	u8  countdown_secs;      /* current countdown value (3→2→1→0) */
	/* R-3: which room this ready gate belongs to */
	u8  room_id;             /* 0xFF = global (no room), else room slot index */
	/* Exact roster frozen by CLC_LOBBY_START prepare.  DECLINE may compact
	 * this roster once, immediately before launch, without consulting mutable
	 * lounge/global topology. */
	u8  roster_count;
	s32 roster_compacted;
	u8  roster_client_ids[MAX_PLAYERS];
	struct mpplayerconfig roster_configs[MAX_PLAYERS];
	struct matchslot roster_slots[MAX_PLAYERS];
} s_ReadyGate;

static void readyGateAbort(const char *canceller_name);

static s32 readyGatePreparingClientIndex(const struct netclient *client)
{
	if (!s_ReadyGate.active || client == NULL) {
		return -1;
	}

	for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
		const u32 bit = 1u << i;

		if (&g_NetClients[i] == client
				&& (s_ReadyGate.expected_mask & bit)
				&& !(s_ReadyGate.declined_mask & bit)
				&& client->room_id == s_ReadyGate.room_id
				&& client->state == CLSTATE_PREPARING) {
			return i;
		}
	}

	return -1;
}

typedef struct lobby_start_client_snapshot_t {
	u32 state;
	u8 playernum;
	u8 settings_handicap;
	struct mpplayerconfig *config;
	struct _ENetPeer *peer;
} lobby_start_client_snapshot_t;

/* One server-side match-start transaction may be active while the manifest
 * gate/countdown runs.  The snapshot owns the pre-start manifest and a deep
 * copy of the participant pool; commit success releases them, while any
 * cancellation or launch rejection restores every locally published input. */
static struct {
	s32 active;
	u8 room_id;
	room_state_t room_state;
	u32 room_state_enter_tick;
	u8 net_game_mode;
	u8 counterop_client_id;
	u8 match_room_id;
	s32 mp_weapon_set_num;
	u64 rng_seed;
	u64 rng2_seed;
	s32 bond_playernum;
	s32 coop_playernum;
	s32 anti_playernum;
	struct mpsetup setup;
	struct matchconfig match;
	struct missionconfig mission;
	struct lobbysettings lobby_settings;
	struct mpplayerconfig players[MAX_MPPLAYERCONFIGS];
	struct mpbotconfig bots[MAX_BOTS];
	lobby_start_client_snapshot_t clients[NET_MAX_CLIENTS];
	MpParticipant *participant_slots;
	s32 participant_count;
	s32 participant_capacity;
	match_manifest_t prior_manifest;
	session_catalog_t prior_catalog;
} s_LobbyStartTxn;

extern s32 g_MpWeaponSetNum;

static s32 lobbyStartTransactionBegin(u8 room_id)
{
	hub_room_t *room;
	size_t participant_bytes;

	if (s_LobbyStartTxn.active || s_ReadyGate.active
			|| !g_MpParticipants.slots
			|| g_MpParticipants.capacity < MAX_MPCHRS) {
		return 0;
	}
	memset(&s_LobbyStartTxn, 0, sizeof(s_LobbyStartTxn));
	participant_bytes = (size_t)g_MpParticipants.capacity
		* sizeof(*g_MpParticipants.slots);
	s_LobbyStartTxn.participant_slots =
		(MpParticipant *)malloc(participant_bytes);
	if (!s_LobbyStartTxn.participant_slots) {
		return 0;
	}
	memcpy(s_LobbyStartTxn.participant_slots, g_MpParticipants.slots,
		participant_bytes);
	s_LobbyStartTxn.participant_count = g_MpParticipants.count;
	s_LobbyStartTxn.participant_capacity = g_MpParticipants.capacity;
	s_LobbyStartTxn.room_id = room_id;
	s_LobbyStartTxn.net_game_mode = g_NetGameMode;
	s_LobbyStartTxn.counterop_client_id = g_NetCounterOpClientId;
	s_LobbyStartTxn.match_room_id = g_NetMatchRoomId;
	s_LobbyStartTxn.mp_weapon_set_num = g_MpWeaponSetNum;
	s_LobbyStartTxn.rng_seed = g_RngSeed;
	s_LobbyStartTxn.rng2_seed = g_Rng2Seed;
	s_LobbyStartTxn.bond_playernum = g_Vars.bondplayernum;
	s_LobbyStartTxn.coop_playernum = g_Vars.coopplayernum;
	s_LobbyStartTxn.anti_playernum = g_Vars.antiplayernum;
	s_LobbyStartTxn.setup = g_MpSetup;
	s_LobbyStartTxn.match = g_MatchConfig;
	s_LobbyStartTxn.mission = g_MissionConfig;
	s_LobbyStartTxn.lobby_settings = g_Lobby.settings;
	memcpy(s_LobbyStartTxn.players, g_PlayerConfigsArray,
		sizeof(s_LobbyStartTxn.players));
	memcpy(s_LobbyStartTxn.bots, g_BotConfigsArray,
		sizeof(s_LobbyStartTxn.bots));
	memcpy(&s_LobbyStartTxn.prior_catalog, &g_SessionCatalog,
		sizeof(s_LobbyStartTxn.prior_catalog));
	for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
		s_LobbyStartTxn.clients[i].state = g_NetClients[i].state;
		s_LobbyStartTxn.clients[i].playernum = g_NetClients[i].playernum;
		s_LobbyStartTxn.clients[i].settings_handicap =
			g_NetClients[i].settings.handicap;
		s_LobbyStartTxn.clients[i].config = g_NetClients[i].config;
		s_LobbyStartTxn.clients[i].peer = g_NetClients[i].peer;
	}
	room = room_id == 0xFF ? NULL : roomGetById(room_id);
	if (room) {
		s_LobbyStartTxn.room_state = room->state;
		s_LobbyStartTxn.room_state_enter_tick = room->state_enter_tick;
	}
	/* Move ownership. The candidate manifest is installed immediately after
	 * this returns; rollback can therefore free the candidate and move this
	 * exact prior value back without an allocation. */
	s_LobbyStartTxn.prior_manifest = g_ServerManifest;
	memset(&g_ServerManifest, 0, sizeof(g_ServerManifest));
	s_LobbyStartTxn.active = 1;
	return 1;
}

static void lobbyStartTransactionRelease(void)
{
	if (!s_LobbyStartTxn.active) {
		return;
	}
	manifestFree(&s_LobbyStartTxn.prior_manifest);
	free(s_LobbyStartTxn.participant_slots);
	s_LobbyStartTxn.participant_slots = NULL;
	s_LobbyStartTxn.active = 0;
	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.COMMIT lobby-start room=%u rollback_snapshot_released=1",
		(unsigned)s_LobbyStartTxn.room_id);
}

static void lobbyStartTransactionRollback(const char *reason)
{
	hub_room_t *room;

	if (!s_LobbyStartTxn.active) {
		return;
	}
	g_NetGameMode = s_LobbyStartTxn.net_game_mode;
	g_NetCounterOpClientId = s_LobbyStartTxn.counterop_client_id;
	g_NetMatchRoomId = s_LobbyStartTxn.match_room_id;
	g_MpWeaponSetNum = s_LobbyStartTxn.mp_weapon_set_num;
	g_RngSeed = s_LobbyStartTxn.rng_seed;
	g_Rng2Seed = s_LobbyStartTxn.rng2_seed;
	g_Vars.bondplayernum = s_LobbyStartTxn.bond_playernum;
	g_Vars.coopplayernum = s_LobbyStartTxn.coop_playernum;
	g_Vars.antiplayernum = s_LobbyStartTxn.anti_playernum;
	g_MpSetup = s_LobbyStartTxn.setup;
	g_MatchConfig = s_LobbyStartTxn.match;
	g_MissionConfig = s_LobbyStartTxn.mission;
	g_Lobby.settings = s_LobbyStartTxn.lobby_settings;
	memcpy(g_PlayerConfigsArray, s_LobbyStartTxn.players,
		sizeof(s_LobbyStartTxn.players));
	memcpy(g_BotConfigsArray, s_LobbyStartTxn.bots,
		sizeof(s_LobbyStartTxn.bots));
	memcpy(&g_SessionCatalog, &s_LobbyStartTxn.prior_catalog,
		sizeof(g_SessionCatalog));
	for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
		/* A disconnect/peer replacement is newer truth than the snapshot. */
		if (g_NetClients[i].state == CLSTATE_DISCONNECTED
				|| g_NetClients[i].peer != s_LobbyStartTxn.clients[i].peer) {
			continue;
		}
		g_NetClients[i].state = s_LobbyStartTxn.clients[i].state;
		g_NetClients[i].playernum = s_LobbyStartTxn.clients[i].playernum;
		g_NetClients[i].settings.handicap =
			s_LobbyStartTxn.clients[i].settings_handicap;
		g_NetClients[i].config = s_LobbyStartTxn.clients[i].config;
	}
	free(g_MpParticipants.slots);
	g_MpParticipants.slots = s_LobbyStartTxn.participant_slots;
	g_MpParticipants.count = s_LobbyStartTxn.participant_count;
	g_MpParticipants.capacity = s_LobbyStartTxn.participant_capacity;
	s_LobbyStartTxn.participant_slots = NULL;
	manifestFree(&g_ServerManifest);
	g_ServerManifest = s_LobbyStartTxn.prior_manifest;
	memset(&s_LobbyStartTxn.prior_manifest, 0,
		sizeof(s_LobbyStartTxn.prior_manifest));
	room = s_LobbyStartTxn.room_id == 0xFF
		? NULL : roomGetById(s_LobbyStartTxn.room_id);
	if (room) {
		room->state = s_LobbyStartTxn.room_state;
		room->state_enter_tick = s_LobbyStartTxn.room_state_enter_tick;
	}
	sysLogPrintf(LOG_ERROR,
		"PLAYER.INIT.ROLLBACK lobby-start room=%u reason=%s globals_restored=1",
		(unsigned)s_LobbyStartTxn.room_id,
		reason ? reason : "unspecified");
	s_LobbyStartTxn.active = 0;
}

/* Phase F: Client-side countdown display state.
 * Updated by netmsgSvcMatchCountdownRead().  UI reads this to display
 * "Match starting in N..." during MANIFEST_PHASE_LOADING. */
struct match_countdown_state g_MatchCountdownState;

/* Phase F: Client-side cancellation state.
 * Updated by netmsgSvcMatchCancelledRead(). UI reads this to display
 * "[Name] cancelled the match start". */
struct match_cancelled_state g_MatchCancelledState;

/* Desync tracking state (client-side, CHR and NPC only) */
static u32 g_NetChrDesyncCount = 0;
static u32 g_NetChrResyncLastReq = 0;
static u32 g_NetNpcDesyncCount = 0;
static u32 g_NetNpcResyncLastReq = 0;

/* Server-side resync request tracking (set by CLC_RESYNC_REQ, consumed by netEndFrame in net.c) */
u8 g_NetPendingResyncFlags = 0;

/* ========================================================================
 * Prop snapshot — event-driven dirty detection (server-side).
 * Replaces SVC_PROP_SYNC CRC polling. The server records what hidden/damage
 * state it last broadcast for each sync-prop and detects out-of-band
 * changes via netPropDirtyCheck(). Cheaper than XOR CRC: one comparison
 * per prop, no rolling hash, and only triggers SVC_PROP_RESYNC when
 * something actually changed.
 * ======================================================================== */

#define PROP_SNAP_MAX 128

typedef struct {
	u32 syncid;
	u32 hidden;
	s16 damage;
} PropStateSnap;

static PropStateSnap s_PropSnaps[PROP_SNAP_MAX];
static s32           s_PropSnapCount = 0;

void netPropSnapReset(void)
{
	s_PropSnapCount = 0;
}

static void netPropSnapUpdate(struct prop *prop)
{
	if (!prop || !prop->syncid || !prop->obj) return;
	for (s32 i = 0; i < s_PropSnapCount; i++) {
		if (s_PropSnaps[i].syncid == prop->syncid) {
			s_PropSnaps[i].hidden = prop->obj->hidden;
			s_PropSnaps[i].damage = prop->obj->damage;
			return;
		}
	}
	if (s_PropSnapCount < PROP_SNAP_MAX) {
		s_PropSnaps[s_PropSnapCount].syncid = prop->syncid;
		s_PropSnaps[s_PropSnapCount].hidden = prop->obj->hidden;
		s_PropSnaps[s_PropSnapCount].damage = prop->obj->damage;
		s_PropSnapCount++;
	}
}

int netPropDirtyCheck(void)
{
	int dirty = 0;
	struct prop *prop = g_Vars.activeprops;
	while (prop) {
		if (prop->syncid && prop->type == PROPTYPE_OBJ && prop->obj) {
			u8 objtype = prop->obj->type;
			if (objtype == OBJTYPE_AUTOGUN || objtype == OBJTYPE_DOOR   ||
			    objtype == OBJTYPE_LIFT    || objtype == OBJTYPE_HOVERPROP ||
			    objtype == OBJTYPE_HOVERBIKE || objtype == OBJTYPE_HOVERCAR ||
			    objtype == OBJTYPE_GLASS   || objtype == OBJTYPE_TINTEDGLASS) {
				s32 found = 0;
				for (s32 i = 0; i < s_PropSnapCount; i++) {
					if (s_PropSnaps[i].syncid == prop->syncid) {
						if (s_PropSnaps[i].hidden != prop->obj->hidden ||
						    s_PropSnaps[i].damage != prop->obj->damage) {
							sysLogPrintf(LOG_NOTE,
							    "NET.PROP: syncid=%u dirty hidden %u→%u damage %d→%d",
							    prop->syncid,
							    s_PropSnaps[i].hidden, prop->obj->hidden,
							    (s32)s_PropSnaps[i].damage, (s32)prop->obj->damage);
							s_PropSnaps[i].hidden = prop->obj->hidden;
							s_PropSnaps[i].damage = prop->obj->damage;
							dirty = 1;
						}
						found = 1;
						break;
					}
				}
				if (!found) {
					netPropSnapUpdate(prop);
					dirty = 1;
				}
			}
		}
		prop = prop->next;
	}
	return dirty;
}

/* Prop syncid -> prop* lookup map.
 * Initial syncids are deterministic 1-indexed prop-slot offsets. Runtime-created
 * props receive monotonic IDs, so IDs outside this fast table use the validated
 * linear fallback in netSyncIdLookup. */
#define NET_PROP_MAP_SIZE 2048
static struct prop *s_PropBySyncId[NET_PROP_MAP_SIZE];

typedef struct netmsg_reconnect_prop_receive_s {
	bool active;
	bool complete;
	u16 expected_count;
	u16 received_count;
	u32 first_dynamic_syncid;
	u32 inventory_expected_mask;
	u32 inventory_received_mask;
	u32 *expected_ids;
	u8 *received;
	u8 *lifecycle_by_slot;
	u32 *parent_ids_by_slot;
	struct netmsg_reconnect_prop_refs_s *refs_by_slot;
} netmsg_reconnect_prop_receive_t;

typedef struct netmsg_reconnect_prop_refs_s {
	u32 projectile_owner_id;
	u32 projectile_target_id;
	u32 projectile_pickupby_id;
	u32 detail_target_id;
	u32 weapon_dual_id;
	s16 attachment_mtx_index;
} netmsg_reconnect_prop_refs_t;

static netmsg_reconnect_prop_receive_t s_ReconnectPropReceive;

static void netmsgReconnectPropReceiveReset(void)
{
	free(s_ReconnectPropReceive.expected_ids);
	free(s_ReconnectPropReceive.received);
	free(s_ReconnectPropReceive.lifecycle_by_slot);
	free(s_ReconnectPropReceive.parent_ids_by_slot);
	free(s_ReconnectPropReceive.refs_by_slot);
	memset(&s_ReconnectPropReceive, 0, sizeof(s_ReconnectPropReceive));
}

void netSyncIdMapClear(void)
{
    memset(s_PropBySyncId, 0, sizeof(s_PropBySyncId));
	netmsgReconnectPropReceiveReset();
}

void netSyncIdMapSet(u32 syncid, struct prop *prop)
{
    if (syncid > 0 && syncid < NET_PROP_MAP_SIZE) {
        s_PropBySyncId[syncid] = prop;
    }
}

void netSyncIdMapRebuild(void)
{
    memset(s_PropBySyncId, 0, sizeof(s_PropBySyncId));
    for (s32 i = 0; i < g_Vars.maxprops; ++i) {
        const u32 sid = g_Vars.props[i].syncid;
        if (sid > 0 && sid < NET_PROP_MAP_SIZE) {
            s_PropBySyncId[sid] = &g_Vars.props[i];
        }
    }
}

static struct prop *netSyncIdLookup(u32 syncid)
{
	if (syncid == 0 || !g_Vars.props || g_Vars.maxprops <= 0) {
		return NULL;
	}
	if (syncid < NET_PROP_MAP_SIZE) {
		struct prop *prop = s_PropBySyncId[syncid];
		if (prop && prop->syncid == syncid) {
			return prop;
		}
	}
	for (s32 i = 0; i < g_Vars.maxprops; ++i) {
		if (g_Vars.props[i].syncid == syncid) {
			return &g_Vars.props[i];
		}
	}
	return NULL;
}

/* Client-side resync request tracking (set by SVC_CHR/PROP/NPC_SYNC handlers on desync, consumed by netEndFrame).
 * These flags CANNOT be written directly to g_NetMsgRel inside netStartFrame() recv handlers because
 * netStartFrame() resets g_NetMsgRel after the event loop — any write during dispatch is silently dropped.
 * The fix mirrors the server-side pattern: set a flag in the handler, write the message in netEndFrame. */
u8 g_NetPendingResyncReqFlags = 0;

typedef struct net_npc_snapshot_validation_s {
	bool pending;
	u32 stage_epoch;
	u32 tick;
	u16 count;
	u32 checksum;
} net_npc_snapshot_validation_t;

static net_npc_snapshot_validation_t s_NetNpcSnapshotValidation;

void netNpcReplicationReset(void)
{
	memset(&s_NetNpcSnapshotValidation, 0,
		sizeof(s_NetNpcSnapshotValidation));
	g_NetNpcDesyncCount = 0;
	g_NetNpcResyncLastReq = 0;
	g_NetPendingResyncReqFlags &= (u8)~NET_RESYNC_FLAG_NPCS;
}

/* utils */

static inline u32 netbufReadHidden(struct netbuf *buf)
{
	u32 hidden = netbufReadU32(buf);

	// swap owner player numbers to match server
	const u8 ownerclid = (hidden & 0xf0000000) >> 28;
	if (ownerclid < NET_MAX_CLIENTS) {
		const u8 ownerplayernum = g_NetClients[ownerclid].playernum;
		if (ownerplayernum < MAX_PLAYERS) {
			hidden = (hidden & 0x0fffffff) | (ownerplayernum << 28);
		}
	}

	return hidden;
}

static inline u32 netbufWriteHidden(struct netbuf *buf, u32 hidden)
{
	/* The high nibble is a runtime player number locally. Carry the stable
	 * client identity on the wire so reconnect and differently compacted
	 * player arrays resolve the same owner. */
	const u8 ownerplayernum = (u8)((hidden & 0xf0000000u) >> 28);
	u8 ownerclientid = NET_NULL_CLIENT;
	if (ownerplayernum < MAX_PLAYERS && g_Vars.players[ownerplayernum]
			&& g_Vars.players[ownerplayernum]->client
			&& g_Vars.players[ownerplayernum]->client->id < NET_MAX_CLIENTS) {
		ownerclientid = (u8)g_Vars.players[ownerplayernum]->client->id;
	} else if (g_NetMode == NETMODE_SERVER) {
		for (u8 i = 0; i < NET_MAX_CLIENTS; ++i) {
			struct netpreservedplayer *preserved =
				netServerFindPreservedByClientId(i);
			if (preserved && preserved->active
					&& preserved->playernum == ownerplayernum) {
				ownerclientid = preserved->client_id;
				break;
			}
		}
	}
	if (ownerclientid < NET_MAX_CLIENTS) {
		hidden = (hidden & 0x0fffffffu)
			| ((u32)ownerclientid << 28);
	}
	return netbufWriteU32(buf, hidden);
}

static inline u32 netbufWriteRooms(struct netbuf *buf, const s16 *rooms, const s32 num)
{
	for (s32 i = 0; i < num; ++i) {
		netbufWriteS16(buf, rooms[i]);
		if (rooms[i] < 0) {
			break;
		}
	}
	return buf->error;
}

static inline u32 netbufReadRooms(struct netbuf *buf, s16 *rooms, const s32 num)
{
	for (s32 i = 0; i < num; ++i) {
		rooms[i] = netbufReadS16(buf);
		if (rooms[i] < 0) {
			break;
		}
	}
	return buf->error;
}

static inline u32 netbufWriteGset(struct netbuf *buf, const struct gset *gset)
{
	netbufWriteU8(buf, gset->weaponnum);
	netbufWriteU8(buf, gset->unk0639);
	netbufWriteU8(buf, gset->unk063a);
	netbufWriteU8(buf, gset->weaponfunc);
	return buf->error;
}

static inline u32 netbufReadGset(struct netbuf *buf, struct gset *gset)
{
	gset->weaponnum = netbufReadU8(buf);
	gset->unk0639 = netbufReadU8(buf);
	gset->unk063a = netbufReadU8(buf);
	gset->weaponfunc = netbufReadU8(buf);
	return buf->error;
}

static inline u32 netbufWritePlayerMove(struct netbuf *buf, const struct netplayermove *in)
{
	netbufWriteU32(buf, in->tick);
	netbufWriteU32(buf, in->ucmd);
	netbufWriteF32(buf, in->leanofs);
	netbufWriteF32(buf, in->crouchofs);
	netbufWriteF32(buf, in->movespeed[0]);
	netbufWriteF32(buf, in->movespeed[1]);
	netbufWriteF32(buf, in->angles[0]);
	netbufWriteF32(buf, in->angles[1]);
	netbufWriteF32(buf, in->crosspos[0]);
	netbufWriteF32(buf, in->crosspos[1]);
	netbufWriteS8(buf, in->weaponnum);
	netbufWriteCoord(buf, &in->pos);
	if (in->ucmd & UCMD_AIMMODE) {
		netbufWriteF32(buf, in->zoomfov);
	}
	return buf->error;
}

static inline u32 netbufReadPlayerMove(struct netbuf *buf, struct netplayermove *in)
{
	in->tick = netbufReadU32(buf);
	in->ucmd = netbufReadU32(buf);
	in->leanofs = netbufReadF32(buf);
	in->crouchofs = netbufReadF32(buf);
	in->movespeed[0] = netbufReadF32(buf);
	in->movespeed[1] = netbufReadF32(buf);
	in->angles[0] = netbufReadF32(buf);
	in->angles[1] = netbufReadF32(buf);
	in->crosspos[0] = netbufReadF32(buf);
	in->crosspos[1] = netbufReadF32(buf);
	in->weaponnum = netbufReadS8(buf);
	/* M-6: Clamp weaponnum to valid range to prevent OOB from malicious packets. */
	if (in->weaponnum < WEAPON_NONE || in->weaponnum >= WEAPON_CUSTOM_END) {
		in->weaponnum = WEAPON_UNARMED;
	}
	netbufReadCoord(buf, &in->pos);
	if (in->ucmd & UCMD_AIMMODE) {
		in->zoomfov = netbufReadF32(buf);
	} else {
		in->zoomfov = 0.f;
	}
	return buf->error;
}

static bool netmsgClientCanSelectWeapon(struct netclient *cl, s32 weaponnum, bool dualwield)
{
	if (!cl || cl->playernum >= MAX_PLAYERS || !cl->player) {
		return false;
	}

	if (weaponnum < WEAPON_UNARMED || weaponnum >= WEAPON_CUSTOM_END) {
		return false;
	}

	const s32 prevplayernum = g_Vars.currentplayernum;
	setCurrentPlayerNum(cl->playernum);
	const bool has_single = invHasSingleWeaponIncAllGuns(weaponnum);
	const bool has_double = !dualwield || invHasDoubleWeaponIncAllGuns(weaponnum, weaponnum);
	setCurrentPlayerNum(prevplayernum);

	return has_single && has_double;
}

static inline u32 netbufWritePropPtr(struct netbuf *buf, const struct prop *prop)
{
	netbufWriteU32(buf, prop ? prop->syncid : 0);
	return buf->error;
}

static inline struct prop *netbufReadPropPtr(struct netbuf *buf)
{
	const u32 syncid = netbufReadU32(buf);
	if (syncid == 0) {
		return NULL;
	}
	struct prop *prop = netSyncIdLookup(syncid);
	if (prop) {
		return prop;
	}

	sysLogPrintf(LOG_WARNING, "NET: prop with syncid %u does not exist", syncid);
	return NULL;
}

static inline s32 propRoomsEqual(const RoomNum *ra, const RoomNum *rb)
{
	for (s32 i = 0; i < 8; ++i) {
		if (ra[i] != rb[i]) {
			return 0;
		}
		if (ra[i] == -1) {
			break;
		}
	}
	return 1;
}

/* client -> server */

/* v57: one in-memory, endpoint-scoped reconnect credential. The endpoint is
 * captured after address resolution and the server-assigned client id is only
 * a slot hint; the 128-bit cookie remains the authenticator. Nothing here is
 * persisted, so a process restart cannot replay a credential. */
static struct {
	u8 cookie[NET_AUTH_COOKIE_LEN];
	ENetAddress endpoint;
	u8 client_id;
	bool valid;
	ENetAddress active_endpoint;
	bool active_endpoint_valid;
} s_AuthSession;

static s32 netmsgClcAuthEndpointEqual(const ENetAddress *a,
		const ENetAddress *b)
{
	return a && b && a->port == b->port
		&& memcmp(&a->ipv6, &b->ipv6, sizeof(a->ipv6)) == 0;
}

u32 netmsgClcAuthPrepareConnect(const ENetAddress *endpoint, u16 protocol)
{
	s32 reconnect;

	if (!endpoint || protocol == 0) {
		return 0;
	}
	s_AuthSession.active_endpoint = *endpoint;
	s_AuthSession.active_endpoint_valid = true;
	reconnect = s_AuthSession.valid
		&& netmsgClcAuthEndpointEqual(endpoint, &s_AuthSession.endpoint);
	return netReconnectConnectDataEncode(protocol, reconnect,
		s_AuthSession.client_id, NET_NULL_CLIENT);
}

s32 netmsgClcAuthReconnectAvailable(const ENetAddress *endpoint)
{
	return s_AuthSession.valid && endpoint
		&& netmsgClcAuthEndpointEqual(endpoint, &s_AuthSession.endpoint);
}

void netmsgClcAuthClearCookie(void)
{
	memset(&s_AuthSession, 0, sizeof(s_AuthSession));
}

static void netmsgClcAuthStoreCookie(
		const u8 cookie[NET_AUTH_COOKIE_LEN], u8 client_id)
{
	u8 nonzero = 0;

	if (!cookie || client_id >= NET_NULL_CLIENT
			|| !s_AuthSession.active_endpoint_valid) {
		netmsgClcAuthClearCookie();
		return;
	}
	for (s32 i = 0; i < NET_AUTH_COOKIE_LEN; i++) {
		nonzero |= cookie[i];
	}
	if (!nonzero) {
		netmsgClcAuthClearCookie();
		return;
	}
	memcpy(s_AuthSession.cookie, cookie, sizeof(s_AuthSession.cookie));
	s_AuthSession.endpoint = s_AuthSession.active_endpoint;
	s_AuthSession.client_id = client_id;
	s_AuthSession.valid = true;
}

u32 netmsgClcAuthWrite(struct netbuf *dst)
{
	const char *modDir = fsGetModDir();
	bool reconnecting;
	if (!modDir) {
		modDir = "";
	}

	/* AUDIT-24-L4 (2026-04-25): defensive NULL guard on g_NetLocalClient.
	 * In normal operation this function is only invoked from
	 * `netClientEvConnect` which is registered as a client-side connect
	 * handler, so `g_NetLocalClient` is always set when we get here.
	 * The function body still compiles into pd-server (no PD_SERVER
	 * guard around the function); a future refactor that wires this
	 * write into a generic dispatch could expose the unconditional
	 * deref.  The guard converts the latent footgun into a logged
	 * warning + early-return. */
	if (!g_NetLocalClient) {
		sysLogPrintf(LOG_WARNING,
			"NETMSG: netmsgClcAuthWrite called with g_NetLocalClient == NULL "
			"-- skipping write (server build path or refactor regression)");
		return 0;
	}

	reconnecting = s_AuthSession.valid && s_AuthSession.active_endpoint_valid
		&& netmsgClcAuthEndpointEqual(&s_AuthSession.endpoint,
			&s_AuthSession.active_endpoint);
	/* A fresh connection uses the active profile name. A reconnect must send
	 * the exact name frozen with its cookie/settings transaction; consulting a
	 * mutable profile here would make a valid stable-slot retry fail auth. */
	const char *name = g_NetLocalClient->settings.name;
	identity_profile_t *profile = identityGetActiveProfile();
	if (!reconnecting && profile && profile->name[0]) {
		name = profile->name;
	}

	netbufWriteU8(dst, CLC_AUTH);
	netbufWriteStr(dst, name);
	netbufWriteU32(dst, utilCrc32(g_RomName)); // CRC32 of ROM identifier — avoids leaking the name string and is more compact
	netbufWriteStr(dst, modDir);
	netbufWriteU8(dst, (u8)PLAYERCOUNT()); // number of local (splitscreen) players on this client

	/* MASTER-C3: include the previously-issued cookie (zeros on first join).
	 * Server uses it to disambiguate reconnect vs fresh join, defeating the
	 * name-spoof preserved-slot hijack described in SEC-3. */
	u8 zeros[NET_AUTH_COOKIE_LEN];
	memset(zeros, 0, sizeof(zeros));
	if (reconnecting) {
		netbufWriteData(dst, s_AuthSession.cookie, NET_AUTH_COOKIE_LEN);
	} else {
		netbufWriteData(dst, zeros, NET_AUTH_COOKIE_LEN);
	}

	return dst->error;
}

static s32 netmsgReconnectCredentialMatches(
		const struct netpreservedplayer *pp, const char *name,
		const u8 cookie[NET_AUTH_COOKIE_LEN])
{
	u8 diff = 0;
	u8 nonzero = 0;

	if (!pp || !pp->active || !name || !name[0] || !cookie
			|| strncasecmp(pp->name, name, NET_MAX_NAME) != 0) {
		return false;
	}
	for (s32 i = 0; i < NET_AUTH_COOKIE_LEN; i++) {
		nonzero |= cookie[i];
		diff |= (u8)(pp->cookie[i] ^ cookie[i]);
	}
	return nonzero != 0 && diff == 0;
}

void netmsgServerPublishAuthenticatedTopology(void)
{
	lobbyUpdate();
	if (g_Lobby.leaderSlot != 0xFF
			&& g_Lobby.leaderSlot < g_Lobby.numPlayers) {
		u8 leaderClientId = g_Lobby.players[g_Lobby.leaderSlot].clientId;
		for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
			struct netclient *ncl = &g_NetClients[ci];
			if (ncl == g_NetLocalClient || !ncl->peer) continue;
			if (ncl->state >= CLSTATE_LOBBY) {
				netbufStartWrite(&ncl->out);
				netmsgSvcLobbyLeaderWrite(&ncl->out, leaderClientId);
				netSend(ncl, NULL, true, NETCHAN_CONTROL);
			}
		}
		sysLogPrintf(LOG_NOTE,
			"NET: broadcast SVC_LOBBY_LEADER: client %u is leader",
			leaderClientId);
	}
	netBroadcastRoomList();
}

u32 netmsgClcAuthRead(struct netbuf *src, struct netclient *srccl)
{
	if (srccl->state != CLSTATE_AUTH) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_AUTH from client %u, who is not in CLSTATE_AUTH", srccl->id);
		return 1;
	}

	const char *name = netbufReadStr(src);
	const u32 romCrc = netbufReadU32(src); // CRC32 of client's g_RomName
	const char *modDir = netbufReadStr(src);
	const u8 players = netbufReadU8(src);

	/* MASTER-C3: client-supplied reconnect cookie (zeros = fresh join). */
	u8 suppliedCookie[NET_AUTH_COOKIE_LEN];
	netbufReadData(src, suppliedCookie, NET_AUTH_COOKIE_LEN);

	if (src->error) {
		sysLogPrintf(LOG_WARNING, "NET: malformed CLC_AUTH from client %u", srccl->id);
		netServerKick(srccl, DISCONNECT_KICKED);
		return 1;
	}
	if (players == 0 || players > MAX_PLAYERS) {
		sysLogPrintf(LOG_WARNING, "NET: malformed CLC_AUTH from client %u: invalid local player count %u",
			srccl->id, players);
		netServerKick(srccl, DISCONNECT_KICKED);
		return 1;
	}

	/* Dedicated servers have no ROM or mods loaded — skip file checks entirely.
	 * Only a listen server (client hosting) validates ROM and mod agreement. */
	if (!g_NetDedicated) {
		if (romCrc != utilCrc32(g_RomName)) {
			sysLogPrintf(LOG_WARNING, "NET: CLC_AUTH: client %u has wrong ROM (crc %08x vs %08x), disconnecting",
			             srccl->id, romCrc, utilCrc32(g_RomName));
			netServerKick(srccl, DISCONNECT_FILES);
			return src->error;
		}
	}

	/* B-1033: this legacy wire field is a client-local installation hint, not
	 * portable content identity.  Two peers may hold identical content under
	 * different absolute paths, and a clean peer may intentionally start with
	 * no package so the manifest/READY protocol can distribute it.  Preserve
	 * the field for wire compatibility, but leave content agreement to the
	 * authoritative match manifest and ready gate below the auth handshake. */
	(void)modDir;

	/* Remote settings never inherit the listen host. CLC_SETTINGS supplies one
	 * complete typed candidate immediately after auth. */
	memset(&srccl->settings, 0, sizeof(srccl->settings));
	srccl->settings.team = 0xff;
	strncpy(srccl->settings.name, name ? name : "Player", sizeof(srccl->settings.name) - 1);
	srccl->settings.name[sizeof(srccl->settings.name) - 1] = '\0';

	sysLogPrintf(LOG_NOTE, "NET: CLC_AUTH from client %u (%s), responding", srccl->id, srccl->settings.name);

	/* A live-match peer was admitted only into its hinted stable slot. CLC_AUTH
	 * now proves that the same preserved record owns both the name and cookie.
	 * SVC_AUTH publication is deferred until CLC_SETTINGS freezes its exact
	 * candidate; the same credential remains valid until stage commit. */
	struct netpreservedplayer *pp = NULL;
	const bool ingame = netServerMatchInProgress() != 0;
	if (srccl->reconnect_preserved_index < NET_MAX_CLIENTS) {
		pp = &g_NetPreservedPlayers[srccl->reconnect_preserved_index];
		if (!pp->active || pp->client_id != srccl->id
				|| !netmsgReconnectCredentialMatches(pp, name,
					suppliedCookie)) {
			sysLogPrintf(LOG_WARNING,
				"NET: CLC_AUTH reconnect credential mismatch client=%u preserved_client=%u active=%u name='%s'",
				(unsigned)srccl->id, (unsigned)pp->client_id,
				(unsigned)pp->active, name ? name : "");
			netServerKick(srccl, DISCONNECT_LATE);
			return 1;
		}
	} else if (ingame) {
		/* A fresh peer admitted just before a match transition cannot cross the
		 * boundary as an unreserved late join. */
		sysLogPrintf(LOG_WARNING,
			"NET: CLC_AUTH client %u crossed into live match without an admitted reconnect record",
			(unsigned)srccl->id);
		netServerKick(srccl, DISCONNECT_LATE);
		return 1;
	}

	srccl->state = CLSTATE_LOBBY;
	++g_NetNumClients; /* M-7: Increment only after successful auth (moved from netServerEvConnect). */
	if (pp) {
		sysLogPrintf(LOG_NOTE,
			"NET.RECONNECT.AUTH client=%u preserved_index=%u credential=accepted settings=pending",
			(unsigned)srccl->id,
			(unsigned)srccl->reconnect_preserved_index);
		return 0;
	}

	/* Fresh join: there is no preserved transaction to protect, so issue and
	 * publish the initial credential immediately. */
	netServerIssueCookie(srccl->auth_cookie);

	/* Send SVC_AUTH first so the client transitions to CLSTATE_LOBBY before receiving
	 * any further messages. Catalog info must come after auth — the client must be
	 * authenticated before it can meaningfully respond to catalog requests. */
	netbufStartWrite(&srccl->out);
	netmsgSvcAuthWrite(&srccl->out, srccl);
	netSend(srccl, NULL, true, NETCHAN_CONTROL);

	/* D3R-9: send catalog info so the client can diff and request missing components.
	 * Sent after SVC_AUTH so the client is in CLSTATE_LOBBY when it processes this. */
	netDistribServerSendCatalogInfo(srccl);

	sysLogPrintf(LOG_NOTE, "NET: %s (%u) joined", srccl->settings.name,
		srccl->id);
	netChatPrintf(NULL, "%s joined", name);
	netmsgServerPublishAuthenticatedTopology();

	return 0;
}

u32 netmsgClcChatWrite(struct netbuf *dst, const char *str)
{
	netbufWriteU8(dst, CLC_CHAT);
	netbufWriteStr(dst, str);
	return dst->error;
}

u32 netmsgClcChatRead(struct netbuf *src, struct netclient *srccl)
{
	const char *msg = netbufReadStr(src);
	if (msg && !src->error) {
		/* Reject oversized messages before rate-limiting — limits amplification
		 * even at the maximum allowed send rate. */
		if (strlen(msg) > CHAT_MSG_MAX_LEN) {
			sysLogPrintf(LOG_WARNING, "NET: chat message too long from client %u — dropped", srccl->id);
			return src->error;
		}
		/* Rate limit: max CHAT_RATE_MAX_MSGS per CHAT_RATE_WINDOW_MS per client.
		 * Ring buffer stores timestamps of last N sends; if the oldest slot is
		 * still within the window the ring is full and we drop the message. */
		u32 idx = (u32)(srccl - g_NetClients);
		if (idx <= (u32)NET_MAX_CLIENTS) {
			struct chatrate *rate = &s_ChatRate[idx];
			u32 now = SDL_GetTicks();
			u32 oldest = rate->timestamps[rate->head];
			if (now - oldest < CHAT_RATE_WINDOW_MS) {
				sysLogPrintf(LOG_WARNING, "NET: chat rate limit hit for client %u — message dropped", srccl->id);
				return src->error;
			}
			rate->timestamps[rate->head] = now;
			rate->head = (rate->head + 1) % CHAT_RATE_MAX_MSGS;
		}
		sysLogPrintf(LOG_CHAT, "%s", msg);
		netbufStartWrite(&g_NetMsgRel);
		netmsgSvcChatWrite(&g_NetMsgRel, msg);
		netSend(NULL, &g_NetMsgRel, true, NETCHAN_DEFAULT);
	}
	return src->error;
}

enum netmsg_reconnect_prop_detail_e {
	NET_RECONNECT_PROP_DETAIL_NONE = 0,
	NET_RECONNECT_PROP_DETAIL_DOOR,
	NET_RECONNECT_PROP_DETAIL_LIFT,
	NET_RECONNECT_PROP_DETAIL_AUTOGUN,
	NET_RECONNECT_PROP_DETAIL_WEAPON,
};

enum netmsg_reconnect_projectile_kind_e {
	NET_RECONNECT_PROJECTILE_NONE = 0,
	NET_RECONNECT_PROJECTILE_DIRECT,
	NET_RECONNECT_PROJECTILE_EMBEDDED,
	NET_RECONNECT_PROJECTILE_EMBEDDED_EMPTY,
	NET_RECONNECT_PROJECTILE_INVALID = 0xff,
};

#define NET_RECONNECT_LIFECYCLE_PRUNED_PARENT 0x80u

_Static_assert(NET_MAX_CLIENTS > 0 && NET_MAX_CLIENTS <= 32,
	"reconnect inventory mask requires a 1..32 client domain");

static u32 netmsgReconnectSnapshotFail(
		net_reconnect_snapshot_result_t *result,
		net_reconnect_snapshot_status_e status, u8 client_id,
		u32 prop_syncid, s32 objective_index)
{
	if (result && result->status == NET_RECONNECT_SNAPSHOT_OK) {
		result->status = status;
		result->client_id = client_id;
		result->prop_syncid = prop_syncid;
		result->objective_index = objective_index;
	}
	return 1;
}

static void netmsgReconnectSnapshotResultInit(
		net_reconnect_snapshot_result_t *result, const struct netbuf *dst)
{
	if (!result) {
		return;
	}
	memset(result, 0, sizeof(*result));
	result->status = NET_RECONNECT_SNAPSHOT_OK;
	result->client_id = NET_NULL_CLIENT;
	result->prop_syncid = NET_NULL_PROP;
	result->objective_index = -1;
	result->prop_state_status = NET_RECONNECT_PROP_STATE_NONE;
	result->prop_type = -1;
	result->object_type = -1;
	result->model_num = -1;
	result->weapon_num = -1;
	result->dual_weapon_num = -1;
	result->attachment_mtx_index = -1;
	result->capacity = dst ? dst->size : 0;
}

static u32 netmsgReconnectSnapshotFailProp(
		net_reconnect_snapshot_result_t *result,
		net_reconnect_snapshot_status_e status, const struct prop *prop,
		net_reconnect_prop_state_status_e prop_state_status,
		s32 attachment_mtx_index)
{
	if (result && result->status == NET_RECONNECT_SNAPSHOT_OK) {
		result->prop_state_status = prop_state_status;
		result->prop_type = prop ? prop->type : -1;
		result->parent_syncid = prop && prop->parent
			? prop->parent->syncid : NET_NULL_PROP;
		result->attachment_mtx_index = attachment_mtx_index;
		if (prop && prop->obj) {
			result->object_type = prop->obj->type;
			result->model_num = prop->obj->modelnum;
			result->model_scale = prop->obj->model
				? prop->obj->model->scale : 0.0f;
			result->object_hidden = prop->obj->hidden;
			result->object_hidden2 = prop->obj->hidden2;
			if (prop->type == PROPTYPE_WEAPON
					&& prop->obj->type == OBJTYPE_WEAPON) {
				result->weapon_num = prop->weapon->weaponnum;
				result->dual_weapon_num = prop->weapon->dualweaponnum;
				result->dual_prop_syncid = prop->weapon->dualweapon
					&& prop->weapon->dualweapon->base.prop
					? prop->weapon->dualweapon->base.prop->syncid
					: NET_NULL_PROP;
			}
		}
	}
	return netmsgReconnectSnapshotFail(result, status, NET_NULL_CLIENT,
		prop ? prop->syncid : NET_NULL_PROP, -1);
}

const char *netmsgReconnectSnapshotStatusString(
		net_reconnect_snapshot_status_e status)
{
	switch (status) {
	case NET_RECONNECT_SNAPSHOT_OK: return "ok";
	case NET_RECONNECT_SNAPSHOT_INVALID_ARGUMENT: return "invalid_argument";
	case NET_RECONNECT_SNAPSHOT_CUTSCENE: return "cutscene";
	case NET_RECONNECT_SNAPSHOT_ROSTER: return "roster";
	case NET_RECONNECT_SNAPSHOT_WORLD_ARGUMENT: return "world_argument";
	case NET_RECONNECT_SNAPSHOT_WORLD_DUPLICATE_SYNC_ID:
		return "world_duplicate_sync_id";
	case NET_RECONNECT_SNAPSHOT_WORLD_UNSUPPORTED_DYNAMIC:
		return "world_unsupported_dynamic";
	case NET_RECONNECT_SNAPSHOT_WORLD_SPAWN_WRITE:
		return "world_spawn_write";
	case NET_RECONNECT_SNAPSHOT_WORLD_PROP_STATE:
		return "world_prop_state";
	case NET_RECONNECT_SNAPSHOT_WORLD_PROJECTILE_STATE:
		return "world_projectile_state";
	case NET_RECONNECT_SNAPSHOT_WORLD_PROJECTILE_OWNER:
		return "world_projectile_owner";
	case NET_RECONNECT_SNAPSHOT_INVENTORY: return "inventory";
	case NET_RECONNECT_SNAPSHOT_PLAYER_STATS: return "player_stats";
	case NET_RECONNECT_SNAPSHOT_PLAYER_MOVEMENT: return "player_movement";
	case NET_RECONNECT_SNAPSHOT_CHARACTER: return "character";
	case NET_RECONNECT_SNAPSHOT_NPC: return "npc";
	case NET_RECONNECT_SNAPSHOT_STAGE_FLAGS: return "stage_flags";
	case NET_RECONNECT_SNAPSHOT_OBJECTIVE: return "objective";
	case NET_RECONNECT_SNAPSHOT_SCORES: return "scores";
	case NET_RECONNECT_SNAPSHOT_COMMIT: return "commit";
	case NET_RECONNECT_SNAPSHOT_BUFFER: return "buffer";
	default: return "unknown";
	}
}

const char *netmsgReconnectPropStateStatusString(
		net_reconnect_prop_state_status_e status)
{
	switch (status) {
	case NET_RECONNECT_PROP_STATE_NONE: return "none";
	case NET_RECONNECT_PROP_STATE_INVALID_ARGUMENT:
		return "invalid_argument";
	case NET_RECONNECT_PROP_STATE_MODEL_SCALE_NONPOSITIVE:
		return "model_scale_nonpositive";
	case NET_RECONNECT_PROP_STATE_MODEL_SCALE_TOO_LARGE:
		return "model_scale_too_large";
	case NET_RECONNECT_PROP_STATE_MODEL_SCALE_NAN:
		return "model_scale_nan";
	case NET_RECONNECT_PROP_STATE_ATTACHMENT_PAIR:
		return "attachment_pair";
	case NET_RECONNECT_PROP_STATE_ATTACHMENT_PARENT:
		return "attachment_parent";
	case NET_RECONNECT_PROP_STATE_ATTACHMENT_MATRIX_RANGE:
		return "attachment_matrix_range";
	case NET_RECONNECT_PROP_STATE_ATTACHMENT_MATRIX_MISSING:
		return "attachment_matrix_missing";
	case NET_RECONNECT_PROP_STATE_EMBEDDED_ATTACHMENT:
		return "embedded_attachment";
	case NET_RECONNECT_PROP_STATE_WEAPON_IDENTITY:
		return "weapon_identity";
	case NET_RECONNECT_PROP_STATE_WEAPON_DUAL_REFERENCE:
		return "weapon_dual_reference";
	default: return "unknown";
	}
}

static u32 netmsgReconnectValidClientMask(void)
{
#if NET_MAX_CLIENTS >= 32
	return ~(u32)0;
#else
	return ((u32)1u << NET_MAX_CLIENTS) - 1u;
#endif
}

typedef struct netmsg_reconnect_projectile_wire_s {
	u8 kind;
	u32 flags;
	struct coord speed;
	struct coord nextsteppos;
	f32 unk010;
	f32 unk014;
	f32 unk018;
	f32 unk08c;
	f32 unk098;
	f32 unk0dc;
	f32 unk0e0;
	f32 unk0e4;
	f32 unk0ec;
	f32 unk0f0;
	s32 bouncecount;
	s32 bounceframe;
	s32 flighttime240;
	s32 pickuptimer240;
	s32 losttimer240;
	s32 smoketimer240;
	s16 droptype;
	s16 powerlimit240;
	u32 owner_id;
	u32 target_id;
	u32 pickupby_id;
	Mtxf mtx;
	u32 embed_flags;
	Mtxf embed_matrix;
	f32 hominggain;
	f32 homingdamping;
	f32 homingpreverr;
	f32 flyturnrate;
	f32 flyaccel;
	f32 flyproxradius;
	f32 flymaxaltitude;
	s32 flylosttimeout240;
	s32 flysmokeinterval240;
	s32 graphtrailsmoketype;
	s32 graphtrailinterval240;
} netmsg_reconnect_projectile_wire_t;

typedef struct netmsg_reconnect_prop_wire_s {
	u32 syncid;
	u8 prop_type;
	u8 obj_type;
	u8 prop_flags;
	u8 lifecycle_flags;
	s16 timetoregen;
	struct coord pos;
	RoomNum rooms[8];
	u32 parent_id;
	s16 attachment_mtx_index;
	u32 obj_flags;
	u32 obj_flags2;
	u32 obj_flags3;
	u32 hidden;
	u8 hidden2;
	u8 has_model;
	u16 extrascale;
	s16 pad;
	f32 model_scale;
	s16 damage;
	s16 maxdamage;
	u8 shadecol[4];
	u8 nextcol[4];
	u16 floorcol;
	f32 realrot[3][3];
	u8 detail;
	f32 detail_f32[4];
	s32 detail_s32[4];
	u32 detail_target_id;
	u32 weapon_dual_id;
	s32 weaponnum;
	s32 dualweaponnum;
	netmsg_reconnect_projectile_wire_t projectile;
} netmsg_reconnect_prop_wire_t;

typedef struct netmsg_reconnect_inventory_item_s {
	u8 type;
	s32 weapon1;
	s32 weapon2;
	s16 pickuppad;
	u32 prop_id;
} netmsg_reconnect_inventory_item_t;

static bool netmsgReconnectCanSpawnDynamicProp(const struct prop *prop)
{
	return prop && prop->obj
		&& (prop->type == PROPTYPE_WEAPON
			|| (prop->type == PROPTYPE_OBJ
				&& prop->obj->type == OBJTYPE_AUTOGUN));
}

static bool netmsgReconnectIsWorldPropCandidate(const struct prop *prop)
{
	if (!prop || !prop->syncid || !prop->obj
			|| (prop->type != PROPTYPE_OBJ
				&& prop->type != PROPTYPE_DOOR
				&& prop->type != PROPTYPE_WEAPON)) {
		return false;
	}
	/* The network owns every setup object plus the runtime spawn vocabulary it
	 * already distributes. Client-local transient debris/effects can also use
	 * prop slots, but requiring them here would make reconnect less faithful to
	 * the normal protocol rather than more faithful. */
	return prop->syncid < g_NetFirstDynamicSyncId
		|| netmsgReconnectCanSpawnDynamicProp(prop);
}

static bool netmsgReconnectIsWorldProp(const struct prop *prop)
{
	if (!netmsgReconnectIsWorldPropCandidate(prop)) {
		return false;
	}
	return netReconnectWorldPropShouldSerialize(
		(prop->obj->hidden & OBJHFLAG_DELETING) != 0,
		(prop->obj->hidden2 & OBJH2FLAG_CANREGEN) != 0) != 0;
}

static struct model *netmsgReconnectModelForProp(const struct prop *prop)
{
	if (!prop) {
		return NULL;
	}
	if (prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER) {
		return prop->chr ? prop->chr->model : NULL;
	}
	if (prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_DOOR
			|| prop->type == PROPTYPE_WEAPON) {
		return prop->obj ? prop->obj->model : NULL;
	}
	return NULL;
}

static struct projectile *netmsgReconnectProjectileForProp(
		const struct prop *prop, u8 *out_kind)
{
	if (out_kind) {
		*out_kind = NET_RECONNECT_PROJECTILE_NONE;
	}
	if (!prop || !prop->obj) {
		return NULL;
	}
	if ((prop->obj->hidden & OBJHFLAG_EMBEDDED)
			&& (prop->obj->hidden & OBJHFLAG_PROJECTILE)) {
		if (out_kind) {
			*out_kind = NET_RECONNECT_PROJECTILE_INVALID;
		}
		return NULL;
	}
	if (prop->obj->hidden & OBJHFLAG_EMBEDDED) {
		if (!prop->obj->embedment) {
			if (out_kind) {
				*out_kind = NET_RECONNECT_PROJECTILE_INVALID;
			}
			return NULL;
		}
		if (out_kind) {
			*out_kind = prop->obj->embedment->projectile
				? NET_RECONNECT_PROJECTILE_EMBEDDED
				: NET_RECONNECT_PROJECTILE_EMBEDDED_EMPTY;
		}
		return prop->obj->embedment->projectile;
	}
	if (prop->obj->hidden & OBJHFLAG_PROJECTILE) {
		if (!prop->obj->projectile) {
			if (out_kind) {
				*out_kind = NET_RECONNECT_PROJECTILE_INVALID;
			}
			return NULL;
		}
		if (out_kind) {
			*out_kind = NET_RECONNECT_PROJECTILE_DIRECT;
		}
		return prop->obj->projectile;
	}
	return NULL;
}

static u32 netmsgReconnectProjectileWrite(struct netbuf *dst,
		const struct prop *prop, net_reconnect_snapshot_result_t *result)
{
	u8 kind;
	struct projectile *projectile =
		netmsgReconnectProjectileForProp(prop, &kind);
	const bool has_projectile = kind == NET_RECONNECT_PROJECTILE_DIRECT
		|| kind == NET_RECONNECT_PROJECTILE_EMBEDDED;
	const bool embedded = kind == NET_RECONNECT_PROJECTILE_EMBEDDED
		|| kind == NET_RECONNECT_PROJECTILE_EMBEDDED_EMPTY;

	if (!dst || !prop || !prop->obj) {
		return netmsgReconnectSnapshotFailProp(result,
			NET_RECONNECT_SNAPSHOT_WORLD_PROP_STATE, prop,
			NET_RECONNECT_PROP_STATE_INVALID_ARGUMENT, -1);
	}
	if (kind == NET_RECONNECT_PROJECTILE_INVALID || (embedded
			&& !prop->obj->embedment) || (has_projectile && !projectile)) {
		return netmsgReconnectSnapshotFailProp(result,
			NET_RECONNECT_SNAPSHOT_WORLD_PROJECTILE_STATE, prop,
			NET_RECONNECT_PROP_STATE_NONE, -1);
	}
	if (has_projectile && projectile->obj != prop->obj) {
		return netmsgReconnectSnapshotFailProp(result,
			NET_RECONNECT_SNAPSHOT_WORLD_PROJECTILE_OWNER, prop,
			NET_RECONNECT_PROP_STATE_NONE, -1);
	}
	netbufWriteU8(dst, kind);
	if (has_projectile) {
		netbufWriteU32(dst, projectile->flags);
		netbufWriteCoord(dst, &projectile->speed);
		netbufWriteCoord(dst, &projectile->nextsteppos);
		netbufWriteF32(dst, projectile->unk010);
		netbufWriteF32(dst, projectile->unk014);
		netbufWriteF32(dst, projectile->unk018);
		netbufWriteF32(dst, projectile->unk08c);
		netbufWriteF32(dst, projectile->unk098);
		netbufWriteF32(dst, projectile->unk0dc);
		netbufWriteF32(dst, projectile->unk0e0);
		netbufWriteF32(dst, projectile->unk0e4);
		netbufWriteF32(dst, projectile->unk0ec);
		netbufWriteF32(dst, projectile->unk0f0);
		netbufWriteS32(dst, projectile->bouncecount);
		netbufWriteS32(dst, projectile->bounceframe);
		netbufWriteS32(dst, projectile->flighttime240);
		netbufWriteS32(dst, projectile->pickuptimer240);
		netbufWriteS32(dst, projectile->losttimer240);
		netbufWriteS32(dst, projectile->smoketimer240);
		netbufWriteS16(dst, projectile->droptype);
		netbufWriteS16(dst, projectile->powerlimit240);
		netbufWritePropPtr(dst, projectile->ownerprop);
		netbufWritePropPtr(dst, projectile->targetprop);
		netbufWritePropPtr(dst, projectile->pickupby);
		netbufWriteMtxf(dst, &projectile->mtx);
		netbufWriteF32(dst, projectile->hominggain);
		netbufWriteF32(dst, projectile->homingdamping);
		netbufWriteF32(dst, projectile->homingpreverr);
		netbufWriteF32(dst, projectile->flyturnrate);
		netbufWriteF32(dst, projectile->flyaccel);
		netbufWriteF32(dst, projectile->flyproxradius);
		netbufWriteF32(dst, projectile->flymaxaltitude);
		netbufWriteS32(dst, projectile->flylosttimeout240);
		netbufWriteS32(dst, projectile->flysmokeinterval240);
		netbufWriteS32(dst, projectile->graphtrailsmoketype);
		netbufWriteS32(dst, projectile->graphtrailinterval240);
	}
	if (embedded) {
		netbufWriteU32(dst, prop->obj->embedment->flags);
		netbufWriteMtxf(dst, &prop->obj->embedment->matrix);
	}
	return dst->error ? netmsgReconnectSnapshotFailProp(result,
		NET_RECONNECT_SNAPSHOT_BUFFER, prop,
		NET_RECONNECT_PROP_STATE_NONE, -1) : 0;
}

static u32 netmsgReconnectProjectileRead(struct netbuf *src,
		netmsg_reconnect_projectile_wire_t *out)
{
	if (!src || !out) {
		return 1;
	}
	memset(out, 0, sizeof(*out));
	out->kind = netbufReadU8(src);
	if (out->kind == NET_RECONNECT_PROJECTILE_NONE) {
		return src->error;
	}
	if (out->kind != NET_RECONNECT_PROJECTILE_DIRECT
			&& out->kind != NET_RECONNECT_PROJECTILE_EMBEDDED
			&& out->kind != NET_RECONNECT_PROJECTILE_EMBEDDED_EMPTY) {
		return 1;
	}

	if (out->kind == NET_RECONNECT_PROJECTILE_DIRECT
			|| out->kind == NET_RECONNECT_PROJECTILE_EMBEDDED) {
		out->flags = netbufReadU32(src);
		netbufReadCoord(src, &out->speed);
		netbufReadCoord(src, &out->nextsteppos);
		out->unk010 = netbufReadF32(src);
		out->unk014 = netbufReadF32(src);
		out->unk018 = netbufReadF32(src);
		out->unk08c = netbufReadF32(src);
		out->unk098 = netbufReadF32(src);
		out->unk0dc = netbufReadF32(src);
		out->unk0e0 = netbufReadF32(src);
		out->unk0e4 = netbufReadF32(src);
		out->unk0ec = netbufReadF32(src);
		out->unk0f0 = netbufReadF32(src);
		out->bouncecount = netbufReadS32(src);
		out->bounceframe = netbufReadS32(src);
		out->flighttime240 = netbufReadS32(src);
		out->pickuptimer240 = netbufReadS32(src);
		out->losttimer240 = netbufReadS32(src);
		out->smoketimer240 = netbufReadS32(src);
		out->droptype = netbufReadS16(src);
		out->powerlimit240 = netbufReadS16(src);
		out->owner_id = netbufReadU32(src);
		out->target_id = netbufReadU32(src);
		out->pickupby_id = netbufReadU32(src);
		netbufReadMtxf(src, &out->mtx);
		out->hominggain = netbufReadF32(src);
		out->homingdamping = netbufReadF32(src);
		out->homingpreverr = netbufReadF32(src);
		out->flyturnrate = netbufReadF32(src);
		out->flyaccel = netbufReadF32(src);
		out->flyproxradius = netbufReadF32(src);
		out->flymaxaltitude = netbufReadF32(src);
		out->flylosttimeout240 = netbufReadS32(src);
		out->flysmokeinterval240 = netbufReadS32(src);
		out->graphtrailsmoketype = netbufReadS32(src);
		out->graphtrailinterval240 = netbufReadS32(src);
	}
	if (out->kind == NET_RECONNECT_PROJECTILE_EMBEDDED
			|| out->kind == NET_RECONNECT_PROJECTILE_EMBEDDED_EMPTY) {
		out->embed_flags = netbufReadU32(src);
		netbufReadMtxf(src, &out->embed_matrix);
	}
	return src->error;
}

u32 netmsgClcMoveWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, CLC_MOVE);
	netbufWriteU32(dst, g_NetLocalClient->inmove[0].tick);
	netbufWritePlayerMove(dst, &g_NetLocalClient->outmove[0]);
	return dst->error;
}

u32 netmsgClcMoveRead(struct netbuf *src, struct netclient *srccl)
{
	struct netplayermove newmove;
	const u32 outmoveack = netbufReadU32(src);
	netbufReadPlayerMove(src, &newmove);
	if (src->error) {
		return src->error;
	}

	if (srccl->state != CLSTATE_GAME) {
		// silently ignore
		return src->error;
	}

	if ((newmove.ucmd & UCMD_SELECT) && newmove.weaponnum >= 0) {
		const bool dualwield = (newmove.ucmd & UCMD_SELECT_DUAL) != 0;
		if (!netmsgClientCanSelectWeapon(srccl, newmove.weaponnum, dualwield)) {
			sysLogPrintf(LOG_WARNING,
				"NET: rejected CLC_MOVE weapon select from client %u (weapon=%d dual=%d)",
				srccl->id, newmove.weaponnum, dualwield ? 1 : 0);
			newmove.weaponnum = -1;
			newmove.ucmd &= ~(UCMD_SELECT | UCMD_SELECT_DUAL);
		}
	}

	srccl->outmoveack = outmoveack;

	if (!src->error) {
		if (srccl->inmove[0].tick != 0
				&& (s32)(newmove.tick - srccl->inmove[0].tick) <= 0) {
			/* Drop stale/out-of-order moves to prevent rollback snaps. */
			return src->error;
		}

		// enforce teleports and such
		if (srccl->forcetick && srccl->player) {
			if (srccl->outmoveack >= srccl->forcetick) {
				// client has acknowledged our last sent move, clear the force flags
				srccl->forcetick = 0;
				srccl->player->ucmd &= ~UCMD_FL_FORCEMASK;
				sysLogPrintf(LOG_NOTE, "NET: client %u successfully forcemoved", srccl->id);
			} else {
				// client hasn't teleported yet, discard the new position from the input command
				if (srccl->player->ucmd & UCMD_FL_FORCEPOS) {
					newmove.pos = srccl->player->prop->pos;
				}
				if (srccl->player->ucmd & UCMD_FL_FORCEANGLE) {
					newmove.angles[0] = srccl->player->vv_theta;
					newmove.angles[1] = srccl->player->vv_verta;
				}
			}
		}
		// make space in the move stack
		memmove(srccl->inmove + 1, srccl->inmove, sizeof(srccl->inmove) - sizeof(*srccl->inmove));
		srccl->inmove[0] = newmove;
		srccl->lerpticks = 0;
		if (srccl->reconnect_gameplay_witness_pending
				&& (newmove.ucmd & UCMD_FIRE)
				&& srccl->player && srccl->player->prop) {
			srccl->reconnect_gameplay_witness_pending = false;
			sysLogPrintf(LOG_NOTE,
				"NET.RECONNECT.GAMEPLAY client=%u player=%u room=%u prop=%u move_tick=%u fire=1 server_accepted=1",
				(unsigned)srccl->id, (unsigned)srccl->playernum,
				(unsigned)srccl->room_id,
				(unsigned)srccl->player->prop->syncid,
				(unsigned)newmove.tick);
		}
	}

	return src->error;
}

static u8 netmsgCurrentClientTeamOrDefault(const struct netclient *srccl)
{
	if (srccl && srccl->state >= CLSTATE_GAME && srccl->config && srccl->config->base.team < MAX_TEAMS) {
		return srccl->config->base.team;
	}
	if (srccl && srccl->settings.team < MAX_TEAMS) {
		return srccl->settings.team;
	}
	if (srccl && srccl->config && srccl->config->base.team < MAX_TEAMS) {
		return srccl->config->base.team;
	}
	return 0;
}

static u8 netmsgSanitizeClientTeam(struct netclient *srccl, u8 wireTeam)
{
	const u8 fallback = netmsgCurrentClientTeamOrDefault(srccl);
	const u32 clientId = srccl ? srccl->id : 0;

	if (wireTeam >= MAX_TEAMS) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_SETTINGS client %u sent invalid team %u; keeping %u",
		             clientId, (unsigned)wireTeam, (unsigned)fallback);
		return fallback;
	}

	if (srccl && srccl->state >= CLSTATE_GAME && !(g_MpSetup.options & MPOPTION_TEAMSENABLED)
			&& wireTeam != fallback) {
		sysLogPrintf(LOG_WARNING,
		             "NET: CLC_SETTINGS client %u attempted team switch in non-team match %u -> %u; keeping %u",
		             clientId, (unsigned)fallback, (unsigned)wireTeam, (unsigned)fallback);
		return fallback;
	}

	return wireTeam;
}

u32 netmsgClcSettingsWrite(struct netbuf *dst)
{
	net_client_settings_input_t input;
	net_client_settings_wire_status_e status;
	player_identity_status_e identity_status;

	if (!dst || !g_NetLocalClient) {
		return 1;
	}

	input.options = g_NetLocalClient->settings.options;
	input.body_id = g_NetLocalClient->settings.body_id;
	input.head_id = g_NetLocalClient->settings.head_id;
	input.team = g_NetLocalClient->settings.team;
	input.handicap = g_NetLocalClient->settings.handicap;
	input.fovy = g_NetLocalClient->settings.fovy;
	input.fovzoommult = g_NetLocalClient->settings.fovzoommult;
	input.name = g_NetLocalClient->settings.name;
	status = netClientSettingsWireWrite(dst, CLC_SETTINGS, &input,
		&identity_status);
	if (status != NET_CLIENT_SETTINGS_WIRE_OK) {
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.PREFLIGHT client-settings=reject status=%s identity=%s team=%u handicap=%u bytes_published=0",
			netClientSettingsWireStatusString(status),
			playerIdentityStatusString(identity_status),
			(unsigned)g_NetLocalClient->settings.team,
			(unsigned)g_NetLocalClient->settings.handicap);
		return 1;
	}
	return 0;
}

typedef struct netmsg_reconnect_context_wire_s {
	u8 auth_data[32];
	u32 auth_len;
	u8 *catalog_data;
	u32 catalog_len;
	u8 *manifest_data;
	u32 manifest_len;
	u8 assignment_data[8];
	u32 assignment_len;
} netmsg_reconnect_context_wire_t;

static void netmsgReconnectContextFree(
		netmsg_reconnect_context_wire_t *wire)
{
	if (!wire) return;
	free(wire->catalog_data);
	free(wire->manifest_data);
	memset(wire, 0, sizeof(*wire));
}

static u32 netmsgSvcAuthWriteValues(struct netbuf *dst, u8 client_id,
		u8 max_clients, u32 tick,
		const u8 cookie[NET_AUTH_COOKIE_LEN])
{
	if (!dst || !cookie || client_id >= NET_NULL_CLIENT
			|| max_clients == 0 || max_clients > NET_MAX_CLIENTS) {
		return 1;
	}
	netbufWriteU8(dst, SVC_AUTH);
	netbufWriteU8(dst, client_id);
	netbufWriteU8(dst, max_clients);
	netbufWriteU32(dst, tick);
	netbufWriteData(dst, cookie, NET_AUTH_COOKIE_LEN);
	return dst->error;
}

static s32 netmsgReconnectContextPrepare(const struct netclient *srccl,
		const struct netpreservedplayer *pp,
		netmsg_reconnect_context_wire_t *out)
{
	const u32 capacity = NET_BUFSIZE;
	struct netbuf buf;
	netmsg_reconnect_context_wire_t candidate;

	if (!out || !srccl || !pp || !pp->active
			|| pp->client_id != srccl->id
			|| g_ServerManifest.num_entries == 0
			|| g_SessionCatalog.num_entries == 0
			|| g_SessionCatalog.num_entries
				!= g_ServerManifest.num_entries) {
		return false;
	}
	memset(&candidate, 0, sizeof(candidate));
	candidate.catalog_data = (u8 *)malloc(capacity);
	candidate.manifest_data = (u8 *)malloc(capacity);
	if (!candidate.catalog_data || !candidate.manifest_data) {
		netmsgReconnectContextFree(&candidate);
		return false;
	}

	memset(&buf, 0, sizeof(buf));
	buf.data = candidate.auth_data;
	buf.size = sizeof(candidate.auth_data);
	netbufStartWrite(&buf);
	if (netmsgSvcAuthWriteValues(&buf, (u8)srccl->id,
			(u8)g_NetMaxClients, g_NetTick, pp->cookie) != 0
			|| buf.wp == 0) {
		netmsgReconnectContextFree(&candidate);
		return false;
	}
	candidate.auth_len = buf.wp;

	memset(&buf, 0, sizeof(buf));
	buf.data = candidate.catalog_data;
	buf.size = capacity;
	netbufStartWrite(&buf);
	netbufWriteU8(&buf, SVC_SESSION_CATALOG);
	netbufWriteU16(&buf, g_SessionCatalog.num_entries);
	for (u16 i = 0; i < g_SessionCatalog.num_entries; i++) {
		const session_catalog_entry_t *entry = &g_SessionCatalog.entries[i];
		netbufWriteU16(&buf, entry->wire_id);
		netbufWriteU8(&buf, entry->asset_type);
		netbufWriteStr(&buf, entry->catalog_id);
	}
	if (buf.error || buf.wp == 0) {
		netmsgReconnectContextFree(&candidate);
		return false;
	}
	candidate.catalog_len = buf.wp;

	memset(&buf, 0, sizeof(buf));
	buf.data = candidate.manifest_data;
	buf.size = capacity;
	netbufStartWrite(&buf);
	if (netmsgSvcMatchManifestWrite(&buf, &g_ServerManifest) != 0
			|| buf.wp == 0) {
		netmsgReconnectContextFree(&candidate);
		return false;
	}
	candidate.manifest_len = buf.wp;

	memset(&buf, 0, sizeof(buf));
	buf.data = candidate.assignment_data;
	buf.size = sizeof(candidate.assignment_data);
	netbufStartWrite(&buf);
	if (netmsgSvcRoomAssignWrite(&buf, pp->room_id) != 0 || buf.wp == 0) {
		netmsgReconnectContextFree(&candidate);
		return false;
	}
	candidate.assignment_len = buf.wp;
	*out = candidate;
	return true;
}

static s32 netmsgReconnectPreparedSend(struct netclient *dstcl,
		const u8 *data, u32 len)
{
	struct netbuf buf;

	if (!dstcl || !data || len == 0) {
		return false;
	}
	memset(&buf, 0, sizeof(buf));
	buf.data = (u8 *)data;
	buf.size = len;
	buf.wp = len;
	return netSend(dstcl, &buf, true, NETCHAN_CONTROL) == len;
}

static s32 netmsgReconnectContextSend(struct netclient *dstcl,
		const netmsg_reconnect_context_wire_t *wire)
{
	/* Every packet uses the same reliable ordered channel. A failed enqueue
	 * leaves the preserved record untouched; the caller closes this attempt
	 * with the retryable timeout reason. */
	return wire
		&& netmsgReconnectPreparedSend(dstcl, wire->auth_data, wire->auth_len)
		&& netmsgReconnectPreparedSend(dstcl, wire->assignment_data,
			wire->assignment_len)
		&& netmsgReconnectPreparedSend(dstcl, wire->catalog_data,
			wire->catalog_len)
		&& netmsgReconnectPreparedSend(dstcl, wire->manifest_data,
			wire->manifest_len);
}

u32 netmsgClcSettingsRead(struct netbuf *src, struct netclient *srccl)
{
	net_client_settings_plan_t plan;
	net_client_settings_wire_status_e status;
	player_identity_status_e identity_status;
	bool in_match;
	u8 effective_handicap;
	u8 sanitizedTeam;

	if (!src || !srccl) {
		return 1;
	}
	status = netClientSettingsWireRead(src, &plan, &identity_status);
	if (status != NET_CLIENT_SETTINGS_WIRE_OK) {
		sysLogPrintf(LOG_WARNING,
			"NET: rejected CLC_SETTINGS client=%u status=%s identity=%s",
			(unsigned)srccl->id,
			netClientSettingsWireStatusString(status),
			playerIdentityStatusString(identity_status));
		netServerKick(srccl, DISCONNECT_KICKED);
		return 1;
	}

	if (srccl->reconnect_preserved_index < NET_MAX_CLIENTS) {
		netmsg_reconnect_context_wire_t context_wire;
		struct netpreservedplayer *pp =
			&g_NetPreservedPlayers[srccl->reconnect_preserved_index];

		sanitizedTeam = netmsgSanitizeClientTeam(srccl, plan.team);

		memset(&context_wire, 0, sizeof(context_wire));
		if (srccl->reconnect_settings_pending || !pp->active
				|| pp->client_id != srccl->id
				|| !netServerReconnectSettingsMatch(pp, &plan,
					sanitizedTeam)) {
			sysLogPrintf(LOG_ERROR,
				"PLAYER.INIT.ROLLBACK reconnect status=settings_or_record_mismatch client=%u globals_published=0",
				(unsigned)srccl->id);
			netServerKick(srccl, DISCONNECT_FILES);
			return 1;
		}
		if (!netmsgReconnectContextPrepare(srccl, pp, &context_wire)) {
			sysLogPrintf(LOG_ERROR,
				"PLAYER.INIT.ROLLBACK reconnect status=context_wire_failed client=%u globals_published=0",
				(unsigned)srccl->id);
			/* No authoritative state was consumed. A transport-style close lets
			 * the endpoint-scoped client credential retry the same reservation. */
			netServerKick(srccl, DISCONNECT_TIMEOUT);
			return 1;
		}

		/* Publish only the pending transaction metadata before the wire. Exact
		 * gameplay state and the preserved record remain untouched until the
		 * client proves this manifest hash READY. */
		srccl->reconnect_settings_plan = plan;
		srccl->reconnect_sanitized_team = sanitizedTeam;
		srccl->reconnect_manifest_hash = g_ServerManifest.manifest_hash;
		srccl->reconnect_manifest_phase = NET_RECONNECT_MANIFEST_WAITING;
		srccl->reconnect_settings_pending = true;
		srccl->state = CLSTATE_PREPARING;
		const s32 context_sent = netmsgReconnectContextSend(srccl,
			&context_wire);
		netmsgReconnectContextFree(&context_wire);
		if (!context_sent) {
			sysLogPrintf(LOG_ERROR,
				"PLAYER.INIT.ROLLBACK reconnect status=context_send_failed client=%u globals_published=0",
				(unsigned)srccl->id);
			netServerKick(srccl, DISCONNECT_TIMEOUT);
			return 1;
		}

		sysLogPrintf(LOG_NOTE,
			"NET.RECONNECT.MANIFEST client=%u hash=0x%08x status=waiting exact_settings=1",
			(unsigned)srccl->id,
			(unsigned)srccl->reconnect_manifest_hash);
		return 0;
	}

	/* B-1103/SP-67: the ready gate owns an immutable prepared roster. A valid
	 * settings candidate is normal lobby input, but it cannot publish beneath
	 * that snapshot. Roll back the exact gate first, then apply the candidate to
	 * restored lobby state. Invalid wire never reaches this boundary. */
	if (readyGatePreparingClientIndex(srccl) >= 0) {
		sysLogPrintf(LOG_NOTE,
			"PLAYER.INIT.ROLLBACK ready-gate reason=settings_changed client=%u validated_candidate=1 settings_published=0",
			(unsigned)srccl->id);
		readyGateAbort(srccl->settings.name[0]
			? srccl->settings.name : plan.name);
	}

	sanitizedTeam = netmsgSanitizeClientTeam(srccl, plan.team);

	in_match = srccl->state >= CLSTATE_GAME && srccl->config;
	effective_handicap = netClientSettingsEffectiveHandicap(plan.handicap,
		in_match, in_match ? srccl->config->handicap : plan.handicap);

	if (srccl->settings.name[0]
			&& strncmp(srccl->settings.name, plan.name, MAX_PLAYERNAME) != 0) {
		netChatPrintf(NULL, "%s is now known as %s", srccl->settings.name,
			plan.name);
	}

	strncpy(srccl->settings.body_id, plan.identity.body_id, CATALOG_ID_LEN - 1);
	srccl->settings.body_id[CATALOG_ID_LEN - 1] = '\0';
	strncpy(srccl->settings.head_id, plan.identity.head_id, CATALOG_ID_LEN - 1);
	srccl->settings.head_id[CATALOG_ID_LEN - 1] = '\0';

	strncpy(srccl->settings.name, plan.name, sizeof(srccl->settings.name) - 1);
	srccl->settings.name[sizeof(srccl->settings.name) - 1] = '\0';
	srccl->settings.options = plan.options;
	srccl->settings.fovy = plan.fovy;
	srccl->settings.fovzoommult = plan.fovzoommult;
	if (in_match) {
		if (plan.handicap != effective_handicap) {
			sysLogPrintf(LOG_WARNING,
				"NET: client %u attempted in-match handicap change %u -> %u; keeping authoritative %u",
				(unsigned)srccl->id,
				(unsigned)srccl->config->handicap, (unsigned)plan.handicap,
				(unsigned)effective_handicap);
		}
	}
	srccl->settings.handicap = effective_handicap;

	// apply team change if in-game
	if (sanitizedTeam != srccl->settings.team && srccl->state >= CLSTATE_GAME && srccl->config) {
		sysLogPrintf(LOG_NOTE, "NET: client %u (%s) switched to team %u",
		             srccl->id, srccl->settings.name, sanitizedTeam);
		netChatPrintf(NULL, "%s switched teams", srccl->settings.name);
		srccl->config->base.team = sanitizedTeam;
	}
	srccl->settings.team = sanitizedTeam;
	sysLogPrintf(LOG_NOTE,
		"NET: accepted CLC_SETTINGS client=%u body='%s' head='%s' team=%u handicap=%u",
		(unsigned)srccl->id, srccl->settings.body_id,
		srccl->settings.head_id, (unsigned)srccl->settings.team,
		(unsigned)srccl->settings.handicap);

	return 0;
}

/* server -> client */

u32 netmsgSvcAuthWrite(struct netbuf *dst, struct netclient *authcl)
{
	/* MASTER-C3: ship the server-issued identity cookie to the client.  The
	 * client echoes this back on any subsequent reconnect to reclaim its
	 * preserved slot. */
	return authcl
		? netmsgSvcAuthWriteValues(dst, (u8)(authcl - g_NetClients),
			(u8)g_NetMaxClients, g_NetTick, authcl->auth_cookie)
		: 1;
}

u32 netmsgSvcAuthRead(struct netbuf *src, struct netclient *srccl)
{
	if (!g_NetLocalClient || g_NetLocalClient->state != CLSTATE_AUTH) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_AUTH from server but we're not in AUTH state");
		return 1;
	}

	const u8 id = netbufReadU8(src);
	const u8 maxclients = netbufReadU8(src);
	g_NetTick = netbufReadU32(src);
	/* MASTER-C3: server-issued identity cookie.  Stored locally for reconnect. */
	u8 cookie[NET_AUTH_COOKIE_LEN];
	netbufReadData(src, cookie, NET_AUTH_COOKIE_LEN);
	if (g_NetLocalClient->in.error || id == NET_NULL_CLIENT || id >= NET_MAX_CLIENTS
			|| maxclients == 0 || maxclients > NET_MAX_CLIENTS) {
		sysLogPrintf(LOG_WARNING, "NET: malformed SVC_AUTH from server (id=%u maxclients=%u)", id, maxclients);
		return 1;
	}

	/* Persist only after the complete SVC_AUTH packet validates. The active
	 * resolved endpoint was captured before enet_host_connect. */
	netmsgClcAuthStoreCookie(cookie, id);
	netClientReconnectAuthAccepted();

	sysLogPrintf(LOG_NOTE, "NET: SVC_AUTH from server, our ID is %u", id);

	// there's at least one client, which is us, and we know maxclients as well
	g_NetMaxClients = maxclients;
	g_NetNumClients = 1;

	// we now know our proper ID, so move to the appropriate client slot and reset the old one
	g_NetLocalClient = &g_NetClients[id];
	g_NetClients[id] = g_NetClients[NET_MAX_CLIENTS];
	g_NetLocalClient->out.data = g_NetLocalClient->out_data;
	g_NetLocalClient->id = id;

	// clear out the old slot
	g_NetClients[NET_MAX_CLIENTS].id = NET_MAX_CLIENTS;
	g_NetClients[NET_MAX_CLIENTS].state = 0;
	g_NetClients[NET_MAX_CLIENTS].peer = NULL;

	g_NetLocalClient->state = CLSTATE_LOBBY;

	/* J-1/J-5: clear the N64 menu stack so MENU_JOIN doesn't co-render
	 * behind the lobby screen, and reset the ImGui main menu view so
	 * a later disconnect reopens at the top level. */
	menuStop();
#if !defined(PD_SERVER)
	pdguiMainMenuReset();
#endif

	return src->error;
}

u32 netmsgSvcChatWrite(struct netbuf *dst, const char *str)
{
	netbufWriteU8(dst, SVC_CHAT);
	netbufWriteStr(dst, str);
	return dst->error;
}

u32 netmsgSvcChatRead(struct netbuf *src, struct netclient *srccl)
{
	const char *msg = netbufReadStr(src);
	if (msg && !src->error) {
		sysLogPrintf(LOG_CHAT, "%s", msg);
	}
	return src->error;
}

typedef struct net_stage_start_write_client_t {
	u8 client_id;
	u8 playernum;
	u8 flags;
	u8 team;
	u8 handicap;
	u16 options;
	u16 body_session;
	u16 head_session;
	f32 fovy;
	f32 fovzoommult;
	char name[NET_MAX_NAME];
} net_stage_start_write_client_t;

typedef struct net_stage_start_write_bot_t {
	char name[sizeof(((struct mpbotconfig *)0)->base.name)];
	u16 body_session;
	u16 head_session;
	u16 profile_session;
	u8 difficulty;
	u8 type;
	u8 team;
} net_stage_start_write_bot_t;

typedef struct net_stage_start_write_plan_t {
	u32 net_tick;
	u64 rng_seed_0;
	u64 rng_seed_1;
	u32 match_seed;
	u32 stage_epoch;
	u8 mode;
	u16 stage_session;
	char stage_id[CATALOG_ID_LEN];
	u8 difficulty;
	u8 coop_friendly_fire;
	u8 coop_radar;
	u8 anti_playernum;
	char scenario_id[CATALOG_ID_LEN];
	u8 scorelimit;
	u8 timelimit;
	u16 teamscorelimit;
	u64 active_mask;
	u32 options;
	u16 weapon_sessions[NUM_MPWEAPONSLOTS];
	char spawn_weapon_id[CATALOG_ID_LEN];
	u8 spawn_weapon_mode;
	u8 spawn_weapon_num;
	char mod_track_id[CATALOG_ID_LEN];
	u8 roster_count;
	net_stage_start_write_client_t clients[MAX_PLAYERS];
	bool bot_active[MAX_BOTS];
	net_stage_start_write_bot_t bots[MAX_BOTS];
} net_stage_start_write_plan_t;

static s32 netStageStartPopcount64(u64 value);

static bool netStageStartWriteCopyId(char dst[CATALOG_ID_LEN], const char *src)
{
	size_t length;

	if (dst == NULL || src == NULL) {
		return false;
	}
	length = strnlen(src, CATALOG_ID_LEN);
	if (length == 0 || length >= CATALOG_ID_LEN) {
		dst[0] = '\0';
		return false;
	}
	memcpy(dst, src, length + 1);
	return true;
}

static bool netStageStartWriteReject(const char *detail)
{
	sysLogPrintf(LOG_ERROR,
		"PLAYER.INIT.ROLLBACK stage-write detail=%s bytes_published=0",
		detail ? detail : "unspecified");
	return false;
}

static bool netStageStartWriteAppendClient(
		net_stage_start_write_plan_t *plan,
		bool client_ids[NET_MAX_CLIENTS], bool player_slots[MAX_PLAYERS],
		u64 *roster_mask, u8 client_id, u8 playernum, u8 flags,
		const struct netclientsettings *settings,
		const struct mpplayerconfig *config)
{
	net_stage_start_write_client_t *prepared;
	player_identity_plan_t identity;
	u8 team;
	u8 handicap;
	size_t name_length;

	if (!plan || !roster_mask || !settings
			|| plan->roster_count >= MAX_PLAYERS
			|| client_id >= NET_MAX_CLIENTS || client_ids[client_id]
			|| playernum >= MAX_PLAYERS || player_slots[playernum]
			|| (flags & ~CLFLAG_ABSENT) != 0) {
		return netStageStartWriteReject(
			"roster has too many, duplicate, invalid, or unsupported slots");
	}

	team = plan->mode == NETGAMEMODE_MP
		? (config ? config->base.team : settings->team) : 0;
	handicap = config ? config->handicap : settings->handicap;
	name_length = strnlen(settings->name, sizeof(settings->name));
	if (team >= MAX_TEAMS || handicap == 0 || name_length == 0
			|| name_length >= sizeof(settings->name)
			|| name_length >= sizeof(plan->clients[0].name)
			|| playerIdentityPrepare(settings->body_id,
				settings->head_id, &identity) != PLAYER_IDENTITY_OK) {
		return netStageStartWriteReject(
			"roster team, handicap, name, or typed identity is invalid");
	}

	prepared = &plan->clients[plan->roster_count++];
	prepared->client_id = client_id;
	prepared->playernum = playernum;
	prepared->flags = flags;
	prepared->team = team;
	prepared->handicap = handicap;
	prepared->options = settings->options;
	prepared->body_session = sessionCatalogGetId(identity.body_id);
	prepared->head_session = sessionCatalogGetId(identity.head_id);
	prepared->fovy = settings->fovy;
	prepared->fovzoommult = settings->fovzoommult;
	memcpy(prepared->name, settings->name, name_length + 1);
	if (prepared->body_session == 0 || prepared->head_session == 0) {
		return netStageStartWriteReject(
			"roster identity is absent from the session catalog");
	}

	client_ids[client_id] = true;
	player_slots[playernum] = true;
	*roster_mask |= 1ull << playernum;
	return true;
}

static bool netStageStartWritePrepare(net_stage_start_write_plan_t *plan,
		bool select_track)
{
	const char *stage_id;
	catalog_stage_result_t stage;
	bool client_ids[NET_MAX_CLIENTS] = { false };
	bool player_slots[MAX_PLAYERS] = { false };
	u64 roster_mask = 0;
	const s32 represented_slots = MAX_PLAYERS + MAX_BOTS;
	const u64 allowed_mask = represented_slots == 64
		? ~(u64)0 : ((1ull << represented_slots) - 1ull);

	if (plan == NULL) {
		return netStageStartWriteReject("missing plan");
	}
	memset(plan, 0, sizeof(*plan));
	plan->anti_playernum = NET_NULL_CLIENT;
	plan->net_tick = g_NetTick;
	plan->rng_seed_0 = g_RngSeed;
	plan->rng_seed_1 = g_Rng2Seed;
	plan->match_seed = g_NetMatchSeed;
	plan->stage_epoch = g_NetStageEpoch;
	plan->mode = g_NetGameMode;
	if (plan->stage_epoch == 0) {
		return netStageStartWriteReject("stage epoch is zero");
	}

	if (plan->mode != NETGAMEMODE_MP && plan->mode != NETGAMEMODE_COOP
			&& plan->mode != NETGAMEMODE_ANTI) {
		return netStageStartWriteReject("invalid game mode");
	}
	stage_id = plan->mode == NETGAMEMODE_MP
		? g_MpSetup.stage_id : g_MissionConfig.stage_id;
	if (!netStageStartWriteCopyId(plan->stage_id, stage_id)) {
		return netStageStartWriteReject("stage has no typed ID");
	}
	plan->stage_session = sessionCatalogGetId(plan->stage_id);
	if (plan->stage_session == 0
			|| !catalogResolveStageBySession(plan->stage_session, &stage)
			|| stage.entry == NULL || !stage.entry->occupied
			|| !stage.entry->enabled
			|| (stage.entry->type != ASSET_ARENA
				&& stage.entry->type != ASSET_MAP)
			|| strcmp(stage.entry->id, plan->stage_id) != 0
			|| stage.stagenum <= 0 || stage.stagenum > 255
			|| stage.stagenum != (plan->mode == NETGAMEMODE_MP
				? (s32)g_MpSetup.stagenum : (s32)g_MissionConfig.stagenum)) {
		return netStageStartWriteReject(
			"stage session, type, ID, and runtime binding disagree");
	}

	if (plan->mode == NETGAMEMODE_COOP || plan->mode == NETGAMEMODE_ANTI) {
		plan->difficulty = g_MissionConfig.difficulty;
		plan->coop_friendly_fire = g_NetCoopFriendlyFire;
		plan->coop_radar = g_NetCoopRadar;
		if (plan->difficulty > DIFF_PD) {
			return netStageStartWriteReject("mission difficulty is invalid");
		}
		if (plan->mode == NETGAMEMODE_ANTI) {
			if (g_Vars.antiplayernum < 0
					|| g_Vars.antiplayernum >= MAX_PLAYERS) {
				return netStageStartWriteReject(
					"Counter-Op has no exact anti-player slot");
			}
			plan->anti_playernum = (u8)g_Vars.antiplayernum;
		}
	} else {
		const asset_entry_t *scenario;

		if (!netStageStartWriteCopyId(plan->scenario_id,
				g_MatchConfig.scenario_id)) {
			return netStageStartWriteReject("scenario has no typed ID");
		}
		scenario = assetCatalogResolve(plan->scenario_id);
		if (!scenario || !scenario->occupied || !scenario->enabled
				|| scenario->type != ASSET_GAMEMODE
				|| scenario->ext.gamemode.mode_id < MPSCENARIO_COMBAT
				|| scenario->ext.gamemode.mode_id > MPSCENARIO_CAPTURETHECASE
				|| scenario->ext.gamemode.mode_id != g_MpSetup.scenario) {
			return netStageStartWriteReject(
				"scenario ID and runtime mode disagree");
		}
		plan->scorelimit = g_MpSetup.scorelimit;
		plan->timelimit = g_MpSetup.timelimit;
		plan->teamscorelimit = g_MpSetup.teamscorelimit;
		plan->active_mask = mpParticipantsEncodeActiveMask();
		plan->options = g_MpSetup.options;
		if ((plan->active_mask & ~allowed_mask) != 0) {
			return netStageStartWriteReject(
				"participant mask exceeds wire capacity");
		}

		for (s32 wi = 0; wi < NUM_MPWEAPONSLOTS; wi++) {
			const u8 mp_weapon_id = g_MpSetup.weapons[wi];
			catalog_weapon_result_t weapon;
			char weapon_id[CATALOG_ID_LEN];

			if (mp_weapon_id == MPWEAPON_NONE) {
				if (g_MatchConfig.weapon_ids[wi][0] != '\0') {
					return netStageStartWriteReject(
						"empty weapon slot retains a typed ID");
				}
				continue;
			}
			if (!netStageStartWriteCopyId(weapon_id,
					g_MatchConfig.weapon_ids[wi])) {
				return netStageStartWriteReject(
					"non-empty weapon slot has no typed ID");
			}
			plan->weapon_sessions[wi] =
				sessionCatalogGetId(g_MatchConfig.weapon_ids[wi]);
			if (plan->weapon_sessions[wi] == 0
					|| !catalogResolveWeaponBySession(
						plan->weapon_sessions[wi], &weapon)
					|| weapon.entry == NULL || !weapon.entry->occupied
					|| !weapon.entry->enabled
					|| weapon.entry->type != ASSET_WEAPON
					|| strcmp(weapon.entry->id, weapon_id) != 0
					|| weapon.mp_weapon_id != (s32)mp_weapon_id
					|| weapon.weapon_num <= 0
					|| catalogGetMpWeaponNum(weapon.mp_weapon_id)
						!= weapon.weapon_num) {
				return netStageStartWriteReject(
					"weapon session and runtime binding disagree");
			}
		}

		plan->spawn_weapon_mode = g_MatchConfig.spawnWeaponMode;
		plan->spawn_weapon_num = g_MatchConfig.spawnWeaponNum;
		if (plan->spawn_weapon_mode == SPAWNWEAPON_MODE_FIESTA) {
			if (plan->spawn_weapon_num != SPAWNWEAPON_FIESTA_SENTINEL) {
				return netStageStartWriteReject(
					"Fiesta spawn sentinel is invalid");
			}
			plan->spawn_weapon_id[0] = '\0';
		} else if (plan->spawn_weapon_mode == SPAWNWEAPON_MODE_SPECIFIC
				|| plan->spawn_weapon_mode == SPAWNWEAPON_MODE_RANDOM) {
			const char *spawn_id = plan->spawn_weapon_mode
				== SPAWNWEAPON_MODE_RANDOM
				? catalogWeaponIdByRuntimeWeaponNum(plan->spawn_weapon_num)
				: g_MatchConfig.spawn_weapon_id;
			const asset_entry_t *spawn;

			if (!spawnWeaponNumIsResolved(plan->spawn_weapon_num)
					|| !netStageStartWriteCopyId(
						plan->spawn_weapon_id, spawn_id)) {
				return netStageStartWriteReject(
					"resolved spawn weapon has no typed ID");
			}
			spawn = assetCatalogResolve(plan->spawn_weapon_id);
			if (!spawn || !spawn->occupied || !spawn->enabled
					|| spawn->type != ASSET_WEAPON
					|| spawn->runtime_index != plan->spawn_weapon_num) {
				return netStageStartWriteReject(
					"spawn weapon ID and runtime binding disagree");
			}
		} else {
			return netStageStartWriteReject("spawn weapon mode is invalid");
		}
	}

	/* Walk stable client IDs once. A live GAME binding wins. Otherwise an
	 * active timed reconnect record contributes the exact same participant as
	 * explicitly absent. This keeps simultaneous disconnects in the immutable
	 * match roster while preventing the current reconnect candidate from being
	 * serialized twice. */
	for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
		const struct netclient *client = &g_NetClients[i];
		const struct netpreservedplayer *preserved =
			netServerFindPreservedByClientId((u8)i);

		if (client->state == CLSTATE_GAME
				&& !(client->flags & CLFLAG_SPECTATOR)
				&& (g_NetMatchRoomId == 0xFF
					|| client->room_id == g_NetMatchRoomId)) {
			if (client->id != (u32)i) {
				return netStageStartWriteReject(
					"live roster client ID disagrees with stable slot");
			}
			if (!netStageStartWriteAppendClient(plan, client_ids,
						player_slots, &roster_mask, (u8)client->id,
						client->playernum,
						(u8)(client->flags & CLFLAG_ABSENT),
						&client->settings, client->config)) {
				return false;
			}
			continue;
		}

		if (preserved && preserved->active
				&& (g_NetMatchRoomId == 0xFF
					|| preserved->room_id == g_NetMatchRoomId)
				&& !netStageStartWriteAppendClient(plan, client_ids,
					player_slots, &roster_mask, preserved->client_id,
					preserved->playernum, CLFLAG_ABSENT,
					&preserved->settings, &preserved->config)) {
			return false;
		}
	}
	if (plan->roster_count == 0 || !client_ids[0]) {
		return netStageStartWriteReject(
			"authoritative roster has no player or wire client zero");
	}
	if (plan->mode == NETGAMEMODE_MP) {
		const u64 player_mask = plan->active_mask
			& ((1ull << MAX_PLAYERS) - 1ull);
		if (player_mask != roster_mask) {
			return netStageStartWriteReject(
				"participant mask and authoritative roster disagree");
		}
	} else if (plan->roster_count > 2) {
		return netStageStartWriteReject(
			"co-op/counter-op roster exceeds two players");
	}
	if (plan->mode == NETGAMEMODE_ANTI
			&& (plan->roster_count != 2
				|| !player_slots[plan->anti_playernum])) {
		return netStageStartWriteReject(
			"Counter-Op anti-player is absent from exact roster");
	}

	if (plan->mode == NETGAMEMODE_MP) {
		s32 bot_slot_map[MAX_BOTS];
		s32 bot_slot_count = 0;

		for (s32 si = 0; si < g_MatchConfig.numSlots
				&& bot_slot_count < MAX_BOTS; si++) {
			if (g_MatchConfig.slots[si].type == SLOT_BOT) {
				bot_slot_map[bot_slot_count++] = si;
			}
		}
		for (s32 botidx = 0; botidx < MAX_BOTS; botidx++) {
			const struct matchslot *slot;
			const struct mpbotconfig *bot;
			const asset_entry_t *profile_entry;
			const asset_runtime_binding_t *profile;
			player_identity_plan_t identity;
			net_stage_start_write_bot_t *prepared;
			size_t bot_name_length;

			if (!(plan->active_mask & (1ull << (MAX_PLAYERS + botidx)))) {
				continue;
			}
			if (botidx >= bot_slot_count) {
				return netStageStartWriteReject(
					"active bot has no typed match slot");
			}
			slot = &g_MatchConfig.slots[bot_slot_map[botidx]];
			bot = &g_BotConfigsArray[botidx];
			bot_name_length = strnlen(bot->base.name,
				sizeof(bot->base.name));
			if (bot_name_length == 0
					|| bot_name_length >= sizeof(bot->base.name)
					|| playerIdentityPrepare(slot->body_id, slot->head_id,
					&identity) != PLAYER_IDENTITY_OK
					|| strcmp(bot->base.body_id, identity.body_id) != 0
					|| strcmp(bot->base.head_id, identity.head_id) != 0) {
				return netStageStartWriteReject(
					"bot slot and runtime identity disagree");
			}
			profile_entry = assetCatalogResolve(slot->profile_id);
			profile = profile_entry && profile_entry->type == ASSET_BOT_PROFILE
				? mpBotProfileRuntimeBindingById(profile_entry->id) : NULL;
			if (!profile_entry || !profile_entry->occupied
					|| !profile_entry->enabled || !profile || !profile->active
					|| !profile->source_hydrated
					|| profile->type != ASSET_BOT_PROFILE
					|| strcmp(profile->id, profile_entry->id) != 0
					|| strcmp(bot->profile_id, profile_entry->id) != 0
					|| !assetRuntimePrimaryFileAccessible(profile)
					|| profile->bot_profile_type < 0
					|| profile->bot_profile_type > 255
					|| profile->bot_profile_difficulty < 0
					|| profile->bot_profile_difficulty > 255
					|| slot->team >= MAX_TEAMS
					|| bot->base.team != slot->team) {
				return netStageStartWriteReject(
					"bot profile has no exact hydrated runtime binding");
			}

			prepared = &plan->bots[botidx];
			strncpy(prepared->name, bot->base.name,
				sizeof(prepared->name) - 1);
			prepared->name[sizeof(prepared->name) - 1] = '\0';
			prepared->body_session = sessionCatalogGetId(identity.body_id);
			prepared->head_session = sessionCatalogGetId(identity.head_id);
			prepared->profile_session = sessionCatalogGetId(profile_entry->id);
			prepared->difficulty = (u8)profile->bot_profile_difficulty;
			prepared->type = (u8)profile->bot_profile_type;
			prepared->team = slot->team;
			if (prepared->body_session == 0 || prepared->head_session == 0
					|| prepared->profile_session == 0) {
				return netStageStartWriteReject(
					"bot identity is absent from the session catalog");
			}
			plan->bot_active[botidx] = true;
		}
	}

#if !defined(PD_SERVER)
	if (plan->mode == NETGAMEMODE_MP) {
		const s32 playlist_count = audioGetModPlaylistCount();
		const char *track = playlist_count > 0 && select_track
			? audioPickNextPlaylistTrack() : audioGetCurrentModTrackId();

		if (!select_track && playlist_count > 0) {
			for (s32 i = 0; i < playlist_count; i++) {
				const char *entry_id = audioGetModPlaylistEntry(i);
				const asset_entry_t *entry = entry_id && entry_id[0]
					? assetCatalogResolve(entry_id) : NULL;
				if (!entry || !entry->occupied || !entry->enabled
						|| entry->type != ASSET_AUDIO
						|| !entry->ext.audio.file_path[0]) {
					return netStageStartWriteReject(
						"playlist contains an unavailable or wrong-type track");
				}
			}
			if (!track || !track[0] || !audioIsInModPlaylist(track)) {
				return netStageStartWriteReject(
					"active playlist track is unavailable during replay");
			}
		}
		if (track && track[0]) {
			const asset_entry_t *entry;
			if (!netStageStartWriteCopyId(plan->mod_track_id, track)) {
				return netStageStartWriteReject("mod track ID is invalid");
			}
			entry = assetCatalogResolve(plan->mod_track_id);
			if (!entry || !entry->occupied || !entry->enabled
					|| entry->type != ASSET_AUDIO
					|| !entry->ext.audio.file_path[0]) {
				return netStageStartWriteReject(
					"mod track is unavailable or wrong-type");
			}
		}
	}
#endif

	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.PREFLIGHT stage-write mode=%u stage='%s' players=%u bots=%d",
		(unsigned)plan->mode, plan->stage_id, (unsigned)plan->roster_count,
		plan->mode == NETGAMEMODE_MP
			? netStageStartPopcount64(plan->active_mask >> MAX_PLAYERS) : 0);
	return true;
}


static u32 netmsgSvcStageStartWriteInternal(struct netbuf *dst,
		bool capture_authority_roster, bool select_track)
{
	net_stage_start_write_plan_t plan;

	if (capture_authority_roster) {
		memset(&s_StageStartAuthorityCandidate, 0,
			sizeof(s_StageStartAuthorityCandidate));
	}
	if (dst == NULL || !netStageStartWritePrepare(&plan, select_track)) {
		return 1;
	}
	/* S301 Airbase diag: capture every SVC_STAGE_START send with breadcrumb
	 * so the Airbase "no SVC_STAGE_START" repro shows whether the send
	 * even began. Tagged MATCHSTART.DIAG so it can be grepped separately
	 * from the existing MATCH-START: warnings. */
	sysLogPrintf(LOG_NOTE,
		"MATCHSTART.DIAG: SVC_STAGE_START write begin tick=%u seed=0x%llx matchSeed=%u "
		"stage_id='%s' mode=%d",
		(unsigned)plan.net_tick, (unsigned long long)plan.rng_seed_0,
		(unsigned)plan.match_seed, plan.stage_id, (int)plan.mode);
	crashBreadcrumbPush("SVC_STAGE_START.write stage='%s' mode=%d tick=%u",
		plan.stage_id, (int)plan.mode, (unsigned)plan.net_tick);

	netbufWriteU8(dst, SVC_STAGE_START);

	netbufWriteU32(dst, plan.net_tick);

	netbufWriteU64(dst, plan.rng_seed_0);
	netbufWriteU64(dst, plan.rng_seed_1);
	/* L2-4: match_seed for deterministic spawn pool generation.
	 * All clients receive this and store in g_NetMatchSeed so future
	 * spawnpool.c can produce identical spawn pools on every machine. */
	netbufWriteU32(dst, plan.match_seed);
	/* v58: exact stage-session identity echoed by CLC_STAGE_READY. */
	netbufWriteU32(dst, plan.stage_epoch);

	/* SA-3: the preflight froze the mode-appropriate primary stage ID and
	 * proved its nonzero session/runtime binding before any bytes were written. */
	catalogWriteAssetRef(dst, plan.stage_session);
	sysLogPrintf(LOG_WARNING,
		"MATCH-START: writing SVC_STAGE_START, stage_session=%d, stage_id='%s'",
		(int)plan.stage_session, plan.stage_id);

	// game settings
	netbufWriteU8(dst, plan.mode);

	if (plan.mode == NETGAMEMODE_COOP || plan.mode == NETGAMEMODE_ANTI) {
		// co-op / counter-op mission settings
		netbufWriteU8(dst, plan.difficulty);
		netbufWriteU8(dst, plan.coop_friendly_fire);
		netbufWriteU8(dst, plan.coop_radar);
		/* v36: authoritative anti player slot for Counter-Op.
		 * NET_NULL_CLIENT / 0xFF means "none / unused". */
		netbufWriteU8(dst, plan.anti_playernum);
	} else {
		// combat simulator settings
		netbufWriteStr(dst, plan.scenario_id);
		netbufWriteU8(dst, plan.scorelimit);
		netbufWriteU8(dst, plan.timelimit);
		netbufWriteU16(dst, plan.teamscorelimit);
		/* v37: active-slot bitmap derived from the participant pool.
		 * Bits 0..MAX_PLAYERS-1 = players, MAX_PLAYERS..MAX_MPCHRS-1 = bots. */
		netbufWriteU64(dst, plan.active_mask);
		netbufWriteU32(dst, plan.options);
		/* SA-3: weapons as session IDs (NUM_MPWEAPONSLOTS u16 entries) */
		for (s32 wi = 0; wi < NUM_MPWEAPONSLOTS; wi++) {
			catalogWriteAssetRef(dst, plan.weapon_sessions[wi]);
		}
		/* B-125: spawn_weapon_id as catalog ID string — clients need this
		 * to resolve spawnWeaponNum on their side for player spawn.
		 *
		 * S482 (2026-04-27, NET_PROTOCOL_VER 44 -> 45): spawn-weapon mode
		 * (SPECIFIC / RANDOM / FIESTA) plus the host-resolved spawnWeaponNum
		 * follow the catalog ID. The host resolves SPECIFIC + RANDOM at
		 * matchStart() so clients receive the integer directly without
		 * re-rolling. FIESTA carries SPAWNWEAPON_FIESTA_SENTINEL and clients
		 * roll per-spawn at the spawn site. */
		netbufWriteStr(dst, plan.spawn_weapon_id);
		netbufWriteU8(dst, plan.spawn_weapon_mode);
		netbufWriteU8(dst, plan.spawn_weapon_num);

		/* A-7: mod track ID for network-synced mod audio.
		 * Host resolves one track from the playlist (shuffle/sequential)
		 * and sends that single ID so all clients play the same music.
		 * Server build has no audio state — write empty string. */
		netbufWriteStr(dst, plan.mod_track_id);
	}

	/* Serialize exactly the authoritative match roster. Lounge clients and
	 * Theater spectators remain connected peers but are not player slots. */
	netbufWriteU8(dst, plan.roster_count);
	for (s32 i = 0; i < plan.roster_count; i++) {
		const net_stage_start_write_client_t *prepared = &plan.clients[i];
		netbufWriteU8(dst, prepared->client_id);
		netbufWriteU8(dst, prepared->playernum);
		netbufWriteU8(dst, prepared->flags);
		netbufWriteU8(dst, prepared->team);
		netbufWriteU16(dst, prepared->options);
		netbufWriteU8(dst, prepared->handicap);
		catalogWriteAssetRef(dst, prepared->body_session);
		catalogWriteAssetRef(dst, prepared->head_session);
		netbufWriteF32(dst, prepared->fovy);
		netbufWriteF32(dst, prepared->fovzoommult);
		netbufWriteStr(dst, prepared->name);
	}

	/* Serialize per-bot configs (combat sim only; bots don't apply to co-op). */
	if (plan.mode == NETGAMEMODE_MP) {
		for (s32 botidx = 0; botidx < MAX_BOTS; botidx++) {
			const net_stage_start_write_bot_t *bot;
			if (!plan.bot_active[botidx]) {
				continue;
			}
			bot = &plan.bots[botidx];
			netbufWriteStr(dst, bot->name);
			catalogWriteAssetRef(dst, bot->body_session);
			catalogWriteAssetRef(dst, bot->head_session);
			catalogWriteAssetRef(dst, bot->profile_session);
			netbufWriteU8(dst, bot->difficulty);
			netbufWriteU8(dst, bot->type);
			netbufWriteU8(dst, bot->team);
		}
	}

	if (!dst->error && capture_authority_roster) {
		for (u8 i = 0; i < plan.roster_count; i++) {
			s_StageStartAuthorityCandidate.participants[i].client_id =
				plan.clients[i].client_id;
			s_StageStartAuthorityCandidate.participants[i].runtime_playernum =
				plan.clients[i].playernum;
		}
		s_StageStartAuthorityCandidate.participant_count = plan.roster_count;
		s_StageStartAuthorityCandidate.valid = true;
	}
	return dst->error;
}

u32 netmsgSvcStageStartWrite(struct netbuf *dst)
{
	return netmsgSvcStageStartWriteInternal(dst, false, true);
}

u32 netmsgSvcStageReplayWrite(struct netbuf *dst)
{
	return netmsgSvcStageStartWriteInternal(dst, false, false);
}

u32 netmsgServerStageStartWrite(struct netbuf *dst, u8 room_id)
{
	u32 wp_before;
	u32 error_before;

	if (!dst || g_NetMode != NETMODE_SERVER) {
		return 1;
	}
	wp_before = dst->wp;
	error_before = dst->error;
	if (netmsgSvcStageStartWriteInternal(dst, true, true) != 0
			|| !s_StageStartAuthorityCandidate.valid
			|| !netmsgCutsceneAuthorityBeginMatch(room_id,
				s_StageStartAuthorityCandidate.participants,
				s_StageStartAuthorityCandidate.participant_count)) {
		dst->wp = wp_before;
		dst->error = error_before;
		netmsgCutsceneAuthorityReset();
		memset(&s_StageStartAuthorityCandidate, 0,
			sizeof(s_StageStartAuthorityCandidate));
		return 1;
	}
	memset(&s_StageStartAuthorityCandidate, 0,
		sizeof(s_StageStartAuthorityCandidate));
	return 0;
}

u32 netmsgSvcStageStartValidate(void)
{
	net_stage_start_write_plan_t plan;
	return netStageStartWritePrepare(&plan, false) ? 0 : 1;
}

typedef enum net_stage_start_status_e {
	NET_STAGE_START_OK = 0,
	NET_STAGE_START_MALFORMED,
	NET_STAGE_START_INVALID_STAGE,
	NET_STAGE_START_INVALID_MODE,
	NET_STAGE_START_INVALID_SCENARIO,
	NET_STAGE_START_INVALID_WEAPON,
	NET_STAGE_START_INVALID_SPAWN_WEAPON,
	NET_STAGE_START_INVALID_ROSTER,
	NET_STAGE_START_INVALID_PLAYER_IDENTITY,
	NET_STAGE_START_INVALID_BOT_IDENTITY,
	NET_STAGE_START_INVALID_BOT_PROFILE,
	NET_STAGE_START_PARTICIPANT_POOL_UNAVAILABLE,
} net_stage_start_status_e;

typedef struct net_stage_start_client_plan_t {
	u8 id;
	u8 playernum;
	u8 runtime_playernum;
	u8 flags;
	u8 team;
	u8 handicap;
	u16 options;
	f32 fovy;
	f32 fovzoommult;
	char name[NET_MAX_NAME];
	player_identity_plan_t identity;
} net_stage_start_client_plan_t;

typedef struct net_stage_start_plan_t {
	net_stage_start_status_e status;
	u32 net_tick;
	u64 rng_seed_0;
	u64 rng_seed_1;
	u32 match_seed;
	u32 stage_epoch;
	u8 mode;
	u8 stagenum;
	char stage_id[CATALOG_ID_LEN];
	struct missionconfig mission;
	struct mpsetup setup;
	char scenario_id[CATALOG_ID_LEN];
	u8 coop_friendly_fire;
	u8 coop_radar;
	u8 anti_playernum_wire;
	u64 active_mask;
	char weapon_ids[NUM_MPWEAPONSLOTS][CATALOG_ID_LEN];
	char spawn_weapon_id[CATALOG_ID_LEN];
	u8 spawn_weapon_mode;
	u8 spawn_weapon_num;
	char mod_track_id[CATALOG_ID_LEN];
	u8 numplayers;
	s32 bond_playernum;
	s32 coop_playernum;
	s32 anti_playernum;
	net_stage_start_client_plan_t clients[MAX_PLAYERS];
	struct mpbotconfig bots[MAX_BOTS];
	bool bot_active[MAX_BOTS];
} net_stage_start_plan_t;

static const char *netStageStartStatusString(net_stage_start_status_e status)
{
	switch (status) {
	case NET_STAGE_START_OK: return "ok";
	case NET_STAGE_START_MALFORMED: return "malformed";
	case NET_STAGE_START_INVALID_STAGE: return "invalid_stage";
	case NET_STAGE_START_INVALID_MODE: return "invalid_mode";
	case NET_STAGE_START_INVALID_SCENARIO: return "invalid_scenario";
	case NET_STAGE_START_INVALID_WEAPON: return "invalid_weapon";
	case NET_STAGE_START_INVALID_SPAWN_WEAPON: return "invalid_spawn_weapon";
	case NET_STAGE_START_INVALID_ROSTER: return "invalid_roster";
	case NET_STAGE_START_INVALID_PLAYER_IDENTITY: return "invalid_player_identity";
	case NET_STAGE_START_INVALID_BOT_IDENTITY: return "invalid_bot_identity";
	case NET_STAGE_START_INVALID_BOT_PROFILE: return "invalid_bot_profile";
	case NET_STAGE_START_PARTICIPANT_POOL_UNAVAILABLE: return "participant_pool_unavailable";
	default: return "unknown";
	}
}

static u32 netStageStartReject(net_stage_start_plan_t *plan,
		net_stage_start_status_e status, const char *detail)
{
	if (plan != NULL) {
		plan->status = status;
	}
	sysLogPrintf(LOG_ERROR,
		"PLAYER.INIT.ROLLBACK stage-start status=%s detail=%s globals_published=0",
		netStageStartStatusString(status), detail ? detail : "unspecified");
	return 1;
}

static bool netStageStartCopyId(char dst[CATALOG_ID_LEN], const char *src)
{
	size_t length;

	if (dst == NULL || src == NULL) {
		return false;
	}
	length = strnlen(src, CATALOG_ID_LEN);
	if (length == 0 || length >= CATALOG_ID_LEN) {
		dst[0] = '\0';
		return false;
	}
	memcpy(dst, src, length + 1);
	return true;
}

static s32 netStageStartPopcount64(u64 value)
{
	s32 count = 0;
	while (value != 0) {
		value &= value - 1;
		count++;
	}
	return count;
}

static s32 netStageStartMappedPlayernum(const net_stage_start_plan_t *plan,
		s32 client_index)
{
	s32 local_wire_playernum = -1;
	const net_stage_start_client_plan_t *client;

	if (plan == NULL || client_index < 0 || client_index >= plan->numplayers
			|| g_NetLocalClient == NULL) {
		return -1;
	}
	client = &plan->clients[client_index];
	for (s32 i = 0; i < plan->numplayers; i++) {
		if (plan->clients[i].id == g_NetLocalClient->id) {
			local_wire_playernum = plan->clients[i].playernum;
			break;
		}
	}
	if (local_wire_playernum < 0) {
		return -1;
	}
	if (client->id == g_NetLocalClient->id) {
		return 0;
	}
	if (client->id == 0) {
		return local_wire_playernum;
	}
	return client->playernum;
}

u32 netmsgSvcStageStartRead(struct netbuf *src, struct netclient *srccl)
{
	net_stage_start_plan_t plan;
	catalog_stage_result_t stage_result;

	memset(&plan, 0, sizeof(plan));
	plan.status = NET_STAGE_START_MALFORMED;
	plan.mission = g_MissionConfig;
	plan.setup = g_MpSetup;
	plan.anti_playernum_wire = NET_NULL_CLIENT;
	plan.spawn_weapon_num = 0xFF;
	plan.bond_playernum = -1;
	plan.coop_playernum = -1;
	plan.anti_playernum = -1;

	/* S301 Airbase diag: capture every SVC_STAGE_START receive. If the
	 * Airbase repro is reproduced and this log is ABSENT from client
	 * pd-client.log, the message never left the server. If it IS present
	 * but the stage doesn't load, the failure is downstream (manifest
	 * decode, catalog resolve, main change). */
	sysLogPrintf(LOG_NOTE,
		"MATCHSTART.DIAG: SVC_STAGE_START read begin srccl=%d state=%u",
		srccl ? srccl->id : -1, srccl ? srccl->state : 0xFFu);
	crashBreadcrumbPush("SVC_STAGE_START.read srccl=%d state=%u",
		srccl ? srccl->id : -1, srccl ? srccl->state : 0xFFu);

	/* c3845 (2026-06-23): two-process match-smoke milestone. This read path
	 * runs on the CLIENT when it receives the host's SVC_STAGE_START. */
	sysLogPrintf(LOG_NOTE, "MATCH: client stage start received");

	if (!srccl) {
		sysLogPrintf(LOG_WARNING,
			"MATCHSTART.DIAG: SVC_STAGE reject — missing source client");
		return netStageStartReject(&plan, NET_STAGE_START_MALFORMED,
			"missing source client");
	}

	if (srccl->state != CLSTATE_LOBBY && srccl->state != CLSTATE_GAME
	    && srccl->state != CLSTATE_PREPARING) {
		sysLogPrintf(LOG_WARNING,
			"MATCHSTART.DIAG: SVC_STAGE reject — client state=%u not in LOBBY/GAME/PREPARING",
			srccl->state);
		return netStageStartReject(&plan, NET_STAGE_START_MALFORMED,
			"source client is not ready for stage start");
	}

	plan.net_tick = netbufReadU32(src);
	plan.rng_seed_0 = netbufReadU64(src);
	plan.rng_seed_1 = netbufReadU64(src);
	/* L2-4: match_seed for deterministic spawn pools */
	plan.match_seed = netbufReadU32(src);
	plan.stage_epoch = netbufReadU32(src);
	if (src->error || plan.stage_epoch == 0) {
		return netStageStartReject(&plan, NET_STAGE_START_MALFORMED,
			"truncated or zero stage epoch");
	}

	/* SA-3: stage as session ID; 0 = return to lobby */
	const u16 stage_session = catalogReadAssetRef(src);
	sysLogPrintf(LOG_WARNING, "MATCH-START: client received SVC_STAGE_START, stage_session=%d", (int)stage_session);
	u8 stagenum = 0;
	if (stage_session == 0) {
		sysLogPrintf(LOG_WARNING, "MATCH-START: WARNING stage_session is 0 — aborting");
		return netStageStartReject(&plan, NET_STAGE_START_INVALID_STAGE,
			"stage session is zero");
	}
	{
		if (catalogResolveStageBySession(stage_session, &stage_result)
				&& stage_result.entry != NULL && stage_result.entry->occupied
				&& stage_result.entry->enabled
				&& (stage_result.entry->type == ASSET_ARENA
					|| stage_result.entry->type == ASSET_MAP)
				&& stage_result.stagenum > 0 && stage_result.stagenum <= 255) {
			stagenum = (u8)stage_result.stagenum;
			sysLogPrintf(LOG_WARNING, "MATCH-START: stage_session=%d resolved to stagenum=0x%x", (int)stage_session, (unsigned)stagenum);
		} else {
			sysLogPrintf(LOG_WARNING, "NET: SVC_STAGE unknown stage session %u", (unsigned)stage_session);
			return netStageStartReject(&plan, NET_STAGE_START_INVALID_STAGE,
				"stage session is missing, disabled, wrong-type, or unbound");
		}
	}
	plan.stagenum = stagenum;
	strncpy(plan.stage_id, stage_result.entry->id, sizeof(plan.stage_id) - 1);
	plan.stage_id[sizeof(plan.stage_id) - 1] = '\0';

	const u8 mode = netbufReadU8(src);
	u8 antiPlayerNumWire = NET_NULL_CLIENT;
	if (src->error) {
		sysLogPrintf(LOG_WARNING, "NET: malformed SVC_STAGE from server");
		return netStageStartReject(&plan, NET_STAGE_START_MALFORMED,
			"truncated fixed stage header");
	}
	if (mode != NETGAMEMODE_MP && mode != NETGAMEMODE_COOP &&
	    mode != NETGAMEMODE_ANTI) {
		sysLogPrintf(LOG_WARNING,
		             "NET: SVC_STAGE invalid gamemode=%u",
		             (unsigned)mode);
		return netStageStartReject(&plan, NET_STAGE_START_INVALID_MODE,
			"unknown game mode");
	}
	plan.mode = mode;

	if (mode == NETGAMEMODE_COOP || mode == NETGAMEMODE_ANTI) {
		// co-op / counter-op mission settings
		plan.mission.stagenum = stagenum;
		/* Phase 2: populate PRIMARY catalog ID string field */
		strncpy(plan.mission.stage_id, plan.stage_id,
			sizeof(plan.mission.stage_id) - 1);
		plan.mission.stage_id[sizeof(plan.mission.stage_id) - 1] = '\0';
		plan.mission.difficulty = netbufReadU8(src);
		plan.mission.iscoop = (mode == NETGAMEMODE_COOP);
		plan.mission.isanti = (mode == NETGAMEMODE_ANTI);
		plan.coop_friendly_fire = netbufReadU8(src);
		plan.coop_radar = netbufReadU8(src);
		antiPlayerNumWire = netbufReadU8(src);
		plan.anti_playernum_wire = antiPlayerNumWire;
		if (plan.mission.difficulty > DIFF_PD) {
			return netStageStartReject(&plan, NET_STAGE_START_MALFORMED,
				"co-op difficulty is outside runtime domain");
		}
	} else {
		// combat simulator settings
		plan.setup.stagenum = stagenum;
		/* Phase 2: populate PRIMARY catalog ID string field */
		strncpy(plan.setup.stage_id, plan.stage_id,
			sizeof(plan.setup.stage_id) - 1);
		plan.setup.stage_id[sizeof(plan.setup.stage_id) - 1] = '\0';
		/* M0.1d: scenario as catalog ID string (v32+). */
		{
			const char *scid_str = netbufReadStr(src);
			const char *scid = scid_str ? scid_str : "";
			const asset_entry_t *gm;
			if (!netStageStartCopyId(plan.scenario_id, scid)) {
				return netStageStartReject(&plan,
					NET_STAGE_START_INVALID_SCENARIO,
					"scenario ID is empty or unterminated");
			}
			gm = assetCatalogResolve(plan.scenario_id);
			if (!gm || !gm->occupied || !gm->enabled
					|| gm->type != ASSET_GAMEMODE
					|| gm->ext.gamemode.mode_id < MPSCENARIO_COMBAT
					|| gm->ext.gamemode.mode_id > MPSCENARIO_CAPTURETHECASE) {
				return netStageStartReject(&plan,
					NET_STAGE_START_INVALID_SCENARIO,
					plan.scenario_id);
			}
			plan.setup.scenario = (u8)gm->ext.gamemode.mode_id;
		}
		plan.setup.scorelimit = netbufReadU8(src);
		plan.setup.timelimit = netbufReadU8(src);
		plan.setup.teamscorelimit = netbufReadU16(src);
		{
			/* v37: active-slot bitmap → participant pool. */
			plan.active_mask = netbufReadU64(src);
		}
		plan.setup.options = netbufReadU32(src);
		/* SA-3: weapons as session IDs */
		{
			s32 wi;
			for (wi = 0; wi < NUM_MPWEAPONSLOTS; wi++) {
				const u16 wsession = catalogReadAssetRef(src);
				if (wsession == 0) {
					plan.setup.weapons[wi] = MPWEAPON_NONE;
					plan.weapon_ids[wi][0] = '\0';
				} else {
					catalog_weapon_result_t wr;
					if (!catalogResolveWeaponBySession(wsession, &wr)
							|| wr.entry == NULL || !wr.entry->occupied
							|| !wr.entry->enabled || wr.entry->type != ASSET_WEAPON
							|| wr.mp_weapon_id < 0
							|| wr.mp_weapon_id >= NUM_MPWEAPONS
							|| wr.weapon_num <= 0
							|| catalogGetMpWeaponNum(wr.mp_weapon_id) != wr.weapon_num) {
						return netStageStartReject(&plan,
							NET_STAGE_START_INVALID_WEAPON,
							"weapon session is missing, wrong-type, or inconsistently bound");
					}
					plan.setup.weapons[wi] = (u8)wr.mp_weapon_id;
					strncpy(plan.weapon_ids[wi], wr.entry->id,
						sizeof(plan.weapon_ids[wi]) - 1);
					plan.weapon_ids[wi][sizeof(plan.weapon_ids[wi]) - 1] = '\0';
				}
			}
		}
		/* B-125: read spawn_weapon_id and resolve to spawnWeaponNum.
		 *
		 * S482 (2026-04-27, NET_PROTOCOL_VER 45): wire format adds
		 *   u8 spawnWeaponMode + u8 spawnWeaponNum after spawn_weapon_id.
		 * The host resolves the integer at matchStart() so the client uses
		 * it directly (no re-roll). FIESTA carries SPAWNWEAPON_FIESTA_SENTINEL
		 * and the client rolls fresh per-spawn at the spawn site. */
		{
			const char *swid_str = netbufReadStr(src);
			const char *swid = swid_str ? swid_str : "";
			const u8 wireMode = netbufReadU8(src);
			const u8 wireNum  = netbufReadU8(src);
			const asset_entry_t *spawn_entry = NULL;

			if (wireMode != SPAWNWEAPON_MODE_SPECIFIC
					&& wireMode != SPAWNWEAPON_MODE_RANDOM
					&& wireMode != SPAWNWEAPON_MODE_FIESTA) {
				return netStageStartReject(&plan,
					NET_STAGE_START_INVALID_SPAWN_WEAPON,
					"spawn weapon mode is outside the protocol domain");
			}
			if (wireMode == SPAWNWEAPON_MODE_FIESTA) {
				if (swid[0] != '\0' || wireNum != SPAWNWEAPON_FIESTA_SENTINEL) {
					return netStageStartReject(&plan,
						NET_STAGE_START_INVALID_SPAWN_WEAPON,
						"Fiesta spawn identity/sentinel pair is inconsistent");
				}
				plan.spawn_weapon_id[0] = '\0';
			} else {
				if (!netStageStartCopyId(plan.spawn_weapon_id, swid)) {
					return netStageStartReject(&plan,
						NET_STAGE_START_INVALID_SPAWN_WEAPON,
						"resolved spawn weapon has no typed ID");
				}
				spawn_entry = assetCatalogResolve(plan.spawn_weapon_id);
				if (!spawn_entry || !spawn_entry->occupied || !spawn_entry->enabled
						|| spawn_entry->type != ASSET_WEAPON
						|| !spawnWeaponNumIsResolved(wireNum)
						|| spawn_entry->runtime_index != (s32)wireNum) {
					return netStageStartReject(&plan,
						NET_STAGE_START_INVALID_SPAWN_WEAPON,
						"spawn weapon ID and runtime cache disagree");
				}
			}
			plan.spawn_weapon_mode = wireMode;
			plan.spawn_weapon_num = wireNum;
			sysLogPrintf(LOG_NOTE,
				"NET: SVC_STAGE_START spawn weapon '%s' mode=%u weaponnum=%d",
				plan.spawn_weapon_id[0] ? plan.spawn_weapon_id : "(empty)",
				(unsigned)plan.spawn_weapon_mode,
				(s32)plan.spawn_weapon_num);
		}

		/* A-7: read host's mod track ID for network-synced mod audio.
		 * If non-empty, the client will use this mod track at match start
		 * instead of their own selection — host's music is authoritative.
		 * Server build discards this field (no audio state). */
		{
			const char *modtrack_str = netbufReadStr(src);
			const char *modtrack = modtrack_str ? modtrack_str : "";
			if (modtrack[0]) {
				const asset_entry_t *ae = assetCatalogResolve(modtrack);
				if (!netStageStartCopyId(plan.mod_track_id, modtrack)
						|| !ae || !ae->occupied || !ae->enabled
						|| ae->type != ASSET_AUDIO
						|| !ae->ext.audio.file_path[0]) {
					return netStageStartReject(&plan,
						NET_STAGE_START_MALFORMED,
						"mod track ID is unavailable or wrong-type");
				}
			} else {
				plan.mod_track_id[0] = '\0';
			}
		}
		snprintf(plan.setup.name, sizeof(plan.setup.name), "server");
	}

	if (src->error) {
		sysLogPrintf(LOG_WARNING, "NET: malformed SVC_STAGE from server");
		return netStageStartReject(&plan, NET_STAGE_START_MALFORMED,
			"truncated game settings");
	}

	// read players
	const u8 numplayers = netbufReadU8(src);
	if (src->error || !numplayers || numplayers > MAX_PLAYERS) {
		sysLogPrintf(LOG_WARNING, "NET: malformed SVC_STAGE from server");
		return netStageStartReject(&plan, NET_STAGE_START_INVALID_ROSTER,
			"player count is zero, truncated, or exceeds engine capacity");
	}
	plan.numplayers = numplayers;

	for (u8 i = 0; i < numplayers; ++i) {
		net_stage_start_client_plan_t *client = &plan.clients[i];
		const u8 id = netbufReadU8(src);
		const u8 playernum = netbufReadU8(src);
		const u8 flags = netbufReadU8(src);
		const u8 team = netbufReadU8(src);
		const u16 options = netbufReadU16(src);
		const u8 handicap = netbufReadU8(src);
		const u16 body_session = catalogReadAssetRef(src);
		const u16 head_session = catalogReadAssetRef(src);
		const f32 fovy = netbufReadF32(src);
		const f32 fovzoommult = netbufReadF32(src);
		const char *name = netbufReadStr(src);
		size_t name_length;
		const asset_entry_t *body_entry;
		const asset_entry_t *head_entry;
		player_identity_status_e identity_status;

		if (src->error || name == NULL) {
			return netStageStartReject(&plan, NET_STAGE_START_MALFORMED,
				"truncated player roster entry");
		}
		name_length = strnlen(name, sizeof(client->name));
		if (name_length == 0 || name_length >= sizeof(client->name)) {
			return netStageStartReject(&plan, NET_STAGE_START_INVALID_ROSTER,
				"player name is empty, oversized, or unterminated");
		}
		if (id >= NET_MAX_CLIENTS) {
			sysLogPrintf(LOG_WARNING, "NET: SVC_STAGE invalid client id %u", id);
			return netStageStartReject(&plan, NET_STAGE_START_INVALID_ROSTER,
				"client ID is outside protocol domain");
		}
		if (playernum >= MAX_PLAYERS || team >= MAX_TEAMS
				|| (flags & ~CLFLAG_ABSENT) != 0) {
			return netStageStartReject(&plan, NET_STAGE_START_INVALID_ROSTER,
				"player slot, team, or participant flags are outside runtime domain");
		}

		for (u8 prior = 0; prior < i; prior++) {
			if (plan.clients[prior].id == id
					|| plan.clients[prior].playernum == playernum) {
				return netStageStartReject(&plan,
					NET_STAGE_START_INVALID_ROSTER,
					"duplicate client ID or player slot");
			}
		}

		if (body_session == 0 || head_session == 0) {
			return netStageStartReject(&plan,
				NET_STAGE_START_INVALID_PLAYER_IDENTITY,
				"player body/head session is zero");
		}
		body_entry = sessionCatalogLocalResolve(body_session);
		head_entry = sessionCatalogLocalResolve(head_session);
		identity_status = playerIdentityPrepareResolved(
			body_entry ? body_entry->id : NULL,
			head_entry ? head_entry->id : NULL,
			body_entry, head_entry, &client->identity);
		if (identity_status != PLAYER_IDENTITY_OK) {
			return netStageStartReject(&plan,
				NET_STAGE_START_INVALID_PLAYER_IDENTITY,
				playerIdentityStatusString(identity_status));
		}

		client->id = id;
		client->playernum = playernum;
		client->flags = flags;
		client->team = team;
		client->handicap = handicap;
		client->options = options;
		client->fovy = fovy;
		client->fovzoommult = fovzoommult;
		memcpy(client->name, name, name_length + 1);
	}

	if (src->error) {
		return netStageStartReject(&plan, NET_STAGE_START_MALFORMED,
			"truncated player roster");
	}
	{
		u64 roster_mask = 0;
		u64 allowed_mask;
		bool runtime_slots[MAX_PLAYERS] = { false };
		bool has_server = false;
		bool has_local = false;
		const s32 represented_slots = MAX_PLAYERS + MAX_BOTS;

		if (g_NetLocalClient == NULL) {
			return netStageStartReject(&plan, NET_STAGE_START_INVALID_ROSTER,
				"local authenticated client is unavailable");
		}
		if (g_MpParticipants.slots == NULL
				|| g_MpParticipants.capacity < represented_slots) {
			return netStageStartReject(&plan,
				NET_STAGE_START_PARTICIPANT_POOL_UNAVAILABLE,
				"participant pool is not initialized at wire capacity");
		}

		for (u8 i = 0; i < plan.numplayers; i++) {
			roster_mask |= 1ull << plan.clients[i].playernum;
			has_server = has_server || plan.clients[i].id == 0;
			has_local = has_local
				|| plan.clients[i].id == g_NetLocalClient->id;
		}
		if (!has_server || !has_local) {
			return netStageStartReject(&plan, NET_STAGE_START_INVALID_ROSTER,
				"roster omits server or authenticated local client");
		}
		for (u8 i = 0; i < plan.numplayers; i++) {
			if (plan.clients[i].id == g_NetLocalClient->id
					&& (plan.clients[i].flags & CLFLAG_ABSENT)) {
				return netStageStartReject(&plan,
					NET_STAGE_START_INVALID_ROSTER,
					"authenticated local client is marked absent");
			}
		}

		allowed_mask = represented_slots == 64
			? ~(u64)0 : ((1ull << represented_slots) - 1ull);
		if (mode == NETGAMEMODE_MP) {
			const u64 player_mask = plan.active_mask
				& ((1ull << MAX_PLAYERS) - 1ull);
			if ((plan.active_mask & ~allowed_mask) != 0
					|| player_mask != roster_mask
					|| netStageStartPopcount64(player_mask) != plan.numplayers) {
				return netStageStartReject(&plan,
					NET_STAGE_START_INVALID_ROSTER,
					"participant mask and player roster disagree");
			}
		} else {
			if (plan.numplayers > 2) {
				return netStageStartReject(&plan,
					NET_STAGE_START_INVALID_ROSTER,
					"co-op/counter-op roster exceeds two players");
			}
			plan.active_mask = roster_mask;
		}

		if (mode == NETGAMEMODE_ANTI) {
			if (plan.numplayers != 2
					|| antiPlayerNumWire == NET_NULL_CLIENT
					|| antiPlayerNumWire >= MAX_PLAYERS
					|| !(roster_mask & (1ull << antiPlayerNumWire))) {
				return netStageStartReject(&plan,
					NET_STAGE_START_INVALID_ROSTER,
					"Counter-Op requires an exact two-player anti slot");
			}
		} else if (antiPlayerNumWire != NET_NULL_CLIENT) {
			return netStageStartReject(&plan, NET_STAGE_START_INVALID_ROSTER,
				"non-Counter-Op stage carries an anti-player slot");
		}

		/* Wire player numbers describe the server's layout.  A client always
		 * owns runtime slot zero, so map the server and local wire entries now
		 * and reject any collision before publishing either layout. */
		for (u8 i = 0; i < plan.numplayers; i++) {
			net_stage_start_client_plan_t *client = &plan.clients[i];
			const s32 mapped = netStageStartMappedPlayernum(&plan, i);

			if (mapped < 0 || mapped >= MAX_PLAYERS
					|| runtime_slots[mapped]) {
				return netStageStartReject(&plan,
					NET_STAGE_START_INVALID_ROSTER,
					"wire-to-runtime player remap is invalid or duplicated");
			}
			client->runtime_playernum = (u8)mapped;
			runtime_slots[mapped] = true;

			if (client->id == 0) {
				plan.bond_playernum = mapped;
			}
			if (mode == NETGAMEMODE_ANTI
					&& client->playernum == antiPlayerNumWire) {
				plan.anti_playernum = mapped;
			}
		}
		if (plan.bond_playernum < 0) {
			return netStageStartReject(&plan, NET_STAGE_START_INVALID_ROSTER,
				"server player has no runtime role mapping");
		}
		if (mode == NETGAMEMODE_COOP && plan.numplayers == 2) {
			for (u8 i = 0; i < plan.numplayers; i++) {
				if (plan.clients[i].runtime_playernum != plan.bond_playernum) {
					plan.coop_playernum = plan.clients[i].runtime_playernum;
					break;
				}
			}
			if (plan.coop_playernum < 0) {
				return netStageStartReject(&plan,
					NET_STAGE_START_INVALID_ROSTER,
					"co-op partner has no runtime role mapping");
			}
		}
		if (mode == NETGAMEMODE_ANTI
				&& (plan.anti_playernum < 0
					|| plan.anti_playernum == plan.bond_playernum)) {
			return netStageStartReject(&plan, NET_STAGE_START_INVALID_ROSTER,
				"Counter-Op Bond and anti roles are not distinct");
		}
	}

	if (mode == NETGAMEMODE_MP) {
		for (s32 botidx = 0; botidx < MAX_BOTS; botidx++) {
			struct mpbotconfig *bot;
			const char *botname;
			u16 body_session;
			u16 head_session;
			u16 profile_session;
			u8 wire_difficulty;
			u8 wire_type;
			u8 wire_team;
			size_t botname_length;
			const asset_entry_t *body_entry;
			const asset_entry_t *head_entry;
			const asset_entry_t *profile_entry;
			const asset_runtime_binding_t *profile;
			player_identity_plan_t identity;
			player_identity_status_e identity_status;

			if (!(plan.active_mask & (1ull << (MAX_PLAYERS + botidx)))) {
				continue;
			}

			botname = netbufReadStr(src);
			body_session = catalogReadAssetRef(src);
			head_session = catalogReadAssetRef(src);
			profile_session = catalogReadAssetRef(src);
			wire_difficulty = netbufReadU8(src);
			wire_type = netbufReadU8(src);
			wire_team = netbufReadU8(src);
			botname_length = botname
				? strnlen(botname, sizeof(bot->base.name)) : 0;
			if (src->error || botname == NULL || botname_length == 0
					|| botname_length >= sizeof(bot->base.name)
					|| body_session == 0
					|| head_session == 0 || profile_session == 0
					|| wire_team >= MAX_TEAMS) {
				return netStageStartReject(&plan, NET_STAGE_START_MALFORMED,
					"truncated bot record or zero typed session reference");
			}

			body_entry = sessionCatalogLocalResolve(body_session);
			head_entry = sessionCatalogLocalResolve(head_session);
			identity_status = playerIdentityPrepareResolved(
				body_entry ? body_entry->id : NULL,
				head_entry ? head_entry->id : NULL,
				body_entry, head_entry, &identity);
			if (identity_status != PLAYER_IDENTITY_OK) {
				return netStageStartReject(&plan,
					NET_STAGE_START_INVALID_BOT_IDENTITY,
					playerIdentityStatusString(identity_status));
			}

			profile_entry = sessionCatalogLocalResolve(profile_session);
			profile = profile_entry && profile_entry->type == ASSET_BOT_PROFILE
				? mpBotProfileRuntimeBindingById(profile_entry->id) : NULL;
			if (!profile_entry || !profile_entry->occupied || !profile_entry->enabled
					|| !profile || !profile->active || !profile->source_hydrated
					|| profile->type != ASSET_BOT_PROFILE
					|| strcmp(profile->id, profile_entry->id) != 0
					|| !assetRuntimePrimaryFileAccessible(profile)
					|| profile->bot_profile_type < 0
					|| profile->bot_profile_type > 255
					|| profile->bot_profile_difficulty < 0
					|| profile->bot_profile_difficulty > 255) {
				return netStageStartReject(&plan,
					NET_STAGE_START_INVALID_BOT_PROFILE,
					"bot profile session has no exact hydrated runtime binding");
			}

			bot = &plan.bots[botidx];
			*bot = g_BotConfigsArray[botidx];
			bot->base.mpbodynum = identity.mp_body_index >= 0
				? (u8)identity.mp_body_index : 0xFF;
			bot->base.mpheadnum = identity.mp_head_index >= 0
				? (u8)identity.mp_head_index : 0xFF;
			strncpy(bot->base.body_id, identity.body_id,
				sizeof(bot->base.body_id) - 1);
			bot->base.body_id[sizeof(bot->base.body_id) - 1] = '\0';
			strncpy(bot->base.head_id, identity.head_id,
				sizeof(bot->base.head_id) - 1);
			bot->base.head_id[sizeof(bot->base.head_id) - 1] = '\0';
			memcpy(bot->base.name, botname, botname_length + 1);
			strncpy(bot->profile_id, profile_entry->id,
				sizeof(bot->profile_id) - 1);
			bot->profile_id[sizeof(bot->profile_id) - 1] = '\0';
			bot->difficulty = (u8)profile->bot_profile_difficulty;
			bot->type = (u8)profile->bot_profile_type;
			bot->base.team = wire_team;
			plan.bot_active[botidx] = true;

			if (wire_difficulty != bot->difficulty || wire_type != bot->type) {
				return netStageStartReject(&plan,
					NET_STAGE_START_INVALID_BOT_PROFILE,
					"bot wire traits disagree with public profile");
			}
		}
	}

	if (src->error) {
		return netStageStartReject(&plan, NET_STAGE_START_MALFORMED,
			"truncated bot roster");
	}

	/* Freeze the exact validated wire roster before publishing any stage
	 * globals. Runtime slots are receiver-local, so use the plan's prepared
	 * client-ID -> runtime mapping rather than mutable netclient fields. */
	{
		net_cutscene_participant_t participants[MAX_PLAYERS];
		for (u8 i = 0; i < plan.numplayers; i++) {
			participants[i].client_id = plan.clients[i].id;
			participants[i].runtime_playernum =
				plan.clients[i].runtime_playernum;
		}
		if (!netmsgCutsceneAuthorityBeginMatch(srccl->room_id, participants,
				plan.numplayers)) {
			return netStageStartReject(&plan,
				NET_STAGE_START_INVALID_ROSTER,
				"cutscene authority roster could not be frozen");
		}
	}

	/* One publication point after the complete packet, roster, typed assets,
	 * and derived runtime bindings have validated. */
	g_NetTick = plan.net_tick;
	g_NetRngSeeds[0] = plan.rng_seed_0;
	g_NetRngSeeds[1] = plan.rng_seed_1;
	g_NetRngLatch = true;
	g_NetMatchSeed = plan.match_seed;
	g_NetStageEpoch = plan.stage_epoch;
	netNpcReplicationReset();
	g_NetGameMode = plan.mode;
	g_NetCoopFriendlyFire = plan.coop_friendly_fire;
	g_NetCoopRadar = plan.coop_radar;

	if (mode == NETGAMEMODE_MP) {
		g_MpSetup = plan.setup;
		strncpy(g_MatchConfig.scenario_id, plan.scenario_id,
			sizeof(g_MatchConfig.scenario_id) - 1);
		g_MatchConfig.scenario_id[sizeof(g_MatchConfig.scenario_id) - 1] = '\0';
		g_MatchConfig.scenario = plan.setup.scenario;
		strncpy(g_MatchConfig.stage_id, plan.stage_id,
			sizeof(g_MatchConfig.stage_id) - 1);
		g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
		g_MatchConfig.stagenum = plan.stagenum;
		g_MatchConfig.timelimit = plan.setup.timelimit;
		g_MatchConfig.scorelimit = plan.setup.scorelimit;
		g_MatchConfig.teamscorelimit = plan.setup.teamscorelimit;
		matchConfigReplaceUserOptions(plan.setup.options, "SVC_STAGE_START");
		g_MatchConfig.weaponSetIndex = -1;
		memcpy(g_MatchConfig.weapon_ids, plan.weapon_ids,
			sizeof(g_MatchConfig.weapon_ids));
		memcpy(g_MatchConfig.weapons, plan.setup.weapons,
			sizeof(g_MatchConfig.weapons));
		mpCommitPreparedWeaponSet(WEAPONSET_CUSTOM, plan.setup.weapons);
		strncpy(g_MatchConfig.spawn_weapon_id, plan.spawn_weapon_id,
			sizeof(g_MatchConfig.spawn_weapon_id) - 1);
		g_MatchConfig.spawn_weapon_id[
			sizeof(g_MatchConfig.spawn_weapon_id) - 1] = '\0';
		g_MatchConfig.spawnWeaponMode = plan.spawn_weapon_mode;
		g_MatchConfig.spawnWeaponNum = plan.spawn_weapon_num;
#if !defined(PD_SERVER)
		if (plan.mod_track_id[0]) {
			audioSetModTrackId(plan.mod_track_id);
		}
#endif
	} else {
		g_MissionConfig = plan.mission;
	}

	for (s32 i = 0; i < g_NetMaxClients; i++) {
		if (g_NetClients[i].state == CLSTATE_GAME
				|| g_NetClients[i].state == CLSTATE_PREPARING) {
			g_NetClients[i].state = CLSTATE_LOBBY;
			g_NetClients[i].player = NULL;
			g_NetClients[i].config = NULL;
		}
	}
	for (u8 i = 0; i < plan.numplayers; i++) {
		const net_stage_start_client_plan_t *prepared = &plan.clients[i];
		struct netclient *client = &g_NetClients[prepared->id];
		struct mpplayerconfig *config =
			&g_PlayerConfigsArray[prepared->runtime_playernum];
		client->id = prepared->id;
		client->playernum = prepared->playernum;
		client->settings.team = prepared->team;
		client->settings.handicap = prepared->handicap;
		client->settings.options = prepared->options;
		client->settings.fovy = prepared->fovy;
		client->settings.fovzoommult = prepared->fovzoommult;
		strncpy(client->settings.name, prepared->name,
			sizeof(client->settings.name) - 1);
		client->settings.name[sizeof(client->settings.name) - 1] = '\0';
		strncpy(client->settings.body_id, prepared->identity.body_id,
			sizeof(client->settings.body_id) - 1);
		client->settings.body_id[sizeof(client->settings.body_id) - 1] = '\0';
		strncpy(client->settings.head_id, prepared->identity.head_id,
			sizeof(client->settings.head_id) - 1);
		client->settings.head_id[sizeof(client->settings.head_id) - 1] = '\0';
		client->flags = prepared->flags;
		client->state = CLSTATE_GAME;
		client->stage_ready = false;
		client->player = NULL;
		client->config = NULL;

		config->base.team = prepared->team;
		config->handicap = prepared->handicap;
		config->base.mpbodynum = prepared->identity.mp_body_index >= 0
			? (u8)prepared->identity.mp_body_index : 0xFF;
		config->base.mpheadnum = prepared->identity.mp_head_index >= 0
			? (u8)prepared->identity.mp_head_index : 0xFF;
		strncpy(config->base.body_id, prepared->identity.body_id,
			sizeof(config->base.body_id) - 1);
		config->base.body_id[sizeof(config->base.body_id) - 1] = '\0';
		strncpy(config->base.head_id, prepared->identity.head_id,
			sizeof(config->base.head_id) - 1);
		config->base.head_id[sizeof(config->base.head_id) - 1] = '\0';
	}
	g_NetNumClients = plan.numplayers;
	mpParticipantsDecodeActiveMask(plan.active_mask);
	for (s32 botidx = 0; botidx < MAX_BOTS; botidx++) {
		if (plan.bot_active[botidx]) {
			g_BotConfigsArray[botidx] = plan.bots[botidx];
		}
	}

	g_Vars.bondplayernum = plan.bond_playernum;
	g_Vars.coopplayernum = plan.coop_playernum;
	g_Vars.antiplayernum = plan.anti_playernum;

	plan.status = NET_STAGE_START_OK;
	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.COMMIT stage-start mode=%u stage='%s' players=%u bots=%d epoch=%u",
		(unsigned)plan.mode, plan.stage_id, (unsigned)plan.numplayers,
		netStageStartPopcount64(plan.active_mask >> MAX_PLAYERS),
		(unsigned)plan.stage_epoch);
	/* The reconnect credential remains retryable through the asynchronous stage
	 * load. pdmain emits CLC_STAGE_READY and retires it only after lvReset and
	 * player allocation reach the real SCENE_EVENT_STAGE_READY boundary. */

	if (mode == NETGAMEMODE_COOP || mode == NETGAMEMODE_ANTI) {
		sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE from server: co-op stage 0x%02x difficulty %u with %u players",
			stagenum, g_MissionConfig.difficulty, numplayers);

		/* Dismiss countdown overlay before changing stage. */
		memset(&g_MatchCountdownState, 0, sizeof(g_MatchCountdownState));
		menuStop();
#if !defined(PD_SERVER)
		/* Phase 2 / Priority K-b3: pool slot cleanup is sufficient.
		 * menupoolReleaseAll pops every owned ctx (including unregistered-
		 * fallback after K-b1), so the legacy paired
		 * inputCtxPopDeferred(&g_CtxImGuiMenu) is no longer needed. */
		sceneStageTransitionPrepare(SCENE_STAGE_TRANSITION_RELEASE_MENU_POOL,
			"SVC_STAGE_START coop");
#endif

		g_NotLoadMod = true;
		romdataFileFreeForSolo();

		titleSetNextStage(stagenum);
		setNumPlayers(plan.numplayers);
		lvSetDifficulty(g_MissionConfig.difficulty);
		titleSetNextMode(TITLEMODE_SKIP);
		sysLogPrintf(LOG_WARNING, "MATCH-START: calling mainChangeToStage, stagenum=0x%x (co-op)", (unsigned)stagenum);
		mainChangeToStage(stagenum);

#if VERSION >= VERSION_NTSC_1_0
		viBlack(true);
#endif
	} else {
		sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE from server: going to stage 0x%02x with %u players activeMask=0x%llx",
			g_MpSetup.stagenum, numplayers,
			(unsigned long long)mpParticipantsEncodeActiveMask());

		/* The validated participant pool and exact player/bot identities were
		 * published at the single commit boundary above.  mpStartMatch does not
		 * rebuild participants for NETMODE_CLIENT. */

		sysLogPrintf(LOG_WARNING, "MATCH-START: calling mainChangeToStage, stagenum=0x%x", (unsigned)g_MpSetup.stagenum);
		mpStartMatch();
#if !defined(PD_SERVER)
		/* scenarioInitProps() configures gameplay flags on props (doors, pickups, etc.)
		 * via the scenario's initpropsfunc.  On the server/solo paths this is driven by
		 * setup.c's g_Vars.normmplayerisrunning guard; on a networked client that guard
		 * can be false at stage-init time, so we call it explicitly here. */
		scenarioInitProps();
#endif
		/* Dismiss countdown overlay before going in-game. */
		memset(&g_MatchCountdownState, 0, sizeof(g_MatchCountdownState));
		menuStop();
#if !defined(PD_SERVER)
		/* Phase 2 / Priority K-b3: pool slot cleanup is sufficient -- bulk
		 * release pops every owned ctx (incl. unregistered-fallback after
		 * K-b1), so no paired direct ctx pop is needed. */
		sceneStageTransitionPrepare(SCENE_STAGE_TRANSITION_RELEASE_MENU_POOL,
			"SVC_STAGE_START combat");
#endif
	}

	return 0;
}

u32 netmsgSvcStageEndWrite(struct netbuf *dst, u8 room_id, u8 mode)
{
	netbufWriteU8(dst, SVC_STAGE_END);
	netbufWriteU8(dst, mode);

	sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE_END prepared mode=%u room=%u",
		(unsigned)mode, (unsigned)room_id);
	return dst->error;
}

void netmsgSvcStageEndCommit(u8 room_id, u8 mode)
{
	g_NetStageEpoch = 0;
	netNpcReplicationReset();
	/* A reconnect gameplay witness belongs only to the restored live stage. */
	for (s32 i = 0; i < g_NetMaxClients; ++i) {
		g_NetClients[i].reconnect_resync_pending = false;
		g_NetClients[i].reconnect_gameplay_witness_pending = false;
	}
	if (mode == NETGAMEMODE_COOP || mode == NETGAMEMODE_ANTI) {
		// Co-op/anti: keep player linkages intact for endscreen display.
		// The endscreen needs g_Vars.bond, g_Vars.coop, and player stats.
		// Client state transitions happen via mainEndStage -> endscreenPushCoop.
	} else {
		// MP: unlink players in this room and return them to lobby
		for (s32 i = 0; i < g_NetMaxClients; ++i) {
			struct netclient *ncl = &g_NetClients[i];
			if (ncl->state && ncl->room_id == room_id) {
				ncl->state = CLSTATE_LOBBY;
				ncl->playernum = 0;
				if (ncl->player) {
					ncl->player->client = NULL;
					ncl->player->isremote = false;
					ncl->player = NULL;
				}
				if (ncl->config) {
					ncl->config->client = NULL;
					ncl->config = NULL;
				}
			}
		}
	}
	sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE_END committed mode=%u room=%u",
		(unsigned)mode, (unsigned)room_id);
}

u32 netmsgSvcStageEndRead(struct netbuf *src, struct netclient *srccl)
{
	u8 mode = netbufReadU8(src);

	if (src->error) {
		return src->error;
	}
	/* A partial room send is retried as the same reliable END + STAGE_END
	 * packet. Peers that accepted the first attempt consume the duplicate
	 * stage boundary idempotently after their authority context is retired. */
	if (!netmsgCutsceneAuthorityHasMatch()) {
		sysLogPrintf(LOG_NOTE,
			"NET: SVC_STAGE_END ignored duplicate_or_stale mode=%u",
			(unsigned)mode);
		return 0;
	}

	sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE_END read mode=%u path=%s", mode,
		(mode == NETGAMEMODE_COOP || mode == NETGAMEMODE_ANTI) ? "co-op" : "mp");
	playerResetAllCutsceneStates();
	netmsgCutsceneAuthorityReset();

	/* S303 bookend: SVC_STAGE_END receive is the canonical end-of-match
	 * teardown on the client side. Tag with mode so the log clearly marks
	 * which endscreen path mainEndStage will enter below. */
	sysLogPrintf(LOG_NOTE,
		"GAMELOOP.%s: SVC_STAGE_END received — entering endscreen teardown (manifest=%d)",
		(mode == NETGAMEMODE_COOP) ? "COOP"
			: (mode == NETGAMEMODE_ANTI) ? "COUNTEROP"
			: "CAMPAIGN",
		g_ClientManifest.num_entries);

	if (mode == NETGAMEMODE_COOP || mode == NETGAMEMODE_ANTI) {
		// Co-op/anti: transition to endscreen with players intact.
		// Disable further objective re-evaluation on client
		// (server has already determined mission outcome).
		objectivesDisableChecking();
		mainEndStage();
	} else {
		/* L-CE1: only disconnect peers in the same room as the local client */
		u8 local_room = g_NetLocalClient ? g_NetLocalClient->room_id : 0xFF;
		for (s32 i = 0; i < g_NetMaxClients; ++i) {
			struct netclient *ncl = &g_NetClients[i];
			if (ncl->state && ncl->room_id == local_room) {
				ncl->state = CLSTATE_DISCONNECTED;
				ncl->player = NULL;
				ncl->config = NULL;
			}
		}

		if (g_NetLocalClient) {
			g_NetLocalClient->state = CLSTATE_LOBBY;
		}

		g_NumReasonsToEndMpMatch = 1;
		mainEndStage();
	}

	/* SA-1: tear down session catalog on match end (client-side). */
	sessionCatalogTeardown();

	/* L1-1: clear stale client manifest so no match-N assets leak into match N+1.
	 * The manifest is fully rebuilt when SVC_MATCH_MANIFEST arrives for the next
	 * match.  Without this, any code path that triggers mainChangeToStage() between
	 * matches could load the wrong assets from the previous manifest. */
	sceneStageTransitionPrepare(SCENE_STAGE_TRANSITION_CLEAR_CLIENT_MANIFEST,
		"SVC_STAGE_END");

	/* Bot authority relinquished at match end — next match will re-assign */
	g_NetLocalBotAuthority = false;
	g_NetPendingBotAuthority = false;

	/* GAP-4 / SP-14: mirror the server-side reset at net.c:876 so the client's
	 * view of the "match room" is cleared once the match actually ends.
	 * Without this, stale match_room_id survives into the lobby state and
	 * any code path that branches on g_NetMatchRoomId != 0xFF misbehaves. */
	{
		extern u8 g_NetMatchRoomId;
		g_NetMatchRoomId = 0xFF;
	}

	return src->error;
}

u32 netmsgSvcPlayerMoveWrite(struct netbuf *dst, struct netclient *movecl)
{
	if (movecl->state < CLSTATE_GAME || !movecl->player || !movecl->player->prop) {
		return dst->error;
	}

	netbufWriteU8(dst, SVC_PLAYER_MOVE);
	netbufWriteU8(dst, movecl->id);
	netbufWriteU32(dst, movecl->inmove[0].tick);
	netbufWritePlayerMove(dst, &movecl->outmove[0]);
	if (movecl->outmove[0].ucmd & UCMD_FL_FORCEMASK) {
		netbufWriteRooms(dst, movecl->player->prop->rooms, ARRAYCOUNT(movecl->player->prop->rooms));
	}

	return dst->error;
}

static u32 netmsgSvcPlayerMoveSnapshotWrite(struct netbuf *dst,
		const struct netclient *movecl)
{
	struct netplayermove move;
	const struct player *player;

	if (!dst || !movecl || movecl->state != CLSTATE_GAME
			|| movecl->id >= NET_MAX_CLIENTS || !movecl->player
			|| !movecl->player->prop || !movecl->player->prop->chr) {
		return 1;
	}
	player = movecl->player;
	move = movecl->outmove[0];
	move.tick = g_NetTick;
	/* A reconnect snapshot is state, never a replay of one-shot gameplay
	 * commands. Carry only force bits so fire/use/reload cannot execute twice. */
	move.ucmd = UCMD_FL_FORCEPOS | UCMD_FL_FORCEANGLE
		| UCMD_FL_FORCEGROUND;
	move.crouchofs = player->crouchoffset;
	move.leanofs = player->swaytarget / 75.0f;
	move.movespeed[0] = player->speedforwards;
	move.movespeed[1] = player->speedsideways;
	move.angles[0] = player->vv_theta;
	move.angles[1] = player->vv_verta;
	move.weaponnum = (s8)player->gunctrl.weaponnum;
	move.pos = player->prop->pos;
	move.crosspos[0] = player->crosspos[0];
	move.crosspos[1] = player->crosspos[1];
	if (!player->isremote) {
		move.crosspos[0] -= (f32)(SCREEN_WIDTH_LO / 2);
		move.crosspos[0] = (f32)(SCREEN_WIDTH_LO / 2)
			+ move.crosspos[0] * player->aspect / SCREEN_ASPECT;
	}

	netbufWriteU8(dst, SVC_PLAYER_MOVE);
	netbufWriteU8(dst, (u8)movecl->id);
	netbufWriteU32(dst, movecl->inmove[0].tick);
	netbufWritePlayerMove(dst, &move);
	netbufWriteRooms(dst, player->prop->rooms,
		ARRAYCOUNT(player->prop->rooms));
	return dst->error;
}

u32 netmsgSvcPlayerMoveRead(struct netbuf *src, struct netclient *srccl)
{
	u8 id = 0;
	u32 outmoveack = 0;
	struct netplayermove newmove;
	RoomNum newrooms[8] = { -1 };

	id = netbufReadU8(src);
	outmoveack = netbufReadU32(src);
	netbufReadPlayerMove(src, &newmove);
	if (newmove.ucmd & UCMD_FL_FORCEMASK) {
		netbufReadRooms(src, newrooms, ARRAYCOUNT(newrooms));
	}

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	if (id >= NET_MAX_CLIENTS) {
		sysLogPrintf(LOG_WARNING, "NET: PlayerMove: client id %u out of range", id);
		return 1;
	}

	struct netclient *movecl = &g_NetClients[id];
	if (movecl->state < CLSTATE_GAME) {
		return src->error;
	}

	// make space in the move stack
	memmove(movecl->inmove + 1, movecl->inmove, sizeof(movecl->inmove) - sizeof(*movecl->inmove));
	movecl->inmove[0] = newmove;
	movecl->outmoveack = outmoveack;
	movecl->lerpticks = 0;

	if (movecl == g_NetLocalClient && (newmove.ucmd & UCMD_FL_FORCEMASK)) {
		// server wants to teleport us
		if (movecl->player && movecl->player->prop) {
			chrSetPos(movecl->player->prop->chr, &newmove.pos, newrooms, newmove.angles[0], (newmove.ucmd & UCMD_FL_FORCEGROUND) != 0);
		}
	}

	return src->error;
}

/* ── Weapon wire helpers ─────────────────────────────────────────────────
 * Write/read a WEAPON_* enum as a catalog session reference (2 bytes).
 * Used by SVC_PLAYER_STATS, SVC_PROP_SPAWN, SVC_PROP_DAMAGE,
 * SVC_CHR_DISARM, SVC_CHR_STATE, and SVC_CHR_RESYNC.
 *
 * The catalog stores ext.weapon.weapon_id as MPWEAPON_* slots, while
 * catalog_weapon_result_t.weapon_num carries the final runtime WEAPON_* enum. */

static void netWriteWeaponRef(struct netbuf *dst, s32 weaponnum)
{
#if !defined(PD_SERVER)
	const char *wid = catalogWeaponIdByRuntimeWeaponNum(weaponnum);
	catalogWriteAssetRef(dst, wid ? sessionCatalogGetId(wid) : 0);
#else
	catalogWriteAssetRef(dst, 0);
#endif
}

static s32 netReadWeaponRef(struct netbuf *src)
{
	u16 wsession = catalogReadAssetRef(src);
	if (wsession == 0) {
		return WEAPON_UNARMED;
	}
#if !defined(PD_SERVER)
	catalog_weapon_result_t wr;
	if (catalogResolveWeaponBySession(wsession, &wr)) {
		return wr.weapon_num;
	}
#endif
	return WEAPON_UNARMED;
}

/* Write a model number (g_ModelStates[] index) as a session catalog u16 ref.
 * All g_ModelStates entries are registered as ASSET_MODEL in the catalog. */
static void netWriteModelRef(struct netbuf *dst, s32 modelnum)
{
	const char *id = catalogModelIdByModelnum(modelnum);
	catalogWriteAssetRef(dst, id ? sessionCatalogGetId(id) : 0);
}

/* Read a session catalog u16 ref and resolve back to a model number.
 * Returns 0 if unresolvable (caller should handle gracefully). */
static s32 netReadModelRef(struct netbuf *src)
{
	const u16 session = catalogReadAssetRef(src);
	if (session == 0) {
		return 0;
	}
	const asset_entry_t *e = sessionCatalogLocalResolve(session);
	if (e && e->type == ASSET_MODEL) {
		return (s32)e->runtime_index;
	}
	return 0;
}

u32 netmsgSvcPlayerStatsWrite(struct netbuf *dst, struct netclient *actcl)
{
	if (actcl->state < CLSTATE_GAME || !actcl->player || !actcl->player->prop || !actcl->player->prop->chr) {
		return dst->error;
	}
	const struct player *pl = actcl->player;
	const u8 flags = (pl->isdead != 0) | (pl->gunctrl.dualwielding << 1) |
		(pl->hands[0].inuse << 2) | (pl->hands[1].inuse << 3);
	netbufWriteU8(dst, SVC_PLAYER_STATS);
	netbufWriteU8(dst, actcl->id);
	netbufWriteU8(dst, flags);
	netWriteWeaponRef(dst, pl->gunctrl.weaponnum);
	netbufWriteF32(dst, pl->prop->chr->damage);
	netbufWriteF32(dst, pl->bondhealth);
	netbufWriteF32(dst, pl->prop->chr->cshield);
	netbufWriteCoord(dst, &pl->bondshotspeed);

	for (s32 i = 0; i < 2; ++i) {
		if (pl->hands[i].inuse) {
			netbufWriteS16(dst, pl->hands[i].loadedammo[0]);
			netbufWriteS16(dst, pl->hands[i].loadedammo[1]);
		}
	}

	// assemble a bitmask of all non-zero ammo entries below 32
	u32 mask = 0;
	for (u32 i = 0; i < 32; ++i) {
		if (pl->ammoheldarr[i]) {
			mask |= (1 << i);
		}
	}

	netbufWriteU32(dst, mask);

	// write all entries that are non-zero or have index above 32
	for (s32 i = 0; i < ARRAYCOUNT(pl->ammoheldarr); ++i) {
		if (pl->ammoheldarr[i] || i >= 32) {
			netbufWriteS16(dst, pl->ammoheldarr[i]);
		}
	}

	/* B-256 structural fix (v43, 2026-04-25): authoritative attacker
	 * identity on the wire. The dying chr's lastattacker pointer is
	 * resolved to a player index here at write time on the producing
	 * peer (where the chr-data is canonical), then transmitted so the
	 * receiver can attribute the kill without re-resolving local state
	 * that may not yet reflect the death event. -1 sentinel when the
	 * attacker is not a player (NULL lastattacker, world damage,
	 * out-of-bounds, suicide) -- receiver folds that to currentplayernum
	 * via the legacy fallback path. Mandatory in v43; mixed-version play
	 * is rejected at the auth handshake. */
	{
		s8 attacker_id = -1;
		if (pl->prop->chr->lastattacker) {
			s32 idx = mpPlayerGetIndex(pl->prop->chr->lastattacker);
			if (idx >= 0 && idx <= 127) {
				attacker_id = (s8)idx;
			}
		}
		netbufWriteS8(dst, attacker_id);
	}

	return dst->error;
}

u32 netmsgSvcPlayerStatsRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 clid = netbufReadU8(src);
	const u8 flags = netbufReadU8(src);
	const s32 newweaponnum = netReadWeaponRef(src);
	const f32 newdamage = netbufReadF32(src);
	const f32 newhealth = netbufReadF32(src);
	const f32 newshield = netbufReadF32(src);
	struct coord newshotspeed; netbufReadCoord(src, &newshotspeed);
	const bool handused[2] = { (flags & (1 << 2)) != 0, (flags & (1 << 3)) != 0 };

	if (src->error || clid >= NET_MAX_CLIENTS + 1) {
		return src->error ? src->error : 1;
	}

	struct netclient *actcl = g_NetClients + clid;
	if (actcl->state < CLSTATE_GAME) {
		return 1;
	}

	struct player *pl = actcl->player;
	if (!pl || !pl->prop || !pl->prop->chr) {
		return src->error;
	}

	pl->prop->chr->damage = newdamage;
	pl->prop->chr->cshield = newshield;
	pl->bondhealth = newhealth;
	pl->bondshotspeed = newshotspeed;

	for (s32 i = 0; i < 2; ++i) {
		if (handused[i]) {
			pl->hands[i].loadedammo[0] = netbufReadS16(src);
			pl->hands[i].loadedammo[1] = netbufReadS16(src);
		}
	}

	const u32 ammomask = netbufReadU32(src);
	for (s32 i = 0; i < ARRAYCOUNT(pl->ammoheldarr); ++i) {
		if (i >= 32 || (ammomask & (1 << i))) {
			pl->ammoheldarr[i] = netbufReadS16(src);
		} else {
			pl->ammoheldarr[i] = 0;
		}
	}

	/* B-256 v43 (2026-04-25): authoritative attacker identity from the
	 * wire. The producing peer resolved mpPlayerGetIndex(lastattacker)
	 * at write time and shipped it here. This bypasses the local-state
	 * resolution race that the v42 fix at this site relied on (chrdata
	 * may not yet reflect the attacker pointer when the death packet
	 * arrives if events are reordered relative to chr-state replication).
	 * Mandatory field in v43; mixed-version play rejected at handshake. */
	const s8 wire_attacker_id = netbufReadS8(src);

	const s32 prevplayernum = g_Vars.currentplayernum;

	if (actcl->playernum >= MAX_PLAYERS) {
		sysLogPrintf(LOG_WARNING, "NET: SvcPlayerStatsRead invalid playernum %d", actcl->playernum);
		return 1;
	}

	setCurrentPlayerNum(actcl->playernum);

	const bool newisdead = (flags & (1 << 0)) != 0;
	if (!pl->isdead && newisdead) {
		/* B-256 v43 dispatch:
		 *   1. Wire field authoritative when in range and != self.  This
		 *      is the new structural path: the producing peer already
		 *      resolved the attacker so receivers don't re-resolve from
		 *      potentially-stale local chr-state.
		 *   2. Fallback to the local-resolve path (the v42 third-site
		 *      closure: mpPlayerGetIndex on lastattacker pointer, then
		 *      currentplayernum) when the wire reports -1 sentinel or a
		 *      self-attribution, since "self" on the wire would be a
		 *      suicide and the legacy path encodes that as
		 *      currentplayernum == self anyway.
		 * Both layers exist now: wire-authoritative for normal kills,
		 * local-fallback for v42 grace and edge cases (suicides, world
		 * damage, NULL attacker). */
		s16 shooter;
		if (wire_attacker_id >= 0 && (s32)wire_attacker_id != g_Vars.currentplayernum) {
			shooter = (s16)wire_attacker_id;
		} else if (pl->prop->chr->lastattacker) {
			shooter = (s16)mpPlayerGetIndex(pl->prop->chr->lastattacker);
			if (shooter < 0) shooter = g_Vars.currentplayernum;
		} else {
			shooter = g_Vars.currentplayernum;
		}
		playerDieByShooter(shooter, true);
	} else if (pl->isdead && !newisdead) {
		playerStartNewLife();
	}

	const bool dualwielding = (flags & (1 << 1)) != 0;
	if (!pl->isdead && newweaponnum >= WEAPON_UNARMED && newweaponnum < WEAPON_CUSTOM_END
			&& (newweaponnum != pl->gunctrl.weaponnum || dualwielding != pl->gunctrl.dualwielding)) {
		pl->gunctrl.dualwielding = dualwielding;
		bgunEquipWeapon(newweaponnum);
	}

	setCurrentPlayerNum(prevplayernum);

	return src->error;
}

static s32 netmsgReconnectPropExpectedIndex(u32 syncid)
{
	for (u16 i = 0; i < s_ReconnectPropReceive.expected_count; ++i) {
		if (s_ReconnectPropReceive.expected_ids[i] == syncid) {
			return (s32)i;
		}
	}
	return -1;
}

static void netmsgReconnectRemoveWorldProp(struct prop *prop)
{
	if (!netmsgReconnectIsWorldProp(prop)) {
		return;
	}
	if (prop->obj) {
		objFreePermanently(prop->obj, true);
		return;
	}
	if (prop->active) {
		propDeregisterRooms(prop);
	}
	propDelist(prop);
	propDisable(prop);
	propFree(prop);
}

static u32 netmsgReconnectApplyProjectile(struct prop *prop,
		const netmsg_reconnect_prop_wire_t *wire)
{
	struct defaultobj *obj;
	struct projectile *projectile = NULL;

	if (!prop || !prop->obj || !wire) {
		return 1;
	}
	obj = prop->obj;
	objFreeEmbedmentOrProjectile(prop);
	obj->hidden = wire->hidden
		& ~(OBJHFLAG_PROJECTILE | OBJHFLAG_EMBEDDED);

	if (wire->projectile.kind == NET_RECONNECT_PROJECTILE_DIRECT) {
		projectile = projectileAllocate();
		if (!projectile) {
			return 1;
		}
		obj->projectile = projectile;
		obj->hidden |= OBJHFLAG_PROJECTILE;
	} else if (wire->projectile.kind == NET_RECONNECT_PROJECTILE_EMBEDDED
			|| wire->projectile.kind
				== NET_RECONNECT_PROJECTILE_EMBEDDED_EMPTY) {
		struct embedment *embedment = embedmentAllocate();
		if (!embedment) {
			return 1;
		}
		if (wire->projectile.kind == NET_RECONNECT_PROJECTILE_EMBEDDED) {
			projectile = projectileAllocate();
			if (!projectile) {
				embedmentFree(embedment);
				return 1;
			}
		}
		embedment->flags = wire->projectile.embed_flags;
		embedment->matrix = wire->projectile.embed_matrix;
		embedment->projectile = projectile;
		obj->embedment = embedment;
		obj->hidden |= OBJHFLAG_EMBEDDED;
	} else if (wire->projectile.kind != NET_RECONNECT_PROJECTILE_NONE) {
		return 1;
	}

	if (!projectile) {
		return 0;
	}
	projectile->flags = wire->projectile.flags;
	projectile->speed = wire->projectile.speed;
	projectile->nextsteppos = wire->projectile.nextsteppos;
	projectile->unk010 = wire->projectile.unk010;
	projectile->unk014 = wire->projectile.unk014;
	projectile->unk018 = wire->projectile.unk018;
	projectile->unk08c = wire->projectile.unk08c;
	projectile->unk098 = wire->projectile.unk098;
	projectile->unk0dc = wire->projectile.unk0dc;
	projectile->unk0e0 = wire->projectile.unk0e0;
	projectile->unk0e4 = wire->projectile.unk0e4;
	projectile->unk0ec = wire->projectile.unk0ec;
	projectile->unk0f0 = wire->projectile.unk0f0;
	projectile->bouncecount = wire->projectile.bouncecount;
	projectile->bounceframe = wire->projectile.bounceframe;
	projectile->flighttime240 = wire->projectile.flighttime240;
	projectile->pickuptimer240 = wire->projectile.pickuptimer240;
	projectile->losttimer240 = wire->projectile.losttimer240;
	projectile->smoketimer240 = wire->projectile.smoketimer240;
	projectile->droptype = wire->projectile.droptype;
	projectile->powerlimit240 = wire->projectile.powerlimit240;
	projectile->mtx = wire->projectile.mtx;
	projectile->hominggain = wire->projectile.hominggain;
	projectile->homingdamping = wire->projectile.homingdamping;
	projectile->homingpreverr = wire->projectile.homingpreverr;
	projectile->flyturnrate = wire->projectile.flyturnrate;
	projectile->flyaccel = wire->projectile.flyaccel;
	projectile->flyproxradius = wire->projectile.flyproxradius;
	projectile->flymaxaltitude = wire->projectile.flymaxaltitude;
	projectile->flylosttimeout240 = wire->projectile.flylosttimeout240;
	projectile->flysmokeinterval240 = wire->projectile.flysmokeinterval240;
	projectile->graphtrailsmoketype = wire->projectile.graphtrailsmoketype;
	projectile->graphtrailinterval240 =
		wire->projectile.graphtrailinterval240;
	projectile->ownerprop = NULL;
	projectile->targetprop = NULL;
	projectile->pickupby = NULL;
	projectile->obj = obj;
	return 0;
}

static u32 netmsgSvcReconnectPropStateWrite(struct netbuf *dst,
		const struct prop *prop, net_reconnect_snapshot_result_t *result)
{
	u8 detail = NET_RECONNECT_PROP_DETAIL_NONE;
	s16 attachment_mtx_index = -1;

	if (!dst || !netmsgReconnectIsWorldProp(prop)) {
		return netmsgReconnectSnapshotFailProp(result,
			NET_RECONNECT_SNAPSHOT_WORLD_PROP_STATE, prop,
			NET_RECONNECT_PROP_STATE_INVALID_ARGUMENT, -1);
	}
	if (prop->obj->model && prop->obj->model->scale <= 0.0f) {
		return netmsgReconnectSnapshotFailProp(result,
			NET_RECONNECT_SNAPSHOT_WORLD_PROP_STATE, prop,
			NET_RECONNECT_PROP_STATE_MODEL_SCALE_NONPOSITIVE, -1);
	}
	if (prop->obj->model
			&& prop->obj->model->scale != prop->obj->model->scale) {
		return netmsgReconnectSnapshotFailProp(result,
			NET_RECONNECT_SNAPSHOT_WORLD_PROP_STATE, prop,
			NET_RECONNECT_PROP_STATE_MODEL_SCALE_NAN, -1);
	}
	if (prop->obj->model && prop->obj->model->scale > 1000000.0f) {
		return netmsgReconnectSnapshotFailProp(result,
			NET_RECONNECT_SNAPSHOT_WORLD_PROP_STATE, prop,
			NET_RECONNECT_PROP_STATE_MODEL_SCALE_TOO_LARGE, -1);
	}
	if (prop->obj->model
			&& (prop->obj->model->attachedtomodel
				|| prop->obj->model->attachedtonode)) {
		struct model *parent_model = netmsgReconnectModelForProp(prop->parent);
		s32 mtx_index;

		if (!prop->obj->model->attachedtomodel
				|| !prop->obj->model->attachedtonode) {
			return netmsgReconnectSnapshotFailProp(result,
				NET_RECONNECT_SNAPSHOT_WORLD_PROP_STATE, prop,
				NET_RECONNECT_PROP_STATE_ATTACHMENT_PAIR, -1);
		}
		if (!parent_model || !parent_model->definition
				|| parent_model != prop->obj->model->attachedtomodel) {
			return netmsgReconnectSnapshotFailProp(result,
				NET_RECONNECT_SNAPSHOT_WORLD_PROP_STATE, prop,
				NET_RECONNECT_PROP_STATE_ATTACHMENT_PARENT, -1);
		}
		mtx_index = modelFindNodeMtxIndex(
			prop->obj->model->attachedtonode, 0);
		if (mtx_index < 0 || mtx_index > 0x7fff
				|| mtx_index >= parent_model->definition->nummatrices) {
			return netmsgReconnectSnapshotFailProp(result,
				NET_RECONNECT_SNAPSHOT_WORLD_PROP_STATE, prop,
				NET_RECONNECT_PROP_STATE_ATTACHMENT_MATRIX_RANGE,
				mtx_index);
		}
		if (!modelFindNodeByMtxIndex(parent_model, mtx_index)) {
			return netmsgReconnectSnapshotFailProp(result,
				NET_RECONNECT_SNAPSHOT_WORLD_PROP_STATE, prop,
				NET_RECONNECT_PROP_STATE_ATTACHMENT_MATRIX_MISSING,
				mtx_index);
		}
		attachment_mtx_index = (s16)mtx_index;
	}
	if ((prop->obj->hidden & OBJHFLAG_EMBEDDED)
			&& attachment_mtx_index < 0) {
		return netmsgReconnectSnapshotFailProp(result,
			NET_RECONNECT_SNAPSHOT_WORLD_PROP_STATE, prop,
			NET_RECONNECT_PROP_STATE_EMBEDDED_ATTACHMENT,
			attachment_mtx_index);
	}
	if (prop->type == PROPTYPE_DOOR && prop->obj->type == OBJTYPE_DOOR) {
		detail = NET_RECONNECT_PROP_DETAIL_DOOR;
	} else if (prop->type == PROPTYPE_OBJ
			&& prop->obj->type == OBJTYPE_LIFT) {
		detail = NET_RECONNECT_PROP_DETAIL_LIFT;
	} else if (prop->type == PROPTYPE_OBJ
			&& prop->obj->type == OBJTYPE_AUTOGUN) {
		detail = NET_RECONNECT_PROP_DETAIL_AUTOGUN;
	} else if (prop->type == PROPTYPE_WEAPON
			&& prop->obj->type == OBJTYPE_WEAPON) {
		detail = NET_RECONNECT_PROP_DETAIL_WEAPON;
	}
	if (detail == NET_RECONNECT_PROP_DETAIL_WEAPON) {
		const struct weaponobj *weapon = prop->weapon;
		if (weapon->weaponnum < WEAPON_UNARMED
				|| weapon->weaponnum >= WEAPON_CUSTOM_END
				|| weapon->dualweaponnum < -1
				|| weapon->dualweaponnum >= WEAPON_CUSTOM_END) {
			return netmsgReconnectSnapshotFailProp(result,
				NET_RECONNECT_SNAPSHOT_WORLD_PROP_STATE, prop,
				NET_RECONNECT_PROP_STATE_WEAPON_IDENTITY,
				attachment_mtx_index);
		}
		if (weapon->dualweapon
				&& (!netmsgReconnectIsWorldProp(
						weapon->dualweapon->base.prop)
					|| weapon->dualweapon == weapon
					|| weapon->dualweapon->base.prop->type
						!= PROPTYPE_WEAPON
					|| weapon->dualweaponnum
						!= weapon->dualweapon->weaponnum
					|| weapon->dualweapon->dualweapon != weapon
					|| weapon->dualweapon->dualweaponnum
						!= weapon->weaponnum)) {
			return netmsgReconnectSnapshotFailProp(result,
				NET_RECONNECT_SNAPSHOT_WORLD_PROP_STATE, prop,
				NET_RECONNECT_PROP_STATE_WEAPON_DUAL_REFERENCE,
				attachment_mtx_index);
		}
	}

	netbufWriteU8(dst, SVC_RECONNECT_PROP_STATE);
	netbufWriteU32(dst, prop->syncid);
	netbufWriteU8(dst, prop->type);
	netbufWriteU8(dst, prop->obj->type);
	netbufWriteU8(dst, prop->flags);
	netbufWriteU8(dst, (prop->forcetick ? 1u : 0u)
		| (prop->forceonetick ? 2u : 0u)
		| (prop->active ? 4u : 0u));
	netbufWriteS16(dst, prop->timetoregen);
	netbufWriteCoord(dst, &prop->pos);
	netbufWriteRooms(dst, prop->rooms, ARRAYCOUNT(prop->rooms));
	netbufWritePropPtr(dst, prop->parent);
	netbufWriteS16(dst, attachment_mtx_index);
	netbufWriteU32(dst, prop->obj->flags);
	netbufWriteU32(dst, prop->obj->flags2);
	netbufWriteU32(dst, prop->obj->flags3);
	netbufWriteHidden(dst, prop->obj->hidden);
	netbufWriteU8(dst, prop->obj->hidden2);
	netbufWriteU8(dst, prop->obj->model ? 1u : 0u);
	netbufWriteU16(dst, prop->obj->extrascale);
	netbufWriteS16(dst, prop->obj->pad);
	netbufWriteF32(dst, prop->obj->model
		? prop->obj->model->scale : 0.0f);
	netbufWriteS16(dst, prop->obj->damage);
	netbufWriteS16(dst, prop->obj->maxdamage);
	netbufWriteData(dst, prop->obj->shadecol,
		sizeof(prop->obj->shadecol));
	netbufWriteData(dst, prop->obj->nextcol,
		sizeof(prop->obj->nextcol));
	netbufWriteU16(dst, prop->obj->floorcol);
	for (s32 i = 0; i < 3; ++i) {
		for (s32 j = 0; j < 3; ++j) {
			netbufWriteF32(dst, prop->obj->realrot[i][j]);
		}
	}

	netbufWriteU8(dst, detail);
	if (detail == NET_RECONNECT_PROP_DETAIL_DOOR) {
		const struct doorobj *door = prop->door;
		netbufWriteF32(dst, door->frac);
		netbufWriteF32(dst, door->fracspeed);
		netbufWriteS8(dst, door->mode);
		netbufWriteS32(dst, door->autoclosetime);
	} else if (detail == NET_RECONNECT_PROP_DETAIL_LIFT) {
		const struct liftobj *lift = (const struct liftobj *)prop->obj;
		netbufWriteF32(dst, lift->dist);
		netbufWriteF32(dst, lift->speed);
		netbufWriteF32(dst, lift->accel);
		netbufWriteS8(dst, lift->levelcur);
		netbufWriteS8(dst, lift->levelaim);
	} else if (detail == NET_RECONNECT_PROP_DETAIL_AUTOGUN) {
		const struct autogunobj *autogun =
			(const struct autogunobj *)prop->obj;
		netbufWriteF32(dst, autogun->yrot);
		netbufWriteF32(dst, autogun->xrot);
		netbufWriteS8(dst, autogun->firing);
		netbufWriteU8(dst, autogun->ammoquantity);
		netbufWriteU8(dst, autogun->firecount);
		netbufWriteU8(dst, autogun->targetteam);
		netbufWritePropPtr(dst, autogun->target);
	} else if (detail == NET_RECONNECT_PROP_DETAIL_WEAPON) {
		const struct weaponobj *weapon = prop->weapon;
		netWriteWeaponRef(dst, weapon->weaponnum);
		netWriteWeaponRef(dst, weapon->dualweaponnum >= 0
			? weapon->dualweaponnum : WEAPON_UNARMED);
		netbufWriteS8(dst, weapon->unk5d);
		netbufWriteS8(dst, weapon->unk5e);
		netbufWriteU8(dst, weapon->gunfunc);
		netbufWriteS16(dst, weapon->timer240);
		netbufWritePropPtr(dst, weapon->dualweapon
			? weapon->dualweapon->base.prop : NULL);
	}

	return netmsgReconnectProjectileWrite(dst, prop, result);
}

u32 netmsgSvcReconnectPropStateRead(struct netbuf *src,
		struct netclient *srccl)
{
	netmsg_reconnect_prop_wire_t wire;
	struct prop *prop;
	struct defaultobj *obj;
	s32 expected_index;
	s32 slot;
	bool was_active;
	u8 expected_detail = NET_RECONNECT_PROP_DETAIL_NONE;

	if (!src || !srccl || srccl != g_NetLocalClient
			|| srccl->state != CLSTATE_GAME || !srccl->stage_ready
			|| !s_ReconnectPropReceive.active) {
		return 1;
	}
	memset(&wire, 0, sizeof(wire));
	wire.syncid = netbufReadU32(src);
	wire.prop_type = netbufReadU8(src);
	wire.obj_type = netbufReadU8(src);
	wire.prop_flags = netbufReadU8(src);
	wire.lifecycle_flags = netbufReadU8(src);
	wire.timetoregen = netbufReadS16(src);
	netbufReadCoord(src, &wire.pos);
	for (s32 i = 0; i < ARRAYCOUNT(wire.rooms); ++i) {
		wire.rooms[i] = -1;
	}
	netbufReadRooms(src, wire.rooms, ARRAYCOUNT(wire.rooms));
	wire.parent_id = netbufReadU32(src);
	wire.attachment_mtx_index = netbufReadS16(src);
	wire.obj_flags = netbufReadU32(src);
	wire.obj_flags2 = netbufReadU32(src);
	wire.obj_flags3 = netbufReadU32(src);
	wire.hidden = netbufReadHidden(src);
	wire.hidden2 = netbufReadU8(src);
	wire.has_model = netbufReadU8(src);
	wire.extrascale = netbufReadU16(src);
	wire.pad = netbufReadS16(src);
	wire.model_scale = netbufReadF32(src);
	wire.damage = netbufReadS16(src);
	wire.maxdamage = netbufReadS16(src);
	netbufReadData(src, wire.shadecol, sizeof(wire.shadecol));
	netbufReadData(src, wire.nextcol, sizeof(wire.nextcol));
	wire.floorcol = netbufReadU16(src);
	for (s32 i = 0; i < 3; ++i) {
		for (s32 j = 0; j < 3; ++j) {
			wire.realrot[i][j] = netbufReadF32(src);
		}
	}
	wire.detail = netbufReadU8(src);
	if (wire.detail == NET_RECONNECT_PROP_DETAIL_DOOR) {
		wire.detail_f32[0] = netbufReadF32(src);
		wire.detail_f32[1] = netbufReadF32(src);
		wire.detail_s32[0] = netbufReadS8(src);
		wire.detail_s32[1] = netbufReadS32(src);
	} else if (wire.detail == NET_RECONNECT_PROP_DETAIL_LIFT) {
		wire.detail_f32[0] = netbufReadF32(src);
		wire.detail_f32[1] = netbufReadF32(src);
		wire.detail_f32[2] = netbufReadF32(src);
		wire.detail_s32[0] = netbufReadS8(src);
		wire.detail_s32[1] = netbufReadS8(src);
	} else if (wire.detail == NET_RECONNECT_PROP_DETAIL_AUTOGUN) {
		wire.detail_f32[0] = netbufReadF32(src);
		wire.detail_f32[1] = netbufReadF32(src);
		wire.detail_s32[0] = netbufReadS8(src);
		wire.detail_s32[1] = netbufReadU8(src);
		wire.detail_s32[2] = netbufReadU8(src);
		wire.detail_s32[3] = netbufReadU8(src);
		wire.detail_target_id = netbufReadU32(src);
	} else if (wire.detail == NET_RECONNECT_PROP_DETAIL_WEAPON) {
		wire.weaponnum = netReadWeaponRef(src);
		wire.dualweaponnum = netReadWeaponRef(src);
		wire.detail_s32[0] = netbufReadS8(src);
		wire.detail_s32[1] = netbufReadS8(src);
		wire.detail_s32[2] = netbufReadU8(src);
		wire.detail_s32[3] = netbufReadS16(src);
		wire.weapon_dual_id = netbufReadU32(src);
	} else if (wire.detail != NET_RECONNECT_PROP_DETAIL_NONE) {
		return 1;
	}
	if (netmsgReconnectProjectileRead(src, &wire.projectile) != 0
			|| src->error || wire.syncid == 0
			|| (wire.lifecycle_flags & ~7u) != 0
			|| wire.attachment_mtx_index < -1
			|| (wire.attachment_mtx_index >= 0 && wire.parent_id == 0)
			|| ((wire.projectile.kind
					== NET_RECONNECT_PROJECTILE_EMBEDDED
					|| wire.projectile.kind
						== NET_RECONNECT_PROJECTILE_EMBEDDED_EMPTY)
				&& wire.attachment_mtx_index < 0)
			|| (!!(wire.hidden & OBJHFLAG_EMBEDDED)
				!= (wire.projectile.kind
					== NET_RECONNECT_PROJECTILE_EMBEDDED
					|| wire.projectile.kind
						== NET_RECONNECT_PROJECTILE_EMBEDDED_EMPTY))
			|| (!!(wire.hidden & OBJHFLAG_PROJECTILE)
				!= (wire.projectile.kind
					== NET_RECONNECT_PROJECTILE_DIRECT))) {
		return 1;
	}

	expected_index = netmsgReconnectPropExpectedIndex(wire.syncid);
	if (expected_index < 0
			|| s_ReconnectPropReceive.received[expected_index]) {
		return 1;
	}
	prop = netSyncIdLookup(wire.syncid);
	if (!netmsgReconnectIsWorldProp(prop)
			|| prop->type != wire.prop_type
			|| prop->obj->type != wire.obj_type
			|| wire.has_model > 1
			|| wire.has_model != (prop->obj->model ? 1u : 0u)
			|| (wire.has_model && (wire.model_scale <= 0.0f
				|| wire.model_scale > 1000000.0f
				|| wire.model_scale != wire.model_scale))) {
		return 1;
	}
	if (wire.prop_type == PROPTYPE_DOOR
			&& wire.obj_type == OBJTYPE_DOOR) {
		expected_detail = NET_RECONNECT_PROP_DETAIL_DOOR;
	} else if (wire.prop_type == PROPTYPE_OBJ
			&& wire.obj_type == OBJTYPE_LIFT) {
		expected_detail = NET_RECONNECT_PROP_DETAIL_LIFT;
	} else if (wire.prop_type == PROPTYPE_OBJ
			&& wire.obj_type == OBJTYPE_AUTOGUN) {
		expected_detail = NET_RECONNECT_PROP_DETAIL_AUTOGUN;
	} else if (wire.prop_type == PROPTYPE_WEAPON
			&& wire.obj_type == OBJTYPE_WEAPON) {
		expected_detail = NET_RECONNECT_PROP_DETAIL_WEAPON;
	}
	if (wire.detail != expected_detail) {
		return 1;
	}
	if ((wire.detail == NET_RECONNECT_PROP_DETAIL_DOOR
			&& (wire.prop_type != PROPTYPE_DOOR
				|| wire.obj_type != OBJTYPE_DOOR))
			|| (wire.detail == NET_RECONNECT_PROP_DETAIL_LIFT
				&& (wire.prop_type != PROPTYPE_OBJ
					|| wire.obj_type != OBJTYPE_LIFT))
			|| (wire.detail == NET_RECONNECT_PROP_DETAIL_AUTOGUN
				&& (wire.prop_type != PROPTYPE_OBJ
					|| wire.obj_type != OBJTYPE_AUTOGUN))
			|| (wire.detail == NET_RECONNECT_PROP_DETAIL_WEAPON
				&& (wire.prop_type != PROPTYPE_WEAPON
					|| wire.obj_type != OBJTYPE_WEAPON))) {
		return 1;
	}

	slot = (s32)(prop - g_Vars.props);
	if (slot < 0 || slot >= g_Vars.maxprops) {
		return 1;
	}
	obj = prop->obj;
	was_active = prop->active != 0;
	if (was_active) {
		propDeregisterRooms(prop);
	}
	prop->pos = wire.pos;
	roomsCopy(wire.rooms, prop->rooms);
	if (was_active) {
		propRegisterRooms(prop);
	}
	prop->flags = wire.prop_flags & ~PROPFLAG_ENABLED;
	if (wire.prop_flags & PROPFLAG_ENABLED) {
		propEnable(prop);
	} else {
		propDisable(prop);
	}
	prop->forcetick = (wire.lifecycle_flags & 1u) != 0;
	prop->forceonetick = (wire.lifecycle_flags & 2u) != 0;
	prop->timetoregen = wire.timetoregen;
	obj->flags = wire.obj_flags;
	obj->flags2 = wire.obj_flags2;
	obj->flags3 = wire.obj_flags3;
	obj->hidden2 = wire.hidden2;
	obj->extrascale = wire.extrascale;
	obj->pad = wire.pad;
	if (obj->model) {
		modelSetScale(obj->model, wire.model_scale);
	}
	obj->damage = wire.damage;
	obj->maxdamage = wire.maxdamage;
	memcpy(obj->shadecol, wire.shadecol, sizeof(obj->shadecol));
	memcpy(obj->nextcol, wire.nextcol, sizeof(obj->nextcol));
	obj->floorcol = wire.floorcol;
	memcpy(obj->realrot, wire.realrot, sizeof(obj->realrot));
	if (netmsgReconnectApplyProjectile(prop, &wire) != 0) {
		return 1;
	}
	if (obj->model) {
		obj->model->attachedtomodel = NULL;
		obj->model->attachedtonode = NULL;
	}

	if (wire.detail == NET_RECONNECT_PROP_DETAIL_DOOR) {
		struct doorobj *door = prop->door;
		door->frac = wire.detail_f32[0];
		door->fracspeed = wire.detail_f32[1];
		door->mode = (s8)wire.detail_s32[0];
		door->autoclosetime = wire.detail_s32[1];
	} else if (wire.detail == NET_RECONNECT_PROP_DETAIL_LIFT) {
		struct liftobj *lift = (struct liftobj *)obj;
		lift->dist = wire.detail_f32[0];
		lift->speed = wire.detail_f32[1];
		lift->accel = wire.detail_f32[2];
		lift->levelcur = (s8)wire.detail_s32[0];
		lift->levelaim = (s8)wire.detail_s32[1];
	} else if (wire.detail == NET_RECONNECT_PROP_DETAIL_AUTOGUN) {
		struct autogunobj *autogun = (struct autogunobj *)obj;
		autogun->yrot = wire.detail_f32[0];
		autogun->xrot = wire.detail_f32[1];
		autogun->firing = (s8)wire.detail_s32[0];
		autogun->ammoquantity = (u8)wire.detail_s32[1];
		autogun->firecount = (u8)wire.detail_s32[2];
		autogun->targetteam = (u8)wire.detail_s32[3];
		autogun->target = NULL;
	} else if (wire.detail == NET_RECONNECT_PROP_DETAIL_WEAPON) {
		struct weaponobj *weapon = prop->weapon;
		if (wire.weaponnum < WEAPON_UNARMED
				|| wire.weaponnum >= WEAPON_CUSTOM_END
				|| wire.dualweaponnum < WEAPON_UNARMED
				|| wire.dualweaponnum >= WEAPON_CUSTOM_END) {
			return 1;
		}
		weapon->weaponnum = (u8)wire.weaponnum;
		weapon->dualweaponnum = wire.dualweaponnum == WEAPON_UNARMED
			? -1 : (s8)wire.dualweaponnum;
		weapon->unk5d = (s8)wire.detail_s32[0];
		weapon->unk5e = (s8)wire.detail_s32[1];
		weapon->gunfunc = (u8)wire.detail_s32[2];
		weapon->timer240 = (s16)wire.detail_s32[3];
		weapon->dualweapon = NULL;
	}

	s_ReconnectPropReceive.parent_ids_by_slot[slot] = wire.parent_id;
	s_ReconnectPropReceive.lifecycle_by_slot[slot] =
		(s_ReconnectPropReceive.lifecycle_by_slot[slot]
			& NET_RECONNECT_LIFECYCLE_PRUNED_PARENT)
		| wire.lifecycle_flags;
	s_ReconnectPropReceive.refs_by_slot[slot].projectile_owner_id =
		wire.projectile.owner_id;
	s_ReconnectPropReceive.refs_by_slot[slot].projectile_target_id =
		wire.projectile.target_id;
	s_ReconnectPropReceive.refs_by_slot[slot].projectile_pickupby_id =
		wire.projectile.pickupby_id;
	s_ReconnectPropReceive.refs_by_slot[slot].detail_target_id =
		wire.detail_target_id;
	s_ReconnectPropReceive.refs_by_slot[slot].weapon_dual_id =
		wire.weapon_dual_id;
	s_ReconnectPropReceive.refs_by_slot[slot].attachment_mtx_index =
		wire.attachment_mtx_index;
	s_ReconnectPropReceive.received[expected_index] = 1;
	s_ReconnectPropReceive.received_count++;
	return 0;
}

static u32 netmsgSvcReconnectPropsWrite(struct netbuf *dst,
		u32 inventory_expected_mask, u16 *out_count,
		u16 *out_dynamic_count, u16 *out_terminal_absent_count,
		u32 *out_first_terminal_absent_syncid,
		net_reconnect_snapshot_result_t *result)
{
	u16 count = 0;
	u16 dynamic_count = 0;
	u16 terminal_absent_count = 0;
	u32 first_terminal_absent_syncid = NET_NULL_PROP;

	if (!dst || !g_Vars.props || g_Vars.maxprops <= 0
			|| g_Vars.maxprops > 0xffff
			|| g_NetFirstDynamicSyncId == 0
			|| inventory_expected_mask == 0
			|| (inventory_expected_mask
				& ~netmsgReconnectValidClientMask())) {
		return netmsgReconnectSnapshotFail(result,
			NET_RECONNECT_SNAPSHOT_WORLD_ARGUMENT, NET_NULL_CLIENT,
			NET_NULL_PROP, -1);
	}

	/* Validate the complete authoritative replicated set before writing BEGIN.
	 * Client-local transient props are outside this set; every included runtime
	 * prop must be reconstructable by the shipping spawn vocabulary. */
	for (s32 i = 0; i < g_Vars.maxprops; ++i) {
		const struct prop *prop = &g_Vars.props[i];
		if (netmsgReconnectIsWorldPropCandidate(prop)
				&& !netmsgReconnectIsWorldProp(prop)) {
			if (terminal_absent_count == 0) {
				first_terminal_absent_syncid = prop->syncid;
			}
			terminal_absent_count++;
			continue;
		}
		if (!netmsgReconnectIsWorldProp(prop)) {
			continue;
		}
		for (s32 j = i + 1; j < g_Vars.maxprops; ++j) {
			if (g_Vars.props[j].syncid == prop->syncid) {
				return netmsgReconnectSnapshotFail(result,
					NET_RECONNECT_SNAPSHOT_WORLD_DUPLICATE_SYNC_ID,
					NET_NULL_CLIENT, prop->syncid, -1);
			}
		}
		if (prop->syncid >= g_NetFirstDynamicSyncId) {
			if (!netmsgReconnectCanSpawnDynamicProp(prop)) {
				return netmsgReconnectSnapshotFail(result,
					NET_RECONNECT_SNAPSHOT_WORLD_UNSUPPORTED_DYNAMIC,
					NET_NULL_CLIENT, prop->syncid, -1);
			}
			dynamic_count++;
		}
		count++;
	}

	netbufWriteU8(dst, SVC_RECONNECT_PROP_BEGIN);
	netbufWriteU32(dst, g_NetTick);
	netbufWriteU32(dst, g_NetFirstDynamicSyncId);
	netbufWriteU32(dst, inventory_expected_mask);
	netbufWriteU16(dst, count);
	for (s32 i = 0; i < g_Vars.maxprops; ++i) {
		const struct prop *prop = &g_Vars.props[i];
		if (netmsgReconnectIsWorldProp(prop)) {
			netbufWriteU32(dst, prop->syncid);
		}
	}
	if (dst->error) {
		return netmsgReconnectSnapshotFail(result,
			NET_RECONNECT_SNAPSHOT_BUFFER, NET_NULL_CLIENT,
			NET_NULL_PROP, -1);
	}

	for (s32 i = 0; i < g_Vars.maxprops; ++i) {
		struct prop *prop = &g_Vars.props[i];
		if (!netmsgReconnectIsWorldProp(prop)) {
			continue;
		}
		if (prop->syncid >= g_NetFirstDynamicSyncId
				&& netmsgSvcPropSpawnWrite(dst, prop) != 0) {
			return netmsgReconnectSnapshotFailProp(result, dst->error
				? NET_RECONNECT_SNAPSHOT_BUFFER
				: NET_RECONNECT_SNAPSHOT_WORLD_SPAWN_WRITE,
				prop, NET_RECONNECT_PROP_STATE_NONE, -1);
		}
		if (netmsgSvcReconnectPropStateWrite(dst, prop, result) != 0) {
			return 1;
		}
	}
	netbufWriteU8(dst, SVC_RECONNECT_PROP_END);
	netbufWriteU16(dst, count);
	if (dst->error) {
		return netmsgReconnectSnapshotFail(result,
			NET_RECONNECT_SNAPSHOT_BUFFER, NET_NULL_CLIENT,
			NET_NULL_PROP, -1);
	}
	if (out_count) {
		*out_count = count;
	}
	if (out_dynamic_count) {
		*out_dynamic_count = dynamic_count;
	}
	if (out_terminal_absent_count) {
		*out_terminal_absent_count = terminal_absent_count;
	}
	if (out_first_terminal_absent_syncid) {
		*out_first_terminal_absent_syncid = first_terminal_absent_syncid;
	}
	return 0;
}

u32 netmsgSvcReconnectPropBeginRead(struct netbuf *src,
		struct netclient *srccl)
{
	u32 tick;
	u32 first_dynamic_syncid;
	u32 inventory_expected_mask;
	u16 count;
	u32 *ids = NULL;
	u8 *received = NULL;
	u8 *lifecycle = NULL;
	u32 *parents = NULL;
	netmsg_reconnect_prop_refs_t *refs = NULL;
	u32 removed_count = 0;
	u32 detached_count = 0;

	if (!src || !srccl || srccl != g_NetLocalClient
			|| srccl->state != CLSTATE_GAME || !srccl->stage_ready
			|| s_ReconnectPropReceive.active
			|| s_ReconnectPropReceive.complete
			|| !g_Vars.props || g_Vars.maxprops <= 0) {
		return 1;
	}
	tick = netbufReadU32(src);
	first_dynamic_syncid = netbufReadU32(src);
	inventory_expected_mask = netbufReadU32(src);
	count = netbufReadU16(src);
	if (src->error || first_dynamic_syncid == 0
			|| first_dynamic_syncid != g_NetFirstDynamicSyncId
			|| g_Vars.maxprops > 0xffff
			|| count > (u16)g_Vars.maxprops
			|| inventory_expected_mask == 0
			|| (inventory_expected_mask
				& ~netmsgReconnectValidClientMask())
			|| srccl->id >= NET_MAX_CLIENTS
			|| !(inventory_expected_mask & (1u << srccl->id))) {
		return 1;
	}
	if (count > 0) {
		ids = (u32 *)calloc(count, sizeof(*ids));
		received = (u8 *)calloc(count, sizeof(*received));
		if (!ids || !received) {
			free(ids);
			free(received);
			return 1;
		}
	}
	lifecycle = (u8 *)calloc((size_t)g_Vars.maxprops,
		sizeof(*lifecycle));
	parents = (u32 *)calloc((size_t)g_Vars.maxprops,
		sizeof(*parents));
	refs = (netmsg_reconnect_prop_refs_t *)calloc(
		(size_t)g_Vars.maxprops, sizeof(*refs));
	if (!lifecycle || !parents || !refs) {
		free(ids);
		free(received);
		free(lifecycle);
		free(parents);
		free(refs);
		return 1;
	}

	for (u16 i = 0; i < count; ++i) {
		ids[i] = netbufReadU32(src);
		if (src->error || ids[i] == 0) {
			goto reject;
		}
		for (u16 j = 0; j < i; ++j) {
			if (ids[j] == ids[i]) {
				goto reject;
			}
		}
	}

	/* Initial IDs must resolve in the freshly loaded stage. Runtime IDs must
	 * not already exist; they are reconstructed only by announced spawn rows. */
	for (u16 i = 0; i < count; ++i) {
		struct prop *prop = netSyncIdLookup(ids[i]);
		if (ids[i] < first_dynamic_syncid) {
			if (!netmsgReconnectIsWorldProp(prop)) {
				goto reject;
			}
		} else if (prop) {
			goto reject;
		}
	}

	netmsgReconnectPropReceiveReset();
	s_ReconnectPropReceive.active = true;
	s_ReconnectPropReceive.expected_count = count;
	s_ReconnectPropReceive.first_dynamic_syncid = first_dynamic_syncid;
	s_ReconnectPropReceive.inventory_expected_mask =
		inventory_expected_mask;
	s_ReconnectPropReceive.expected_ids = ids;
	s_ReconnectPropReceive.received = received;
	s_ReconnectPropReceive.lifecycle_by_slot = lifecycle;
	s_ReconnectPropReceive.parent_ids_by_slot = parents;
	s_ReconnectPropReceive.refs_by_slot = refs;

	/* Preserve an announced child before deleting an obsolete direct parent.
	 * objFreePermanently recursively owns its child chain, but the authority may
	 * have moved that child elsewhere since this receiver's pristine stage was
	 * loaded. The state row restores its exact parent and list placement. */
	for (u16 i = 0; i < count; ++i) {
		struct prop *prop = netSyncIdLookup(ids[i]);
		if (netmsgReconnectIsWorldProp(prop) && prop->parent
				&& netmsgReconnectIsWorldProp(prop->parent)
				&& netmsgReconnectPropExpectedIndex(
					prop->parent->syncid) < 0) {
			s32 slot = (s32)(prop - g_Vars.props);
			if (slot < 0 || slot >= g_Vars.maxprops) {
				netmsgReconnectPropReceiveReset();
				return 1;
			}
			propDetach(prop);
			s_ReconnectPropReceive.lifecycle_by_slot[slot] |=
				NET_RECONNECT_LIFECYCLE_PRUNED_PARENT;
			detached_count++;
		}
	}

	/* The announcement is applied before dynamic spawns so a client with the
	 * pristine stage cannot run out of prop slots that the authority freed and
	 * subsequently reused. */
	for (s32 i = 0; i < g_Vars.maxprops; ++i) {
		struct prop *prop = &g_Vars.props[i];
		if (netmsgReconnectIsWorldProp(prop)
				&& netmsgReconnectPropExpectedIndex(prop->syncid) < 0) {
			netmsgReconnectRemoveWorldProp(prop);
			removed_count++;
		}
	}
	netSyncIdMapRebuild();
	sysLogPrintf(LOG_NOTE,
		"NET.RECONNECT.WORLD begin tick=%u props=%u removed=%u detached=%u first_dynamic=%u",
		(unsigned)tick, (unsigned)count, (unsigned)removed_count,
		(unsigned)detached_count, (unsigned)first_dynamic_syncid);
	return 0;

reject:
	free(ids);
	free(received);
	free(lifecycle);
	free(parents);
	free(refs);
	return 1;
}

u32 netmsgSvcReconnectPropEndRead(struct netbuf *src,
		struct netclient *srccl)
{
	u16 count;

	if (!src || !srccl || srccl != g_NetLocalClient
			|| srccl->state != CLSTATE_GAME || !srccl->stage_ready
			|| !s_ReconnectPropReceive.active) {
		return 1;
	}
	count = netbufReadU16(src);
	if (src->error || count != s_ReconnectPropReceive.expected_count
			|| s_ReconnectPropReceive.received_count != count) {
		return 1;
	}
	for (u16 i = 0; i < count; ++i) {
		if (!s_ReconnectPropReceive.received[i]) {
			return 1;
		}
	}
	/* Held-object arrays are a second gameplay-facing view of the prop graph.
	 * Clear the pristine-stage view and rebuild it solely from the announced
	 * parent plus model-attachment rows below. */
	for (s32 slot = 0; slot < g_Vars.maxprops; ++slot) {
		struct prop *chrprop = &g_Vars.props[slot];
		if ((chrprop->type == PROPTYPE_CHR
				|| chrprop->type == PROPTYPE_PLAYER) && chrprop->chr) {
			memset(chrprop->chr->weapons_held, 0,
				sizeof(chrprop->chr->weapons_held));
		}
	}

	/* Resolve all cross-prop references only after every dynamic spawn exists.
	 * This avoids order-dependent parent, homing-target, and autogun state. */
	for (s32 slot = 0; slot < g_Vars.maxprops; ++slot) {
		struct prop *prop = &g_Vars.props[slot];
		u32 parent_id;
		struct prop *parent;
		netmsg_reconnect_prop_refs_t *refs;
		struct projectile *projectile;
		struct model *parent_model;
		struct modelnode *attachment_node = NULL;
		s16 attachment_mtx_index;
		bool was_parented;

		if (!netmsgReconnectIsWorldProp(prop)
				|| netmsgReconnectPropExpectedIndex(prop->syncid) < 0) {
			continue;
		}
		parent_id = s_ReconnectPropReceive.parent_ids_by_slot[slot];
		parent = parent_id ? netSyncIdLookup(parent_id) : NULL;
		if ((parent_id && !parent) || parent == prop) {
			return 1;
		}
		refs = &s_ReconnectPropReceive.refs_by_slot[slot];
		attachment_mtx_index = refs->attachment_mtx_index;
		if ((refs->projectile_owner_id
				&& !netSyncIdLookup(refs->projectile_owner_id))
				|| (refs->projectile_target_id
					&& !netSyncIdLookup(refs->projectile_target_id))
				|| (refs->projectile_pickupby_id
					&& !netSyncIdLookup(refs->projectile_pickupby_id))
				|| (refs->detail_target_id
					&& !netSyncIdLookup(refs->detail_target_id))
				|| (refs->weapon_dual_id
					&& !netSyncIdLookup(refs->weapon_dual_id))) {
			return 1;
		}
		parent_model = netmsgReconnectModelForProp(parent);
		if (attachment_mtx_index >= 0) {
			if (!parent || !parent_model || !parent_model->definition
					|| !prop->obj->model
					|| attachment_mtx_index
						>= parent_model->definition->nummatrices) {
				return 1;
			}
			attachment_node = modelFindNodeByMtxIndex(parent_model,
				attachment_mtx_index);
			if (!attachment_node) {
				return 1;
			}
		} else if (prop->obj->hidden & OBJHFLAG_EMBEDDED) {
			return 1;
		}

		was_parented = prop->parent != NULL
			|| (s_ReconnectPropReceive.lifecycle_by_slot[slot]
				& NET_RECONNECT_LIFECYCLE_PRUNED_PARENT) != 0;
		if (prop->parent != parent) {
			if (prop->parent) {
				propDetach(prop);
			}
			if (parent) {
				if (prop->active) {
					propDeregisterRooms(prop);
				}
				propDelist(prop);
				propReparent(prop, parent);
			}
		}
		/* Active/paused placement is normally receiver-local spatial state. Only
		 * reconstruct it when this snapshot actually detaches an inventory or
		 * attachment child back into the world. Dynamic spawns already consumed
		 * their authoritative initial list state in SVC_PROP_SPAWN. */
		if (!parent && was_parented) {
			const bool want_active =
				(s_ReconnectPropReceive.lifecycle_by_slot[slot] & 4u) != 0;
			if (want_active) {
				propUnpause(prop);
				propRegisterRooms(prop);
			} else {
				propPause(prop);
			}
		}
		if (prop->obj->model) {
			prop->obj->model->attachedtomodel = attachment_node
				? parent_model : NULL;
			prop->obj->model->attachedtonode = attachment_node;
		}
		/* Parent-only props can be valid transition state. A held slot is more
		 * specific: restore it only from a validated model attachment pair. */
		if (attachment_node && parent
				&& (parent->type == PROPTYPE_CHR
					|| parent->type == PROPTYPE_PLAYER)
				&& parent->chr) {
			s32 held_slot = -1;
			if (prop->obj->type == OBJTYPE_HAT) {
				held_slot = 2;
			} else if (prop->type == PROPTYPE_WEAPON
					&& prop->obj->type == OBJTYPE_WEAPON) {
				held_slot = (prop->obj->flags
					& OBJFLAG_WEAPON_LEFTHANDED)
						? HAND_LEFT : HAND_RIGHT;
			}
			if (held_slot >= 0) {
				if (parent->chr->weapons_held[held_slot]
						&& parent->chr->weapons_held[held_slot]
							!= prop) {
					return 1;
				}
				parent->chr->weapons_held[held_slot] = prop;
			}
		}

		projectile = netmsgReconnectProjectileForProp(prop, NULL);
		if (projectile) {
			projectile->ownerprop = refs->projectile_owner_id
				? netSyncIdLookup(refs->projectile_owner_id) : NULL;
			projectile->targetprop = refs->projectile_target_id
				? netSyncIdLookup(refs->projectile_target_id) : NULL;
			projectile->pickupby = refs->projectile_pickupby_id
				? netSyncIdLookup(refs->projectile_pickupby_id) : NULL;
		}
		if (prop->obj->type == OBJTYPE_AUTOGUN) {
			((struct autogunobj *)prop->obj)->target = refs->detail_target_id
				? netSyncIdLookup(refs->detail_target_id) : NULL;
		}
		if (prop->type == PROPTYPE_WEAPON
				&& prop->obj->type == OBJTYPE_WEAPON
				&& refs->weapon_dual_id) {
			struct prop *dualprop = netSyncIdLookup(refs->weapon_dual_id);
			s32 dualslot = dualprop ? (s32)(dualprop - g_Vars.props) : -1;
			if (!netmsgReconnectIsWorldProp(dualprop)
					|| dualprop->type != PROPTYPE_WEAPON
					|| dualprop->obj->type != OBJTYPE_WEAPON
					|| dualslot < 0 || dualslot >= g_Vars.maxprops
					|| prop->weapon->dualweaponnum
						!= dualprop->weapon->weaponnum
					|| s_ReconnectPropReceive.refs_by_slot[dualslot]
						.weapon_dual_id != prop->syncid) {
				return 1;
			}
			prop->weapon->dualweapon = dualprop->weapon;
		}
	}

	netSyncIdMapRebuild();
	s_ReconnectPropReceive.active = false;
	s_ReconnectPropReceive.complete = true;
	sysLogPrintf(LOG_NOTE,
		"NET.RECONNECT.WORLD end props=%u exact_set=1 refs=resolved",
		(unsigned)count);
	return 0;
}

static u32 netmsgSvcReconnectInventoryWrite(struct netbuf *dst,
		const struct netclient *client)
{
	const struct invitem *first;
	const struct invitem *item;
	u16 count = 0;

	if (!dst || !client || client->id >= NET_MAX_CLIENTS
			|| client->state != CLSTATE_GAME
			|| client->playernum >= MAX_PLAYERS || !client->player
			|| !client->player->equipment
			|| client->player->equipmaxitems < 0
			|| client->player->equipmaxitems > 0xffff) {
		return 1;
	}
	first = client->player->weapons;
	item = first;
	while (item) {
		if (count >= (u16)client->player->equipmaxitems
				|| (item->type != INVITEMTYPE_WEAP
					&& item->type != INVITEMTYPE_DUAL
					&& item->type != INVITEMTYPE_PROP)) {
			return 1;
		}
		if (item->type == INVITEMTYPE_PROP
				&& (!netmsgReconnectIsWorldProp(item->type_prop.prop)
					|| !item->type_prop.prop->parent)) {
			return 1;
		}
		count++;
		item = item->next;
		if (item == first) {
			break;
		}
	}

	netbufWriteU8(dst, SVC_RECONNECT_INVENTORY);
	netbufWriteU8(dst, (u8)client->id);
	netbufWriteU8(dst, client->player->equipallguns ? 1u : 0u);
	netbufWriteU32(dst, client->player->equipcuritem);
	netbufWriteU16(dst, count);
	item = first;
	while (item) {
		netbufWriteU8(dst, (u8)item->type);
		if (item->type == INVITEMTYPE_WEAP) {
			netWriteWeaponRef(dst, item->type_weap.weapon1);
			netbufWriteS16(dst, item->type_weap.pickuppad);
		} else if (item->type == INVITEMTYPE_DUAL) {
			netWriteWeaponRef(dst, item->type_dual.weapon1);
			netWriteWeaponRef(dst, item->type_dual.weapon2);
		} else {
			netbufWritePropPtr(dst, item->type_prop.prop);
		}
		item = item->next;
		if (item == first) {
			break;
		}
	}
	return dst->error;
}

u32 netmsgSvcReconnectInventoryRead(struct netbuf *src,
		struct netclient *srccl)
{
	u8 client_id;
	u8 allguns;
	u32 current_index;
	u16 count;
	netmsg_reconnect_inventory_item_t *items = NULL;
	struct netclient *client;
	struct player *player;
	s32 prevplayernum;

	if (!src || !srccl || srccl != g_NetLocalClient
			|| srccl->state != CLSTATE_GAME || !srccl->stage_ready
			|| s_ReconnectPropReceive.active
			|| !s_ReconnectPropReceive.complete) {
		return 1;
	}
	client_id = netbufReadU8(src);
	allguns = netbufReadU8(src);
	current_index = netbufReadU32(src);
	count = netbufReadU16(src);
	if (src->error || client_id >= NET_MAX_CLIENTS || allguns > 1
			|| !(s_ReconnectPropReceive.inventory_expected_mask
				& (1u << client_id))
			|| (s_ReconnectPropReceive.inventory_received_mask
				& (1u << client_id))) {
		return 1;
	}
	client = &g_NetClients[client_id];
	if (client->state != CLSTATE_GAME || client->playernum >= MAX_PLAYERS
			|| !client->player || !client->player->equipment
			|| client->player->equipmaxitems < 0
			|| count > (u16)client->player->equipmaxitems) {
		return 1;
	}
	player = client->player;
	if (count > 0) {
		items = (netmsg_reconnect_inventory_item_t *)calloc(count,
			sizeof(*items));
		if (!items) {
			return 1;
		}
	}
	for (u16 i = 0; i < count; ++i) {
		items[i].type = netbufReadU8(src);
		if (items[i].type == INVITEMTYPE_WEAP) {
			items[i].weapon1 = netReadWeaponRef(src);
			items[i].pickuppad = netbufReadS16(src);
			if (items[i].weapon1 < WEAPON_UNARMED
					|| items[i].weapon1 >= WEAPON_CUSTOM_END) {
				goto reject;
			}
		} else if (items[i].type == INVITEMTYPE_DUAL) {
			items[i].weapon1 = netReadWeaponRef(src);
			items[i].weapon2 = netReadWeaponRef(src);
			if (items[i].weapon1 < WEAPON_UNARMED
					|| items[i].weapon1 >= WEAPON_CUSTOM_END
					|| items[i].weapon2 < WEAPON_UNARMED
					|| items[i].weapon2 >= WEAPON_CUSTOM_END) {
				goto reject;
			}
		} else if (items[i].type == INVITEMTYPE_PROP) {
			items[i].prop_id = netbufReadU32(src);
			if (!netmsgReconnectIsWorldProp(
					netSyncIdLookup(items[i].prop_id))) {
				goto reject;
			}
			for (u16 j = 0; j < i; ++j) {
				if (items[j].type == INVITEMTYPE_PROP
						&& items[j].prop_id == items[i].prop_id) {
					goto reject;
				}
			}
		} else {
			goto reject;
		}
	}
	if (src->error || current_index > 0xffffu) {
		goto reject;
	}

	prevplayernum = g_Vars.currentplayernum;
	setCurrentPlayerNum(client->playernum);
	invClear();
	player->equipallguns = allguns != 0;
	for (u16 i = 0; i < count; ++i) {
		struct invitem *item = invFindUnusedSlot();
		if (!item) {
			setCurrentPlayerNum(prevplayernum);
			goto reject;
		}
		memset(item, 0, sizeof(*item));
		item->type = items[i].type;
		if (item->type == INVITEMTYPE_WEAP) {
			item->type_weap.weapon1 = (s16)items[i].weapon1;
			item->type_weap.pickuppad = items[i].pickuppad;
		} else if (item->type == INVITEMTYPE_DUAL) {
			item->type_dual.weapon1 = items[i].weapon1;
			item->type_dual.weapon2 = items[i].weapon2;
		} else {
			item->type_prop.prop = netSyncIdLookup(items[i].prop_id);
		}
		invInsertItem(item);
	}
	player->equipcuritem = current_index;
	setCurrentPlayerNum(prevplayernum);
	free(items);
	s_ReconnectPropReceive.inventory_received_mask |= 1u << client_id;
	sysLogPrintf(LOG_NOTE,
		"NET.RECONNECT.INVENTORY client=%u items=%u allguns=%u current=%u exact=1",
		(unsigned)client_id, (unsigned)count, (unsigned)allguns,
		(unsigned)current_index);
	return 0;

reject:
	free(items);
	return 1;
}

u32 netmsgSvcReconnectStateWrite(struct netbuf *dst,
		struct netclient *reconnecting_client,
		net_reconnect_snapshot_result_t *out_result)
{
	bool client_ids[NET_MAX_CLIENTS] = { false };
	u32 client_mask = 0;
	u32 live_count = 0;
	u32 absent_count = 0;
	u16 prop_count = 0;
	u16 dynamic_prop_count = 0;
	u16 terminal_absent_prop_count = 0;
	u32 first_terminal_absent_syncid = NET_NULL_PROP;
	u32 bytes_before = 0;
	u32 error_before = 0;
	u32 component_bytes_before;

	netmsgReconnectSnapshotResultInit(out_result, dst);
	if (!dst || !out_result || !reconnecting_client
			|| reconnecting_client->state != CLSTATE_GAME
			|| reconnecting_client->id >= NET_MAX_CLIENTS
			|| !reconnecting_client->reconnect_resync_pending) {
		return netmsgReconnectSnapshotFail(out_result,
			NET_RECONNECT_SNAPSHOT_INVALID_ARGUMENT,
			reconnecting_client && reconnecting_client->id < NET_MAX_CLIENTS
				? (u8)reconnecting_client->id : NET_NULL_CLIENT,
			NET_NULL_PROP, -1);
	}
	bytes_before = dst->wp;
	error_before = dst->error;
	if (netmsgReconnectCutsceneAuthorityWrite(dst) != 0) {
		netmsgReconnectSnapshotFail(out_result,
			NET_RECONNECT_SNAPSHOT_CUTSCENE, NET_NULL_CLIENT,
			NET_NULL_PROP, -1);
		goto rollback;
	}

	/* Freeze and validate the exact stable-ID player set before emitting any
	 * dependent world or inventory state. */
	for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
		struct netclient *client = &g_NetClients[i];
		struct netpreservedplayer *preserved =
			netServerFindPreservedByClientId((u8)i);

		if (client->state == CLSTATE_GAME
				&& !(client->flags & CLFLAG_SPECTATOR)
				&& client->room_id == reconnecting_client->room_id) {
			if (client->id != (u32)i || client_ids[i]
					|| client->playernum >= MAX_PLAYERS || !client->player
					|| !client->player->prop || !client->player->prop->chr) {
				netmsgReconnectSnapshotFail(out_result,
					NET_RECONNECT_SNAPSHOT_ROSTER, (u8)i,
					client->player && client->player->prop
						? client->player->prop->syncid : NET_NULL_PROP,
					-1);
				goto rollback;
			}
			client_ids[i] = true;
			client_mask |= 1u << i;
			live_count++;
			continue;
		}

		if (preserved && preserved->active
				&& preserved->room_id == reconnecting_client->room_id) {
			if (preserved->client_id != (u8)i || client_ids[i]
					|| preserved->playernum >= MAX_PLAYERS
					|| !g_Vars.players[preserved->playernum]
					|| !g_Vars.players[preserved->playernum]->prop
					|| !g_Vars.players[preserved->playernum]->prop->chr) {
				netmsgReconnectSnapshotFail(out_result,
					NET_RECONNECT_SNAPSHOT_ROSTER, (u8)i,
					preserved->prop_syncid, -1);
				goto rollback;
			}
			client_ids[i] = true;
			client_mask |= 1u << i;
			absent_count++;
		}
	}

	if (!client_ids[reconnecting_client->id]) {
		netmsgReconnectSnapshotFail(out_result,
			NET_RECONNECT_SNAPSHOT_ROSTER,
			(u8)reconnecting_client->id, NET_NULL_PROP, -1);
		goto rollback;
	}
	if (netmsgSvcReconnectPropsWrite(dst, client_mask, &prop_count,
			&dynamic_prop_count, &terminal_absent_prop_count,
			&first_terminal_absent_syncid, out_result) != 0) {
		goto rollback;
	}

	/* The announced world set exists before inventories, so prop-backed keys,
	 * mission items, and dropped weapons resolve without ordering assumptions.
	 * Other timed reservations remain explicitly absent but retain their exact
	 * inventory, stats, and last authoritative movement. */
	for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
		struct netclient *client = &g_NetClients[i];
		struct netpreservedplayer *preserved =
			netServerFindPreservedByClientId((u8)i);
		struct netclient absent;
		struct netclient *snapshot_client = NULL;

		if (!client_ids[i]) {
			continue;
		}
		if (client->state == CLSTATE_GAME
				&& !(client->flags & CLFLAG_SPECTATOR)
				&& client->room_id == reconnecting_client->room_id) {
			snapshot_client = client;
		} else if (preserved && preserved->active
				&& preserved->room_id == reconnecting_client->room_id) {
			memset(&absent, 0, sizeof(absent));
			absent.id = preserved->client_id;
			absent.state = CLSTATE_GAME;
			absent.flags = CLFLAG_ABSENT;
			absent.playernum = preserved->playernum;
			absent.player = g_Vars.players[preserved->playernum];
			snapshot_client = &absent;
		}
		if (!snapshot_client) {
			netmsgReconnectSnapshotFail(out_result,
				NET_RECONNECT_SNAPSHOT_ROSTER, (u8)i,
				NET_NULL_PROP, -1);
			goto rollback;
		}

		component_bytes_before = dst->wp;
		if (netmsgSvcReconnectInventoryWrite(dst, snapshot_client) != 0
				|| dst->wp == component_bytes_before) {
			netmsgReconnectSnapshotFail(out_result, dst->error
				? NET_RECONNECT_SNAPSHOT_BUFFER
				: NET_RECONNECT_SNAPSHOT_INVENTORY,
				(u8)i, NET_NULL_PROP, -1);
			goto rollback;
		}
		component_bytes_before = dst->wp;
		if (netmsgSvcPlayerStatsWrite(dst, snapshot_client) != 0
				|| dst->wp == component_bytes_before) {
			netmsgReconnectSnapshotFail(out_result, dst->error
				? NET_RECONNECT_SNAPSHOT_BUFFER
				: NET_RECONNECT_SNAPSHOT_PLAYER_STATS,
				(u8)i, NET_NULL_PROP, -1);
			goto rollback;
		}
		component_bytes_before = dst->wp;
		if (netmsgSvcPlayerMoveSnapshotWrite(dst, snapshot_client) != 0
				|| dst->wp == component_bytes_before) {
			netmsgReconnectSnapshotFail(out_result, dst->error
				? NET_RECONNECT_SNAPSHOT_BUFFER
				: NET_RECONNECT_SNAPSHOT_PLAYER_MOVEMENT,
				(u8)i, NET_NULL_PROP, -1);
			goto rollback;
		}
	}

	if (netmsgSvcChrResyncWrite(dst) != 0) {
		netmsgReconnectSnapshotFail(out_result, dst->error
			? NET_RECONNECT_SNAPSHOT_BUFFER
			: NET_RECONNECT_SNAPSHOT_CHARACTER,
			NET_NULL_CLIENT, NET_NULL_PROP, -1);
		goto rollback;
	}
	if (g_NetGameMode == NETGAMEMODE_COOP
			|| g_NetGameMode == NETGAMEMODE_ANTI) {
		if (g_ObjectiveLastIndex > 255) {
			netmsgReconnectSnapshotFail(out_result,
				NET_RECONNECT_SNAPSHOT_OBJECTIVE, NET_NULL_CLIENT,
				NET_NULL_PROP, g_ObjectiveLastIndex);
			goto rollback;
		}
		if (netmsgSvcNpcResyncWrite(dst) != 0) {
			netmsgReconnectSnapshotFail(out_result, dst->error
				? NET_RECONNECT_SNAPSHOT_BUFFER
				: NET_RECONNECT_SNAPSHOT_NPC,
				NET_NULL_CLIENT, NET_NULL_PROP, -1);
			goto rollback;
		}
		if (netmsgSvcNpcSyncWrite(dst) != 0) {
			netmsgReconnectSnapshotFail(out_result, dst->error
				? NET_RECONNECT_SNAPSHOT_BUFFER
				: NET_RECONNECT_SNAPSHOT_NPC,
				NET_NULL_CLIENT, NET_NULL_PROP, -1);
			goto rollback;
		}
		if (netmsgSvcStageFlagWrite(dst) != 0) {
			netmsgReconnectSnapshotFail(out_result, dst->error
				? NET_RECONNECT_SNAPSHOT_BUFFER
				: NET_RECONNECT_SNAPSHOT_STAGE_FLAGS,
				NET_NULL_CLIENT, NET_NULL_PROP, -1);
			goto rollback;
		}
		for (s32 i = 0; i <= g_ObjectiveLastIndex; i++) {
			if (netmsgSvcObjStatusWrite(dst, (u8)i,
					(u8)g_ObjectiveStatuses[i]) != 0) {
				netmsgReconnectSnapshotFail(out_result, dst->error
					? NET_RECONNECT_SNAPSHOT_BUFFER
					: NET_RECONNECT_SNAPSHOT_OBJECTIVE,
					NET_NULL_CLIENT, NET_NULL_PROP, i);
				goto rollback;
			}
		}
	}
	/* PlayerStatsRead can trigger local death bookkeeping while applying the
	 * snapshot. Scores follow every stats record and overwrite that transient
	 * receiver-local side effect with the authority's exact table. */
	if (netmsgSvcPlayerScoresWrite(dst) != 0) {
		netmsgReconnectSnapshotFail(out_result, dst->error
			? NET_RECONNECT_SNAPSHOT_BUFFER : NET_RECONNECT_SNAPSHOT_SCORES,
			NET_NULL_CLIENT, NET_NULL_PROP, -1);
		goto rollback;
	}
	if (netmsgSvcReconnectCommitWrite(dst,
			(u8)reconnecting_client->id) != 0) {
		netmsgReconnectSnapshotFail(out_result, dst->error
			? NET_RECONNECT_SNAPSHOT_BUFFER : NET_RECONNECT_SNAPSHOT_COMMIT,
			(u8)reconnecting_client->id, NET_NULL_PROP, -1);
		goto rollback;
	}
	if (dst->error) {
		netmsgReconnectSnapshotFail(out_result,
			NET_RECONNECT_SNAPSHOT_BUFFER, NET_NULL_CLIENT,
			NET_NULL_PROP, -1);
		goto rollback;
	}

	sysLogPrintf(LOG_NOTE,
		"NET.RECONNECT.RESYNC.PREPARE client=%u live=%u absent=%u bytes=%u inventory=%u player=1 chr=1 prop=%u dynamic_prop=%u terminal_absent=%u first_terminal_absent=%u exact_set=1 score=1 npc=%u",
		(unsigned)reconnecting_client->id, (unsigned)live_count,
		(unsigned)absent_count, (unsigned)(dst->wp - bytes_before),
		(unsigned)(live_count + absent_count), (unsigned)prop_count,
		(unsigned)dynamic_prop_count, (unsigned)terminal_absent_prop_count,
		(unsigned)first_terminal_absent_syncid,
		(unsigned)(g_NetGameMode == NETGAMEMODE_COOP
			|| g_NetGameMode == NETGAMEMODE_ANTI));
	out_result->bytes_written = dst->wp - bytes_before;
	out_result->capacity = dst->size;
	return 0;

rollback:
	/* The caller currently owns a fresh packet, but keeping this writer atomic
	 * prevents a future shared-buffer caller from publishing a valid prefix. */
	out_result->bytes_written = dst->wp - bytes_before;
	out_result->capacity = dst->size;
	dst->wp = bytes_before;
	dst->error = error_before;
	return 1;
}

u32 netmsgSvcReconnectCommitWrite(struct netbuf *dst, u8 client_id)
{
	if (!dst || client_id >= NET_MAX_CLIENTS) {
		return 1;
	}
	netbufWriteU8(dst, SVC_RECONNECT_COMMIT);
	netbufWriteU8(dst, client_id);
	return dst->error;
}

u32 netmsgSvcReconnectCommitRead(struct netbuf *src,
		struct netclient *srccl)
{
	u8 client_id;

	if (!src) {
		return 1;
	}
	client_id = netbufReadU8(src);

	if (src->error || !srccl || srccl != g_NetLocalClient
			|| srccl->state != CLSTATE_GAME || !srccl->stage_ready
			|| client_id >= NET_MAX_CLIENTS || client_id != srccl->id
			|| s_ReconnectPropReceive.active
			|| !s_ReconnectPropReceive.complete
			|| s_ReconnectPropReceive.inventory_received_mask
				!= s_ReconnectPropReceive.inventory_expected_mask) {
		sysLogPrintf(LOG_WARNING,
			"NET.RECONNECT.CLIENT commit=rejected wire_client=%u local_client=%d state=%u stage_ready=%u world_complete=%u inventory_expected=0x%08x inventory_received=0x%08x",
			(unsigned)client_id,
			srccl ? (s32)srccl->id : -1,
			srccl ? (unsigned)srccl->state : 0u,
			srccl && srccl->stage_ready ? 1u : 0u,
			s_ReconnectPropReceive.complete ? 1u : 0u,
		(unsigned)s_ReconnectPropReceive.inventory_expected_mask,
		(unsigned)s_ReconnectPropReceive.inventory_received_mask);
		return 1;
	}

	sysLogPrintf(LOG_NOTE,
		"NET.RECONNECT.CLIENT commit=accepted client=%u ordered_snapshot=complete retry_transaction_retire=1",
		(unsigned)client_id);
	netmsgReconnectPropReceiveReset();
	netClientReconnectCommitAccepted();
	return 0;
}

u32 netmsgSvcPropMoveWrite(struct netbuf *dst, struct prop *prop, struct coord *initrot)
{
	netPropMarkDirty(prop->syncid);
	u8 flags = (prop->obj != NULL);

	struct projectile *projectile = NULL;
	if (prop->obj) {
		if (prop->obj->hidden & OBJHFLAG_EMBEDDED) {
			projectile = prop->obj->embedment->projectile;
		} else if (prop->obj->hidden & OBJHFLAG_PROJECTILE) {
			projectile = prop->obj->projectile;
		}
		if (projectile) {
			flags |= (1 << 1);
			if (initrot) {
				flags |= (1 << 2);
			}
			if (prop->obj->type == OBJTYPE_HOVERPROP || prop->obj->type == OBJTYPE_HOVERBIKE) {
				flags |= (1 << 3);
			}
		}
	}

	netPropSnapUpdate(prop);
	netbufWriteU8(dst, SVC_PROP_MOVE);
	netbufWriteU8(dst, flags);
	netbufWritePropPtr(dst, prop);
	netbufWriteCoord(dst, &prop->pos);
	netbufWriteRooms(dst, prop->rooms, ARRAYCOUNT(prop->rooms));
	if (projectile) {
		netbufWriteCoord(dst, &projectile->speed);
		netbufWriteF32(dst, projectile->unk0dc);
		netbufWriteU32(dst, projectile->flags);
		netbufWriteS8(dst, projectile->bouncecount);
		netbufWritePropPtr(dst, projectile->ownerprop);
		netbufWritePropPtr(dst, projectile->targetprop);
		if (initrot) {
			netbufWriteCoord(dst, initrot);
		}
		if (prop->obj->type == OBJTYPE_HOVERPROP || prop->obj->type == OBJTYPE_HOVERBIKE) {
			netbufWriteF32(dst, projectile->unk08c);
			netbufWriteF32(dst, projectile->unk098);
			netbufWriteF32(dst, projectile->unk0e0);
			netbufWriteF32(dst, projectile->unk0e4);
			netbufWriteF32(dst, projectile->unk0ec);
			netbufWriteF32(dst, projectile->unk0f0);
		}
	}

	return dst->error;
}

u32 netmsgSvcPropMoveRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 flags = netbufReadU8(src);
	struct prop *prop = netbufReadPropPtr(src);
	struct coord pos; netbufReadCoord(src, &pos);
	RoomNum rooms[8] = { -1 }; netbufReadRooms(src, rooms, ARRAYCOUNT(rooms));

	if (src->error || !prop) {
		return src->error;
	}

	if (srccl->state < CLSTATE_GAME) {
		return 1;
	}

	prop->pos = pos;

	if (!propRoomsEqual(rooms, prop->rooms)) {
		if (prop->active) {
			propDeregisterRooms(prop);
		}
		roomsCopy(rooms, prop->rooms);
		if (prop->active) {
			propRegisterRooms(prop);
		}
	}

	if (!(flags & (1 << 0))) {
		return src->error;
	}

	if (!prop->obj) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_PROP_MOVE: prop %u should have an obj, but doesn't", prop->syncid);
		return 1;
	}

	if (!(flags & (1 << 1))) {
		return src->error;
	}

	// create a projectile for this prop if it isn't already there
	func0f0685e4(prop);

	struct projectile *projectile = NULL;
	if (prop->obj->hidden & OBJHFLAG_EMBEDDED) {
		projectile = prop->obj->embedment->projectile;
	} else if (prop->obj->hidden & OBJHFLAG_PROJECTILE) {
		projectile = prop->obj->projectile;
	}

	if (!projectile) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_PROP_MOVE: prop %u should have a projectile, but doesn't", prop->syncid);
		return 1;
	}

	netbufReadCoord(src, &projectile->speed);
	projectile->unk0dc = netbufReadF32(src);
	projectile->flags = netbufReadU32(src);
	projectile->bouncecount = netbufReadS8(src);
	projectile->ownerprop = netbufReadPropPtr(src);
	projectile->targetprop = netbufReadPropPtr(src);

	if (flags & (1 << 2)) {
		struct coord initrot; netbufReadCoord(src, &initrot);
		mtx4LoadRotation(&initrot, &projectile->mtx);
	}

	if (flags & (1 << 3)) {
		projectile->unk08c = netbufReadF32(src);
		projectile->unk098 = netbufReadF32(src);
		projectile->unk0e0 = netbufReadF32(src);
		projectile->unk0e4 = netbufReadF32(src);
		projectile->unk0ec = netbufReadF32(src);
		projectile->unk0f0 = netbufReadF32(src);
	}

	prop->pos = pos;

	return src->error;
}

u32 netmsgSvcPlayerScoresWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, SVC_PLAYER_SCORES);
	netbufWriteU8(dst, g_MpNumChrs);

	for (s32 i = 0; i < g_MpNumChrs; ++i) {
		struct mpchrconfig *mpchr = g_MpAllChrConfigPtrs[i];
		if (!mpchr) {
			continue;
		}
		netbufWriteU8(dst, (u8)i);
		netbufWriteU8(dst, mpchr->team);
		netbufWriteS16(dst, mpchr->numdeaths);
		netbufWriteS16(dst, mpchr->numpoints);
		for (s32 j = 0; j < MAX_MPCHRS; ++j) {
			netbufWriteS16(dst, mpchr->killcounts[j]);
		}
	}

	return dst->error;
}

u32 netmsgSvcPlayerScoresRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 numchrs = netbufReadU8(src);

	if (src->error || numchrs > MAX_MPCHRS) {
		return src->error ? src->error : 1;
	}

	for (s32 i = 0; i < numchrs; ++i) {
		const u8 idx = netbufReadU8(src);
		const u8 team = netbufReadU8(src);
		const s16 numdeaths = netbufReadS16(src);
		const s16 numpoints = netbufReadS16(src);
		s16 killcounts[MAX_MPCHRS];
		for (s32 j = 0; j < MAX_MPCHRS; ++j) {
			killcounts[j] = netbufReadS16(src);
		}

		if (src->error || idx >= MAX_MPCHRS) {
			return src->error ? src->error : 1;
		}

		struct mpchrconfig *mpchr = g_MpAllChrConfigPtrs[idx];
		if (mpchr) {
			mpchr->team = team;
			mpchr->numdeaths = numdeaths;
			mpchr->numpoints = numpoints;
			memcpy(mpchr->killcounts, killcounts, sizeof(killcounts));
		}
	}

	return src->error;
}

u32 netmsgSvcPropSpawnWrite(struct netbuf *dst, struct prop *prop)
{
	if (!dst || !prop || !prop->obj || prop->syncid == 0
			|| !netmsgReconnectCanSpawnDynamicProp(prop)) {
		return 1;
	}
	const u8 msgflags = (prop->active != 0) | ((prop->obj != NULL) << 1) | ((prop->forcetick != 0) << 2);
	const u8 objtype = prop->obj ? prop->obj->type : 0;

	netbufWriteU8(dst, SVC_PROP_SPAWN);
	netbufWriteU8(dst, msgflags);
	netbufWriteU32(dst, prop->syncid);
	netbufWriteCoord(dst, &prop->pos);
	netbufWriteRooms(dst, prop->rooms, ARRAYCOUNT(prop->rooms));
	netbufWritePropPtr(dst, prop->parent);
	netbufWriteU8(dst, prop->type);
	netbufWriteU8(dst, objtype);
	netbufWriteU8(dst, prop->flags);

	switch (prop->type) {
		case PROPTYPE_WEAPON:
			// dropped gun or projectile
			netWriteModelRef(dst, prop->weapon->base.modelnum);
			netWriteWeaponRef(dst, prop->weapon->weaponnum);
			netWriteWeaponRef(dst, prop->weapon->dualweaponnum >= 0
				? prop->weapon->dualweaponnum : WEAPON_UNARMED);
			netbufWriteS8(dst, prop->weapon->unk5d);
			netbufWriteS8(dst, prop->weapon->unk5e);
			netbufWriteU8(dst, prop->weapon->gunfunc);
			netbufWriteS16(dst, prop->weapon->timer240);
			break;
		case PROPTYPE_OBJ:
			// we already send most of the important obj stuff below, so
			netWriteModelRef(dst, prop->obj->modelnum);
			if (objtype == OBJTYPE_AUTOGUN) {
				// thrown laptop probably
				struct autogunobj *autogun = (struct autogunobj *)prop->obj;
				const u8 ownerplayernum = (prop->obj->hidden & 0xf0000000) >> 28;
				netbufWriteU8(dst, autogun->ammoquantity);
				netbufWriteU8(dst, autogun->firecount);
				netbufWriteU8(dst, autogun->targetteam);
				u8 ownerid = 0;
				if (ownerplayernum < MAX_PLAYERS && g_Vars.players[ownerplayernum]
						&& g_Vars.players[ownerplayernum]->client) {
					ownerid = g_Vars.players[ownerplayernum]->client->id;
				}
				netbufWriteU8(dst, ownerid);
			}
			break;
		default:
			break;
	}

	if (prop->obj) {
		netbufWriteU32(dst, prop->obj->flags);
		netbufWriteU32(dst, prop->obj->flags2);
		netbufWriteU32(dst, prop->obj->flags3);
		netbufWriteHidden(dst, prop->obj->hidden);
		netbufWriteU8(dst, prop->obj->hidden2);
		netbufWriteU16(dst, prop->obj->extrascale);
		netbufWriteS16(dst, prop->obj->pad);
		for (s32 i = 0; i < 3; ++i) {
			for (s32 j = 0; j < 3; ++j) {
				netbufWriteF32(dst, prop->obj->realrot[i][j]);
			}
		}
		if ((prop->obj->hidden & OBJHFLAG_PROJECTILE) && prop->obj->projectile) {
			netbufWriteCoord(dst, &prop->obj->projectile->nextsteppos);
			netbufWritePropPtr(dst, prop->obj->projectile->ownerprop);
			netbufWritePropPtr(dst, prop->obj->projectile->targetprop);
			netbufWriteU32(dst, prop->obj->projectile->flags);
			netbufWriteF32(dst, prop->obj->projectile->unk08c);
			netbufWriteS16(dst, prop->obj->projectile->pickuptimer240);
			netbufWriteS16(dst, prop->obj->projectile->droptype);
			netbufWriteMtxf(dst, &prop->obj->projectile->mtx);
			if (prop->obj->projectile->flags & PROJECTILEFLAG_POWERED) {
				netbufWriteF32(dst, prop->obj->projectile->unk010);
				netbufWriteF32(dst, prop->obj->projectile->unk014);
				netbufWriteF32(dst, prop->obj->projectile->unk018);
			}
		}
	}

	return dst->error;
}

u32 netmsgSvcPropSpawnRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 msgflags = netbufReadU8(src);
	const u32 syncid = netbufReadU32(src);
	struct coord pos; netbufReadCoord(src, &pos);
	RoomNum rooms[8] = { -1 }; netbufReadRooms(src, rooms, ARRAYCOUNT(rooms));
	struct prop *parent = netbufReadPropPtr(src);
	const u8 type = netbufReadU8(src);
	const u8 objtype = netbufReadU8(src);
	const u8 propflags = netbufReadU8(src);

	if (src->error) {
		return src->error;
	}

	if (srccl->state < CLSTATE_GAME || syncid == 0
			|| netSyncIdLookup(syncid)
			|| !((type == PROPTYPE_WEAPON && objtype == OBJTYPE_WEAPON)
				|| (type == PROPTYPE_OBJ
					&& objtype == OBJTYPE_AUTOGUN))) {
		return 1;
	}
	if (s_ReconnectPropReceive.active) {
		const s32 expected_index = netmsgReconnectPropExpectedIndex(syncid);
		if (syncid < s_ReconnectPropReceive.first_dynamic_syncid
				|| expected_index < 0
				|| s_ReconnectPropReceive.received[expected_index]) {
			return 1;
		}
	}

	struct prop *prop = (type == PROPTYPE_OBJ && objtype == OBJTYPE_AUTOGUN) ? NULL : propAllocate();

	if (type == PROPTYPE_WEAPON) {
		const s16 modelnum = (s16)netReadModelRef(src);
		const s32 weaponnum_raw = netReadWeaponRef(src);
		const s32 dualweaponnum_raw = netReadWeaponRef(src);
		const u8 weaponnum = (u8)weaponnum_raw;
		const s8 dualweaponnum = dualweaponnum_raw > WEAPON_UNARMED
			? (s8)dualweaponnum_raw : -1;
		const s8 unk5d = netbufReadS8(src);
		const s8 unk5e = netbufReadS8(src);
		const u8 gunfunc = netbufReadU8(src);
		const s16 timer240 = netbufReadS16(src);
		setupLoadModeldef(modelnum);
		struct modeldef *modeldef = g_ModelStates[modelnum].modeldef;
		struct model *model = modelmgrInstantiateModelWithoutAnim(modeldef);
		struct weaponobj *weapon = weaponCreate(prop == NULL, model == NULL, modeldef);
		struct weaponobj tmp = {
			256,                    // extrascale
			0,                      // hidden2
			OBJTYPE_WEAPON,         // type
			0,                      // modelnum
			-1,                     // pad
			OBJFLAG_FALL,           // flags
			0,                      // flags2
			OBJFLAG3_WALKTHROUGH,   // flags3 — weapon pickups must be walkthrough, not solid
			NULL,                   // prop
			NULL,                   // model
			1, 0, 0,                // realrot
			0, 1, 0,
			0, 0, 1,
			0,                      // hidden
			NULL,                   // geo
			NULL,                   // projectile
			0,                      // damage
			1000,                   // maxdamage
			0xff, 0xff, 0xff, 0x00, // shadecol
			0xff, 0xff, 0xff, 0x00, // nextcol
			0x0fff,                 // floorcol
			0,                      // tiles
			0,                      // weaponnum
			0,                      // unk5d
			0,                      // unk5e
			0,                      // gunfunc
			0,                      // fadeouttimer60
			-1,                     // dualweaponnum
			-1,                     // timer240
			NULL,                   // dualweapon
		};
		*weapon = tmp;
		weapon->base.modelnum = modelnum;
		weapon->weaponnum = weaponnum;
		weapon->unk5d = unk5d;
		weapon->unk5e = unk5e;
		weapon->gunfunc = gunfunc;
		weapon->timer240 = timer240;
		prop = func0f08adc8(weapon, modeldef, prop, model);
	} else if (type == PROPTYPE_OBJ) {
		const s16 modelnum = (s16)netReadModelRef(src);
		if (objtype == OBJTYPE_AUTOGUN) {
			// thrown laptop?
			const u8 ammocount = netbufReadU8(src);
			const u8 firecount = netbufReadU8(src);
			const u8 targetteam = netbufReadU8(src);
			const u8 clid = netbufReadU8(src);
			if (clid >= NET_MAX_CLIENTS + 1 || !g_NetClients[clid].player
					|| !g_NetClients[clid].player->prop || !g_NetClients[clid].player->prop->chr) {
				return src->error ? src->error : 1;
			}
			struct chrdata *ownerchr = g_NetClients[clid].player->prop->chr;
			struct autogunobj *obj = laptopDeploy(modelnum, NULL, ownerchr);
			/* B-163: laptopDeploy can return NULL if the modeldef failed to load
			 * (torn asset, catalog miss).  Skip spawn; server will resync. */
			if (obj == NULL) {
				sysLogPrintf(LOG_WARNING,
					"NETMSG: laptopDeploy returned NULL for modelnum %d — spawn skipped",
					modelnum);
				return src->error ? src->error : 1;
			}
			obj->ammoquantity = ammocount;
			obj->firecount = firecount;
			obj->targetteam = targetteam;
			prop = obj->base.prop;
		}
	}

	if (prop) {
		prop->type = type;
		prop->syncid = syncid;
		netSyncIdMapSet(syncid, prop); // keep lookup map current for client-side dynamic spawns
		prop->pos = pos;
		prop->forcetick = (msgflags & (1 << 2)) != 0;
		// prop->flags = propflags;
		roomsCopy(rooms, prop->rooms);
		if (msgflags & (1 << 0)) {
			propActivate(prop);
			propRegisterRooms(prop);
		} else {
			propPause(prop);
		}
		if (propflags & PROPFLAG_ENABLED) {
			propEnable(prop);
		} else {
			propDisable(prop);
		}
		if (parent) {
			propReparent(prop, parent);
		}
	} else {
		sysLogPrintf(LOG_WARNING, "NET: no prop allocated when spawning prop %u (%u)", syncid, type);
		return src->error;
	}

	if (msgflags & (1 << 1)) {
		const u32 flags = netbufReadU32(src);
		const u32 flags2 = netbufReadU32(src);
		const u32 flags3 = netbufReadU32(src);
		const u32 hidden = netbufReadHidden(src);
		const u8 hidden2 = netbufReadU8(src);
		const u16 extrascale = netbufReadU16(src);
		const s16 pad = netbufReadS16(src);
		for (s32 i = 0; i < 3; ++i) {
			for (s32 j = 0; j < 3; ++j) {
				prop->obj->realrot[i][j] = netbufReadF32(src);
			}
		}
		if (prop->obj) {
			if (hidden & OBJHFLAG_PROJECTILE) {
				func0f0685e4(prop);
				netbufReadCoord(src, &prop->obj->projectile->nextsteppos);
				prop->obj->projectile->ownerprop = netbufReadPropPtr(src);
				prop->obj->projectile->targetprop = netbufReadPropPtr(src);
				prop->obj->projectile->flags = netbufReadU32(src);
				prop->obj->projectile->unk08c = netbufReadF32(src);
				prop->obj->projectile->pickuptimer240 = netbufReadS16(src);
				prop->obj->projectile->droptype = netbufReadS16(src);
				prop->obj->projectile->flighttime240 = 0;
				netbufReadMtxf(src, &prop->obj->projectile->mtx);
				if (prop->obj->projectile->flags & PROJECTILEFLAG_POWERED) {
					// rocket; get acceleration and realrot
					prop->obj->projectile->unk010 = netbufReadF32(src);
					prop->obj->projectile->unk014 = netbufReadF32(src);
					prop->obj->projectile->unk018 = netbufReadF32(src);
					prop->obj->projectile->powerlimit240 = TICKS(1200);
					prop->obj->projectile->smoketimer240 = TICKS(24);
				}
				if (!s_ReconnectPropReceive.active
						&& ((type == PROPTYPE_WEAPON
								&& (prop->obj->projectile->flags
									& PROJECTILEFLAG_00000002))
							|| objtype == OBJTYPE_AUTOGUN)) {
					/* A live spawn owns this one-shot sound. Reconnect reconstructs
					 * state and must not replay an already-consumed throw effect. */
					psCreate(NULL, prop, SFX_THROW, -1, -1, 0, 0, PSTYPE_NONE, NULL, -1, NULL, -1, -1, -1, -1);
				}
			}
			prop->obj->flags = flags;
			prop->obj->flags2 = flags2;
			prop->obj->flags3 = flags3;
			prop->obj->hidden = hidden;
			prop->obj->hidden2 = hidden2;
			prop->obj->extrascale = extrascale;
			prop->obj->pad = pad;
			if (prop->obj->model) {
				modelSetScale(prop->obj->model, prop->obj->model->scale * ((f32)extrascale / 256.f));
			}
		}
	}

	// just in case
	prop->pos = pos;

	return src->error;
}

u32 netmsgSvcPropDamageWrite(struct netbuf *dst, struct prop *prop, f32 damage, struct coord *pos, s32 weaponnum, s32 playernum)
{
	if (!prop || !prop->obj) {
		return dst->error;
	}
	netPropMarkDirty(prop->syncid);
	netPropSnapUpdate(prop);
	netbufWriteU8(dst, SVC_PROP_DAMAGE);
	netbufWritePropPtr(dst, prop);
	netbufWriteCoord(dst, pos);
	netbufWriteF32(dst, prop->obj->damage);
	netbufWriteF32(dst, damage);
	netWriteWeaponRef(dst, weaponnum);
	netbufWriteS8(dst, playernum);
	netbufWriteHidden(dst,
		prop->obj->hidden & ~(OBJHFLAG_PROJECTILE | OBJHFLAG_EMBEDDED));
	return dst->error;
}

u32 netmsgSvcPropDamageRead(struct netbuf *src, struct netclient *srccl)
{
	struct prop *prop = netbufReadPropPtr(src);
	struct coord pos; netbufReadCoord(src, &pos);
	const f32 damagepre = netbufReadF32(src);
	const f32 damage = netbufReadF32(src);
	const s32 weaponnum = netReadWeaponRef(src);
	const s8 playernum = netbufReadS8(src);
	const u32 hidden = netbufReadHidden(src);
	if (srccl->state < CLSTATE_GAME) {
		return src->error;
	}
	if (prop && prop->obj && prop->type != PROPTYPE_PLAYER && prop->type != PROPTYPE_CHR && !src->error
			&& weaponnum >= WEAPON_UNARMED && weaponnum < WEAPON_CUSTOM_END) {
		prop->obj->damage = damagepre;
		prop->obj->hidden = hidden | (prop->obj->hidden & (OBJHFLAG_PROJECTILE | OBJHFLAG_EMBEDDED));
		objDamage(prop->obj, -damage, &pos, weaponnum, playernum);
	}
	return src->error;
}

u32 netmsgSvcPropPickupWrite(struct netbuf *dst, struct netclient *actcl, struct prop *prop, const s32 tickop)
{
	netPropMarkDirty(prop->syncid);
	netbufWriteU8(dst, SVC_PROP_PICKUP);
	netbufWriteU8(dst, actcl->id);
	netbufWriteS8(dst, tickop);
	netbufWritePropPtr(dst, prop);
	return dst->error;
}

u32 netmsgSvcPropPickupRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 clid = netbufReadU8(src);
	const s8 tickop = netbufReadS8(src);
	struct prop *prop = netbufReadPropPtr(src);
	if (src->error || !prop || srccl->state < CLSTATE_GAME || clid >= NET_MAX_CLIENTS + 1) {
		return src->error ? src->error : 1;
	}

	struct netclient *actcl = g_NetClients + clid;
	if (actcl->playernum >= MAX_PLAYERS) {
		return 1;
	}

	const s32 prevplayernum = g_Vars.currentplayernum;
	setCurrentPlayerNum(actcl->playernum);

	propPickupByPlayer(prop, true);
	if (tickop != TICKOP_NONE) {
		propExecuteTickOperation(prop, tickop);
	}

	setCurrentPlayerNum(prevplayernum);

	return src->error;
}

u32 netmsgSvcPropUseWrite(struct netbuf *dst, struct prop *prop, struct netclient *usercl, const s32 tickop)
{
	netPropMarkDirty(prop->syncid);
	netbufWriteU8(dst, SVC_PROP_USE);
	netbufWritePropPtr(dst, prop);
	netbufWriteU8(dst, usercl->id);
	netbufWriteS8(dst, tickop);
	return dst->error;
}

u32 netmsgSvcPropUseRead(struct netbuf *src, struct netclient *srccl)
{
	struct prop *prop = netbufReadPropPtr(src);
	const u8 clid = netbufReadU8(src);
	const s8 tickop = netbufReadS8(src);

	if (!prop || srccl->state < CLSTATE_GAME || clid >= NET_MAX_CLIENTS + 1) {
		return src->error ? src->error : 1;
	}

	struct netclient *actcl = &g_NetClients[clid];
	if (actcl->playernum >= MAX_PLAYERS) {
		return 1;
	}

	const s32 prevplayernum = g_Vars.currentplayernum;
	setCurrentPlayerNum(actcl->playernum);

	s32 ownop;
	switch (prop->type) {
		case PROPTYPE_OBJ:
		case PROPTYPE_WEAPON:
			if (!prop->obj || prop->obj->type != OBJTYPE_LIFT) {
				ownop = propobjInteract(prop);
			} else {
				ownop = TICKOP_NONE;
			}
			break;
		default:
			// Unhandled prop types (CHR, etc.) produce no interaction on the client.
			// Doors and lifts have dedicated SVC_PROP_DOOR / SVC_PROP_LIFT messages.
			// SVC_PROP_USE can be removed once every interactive prop type has a
			// dedicated handler and no callers of netmsgSvcPropUseWrite remain.
			ownop = TICKOP_NONE;
			break;
	}

	propExecuteTickOperation(prop, tickop);

	setCurrentPlayerNum(prevplayernum);

	return src->error;
}

u32 netmsgSvcPropDoorWrite(struct netbuf *dst, struct prop *prop, struct netclient *usercl)
{
	if (prop->type != PROPTYPE_DOOR || !prop->door) {
		return dst->error;
	}

	netPropMarkDirty(prop->syncid);
	struct doorobj *door = prop->door;

	netPropSnapUpdate(prop);
	netbufWriteU8(dst, SVC_PROP_DOOR);
	netbufWritePropPtr(dst, prop);
	netbufWriteU8(dst, usercl ? usercl->id : NET_NULL_CLIENT);
	netbufWriteS8(dst, door->mode);
	netbufWriteU32(dst, door->base.flags);
	netbufWriteHidden(dst, door->base.hidden);

	return dst->error;
}

u32 netmsgSvcPropDoorRead(struct netbuf *src, struct netclient *srccl)
{
	struct prop *prop = netbufReadPropPtr(src);
	const u8 clid = netbufReadU8(src);
	const s8 doormode = netbufReadS8(src);
	const u32 flags = netbufReadU32(src);
	const u32 hidden = netbufReadHidden(src);

	struct netclient *actcl = (clid == NET_NULL_CLIENT) ? NULL :
		(clid < NET_MAX_CLIENTS + 1) ? &g_NetClients[clid] : NULL;

	if (!prop || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	if (!prop->door || prop->type != PROPTYPE_DOOR) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_PROP_DOOR: prop %u should be a door, but isn't", prop->syncid);
		return src->error;
	}

	const s32 prevplayernum = g_Vars.currentplayernum;
	if (actcl) {
		if (actcl->playernum >= MAX_PLAYERS) {
			sysLogPrintf(LOG_WARNING, "NET: SvcPropDoorRead invalid playernum %d", actcl->playernum);
			return 1;
		}
		setCurrentPlayerNum(actcl->playernum);
	}

	doorSetMode(prop->door, doormode);
	prop->door->base.hidden = hidden;
	prop->door->base.flags = flags;

	if (actcl) {
		setCurrentPlayerNum(prevplayernum);
	}

	return src->error;
}

u32 netmsgSvcPropLiftWrite(struct netbuf *dst, struct prop *prop)
{
	if (prop->type != PROPTYPE_OBJ || !prop->obj || prop->obj->type != OBJTYPE_LIFT) {
		return dst->error;
	}

	netPropMarkDirty(prop->syncid);
	struct liftobj *lift = (struct liftobj *)prop->obj;

	netPropSnapUpdate(prop);
	netbufWriteU8(dst, SVC_PROP_LIFT);
	netbufWritePropPtr(dst, prop);
	netbufWriteS8(dst, lift->levelcur);
	netbufWriteS8(dst, lift->levelaim);
	netbufWriteF32(dst, lift->accel);
	netbufWriteF32(dst, lift->speed);
	netbufWriteF32(dst, lift->dist);
	netbufWriteU32(dst, lift->base.flags);
	netbufWriteCoord(dst, &prop->pos);
	netbufWriteRooms(dst, prop->rooms, ARRAYCOUNT(prop->rooms));

	return dst->error;
}

u32 netmsgSvcPropLiftRead(struct netbuf *src, struct netclient *srccl)
{
	struct prop *prop = netbufReadPropPtr(src);
	const s8 levelcur = netbufReadS8(src);
	const s8 levelaim = netbufReadS8(src);
	const f32 accel = netbufReadF32(src);
	const f32 speed = netbufReadF32(src);
	const f32 dist = netbufReadF32(src);
	const u32 flags = netbufReadU32(src);
	struct coord pos; netbufReadCoord(src, &pos);
	RoomNum rooms[8]; netbufReadRooms(src, rooms, ARRAYCOUNT(rooms));

	if (!prop || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	if (!prop->obj || prop->type != PROPTYPE_OBJ || prop->obj->type != OBJTYPE_LIFT) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_PROP_LIFT: prop %u should be a lift, but isn't", prop->syncid);
		return src->error;
	}

	struct liftobj *lift = (struct liftobj *)prop->obj;
	lift->levelcur = levelcur;
	lift->levelaim = levelaim;
	lift->speed = speed;
	lift->dist = dist;
	lift->accel = accel;
	lift->base.flags = flags;

	prop->pos = pos;

	if (!propRoomsEqual(rooms, prop->rooms)) {
		if (prop->active) {
			propDeregisterRooms(prop);
		}
		roomsCopy(rooms, prop->rooms);
		if (prop->active) {
			propRegisterRooms(prop);
		}
	}

	return src->error;
}

u32 netmsgSvcChrDamageWrite(struct netbuf *dst, struct chrdata *chr, f32 damage, struct coord *vector, struct gset *gset,
		struct prop *aprop, s32 hitpart, bool damageshield, struct prop *prop2, s32 side, s16 *arg11, bool explosion, struct coord *explosionpos)
{
	const u8 flags = damageshield | (explosion << 1) | ((gset != NULL) << 2) |
		((aprop != NULL) << 3) | ((prop2 != NULL) << 4) | ((arg11 != NULL) << 5) | ((explosionpos != NULL) << 6);

	netbufWriteU8(dst, SVC_CHR_DAMAGE);
	netbufWriteU8(dst, flags);
	netbufWritePropPtr(dst, chr->prop);
	netbufWriteF32(dst, damage);
	netbufWriteCoord(dst, vector);
	netbufWriteS16(dst, hitpart);
	netbufWriteS16(dst, side);
	if (gset) {
		netbufWriteGset(dst, gset);
	}
	if (aprop) {
		netbufWritePropPtr(dst, aprop);
	}
	if (prop2) {
		netbufWritePropPtr(dst, prop2);
	}
	if (arg11) {
		netbufWriteS16(dst, arg11[0]);
		netbufWriteS16(dst, arg11[1]);
		netbufWriteS16(dst, arg11[2]);
	}
	if (explosionpos) {
		netbufWriteCoord(dst, explosionpos);
	}

	return dst->error;
}

u32 netmsgSvcChrDamageRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 flags = netbufReadU8(src);
	struct prop *chrprop = netbufReadPropPtr(src);
	const f32 damage = netbufReadF32(src);
	struct coord vector; netbufReadCoord(src, &vector);
	const s16 hitpart = netbufReadS16(src);
	const s16 side = netbufReadS16(src);

	struct gset gsetvalue;
	struct gset *gset = NULL;
	if (flags & (1 << 2)) {
		netbufReadGset(src, &gsetvalue);
		gset = &gsetvalue;
	}

	struct prop *aprop = (flags & (1 << 3)) ? netbufReadPropPtr(src) : NULL;
	struct prop *prop2 = (flags & (1 << 4)) ? netbufReadPropPtr(src) : NULL;

	s16 arg11[3], *arg11ptr = NULL;
	if (flags & (1 << 5)) {
		arg11[0] = netbufReadS16(src);
		arg11[1] = netbufReadS16(src);
		arg11[2] = netbufReadS16(src);
		arg11ptr = arg11;
	}

	struct coord explosionpos, *explosionposptr = NULL;
	if (flags & (1 << 6)) {
		netbufReadCoord(src, &explosionpos);
		explosionposptr = &explosionpos;
	}

	if (src->error || srccl->state < CLSTATE_GAME || !chrprop || !chrprop->chr) {
		return src->error ? src->error : 1;
	}

	const bool damageshield = (flags & (1 << 0)) != 0;
	const bool explosion = (flags & (1 << 1)) != 0;

	const s32 prevplayernum = g_Vars.currentplayernum;
	if (chrprop->type == PROPTYPE_PLAYER) {
		setCurrentPlayerNum(playermgrGetPlayerNumByProp(chrprop));
	}

	chrDamage(chrprop->chr, damage, &vector, gset, aprop, hitpart, damageshield, prop2, NULL, NULL, side, arg11ptr, explosion, explosionposptr);

	if (chrprop->type == PROPTYPE_PLAYER) {
		setCurrentPlayerNum(prevplayernum);
	}

	return src->error;
}

u32 netmsgSvcChrDisarmWrite(struct netbuf *dst, struct chrdata *chr, struct prop *aprop, u8 weaponnum, f32 wpndamage, struct coord *wpnpos)
{
	netbufWriteU8(dst, SVC_CHR_DISARM);
	netbufWritePropPtr(dst, chr->prop);
	netbufWritePropPtr(dst, aprop);
	netWriteWeaponRef(dst, weaponnum);
	netbufWriteF32(dst, wpndamage);
	if (wpndamage > 0.f && wpnpos) {
		netbufWriteCoord(dst, wpnpos);
	}
	return dst->error;
}

u32 netmsgSvcChrDisarmRead(struct netbuf *src, struct netclient *srccl)
{
	struct prop *chrprop = netbufReadPropPtr(src);
	struct prop *aprop = netbufReadPropPtr(src);
	const s32 weaponnum = netReadWeaponRef(src);
	const f32 weapondmg = netbufReadF32(src);
	struct coord pos = { 0.f, 0.f, 0.f };

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	if (!chrprop || !chrprop->chr) {
		return 1;
	}

	struct chrdata *chr = chrprop->chr;

	if (chrprop->type == PROPTYPE_CHR) {
		return src->error;
	}

	if (weapondmg > 0.f) {
		// someone shot a grenade the chr is holding, explode that shit
		netbufReadCoord(src, &pos);
		if (weaponnum < WEAPON_UNARMED || weaponnum >= WEAPON_CUSTOM_END) {
			return src->error;
		}
		struct weaponobj *weapon = NULL;
		if (chr->weapons_held[0] && chr->weapons_held[0]->weapon) {
			weapon = chr->weapons_held[0]->weapon;
		} else if (chr->weapons_held[1] && chr->weapons_held[1]->weapon) {
			weapon = chr->weapons_held[1]->weapon;
		} else {
			sysLogPrintf(LOG_WARNING, "NET: trying to explode chr %u's gun, but there's no gun", chrprop->syncid);
			return src->error;
		}
		objSetDropped(chrprop, DROPTYPE_DEFAULT);
		chr->hidden |= CHRHFLAG_DROPPINGITEM;
		objDamage(&weapon->base, -weapondmg, &pos, weaponnum, g_Vars.currentplayernum);
		return src->error;
	}

	const s32 prevplayernum = g_Vars.currentplayernum;
	setCurrentPlayerNum(playermgrGetPlayerNumByProp(chrprop));

	struct player *player = g_Vars.currentplayer;

	if (weaponHasFlag(weaponnum, WEAPONFLAG_UNDROPPABLE) || weaponnum > WEAPON_MAX_DROPPABLE || weaponnum <= WEAPON_UNARMED) {
		setCurrentPlayerNum(prevplayernum);
		return src->error;
	}

	if (weaponnum == WEAPON_RCP120) {
		player->devicesactive &= ~DEVICE_CLOAKRCP120;
	}

	if (weaponnum == WEAPON_CLOAKINGDEVICE) {
		player->devicesactive &= ~DEVICE_CLOAKDEVICE;
	}

	weaponDeleteFromChr(chr, HAND_RIGHT);
	weaponDeleteFromChr(chr, HAND_LEFT);

	invRemoveItemByNum(weaponnum);

	player->hands[1].state = HANDSTATE_IDLE;
	player->hands[1].ejectstate = EJECTSTATE_INIT;
	player->hands[1].ejecttype = EJECTTYPE_GUN;
	player->hands[0].ejectstate = EJECTSTATE_INIT;
	player->hands[0].ejecttype = EJECTTYPE_GUN;
	player->hands[0].state = HANDSTATE_IDLE;

	if (player->visionmode == VISIONMODE_SLAYERROCKET) {
		struct weaponobj *rocket = g_Vars.currentplayer->slayerrocket;
		if (rocket && rocket->base.prop) {
			rocket->timer240 = 0;
		}
		player->visionmode = VISIONMODE_NORMAL;
	}

	bgunEquipWeapon2(HAND_RIGHT, WEAPON_UNARMED);
	bgunEquipWeapon2(HAND_LEFT, WEAPON_NONE);

	setCurrentPlayerNum(prevplayernum);

	return src->error;
}

/* ========================================================================
 * SVC_CHR_MOVE - Server-authoritative bot/simulant position update
 * Sent from server to all clients every update frame for each active bot.
 * Clients do NOT run bot AI; they receive positions from the server.
 * ======================================================================== */

u32 netmsgSvcChrMoveWrite(struct netbuf *dst, struct chrdata *chr)
{
	if (!chr || !chr->prop || !chr->aibot) {
		return dst->error;
	}

	struct prop *prop = chr->prop;
	struct aibot *aibot = chr->aibot;

	// flags: bit 0 = has angle, bit 1 = has rooms
	const u8 flags = (1 << 0) | (1 << 1);

	netbufWriteU8(dst, SVC_CHR_MOVE);
	netbufWritePropPtr(dst, prop);
	netbufWriteU8(dst, flags);
	netbufWriteCoord(dst, &prop->pos);

	// yaw heading (look angle)
	if (flags & (1 << 0)) {
		netbufWriteF32(dst, chrGetInverseTheta(chr));
	}

	// rooms
	if (flags & (1 << 1)) {
		netbufWriteRooms(dst, prop->rooms, ARRAYCOUNT(prop->rooms));
	}

	// movement speed multipliers for animation blending
	netbufWriteF32(dst, aibot->speedmultforwards);
	netbufWriteF32(dst, aibot->speedmultsideways);
	netbufWriteF32(dst, aibot->speedtheta);

	// action state for animation
	netbufWriteS8(dst, chr->myaction);
	netbufWriteU8(dst, chr->actiontype);

	/* v47 (S594h-B Slice 3): surface_up vec3 for surface-normal
	 * locomotion. Skedars on slopes/walls/ceilings sync their local-up
	 * to clients so the model tilt matches host. Always 12 bytes. */
	netbufWriteF32(dst, chr->surface_up[0]);
	netbufWriteF32(dst, chr->surface_up[1]);
	netbufWriteF32(dst, chr->surface_up[2]);

	return dst->error;
}

u32 netmsgSvcChrMoveRead(struct netbuf *src, struct netclient *srccl)
{
	struct prop *prop = netbufReadPropPtr(src);
	const u8 flags = netbufReadU8(src);
	struct coord newpos;
	netbufReadCoord(src, &newpos);

	f32 newangle = 0.f;
	if (flags & (1 << 0)) {
		newangle = netbufReadF32(src);
	}

	s16 newrooms[8] = { -1 };
	if (flags & (1 << 1)) {
		netbufReadRooms(src, newrooms, ARRAYCOUNT(newrooms));
	}

	const f32 speedmultfwd = netbufReadF32(src);
	const f32 speedmultside = netbufReadF32(src);
	const f32 speedtheta = netbufReadF32(src);
	const s8 myaction = netbufReadS8(src);
	const u8 actiontype = netbufReadU8(src);
	const f32 newsurface_x = netbufReadF32(src);
	const f32 newsurface_y = netbufReadF32(src);
	const f32 newsurface_z = netbufReadF32(src);

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	if (!prop || !prop->chr || !prop->chr->aibot) {
		return src->error;
	}

	struct chrdata *chr = prop->chr;
	struct aibot *aibot = chr->aibot;

	// Apply the server-authoritative position
	chr->prevpos = prop->pos;
	prop->pos = newpos;
	chr->myaction = myaction;
	chr->actiontype = actiontype;

	// Apply facing angle
	if (flags & (1 << 0)) {
		chrSetLookAngle(chr, newangle);
	}

	// Apply movement speed multipliers for third-person animation
	aibot->speedmultforwards = speedmultfwd;
	aibot->speedmultsideways = speedmultside;
	aibot->speedtheta = speedtheta;

	/* v47 (S594h-B Slice 3): apply server-authoritative surface_up.
	 * Local chrSurfaceLocoTick continues to sample + blend so the
	 * client renders smoothly between wire updates. */
	chr->surface_up[0] = newsurface_x;
	chr->surface_up[1] = newsurface_y;
	chr->surface_up[2] = newsurface_z;

	// Update rooms if provided
	if (flags & (1 << 1)) {
		for (s32 i = 0; i < 8; ++i) {
			prop->rooms[i] = newrooms[i];
			if (newrooms[i] < 0) {
				break;
			}
		}
	}

	return src->error;
}

/* ========================================================================
 * SVC_CHR_STATE - Server-authoritative bot/simulant state update
 * Sent less frequently (every N frames) or on state change.
 * Syncs health, shield, weapon, and key behavioral flags.
 * ======================================================================== */

u32 netmsgSvcChrStateWrite(struct netbuf *dst, struct chrdata *chr)
{
	if (!chr || !chr->prop || !chr->aibot) {
		return dst->error;
	}

	struct aibot *aibot = chr->aibot;

	// flags: bit 0 = is dead, bit 1 = is cloaked, bit 2 = has target, bit 3 = respawning
	const u8 flags = (chrIsDead(chr) ? (1 << 0) : 0)
		| ((chr->hidden & CHRHFLAG_CLOAKED) ? (1 << 1) : 0)
		| ((chr->target >= 0) ? (1 << 2) : 0)
		| (aibot->respawning ? (1 << 3) : 0);

	netbufWriteU8(dst, SVC_CHR_STATE);
	netbufWritePropPtr(dst, chr->prop);
	netbufWriteU8(dst, flags);
	netbufWriteF32(dst, chr->damage);
	netbufWriteF32(dst, chr->cshield);
	netWriteWeaponRef(dst, aibot->weaponnum);
	netbufWriteS8(dst, aibot->gunfunc);
	netbufWriteS16(dst, aibot->loadedammo[0]);
	netbufWriteS16(dst, aibot->loadedammo[1]);
	netbufWriteS8(dst, chr->team);
	netbufWriteF32(dst, chr->blurdrugamount);
	netbufWriteU8(dst, chr->fadealpha);
	netbufWriteU8(dst, aibot->fadeintimer60);

	return dst->error;
}

u32 netmsgSvcChrStateRead(struct netbuf *src, struct netclient *srccl)
{
	struct prop *prop = netbufReadPropPtr(src);
	const u8 flags = netbufReadU8(src);
	const f32 damage = netbufReadF32(src);
	const f32 shield = netbufReadF32(src);
	const s32 weaponnum = netReadWeaponRef(src);
	const s8 gunfunc = netbufReadS8(src);
	const s16 loadedammo0 = netbufReadS16(src);
	const s16 loadedammo1 = netbufReadS16(src);
	const s8 team = netbufReadS8(src);
	const f32 blurdrugamount = netbufReadF32(src);
	const u8 fadealpha = netbufReadU8(src);
	const u8 fadeintimer60 = netbufReadU8(src);

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	if (!prop || !prop->chr || !prop->chr->aibot) {
		return src->error;
	}

	struct chrdata *chr = prop->chr;
	struct aibot *aibot = chr->aibot;

	chr->damage = damage;
	chr->cshield = shield;
	if (weaponnum >= WEAPON_UNARMED && weaponnum < WEAPON_CUSTOM_END) {
		aibot->weaponnum = weaponnum;
	}
	aibot->gunfunc = gunfunc;
	aibot->loadedammo[0] = loadedammo0;
	aibot->loadedammo[1] = loadedammo1;
	chr->team = team;
	chr->blurdrugamount = blurdrugamount;
	chr->fadealpha = fadealpha;
	aibot->respawning = (flags >> 3) & 1;
	aibot->fadeintimer60 = fadeintimer60;

	return src->error;
}

/* ========================================================================
 * SVC_CHR_SYNC - Periodic checksum of all bot states for desync detection
 * Server sends a compact checksum every N frames. If a client's local
 * checksum doesn't match, it logs a warning (future: request full resync).
 * ======================================================================== */

static u32 netChrSyncChecksum(void)
{
	u32 crc = 0;
	for (s32 i = 0; i < g_BotCount; ++i) {
		struct chrdata *chr = g_MpBotChrPtrs[i];
		if (!chr || !chr->prop) {
			continue;
		}
		// Mix position, health, and action into a simple checksum
		u32 px = *(u32 *)&chr->prop->pos.x;
		u32 py = *(u32 *)&chr->prop->pos.y;
		u32 pz = *(u32 *)&chr->prop->pos.z;
		u32 dm = *(u32 *)&chr->damage;
		crc ^= px ^ (py << 7) ^ (pz << 13) ^ (dm << 19) ^ ((u32)chr->myaction << 24);
		crc = (crc << 5) | (crc >> 27); // rotate
	}
	return crc;
}

u32 netmsgSvcChrSyncWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, SVC_CHR_SYNC);
	netbufWriteU32(dst, g_NetTick);
	netbufWriteU8(dst, g_BotCount);
	netbufWriteU32(dst, netChrSyncChecksum());
	return dst->error;
}

u32 netmsgSvcChrSyncRead(struct netbuf *src, struct netclient *srccl)
{
	const u32 tick = netbufReadU32(src);
	const u8 botcount = netbufReadU8(src);
	const u32 serverCrc = netbufReadU32(src);

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	// Bot authority client IS the source of truth for bot state — the server's
	// stub hash will always lag/differ due to network delay and lossy encoding.
	// Skip desync detection entirely to prevent resync storms that overwrite
	// our valid local simulation with stale server stubs.
	if (g_NetLocalBotAuthority) {
		return src->error;
	}

	// Validate bot count matches
	if (botcount != g_BotCount) {
		sysLogPrintf(LOG_WARNING, "NET: chr sync mismatch at tick %u: server has %u bots, we have %u",
			tick, botcount, g_BotCount);
		g_NetChrDesyncCount++;
	} else {
		// Compare checksums
		u32 localCrc = netChrSyncChecksum();
		if (localCrc != serverCrc) {
			sysLogPrintf(LOG_WARNING, "NET: chr desync detected at tick %u: server=0x%08x local=0x%08x",
				tick, serverCrc, localCrc);
			g_NetChrDesyncCount++;
		} else {
			g_NetChrDesyncCount = 0;
		}
	}

	// After consecutive desyncs, request full resync from server.
	// Use the pending-flag pattern (not a direct write to g_NetMsgRel) because this handler runs
	// inside netStartFrame()'s recv dispatch — netStartFrame resets g_NetMsgRel after the loop,
	// so any direct write here would be silently dropped. netEndFrame consumes the flag instead.
	if (g_NetChrDesyncCount >= NET_DESYNC_THRESHOLD &&
		(g_NetTick - g_NetChrResyncLastReq) > NET_RESYNC_COOLDOWN) {
		sysLogPrintf(LOG_WARNING, "NET: requesting chr resync after %u consecutive desyncs", g_NetChrDesyncCount);
		g_NetPendingResyncReqFlags |= NET_RESYNC_FLAG_CHRS;
		g_NetChrResyncLastReq = g_NetTick;
		g_NetChrDesyncCount = 0;
	}

	return src->error;
}

/*
 * Event-driven dirty flag tracking for prop state sync.
 *
 * The write functions (SvcPropMove, SvcPropDoor, etc.) mark a prop dirty
 * when they send state. The 120-tick PROP_SYNC heartbeat only CRCs dirty
 * props, then clears the flags. If nothing changed since the last heartbeat,
 * the scan and the message are skipped entirely (O(1) vs O(N_props)).
 *
 * The heartbeat hashes the complete active set; dirty state is therefore a
 * wake-up latch, not a per-syncid bitmap. Runtime syncids are monotonic and
 * may exceed the fixed O(1) lookup window, so keying this latch by raw syncid
 * would silently suppress heartbeats late in a match.
 */
static s32 s_PropDirtyCount = 0;

void netPropMarkDirty(u32 syncid)
{
	if (syncid > 0 && netSyncIdLookup(syncid)) {
		s_PropDirtyCount = 1;
	}
}

static void netPropDirtyBitsClear(void)
{
	s_PropDirtyCount = 0;
}

/**
 * Compute a rolling checksum over active props that have syncids.
 * Covers autoguns, doors, lifts, and hover vehicles — the key dynamic
 * entity types whose state must agree between server and clients.
 *
 * Both server and client call this with identical inputs so the CRC
 * is comparable. Dirty flags only gate whether the server sends the
 * message at all — not which props contribute to the hash.
 */
static u32 netPropSyncChecksum(u32 *out_count)
{
	u32 crc = 0;
	u32 count = 0;

	struct prop *prop = g_Vars.activeprops;
	while (prop) {
		if (prop->syncid && prop->type == PROPTYPE_OBJ && prop->obj) {
			struct defaultobj *obj = prop->obj;
			u8 objtype = obj->type;

			// Only checksum the entity types we care about for sync
			if (objtype == OBJTYPE_AUTOGUN || objtype == OBJTYPE_DOOR ||
				objtype == OBJTYPE_LIFT || objtype == OBJTYPE_HOVERPROP ||
				objtype == OBJTYPE_HOVERBIKE || objtype == OBJTYPE_HOVERCAR ||
				objtype == OBJTYPE_GLASS || objtype == OBJTYPE_TINTEDGLASS) {

				u32 px = *(u32 *)&prop->pos.x;
				u32 py = *(u32 *)&prop->pos.y;
				u32 pz = *(u32 *)&prop->pos.z;
				u32 hf = obj->hidden;
				u32 dm = (u32)obj->damage;

				crc ^= px ^ (py << 5) ^ (pz << 11) ^ (hf << 17) ^ (dm << 23) ^ ((u32)objtype << 28);
				crc = (crc << 7) | (crc >> 25);
				++count;
			}
		}
		prop = prop->next;
	}

	if (out_count) {
		*out_count = count;
	}
	return crc;
}

u32 netmsgSvcPropSyncWrite(struct netbuf *dst)
{
	if (s_PropDirtyCount == 0) {
		return 0;
	}

	u32 propcount = 0;
	u32 checksum = netPropSyncChecksum(&propcount);

	netPropDirtyBitsClear();

	if (propcount == 0) {
		return 0;
	}

	netbufWriteU8(dst, SVC_PROP_SYNC);
	netbufWriteU32(dst, g_NetTick);
	netbufWriteU16(dst, (u16)propcount);
	netbufWriteU32(dst, checksum);
	return dst->error;
}

/**
 * SVC_PROP_SYNC (0x37) read handler — retained for backward-compat with old
 * server versions. Snapshot dirty detection (netPropDirtyCheck) is the active
 * mechanism; this just consumes bytes when old servers send the legacy CRC.
 */
u32 netmsgSvcPropSyncRead(struct netbuf *src, struct netclient *srccl)
{
	(void)srccl;
	netbufReadU32(src);  /* tick */
	netbufReadU16(src);  /* propcount */
	netbufReadU32(src);  /* legacy CRC — no longer compared */
	return src->error;
}

/* ========================================================================
 * CLC_RESYNC_REQ - Client requests full state resync from server.
 * Flags byte: bit 0 = request chr resync, bit 1 = request prop resync.
 * Server sets g_NetPendingResyncFlags which netEndFrame consumes.
 * ======================================================================== */

u32 netmsgClcResyncReqWrite(struct netbuf *dst, u8 flags)
{
	netbufWriteU8(dst, CLC_RESYNC_REQ);
	netbufWriteU8(dst, flags);
	return dst->error;
}

u32 netmsgClcResyncReqRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 flags = netbufReadU8(src);

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	sysLogPrintf(LOG_NOTE, "NET: client %d requested resync (flags=0x%02x)", srccl->id, flags);
	g_NetPendingResyncFlags |= flags;

	return src->error;
}

/* ========================================================================
 * CLC_COOP_READY - Client signals ready to start a co-op mission
 * Sent after the client has accepted the host's mission selection.
 * When all clients are ready, the server initiates stage load.
 * ======================================================================== */

u32 netmsgClcCoopReadyWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, CLC_COOP_READY);
	return dst->error;
}

u32 netmsgClcCoopReadyRead(struct netbuf *src, struct netclient *srccl)
{
	if (srccl->state < CLSTATE_LOBBY) {
		return 1;
	}

	// Ignore late CLC_COOP_READY if game has already started (race condition prevention)
	if (srccl->state >= CLSTATE_GAME) {
		sysLogPrintf(LOG_WARNING, "NET: ignored late CLC_COOP_READY from client %d (game already started)", srccl->id);
		return 0;
	}

	srccl->flags |= CLFLAG_COOPREADY;

	// count total ready clients
	u32 readycount = 0;
	for (s32 i = 0; i < g_NetMaxClients; ++i) {
		if (g_NetClients[i].state >= CLSTATE_LOBBY && (g_NetClients[i].flags & CLFLAG_COOPREADY)) {
			readycount++;
		}
	}

	sysLogPrintf(LOG_NOTE, "NET: CLC_COOP_READY client %d (%s) ready, %u/%u clients ready",
		srccl->id, srccl->settings.name, readycount, g_NetNumClients);

	// check if all connected clients are ready
	if (g_NetGameMode == NETGAMEMODE_COOP || g_NetGameMode == NETGAMEMODE_ANTI) {
		bool allready = true;
		for (s32 i = 0; i < g_NetMaxClients; ++i) {
			if (g_NetClients[i].state >= CLSTATE_LOBBY && !(g_NetClients[i].flags & CLFLAG_COOPREADY)) {
				allready = false;
				break;
			}
		}
		if (allready && g_NetNumClients > 0) {
			sysLogPrintf(LOG_NOTE,
				"NET: legacy CLC_COOP_READY quorum observed; manifest ready gate remains sole launch authority");
		}
	}

	return 0;
}

/* ========================================================================
 * SVC_CHR_RESYNC - Full state correction for all bots.
 * Server sends this on demand when a client requests it (after desync).
 * Contains a complete state dump of every active bot: position, angle,
 * rooms, speed, actions, health, shield, weapon, ammo, team, blur,
 * fade alpha, respawn state, target, and hidden flags.
 * ======================================================================== */

u32 netmsgSvcChrResyncWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, SVC_CHR_RESYNC);
	netbufWriteU32(dst, g_NetTick);
	netbufWriteU8(dst, (u8)g_BotCount);

	for (s32 i = 0; i < g_BotCount; ++i) {
		struct chrdata *chr = g_MpBotChrPtrs[i];

		if (!chr || !chr->prop || !chr->aibot) {
			// Write a null prop ptr — client will skip this entry
			netbufWriteU32(dst, 0);
			continue;
		}

		struct prop *prop = chr->prop;
		struct aibot *aibot = chr->aibot;

		const u8 chrflags = (chrIsDead(chr) ? (1 << 0) : 0)
			| ((chr->hidden & CHRHFLAG_CLOAKED) ? (1 << 1) : 0)
			| ((chr->target >= 0) ? (1 << 2) : 0)
			| (aibot->respawning ? (1 << 3) : 0);

		// Identity
		netbufWritePropPtr(dst, prop);

		// Position and orientation
		netbufWriteCoord(dst, &prop->pos);
		netbufWriteF32(dst, chrGetInverseTheta(chr));
		netbufWriteRooms(dst, prop->rooms, ARRAYCOUNT(prop->rooms));

		// Movement
		netbufWriteF32(dst, aibot->speedmultforwards);
		netbufWriteF32(dst, aibot->speedmultsideways);
		netbufWriteF32(dst, aibot->speedtheta);

		// Actions
		netbufWriteS8(dst, chr->myaction);
		netbufWriteU8(dst, chr->actiontype);

		// State flags
		netbufWriteU8(dst, chrflags);

		// Health and combat
		netbufWriteF32(dst, chr->damage);
		netbufWriteF32(dst, chr->cshield);
		netWriteWeaponRef(dst, aibot->weaponnum);
		netbufWriteS8(dst, aibot->gunfunc);
		netbufWriteS16(dst, aibot->loadedammo[0]);
		netbufWriteS16(dst, aibot->loadedammo[1]);

		// Team and status
		netbufWriteS8(dst, chr->team);
		netbufWriteF32(dst, chr->blurdrugamount);
		netbufWriteU8(dst, chr->fadealpha);
		netbufWriteU8(dst, aibot->fadeintimer60);

		// Target (as prop ptr if valid)
		if (chr->target >= 0 && chr->target < g_Vars.maxprops) {
			netbufWritePropPtr(dst, &g_Vars.props[chr->target]);
		} else {
			netbufWriteU32(dst, 0);
		}
	}

	return dst->error;
}

u32 netmsgSvcChrResyncRead(struct netbuf *src, struct netclient *srccl)
{
	const u32 tick = netbufReadU32(src);
	const u8 botcount = netbufReadU8(src);

	if (src->error) {
		return src->error;
	}

	sysLogPrintf(LOG_NOTE, "NET: received chr resync at tick %u for %u bots", tick, botcount);

	// Bot authority client must never accept server resync — our local simulation
	// is canonical and the server only has lightweight stubs. Consume the message
	// (read all fields) but don't apply state changes.
	const bool skipApply = g_NetLocalBotAuthority;
	if (skipApply) {
		sysLogPrintf(LOG_NOTE, "NET: skipping chr resync apply — we are bot authority");
	}

	for (u8 i = 0; i < botcount; ++i) {
		struct prop *prop = netbufReadPropPtr(src);

		// Position and orientation
		struct coord pos;
		netbufReadCoord(src, &pos);
		f32 angle = netbufReadF32(src);
		RoomNum rooms[8] = { -1 };
		netbufReadRooms(src, rooms, ARRAYCOUNT(rooms));

		// Movement
		f32 speedfwd = netbufReadF32(src);
		f32 speedside = netbufReadF32(src);
		f32 speedtheta = netbufReadF32(src);

		// Actions
		s8 myaction = netbufReadS8(src);
		u8 actiontype = netbufReadU8(src);

		// State flags
		u8 chrflags = netbufReadU8(src);

		// Health and combat
		f32 damage = netbufReadF32(src);
		f32 shield = netbufReadF32(src);
		s32 weaponnum = netReadWeaponRef(src);
		s8 gunfunc = netbufReadS8(src);
		s16 loadedammo0 = netbufReadS16(src);
		s16 loadedammo1 = netbufReadS16(src);

		// Team and status
		s8 team = netbufReadS8(src);
		f32 blur = netbufReadF32(src);
		u8 fadealpha = netbufReadU8(src);
		u8 fadeintimer60 = netbufReadU8(src);

		// Target
		struct prop *targetprop = netbufReadPropPtr(src);

		if (src->error || srccl->state < CLSTATE_GAME) {
			return src->error;
		}

		if (!prop || !prop->chr || !prop->chr->aibot || skipApply) {
			continue;
		}

		struct chrdata *chr = prop->chr;
		struct aibot *aibot = chr->aibot;

		// Apply full state correction
		chr->prevpos = prop->pos;
		prop->pos = pos;
		chrSetLookAngle(chr, angle);
		for (s32 r = 0; r < ARRAYCOUNT(prop->rooms); ++r) {
			prop->rooms[r] = rooms[r];
		}

		aibot->speedmultforwards = speedfwd;
		aibot->speedmultsideways = speedside;
		aibot->speedtheta = speedtheta;

		chr->myaction = myaction;
		chr->actiontype = actiontype;

		chr->damage = damage;
		chr->cshield = shield;
		if (weaponnum >= WEAPON_UNARMED && weaponnum < WEAPON_CUSTOM_END) {
			aibot->weaponnum = weaponnum;
		}
		aibot->gunfunc = gunfunc;
		aibot->loadedammo[0] = loadedammo0;
		aibot->loadedammo[1] = loadedammo1;

		chr->team = team;
		chr->blurdrugamount = blur;
		chr->fadealpha = fadealpha;
		aibot->respawning = (chrflags >> 3) & 1;
		aibot->fadeintimer60 = fadeintimer60;

		if (targetprop) {
			chr->target = targetprop - g_Vars.props;
		} else {
			chr->target = -1;
		}
	}

	// Reset desync counter — we just got a fresh full state
	g_NetChrDesyncCount = 0;

	return src->error;
}

/* ========================================================================
 * SVC_PROP_RESYNC - Full state correction for tracked props.
 * Server sends this on demand when a client requests it (after desync).
 * Iterates active props with syncids and sends position, damage, hidden
 * flags, and type-specific state for key entity types.
 * ======================================================================== */

u32 netmsgSvcPropResyncWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, SVC_PROP_RESYNC);
	netbufWriteU32(dst, g_NetTick);

	// First pass: count props to send
	u16 count = 0;
	struct prop *prop = g_Vars.activeprops;
	while (prop) {
		if (prop->syncid && prop->type == PROPTYPE_OBJ && prop->obj) {
			u8 objtype = prop->obj->type;
			if (objtype == OBJTYPE_AUTOGUN || objtype == OBJTYPE_DOOR ||
				objtype == OBJTYPE_LIFT || objtype == OBJTYPE_HOVERPROP ||
				objtype == OBJTYPE_HOVERBIKE || objtype == OBJTYPE_HOVERCAR ||
				objtype == OBJTYPE_GLASS || objtype == OBJTYPE_TINTEDGLASS) {
				++count;
			}
		}
		prop = prop->next;
	}

	netbufWriteU16(dst, count);

	// Second pass: write each prop's full state
	prop = g_Vars.activeprops;
	while (prop) {
		if (prop->syncid && prop->type == PROPTYPE_OBJ && prop->obj) {
			struct defaultobj *obj = prop->obj;
			u8 objtype = obj->type;

			if (objtype == OBJTYPE_AUTOGUN || objtype == OBJTYPE_DOOR ||
				objtype == OBJTYPE_LIFT || objtype == OBJTYPE_HOVERPROP ||
				objtype == OBJTYPE_HOVERBIKE || objtype == OBJTYPE_HOVERCAR ||
				objtype == OBJTYPE_GLASS || objtype == OBJTYPE_TINTEDGLASS) {

				netbufWritePropPtr(dst, prop);
				netbufWriteU8(dst, objtype);
				netbufWriteCoord(dst, &prop->pos);
				netbufWriteRooms(dst, prop->rooms, ARRAYCOUNT(prop->rooms));
				netbufWriteS16(dst, obj->damage);
				netbufWriteHidden(dst, obj->hidden);

				// Type-specific state
				if (objtype == OBJTYPE_DOOR) {
					struct doorobj *door = prop->door;
					netbufWriteF32(dst, door->frac);
					netbufWriteF32(dst, door->fracspeed);
					netbufWriteS8(dst, door->mode);
				} else if (objtype == OBJTYPE_AUTOGUN) {
					struct autogunobj *autogun = (struct autogunobj *)obj;
					netbufWriteF32(dst, autogun->yrot);
					netbufWriteF32(dst, autogun->xrot);
					netbufWriteS8(dst, autogun->firing);
					netbufWriteU8(dst, autogun->ammoquantity);
					netbufWritePropPtr(dst, autogun->target);
				} else if (objtype == OBJTYPE_LIFT) {
					struct liftobj *lift = (struct liftobj *)obj;
					netbufWriteF32(dst, lift->dist);
					netbufWriteF32(dst, lift->speed);
					netbufWriteS8(dst, lift->levelcur);
					netbufWriteS8(dst, lift->levelaim);
				}
				// Glass, hover vehicles: position+damage+hidden is sufficient
			}
		}
		prop = prop->next;
	}

	return dst->error;
}

u32 netmsgSvcPropResyncRead(struct netbuf *src, struct netclient *srccl)
{
	const u32 tick = netbufReadU32(src);
	const u16 count = netbufReadU16(src);

	if (src->error) {
		return src->error;
	}

	sysLogPrintf(LOG_NOTE, "NET: received prop resync at tick %u for %u props", tick, count);

	if (count == 0) {
		return src->error;
	}

	for (u16 i = 0; i < count; ++i) {
		struct prop *prop = netbufReadPropPtr(src);
		const u8 objtype = netbufReadU8(src);
		struct coord pos;
		netbufReadCoord(src, &pos);
		RoomNum rooms[8] = { -1 };
		netbufReadRooms(src, rooms, ARRAYCOUNT(rooms));
		const s16 damage = netbufReadS16(src);
		const u32 hidden = netbufReadU32(src);

		// Read type-specific state (must match write order even if prop is NULL)
		f32 door_frac = 0, door_fracspeed = 0;
		s8 door_mode = 0;
		f32 autogun_yrot = 0, autogun_xrot = 0;
		s8 autogun_firing = 0;
		u8 autogun_ammo = 0;
		struct prop *autogun_target = NULL;
		f32 lift_dist = 0, lift_speed = 0;
		s8 lift_levelcur = 0, lift_levelaim = 0;

		if (objtype == OBJTYPE_DOOR) {
			door_frac = netbufReadF32(src);
			door_fracspeed = netbufReadF32(src);
			door_mode = netbufReadS8(src);
		} else if (objtype == OBJTYPE_AUTOGUN) {
			autogun_yrot = netbufReadF32(src);
			autogun_xrot = netbufReadF32(src);
			autogun_firing = netbufReadS8(src);
			autogun_ammo = netbufReadU8(src);
			autogun_target = netbufReadPropPtr(src);
		} else if (objtype == OBJTYPE_LIFT) {
			lift_dist = netbufReadF32(src);
			lift_speed = netbufReadF32(src);
			lift_levelcur = netbufReadS8(src);
			lift_levelaim = netbufReadS8(src);
		}

		if (src->error || srccl->state < CLSTATE_GAME) {
			return src->error;
		}

		if (!prop || !prop->obj) {
			continue;
		}

		struct defaultobj *obj = prop->obj;

		// Apply base state
		prop->pos = pos;
		for (s32 r = 0; r < ARRAYCOUNT(prop->rooms); ++r) {
			prop->rooms[r] = rooms[r];
		}
		obj->damage = damage;
		obj->hidden = hidden;

		// Apply type-specific state
		if (objtype == OBJTYPE_DOOR && obj->type == OBJTYPE_DOOR) {
			struct doorobj *door = prop->door;
			door->frac = door_frac;
			door->fracspeed = door_fracspeed;
			door->mode = door_mode;
		} else if (objtype == OBJTYPE_AUTOGUN && obj->type == OBJTYPE_AUTOGUN) {
			struct autogunobj *autogun = (struct autogunobj *)obj;
			autogun->yrot = autogun_yrot;
			autogun->xrot = autogun_xrot;
			autogun->firing = autogun_firing;
			autogun->ammoquantity = autogun_ammo;
			autogun->target = autogun_target;
		} else if (objtype == OBJTYPE_LIFT && obj->type == OBJTYPE_LIFT) {
			struct liftobj *lift = (struct liftobj *)obj;
			lift->dist = lift_dist;
			lift->speed = lift_speed;
			lift->levelcur = lift_levelcur;
			lift->levelaim = lift_levelaim;
		}
	}

	return src->error;
}

/* ========================================================================
 * NPC Replication (Co-op only)
 *
 * In co-op/counter-op modes, solo mission NPCs are server-authoritative.
 * The server runs NPC AI (chraiExecute) and broadcasts position/state to
 * clients via SVC_NPC_* messages. These are separate from SVC_CHR_* (bots)
 * because NPCs lack the aibot struct — different data payload.
 *
 * NPC replication readiness is stricter than identity. Stage setup can expose
 * a PROPTYPE_CHR slot before its model graph commits; orientation-bearing wire
 * payloads must not observe that transitional owner.
 * ======================================================================== */

/* Canonical predicate shared by NPC scheduling, counts, checksums, writers,
 * and readers. A non-CHRINFO root has no orientation rwdata to require; a
 * CHRINFO root must have both immutable rodata and its live rwdata array. */
bool netNpcIsReplicationReady(const struct chrdata *chr)
{
	struct modelnode *root;

	if (!chr || !chr->prop || chr->prop->type != PROPTYPE_CHR
			|| chr->prop->chr != chr
			|| chr->aibot || !chr->model || !chr->model->definition
			|| !chr->model->definition->rootnode) {
		return false;
	}

	root = chr->model->definition->rootnode;

	if ((root->type & 0xff) == MODELNODETYPE_CHRINFO
			&& (!root->rodata || !chr->model->rwdatas)) {
		return false;
	}

	return true;
}

/* ========================================================================
 * SVC_NPC_MOVE - Server-authoritative NPC position update
 * Sent every few frames for NPCs in co-op mode.
 * Syncs position, facing angle, rooms, and action state.
 * ======================================================================== */

u32 netmsgSvcNpcMoveWrite(struct netbuf *dst, struct chrdata *chr)
{
	if (!netNpcIsReplicationReady(chr)) {
		return dst->error;
	}

	struct prop *prop = chr->prop;

	netbufWriteU8(dst, SVC_NPC_MOVE);
	netbufWritePropPtr(dst, prop);
	netbufWriteCoord(dst, &prop->pos);
	netbufWriteF32(dst, chrGetInverseTheta(chr));
	netbufWriteRooms(dst, prop->rooms, ARRAYCOUNT(prop->rooms));
	netbufWriteS8(dst, chr->myaction);
	netbufWriteU8(dst, chr->actiontype);

	/* v47 (S594h-B Slice 3): surface_up vec3. Always 12 bytes;
	 * non-surface-loco chrs send the chrInit world-up default
	 * (0, 1, 0). Cost ~3 KB/s outbound for typical NPC density. */
	netbufWriteF32(dst, chr->surface_up[0]);
	netbufWriteF32(dst, chr->surface_up[1]);
	netbufWriteF32(dst, chr->surface_up[2]);

	return dst->error;
}

u32 netmsgSvcNpcMoveRead(struct netbuf *src, struct netclient *srccl)
{
	struct prop *prop = netbufReadPropPtr(src);
	struct coord newpos;
	netbufReadCoord(src, &newpos);
	const f32 newangle = netbufReadF32(src);
	s16 newrooms[8] = { -1 };
	netbufReadRooms(src, newrooms, ARRAYCOUNT(newrooms));
	const s8 myaction = netbufReadS8(src);
	const u8 actiontype = netbufReadU8(src);
	const f32 newsurface_x = netbufReadF32(src);
	const f32 newsurface_y = netbufReadF32(src);
	const f32 newsurface_z = netbufReadF32(src);

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	if (!prop || !netNpcIsReplicationReady(prop->chr)
			|| prop->chr->prop != prop) {
		return src->error;
	}

	struct chrdata *chr = prop->chr;

	// Apply server-authoritative position
	chr->prevpos = prop->pos;
	prop->pos = newpos;
	chrSetLookAngle(chr, newangle);
	chr->myaction = myaction;
	chr->actiontype = actiontype;

	/* Apply server-authoritative surface_up. The receiver still runs
	 * its own per-tick sample + 8-frame blend (chrSurfaceLocoTick), so
	 * a wire update either confirms what the local raycast computed
	 * (no visible change) or steers the client toward the host's view
	 * when they disagree (chr straddling a tile boundary, etc). */
	chr->surface_up[0] = newsurface_x;
	chr->surface_up[1] = newsurface_y;
	chr->surface_up[2] = newsurface_z;

	// Update rooms
	for (s32 i = 0; i < ARRAYCOUNT(prop->rooms); ++i) {
		prop->rooms[i] = newrooms[i];
		if (newrooms[i] < 0) {
			break;
		}
	}

	return src->error;
}

/* ========================================================================
 * SVC_NPC_STATE - Server-authoritative NPC state update
 * Sent less frequently than NPC_MOVE. Syncs health, flags, alertness.
 * ======================================================================== */

u32 netmsgSvcNpcStateWrite(struct netbuf *dst, struct chrdata *chr)
{
	if (!netNpcIsReplicationReady(chr)) {
		return dst->error;
	}

	// flags: bit 0 = dead, bit 1 = cloaked, bit 2 = has target, bit 3 = hidden2 (CHRHFLAG_00000001)
	const u8 flags = (chrIsDead(chr) ? (1 << 0) : 0)
		| ((chr->hidden & CHRHFLAG_CLOAKED) ? (1 << 1) : 0)
		| ((chr->target >= 0) ? (1 << 2) : 0);

	netbufWriteU8(dst, SVC_NPC_STATE);
	netbufWritePropPtr(dst, chr->prop);
	netbufWriteU8(dst, flags);
	netbufWriteF32(dst, chr->damage);
	netbufWriteF32(dst, chr->maxdamage);
	netbufWriteU8(dst, chr->alertness);
	netbufWriteS8(dst, chr->team);
	netbufWriteU32(dst, chr->chrflags);
	netbufWriteU32(dst, chr->hidden);
	netbufWriteU8(dst, chr->fadealpha);

	return dst->error;
}

u32 netmsgSvcNpcStateRead(struct netbuf *src, struct netclient *srccl)
{
	struct prop *prop = netbufReadPropPtr(src);
	const u8 flags = netbufReadU8(src);
	const f32 damage = netbufReadF32(src);
	const f32 maxdamage = netbufReadF32(src);
	const u8 alertness = netbufReadU8(src);
	const s8 team = netbufReadS8(src);
	const u32 chrflags = netbufReadU32(src);
	const u32 hidden = netbufReadU32(src);
	const u8 fadealpha = netbufReadU8(src);

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	if (!prop || !netNpcIsReplicationReady(prop->chr)
			|| prop->chr->prop != prop) {
		return src->error;
	}

	struct chrdata *chr = prop->chr;

	chr->damage = damage;
	chr->maxdamage = maxdamage;
	chr->alertness = alertness;
	chr->team = team;
	chr->chrflags = chrflags;
	chr->hidden = hidden;
	chr->fadealpha = fadealpha;

	return src->error;
}

/* ========================================================================
 * SVC_NPC_SYNC - Digest of the immediately preceding full NPC snapshot.
 * This message is valid only as the second half of one reliable ordered
 * SVC_NPC_RESYNC transaction; there is deliberately no periodic live digest.
 * ======================================================================== */

u32 netNpcCount(void)
{
	u32 count = 0;
	for (s32 i = 0; i < g_NumChrSlots; ++i) {
		if (netNpcIsReplicationReady(&g_ChrSlots[i])) {
			++count;
		}
	}
	return count;
}

static int netNpcDigestCompare(const void *a, const void *b)
{
	const struct chrdata *const left = *(struct chrdata *const *)a;
	const struct chrdata *const right = *(struct chrdata *const *)b;
	const u32 left_id = left->prop->syncid;
	const u32 right_id = right->prop->syncid;

	return left_id < right_id ? -1 : left_id > right_id ? 1 : 0;
}

static u32 netNpcFloatBits(f32 value)
{
	u32 bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static u32 netNpcDigestMixU32(u32 digest, u32 value)
{
	/* Explicit byte order keeps the digest a protocol value instead of a host
	 * representation accident. FNV-1a is compact and deterministic here; the
	 * protocol's authenticity comes from the trusted ENet authority channel. */
	for (u32 shift = 0; shift < 32; shift += 8) {
		digest ^= (value >> shift) & 0xffu;
		digest *= 16777619u;
	}
	return digest;
}

static u8 netNpcSnapshotFlags(const struct chrdata *chr,
		const struct prop *target)
{
	return (chrIsDead((struct chrdata *)chr) ? (1 << 0) : 0)
		| ((chr->hidden & CHRHFLAG_CLOAKED) ? (1 << 1) : 0)
		| (target ? (1 << 2) : 0);
}

/* Match the target field actually serialized by SVC_NPC_RESYNC. A target of
 * -1 has local p1p2 fallback semantics in chrGetTargetProp, but no target
 * syncid is present on this wire snapshot and therefore must not enter its
 * digest. */
static struct prop *netNpcSnapshotTarget(const struct chrdata *chr)
{
	struct prop *target;

	if (!chr || !g_Vars.props || chr->target < 0
			|| chr->target >= g_Vars.maxprops) {
		return NULL;
	}
	target = &g_Vars.props[chr->target];
	return target->syncid != 0 ? target : NULL;
}

static bool netNpcSnapshotDigest(u16 *out_count, u32 *out_checksum)
{
	const u32 count = netNpcCount();
	struct chrdata **ordered = NULL;
	u32 emitted = 0;
	u32 digest = 2166136261u;

	if (!out_count || !out_checksum || count > 0xffffu) {
		return false;
	}
	if (count > 0) {
		ordered = malloc((size_t)count * sizeof(*ordered));
		if (!ordered) {
			return false;
		}
	}

	for (s32 i = 0; i < g_NumChrSlots; ++i) {
		struct chrdata *chr = &g_ChrSlots[i];
		if (!netNpcIsReplicationReady(chr)) {
			continue;
		}
		if (chr->prop->syncid == 0 || emitted >= count) {
			free(ordered);
			return false;
		}
		ordered[emitted++] = chr;
	}
	if (emitted != count) {
		free(ordered);
		return false;
	}
	if (count > 1) {
		qsort(ordered, (size_t)count, sizeof(*ordered),
			netNpcDigestCompare);
	}

	for (u32 i = 0; i < count; i++) {
		const struct chrdata *chr = ordered[i];
		const struct prop *target = netNpcSnapshotTarget(chr);
		if (i > 0 && ordered[i - 1]->prop->syncid == chr->prop->syncid) {
			free(ordered);
			return false;
		}
		digest = netNpcDigestMixU32(digest, chr->prop->syncid);
		digest = netNpcDigestMixU32(digest,
			netNpcFloatBits(chr->prop->pos.x));
		digest = netNpcDigestMixU32(digest,
			netNpcFloatBits(chr->prop->pos.y));
		digest = netNpcDigestMixU32(digest,
			netNpcFloatBits(chr->prop->pos.z));
		digest = netNpcDigestMixU32(digest,
			netNpcFloatBits(chrGetInverseTheta((struct chrdata *)chr)));
		for (s32 room = 0; room < ARRAYCOUNT(chr->prop->rooms); room++) {
			digest = netNpcDigestMixU32(digest,
				(u32)(u16)chr->prop->rooms[room]);
			if (chr->prop->rooms[room] < 0) {
				break;
			}
		}
		digest = netNpcDigestMixU32(digest, (u32)(u8)chr->myaction);
		digest = netNpcDigestMixU32(digest, (u32)chr->actiontype);
		digest = netNpcDigestMixU32(digest,
			(u32)netNpcSnapshotFlags(chr, target));
		digest = netNpcDigestMixU32(digest,
			netNpcFloatBits(chr->damage));
		digest = netNpcDigestMixU32(digest,
			netNpcFloatBits(chr->maxdamage));
		digest = netNpcDigestMixU32(digest, (u32)chr->alertness);
		digest = netNpcDigestMixU32(digest, (u32)(u8)chr->team);
		digest = netNpcDigestMixU32(digest, chr->chrflags);
		digest = netNpcDigestMixU32(digest, chr->hidden);
		digest = netNpcDigestMixU32(digest, (u32)chr->fadealpha);
		digest = netNpcDigestMixU32(digest,
			target ? target->syncid : 0);
	}

	free(ordered);
	*out_count = (u16)count;
	*out_checksum = digest;
	return true;
}

u32 netmsgSvcNpcSyncWrite(struct netbuf *dst)
{
	const u32 wp_before = dst ? dst->wp : 0;
	const u32 error_before = dst ? dst->error : 1;
	u16 count;
	u32 checksum;

	if (!dst || error_before) {
		return 1;
	}

	if (!netNpcSnapshotDigest(&count, &checksum)) {
		sysLogPrintf(LOG_WARNING,
			"NET: SVC_NPC_SYNC rejected noncanonical snapshot");
		return 1;
	}

	netbufWriteU8(dst, SVC_NPC_SYNC);
	netbufWriteU32(dst, g_NetTick);
	netbufWriteU16(dst, count);
	netbufWriteU32(dst, checksum);

	if (dst->error) {
		dst->wp = wp_before;
		dst->error = error_before;
		return 1;
	}

	return 0;
}

u32 netmsgSvcNpcSyncRead(struct netbuf *src, struct netclient *srccl)
{
	const u32 tick = netbufReadU32(src);
	const u16 npccount = netbufReadU16(src);
	const u32 server_checksum = netbufReadU32(src);
	const net_npc_snapshot_validation_t applied =
		s_NetNpcSnapshotValidation;
	bool matches;

	if (src->error || !srccl || srccl->state < CLSTATE_GAME) {
		return src->error ? src->error : 1;
	}
	memset(&s_NetNpcSnapshotValidation, 0,
		sizeof(s_NetNpcSnapshotValidation));
	matches = applied.pending
		&& applied.stage_epoch == g_NetStageEpoch
		&& applied.tick == tick
		&& applied.count == npccount
		&& applied.checksum == server_checksum;

	if (!matches) {
		sysLogPrintf(LOG_WARNING,
			"NET: SVC_NPC_SYNC snapshot mismatch epoch=%u tick=%u count=%u checksum=0x%08x pending=%u applied_epoch=%u applied_tick=%u applied_count=%u applied_checksum=0x%08x",
			(unsigned)g_NetStageEpoch, (unsigned)tick,
			(unsigned)npccount, (unsigned)server_checksum,
			applied.pending ? 1u : 0u, (unsigned)applied.stage_epoch,
			(unsigned)applied.tick, (unsigned)applied.count,
			(unsigned)applied.checksum);
		g_NetNpcDesyncCount++;
		if (g_NetNpcResyncLastReq == 0
				|| (g_NetTick - g_NetNpcResyncLastReq)
					> NET_RESYNC_COOLDOWN) {
			sysLogPrintf(LOG_WARNING,
				"NET: requesting npc resync after atomic snapshot mismatch");
			g_NetPendingResyncReqFlags |= NET_RESYNC_FLAG_NPCS;
			g_NetNpcResyncLastReq = g_NetTick;
		}
	} else {
		g_NetNpcDesyncCount = 0;
		sysLogPrintf(LOG_NOTE,
			"NET: SVC_NPC_SYNC snapshot verified epoch=%u tick=%u count=%u checksum=0x%08x",
			(unsigned)g_NetStageEpoch, (unsigned)tick,
			(unsigned)npccount, (unsigned)server_checksum);
	}

	return src->error;
}

/* ========================================================================
 * SVC_NPC_RESYNC - Full NPC state dump for correction after desync
 * ======================================================================== */

u32 netmsgSvcNpcResyncWrite(struct netbuf *dst)
{
	const u32 count = netNpcCount();
	const u32 wp_before = dst ? dst->wp : 0;
	const u32 error_before = dst ? dst->error : 1;
	u32 emitted = 0;

	if (!dst || error_before) {
		return 1;
	}

	if (count > 0xffffu) {
		sysLogPrintf(LOG_WARNING,
			"NET: SVC_NPC_RESYNC rejected unrepresentable npc count %u",
			count);
		return 1;
	}

	sysLogPrintf(LOG_NOTE, "NET: SVC_NPC_RESYNC write %u npcs", count);

	netbufWriteU8(dst, SVC_NPC_RESYNC);
	netbufWriteU32(dst, g_NetTick);
	netbufWriteU16(dst, (u16)count);

	for (s32 i = 0; i < g_NumChrSlots; ++i) {
		struct chrdata *chr = &g_ChrSlots[i];
		if (!netNpcIsReplicationReady(chr)) {
			continue;
		}

		struct prop *prop = chr->prop;
		struct prop *targetprop = netNpcSnapshotTarget(chr);

		const u8 flags = netNpcSnapshotFlags(chr, targetprop);

		// Identity
		netbufWritePropPtr(dst, prop);

		// Position and orientation
		netbufWriteCoord(dst, &prop->pos);
		netbufWriteF32(dst, chrGetInverseTheta(chr));
		netbufWriteRooms(dst, prop->rooms, ARRAYCOUNT(prop->rooms));

		// Actions
		netbufWriteS8(dst, chr->myaction);
		netbufWriteU8(dst, chr->actiontype);

		// State
		netbufWriteU8(dst, flags);
		netbufWriteF32(dst, chr->damage);
		netbufWriteF32(dst, chr->maxdamage);
		netbufWriteU8(dst, chr->alertness);
		netbufWriteS8(dst, chr->team);
		netbufWriteU32(dst, chr->chrflags);
		netbufWriteU32(dst, chr->hidden);
		netbufWriteU8(dst, chr->fadealpha);

		// Target
		netbufWritePropPtr(dst, targetprop);

		emitted++;
	}

	if (dst->error || emitted != count) {
		sysLogPrintf(LOG_WARNING,
			"NET: SVC_NPC_RESYNC write rolled back expected=%u emitted=%u buffer_error=%u",
			count, emitted, dst->error ? 1u : 0u);
		dst->wp = wp_before;
		dst->error = error_before;
		return 1;
	}

	return 0;
}

struct net_npc_resync_record {
	struct prop *prop;
	struct coord pos;
	f32 angle;
	RoomNum rooms[8];
	s8 myaction;
	u8 actiontype;
	u8 flags;
	f32 damage;
	f32 maxdamage;
	u8 alertness;
	s8 team;
	u32 chrflags;
	u32 hidden;
	u8 fadealpha;
	struct prop *targetprop;
	bool target_valid;
};

static void netmsgNpcResyncRecordRead(struct netbuf *src,
		struct net_npc_resync_record *record)
{
	record->prop = netbufReadPropPtr(src);
	netbufReadCoord(src, &record->pos);
	record->angle = netbufReadF32(src);

	for (s32 i = 0; i < ARRAYCOUNT(record->rooms); i++) {
		record->rooms[i] = -1;
	}

	netbufReadRooms(src, record->rooms, ARRAYCOUNT(record->rooms));
	record->myaction = netbufReadS8(src);
	record->actiontype = netbufReadU8(src);
	record->flags = netbufReadU8(src);
	record->damage = netbufReadF32(src);
	record->maxdamage = netbufReadF32(src);
	record->alertness = netbufReadU8(src);
	record->team = netbufReadS8(src);
	record->chrflags = netbufReadU32(src);
	record->hidden = netbufReadU32(src);
	record->fadealpha = netbufReadU8(src);
	const u32 target_syncid = netbufReadU32(src);
	record->targetprop = target_syncid ? netSyncIdLookup(target_syncid) : NULL;
	record->target_valid = target_syncid == 0 || record->targetprop != NULL;

	if (!src->error && !record->target_valid) {
		sysLogPrintf(LOG_WARNING,
			"NET: NPC resync target with syncid %u does not exist",
			target_syncid);
	}
}

static bool netmsgNpcResyncRecordIsReady(
		const struct net_npc_resync_record *record)
{
	return record->prop
		&& netNpcIsReplicationReady(record->prop->chr)
		&& record->prop->chr->prop == record->prop
		&& record->target_valid
		&& (!!(record->flags & (1 << 2)) == !!record->targetprop);
}

static void netmsgNpcResyncRecordApply(
		const struct net_npc_resync_record *record)
{
	struct prop *prop = record->prop;
	struct chrdata *chr = prop->chr;

	chr->prevpos = prop->pos;
	prop->pos = record->pos;
	chrSetLookAngle(chr, record->angle);

	for (s32 i = 0; i < ARRAYCOUNT(prop->rooms); i++) {
		prop->rooms[i] = record->rooms[i];

		if (record->rooms[i] < 0) {
			break;
		}
	}

	chr->myaction = record->myaction;
	chr->actiontype = record->actiontype;
	chr->damage = record->damage;
	chr->maxdamage = record->maxdamage;
	chr->alertness = record->alertness;
	chr->team = record->team;
	chr->chrflags = record->chrflags;
	chr->hidden = record->hidden;
	chr->fadealpha = record->fadealpha;
	chr->target = record->targetprop
		? (s32)(record->targetprop - g_Vars.props)
		: -1;
}

u32 netmsgSvcNpcResyncRead(struct netbuf *src, struct netclient *srccl)
{
	const u32 tick = netbufReadU32(src);
	const u16 npccount = netbufReadU16(src);
	const u32 records_rp = src->rp;
	const u32 local_count = netNpcCount();
	bool complete = local_count == npccount;
	u16 applied_count = 0;
	u32 applied_checksum = 0;

	memset(&s_NetNpcSnapshotValidation, 0,
		sizeof(s_NetNpcSnapshotValidation));
	if (src->error || !srccl || srccl->state < CLSTATE_GAME
			|| !srccl->stage_ready || g_NetStageEpoch == 0) {
		return src->error ? src->error : 1;
	}

	/* Validate and consume the complete advertised payload before mutating any
	 * local NPC. A second pass applies only after every identity/model owner is
	 * ready, so a late invalid record cannot leave a partial resync behind. */
	for (u16 i = 0; i < npccount; ++i) {
		struct net_npc_resync_record record;
		netmsgNpcResyncRecordRead(src, &record);

		if (src->error) {
			return src->error;
		}

		if (!netmsgNpcResyncRecordIsReady(&record)) {
			complete = false;
		}
	}

	if (!complete) {
		sysLogPrintf(LOG_WARNING,
			"NET: SVC_NPC_RESYNC rejected incomplete snapshot tick=%u server_count=%u local_ready_count=%u",
			tick, npccount, local_count);
		g_NetPendingResyncReqFlags |= NET_RESYNC_FLAG_NPCS;
		return 1;
	}

	src->rp = records_rp;

	for (u16 i = 0; i < npccount; ++i) {
		struct net_npc_resync_record record;
		netmsgNpcResyncRecordRead(src, &record);

		if (src->error || !netmsgNpcResyncRecordIsReady(&record)) {
			return src->error ? src->error : 1;
		}

		netmsgNpcResyncRecordApply(&record);
	}
	if (!netNpcSnapshotDigest(&applied_count, &applied_checksum)
			|| applied_count != npccount) {
		sysLogPrintf(LOG_WARNING,
			"NET: SVC_NPC_RESYNC rejected noncanonical applied snapshot tick=%u server_count=%u applied_count=%u",
			(unsigned)tick, (unsigned)npccount,
			(unsigned)applied_count);
		g_NetPendingResyncReqFlags |= NET_RESYNC_FLAG_NPCS;
		return 1;
	}
	s_NetNpcSnapshotValidation.pending = true;
	s_NetNpcSnapshotValidation.stage_epoch = g_NetStageEpoch;
	s_NetNpcSnapshotValidation.tick = tick;
	s_NetNpcSnapshotValidation.count = applied_count;
	s_NetNpcSnapshotValidation.checksum = applied_checksum;

	sysLogPrintf(LOG_NOTE,
		"NET: SVC_NPC_RESYNC read %u npcs at tick %u epoch=%u snapshot_pending=1 checksum=0x%08x",
		(unsigned)npccount, (unsigned)tick,
		(unsigned)g_NetStageEpoch, (unsigned)applied_checksum);
	g_NetNpcDesyncCount = 0;

	return src->error;
}

/* ========================================================================
 * SVC_STAGE_FLAG - Server-authoritative stage flags sync (co-op only)
 * Sent whenever g_StageFlags changes on the server (via AI scripts).
 * Stage flags drive objective completion (COMPFLAGS/FAILFLAGS types).
 * ======================================================================== */

u32 netmsgSvcStageFlagWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, SVC_STAGE_FLAG);
	netbufWriteU32(dst, g_StageFlags);
	return dst->error;
}

u32 netmsgSvcStageFlagRead(struct netbuf *src, struct netclient *srccl)
{
	const u32 flags = netbufReadU32(src);

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE_FLAG read flags=0x%08x", flags);

	g_StageFlags = flags;
	scenarioSourceObjectiveGraphRecordStageFlags(g_StageFlags);

	return src->error;
}

/* ========================================================================
 * SVC_OBJ_STATUS - Server-authoritative objective status update (co-op only)
 * Sent when an objective status changes on the server.
 * Client receives this and updates g_ObjectiveStatuses[] + shows HUD msg.
 * ======================================================================== */

u32 netmsgSvcObjStatusWrite(struct netbuf *dst, u8 index, u8 status)
{
	netbufWriteU8(dst, SVC_OBJ_STATUS);
	netbufWriteU8(dst, index);
	netbufWriteU8(dst, status);
	return dst->error;
}

u32 netmsgSvcObjStatusRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 index = netbufReadU8(src);
	const u8 status = netbufReadU8(src);

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	if (index >= MAX_OBJECTIVES) {
		return src->error;
	}

	sysLogPrintf(LOG_NOTE, "NET: SVC_OBJ_STATUS read objective %u status=%u", index, status);

	if (g_ObjectiveStatuses[index] != status) {
		g_ObjectiveStatuses[index] = status;

		// Show HUD message for objective status change (mirrors objectivesCheckAll behavior)
		if (g_Objectives[index]
				&& (objectiveGetDifficultyBits(index) & (1 << lvGetDifficulty()))) {
			// Count which "available" index this is (for display numbering)
			s32 availableindex = 0;
			for (s32 i = 0; i < index; ++i) {
				if (objectiveGetDifficultyBits(i) & (1 << lvGetDifficulty())) {
					availableindex++;
				}
			}

			char buffer[50] = "";
			snprintf(buffer, sizeof(buffer), "%s %d: ", langGet(L_MISC_044), availableindex + 1); // "Objective N: "

			if (status == OBJECTIVE_COMPLETE) {
				strncat(buffer, langGet(L_MISC_045), sizeof(buffer) - strlen(buffer) - 1); // "Completed"
				hudmsgCreateWithFlags(buffer, HUDMSGTYPE_OBJECTIVECOMPLETE, HUDMSGFLAG_ALLOWDUPES);
			} else if (status == OBJECTIVE_INCOMPLETE) {
				strncat(buffer, langGet(L_MISC_046), sizeof(buffer) - strlen(buffer) - 1); // "Incomplete"
				hudmsgCreateWithFlags(buffer, HUDMSGTYPE_OBJECTIVECOMPLETE, HUDMSGFLAG_ALLOWDUPES);
			} else if (status == OBJECTIVE_FAILED) {
				strncat(buffer, langGet(L_MISC_047), sizeof(buffer) - strlen(buffer) - 1); // "Failed"
				hudmsgCreateWithFlags(buffer, HUDMSGTYPE_OBJECTIVEFAILED, HUDMSGFLAG_ALLOWDUPES);
			}
		}
	}

	return src->error;
}

/* ========================================================================
 * SVC_ALARM - Server-authoritative alarm state sync (co-op only)
 * Sent when alarm is activated or deactivated on the server.
 * ======================================================================== */

u32 netmsgSvcAlarmWrite(struct netbuf *dst, u8 active)
{
	netbufWriteU8(dst, SVC_ALARM);
	netbufWriteU8(dst, active);
	return dst->error;
}

u32 netmsgSvcAlarmRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 active = netbufReadU8(src);

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	sysLogPrintf(LOG_NOTE, "NET: SVC_ALARM read active=%u", active);

	if (active) {
		if (g_AlarmTimer < 1) {
			g_AlarmTimer = 1;
		}
	} else {
		g_AlarmTimer = 0;
		alarmStopAudio();
	}

	return src->error;
}

/* ========================================================================
 * v56 cutscene authority.
 *
 * One match-scoped reliable stream owns every START, ACCEPT, and END event.
 * Its roster is frozen from the transactionally validated SVC_STAGE_START
 * roster, never rebuilt from disconnect-sensitive live netclient state. The
 * stream is sent only to that match room on NETCHAN_DEFAULT, so ENet ordering
 * plus stage-start/stage-end reset is the transport epoch.
 * ======================================================================== */

_Static_assert(NET_MAX_CLIENTS == NET_CUTSCENE_AUTHORITY_MAX_CLIENTS,
	"cutscene client-mask domain must match NET_MAX_CLIENTS");
_Static_assert(MAX_PLAYERS == NET_CUTSCENE_AUTHORITY_MAX_PLAYERS,
	"cutscene runtime-player domain must match MAX_PLAYERS");
_Static_assert(NET_CUTSCENE_AUTHORITY_EVENT_CAPACITY
		* (1u + NET_CUTSCENE_STATE_WIRE_SIZE)
		<= NET_CUTSCENE_AUTHORITY_PACKET_CAPACITY,
	"cutscene authority packet must fit the worst-case queued batch");

typedef struct netmsg_cutscene_event_s {
	u8 message_id;
	net_cutscene_state_wire_t state;
	net_cutscene_skip_accept_t accept;
} netmsg_cutscene_event_t;

typedef struct netmsg_cutscene_authority_s {
	bool match_active;
	u8 room_id;
	net_cutscene_participant_t participants[MAX_PLAYERS];
	size_t participant_count;
	u32 full_client_mask;
	u8 full_player_mask;
	net_cutscene_authority_tracker_t tracker;
	u32 accepted_client_mask;
	netmsg_cutscene_event_t events[NET_CUTSCENE_AUTHORITY_EVENT_CAPACITY];
	size_t event_count;
} netmsg_cutscene_authority_t;

static netmsg_cutscene_authority_t s_CutsceneAuthority;

static u32 netmsgWriteCutscenePayload(struct netbuf *dst, u8 message_id,
		const u8 *payload, u32 payload_size)
{
	if (!dst || !payload || payload_size == 0) {
		return 1;
	}
	const u32 wp_before = dst->wp;
	const u32 error_before = dst->error;
	if (netbufWriteU8(dst, message_id) != 1
			|| netbufWriteData(dst, payload, payload_size) != payload_size
			|| dst->error) {
		dst->wp = wp_before;
		dst->error = error_before;
		return 1;
	}
	return 0;
}

static net_cutscene_authority_status_t netmsgCollectCutsceneParticipants(
		net_cutscene_participant_t participants[MAX_PLAYERS],
		size_t *out_count)
{
	if (!participants || !out_count) {
		return NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT;
	}
	if (!s_CutsceneAuthority.match_active
			|| s_CutsceneAuthority.participant_count == 0
			|| s_CutsceneAuthority.participant_count > MAX_PLAYERS) {
		*out_count = 0;
		return NET_CUTSCENE_AUTHORITY_INVALID_ROSTER;
	}
	*out_count = s_CutsceneAuthority.participant_count;
	memcpy(participants, s_CutsceneAuthority.participants,
		*out_count * sizeof(*participants));
	return NET_CUTSCENE_AUTHORITY_OK;
}

static s32 netmsgCutsceneAuthorityBeginMatch(u8 room_id,
		const net_cutscene_participant_t *participants,
		size_t participant_count)
{
	u32 client_mask = 0;
	u8 player_mask = 0;
	if (netCutsceneValidateRoster(participants, participant_count)
			!= NET_CUTSCENE_AUTHORITY_OK) {
		return 0;
	}
	for (size_t i = 0; i < participant_count; i++) {
		client_mask |= 1u << participants[i].client_id;
		player_mask |= (u8)(1u << participants[i].runtime_playernum);
	}
	memset(&s_CutsceneAuthority, 0, sizeof(s_CutsceneAuthority));
	s_CutsceneAuthority.match_active = true;
	s_CutsceneAuthority.room_id = room_id;
	s_CutsceneAuthority.participant_count = participant_count;
	s_CutsceneAuthority.full_client_mask = client_mask;
	s_CutsceneAuthority.full_player_mask = player_mask;
	memcpy(s_CutsceneAuthority.participants, participants,
		participant_count * sizeof(*participants));
	sysLogPrintf(LOG_NOTE,
		"NET: CUTSCENE.AUTHORITY match begin room=%u participants=%u client_mask=0x%08x runtime_player_mask=0x%02x",
		(unsigned)room_id, (unsigned)participant_count, client_mask,
		(unsigned)player_mask);
	return 1;
}

void netmsgCutsceneAuthorityReset(void)
{
	memset(&s_CutsceneAuthority, 0, sizeof(s_CutsceneAuthority));
	memset(&s_StageStartAuthorityCandidate, 0,
		sizeof(s_StageStartAuthorityCandidate));
}

bool netmsgCutsceneAuthorityIsActive(void)
{
	return s_CutsceneAuthority.match_active
		&& s_CutsceneAuthority.tracker.phase
			== NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE;
}

bool netmsgCutsceneAuthorityHasMatch(void)
{
	return s_CutsceneAuthority.match_active;
}

bool netmsgCutsceneAuthorityHasPendingEvents(void)
{
	return s_CutsceneAuthority.match_active
		&& s_CutsceneAuthority.event_count > 0;
}

static net_cutscene_authority_status_t netmsgCutscenePlayerForClient(
		u8 client_id, u8 *out_playernum)
{
	if (!out_playernum) {
		return NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT;
	}
	*out_playernum = 0xff;
	if (!s_CutsceneAuthority.match_active) {
		return NET_CUTSCENE_AUTHORITY_INVALID_ROSTER;
	}
	for (size_t i = 0; i < s_CutsceneAuthority.participant_count; i++) {
		if (s_CutsceneAuthority.participants[i].client_id == client_id) {
			*out_playernum =
				s_CutsceneAuthority.participants[i].runtime_playernum;
			return NET_CUTSCENE_AUTHORITY_OK;
		}
	}
	return NET_CUTSCENE_AUTHORITY_REQUESTER_NOT_FOUND;
}

static s32 netmsgQueueCutsceneEvent(const netmsg_cutscene_event_t *event)
{
	if (!event || s_CutsceneAuthority.event_count
			>= NET_CUTSCENE_AUTHORITY_EVENT_CAPACITY) {
		return 0;
	}
	s_CutsceneAuthority.events[s_CutsceneAuthority.event_count++] = *event;
	return 1;
}

s32 netmsgServerQueueCutsceneState(u8 active, u32 generation,
		u8 *out_player_mask)
{
	netmsg_cutscene_event_t event;
	net_cutscene_authority_tracker_t next;
	net_cutscene_authority_action_t action;
	if (out_player_mask) {
		*out_player_mask = 0;
	}
	if (g_NetMode != NETMODE_SERVER || !s_CutsceneAuthority.match_active
			|| generation == 0 || active > 1) {
		return 0;
	}
	memset(&event, 0, sizeof(event));
	event.message_id = SVC_CUTSCENE;
	event.state.active = active;
	event.state.client_mask = s_CutsceneAuthority.full_client_mask;
	event.state.generation = generation;
	const net_cutscene_authority_status_t status =
		netCutscenePlanStateTransition(&s_CutsceneAuthority.tracker,
			&event.state, &next, &action);
	if (status != NET_CUTSCENE_AUTHORITY_OK
			|| action == NET_CUTSCENE_AUTHORITY_IGNORE_STALE) {
		sysLogPrintf(LOG_ERROR,
			"NET: CUTSCENE.AUTHORITY state queue rejected status=%s action=%u active=%u generation=%u current=%u",
			netCutsceneAuthorityStatusString(status), (unsigned)action,
			(unsigned)active, generation,
			s_CutsceneAuthority.tracker.generation);
		return 0;
	}
	if (action == NET_CUTSCENE_AUTHORITY_APPLY
			&& !netmsgQueueCutsceneEvent(&event)) {
		sysLogPrintf(LOG_ERROR,
			"NET: CUTSCENE.AUTHORITY state queue full active=%u generation=%u",
			(unsigned)active, generation);
		return 0;
	}
	if (action == NET_CUTSCENE_AUTHORITY_APPLY) {
		s_CutsceneAuthority.tracker = next;
		s_CutsceneAuthority.accepted_client_mask = 0;
	}
	if (out_player_mask) {
		*out_player_mask = s_CutsceneAuthority.full_player_mask;
	}
	return 1;
}

static u32 netmsgWriteCutsceneState(struct netbuf *dst,
		const net_cutscene_state_wire_t *state)
{
	u8 wire[NET_CUTSCENE_STATE_WIRE_SIZE];
	if (!state || !netCutsceneStateEncode(state, wire)) {
		return 1;
	}
	return netmsgWriteCutscenePayload(dst, SVC_CUTSCENE, wire, sizeof(wire));
}

u32 netmsgSvcCutsceneRead(struct netbuf *src, struct netclient *srccl)
{
	u8 wire[NET_CUTSCENE_STATE_WIRE_SIZE];
	net_cutscene_state_wire_t state;
	net_cutscene_participant_t participants[MAX_PLAYERS];
	net_cutscene_authority_tracker_t next;
	net_cutscene_authority_action_t action;
	size_t participant_count = 0;
	u8 player_mask = 0;
	if (!src || netbufReadData(src, wire, sizeof(wire)) != sizeof(wire)
			|| src->error || !srccl) {
		return 1;
	}
	if (srccl->state < CLSTATE_GAME || !s_CutsceneAuthority.match_active) {
		sysLogPrintf(LOG_NOTE,
			"NET: SVC_CUTSCENE ignored outside active match state=%u",
			(unsigned)srccl->state);
		return 0;
	}
	net_cutscene_authority_status_t status = netCutsceneStateDecode(
		wire, sizeof(wire), &state);
	if (status == NET_CUTSCENE_AUTHORITY_OK
			&& state.client_mask != s_CutsceneAuthority.full_client_mask) {
		status = NET_CUTSCENE_AUTHORITY_STATE_CONFLICT;
	}
	if (status == NET_CUTSCENE_AUTHORITY_OK) {
		status = netCutscenePlanStateTransition(&s_CutsceneAuthority.tracker,
			&state, &next, &action);
	}
	if (status == NET_CUTSCENE_AUTHORITY_OK
			&& action == NET_CUTSCENE_AUTHORITY_APPLY) {
		status = netmsgCollectCutsceneParticipants(participants,
			&participant_count);
	}
	if (status == NET_CUTSCENE_AUTHORITY_OK
			&& action == NET_CUTSCENE_AUTHORITY_APPLY) {
		status = netCutscenePlayerMaskFromClientMask(state.client_mask,
			participants, participant_count, &player_mask);
	}
	if (status != NET_CUTSCENE_AUTHORITY_OK) {
		sysLogPrintf(LOG_WARNING,
			"NET: SVC_CUTSCENE rejected status=%s",
			netCutsceneAuthorityStatusString(status));
		return 1;
	}
	if (action != NET_CUTSCENE_AUTHORITY_APPLY) {
		sysLogPrintf(LOG_NOTE,
			"NET: SVC_CUTSCENE ignored action=%s active=%u generation=%u current=%u",
			action == NET_CUTSCENE_AUTHORITY_IGNORE_DUPLICATE
				? "duplicate" : "stale",
			(unsigned)state.active, state.generation,
			s_CutsceneAuthority.tracker.generation);
		return 0;
	}
	if (!playerApplyAuthoritativeCutsceneState(state.active, player_mask,
			state.generation)) {
		return 1;
	}
	s_CutsceneAuthority.tracker = next;
	sysLogPrintf(LOG_NOTE,
		"NET: SVC_CUTSCENE read active=%u client_mask=0x%08x runtime_player_mask=0x%02x generation=%u",
		state.active, state.client_mask, player_mask, state.generation);
	return 0;
}

static u32 netmsgSvcCutsceneSkipWrite(struct netbuf *dst,
		u8 requester_client_id,
		u32 generation)
{
	net_cutscene_skip_accept_t accept;
	u8 wire[NET_CUTSCENE_SKIP_ACCEPT_WIRE_SIZE];
	accept.requester_client_id = requester_client_id;
	accept.generation = generation;
	if (!netCutsceneSkipAcceptEncode(&accept, wire)) {
		return 1;
	}
	return netmsgWriteCutscenePayload(dst, SVC_CUTSCENE_SKIP, wire,
		sizeof(wire));
}

static u32 netmsgReconnectCutsceneAuthorityWrite(struct netbuf *dst)
{
	net_cutscene_state_wire_t state;
	const u32 wp_before = dst ? dst->wp : 0;
	const u32 error_before = dst ? dst->error : 0;
	u32 accepted_mask;

	if (!dst || !s_CutsceneAuthority.match_active
			|| s_CutsceneAuthority.full_client_mask == 0
			|| netCutsceneValidateRoster(s_CutsceneAuthority.participants,
				s_CutsceneAuthority.participant_count)
				!= NET_CUTSCENE_AUTHORITY_OK) {
		return 1;
	}

	/* Stage replay creates the same frozen roster with an idle tracker. An
	 * inactive authority therefore already matches the receiver. An active
	 * authority must replay its exact generation and every accepted skip before
	 * any later ordered END or acceptance can be interpreted correctly. */
	if (s_CutsceneAuthority.tracker.phase
			!= NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE) {
		if (s_CutsceneAuthority.accepted_client_mask != 0) {
			return 1;
		}
		sysLogPrintf(LOG_NOTE,
			"NET.RECONNECT.CUTSCENE active=0 phase=%u generation=%u accepted=0x%08x",
			(unsigned)s_CutsceneAuthority.tracker.phase,
			s_CutsceneAuthority.tracker.generation,
			s_CutsceneAuthority.accepted_client_mask);
		return 0;
	}

	accepted_mask = s_CutsceneAuthority.accepted_client_mask;
	if (s_CutsceneAuthority.tracker.generation == 0
			|| s_CutsceneAuthority.tracker.client_mask
				!= s_CutsceneAuthority.full_client_mask
			|| (accepted_mask & ~s_CutsceneAuthority.full_client_mask) != 0) {
		return 1;
	}

	memset(&state, 0, sizeof(state));
	state.active = 1;
	state.client_mask = s_CutsceneAuthority.full_client_mask;
	state.generation = s_CutsceneAuthority.tracker.generation;
	if (netmsgWriteCutsceneState(dst, &state) != 0) {
		goto rollback;
	}
	for (u8 client_id = 0; client_id < NET_MAX_CLIENTS; client_id++) {
		if ((accepted_mask & (1u << client_id)) != 0
				&& netmsgSvcCutsceneSkipWrite(dst, client_id,
					state.generation) != 0) {
			goto rollback;
		}
	}

	sysLogPrintf(LOG_NOTE,
		"NET.RECONNECT.CUTSCENE active=1 generation=%u client_mask=0x%08x accepted=0x%08x",
		state.generation, state.client_mask, accepted_mask);
	return 0;

rollback:
	dst->wp = wp_before;
	dst->error = error_before;
	return 1;
}

static s32 netmsgQueueCutsceneSkipAccept(
		const net_cutscene_skip_accept_t *accept)
{
	netmsg_cutscene_event_t event;
	u8 playernum;
	if (!accept || !s_CutsceneAuthority.match_active
			|| s_CutsceneAuthority.tracker.phase
				!= NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE
			|| accept->generation != s_CutsceneAuthority.tracker.generation
			|| netmsgCutscenePlayerForClient(accept->requester_client_id,
				&playernum) != NET_CUTSCENE_AUTHORITY_OK) {
		return 0;
	}
	const u32 bit = 1u << accept->requester_client_id;
	if (s_CutsceneAuthority.accepted_client_mask & bit) {
		return 1;
	}
	memset(&event, 0, sizeof(event));
	event.message_id = SVC_CUTSCENE_SKIP;
	event.accept = *accept;
	if (!netmsgQueueCutsceneEvent(&event)) {
		return 0;
	}
	s_CutsceneAuthority.accepted_client_mask |= bit;
	return 1;
}

u32 netmsgServerPrepareCutsceneAuthorityPacket(struct netbuf *dst,
		u8 *out_room_id, u32 *out_event_count)
{
	if (out_event_count) {
		*out_event_count = 0;
	}
	if (!s_CutsceneAuthority.event_count) {
		return 0;
	}
	if (!dst || !out_room_id || !out_event_count
			|| !s_CutsceneAuthority.match_active) {
		return 1;
	}
	const u32 wp_before = dst->wp;
	const u32 error_before = dst->error;
	for (size_t i = 0; i < s_CutsceneAuthority.event_count; i++) {
		const netmsg_cutscene_event_t *event =
			&s_CutsceneAuthority.events[i];
		u32 rc;
		if (event->message_id == SVC_CUTSCENE) {
			rc = netmsgWriteCutsceneState(dst, &event->state);
		} else if (event->message_id == SVC_CUTSCENE_SKIP) {
			rc = netmsgSvcCutsceneSkipWrite(dst,
				event->accept.requester_client_id,
				event->accept.generation);
		} else {
			rc = 1;
		}
		if (rc != 0) {
			dst->wp = wp_before;
			dst->error = error_before;
			return 1;
		}
	}
	*out_room_id = s_CutsceneAuthority.room_id;
	*out_event_count = (u32)s_CutsceneAuthority.event_count;
	return 0;
}

s32 netmsgServerCommitCutsceneAuthorityPacket(u32 event_count)
{
	u32 requester_mask = 0;
	if (event_count == 0 || event_count > s_CutsceneAuthority.event_count) {
		return 0;
	}
	for (u32 i = 0; i < event_count; i++) {
		const netmsg_cutscene_event_t *event =
			&s_CutsceneAuthority.events[i];
		if (event->message_id == SVC_CUTSCENE) {
			sysLogPrintf(LOG_NOTE,
				"NET: SVC_CUTSCENE published active=%u client_mask=0x%08x generation=%u room=%u",
				(unsigned)event->state.active, event->state.client_mask,
				event->state.generation,
				(unsigned)s_CutsceneAuthority.room_id);
		} else if (event->message_id == SVC_CUTSCENE_SKIP) {
			requester_mask |= 1u << event->accept.requester_client_id;
			sysLogPrintf(LOG_NOTE,
				"NET: SVC_CUTSCENE_SKIP published requester_mask=0x%08x",
				requester_mask);
		}
	}
	if (event_count < s_CutsceneAuthority.event_count) {
		memmove(s_CutsceneAuthority.events,
			&s_CutsceneAuthority.events[event_count],
			(s_CutsceneAuthority.event_count - event_count)
				* sizeof(s_CutsceneAuthority.events[0]));
	}
	s_CutsceneAuthority.event_count -= event_count;
	return 1;
}

void netmsgCutsceneAuthorityRetireClient(u8 client_id)
{
	if (!s_CutsceneAuthority.match_active || client_id >= NET_MAX_CLIENTS) {
		return;
	}
	u8 playernum = 0xff;
	(void)netmsgCutscenePlayerForClient(client_id, &playernum);
	size_t write_index = 0;
	for (size_t i = 0; i < s_CutsceneAuthority.event_count; i++) {
		const netmsg_cutscene_event_t *event =
			&s_CutsceneAuthority.events[i];
		if (event->message_id == SVC_CUTSCENE_SKIP
				&& event->accept.requester_client_id == client_id) {
			continue;
		}
		s_CutsceneAuthority.events[write_index++] = *event;
	}
	s_CutsceneAuthority.event_count = write_index;
	s_CutsceneAuthority.accepted_client_mask &= ~(1u << client_id);
	if (playernum < MAX_PLAYERS) {
		playerSetCutsceneSkipRequested(playernum, false);
	}
	sysLogPrintf(LOG_NOTE,
		"NET: CUTSCENE.AUTHORITY client retired client=%u player=%u pending_events=%u",
		(unsigned)client_id, (unsigned)playernum,
		(unsigned)s_CutsceneAuthority.event_count);
}

u32 netmsgSvcCutsceneSkipRead(struct netbuf *src, struct netclient *srccl)
{
	u8 wire[NET_CUTSCENE_SKIP_ACCEPT_WIRE_SIZE];
	net_cutscene_skip_accept_t accept;
	net_cutscene_participant_t participants[MAX_PLAYERS];
	size_t participant_count = 0;
	u8 runtime_playernum = 0xff;
	if (!src || netbufReadData(src, wire, sizeof(wire)) != sizeof(wire)
			|| src->error || !srccl) {
		return 1;
	}
	if (srccl->state < CLSTATE_GAME || !s_CutsceneAuthority.match_active) {
		sysLogPrintf(LOG_NOTE,
			"NET: SVC_CUTSCENE_SKIP ignored outside active match state=%u",
			(unsigned)srccl->state);
		return 0;
	}
	net_cutscene_authority_status_t status = netCutsceneSkipAcceptDecode(
		wire, sizeof(wire), &accept);
	if (status == NET_CUTSCENE_AUTHORITY_OK
			&& s_CutsceneAuthority.tracker.phase
				!= NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE) {
		status = NET_CUTSCENE_AUTHORITY_NO_ACTIVE_CUTSCENE;
	}
	if (status == NET_CUTSCENE_AUTHORITY_OK
			&& accept.generation != s_CutsceneAuthority.tracker.generation) {
		status = NET_CUTSCENE_AUTHORITY_GENERATION_MISMATCH;
	}
	if (status == NET_CUTSCENE_AUTHORITY_OK) {
		status = netmsgCollectCutsceneParticipants(participants,
			&participant_count);
	}
	if (status == NET_CUTSCENE_AUTHORITY_OK) {
		status = netCutscenePlanClientSkip(&accept, participants,
			participant_count, 1,
			s_CutsceneAuthority.tracker.generation, &runtime_playernum);
	}
	if (status == NET_CUTSCENE_AUTHORITY_NO_ACTIVE_CUTSCENE
			|| status == NET_CUTSCENE_AUTHORITY_GENERATION_MISMATCH) {
		sysLogPrintf(LOG_NOTE,
			"NET: SVC_CUTSCENE_SKIP ignored status=%s requester_client=%u generation=%u current=%u",
			netCutsceneAuthorityStatusString(status),
			accept.requester_client_id, accept.generation,
			s_CutsceneAuthority.tracker.generation);
		return 0;
	}
	if (status != NET_CUTSCENE_AUTHORITY_OK) {
		sysLogPrintf(LOG_WARNING,
			"NET: SVC_CUTSCENE_SKIP rejected status=%s",
			netCutsceneAuthorityStatusString(status));
		return 1;
	}
	playerSetCutsceneSkipRequested(runtime_playernum, true);
	sysLogPrintf(LOG_NOTE,
		"NET: SVC_CUTSCENE_SKIP applied requester_client=%u runtime_player=%u generation=%u",
		accept.requester_client_id, runtime_playernum, accept.generation);
	return 0;
}

u32 netmsgClcCutsceneSkipWrite(struct netbuf *dst, u8 playernum,
		u32 generation)
{
	net_cutscene_skip_request_t request;
	u8 wire[NET_CUTSCENE_SKIP_REQUEST_WIRE_SIZE];
	request.requested_playernum = playernum;
	request.generation = generation;
	if (!netCutsceneSkipRequestEncode(&request, wire)) {
		return 1;
	}
	return netmsgWriteCutscenePayload(dst, CLC_CUTSCENE_SKIP, wire,
		sizeof(wire));
}

s32 netmsgServerQueueLocalCutsceneSkip(u8 playernum, u32 generation)
{
	net_cutscene_skip_request_t request;
	net_cutscene_skip_accept_t accept;
	u8 authoritative_playernum = 0xff;
	u8 frozen_playernum = 0xff;
	if (g_NetMode != NETMODE_SERVER || !g_NetLocalClient
			|| netmsgCutscenePlayerForClient(g_NetLocalClient->id,
				&frozen_playernum) != NET_CUTSCENE_AUTHORITY_OK
			|| frozen_playernum != playernum) {
		return 0;
	}
	request.requested_playernum = playernum;
	request.generation = generation;
	const net_cutscene_authority_status_t status = netCutscenePlanServerSkip(
		&request, g_NetLocalClient->id, frozen_playernum,
		g_NetLocalClient->state == CLSTATE_GAME ? 1 : 0,
		netmsgCutsceneAuthorityIsActive() ? 1 : 0,
		s_CutsceneAuthority.tracker.generation,
		&accept, &authoritative_playernum);
	if (status != NET_CUTSCENE_AUTHORITY_OK
			|| !netmsgQueueCutsceneSkipAccept(&accept)) {
		sysLogPrintf(LOG_WARNING,
			"NET: CUTSCENE_SKIP.AUTHORITY local rejected status=%s player=%u generation=%u",
			netCutsceneAuthorityStatusString(status), playernum, generation);
		return 0;
	}
	sysLogPrintf(LOG_NOTE,
		"NET: CUTSCENE_SKIP.AUTHORITY accepted source=local client=%u player=%u generation=%u",
		accept.requester_client_id, authoritative_playernum,
		accept.generation);
	return 1;
}

u32 netmsgClcCutsceneSkipRead(struct netbuf *src, struct netclient *srccl)
{
	u8 wire[NET_CUTSCENE_SKIP_REQUEST_WIRE_SIZE];
	net_cutscene_skip_request_t request;
	net_cutscene_skip_accept_t accept;
	u8 authoritative_playernum = 0xff;
	if (!src || netbufReadData(src, wire, sizeof(wire)) != sizeof(wire)
			|| src->error || !srccl) {
		return 1;
	}
	net_cutscene_authority_status_t status = netCutsceneSkipRequestDecode(
		wire, sizeof(wire), &request);
	if (status == NET_CUTSCENE_AUTHORITY_OK) {
		status = netmsgCutscenePlayerForClient(srccl->id,
			&authoritative_playernum);
	}
	if (status == NET_CUTSCENE_AUTHORITY_OK) {
		status = netCutscenePlanServerSkip(&request, srccl->id,
			authoritative_playernum,
			srccl->state == CLSTATE_GAME
				&& !(srccl->flags & CLFLAG_SPECTATOR)
				&& (s_CutsceneAuthority.room_id == 0xff
					|| srccl->room_id == s_CutsceneAuthority.room_id) ? 1 : 0,
			netmsgCutsceneAuthorityIsActive() ? 1 : 0,
			s_CutsceneAuthority.tracker.generation,
			&accept, &authoritative_playernum);
	}
	if (status == NET_CUTSCENE_AUTHORITY_NO_ACTIVE_CUTSCENE
			|| status == NET_CUTSCENE_AUTHORITY_GENERATION_MISMATCH
			|| status == NET_CUTSCENE_AUTHORITY_SOURCE_NOT_IN_GAME) {
		sysLogPrintf(LOG_NOTE,
			"NET: CLC_CUTSCENE_SKIP ignored status=%s client=%u requested_player=%u requested_generation=%u current_generation=%u",
			netCutsceneAuthorityStatusString(status), srccl->id,
			request.requested_playernum, request.generation,
			s_CutsceneAuthority.tracker.generation);
		return 0;
	}
	if (status != NET_CUTSCENE_AUTHORITY_OK
			|| !netmsgQueueCutsceneSkipAccept(&accept)) {
		sysLogPrintf(LOG_WARNING,
			"NET: CLC_CUTSCENE_SKIP rejected status=%s client=%u",
			netCutsceneAuthorityStatusString(status), srccl->id);
		return 1;
	}

	playerSetCutsceneSkipRequested(authoritative_playernum, true);
	sysLogPrintf(LOG_NOTE,
		"NET: CUTSCENE_SKIP.AUTHORITY accepted source=remote client=%u player=%u requested_player=%u generation=%u",
		accept.requester_client_id, authoritative_playernum,
		request.requested_playernum, accept.generation);
	return 0;
}

/* ========================================================================
 * CLC_LOBBY_RESYNC - Client asks server to re-broadcast room state (v49)
 *
 * Fired by a client after returning from a match (post-SVC_STAGE_END) so the
 * client's local room view is brought back in sync with the server's
 * authoritative state. The server responds with SVC_ROOM_ASSIGN carrying the
 * client's current room_id (0xFF for lounge), then replays the current
 * SVC_ROOM_SETTINGS and SVC_ROOM_PLAYLIST when the client is still in a room.
 * No payload either direction; the source netclient identifies whose room to
 * resync.
 * ========================================================================
 */

u32 netmsgClcLobbyResyncWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, CLC_LOBBY_RESYNC);
	return dst->error;
}

static u8 netmsgCountCurrentRoomBots(void)
{
	u8 numBots = 0;

	for (s32 i = 1; i < g_MatchConfig.numSlots && i < MATCH_MAX_SLOTS; i++) {
		if (g_MatchConfig.slots[i].type == SLOT_BOT && numBots < MAX_BOTS) {
			numBots++;
		}
	}

	return numBots;
}

static void netmsgBuildCurrentPlaylistString(char *pl, size_t plsize)
{
	s32 pos = 0;

	if (!pl || plsize == 0) {
		return;
	}

	pl[0] = '\0';

	s32 n = audioGetModPlaylistCount();
	if (n > AUDIO_MAX_PLAYLIST) {
		n = AUDIO_MAX_PLAYLIST;
	}

	for (s32 i = 0; i < n; i++) {
		const char *id = audioGetModPlaylistEntry(i);
		if (!id || !id[0]) {
			continue;
		}
		if (pos > 0 && pos < (s32)plsize - 1) {
			pl[pos++] = ';';
		}
		s32 left = (s32)plsize - pos - 1;
		if (left <= 0) {
			break;
		}
		s32 len = (s32)strlen(id);
		if (len > left) {
			len = left;
		}
		memcpy(pl + pos, id, (size_t)len);
		pos += len;
	}

	pl[pos] = '\0';
}

static void netmsgSendLobbyResyncRoomState(struct netclient *dstcl, u8 room_id)
{
	if (!dstcl || !dstcl->peer) {
		sysLogPrintf(LOG_NOTE,
			"NET: CLC_LOBBY_RESYNC room replay skipped for local/unbound client");
		return;
	}

	struct netbuf assignBuf;
	u8 assignData[8];

	assignBuf.data = assignData;
	assignBuf.size = sizeof(assignData);
	netbufStartWrite(&assignBuf);
	netmsgSvcRoomAssignWrite(&assignBuf, room_id);
	if (assignBuf.error) {
		sysLogPrintf(LOG_WARNING,
			"NET: CLC_LOBBY_RESYNC assignment encode failed for client=%u",
			dstcl ? dstcl->id : NET_NULL_CLIENT);
		return;
	}
	netSend(dstcl, &assignBuf, true, NETCHAN_DEFAULT);

	if (room_id == 0xFF) {
		return;
	}

	const u8 numBots = netmsgCountCurrentRoomBots();
	const u8 wpnIdx = (g_MatchConfig.weaponSetIndex >= 0)
		? (u8)g_MatchConfig.weaponSetIndex : 0xFF;

	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcRoomSettingsWrite(&g_NetMsgRel, numBots,
		g_MatchConfig.timelimit, g_MatchConfig.scorelimit,
		g_MatchConfig.teamscorelimit, matchConfigGetUserOptions(),
		g_MatchConfig.scenario, wpnIdx, g_MatchConfig.stage_id);
	if (g_NetMsgRel.error) {
		sysLogPrintf(LOG_WARNING,
			"NET: CLC_LOBBY_RESYNC settings encode failed for client=%u",
			dstcl ? dstcl->id : NET_NULL_CLIENT);
		netbufStartWrite(&g_NetMsgRel);
		return;
	}
	netSend(dstcl, &g_NetMsgRel, true, NETCHAN_CONTROL);

	char pl[AUDIO_MAX_PLAYLIST * 65];
	netmsgBuildCurrentPlaylistString(pl, sizeof(pl));

	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcRoomPlaylistWrite(&g_NetMsgRel, pl);
	if (g_NetMsgRel.error) {
		sysLogPrintf(LOG_WARNING,
			"NET: CLC_LOBBY_RESYNC playlist encode failed for client=%u",
			dstcl ? dstcl->id : NET_NULL_CLIENT);
		netbufStartWrite(&g_NetMsgRel);
		return;
	}
	netSend(dstcl, &g_NetMsgRel, true, NETCHAN_CONTROL);
}

u32 netmsgClcLobbyResyncRead(struct netbuf *src, struct netclient *srccl)
{
	if (src->error || !srccl) {
		return src->error ? src->error : 1;
	}

	/* Only meaningful from a fully-authed client. CLSTATE_LOBBY is the
	 * canonical post-match state; tolerate CLSTATE_GAME too in case the
	 * resync is fired before the state machine settles. */
	if (srccl->state < CLSTATE_LOBBY) {
		sysLogPrintf(LOG_NOTE,
			"NET: CLC_LOBBY_RESYNC ignored client=%u state=%u",
			srccl->id, (unsigned)srccl->state);
		return src->error;
	}

	const u8 room_id = srccl->room_id;
	sysLogPrintf(LOG_NOTE,
		"NET: CLC_LOBBY_RESYNC from client=%u, replaying room state room=%u",
		srccl->id, (unsigned)room_id);

	netmsgSendLobbyResyncRoomState(srccl, room_id);

	/* Also re-mark the room list dirty so the lobby UI gets fresh data.
	 * netRoomListFlush coalesces the broadcast at end-of-frame. */
	netRoomListMarkDirty();

	return src->error;
}

/* Convenience client-side helper. Caller is responsible for ensuring the
 * net channel is up (NETMODE_CLIENT / NETMODE_DEDICATED with an open peer). */
void netSendLobbyResync(void)
{
	if (g_NetMode == NETMODE_NONE || g_NetLocalClient == NULL) {
		return;
	}

	if (g_NetMode == NETMODE_SERVER && !g_NetDedicated) {
		netRoomListMarkDirty();
		sysLogPrintf(LOG_NOTE,
			"NET: CLC_LOBBY_RESYNC satisfied locally for listen host");
		return;
	}

	netbufStartWrite(&g_NetMsgRel);
	netmsgClcLobbyResyncWrite(&g_NetMsgRel);
	if (g_NetMsgRel.error) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_LOBBY_RESYNC encode failed");
		netbufStartWrite(&g_NetMsgRel);
		return;
	}
	netSend(NULL, &g_NetMsgRel, true, NETCHAN_DEFAULT);
	sysLogPrintf(LOG_NOTE, "NET: CLC_LOBBY_RESYNC sent to server");
}

/* ========================================================================
 * CLC_LOBBY_START - Lobby leader requests match start
 *
 * Sent by the lobby leader client to the dedicated server when they've
 * chosen a game mode and are ready to start. The server validates that
 * the sender is actually the lobby leader, then starts the match.
 *
 * Payload (v36+): gamemode (u8), stage_id (str catalog ID),
 *          difficulty (u8), antiClientId (u8, NET_NULL_CLIENT when unused),
 *          numSims (u8), simType (u8),
 *          timelimit (u8), options (u32), scenario_id (str catalog ID), scorelimit (u8), teamscorelimit (u16),
 *          weaponSetIndex (u8, 0xFF = custom/default),
 *          weapons (str[NUM_MPWEAPONSLOTS]) — per-slot ASSET_WEAPON catalog ID string
 *          Per-bot (repeated numSims times): name (str), body_id (str),
 *          head_id (str), profile_id (str), botDifficulty (u8), botType (u8)
 *
 * v27: all asset references are catalog ID strings — no u32 net_hash on wire.
 * stage_id written from g_MatchConfig.stage_id (Single Source of Truth).
 * Server resolves stage_id → stagenum and weapon IDs → weapon_id via assetCatalogResolve().
 * ======================================================================== */

typedef struct net_lobby_start_write_bot_t {
	char name[sizeof(((struct mpbotconfig *)0)->base.name)];
	char body_id[CATALOG_ID_LEN];
	char head_id[CATALOG_ID_LEN];
	char profile_id[CATALOG_ID_LEN];
	u8 difficulty;
	u8 type;
	u8 team;
} net_lobby_start_write_bot_t;

typedef struct net_lobby_start_write_plan_t {
	u8 mode;
	char stage_id[CATALOG_ID_LEN];
	u8 difficulty;
	u8 anti_client_id;
	u8 num_sims;
	u8 sim_type;
	u8 timelimit;
	u32 options;
	char scenario_id[CATALOG_ID_LEN];
	u8 scenario;
	u8 scorelimit;
	u16 teamscorelimit;
	u8 weapon_set_index;
	char weapon_ids[NUM_MPWEAPONSLOTS][CATALOG_ID_LEN];
	u8 weapons[NUM_MPWEAPONSLOTS];
	char spawn_weapon_id[CATALOG_ID_LEN];
	u8 spawn_weapon_mode;
	net_lobby_start_write_bot_t bots[MAX_BOTS];
	match_manifest_t manifest;
} net_lobby_start_write_plan_t;

static bool netLobbyManifestEntryMatchesCatalog(
		const match_manifest_entry_t *entry, const asset_entry_t *asset)
{
	if (!entry || !asset || !asset->occupied || !asset->enabled
			|| strcmp(entry->id, asset->id) != 0) {
		return false;
	}
	return netManifestTypeAcceptsCatalogAsset(
		entry->type, entry->slot_index, asset->type);
}

static bool netLobbyManifestValidate(const match_manifest_t *manifest,
		const char *stage_id)
{
	s32 stage_count = 0;
	static const u8 zero_sha[32] = { 0 };

	if (!manifest || !stage_id || !stage_id[0]
			|| manifest->num_entries == 0
			|| manifest->num_entries > SESSION_CATALOG_MAX_ENTRIES
			|| !manifest->entries) {
		return false;
	}
	for (s32 i = 0; i < (s32)manifest->num_entries; i++) {
		const match_manifest_entry_t *entry = &manifest->entries[i];
		if (!entry->id[0] || memchr(entry->id, '\0', sizeof(entry->id)) == NULL
				|| entry->type > MANIFEST_TYPE_ASSET) {
			return false;
		}
		for (s32 j = 0; j < i; j++) {
			if (strcmp(manifest->entries[j].id, entry->id) == 0) {
				return false;
			}
		}
		if (entry->type == MANIFEST_TYPE_COMPONENT) {
			if (memcmp(entry->sha256, zero_sha, sizeof(zero_sha)) == 0) {
				return false;
			}
		} else {
			const asset_entry_t *asset = assetCatalogResolve(entry->id);
			if (!netLobbyManifestEntryMatchesCatalog(entry, asset)) {
				return false;
			}
		}
		if (entry->type == MANIFEST_TYPE_STAGE) {
			stage_count++;
			if (strcmp(entry->id, stage_id) != 0) {
				return false;
			}
		}
	}
	return stage_count == 1;
}

static bool netLobbyStartWriteReject(const char *detail)
{
	sysLogPrintf(LOG_ERROR,
		"PLAYER.INIT.ROLLBACK lobby-write detail=%s bytes_published=0",
		detail ? detail : "unspecified");
	return false;
}

static bool netLobbyStartWritePrepare(net_lobby_start_write_plan_t *plan,
		u8 gamemode, const char *stage_id, u8 difficulty, u8 anti_client_id,
		u8 num_sims, u8 sim_type, u8 timelimit, u32 options, u8 scenario,
		u8 scorelimit, u16 teamscorelimit, u8 weapon_set_index)
{
	catalog_stage_result_t stage;
	player_identity_plan_t host_identity;
	s32 bot_count = 0;

	if (!plan) {
		return netLobbyStartWriteReject("missing plan");
	}
	memset(plan, 0, sizeof(*plan));
	plan->mode = gamemode;
	plan->difficulty = difficulty;
	plan->anti_client_id = anti_client_id;
	plan->num_sims = num_sims;
	plan->sim_type = sim_type;
	plan->timelimit = timelimit;
	plan->options = options;
	plan->scenario = scenario;
	plan->scorelimit = scorelimit;
	plan->teamscorelimit = teamscorelimit;
	plan->weapon_set_index = weapon_set_index;

	if (gamemode != NETGAMEMODE_MP && gamemode != NETGAMEMODE_COOP
			&& gamemode != NETGAMEMODE_ANTI) {
		return netLobbyStartWriteReject("invalid game mode");
	}
	if (!netStageStartWriteCopyId(plan->stage_id, stage_id)
			|| !catalogResolveStageForType(plan->stage_id,
				gamemode == NETGAMEMODE_MP ? ASSET_ARENA : ASSET_MAP,
				&stage)
			|| !stage.entry || !stage.entry->occupied || !stage.entry->enabled
			|| strcmp(stage.entry->id, plan->stage_id) != 0
			|| stage.stagenum <= 0 || stage.stagenum > 255
			|| (gamemode == NETGAMEMODE_MP
				? stage.entry->type != ASSET_ARENA
				: stage.entry->type != ASSET_MAP)) {
		return netLobbyStartWriteReject("stage ID has no exact mode binding");
	}
	if (gamemode == NETGAMEMODE_MP) {
		const asset_entry_t *scenario_entry;
		if (difficulty != 0 || anti_client_id != NET_NULL_CLIENT
				|| options != matchConfigGetUserOptions()
				|| !netStageStartWriteCopyId(plan->scenario_id,
					g_MatchConfig.scenario_id)) {
			return netLobbyStartWriteReject(
				"Combat Simulator mode metadata is inconsistent");
		}
		scenario_entry = assetCatalogResolve(plan->scenario_id);
		if (!scenario_entry || !scenario_entry->occupied
				|| !scenario_entry->enabled
				|| scenario_entry->type != ASSET_GAMEMODE
				|| scenario_entry->ext.gamemode.mode_id != scenario
				|| scenario < MPSCENARIO_COMBAT
				|| scenario > MPSCENARIO_CAPTURETHECASE) {
			return netLobbyStartWriteReject(
				"scenario ID and runtime mode disagree");
		}
	} else {
		if (difficulty > DIFF_PD || num_sims != 0
				|| (gamemode == NETGAMEMODE_COOP
					&& anti_client_id != NET_NULL_CLIENT)
				|| (gamemode == NETGAMEMODE_ANTI
					&& (anti_client_id == NET_NULL_CLIENT
						|| anti_client_id == 0
						|| anti_client_id >= NET_MAX_CLIENTS))) {
			return netLobbyStartWriteReject(
				"co-op/Counter-Op metadata is inconsistent");
		}
		/* Mission starts have one canonical wire representation.  Do not carry
		 * ignored Combat Simulator values that a server would have to guess at. */
		plan->sim_type = 0;
		plan->timelimit = 0;
		plan->options = 0;
		plan->scorelimit = 0;
		plan->teamscorelimit = 0;
		plan->weapon_set_index = 0xFF;
		plan->scenario_id[0] = '\0';
		plan->spawn_weapon_id[0] = '\0';
		plan->spawn_weapon_mode = SPAWNWEAPON_MODE_RANDOM;
	}

	if (gamemode == NETGAMEMODE_MP) {
		if (g_MatchConfig.numSlots == 0
				|| g_MatchConfig.numSlots > MATCH_MAX_SLOTS) {
			return netLobbyStartWriteReject("match slot count is invalid");
		}
		for (s32 wi = 0; wi < NUM_MPWEAPONSLOTS; wi++) {
			const char *weapon_id = g_MatchConfig.weapon_ids[wi];
			const asset_entry_t *weapon;

			if (!weapon_id[0]) {
				plan->weapons[wi] = MPWEAPON_NONE;
				continue;
			}
			if (!netStageStartWriteCopyId(plan->weapon_ids[wi], weapon_id)) {
				return netLobbyStartWriteReject(
					"non-empty weapon slot has no typed ID");
			}
			weapon = assetCatalogResolve(plan->weapon_ids[wi]);
			if (!weapon || !weapon->occupied || !weapon->enabled
					|| weapon->type != ASSET_WEAPON
					|| weapon->mp_index < 0 || weapon->mp_index > 255
					|| weapon->runtime_index <= 0
					|| catalogGetMpWeaponNum(weapon->mp_index)
						!= weapon->runtime_index) {
				return netLobbyStartWriteReject(
					"weapon ID and runtime binding disagree");
			}
			plan->weapons[wi] = (u8)weapon->mp_index;
		}
		{
			u8 preset_weapons[NUM_MPWEAPONSLOTS];
			s32 resolved_set;
			const u64 seed_0 = g_RngSeed;
			const u64 seed_1 = g_Rng2Seed;
			const s32 requested_set = weapon_set_index == 0xFF
				? -1 : (s32)weapon_set_index;
			const s32 prepared = mpPrepareWeaponSet(requested_set,
				plan->weapons, preset_weapons, &resolved_set);
			g_RngSeed = seed_0;
			g_Rng2Seed = seed_1;
			if (prepared != 0) {
				return netLobbyStartWriteReject(
					"weapon set metadata cannot prepare");
			}
			/* Random-set intent and exact slots are both authoritative. Auto-random
			 * start resolves one candidate now, using restored RNG state so failed
			 * writes remain observationally pure. The server accepts these exact
			 * slots without a second roll and retains the random-set intent for the
			 * next configured reroll boundary. */
			if ((resolved_set == WEAPONSET_RANDOM
					|| resolved_set == WEAPONSET_RANDOMFIVE)
					&& (plan->options & MPOPTION_AUTORANDOMWEAPON_START)) {
				memcpy(plan->weapons, preset_weapons,
					sizeof(plan->weapons));
				for (s32 wi = 0; wi < NUM_MPWEAPONSLOTS; wi++) {
					const char *weapon_id;
					const asset_entry_t *weapon;
					plan->weapon_ids[wi][0] = '\0';
					if (plan->weapons[wi] == MPWEAPON_NONE) continue;
					weapon_id = catalogWeaponIdByMpWeaponId(
						(s32)plan->weapons[wi]);
					weapon = weapon_id ? assetCatalogResolve(weapon_id) : NULL;
					if (!weapon || !weapon->occupied || !weapon->enabled
							|| weapon->type != ASSET_WEAPON
							|| weapon->mp_index != (s32)plan->weapons[wi]
							|| weapon->runtime_index <= 0
							|| catalogGetMpWeaponNum(weapon->mp_index)
								!= weapon->runtime_index
							|| !netStageStartWriteCopyId(
								plan->weapon_ids[wi], weapon->id)) {
						return netLobbyStartWriteReject(
							"auto-random weapon has no exact typed binding");
					}
				}
			}
			if (resolved_set != WEAPONSET_RANDOM
					&& resolved_set != WEAPONSET_RANDOMFIVE
					&& memcmp(preset_weapons, plan->weapons,
						sizeof(plan->weapons)) != 0) {
				plan->weapon_set_index = 0xFF;
			}
		}

		plan->spawn_weapon_mode = g_MatchConfig.spawnWeaponMode;
		if (plan->spawn_weapon_mode == SPAWNWEAPON_MODE_SPECIFIC) {
			const asset_entry_t *spawn;
			if (!netStageStartWriteCopyId(plan->spawn_weapon_id,
					g_MatchConfig.spawn_weapon_id)) {
				return netLobbyStartWriteReject(
					"specific spawn weapon has no typed ID");
			}
			spawn = assetCatalogResolve(plan->spawn_weapon_id);
			if (!spawn || !spawn->occupied || !spawn->enabled
					|| spawn->type != ASSET_WEAPON
					|| spawn->runtime_index <= 0
					|| spawn->runtime_index >= WEAPON_CUSTOM_END) {
				return netLobbyStartWriteReject(
					"specific spawn weapon is unavailable");
			}
		} else if (plan->spawn_weapon_mode == SPAWNWEAPON_MODE_RANDOM
				|| plan->spawn_weapon_mode == SPAWNWEAPON_MODE_FIESTA) {
			if (g_MatchConfig.spawn_weapon_id[0]) {
				return netLobbyStartWriteReject(
					"random/Fiesta spawn mode retains a typed ID");
			}
		} else {
			return netLobbyStartWriteReject("spawn weapon mode is invalid");
		}

		for (s32 si = 0; si < g_MatchConfig.numSlots; si++) {
			const struct matchslot *slot = &g_MatchConfig.slots[si];
			if (slot->type != SLOT_BOT) {
				continue;
			}
			if (bot_count >= MAX_BOTS) {
				return netLobbyStartWriteReject("bot count exceeds capacity");
			}
			{
				net_lobby_start_write_bot_t *bot = &plan->bots[bot_count];
				player_identity_plan_t identity;
				const asset_entry_t *profile_entry;
				const asset_runtime_binding_t *profile;
				const size_t name_len = strnlen(slot->name, sizeof(slot->name));
				if (name_len == 0 || name_len >= sizeof(slot->name)
						|| name_len >= sizeof(bot->name)
						|| playerIdentityPrepare(slot->body_id, slot->head_id,
							&identity) != PLAYER_IDENTITY_OK) {
					return netLobbyStartWriteReject(
						"bot name or typed identity is invalid");
				}
				profile_entry = assetCatalogResolve(slot->profile_id);
				profile = profile_entry
					&& profile_entry->type == ASSET_BOT_PROFILE
					? mpBotProfileRuntimeBindingById(profile_entry->id) : NULL;
				if (!profile_entry || !profile_entry->occupied
						|| !profile_entry->enabled || !profile
						|| !profile->active || !profile->source_hydrated
						|| profile->type != ASSET_BOT_PROFILE
						|| strcmp(profile->id, profile_entry->id) != 0
						|| !assetRuntimePrimaryFileAccessible(profile)
						|| profile->bot_profile_type < 0
						|| profile->bot_profile_type > 255
						|| profile->bot_profile_difficulty < 0
						|| profile->bot_profile_difficulty > 255
						|| slot->team >= MAX_TEAMS
						|| slot->botType != (u8)profile->bot_profile_type
						|| slot->botDifficulty
							!= (u8)profile->bot_profile_difficulty) {
					return netLobbyStartWriteReject(
						"bot profile has no exact hydrated binding");
				}
				memcpy(bot->name, slot->name, name_len + 1);
				strncpy(bot->body_id, identity.body_id,
					sizeof(bot->body_id) - 1);
				strncpy(bot->head_id, identity.head_id,
					sizeof(bot->head_id) - 1);
				strncpy(bot->profile_id, profile_entry->id,
					sizeof(bot->profile_id) - 1);
				bot->difficulty = (u8)profile->bot_profile_difficulty;
				bot->type = (u8)profile->bot_profile_type;
				bot->team = slot->team;
			}
			bot_count++;
		}
		if (bot_count != (s32)num_sims) {
			return netLobbyStartWriteReject(
				"bot payload count disagrees with the prepared roster");
		}
		/* A zero-bot match has no lead bot difficulty.  Serialize one canonical
		 * sentinel instead of preserving a UI default that the reader must guess
		 * how to interpret. */
		if (bot_count == 0) {
			plan->sim_type = 0;
		} else if (sim_type != plan->bots[0].difficulty) {
			return netLobbyStartWriteReject(
				"lead bot difficulty disagrees with the prepared roster");
		}
	}

	if (!g_NetLocalClient
			|| playerIdentityPrepare(g_NetLocalClient->settings.body_id,
				g_NetLocalClient->settings.head_id,
				&host_identity) != PLAYER_IDENTITY_OK) {
		return netLobbyStartWriteReject(
			"host has no exact typed player identity");
	}

	manifestBuildForHost(&plan->manifest);
	if (!manifestSetStageEntry(&plan->manifest, plan->stage_id)
			|| !manifestAppendCatalogClosure(&plan->manifest,
				plan->stage_id, MANIFEST_SLOT_MATCH)
			|| !manifestAppendCatalogClosure(&plan->manifest,
				host_identity.body_id, 0)
			|| !manifestAppendCatalogClosure(&plan->manifest,
				host_identity.head_id, 0)) {
		return netLobbyStartWriteReject(
			"host manifest cannot publish stage or player closure");
	}
	if (gamemode == NETGAMEMODE_MP) {
		if (!manifestAppendCatalogClosure(&plan->manifest,
				plan->scenario_id, MANIFEST_SLOT_MATCH)) {
			return netLobbyStartWriteReject(
				"host manifest cannot publish scenario closure");
		}
		for (s32 wi = 0; wi < NUM_MPWEAPONSLOTS; wi++) {
			if (plan->weapon_ids[wi][0]
					&& !manifestAppendCatalogClosure(&plan->manifest,
						plan->weapon_ids[wi], MANIFEST_SLOT_MATCH)) {
				return netLobbyStartWriteReject(
					"host manifest cannot publish weapon closure");
			}
		}
		if (plan->spawn_weapon_id[0]
				&& !manifestAppendCatalogClosure(&plan->manifest,
					plan->spawn_weapon_id, MANIFEST_SLOT_MATCH)) {
			return netLobbyStartWriteReject(
				"host manifest cannot publish spawn weapon closure");
		}
		for (s32 bi = 0; bi < bot_count; bi++) {
			const net_lobby_start_write_bot_t *bot = &plan->bots[bi];
			const u8 slot = (u8)(MAX_PLAYERS + bi);
			if (!manifestAppendCatalogClosure(&plan->manifest, bot->body_id, slot)
					|| !manifestAppendCatalogClosure(&plan->manifest,
						bot->head_id, slot)
					|| !manifestAppendCatalogClosure(&plan->manifest,
						bot->profile_id, slot)) {
				return netLobbyStartWriteReject(
					"host manifest cannot publish bot closure");
			}
		}
	}
	manifestComputeHash(&plan->manifest);
	if (!netLobbyManifestValidate(&plan->manifest, plan->stage_id)) {
		return netLobbyStartWriteReject("host manifest failed exact validation");
	}

	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.PREFLIGHT lobby-write mode=%u stage='%s' bots=%u manifest=%u",
		(unsigned)plan->mode, plan->stage_id, (unsigned)plan->num_sims,
		(unsigned)plan->manifest.num_entries);
	return true;
}

u32 netmsgClcLobbyStartWrite(struct netbuf *dst, u8 gamemode,
		const char *stage_id, u8 difficulty, u8 antiClientId, u8 numSims,
		u8 simType, u8 timelimit, u32 options, u8 scenario, u8 scorelimit,
		u16 teamscorelimit, u8 weaponSetIndex)
{
	net_lobby_start_write_plan_t plan;
	u32 result = 1;

	memset(&plan, 0, sizeof(plan));
	if (!dst || !netLobbyStartWritePrepare(&plan, gamemode, stage_id,
			difficulty, antiClientId, numSims, simType, timelimit, options,
			scenario, scorelimit, teamscorelimit, weaponSetIndex)) {
		manifestFree(&plan.manifest);
		return 1;
	}

	netbufWriteU8(dst, CLC_LOBBY_START);
	netbufWriteU8(dst, plan.mode);
	netbufWriteStr(dst, plan.stage_id);
	netbufWriteU8(dst, plan.difficulty);
	netbufWriteU8(dst, plan.anti_client_id);
	netbufWriteU8(dst, plan.num_sims);
	netbufWriteU8(dst, plan.sim_type);
	netbufWriteU8(dst, plan.timelimit);
	netbufWriteU32(dst, plan.options);
	netbufWriteStr(dst, plan.scenario_id);
	netbufWriteU8(dst, plan.scorelimit);
	netbufWriteU16(dst, plan.teamscorelimit);
	netbufWriteU8(dst, plan.weapon_set_index);
	for (s32 wi = 0; wi < NUM_MPWEAPONSLOTS; wi++) {
		netbufWriteStr(dst, plan.weapon_ids[wi]);
	}
	netbufWriteStr(dst, plan.spawn_weapon_id);
	netbufWriteU8(dst, plan.spawn_weapon_mode);
	for (s32 bi = 0; bi < (s32)plan.num_sims; bi++) {
		const net_lobby_start_write_bot_t *bot = &plan.bots[bi];
		netbufWriteStr(dst, bot->name);
		netbufWriteStr(dst, bot->body_id);
		netbufWriteStr(dst, bot->head_id);
		netbufWriteStr(dst, bot->profile_id);
		netbufWriteU8(dst, bot->difficulty);
		netbufWriteU8(dst, bot->type);
		netbufWriteU8(dst, bot->team);
	}
	manifestSerialize(dst, &plan.manifest);
	result = dst->error;
	manifestFree(&plan.manifest);
	return result;
}

/* ---- Phase E: Ready gate helpers (server-side) ---- */

static u8 readyGatePopcount(u32 mask)
{
	u8 n = 0;
	while (mask) { n++; mask &= mask - 1; }
	return n;
}

static void readyGateBroadcastCountdown(u8 phase)
{
	u8 ready_count = readyGatePopcount(s_ReadyGate.ready_mask);
	u8 countdown_secs;
	if (phase == MANIFEST_PHASE_LOADING) {
		/* Phase F: use the dedicated countdown counter, not deadline_ticks */
		countdown_secs = s_ReadyGate.countdown_secs;
	} else {
		u32 remaining = (g_NetTick < s_ReadyGate.deadline_ticks)
		                ? (s_ReadyGate.deadline_ticks - g_NetTick) : 0;
		countdown_secs = (u8)(remaining / 60);
	}

	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcMatchCountdownWrite(&g_NetMsgRel, ready_count,
	                             s_ReadyGate.total_count, phase, countdown_secs);
	netSendToRoom(s_ReadyGate.room_id, &g_NetMsgRel, true, NETCHAN_CONTROL);
	sysLogPrintf(LOG_NOTE, "NET: SVC_MATCH_COUNTDOWN %u/%u phase=%u secs=%u",
	             (unsigned)ready_count, (unsigned)s_ReadyGate.total_count,
	             (unsigned)phase, (unsigned)countdown_secs);
}

static s32 readyGateCompactDeclinedRoster(void)
{
	u8 kept = 0;
	u8 declined = 0;

	if (s_ReadyGate.roster_compacted) return 1;
	if (s_ReadyGate.declined_mask == 0) {
		s_ReadyGate.roster_compacted = 1;
		return 1;
	}
	if (s_ReadyGate.game_mode != NETGAMEMODE_MP) return 0;
	for (u8 i = 0; i < s_ReadyGate.roster_count; i++) {
		const u8 client_id = s_ReadyGate.roster_client_ids[i];
		if (client_id >= NET_MAX_CLIENTS) return 0;
		if (s_ReadyGate.declined_mask & (1u << client_id)) {
			struct netclient *client = &g_NetClients[client_id];
			const lobby_start_client_snapshot_t *snapshot =
				&s_LobbyStartTxn.clients[client_id];
			/* A decline removes this client from the candidate match but not from
			 * its room. Restore its exact pre-transaction lobby binding now so it
			 * cannot remain PREPARING with a compacted player's config pointer. */
			if (s_LobbyStartTxn.active
					&& client->peer == snapshot->peer
					&& client->state != CLSTATE_DISCONNECTED) {
				client->state = snapshot->state;
				client->playernum = snapshot->playernum;
				client->settings.handicap = snapshot->settings_handicap;
				client->config = snapshot->config;
			}
			declined++;
			continue;
		}
		if (g_NetClients[client_id].state != CLSTATE_PREPARING
				|| (g_NetClients[client_id].flags & CLFLAG_SPECTATOR)
				|| g_NetClients[client_id].room_id != s_ReadyGate.room_id) {
			return 0;
		}
		s_ReadyGate.roster_client_ids[kept] = client_id;
		s_ReadyGate.roster_configs[kept] = s_ReadyGate.roster_configs[i];
		s_ReadyGate.roster_slots[kept] = s_ReadyGate.roster_slots[i];
		kept++;
	}
	if (kept == 0) return 0;

	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		mpRemoveParticipant(i);
		memset(&g_MatchConfig.slots[i], 0,
			sizeof(g_MatchConfig.slots[i]));
	}
	for (u8 i = 0; i < kept; i++) {
		const u8 client_id = s_ReadyGate.roster_client_ids[i];
		struct netclient *client = &g_NetClients[client_id];
		const ParticipantType type = !g_NetDedicated
			&& client == g_NetLocalClient
			? PARTICIPANT_LOCAL : PARTICIPANT_REMOTE;
		g_PlayerConfigsArray[i] = s_ReadyGate.roster_configs[i];
		g_MatchConfig.slots[i] = s_ReadyGate.roster_slots[i];
		client->playernum = i;
		client->config = &g_PlayerConfigsArray[i];
		client->config->client = client;
		if (mpAddParticipantAt(i, type,
				g_MatchConfig.slots[i].team, (s8)client_id, 0) != i) {
			return 0;
		}
	}
	if (g_Lobby.settings.numSimulants == 0) {
		g_MatchConfig.numSlots = kept;
	}
	s_ReadyGate.roster_count = kept;
	s_ReadyGate.expected_mask &= ~s_ReadyGate.declined_mask;
	s_ReadyGate.ready_mask &= s_ReadyGate.expected_mask;
	s_ReadyGate.declined_mask = 0;
	s_ReadyGate.total_count = kept;
	s_ReadyGate.roster_compacted = 1;
	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.COMMIT ready-gate roster compacted players=%u declined=%u",
		(unsigned)kept, (unsigned)declined);
	return 1;
}

/* Called each time a client responds; starts Phase F countdown when all done. */
static void readyGateCheck(void)
{
	if (!s_ReadyGate.active) {
		return;
	}

	/* Countdown already ticking — don't restart */
	if (s_ReadyGate.countdown_active) {
		return;
	}

	u8 ready   = readyGatePopcount(s_ReadyGate.ready_mask);
	u8 decl    = readyGatePopcount(s_ReadyGate.declined_mask);
	u8 total   = s_ReadyGate.total_count;
	s32 all_responded = ((u8)(ready + decl) >= total);
	s32 timed_out     = (g_NetTick >= s_ReadyGate.deadline_ticks);

	if (!all_responded && !timed_out) {
		return;
	}
	if (timed_out && !all_responded) {
		readyGateAbort("Manifest readiness timed out");
		return;
	}
	if (!readyGateCompactDeclinedRoster()) {
		readyGateAbort("Match roster declined");
		return;
	}

	sysLogPrintf(LOG_NOTE, "NET: ready gate — %u/%u ready %u declined timed_out=%d — starting countdown",
	             (unsigned)ready, (unsigned)total, (unsigned)decl, timed_out);

	/* Phase F: begin 3-second countdown; actual launch happens in readyGateTickCountdown(). */
	s_ReadyGate.countdown_active    = 1;
	s_ReadyGate.countdown_secs      = 3;
	s_ReadyGate.countdown_next_tick = g_NetTick + 60;
	readyGateBroadcastCountdown(MANIFEST_PHASE_LOADING);
}

/* Phase F: Called every server tick from netEndFrame() to drive the launch countdown. */
void readyGateTickCountdown(void)
{
	if (!s_ReadyGate.active) {
		return;
	}

	/* Bug B defensive: if the room this gate targets no longer exists,
	 * abort now.  Prevents mainChangeToStage() from firing into a dead
	 * room if a teardown path bypassed netReadyGateAbortForRoom.
	 * room_id == 0xFF means the gate is global (co-op) — no room binding. */
	if (s_ReadyGate.room_id != 0xFF && roomGetById(s_ReadyGate.room_id) == NULL) {
		sysLogPrintf(LOG_WARNING,
		             "NET: ready gate room %u missing at tick — aborting (defensive)",
		             (unsigned)s_ReadyGate.room_id);
		readyGateAbort("Room closed");
		return;
	}

	/* Check timeout even if countdown hasn't started — handles the case where
	 * all clients went silent (crash/disconnect) and no CLC arrives to trigger
	 * readyGateCheck(). */
	if (!s_ReadyGate.countdown_active && s_ReadyGate.active) {
		if (g_NetTick >= s_ReadyGate.deadline_ticks) {
			readyGateCheck();
			if (!s_ReadyGate.active) return; /* gate fired */
		}
	}

	if (!s_ReadyGate.countdown_active) {
		return;
	}

	if (g_NetTick < s_ReadyGate.countdown_next_tick) {
		return;
	}

	if (s_ReadyGate.countdown_secs > 0) {
		s_ReadyGate.countdown_secs--;
	}

	readyGateBroadcastCountdown(MANIFEST_PHASE_LOADING);

	if (s_ReadyGate.countdown_secs == 0) {
		s32 launch_result;
		sysLogPrintf(LOG_NOTE, "NET: countdown complete -- launching match (stage %u room %u mode %u)",
		             (unsigned)s_ReadyGate.stagenum, (unsigned)s_ReadyGate.room_id,
		             (unsigned)s_ReadyGate.game_mode);
		hub_room_t *room = s_ReadyGate.room_id != 0xFF
			? roomGetById(s_ReadyGate.room_id) : NULL;
		if (room) {
			roomTransition(room, ROOM_STATE_LOADING);
		}

		/* R-3: Set the match room before calling netServerStageStart so it can scope broadcasts */
		g_NetMatchRoomId = s_ReadyGate.room_id;

		/* L2-1: Branch launch path by game mode.  CombatSim uses
		 * netServerStageStart(); co-op/anti uses netServerCoopStageStart()
		 * which sets up g_MissionConfig and player numbers differently. */
		if (s_ReadyGate.game_mode == NETGAMEMODE_COOP ||
		    s_ReadyGate.game_mode == NETGAMEMODE_ANTI) {
			launch_result = netServerCoopStageStart(
				s_ReadyGate.stagenum, s_ReadyGate.difficulty);
		} else {
			/* B-1039: netServerStageStart owns the one authoritative
			 * mpStartMatch/mainChangeToStage transaction for a listen host. */
			launch_result = netServerStageStart();
		}
		if (launch_result != 0) {
			readyGateAbort("Authoritative stage launch rejected");
			return;
		}
		s_ReadyGate.active = 0;
		s_ReadyGate.countdown_active = 0;
		lobbyStartTransactionRelease();
	} else {
		s_ReadyGate.countdown_next_tick = g_NetTick + 60;
	}
}

/* Phase F: Abort the ready gate — cancel the countdown, reset all preparing clients
 * back to CLSTATE_LOBBY, and broadcast SVC_MATCH_CANCELLED to all connected clients. */
static void readyGateAbort(const char *canceller_name)
{
	const u8 room_id = s_ReadyGate.room_id;
	const u32 expected_mask = s_ReadyGate.expected_mask;

	if (!s_ReadyGate.active) {
		return;
	}

	sysLogPrintf(LOG_NOTE, "NET: ready gate aborted by '%s'",
	             canceller_name ? canceller_name : "?");

	if (s_LobbyStartTxn.active) {
		lobbyStartTransactionRollback(canceller_name);
	} else {
		for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
			if ((expected_mask & (1u << i))
					&& g_NetClients[i].state == CLSTATE_PREPARING) {
				g_NetClients[i].state = CLSTATE_LOBBY;
			}
		}
		if (room_id != 0xFF) {
			hub_room_t *room = roomGetById(room_id);
			if (room) roomTransition(room, ROOM_STATE_LOBBY);
		}
	}

	/* Broadcast only to the topology that participated in this gate. */
	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcMatchCancelledWrite(&g_NetMsgRel, canceller_name ? canceller_name : "?");
	netSendToRoom(room_id, &g_NetMsgRel, true, NETCHAN_CONTROL);
	memset(&s_ReadyGate, 0, sizeof(s_ReadyGate));
}

/* Called from room teardown paths. Abort only the exact scoped topology;
 * lounge scope (0xFF) is not an alias for every room. */
void netReadyGateAbortForRoom(u8 room_id, const char *reason)
{
	if (!s_ReadyGate.active) return;
	if (s_ReadyGate.room_id != room_id) return;
	sysLogPrintf(LOG_NOTE,
	             "NET: ready gate aborting — room %u torn down (%s)",
	             (unsigned)room_id, reason ? reason : "?");
	readyGateAbort(reason ? reason : "Room closed");
}

/* Bug B: called from CLC_ROOM_LEAVE / CLC_ROOM_JOIN / CLC_ROOM_CREATE
 * handlers and netServerEvDisconnect after the client has been pulled out
 * of its room.  If this client was a preparing participant in the current
 * gate, abort so the remaining clients don't keep counting down without
 * them. */
void netReadyGateOnClientLeft(u8 clientId)
{
	if (!s_ReadyGate.active) return;
	if (clientId >= NET_MAX_CLIENTS) return;
	const u32 bit = (1u << clientId);
	if (!(s_ReadyGate.expected_mask & bit)) return;
	const char *who = g_NetClients[clientId].settings.name[0]
	                  ? g_NetClients[clientId].settings.name : "Player";
	sysLogPrintf(LOG_NOTE,
	             "NET: ready gate aborting — client %u (%s) left the room/disconnected",
	             (unsigned)clientId, who);
	readyGateAbort(who);
}

s32 netReadyGateCancelByLocalClient(struct netclient *srccl)
{
	if (g_NetMode != NETMODE_SERVER) {
		return -1;
	}

	if (!srccl) {
		return -2;
	}

	/* Cancellation is only valid while the visible 3-2-1 countdown is active. */
	if (!s_ReadyGate.active || !s_ReadyGate.countdown_active) {
		return -3;
	}

	if (srccl->state != CLSTATE_PREPARING) {
		return -4;
	}

	readyGateAbort(srccl->settings.name[0] ? srccl->settings.name : "Player");
	return 0;
}

s32 netReadyGateCancelByServer(void)
{
	if (g_NetMode != NETMODE_SERVER) {
		return -1;
	}

	if (!s_ReadyGate.active || !s_ReadyGate.countdown_active) {
		return -3;
	}

	readyGateAbort("Server");
	return 0;
}

typedef enum net_lobby_start_status_e {
	NET_LOBBY_START_OK = 0,
	NET_LOBBY_START_INVALID_CONTEXT,
	NET_LOBBY_START_NOT_AUTHORITY,
	NET_LOBBY_START_MALFORMED,
	NET_LOBBY_START_INVALID_MODE,
	NET_LOBBY_START_INVALID_STAGE,
	NET_LOBBY_START_INVALID_SCENARIO,
	NET_LOBBY_START_INVALID_WEAPON_SET,
	NET_LOBBY_START_INVALID_SPAWN_WEAPON,
	NET_LOBBY_START_INVALID_ROSTER,
	NET_LOBBY_START_INVALID_PLAYER_IDENTITY,
	NET_LOBBY_START_INVALID_BOT_PROFILE,
	NET_LOBBY_START_INVALID_BOT_IDENTITY,
	NET_LOBBY_START_INVALID_MANIFEST,
	NET_LOBBY_START_PARTICIPANT_POOL_UNAVAILABLE,
	NET_LOBBY_START_WIRE_CAPACITY,
} net_lobby_start_status_e;

typedef struct net_lobby_start_player_plan_t {
	struct netclient *client;
	u8 client_index;
	u8 playernum;
	player_identity_plan_t identity;
	struct mpplayerconfig config;
	struct matchslot slot;
} net_lobby_start_player_plan_t;

typedef struct net_lobby_start_bot_plan_t {
	player_identity_plan_t identity;
	struct mpbotconfig config;
	struct matchslot slot;
} net_lobby_start_bot_plan_t;

typedef struct net_lobby_start_plan_t {
	net_lobby_start_status_e status;
	u8 room_id;
	u8 mode;
	u8 difficulty;
	u8 anti_client_id;
	u8 sim_type;
	u8 timelimit;
	u32 options;
	u8 scorelimit;
	u16 teamscorelimit;
	u8 weapon_set_index;
	s32 resolved_weapon_set;
	char stage_id[CATALOG_ID_LEN];
	u8 stagenum;
	char scenario_id[CATALOG_ID_LEN];
	u8 scenario;
	char weapon_ids[NUM_MPWEAPONSLOTS][CATALOG_ID_LEN];
	u8 weapons[NUM_MPWEAPONSLOTS];
	char spawn_weapon_id[CATALOG_ID_LEN];
	u8 spawn_weapon_mode;
	u8 spawn_weapon_num;
	u8 num_players;
	u8 num_bots;
	net_lobby_start_player_plan_t players[MAX_PLAYERS];
	net_lobby_start_bot_plan_t bots[MAX_BOTS];
	struct mpsetup setup;
	struct matchconfig match;
	struct missionconfig mission;
	struct lobbysettings lobby_settings;
	match_manifest_t manifest;
	u16 advertised_manifest_entries;
	u8 *manifest_wire;
	u32 manifest_wire_len;
	u8 *catalog_wire;
	u32 catalog_wire_len;
} net_lobby_start_plan_t;

static const char *netLobbyStartStatusString(net_lobby_start_status_e status)
{
	switch (status) {
	case NET_LOBBY_START_OK: return "ok";
	case NET_LOBBY_START_INVALID_CONTEXT: return "invalid_context";
	case NET_LOBBY_START_NOT_AUTHORITY: return "not_authority";
	case NET_LOBBY_START_MALFORMED: return "malformed";
	case NET_LOBBY_START_INVALID_MODE: return "invalid_mode";
	case NET_LOBBY_START_INVALID_STAGE: return "invalid_stage";
	case NET_LOBBY_START_INVALID_SCENARIO: return "invalid_scenario";
	case NET_LOBBY_START_INVALID_WEAPON_SET: return "invalid_weapon_set";
	case NET_LOBBY_START_INVALID_SPAWN_WEAPON: return "invalid_spawn_weapon";
	case NET_LOBBY_START_INVALID_ROSTER: return "invalid_roster";
	case NET_LOBBY_START_INVALID_PLAYER_IDENTITY: return "invalid_player_identity";
	case NET_LOBBY_START_INVALID_BOT_PROFILE: return "invalid_bot_profile";
	case NET_LOBBY_START_INVALID_BOT_IDENTITY: return "invalid_bot_identity";
	case NET_LOBBY_START_INVALID_MANIFEST: return "invalid_manifest";
	case NET_LOBBY_START_PARTICIPANT_POOL_UNAVAILABLE: return "participant_pool_unavailable";
	case NET_LOBBY_START_WIRE_CAPACITY: return "wire_capacity";
	default: return "unknown";
	}
}

static u32 netLobbyStartReject(net_lobby_start_plan_t *plan,
		net_lobby_start_status_e status, const char *detail)
{
	if (plan) {
		plan->status = status;
	}
	sysLogPrintf(LOG_ERROR,
		"PLAYER.INIT.ROLLBACK lobby-start status=%s detail=%s globals_published=0",
		netLobbyStartStatusString(status), detail ? detail : "unspecified");
	return 1;
}

static void netLobbyStartPlanFree(net_lobby_start_plan_t *plan)
{
	if (!plan) return;
	manifestFree(&plan->manifest);
	free(plan->manifest_wire);
	free(plan->catalog_wire);
	plan->manifest_wire = NULL;
	plan->catalog_wire = NULL;
}

static u32 netLobbyStartRejectAndFree(net_lobby_start_plan_t *plan,
		net_lobby_start_status_e status, const char *detail)
{
	const u32 result = netLobbyStartReject(plan, status, detail);
	netLobbyStartPlanFree(plan);
	return result;
}

static bool netLobbyStartCopyString(char *dst, size_t dst_size,
		const char *src, bool allow_empty)
{
	size_t len;
	if (!dst || dst_size == 0 || !src) return false;
	len = strnlen(src, dst_size);
	if (len >= dst_size || (!allow_empty && len == 0)) {
		dst[0] = '\0';
		return false;
	}
	memcpy(dst, src, len + 1);
	return true;
}

static bool netLobbyStartRoomContains(const hub_room_t *room, u8 client_id)
{
	if (!room || room->client_count > HUB_MAX_CLIENTS) return false;
	for (u8 i = 0; i < room->client_count; i++) {
		if (room->clients[i] == client_id) return true;
	}
	return false;
}

static bool netLobbyStartEligibleClient(const struct netclient *client,
		u8 room_id)
{
	const ptrdiff_t index = client ? client - g_NetClients : -1;
	return client && index >= 0 && index < NET_MAX_CLIENTS
		&& client->id == (u32)index
		&& client->state == CLSTATE_LOBBY
		&& !(client->flags & CLFLAG_SPECTATOR)
		&& client->room_id == room_id;
}

static bool netLobbyStartIsAuthority(struct netclient *srccl,
		hub_room_t **out_room)
{
	hub_room_t *room = NULL;
	u8 authority_id = NET_NULL_CLIENT;

	if (out_room) *out_room = NULL;
	if (!srccl || srccl->id >= NET_MAX_CLIENTS
			|| &g_NetClients[srccl->id] != srccl
			|| !netLobbyStartEligibleClient(srccl, srccl->room_id)) {
		return false;
	}
	if (srccl->room_id != 0xFF) {
		room = roomGetById(srccl->room_id);
		if (!room || room->state != ROOM_STATE_LOBBY
				|| room->creator_client_id != srccl->id
				|| !netLobbyStartRoomContains(room, (u8)srccl->id)) {
			return false;
		}
		if (out_room) *out_room = room;
		return true;
	}

	/* Pure lounge election: honor a still-valid explicit leader, otherwise
	 * choose the lowest eligible client ID. Never call mutating lobbyUpdate(). */
	if (g_Lobby.leaderSlot < LOBBY_MAX_PLAYERS) {
		const struct lobbyplayer *leader =
			&g_Lobby.players[g_Lobby.leaderSlot];
		if (leader->active && leader->clientId < NET_MAX_CLIENTS
				&& netLobbyStartEligibleClient(
					&g_NetClients[leader->clientId], 0xFF)) {
			authority_id = leader->clientId;
		}
	}
	if (authority_id == NET_NULL_CLIENT) {
		for (u8 i = 0; i < NET_MAX_CLIENTS; i++) {
			if (netLobbyStartEligibleClient(&g_NetClients[i], 0xFF)) {
				authority_id = i;
				break;
			}
		}
	}
	return authority_id == srccl->id;
}

static u32 netLobbyStartPrng(void *userdata)
{
	u32 *state = (u32 *)userdata;
	u32 value = *state ? *state : 0x9e3779b9u;
	value ^= value << 13;
	value ^= value >> 17;
	value ^= value << 5;
	*state = value;
	return value;
}

static bool netLobbyStartResolveRandomSpawn(net_lobby_start_plan_t *plan)
{
	u8 pool[NUM_MPWEAPONS];
	s32 count = 0;
	u32 prng_state;
	s32 picked;
	const char *picked_id;
	const asset_entry_t *picked_entry;

	for (s32 i = 0; i < (s32)plan->manifest.num_entries; i++) {
		const match_manifest_entry_t *entry = &plan->manifest.entries[i];
		const asset_entry_t *asset;
		bool duplicate = false;
		if (entry->type != MANIFEST_TYPE_WEAPON) continue;
		asset = assetCatalogResolve(entry->id);
		if (!asset || !asset->occupied || !asset->enabled
				|| asset->type != ASSET_WEAPON
				|| asset->mp_index <= MPWEAPON_NONE
				|| asset->mp_index >= NUM_MPWEAPONS
				|| asset->runtime_index <= 0
				|| catalogGetMpWeaponNum(asset->mp_index)
					!= asset->runtime_index) {
			continue;
		}
		for (s32 j = 0; j < count; j++) {
			if (pool[j] == (u8)asset->mp_index) {
				duplicate = true;
				break;
			}
		}
		if (!duplicate && count < (s32)ARRAYCOUNT(pool)) {
			pool[count++] = (u8)asset->mp_index;
		}
	}
	if (count == 0) {
		for (s32 i = 0; i < NUM_MPWEAPONSLOTS; i++) {
			if (plan->weapons[i] > MPWEAPON_NONE
					&& plan->weapons[i] < NUM_MPWEAPONS) {
				pool[count++] = plan->weapons[i];
			}
		}
	}
	if (count == 0) return false;
	prng_state = (u32)g_RngSeed ^ (u32)(g_RngSeed >> 32)
		^ g_NetTick ^ plan->manifest.manifest_hash
		^ ((u32)plan->room_id << 24);
	picked = spawnWeaponPickFromSlots(pool, count,
		netLobbyStartPrng, &prng_state);
	picked_id = catalogWeaponIdByMpWeaponId(picked);
	picked_entry = picked_id ? assetCatalogResolve(picked_id) : NULL;
	if (!picked_entry || !picked_entry->occupied || !picked_entry->enabled
			|| picked_entry->type != ASSET_WEAPON
			|| picked_entry->mp_index != picked
			|| !spawnWeaponNumIsResolved(picked_entry->runtime_index)
			|| !netLobbyStartCopyString(plan->spawn_weapon_id,
				sizeof(plan->spawn_weapon_id), picked_entry->id, false)) {
		return false;
	}
	plan->spawn_weapon_num = (u8)picked_entry->runtime_index;
	return true;
}

static bool netLobbyStartPrepareWire(net_lobby_start_plan_t *plan)
{
	struct netbuf wire;
	const u32 capacity = 65536;

	plan->manifest_wire = (u8 *)malloc(capacity);
	plan->catalog_wire = (u8 *)malloc(capacity);
	if (!plan->manifest_wire || !plan->catalog_wire) return false;
	memset(&wire, 0, sizeof(wire));
	wire.data = plan->manifest_wire;
	wire.size = capacity;
	netbufStartWrite(&wire);
	if (netmsgSvcMatchManifestWrite(&wire, &plan->manifest) != 0
			|| wire.wp == 0) {
		return false;
	}
	plan->manifest_wire_len = wire.wp;

	memset(&wire, 0, sizeof(wire));
	wire.data = plan->catalog_wire;
	wire.size = capacity;
	netbufStartWrite(&wire);
	netbufWriteU8(&wire, SVC_SESSION_CATALOG);
	netbufWriteU16(&wire, plan->manifest.num_entries);
	for (s32 i = 0; i < (s32)plan->manifest.num_entries; i++) {
		netbufWriteU16(&wire, (u16)(i + 1));
		netbufWriteU8(&wire, plan->manifest.entries[i].type);
		netbufWriteStr(&wire, plan->manifest.entries[i].id);
	}
	if (wire.error || wire.wp == 0) return false;
	plan->catalog_wire_len = wire.wp;
	return true;
}

static bool netLobbyStartPrepareRoster(net_lobby_start_plan_t *plan,
		struct netclient *srccl, const hub_room_t *room)
{
	bool source_present = false;
	bool client_zero_present = false;

	if (room) {
		bool seen[NET_MAX_CLIENTS] = { false };
		if (room->client_count == 0 || room->client_count > HUB_MAX_CLIENTS) {
			sysLogPrintf(LOG_ERROR,
				"PLAYER.INIT.PREFLIGHT roster=reject reason=room_count count=%u",
				(unsigned)room->client_count);
			return false;
		}
		for (u8 i = 0; i < room->client_count; i++) {
			const u8 id = room->clients[i];
			if (id >= NET_MAX_CLIENTS || seen[id]
					|| g_NetClients[id].room_id != room->id
					|| g_NetClients[id].state < CLSTATE_LOBBY) {
				sysLogPrintf(LOG_ERROR,
					"PLAYER.INIT.PREFLIGHT roster=reject reason=room_member index=%u id=%u state=%u expected_room=%u actual_room=%u duplicate=%u",
					(unsigned)i, (unsigned)id,
					(unsigned)(id < NET_MAX_CLIENTS ? g_NetClients[id].state : 0),
					(unsigned)room->id,
					(unsigned)(id < NET_MAX_CLIENTS ? g_NetClients[id].room_id : 0xFF),
					(unsigned)(id < NET_MAX_CLIENTS ? seen[id] : 0));
				return false;
			}
			seen[id] = true;
		}
	}

	for (u8 i = 0; i < NET_MAX_CLIENTS; i++) {
		struct netclient *client = &g_NetClients[i];
		net_lobby_start_player_plan_t *player;
		net_client_settings_input_t settings_input;
		net_client_settings_plan_t settings_plan;
		net_client_settings_wire_status_e settings_status;
		player_identity_status_e identity_status;

		if (!netLobbyStartEligibleClient(client, plan->room_id)) continue;
		if (room && !netLobbyStartRoomContains(room, i)) {
			sysLogPrintf(LOG_ERROR,
				"PLAYER.INIT.PREFLIGHT roster=reject reason=eligible_not_in_room client=%u room=%u",
				(unsigned)i, (unsigned)plan->room_id);
			return false;
		}
		if (plan->num_players >= MAX_PLAYERS) {
			sysLogPrintf(LOG_ERROR,
				"PLAYER.INIT.PREFLIGHT roster=reject reason=player_capacity client=%u players=%u",
				(unsigned)i, (unsigned)plan->num_players);
			return false;
		}
		settings_input.options = client->settings.options;
		settings_input.body_id = client->settings.body_id;
		settings_input.head_id = client->settings.head_id;
		settings_input.team = client->settings.team;
		settings_input.handicap = client->settings.handicap;
		settings_input.fovy = client->settings.fovy;
		settings_input.fovzoommult = client->settings.fovzoommult;
		settings_input.name = client->settings.name;
		settings_status = netClientSettingsPrepare(&settings_input,
			&settings_plan, &identity_status);
		if (settings_status != NET_CLIENT_SETTINGS_WIRE_OK) {
			sysLogPrintf(LOG_ERROR,
				"PLAYER.INIT.PREFLIGHT roster=reject reason=client_settings client=%u id=%u state=%u room=%u status=%s identity=%s team=%u handicap=%u",
				(unsigned)i, (unsigned)client->id, (unsigned)client->state,
				(unsigned)client->room_id,
				netClientSettingsWireStatusString(settings_status),
				playerIdentityStatusString(identity_status),
				(unsigned)client->settings.team,
				(unsigned)client->settings.handicap);
			return false;
		}
		player = &plan->players[plan->num_players];
		player->identity = settings_plan.identity;
		player->client = client;
		player->client_index = i;
		player->playernum = plan->num_players;
		player->config = g_PlayerConfigsArray[player->playernum];
		player->config.base.mpbodynum = player->identity.mp_body_index >= 0
			? (u8)player->identity.mp_body_index : 0xFF;
		player->config.base.mpheadnum = player->identity.mp_head_index >= 0
			? (u8)player->identity.mp_head_index : 0xFF;
		player->config.base.team = settings_plan.team;
		/* v55: the authenticated client's settings are the sole handicap
		 * authority. CLC_LOBBY_START no longer carries a positional four-slot
		 * cache that can bind another machine's local slot to this player. */
		player->config.handicap = settings_plan.handicap;
		strncpy(player->config.base.body_id, player->identity.body_id,
			sizeof(player->config.base.body_id) - 1);
		player->config.base.body_id[
			sizeof(player->config.base.body_id) - 1] = '\0';
		strncpy(player->config.base.head_id, player->identity.head_id,
			sizeof(player->config.base.head_id) - 1);
		player->config.base.head_id[
			sizeof(player->config.base.head_id) - 1] = '\0';
		if (client != g_NetLocalClient) {
			player->config.controlmode = CONTROLMODE_NA;
			snprintf(player->config.base.name,
				sizeof(player->config.base.name), "%s\n",
				settings_plan.name);
			player->config.options =
				g_PlayerConfigsArray[0].options & OPTION_PAINTBALL;
			player->config.options |=
				settings_plan.options & ~OPTION_PAINTBALL;
			player->config.options &=
				~(OPTION_AIMCONTROL | OPTION_LOOKAHEAD);
			player->config.options |=
				OPTION_FORWARDPITCH | OPTION_ASKEDSAVEPLAYER;
		} else {
			memcpy(player->config.base.name,
				settings_plan.name, strlen(settings_plan.name) + 1);
		}

		memset(&player->slot, 0, sizeof(player->slot));
		player->slot.type = SLOT_PLAYER;
		player->slot.team = settings_plan.team;
		player->slot.bodynum = player->config.base.mpbodynum;
		player->slot.headnum = player->config.base.mpheadnum;
		strncpy(player->slot.body_id, player->identity.body_id,
			sizeof(player->slot.body_id) - 1);
		strncpy(player->slot.head_id, player->identity.head_id,
			sizeof(player->slot.head_id) - 1);
		memcpy(player->slot.name, settings_plan.name,
			strlen(settings_plan.name) + 1);

		source_present = source_present || client == srccl;
		client_zero_present = client_zero_present || client->id == 0;
		plan->num_players++;
	}
	if (!source_present || !client_zero_present || plan->num_players == 0) {
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.PREFLIGHT roster=reject reason=required_member source=%u client0=%u players=%u",
			(unsigned)source_present, (unsigned)client_zero_present,
			(unsigned)plan->num_players);
		return false;
	}
	if (plan->mode == NETGAMEMODE_COOP && plan->num_players > 2) {
		return false;
	}
	if (plan->mode == NETGAMEMODE_ANTI) {
		bool anti_present = false;
		if (plan->num_players != 2 || plan->anti_client_id == NET_NULL_CLIENT
				|| plan->anti_client_id == srccl->id) {
			return false;
		}
		for (u8 i = 0; i < plan->num_players; i++) {
			anti_present = anti_present
				|| plan->players[i].client->id == plan->anti_client_id;
		}
		if (!anti_present) return false;
	}
	return true;
}

static bool netLobbyStartReadBots(struct netbuf *src,
		net_lobby_start_plan_t *plan, u8 advertised_bots)
{
	if (plan->mode != NETGAMEMODE_MP) {
		return advertised_bots == 0 && plan->sim_type == 0;
	}
	if (advertised_bots > MAX_BOTS
			|| plan->num_players + advertised_bots > MATCH_PARTICIPANT_CAP) {
		return false;
	}
	for (u8 i = 0; i < advertised_bots; i++) {
		net_lobby_start_bot_plan_t *bot = &plan->bots[i];
		const char *name = netbufReadStr(src);
		const char *body_id = netbufReadStr(src);
		const char *head_id = netbufReadStr(src);
		const char *profile_id = netbufReadStr(src);
		const u8 wire_difficulty = netbufReadU8(src);
		const u8 wire_type = netbufReadU8(src);
		const u8 wire_team = netbufReadU8(src);
		const asset_entry_t *profile_entry;
		const asset_runtime_binding_t *profile;
		size_t name_len;

		if (src->error || !name || !body_id || !head_id || !profile_id) {
			return false;
		}
		name_len = strnlen(name, sizeof(bot->config.base.name));
		if (name_len == 0 || name_len >= sizeof(bot->config.base.name)
				|| name_len >= sizeof(bot->slot.name)
				|| wire_team >= MAX_TEAMS
				|| playerIdentityPrepare(body_id, head_id, &bot->identity)
					!= PLAYER_IDENTITY_OK) {
			return false;
		}
		profile_entry = assetCatalogResolve(profile_id);
		profile = profile_entry && profile_entry->occupied
			&& profile_entry->enabled
			&& profile_entry->type == ASSET_BOT_PROFILE
			? mpBotProfileRuntimeBindingById(profile_entry->id) : NULL;
		if (!profile || !profile->active || !profile->source_hydrated
				|| profile->type != ASSET_BOT_PROFILE
				|| strcmp(profile->id, profile_entry->id) != 0
				|| !assetRuntimePrimaryFileAccessible(profile)
				|| profile->bot_profile_type < 0
				|| profile->bot_profile_type > 255
				|| profile->bot_profile_difficulty < 0
				|| profile->bot_profile_difficulty > 255
				|| wire_type != (u8)profile->bot_profile_type
				|| wire_difficulty != (u8)profile->bot_profile_difficulty) {
			return false;
		}

		bot->config = g_BotConfigsArray[i];
		bot->config.base.mpbodynum = bot->identity.mp_body_index >= 0
			? (u8)bot->identity.mp_body_index : 0xFF;
		bot->config.base.mpheadnum = bot->identity.mp_head_index >= 0
			? (u8)bot->identity.mp_head_index : 0xFF;
		bot->config.base.team = wire_team;
		bot->config.type = wire_type;
		bot->config.difficulty = wire_difficulty;
		memcpy(bot->config.base.name, name, name_len + 1);
		strncpy(bot->config.base.body_id, bot->identity.body_id,
			sizeof(bot->config.base.body_id) - 1);
		bot->config.base.body_id[
			sizeof(bot->config.base.body_id) - 1] = '\0';
		strncpy(bot->config.base.head_id, bot->identity.head_id,
			sizeof(bot->config.base.head_id) - 1);
		bot->config.base.head_id[
			sizeof(bot->config.base.head_id) - 1] = '\0';
		strncpy(bot->config.profile_id, profile_entry->id,
			sizeof(bot->config.profile_id) - 1);
		bot->config.profile_id[sizeof(bot->config.profile_id) - 1] = '\0';

		memset(&bot->slot, 0, sizeof(bot->slot));
		bot->slot.type = SLOT_BOT;
		bot->slot.team = wire_team;
		bot->slot.bodynum = bot->config.base.mpbodynum;
		bot->slot.headnum = bot->config.base.mpheadnum;
		bot->slot.botType = wire_type;
		bot->slot.botDifficulty = wire_difficulty;
		memcpy(bot->slot.name, name, name_len + 1);
		strncpy(bot->slot.body_id, bot->identity.body_id,
			sizeof(bot->slot.body_id) - 1);
		strncpy(bot->slot.head_id, bot->identity.head_id,
			sizeof(bot->slot.head_id) - 1);
		strncpy(bot->slot.profile_id, profile_entry->id,
			sizeof(bot->slot.profile_id) - 1);
	}
	plan->num_bots = advertised_bots;
	return (advertised_bots == 0 && plan->sim_type == 0)
		|| (advertised_bots > 0
			&& plan->sim_type == plan->bots[0].config.difficulty);
}

static bool netLobbyStartPrepareManifest(struct netbuf *src,
		net_lobby_start_plan_t *plan)
{
	if (manifestDeserializeStrict(src, &plan->manifest,
			&plan->advertised_manifest_entries) != 0
			|| src->error
			|| plan->manifest.num_entries != plan->advertised_manifest_entries
			|| !netLobbyManifestValidate(&plan->manifest, plan->stage_id)) {
		return false;
	}
	manifestComputeHash(&plan->manifest);

	if (plan->mode == NETGAMEMODE_MP) {
		if (plan->spawn_weapon_mode == SPAWNWEAPON_MODE_RANDOM) {
			if (!netLobbyStartResolveRandomSpawn(plan)) return false;
		} else if (plan->spawn_weapon_mode == SPAWNWEAPON_MODE_FIESTA) {
			plan->spawn_weapon_id[0] = '\0';
			plan->spawn_weapon_num = SPAWNWEAPON_FIESTA_SENTINEL;
		}
	}

	if (!manifestAppendCatalogClosure(&plan->manifest, plan->stage_id,
			MANIFEST_SLOT_MATCH)) {
		return false;
	}
	for (u8 i = 0; i < plan->num_players; i++) {
		if (!manifestAppendCatalogClosure(&plan->manifest,
				plan->players[i].identity.body_id, i)
				|| !manifestAppendCatalogClosure(&plan->manifest,
					plan->players[i].identity.head_id, i)) {
			return false;
		}
	}
	if (plan->mode == NETGAMEMODE_MP) {
		if (!manifestAppendCatalogClosure(&plan->manifest,
				plan->scenario_id, MANIFEST_SLOT_MATCH)) {
			return false;
		}
		for (s32 i = 0; i < NUM_MPWEAPONSLOTS; i++) {
			if (plan->weapon_ids[i][0]
					&& !manifestAppendCatalogClosure(&plan->manifest,
						plan->weapon_ids[i], MANIFEST_SLOT_MATCH)) {
				return false;
			}
		}
		if (plan->spawn_weapon_id[0]
				&& !manifestAppendCatalogClosure(&plan->manifest,
					plan->spawn_weapon_id, MANIFEST_SLOT_MATCH)) {
			return false;
		}
		for (u8 i = 0; i < plan->num_bots; i++) {
			const u8 slot = (u8)(MAX_PLAYERS + i);
			if (!manifestAppendCatalogClosure(&plan->manifest,
					plan->bots[i].identity.body_id, slot)
					|| !manifestAppendCatalogClosure(&plan->manifest,
						plan->bots[i].identity.head_id, slot)
					|| !manifestAppendCatalogClosure(&plan->manifest,
						plan->bots[i].config.profile_id, slot)) {
				return false;
			}
		}
	}
	manifestComputeHash(&plan->manifest);
	return netLobbyManifestValidate(&plan->manifest, plan->stage_id)
		&& netLobbyStartPrepareWire(plan);
}

static bool netLobbyStartBuildState(net_lobby_start_plan_t *plan)
{
	plan->setup = g_MpSetup;
	plan->match = g_MatchConfig;
	plan->mission = g_MissionConfig;
	plan->lobby_settings = g_Lobby.settings;

	strncpy(plan->setup.stage_id, plan->stage_id,
		sizeof(plan->setup.stage_id) - 1);
	plan->setup.stage_id[sizeof(plan->setup.stage_id) - 1] = '\0';
	plan->setup.stagenum = plan->stagenum;
	memset(plan->match.slots, 0, sizeof(plan->match.slots));
	memset(plan->match.weapon_ids, 0, sizeof(plan->match.weapon_ids));
	memset(plan->match.weapons, 0, sizeof(plan->match.weapons));
	strncpy(plan->match.stage_id, plan->stage_id,
		sizeof(plan->match.stage_id) - 1);
	plan->match.stage_id[sizeof(plan->match.stage_id) - 1] = '\0';
	plan->match.stagenum = plan->stagenum;
	plan->match.options_engine_forced = 0;

	for (u8 i = 0; i < plan->num_players; i++) {
		plan->match.slots[i] = plan->players[i].slot;
	}
	for (u8 i = 0; i < plan->num_bots; i++) {
		plan->match.slots[MAX_PLAYERS + i] = plan->bots[i].slot;
	}
	plan->match.numSlots = plan->num_bots
		? (u8)(MAX_PLAYERS + plan->num_bots) : plan->num_players;

	if (plan->mode == NETGAMEMODE_MP) {
		strncpy(plan->match.scenario_id, plan->scenario_id,
			sizeof(plan->match.scenario_id) - 1);
		plan->match.scenario_id[
			sizeof(plan->match.scenario_id) - 1] = '\0';
		plan->match.scenario = plan->scenario;
		plan->match.timelimit = plan->timelimit;
		plan->match.scorelimit = plan->scorelimit;
		plan->match.teamscorelimit = plan->teamscorelimit;
		plan->match.options = plan->options;
		plan->match.weaponSetIndex = plan->weapon_set_index == 0xFF
			? -1 : (s8)plan->weapon_set_index;
		memcpy(plan->match.weapon_ids, plan->weapon_ids,
			sizeof(plan->match.weapon_ids));
		memcpy(plan->match.weapons, plan->weapons,
			sizeof(plan->match.weapons));
		strncpy(plan->match.spawn_weapon_id, plan->spawn_weapon_id,
			sizeof(plan->match.spawn_weapon_id) - 1);
		plan->match.spawn_weapon_id[
			sizeof(plan->match.spawn_weapon_id) - 1] = '\0';
		plan->match.spawnWeaponMode = plan->spawn_weapon_mode;
		plan->match.spawnWeaponNum = plan->spawn_weapon_num;

		plan->setup.scenario = plan->scenario;
		plan->setup.timelimit = plan->timelimit;
		plan->setup.scorelimit = plan->scorelimit;
		plan->setup.teamscorelimit = plan->teamscorelimit;
		plan->setup.options = plan->options;
		memcpy(plan->setup.weapons, plan->weapons,
			sizeof(plan->setup.weapons));
	} else {
		const s32 solo_index = soloStageGetIndex(plan->stagenum);
		if (solo_index < 0 || solo_index > 255
				|| strcmp(g_SoloStages[solo_index].catalog_id,
					plan->stage_id) != 0) {
			return false;
		}
		strncpy(plan->mission.stage_id, plan->stage_id,
			sizeof(plan->mission.stage_id) - 1);
		plan->mission.stage_id[
			sizeof(plan->mission.stage_id) - 1] = '\0';
		plan->mission.stagenum = plan->stagenum;
		plan->mission.stageindex = (u8)solo_index;
		plan->mission.difficulty = plan->difficulty;
		plan->mission.iscoop = plan->mode == NETGAMEMODE_COOP;
		plan->mission.isanti = plan->mode == NETGAMEMODE_ANTI;
	}

	strncpy(plan->lobby_settings.stage_id, plan->stage_id,
		sizeof(plan->lobby_settings.stage_id) - 1);
	plan->lobby_settings.stage_id[
		sizeof(plan->lobby_settings.stage_id) - 1] = '\0';
	plan->lobby_settings.stagenum = plan->stagenum;
	plan->lobby_settings.scenario = plan->mode == NETGAMEMODE_MP
		? plan->scenario : 0;
	plan->lobby_settings.numSimulants = plan->num_bots;
	plan->lobby_settings.teamEnabled = plan->mode == NETGAMEMODE_MP
		&& (plan->options & MPOPTION_TEAMSENABLED) != 0;
	return true;
}

static bool netLobbyStartCommit(net_lobby_start_plan_t *plan)
{
	hub_room_t *room = plan->room_id == 0xFF
		? NULL : roomGetById(plan->room_id);
	struct netbuf wire;
	u32 expected_mask = 0;

	if (!lobbyStartTransactionBegin(plan->room_id)) return false;
	g_ServerManifest = plan->manifest;
	memset(&plan->manifest, 0, sizeof(plan->manifest));
	g_NetGameMode = plan->mode;
	g_NetCounterOpClientId = plan->mode == NETGAMEMODE_ANTI
		? plan->anti_client_id : NET_NULL_CLIENT;
	g_NetMatchRoomId = plan->room_id;
	g_MpSetup = plan->setup;
	g_MatchConfig = plan->match;
	g_MissionConfig = plan->mission;
	g_Lobby.settings = plan->lobby_settings;
	g_Vars.bondplayernum = 0;
	g_Vars.coopplayernum = plan->mode == NETGAMEMODE_COOP
		&& plan->num_players == 2 ? 1 : -1;
	g_Vars.antiplayernum = -1;
	if (plan->mode == NETGAMEMODE_ANTI) {
		for (u8 i = 0; i < plan->num_players; i++) {
			if (plan->players[i].client->id == plan->anti_client_id) {
				g_Vars.antiplayernum = i;
				break;
			}
		}
		if (g_Vars.antiplayernum <= 0) {
			lobbyStartTransactionRollback("Counter-Op role publication failed");
			return false;
		}
	}

	if (plan->mode == NETGAMEMODE_MP) {
		mpCommitPreparedWeaponSet(plan->resolved_weapon_set, plan->weapons);
	}
	memset(g_BotConfigsArray, 0, sizeof(g_BotConfigsArray));
	mpClearAllParticipants();
	for (u8 i = 0; i < plan->num_players; i++) {
		net_lobby_start_player_plan_t *player = &plan->players[i];
		const ParticipantType type = !g_NetDedicated
			&& player->client == g_NetLocalClient
			? PARTICIPANT_LOCAL : PARTICIPANT_REMOTE;
		g_PlayerConfigsArray[i] = player->config;
		player->client->playernum = i;
		player->client->config = &g_PlayerConfigsArray[i];
		player->client->config->client = player->client;
		player->client->settings.handicap = player->config.handicap;
		player->client->state = CLSTATE_PREPARING;
		if (mpAddParticipantAt(i, type, player->slot.team,
				(s8)player->client_index, 0) != i) {
			lobbyStartTransactionRollback("player participant commit failed");
			return false;
		}
		expected_mask |= 1u << player->client_index;
	}
	for (u8 i = 0; i < plan->num_bots; i++) {
		const s32 participant_slot = MAX_PLAYERS + i;
		g_BotConfigsArray[i] = plan->bots[i].config;
		if (mpAddParticipantAt(participant_slot, PARTICIPANT_BOT,
				plan->bots[i].slot.team, -1, 0xFF) != participant_slot) {
			lobbyStartTransactionRollback("bot participant commit failed");
			return false;
		}
	}

	sessionCatalogBuild(&g_ServerManifest);
	if (g_SessionCatalog.num_entries != g_ServerManifest.num_entries) {
		lobbyStartTransactionRollback("session catalog count drifted");
		return false;
	}
	for (u16 i = 0; i < g_SessionCatalog.num_entries; i++) {
		if (g_SessionCatalog.entries[i].wire_id != (u16)(i + 1)
				|| strcmp(g_SessionCatalog.entries[i].catalog_id,
					g_ServerManifest.entries[i].id) != 0) {
			lobbyStartTransactionRollback("session catalog identity drifted");
			return false;
		}
	}

	memset(&s_ReadyGate, 0, sizeof(s_ReadyGate));
	s_ReadyGate.active = 1;
	s_ReadyGate.expected_mask = expected_mask;
	s_ReadyGate.deadline_ticks = g_NetTick + READY_GATE_TIMEOUT_TICKS;
	s_ReadyGate.stagenum = plan->stagenum;
	s_ReadyGate.total_count = plan->num_players;
	s_ReadyGate.game_mode = plan->mode;
	s_ReadyGate.difficulty = plan->difficulty;
	s_ReadyGate.room_id = plan->room_id;
	s_ReadyGate.roster_count = plan->num_players;
	for (u8 i = 0; i < plan->num_players; i++) {
		s_ReadyGate.roster_client_ids[i] = plan->players[i].client_index;
		s_ReadyGate.roster_configs[i] = plan->players[i].config;
		s_ReadyGate.roster_slots[i] = plan->players[i].slot;
	}
	if (!g_NetDedicated && g_NetLocalClient) {
		const ptrdiff_t local_index = g_NetLocalClient - g_NetClients;
		if (local_index >= 0 && local_index < NET_MAX_CLIENTS
				&& (expected_mask & (1u << local_index))) {
			s_ReadyGate.ready_mask |= 1u << local_index;
		}
	}
	if (room) roomTransition(room, ROOM_STATE_PREPARING);

	/* Publish the catalog before the manifest. The manifest handler may send
	 * READY immediately, so reliable channel order must guarantee that the
	 * session-ID translation table already exists when readiness is accepted. */
	memset(&wire, 0, sizeof(wire));
	wire.data = plan->catalog_wire;
	wire.size = plan->catalog_wire_len;
	wire.wp = plan->catalog_wire_len;
	netSendToRoom(plan->room_id, &wire, true, NETCHAN_CONTROL);
	memset(&wire, 0, sizeof(wire));
	wire.data = plan->manifest_wire;
	wire.size = plan->manifest_wire_len;
	wire.wp = plan->manifest_wire_len;
	netSendToRoom(plan->room_id, &wire, true, NETCHAN_CONTROL);
	free(plan->manifest_wire);
	free(plan->catalog_wire);
	plan->manifest_wire = NULL;
	plan->catalog_wire = NULL;

	readyGateBroadcastCountdown(MANIFEST_PHASE_CHECKING);
	readyGateCheck();
	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.COMMIT lobby-start room=%u mode=%u players=%u bots=%u manifest=%u",
		(unsigned)plan->room_id, (unsigned)plan->mode,
		(unsigned)plan->num_players, (unsigned)plan->num_bots,
		(unsigned)g_ServerManifest.num_entries);
	return true;
}

u32 netmsgClcLobbyStartRead(struct netbuf *src, struct netclient *srccl)
{
	net_lobby_start_plan_t plan;
	hub_room_t *room = NULL;
	catalog_stage_result_t stage;
	const char *wire_string;
	u8 advertised_bots;
	u8 prepared_weapons[NUM_MPWEAPONSLOTS];
	s32 prepared_set;
	u64 rng_seed;
	u64 rng2_seed;

	memset(&plan, 0, sizeof(plan));
	plan.status = NET_LOBBY_START_MALFORMED;
	plan.anti_client_id = NET_NULL_CLIENT;
	plan.spawn_weapon_num = 0xFF;
	if (!src || g_NetMode != NETMODE_SERVER || s_ReadyGate.active
			|| s_LobbyStartTxn.active) {
		return netLobbyStartRejectAndFree(&plan,
			NET_LOBBY_START_INVALID_CONTEXT,
			"server is not in an idle lobby-start state");
	}
	if (!netLobbyStartIsAuthority(srccl, &room)) {
		return netLobbyStartRejectAndFree(&plan,
			NET_LOBBY_START_NOT_AUTHORITY,
			"sender is not the pure room/lounge authority");
	}
	plan.room_id = srccl->room_id;
	if (!g_MpParticipants.slots
			|| g_MpParticipants.capacity < MAX_MPCHRS) {
		return netLobbyStartRejectAndFree(&plan,
			NET_LOBBY_START_PARTICIPANT_POOL_UNAVAILABLE,
			"participant pool is not preallocated at runtime capacity");
	}

	plan.mode = netbufReadU8(src);
	wire_string = netbufReadStr(src);
	if (!netLobbyStartCopyString(plan.stage_id, sizeof(plan.stage_id),
			wire_string, false)) {
		return netLobbyStartRejectAndFree(&plan,
			NET_LOBBY_START_INVALID_STAGE,
			"stage ID is empty, truncated, or unterminated");
	}
	plan.difficulty = netbufReadU8(src);
	plan.anti_client_id = netbufReadU8(src);
	advertised_bots = netbufReadU8(src);
	plan.sim_type = netbufReadU8(src);
	plan.timelimit = netbufReadU8(src);
	plan.options = netbufReadU32(src);
	wire_string = netbufReadStr(src);
	if (!netLobbyStartCopyString(plan.scenario_id,
			sizeof(plan.scenario_id), wire_string ? wire_string : "", true)) {
		return netLobbyStartRejectAndFree(&plan,
			NET_LOBBY_START_MALFORMED, "scenario ID is unterminated");
	}
	plan.scorelimit = netbufReadU8(src);
	plan.teamscorelimit = netbufReadU16(src);
	plan.weapon_set_index = netbufReadU8(src);
	if (src->error) {
		return netLobbyStartRejectAndFree(&plan,
			NET_LOBBY_START_MALFORMED, "truncated fixed settings");
	}
	if (plan.mode != NETGAMEMODE_MP && plan.mode != NETGAMEMODE_COOP
			&& plan.mode != NETGAMEMODE_ANTI) {
		return netLobbyStartRejectAndFree(&plan,
			NET_LOBBY_START_INVALID_MODE, "unknown game mode");
	}
	if (!catalogResolveStageForType(plan.stage_id,
			plan.mode == NETGAMEMODE_MP ? ASSET_ARENA : ASSET_MAP,
			&stage) || !stage.entry
			|| !stage.entry->occupied || !stage.entry->enabled
			|| strcmp(stage.entry->id, plan.stage_id) != 0
			|| stage.stagenum <= 0 || stage.stagenum > 255
			|| (plan.mode == NETGAMEMODE_MP
				? stage.entry->type != ASSET_ARENA
				: stage.entry->type != ASSET_MAP)) {
		return netLobbyStartRejectAndFree(&plan,
			NET_LOBBY_START_INVALID_STAGE,
			"stage has no exact mode/runtime binding");
	}
	plan.stagenum = (u8)stage.stagenum;

	for (s32 i = 0; i < NUM_MPWEAPONSLOTS; i++) {
		const asset_entry_t *weapon;
		wire_string = netbufReadStr(src);
		if (!netLobbyStartCopyString(plan.weapon_ids[i],
				sizeof(plan.weapon_ids[i]), wire_string ? wire_string : "", true)) {
			return netLobbyStartRejectAndFree(&plan,
				NET_LOBBY_START_MALFORMED,
				"weapon ID is truncated or unterminated");
		}
		if (!plan.weapon_ids[i][0]) {
			plan.weapons[i] = MPWEAPON_NONE;
			continue;
		}
		weapon = assetCatalogResolve(plan.weapon_ids[i]);
		if (!weapon || !weapon->occupied || !weapon->enabled
				|| weapon->type != ASSET_WEAPON
				|| weapon->mp_index < 0 || weapon->mp_index >= NUM_MPWEAPONS
				|| weapon->runtime_index <= 0
				|| catalogGetMpWeaponNum(weapon->mp_index)
					!= weapon->runtime_index) {
			return netLobbyStartRejectAndFree(&plan,
				NET_LOBBY_START_INVALID_WEAPON_SET,
				"weapon ID has no exact runtime binding");
		}
		plan.weapons[i] = (u8)weapon->mp_index;
	}
	wire_string = netbufReadStr(src);
	if (!netLobbyStartCopyString(plan.spawn_weapon_id,
			sizeof(plan.spawn_weapon_id), wire_string ? wire_string : "", true)) {
		return netLobbyStartRejectAndFree(&plan,
			NET_LOBBY_START_MALFORMED,
			"spawn weapon ID is truncated or unterminated");
	}
	plan.spawn_weapon_mode = netbufReadU8(src);
	if (src->error) {
		return netLobbyStartRejectAndFree(&plan,
			NET_LOBBY_START_MALFORMED,
			"truncated weapon payload");
	}

	if (plan.mode == NETGAMEMODE_MP) {
		const asset_entry_t *scenario = assetCatalogResolve(plan.scenario_id);
		if (plan.difficulty != 0 || plan.anti_client_id != NET_NULL_CLIENT
				|| !scenario || !scenario->occupied || !scenario->enabled
				|| scenario->type != ASSET_GAMEMODE
				|| scenario->ext.gamemode.mode_id < MPSCENARIO_COMBAT
				|| scenario->ext.gamemode.mode_id > MPSCENARIO_CAPTURETHECASE) {
			return netLobbyStartRejectAndFree(&plan,
				NET_LOBBY_START_INVALID_SCENARIO,
				"Combat Simulator metadata is not exact");
		}
		plan.scenario = (u8)scenario->ext.gamemode.mode_id;
		rng_seed = g_RngSeed;
		rng2_seed = g_Rng2Seed;
		prepared_set = plan.weapon_set_index == 0xFF
			? WEAPONSET_CUSTOM : (s32)plan.weapon_set_index;
		if (prepared_set == WEAPONSET_RANDOM
				|| prepared_set == WEAPONSET_RANDOMFIVE) {
			/* v54 transports the random-set intent plus the already resolved exact
			 * slots. Never roll again on the server after the authority has frozen
			 * its candidate and manifest. */
			memcpy(prepared_weapons, plan.weapons,
				sizeof(prepared_weapons));
		} else {
			if (mpPrepareWeaponSet(prepared_set, plan.weapons,
					prepared_weapons, &prepared_set) != 0) {
				g_RngSeed = rng_seed;
				g_Rng2Seed = rng2_seed;
				return netLobbyStartRejectAndFree(&plan,
					NET_LOBBY_START_INVALID_WEAPON_SET,
					"weapon set could not prepare");
			}
			g_RngSeed = rng_seed;
			g_Rng2Seed = rng2_seed;
			if (memcmp(prepared_weapons, plan.weapons,
					sizeof(plan.weapons)) != 0) {
				return netLobbyStartRejectAndFree(&plan,
					NET_LOBBY_START_INVALID_WEAPON_SET,
					"weapon set metadata and exact slots disagree");
			}
		}
		plan.resolved_weapon_set = prepared_set;
		if (plan.spawn_weapon_mode == SPAWNWEAPON_MODE_SPECIFIC) {
			const asset_entry_t *spawn =
				assetCatalogResolve(plan.spawn_weapon_id);
			if (!spawn || !spawn->occupied || !spawn->enabled
					|| spawn->type != ASSET_WEAPON
					|| spawn->runtime_index <= 0
					|| spawn->runtime_index >= WEAPON_CUSTOM_END) {
				return netLobbyStartRejectAndFree(&plan,
					NET_LOBBY_START_INVALID_SPAWN_WEAPON,
					"specific spawn weapon has no runtime binding");
			}
			plan.spawn_weapon_num = (u8)spawn->runtime_index;
		} else if ((plan.spawn_weapon_mode != SPAWNWEAPON_MODE_RANDOM
				&& plan.spawn_weapon_mode != SPAWNWEAPON_MODE_FIESTA)
				|| plan.spawn_weapon_id[0]) {
			return netLobbyStartRejectAndFree(&plan,
				NET_LOBBY_START_INVALID_SPAWN_WEAPON,
				"random/Fiesta spawn intent is inconsistent");
		}
	} else {
		if (plan.difficulty > DIFF_PD || advertised_bots != 0
				|| plan.sim_type != 0 || plan.timelimit != 0
				|| plan.options != 0 || plan.scenario_id[0]
				|| plan.scorelimit != 0 || plan.teamscorelimit != 0
				|| plan.weapon_set_index != 0xFF
				|| plan.spawn_weapon_id[0]
				|| plan.spawn_weapon_mode != SPAWNWEAPON_MODE_RANDOM
				|| (plan.mode == NETGAMEMODE_COOP
					&& plan.anti_client_id != NET_NULL_CLIENT)
				|| (plan.mode == NETGAMEMODE_ANTI
					&& (plan.anti_client_id == NET_NULL_CLIENT
						|| plan.anti_client_id >= NET_MAX_CLIENTS))) {
			return netLobbyStartRejectAndFree(&plan,
				NET_LOBBY_START_INVALID_MODE,
				"co-op/Counter-Op carries noncanonical metadata");
		}
		for (s32 i = 0; i < NUM_MPWEAPONSLOTS; i++) {
			if (plan.weapon_ids[i][0]) {
				return netLobbyStartRejectAndFree(&plan,
					NET_LOBBY_START_INVALID_WEAPON_SET,
					"mission start carries Combat Simulator weapons");
			}
		}
		plan.resolved_weapon_set = WEAPONSET_CUSTOM;
	}

	if (!netLobbyStartPrepareRoster(&plan, srccl, room)) {
		return netLobbyStartRejectAndFree(&plan,
			NET_LOBBY_START_INVALID_ROSTER,
			"room membership, player slots, or Counter-Op roles disagree");
	}
	if (!netLobbyStartReadBots(src, &plan, advertised_bots)) {
		return netLobbyStartRejectAndFree(&plan,
			NET_LOBBY_START_INVALID_BOT_PROFILE,
			"bot payload is malformed or lacks exact typed bindings");
	}
	if (!netLobbyStartPrepareManifest(src, &plan)) {
		return netLobbyStartRejectAndFree(&plan,
			NET_LOBBY_START_INVALID_MANIFEST,
			"host manifest is not strict, complete, or wire-encodable");
	}
	if (!netLobbyStartBuildState(&plan)) {
		return netLobbyStartRejectAndFree(&plan,
			NET_LOBBY_START_INVALID_STAGE,
			"prepared state crossed an invalid stage-index domain");
	}
	if (!netLobbyStartCommit(&plan)) {
		return netLobbyStartRejectAndFree(&plan,
			NET_LOBBY_START_INVALID_CONTEXT,
			"atomic publication failed and was rolled back");
	}
	plan.status = NET_LOBBY_START_OK;
	netLobbyStartPlanFree(&plan);
	return 0;
}

/* ========================================================================
 * SVC_LOBBY_LEADER - Server announces authoritative lobby leader
 *
 * Sent to all clients when the leader changes (first join, leader
 * disconnect, manual reassignment). Clients apply this via lobbySetLeader().
 *
 * Payload: leaderClientId (u8) — the netclient ID of the new leader.
 *          0xFF = no leader.
 * ======================================================================== */

u32 netmsgSvcLobbyLeaderWrite(struct netbuf *dst, u8 leaderClientId)
{
	netbufWriteU8(dst, SVC_LOBBY_LEADER);
	netbufWriteU8(dst, leaderClientId);
	return dst->error;
}

u32 netmsgSvcLobbyLeaderRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 leaderClientId = netbufReadU8(src);

	if (src->error) {
		return src->error;
	}

	sysLogPrintf(LOG_NOTE, "NET: SVC_LOBBY_LEADER: leader is client %u", leaderClientId);

	/* Find the lobby slot for this client ID and set as leader */
	if (leaderClientId == 0xFF) {
		lobbySetLeader(0xFF);
	} else {
		for (s32 i = 0; i < g_Lobby.numPlayers; i++) {
			if (g_Lobby.players[i].active &&
			    g_Lobby.players[i].clientId == leaderClientId) {
				lobbySetLeader(i);
				break;
			}
		}
	}

	return src->error;
}

/* ========================================================================
 * SVC_LOBBY_STATE - Server broadcasts lobby state update
 *
 * Sent to all clients when the lobby state changes (game mode selected,
 * stage changed, match starting/ending). Clients update their local
 * lobby display accordingly.
 *
 * Payload: gamemode (u8), arena (catalog ID str), status (u8)
 * v27: arena encoded as catalog ID string (no net_hash on wire).
 * Status: 0=waiting, 1=starting, 2=in-game
 * ======================================================================== */

u32 netmsgSvcLobbyStateWrite(struct netbuf *dst, u8 gamemode, const char *stage_id, u8 status)
{
	netbufWriteU8(dst, SVC_LOBBY_STATE);
	netbufWriteU8(dst, gamemode);
	/* v27: encode arena as catalog ID string — no net_hash on wire. */
	netbufWriteStr(dst, stage_id ? stage_id : "");
	netbufWriteU8(dst, status);
	return dst->error;
}

u32 netmsgSvcLobbyStateRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 gamemode = netbufReadU8(src);
	/* v27: read arena catalog ID string, resolve to ASSET_ARENA entry, extract stagenum. */
	const char *arena_id = netbufReadStr(src);
	const asset_entry_t *stage_entry = arena_id ? assetCatalogResolve(arena_id) : NULL;
	const u8 stagenum = stage_entry ? (u8)stage_entry->ext.arena.stagenum : 0;
	const u8 status   = netbufReadU8(src);

	if (src->error) {
		return src->error;
	}
	if (gamemode != NETGAMEMODE_MP && gamemode != NETGAMEMODE_COOP &&
	    gamemode != NETGAMEMODE_ANTI) {
		sysLogPrintf(LOG_WARNING,
		             "NET: SVC_LOBBY_STATE invalid gamemode=%u",
		             (unsigned)gamemode);
		return 1;
	}
	if (status > 2) {
		sysLogPrintf(LOG_WARNING,
		             "NET: SVC_LOBBY_STATE invalid status=%u",
		             (unsigned)status);
		return 1;
	}

	sysLogPrintf(LOG_NOTE, "NET: SVC_LOBBY_STATE: mode=%u stage=%u status=%u",
	             gamemode, stagenum, status);

	g_NetGameMode = gamemode;
	g_Lobby.settings.scenario = gamemode;
	g_Lobby.settings.stagenum = stagenum;
	/* Phase 2: populate PRIMARY catalog ID string field */
	if (arena_id && arena_id[0]) {
		strncpy(g_Lobby.settings.stage_id, arena_id, sizeof(g_Lobby.settings.stage_id) - 1);
		g_Lobby.settings.stage_id[sizeof(g_Lobby.settings.stage_id) - 1] = '\0';
	} else {
		g_Lobby.settings.stage_id[0] = '\0';
	}
	g_Lobby.inGame = (status >= 2) ? 1 : 0;

	return src->error;
}

/* ============================================================
 * D3R-9: Network Distribution (protocol v20)
 * ============================================================ */

/* ---- Catalog entry collector (shared by SvcCatalogInfoWrite) ---- */

static const asset_type_e s_CatalogInfoTypes[] = {
	ASSET_MAP, ASSET_CHARACTER, ASSET_SKIN, ASSET_BOT_VARIANT,
	ASSET_WEAPON, ASSET_PROJECTILE, ASSET_ENTITY,
	ASSET_ARENA, ASSET_BODY, ASSET_HEAD, ASSET_MODEL,
	ASSET_ANIMATION,
	ASSET_TEXTURES, ASSET_TEXTURE, ASSET_MATERIAL, ASSET_EFFECT,
	ASSET_SFX, ASSET_MUSIC, ASSET_AUDIO,
	ASSET_PROP, ASSET_VEHICLE, ASSET_MISSION, ASSET_GAMEMODE,
	ASSET_BOT_PROFILE, ASSET_SCENARIO, ASSET_HUD, ASSET_UI,
	ASSET_FONT, ASSET_LANG, ASSET_THEME, ASSET_TOOL,
	ASSET_NONE  /* sentinel */
};

struct catalog_info_chunk_ctx {
	struct netbuf *dst;
	u16 start_offset;
	u16 written;
	u16 total_count;
	u8 stopped;
};

static u32 catalogInfoStringWireSize(const char *str)
{
	return 2u + (u32)strlen(str ? str : "") + 1u;
}

static void catalogInfoPatchU16(struct netbuf *dst, u32 pos, u16 value)
{
	if (!dst || !dst->data || pos + sizeof(value) > dst->size) {
		if (dst) {
			dst->error = 1;
		}
		return;
	}
	value = PD_LE16(value);
	memcpy(dst->data + pos, &value, sizeof(value));
}

static void catalogInfoWriteChunkCb(const asset_entry_t *e, void *ud)
{
	struct catalog_info_chunk_ctx *ctx = (struct catalog_info_chunk_ctx *)ud;
	/* Nested-only typed dependencies are transported by their owning archive.
	 * Advertising their VFS-chain rows independently makes the client request
	 * a component that has no standalone directory to package. */
	if (!ctx || !e || e->bundled || !e->enabled
			|| strstr(e->dirpath, "::") != NULL) {
		return;
	}

	if (ctx->total_count != 0xffffu) {
		ctx->total_count++;
	}

	if (ctx->total_count <= ctx->start_offset || ctx->stopped) {
		return;
	}

	const u32 need = catalogInfoStringWireSize(e->id)
	               + catalogInfoStringWireSize(e->category);
	if ((u32)netbufWriteLeft(ctx->dst) < need) {
		ctx->stopped = 1;
		return;
	}

	netbufWriteStr(ctx->dst, e->id);
	netbufWriteStr(ctx->dst, e->category);
	ctx->written++;
}

/* ---- SVC_CATALOG_INFO ---- */

u32 netmsgSvcCatalogInfoWriteChunk(struct netbuf *dst, u16 start_offset,
                                   u16 *next_offset, u16 *total_count)
{
	struct catalog_info_chunk_ctx ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.dst = dst;
	ctx.start_offset = start_offset;

	netbufWriteU8(dst, SVC_CATALOG_INFO);
	const u32 total_pos = dst->wp;
	netbufWriteU16(dst, 0);
	netbufWriteU16(dst, start_offset);
	const u32 count_pos = dst->wp;
	netbufWriteU16(dst, 0);

	for (s32 ti = 0; s_CatalogInfoTypes[ti] != ASSET_NONE; ti++) {
		assetCatalogIterateByType(s_CatalogInfoTypes[ti],
		                          catalogInfoWriteChunkCb, &ctx);
	}

	catalogInfoPatchU16(dst, total_pos, ctx.total_count);
	catalogInfoPatchU16(dst, count_pos, ctx.written);

	if (next_offset) {
		*next_offset = (u16)(start_offset + ctx.written);
	}
	if (total_count) {
		*total_count = ctx.total_count;
	}

	if (ctx.stopped && ctx.written == 0 && start_offset < ctx.total_count) {
		sysLogPrintf(LOG_WARNING,
		             "NET: SVC_CATALOG_INFO could not fit one catalog entry "
		             "at offset %u", (unsigned)start_offset);
		dst->error = 1;
	}

	return dst->error;
}

u32 netmsgSvcCatalogInfoWrite(struct netbuf *dst)
{
	return netmsgSvcCatalogInfoWriteChunk(dst, 0, NULL, NULL);
}

u32 netmsgSvcCatalogInfoRead(struct netbuf *src, struct netclient *srccl)
{
	(void)srccl;
	u16 total_count = netbufReadU16(src);
	u16 batch_offset = netbufReadU16(src);
	u16 count = netbufReadU16(src);
	if (count > 0 && (u32)count > (u32)netbufReadLeft(src) / 4u) {
		sysLogPrintf(LOG_WARNING,
		             "NET: SVC_CATALOG_INFO count %u exceeds packet capacity",
		             count);
		return 1;
	}

	/* v27: no net_hash on wire — collect catalog ID strings only. */
	char (*ids)[64] = NULL;
	char (*cats)[64] = NULL;
	if (count > 0) {
		ids = (char (*)[64])calloc(count, sizeof(*ids));
		cats = (char (*)[64])calloc(count, sizeof(*cats));
		if (!ids || !cats) {
			free(ids);
			free(cats);
			sysLogPrintf(LOG_WARNING,
			             "NET: SVC_CATALOG_INFO OOM for %u entries", count);
			return 1;
		}
	}

	for (u16 i = 0; i < count; i++) {
		const char *id  = netbufReadStr(src);
		const char *cat = netbufReadStr(src);
		strncpy(ids[i],  id  ? id  : "", 63);  ids[i][63]  = '\0';
		strncpy(cats[i], cat ? cat : "", 63);  cats[i][63] = '\0';
	}

	if (src->error) {
		free(ids);
		free(cats);
		return src->error;
	}

	netDistribClientHandleCatalogInfo((const char (*)[64])ids,
	                                  (const char (*)[64])cats,
	                                  count, batch_offset, total_count);
	free(ids);
	free(cats);
	return src->error;
}

/* ---- CLC_CATALOG_DIFF ---- */

u32 netmsgClcCatalogDiffWrite(struct netbuf *dst, const char (*missing_ids)[CATALOG_ID_LEN],
                               u16 count, u8 temporary)
{
	netbufWriteU8(dst, CLC_CATALOG_DIFF);
	netbufWriteU8(dst, temporary);
	netbufWriteU16(dst, count);
	/* v27: catalog ID strings only — no u32 net_hash on wire. */
	for (u16 i = 0; i < count; i++) {
		netbufWriteStr(dst, missing_ids ? missing_ids[i] : "");
	}
	return dst->error;
}

u32 netmsgClcCatalogDiffRead(struct netbuf *src, struct netclient *srccl)
{
	u8  temporary = netbufReadU8(src);
	u16 count     = netbufReadU16(src);
	if (count > 0 && (u32)count > (u32)netbufReadLeft(src) / 2u) {
		sysLogPrintf(LOG_WARNING,
		             "NET: CLC_CATALOG_DIFF count %u exceeds packet capacity",
		             count);
		return 1;
	}

	/* v27: catalog ID strings only — no u32 net_hash on wire. */
	char (*missing_ids)[CATALOG_ID_LEN] = NULL;
	if (count > 0) {
		missing_ids = (char (*)[CATALOG_ID_LEN])calloc(count, sizeof(*missing_ids));
		if (!missing_ids) {
			sysLogPrintf(LOG_WARNING,
			             "NET: CLC_CATALOG_DIFF OOM for %u entries", count);
			return 1;
		}
	}
	for (u16 i = 0; i < count; i++) {
		const char *id = netbufReadStr(src);
		strncpy(missing_ids[i], id ? id : "", CATALOG_ID_LEN - 1);
		missing_ids[i][CATALOG_ID_LEN - 1] = '\0';
	}

	if (src->error) {
		free(missing_ids);
		return src->error;
	}

	netDistribServerHandleDiff(srccl, (const char (*)[CATALOG_ID_LEN])missing_ids, count, temporary);
	free(missing_ids);
	return src->error;
}

/* ---- SVC_DISTRIB_BEGIN ---- */

u32 netmsgSvcDistribBeginWrite(struct netbuf *dst, const char *catalog_id,
                                const char *category, u32 total_chunks,
                                u32 archive_bytes, const u8 expected_sha256[32])
{
	netbufWriteU8(dst, SVC_DISTRIB_BEGIN);
	/* v27: catalog ID string is the component identity — no net_hash on wire. */
	netbufWriteStr(dst, catalog_id);
	netbufWriteStr(dst, category);
	netbufWriteU32(dst, total_chunks);
	netbufWriteU32(dst, archive_bytes);
	/* v46 / SEC-5: mandatory digest of the compressed PDCA archive bytes. */
	netbufWriteData(dst, expected_sha256, 32);
	return dst->error;
}

u32 netmsgSvcDistribBeginRead(struct netbuf *src, struct netclient *srccl)
{
	(void)srccl;
	/* v27: catalog ID string only — no u32 net_hash. */
	const char *catalog_id  = netbufReadStr(src);
	const char *category    = netbufReadStr(src);
	u32  total_chunks = netbufReadU32(src);
	u32  archive_bytes = netbufReadU32(src);
	u8   expected_sha256[32];
	netbufReadData(src, expected_sha256, sizeof(expected_sha256));

	if (src->error) return src->error;

	if (!catalog_id || !catalog_id[0]) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_DISTRIB_BEGIN: empty catalog_id");
		return 1;
	}

	netDistribClientHandleBegin(catalog_id,
	                             category ? category : "",
	                             total_chunks, archive_bytes,
	                             expected_sha256,
	                             netDistribClientGetTransferTemporary());
	return src->error;
}

/* ---- SVC_DISTRIB_CHUNK ---- */
/*
 * Note: large chunks are sent as direct ENet packets where this function's
 * format also applies. The first byte is SVC_DISTRIB_CHUNK, followed by
 * the fields below. The read handler reads directly from the packet data
 * without copying to avoid large stack allocations.
 */

u32 netmsgSvcDistribChunkWrite(struct netbuf *dst, const char *catalog_id, u16 chunk_idx,
                                u8 compression, const u8 *data, u16 data_len)
{
	netbufWriteU8(dst, SVC_DISTRIB_CHUNK);
	/* v27: catalog ID string identifies the transfer — no net_hash on wire. */
	netbufWriteStr(dst, catalog_id);
	netbufWriteU16(dst, chunk_idx);
	netbufWriteU8(dst, compression);
	netbufWriteU16(dst, data_len);
	netbufWriteData(dst, data, (u32)data_len);
	return dst->error;
}

u32 netmsgSvcDistribChunkRead(struct netbuf *src, struct netclient *srccl)
{
	(void)srccl;
	/* v27: catalog ID string only — no u32 net_hash. */
	const char *catalog_id = netbufReadStr(src);
	u16  chunk_idx   = netbufReadU16(src);
	u8   compression = netbufReadU8(src);
	u16  data_len    = netbufReadU16(src);

	/* Sanity bound: compressed data can't exceed 2× chunk size */
	if (data_len > NET_DISTRIB_CHUNK_SIZE * 2) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_DISTRIB_CHUNK data_len %u too large", data_len);
		return 1;
	}

	/* Point directly into the packet buffer — valid for the duration of this handler */
	const u8 *data = (const u8 *)(src->data + src->rp);
	netbufReadSkip(src, (u32)data_len);

	if (src->error) return src->error;

	if (!catalog_id || !catalog_id[0]) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_DISTRIB_CHUNK: empty catalog_id");
		return 1;
	}

	netDistribClientHandleChunk(catalog_id, chunk_idx, compression, data, data_len);
	return src->error;
}

/* ---- SVC_DISTRIB_END ---- */

u32 netmsgSvcDistribEndWrite(struct netbuf *dst, const char *catalog_id, u8 success)
{
	netbufWriteU8(dst, SVC_DISTRIB_END);
	/* v27: catalog ID string identifies the transfer — no net_hash on wire. */
	netbufWriteStr(dst, catalog_id);
	netbufWriteU8(dst, success);
	return dst->error;
}

u32 netmsgSvcDistribEndRead(struct netbuf *src, struct netclient *srccl)
{
	(void)srccl;
	/* v27: catalog ID string only — no u32 net_hash. */
	const char *catalog_id = netbufReadStr(src);
	u8    success    = netbufReadU8(src);

	if (src->error) return src->error;

	if (!catalog_id || !catalog_id[0]) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_DISTRIB_END: empty catalog_id");
		return 1;
	}

	netDistribClientHandleEnd(catalog_id, success);
	return src->error;
}

/* ---- SVC_LOBBY_KILL_FEED ---- */

u32 netmsgSvcLobbyKillFeedWrite(struct netbuf *dst, const char *attacker,
                                 const char *victim, const char *weapon, u8 flags)
{
	netbufWriteU8(dst, SVC_LOBBY_KILL_FEED);
	netbufWriteStr(dst, attacker ? attacker : "");
	netbufWriteStr(dst, victim   ? victim   : "");
	netbufWriteStr(dst, weapon   ? weapon   : "");
	netbufWriteU8(dst, flags);
	return dst->error;
}

u32 netmsgSvcLobbyKillFeedRead(struct netbuf *src, struct netclient *srccl)
{
	(void)srccl;
	const char *attacker = netbufReadStr(src);
	const char *victim   = netbufReadStr(src);
	const char *weapon   = netbufReadStr(src);
	u8    flags    = netbufReadU8(src);

	if (src->error) return src->error;

	const char *aname = attacker ? attacker : "";
	const char *vname = victim   ? victim   : "";

	netDistribClientHandleKillFeed(aname, vname, weapon ? weapon : "", flags);

#if !defined(PD_SERVER)
	/* Also push to in-game HUD killfeed (displayed during CLSTATE_GAME).
	 * Server-side kills now broadcast SVC_LOBBY_KILL_FEED to all clients
	 * (spectators and players), so in-game clients see all kills including
	 * bot-on-bot and bot-on-player. Look up team colours from local chr config. */
	{
		s32 isSuicide = (!aname[0] || strcmp(aname, vname) == 0);
		u8 ateam = 0, vteam = 0;
		for (s32 i = 0; i < g_MpNumChrs; i++) {
			if (g_MpAllChrConfigPtrs[i]) {
				if (!isSuicide && aname[0] &&
				    strcmp(g_MpAllChrConfigPtrs[i]->name, aname) == 0) {
					ateam = (u8)g_MpAllChrConfigPtrs[i]->team;
				}
				if (vname[0] &&
				    strcmp(g_MpAllChrConfigPtrs[i]->name, vname) == 0) {
					vteam = (u8)g_MpAllChrConfigPtrs[i]->team;
				}
			}
		}
		pdguiKillfeedPush(isSuicide ? NULL : aname, ateam, vname, vteam, isSuicide);
	}
#endif

	return src->error;
}

/* ========================================================================
 * Phase A: Match Startup Pipeline (protocol v24)
 *
 * SVC_MATCH_MANIFEST  (0x62) — server → room, complete asset manifest
 * CLC_MANIFEST_STATUS (0x0E) — client → server, catalog check result
 * SVC_MATCH_COUNTDOWN (0x63) — server → room, ready-gate progress
 *
 * Wire format: see port/include/net/netmanifest.h
 *
 * Phase A scope: message types, structs, and read/write functions defined
 * here.  Neither write nor read function is wired into any dispatch path
 * yet — that happens in Phase B (server-side manifest build + send) and
 * Phase C (client-side manifest check handler).
 * ======================================================================== */

/* ---- SVC_MATCH_MANIFEST ---- */

/* Phase D.4: SVC_MATCH_MANIFEST now uses manifestSerialize/Deserialize (protocol 26+).
 * Per-entry format: u8 type, u8 slot_index, str id,
 *                   [u8[32] sha256] only for MANIFEST_TYPE_COMPONENT entries.
 * The manifest_hash header is retained for integrity checking on the client. */

u32 netmsgSvcMatchManifestWrite(struct netbuf *dst, const match_manifest_t *manifest)
{
	netbufWriteU8(dst, SVC_MATCH_MANIFEST);
	netbufWriteU32(dst, manifest->manifest_hash);
	manifestSerialize(dst, manifest);
	return dst->error;
}

u32 netmsgSvcMatchManifestRead(struct netbuf *src, struct netclient *srccl)
{
	(void)srccl;

	const u32 manifest_hash = netbufReadU32(src);
	if (src->error) {
		sysLogPrintf(LOG_WARNING, "NET: malformed SVC_MATCH_MANIFEST header");
		return 1;
	}

	/* Phase D.4: use manifestDeserialize (handles sha256 for COMPONENT entries) */
	manifestClear(&g_ClientManifest);
	g_ClientManifest.manifest_hash = manifest_hash;

	if (manifestDeserialize(src, &g_ClientManifest) != 0 || src->error) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_MATCH_MANIFEST parse error");
		manifestClear(&g_ClientManifest);
		return 1;
	}

	sysLogPrintf(LOG_NOTE, "NET: SVC_MATCH_MANIFEST hash=0x%08x entries=%u",
	             (unsigned)manifest_hash, (unsigned)g_ClientManifest.num_entries);

	/* Phase C: check local catalog against manifest, send CLC_MANIFEST_STATUS */
	manifestCheck(&g_ClientManifest);

	/* Local client is now in match-start prep (manifest check/transfer/countdown). */
	if (g_NetMode == NETMODE_CLIENT && g_NetLocalClient) {
		g_NetLocalClient->state = CLSTATE_PREPARING;
	}

	return src->error;
}

/* ---- CLC_MANIFEST_STATUS ---- */

u32 netmsgClcManifestStatusWrite(struct netbuf *dst, u32 manifest_hash, u8 status,
                                  const char (*missing_ids)[CATALOG_ID_LEN], u16 num_missing)
{
	netbufWriteU8(dst, CLC_MANIFEST_STATUS);
	netbufWriteU32(dst, manifest_hash);
	netbufWriteU8(dst, status);
	netbufWriteU16(dst, num_missing);

	/* v27: catalog ID strings only — no u32 net_hash on wire. */
	for (u16 i = 0; i < num_missing; i++) {
		netbufWriteStr(dst, missing_ids ? missing_ids[i] : "");
	}

	return dst->error;
}

static u32 netmsgReconnectManifestReady(struct netclient *srccl)
{
	struct netpreservedplayer *pp;
	struct netbuf stage_wire;
	u8 *stage_data;
	enum net_restore_result restore_result;
	u32 stage_len;

	if (!srccl || !srccl->reconnect_settings_pending
			|| srccl->reconnect_preserved_index >= NET_MAX_CLIENTS) {
		return 1;
	}
	if (srccl->reconnect_resync_pending) {
		sysLogPrintf(LOG_NOTE,
			"NET.RECONNECT.MANIFEST client=%u status=duplicate_ready stage_queued=1 ignored=1",
			(unsigned)srccl->id);
		return 0;
	}
	pp = &g_NetPreservedPlayers[srccl->reconnect_preserved_index];

	stage_data = (u8 *)malloc(NET_BUFSIZE);
	if (!stage_data) {
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.ROLLBACK reconnect status=stage_wire_oom client=%u globals_published=0",
			(unsigned)srccl->id);
		netServerKick(srccl, DISCONNECT_TIMEOUT);
		return 1;
	}
	memset(&stage_wire, 0, sizeof(stage_wire));
	stage_wire.data = stage_data;
	stage_wire.size = NET_BUFSIZE;
	restore_result = netServerRestorePreserved(srccl, pp,
		&srccl->reconnect_settings_plan,
		srccl->reconnect_sanitized_team, &stage_wire, &stage_len);
	if (restore_result != NET_RESTORE_OK) {
		free(stage_data);
		sysLogPrintf(LOG_ERROR,
			"NET: reconnect restore rejected for client %u status=%s",
			(unsigned)srccl->id,
			netRestoreResultString(restore_result));
		netServerKick(srccl,
			netRestoreResultRequiresRetryableClose(restore_result)
				? DISCONNECT_TIMEOUT : DISCONNECT_FILES);
		return 1;
	}
	free(stage_data);

	sysLogPrintf(LOG_NOTE,
		"NET.RECONNECT.MANIFEST client=%u status=stage_queued stage_bytes=%u reservation_retained=1 commit=pending_post_load",
		(unsigned)srccl->id, (unsigned)stage_len);
	return 0;
}

u32 netmsgClcManifestStatusRead(struct netbuf *src, struct netclient *srccl)
{
	s32 gate_client_index = -1;
	const u32 manifest_hash = netbufReadU32(src);
	const u8  status        = netbufReadU8(src);
	const u16 num_missing   = netbufReadU16(src);

	if (src->error) {
		sysLogPrintf(LOG_WARNING, "NET: malformed CLC_MANIFEST_STATUS header");
		return 1;
	}
	if (status > MANIFEST_STATUS_DECLINE) {
		sysLogPrintf(LOG_WARNING, "NET: malformed CLC_MANIFEST_STATUS status %u from client %u",
			(unsigned)status, srccl ? srccl->id : 0xFF);
		return 1;
	}
	if (s_ReadyGate.active
			&& !(srccl && srccl->reconnect_settings_pending)) {
		gate_client_index = srccl ? (s32)(srccl - g_NetClients) : -1;
		if (gate_client_index < 0 || gate_client_index >= NET_MAX_CLIENTS
				|| !(s_ReadyGate.expected_mask & (1u << gate_client_index))
				|| srccl->room_id != s_ReadyGate.room_id
				|| (srccl->state != CLSTATE_PREPARING
					&& !(s_ReadyGate.declined_mask
						& (1u << gate_client_index)))) {
			sysLogPrintf(LOG_WARNING,
				"NET: CLC_MANIFEST_STATUS rejected outside exact gate roster");
			return 1;
		}
	}
	if (s_ReadyGate.active
			&& !(srccl && srccl->reconnect_settings_pending)
			&& manifest_hash != g_ServerManifest.manifest_hash) {
		sysLogPrintf(LOG_WARNING,
			"NET: CLC_MANIFEST_STATUS hash mismatch from client %u (got 0x%08x expected 0x%08x)",
			srccl ? srccl->id : 0xFF,
			(unsigned)manifest_hash,
			(unsigned)g_ServerManifest.manifest_hash);
		return 1;
	}

	sysLogPrintf(LOG_NOTE, "NET: CLC_MANIFEST_STATUS from client %u: hash=0x%08x status=%u missing=%u",
	             srccl ? srccl->id : 0xFF,
	             (unsigned)manifest_hash, (unsigned)status, (unsigned)num_missing);

	/* v27: catalog ID strings only — no u32 net_hash on wire. */
	char (*missing_ids)[CATALOG_ID_LEN] = NULL;
	u16 missing_count = 0;
	if (num_missing > 0) {
		missing_ids = (char (*)[CATALOG_ID_LEN])calloc(num_missing,
			sizeof(*missing_ids));
		if (!missing_ids) {
			sysLogPrintf(LOG_WARNING,
			             "NET: CLC_MANIFEST_STATUS OOM for %u missing ids",
			             (unsigned)num_missing);
			return 1;
		}
	}
	for (s32 mi = 0; mi < (s32)num_missing; mi++) {
		const char *id = netbufReadStr(src);
		if (src->error) {
			free(missing_ids);
			sysLogPrintf(LOG_WARNING, "NET: CLC_MANIFEST_STATUS malformed at missing[%d]", mi);
			return 1;
		}
		sysLogPrintf(LOG_NOTE, "NET: MANIFEST MISSING[%d] id=%s", mi, id ? id : "(null)");
		strncpy(missing_ids[missing_count], id ? id : "", CATALOG_ID_LEN - 1);
		missing_ids[missing_count][CATALOG_ID_LEN - 1] = '\0';
		missing_count++;
	}

	/* v57 reconnect has its own one-client manifest gate. It deliberately
	 * does not join the mutable lobby ready-gate roster: the live match is
	 * already frozen and only this preserved stable slot may advance. */
	if (srccl && srccl->reconnect_settings_pending) {
		if (srccl->state != CLSTATE_PREPARING
				|| srccl->reconnect_preserved_index >= NET_MAX_CLIENTS
				|| manifest_hash != srccl->reconnect_manifest_hash
				|| manifest_hash != g_ServerManifest.manifest_hash) {
			free(missing_ids);
			sysLogPrintf(LOG_ERROR,
				"NET.RECONNECT.MANIFEST client=%u status=rejected hash=0x%08x expected=0x%08x phase=%u",
				(unsigned)srccl->id, (unsigned)manifest_hash,
				(unsigned)srccl->reconnect_manifest_hash,
				(unsigned)srccl->reconnect_manifest_phase);
			netServerKick(srccl, DISCONNECT_FILES);
			return 1;
		}

		if (status == MANIFEST_STATUS_READY) {
			if (missing_count != 0) {
				free(missing_ids);
				netServerKick(srccl, DISCONNECT_FILES);
				return 1;
			}
			free(missing_ids);
			return netmsgReconnectManifestReady(srccl);
		}

		if (status == MANIFEST_STATUS_NEED_ASSETS) {
			if (missing_count == 0) {
				free(missing_ids);
				netServerKick(srccl, DISCONNECT_FILES);
				return 1;
			}
			if (srccl->reconnect_manifest_phase
					== NET_RECONNECT_MANIFEST_WAITING) {
				srccl->reconnect_manifest_phase =
					NET_RECONNECT_MANIFEST_TRANSFERRING;
				netDistribServerHandleManifestDiff(srccl,
					(const char (*)[CATALOG_ID_LEN])missing_ids,
					(u16)missing_count, 1);
				sysLogPrintf(LOG_NOTE,
					"NET.RECONNECT.MANIFEST client=%u status=transferring missing=%u",
					(unsigned)srccl->id, (unsigned)missing_count);
			} else {
				sysLogPrintf(LOG_NOTE,
					"NET.RECONNECT.MANIFEST client=%u status=duplicate_need_assets ignored=1",
					(unsigned)srccl->id);
			}
			free(missing_ids);
			return 0;
		}

		free(missing_ids);
		sysLogPrintf(LOG_NOTE,
			"NET.RECONNECT.MANIFEST client=%u status=declined",
			(unsigned)srccl->id);
		netServerKick(srccl, DISCONNECT_FILES);
		return 0;
	}

	/* Phase E: ready gate — update per-client readiness and check for completion.
	 * Dispatch on status: READY marks done, NEED_ASSETS queues Phase D transfer
	 * (client will send READY again after transfer), DECLINE removes from match. */
	if (s_ReadyGate.active && srccl) {
		s32 ci = gate_client_index;
		if (ci >= 0 && ci < NET_MAX_CLIENTS &&
		    (s_ReadyGate.expected_mask & (1u << ci))) {
			const u32 bit = 1u << ci;

			if (status == MANIFEST_STATUS_READY) {
				if (s_ReadyGate.declined_mask & bit) {
					free(missing_ids);
					return 1;
				}
				s_ReadyGate.ready_mask |= bit;
				sysLogPrintf(LOG_NOTE, "NET: ready gate: client %d READY (%u/%u)",
				             ci, (unsigned)readyGatePopcount(s_ReadyGate.ready_mask),
				             (unsigned)s_ReadyGate.total_count);
				readyGateBroadcastCountdown(MANIFEST_PHASE_CHECKING);

			} else if (status == MANIFEST_STATUS_NEED_ASSETS) {
				if ((s_ReadyGate.ready_mask | s_ReadyGate.declined_mask) & bit
						|| missing_count == 0) {
					free(missing_ids);
					return 1;
				}
				/* Phase D: queue missing components; client re-sends READY when done */
				if (missing_count > 0) {
					sysLogPrintf(LOG_NOTE,
					             "NET: ready gate: client %d NEED_ASSETS (%u missing), queuing transfer",
					             ci, (unsigned)missing_count);
					netDistribServerHandleManifestDiff(srccl,
					                           (const char (*)[CATALOG_ID_LEN])missing_ids,
					                           (u16)missing_count, 1);
				}
				readyGateBroadcastCountdown(MANIFEST_PHASE_TRANSFERRING);

			} else if (status == MANIFEST_STATUS_DECLINE) {
				if (s_ReadyGate.ready_mask & bit) {
					free(missing_ids);
					return 1;
				}
				s_ReadyGate.declined_mask |= bit;
				srccl->state = CLSTATE_LOBBY;  /* spectator — excluded from match */
				sysLogPrintf(LOG_NOTE, "NET: ready gate: client %d DECLINED (spectate)", ci);
				readyGateBroadcastCountdown(MANIFEST_PHASE_CHECKING);
			}

			readyGateCheck();
		}
	}

	free(missing_ids);
	return src->error;
}

/* ---- SVC_MATCH_COUNTDOWN ---- */

u32 netmsgSvcMatchCountdownWrite(struct netbuf *dst, u8 ready_count, u8 total_count,
                                  u8 phase, u8 countdown_secs)
{
	netbufWriteU8(dst, SVC_MATCH_COUNTDOWN);
	netbufWriteU8(dst, ready_count);
	netbufWriteU8(dst, total_count);
	netbufWriteU8(dst, phase);
	netbufWriteU8(dst, countdown_secs);
	return dst->error;
}

u32 netmsgSvcMatchCountdownRead(struct netbuf *src, struct netclient *srccl)
{
	(void)srccl;

	const u8 ready_count    = netbufReadU8(src);
	const u8 total_count    = netbufReadU8(src);
	const u8 phase          = netbufReadU8(src);
	const u8 countdown_secs = netbufReadU8(src);

	if (src->error) {
		sysLogPrintf(LOG_WARNING, "NET: malformed SVC_MATCH_COUNTDOWN");
		return 1;
	}

	sysLogPrintf(LOG_NOTE, "NET: SVC_MATCH_COUNTDOWN %u/%u ready phase=%u countdown=%us",
	             (unsigned)ready_count, (unsigned)total_count,
	             (unsigned)phase, (unsigned)countdown_secs);

	/* Phase F: store countdown state so the room UI can display it. */
	g_MatchCountdownState.ready_count    = ready_count;
	g_MatchCountdownState.total_count    = total_count;
	g_MatchCountdownState.phase          = phase;
	g_MatchCountdownState.countdown_secs = countdown_secs;
	g_MatchCountdownState.active         = 1;

	return src->error;
}

/* ---- SVC_SESSION_CATALOG (SA-1) ---- */
u32 netmsgSvcSessionCatalogRead(struct netbuf *src, struct netclient *srccl)
{
	(void)srccl;
	return sessionCatalogReceive(src);
}

/* ---- CLC_LOBBY_CANCEL ---- */

u32 netmsgClcLobbyCancelWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, CLC_LOBBY_CANCEL);
	return dst->error;
}

u32 netmsgClcLobbyCancelRead(struct netbuf *src, struct netclient *srccl)
{
	if (g_NetMode != NETMODE_SERVER) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_LOBBY_CANCEL received but not server");
		return src->error;
	}

	if (!srccl) {
		return src->error;
	}

	if (netReadyGateCancelByLocalClient(srccl) != 0) {
		sysLogPrintf(LOG_NOTE, "NET: CLC_LOBBY_CANCEL from '%s' ignored (gate/state mismatch)",
		             srccl->settings.name[0] ? srccl->settings.name : "?");
		return src->error;
	}

	return src->error;
}

/* ---- SVC_MATCH_CANCELLED ---- */

u32 netmsgSvcMatchCancelledWrite(struct netbuf *dst, const char *canceller_name)
{
	netbufWriteU8(dst, SVC_MATCH_CANCELLED);
	netbufWriteStr(dst, canceller_name ? canceller_name : "");
	return dst->error;
}

u32 netmsgSvcMatchCancelledRead(struct netbuf *src, struct netclient *srccl)
{
	const char *name;

	(void)srccl;

	name = netbufReadStr(src);

	if (src->error) {
		sysLogPrintf(LOG_WARNING, "NET: malformed SVC_MATCH_CANCELLED");
		return 1;
	}

	sysLogPrintf(LOG_NOTE, "NET: SVC_MATCH_CANCELLED — cancelled by '%s'",
	             name ? name : "?");

	/* Clear the countdown so the overlay goes away */
	memset(&g_MatchCountdownState, 0, sizeof(g_MatchCountdownState));

	/* GAP-10: explicit canonical reset call mirrors the B-139 pattern used in
	 * pdgui_lobby.cpp on mode→NONE transition. The memset above already
	 * zeros the state, but routing through pdguiCountdownReset() keeps the
	 * "countdown cleared" site grep-able and future-proofs against the
	 * reset function gaining side-effects (UI notification, sound, etc.). */
	{
		extern void pdguiCountdownReset(void);
		pdguiCountdownReset();
	}

	/* Record who cancelled so the UI can display the message */
	g_MatchCancelledState.active = 1;
	if (name) {
		strncpy(g_MatchCancelledState.name, name, MATCH_CANCEL_NAME_LEN - 1);
		g_MatchCancelledState.name[MATCH_CANCEL_NAME_LEN - 1] = '\0';
	} else {
		g_MatchCancelledState.name[0] = '\0';
	}

	if (g_NetMode == NETMODE_CLIENT && g_NetLocalClient) {
		g_NetLocalClient->state = CLSTATE_LOBBY;
		manifestFree(&g_ClientManifest);
		sessionCatalogTeardown();
	}

	return src->error;
}

/* ========================================================================
 * SVC_BOT_AUTHORITY — server→designated client
 * Tells the first connected client (slot 0 in a dedicated server game) that
 * it is the bot AI authority: it should run full bot AI each frame and send
 * CLC_BOT_MOVE updates back to the server for relay to all other clients.
 *
 * This message has no payload — it is a capability grant, not a data update.
 * Only sent in dedicated-server mode (g_NetDedicated); in listen-server mode
 * the server host runs bot AI directly and no authority designation is needed.
 * ======================================================================== */

u32 netmsgSvcBotAuthorityWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, SVC_BOT_AUTHORITY);
	return dst->error;
}

u32 netmsgSvcBotAuthorityRead(struct netbuf *src, struct netclient *srccl)
{
	(void)srccl;

	if (src->error) {
		return src->error;
	}

	g_NetPendingBotAuthority = true;
	sysLogPrintf(LOG_NOTE, "NET: SVC_BOT_AUTHORITY received — deferred until stage load complete");
	return src->error;
}

/* ========================================================================
 * CLC_BOT_MOVE — bot-authority client→server
 * Sent each update frame by the designated authority client for each active
 * bot.  Carries: aibotnum (server stub index), syncid (so the server can
 * write valid prop identifiers in its SVC_CHR_MOVE relay), position, facing
 * angle, rooms, animation speed multipliers, and action state.
 *
 * The server's netmsgClcBotMoveRead() updates minimal stub fields and the
 * existing SVC_CHR_MOVE broadcast loop (net.c:netEndFrame) relays the
 * updated positions to all clients.
 *
 * No asset references (body/head/weapon) are transmitted — position/animation
 * state only.  Asset catalog compliance: fully met.
 * ======================================================================== */

u32 netmsgClcBotMoveWrite(struct netbuf *dst)
{
	/* Count bots with valid data */
	u8 count = 0;
	for (s32 i = 0; i < g_BotCount; i++) {
		struct chrdata *chr = g_MpBotChrPtrs[i];
		if (chr && chr->prop && chr->aibot) {
			count++;
		}
	}

	if (!count) {
		return dst->error;
	}

	netbufWriteU8(dst, CLC_BOT_MOVE);
	netbufWriteU8(dst, count);

	if (g_Vars.lvframe60 < 600 && (g_Vars.lvframe60 % 60) == 0) {
		sysLogPrintf(LOG_NOTE, "MATCH-TRACE: CLC_BOT_MOVE write count=%d frame=%d", (s32)count, g_Vars.lvframe60);
	}

	for (s32 i = 0; i < g_BotCount; i++) {
		struct chrdata *chr = g_MpBotChrPtrs[i];
		if (!chr || !chr->prop || !chr->aibot) {
			continue;
		}
		struct prop *prop = chr->prop;
		struct aibot *aibot = chr->aibot;

		netbufWriteU8(dst, (u8)i);                          /* aibotnum: server stub index */
		netbufWriteU32(dst, prop->syncid);                  /* syncid: needed by server for SVC_CHR_MOVE relay */
		netbufWriteCoord(dst, &prop->pos);
		netbufWriteF32(dst, chrGetInverseTheta(chr));       /* yaw heading */
		netbufWriteRooms(dst, prop->rooms, ARRAYCOUNT(prop->rooms));
		netbufWriteF32(dst, aibot->speedmultforwards);
		netbufWriteF32(dst, aibot->speedmultsideways);
		netbufWriteF32(dst, aibot->speedtheta);
		netbufWriteS8(dst, (s8)chr->myaction);
		netbufWriteU8(dst, (u8)chr->actiontype);

		/* v47 (S594h-B Slice 3): surface_up vec3 from authority client.
		 * Server stub stores it on chr->surface_up so SVC_CHR_MOVE
		 * relays the same value to all other clients. */
		netbufWriteF32(dst, chr->surface_up[0]);
		netbufWriteF32(dst, chr->surface_up[1]);
		netbufWriteF32(dst, chr->surface_up[2]);
	}

	return dst->error;
}

u32 netmsgClcBotMoveRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 count = netbufReadU8(src);
	if (src->error) {
		return src->error;
	}
	if (count > MAX_BOTS || count > g_BotCount) {
		sysLogPrintf(LOG_WARNING, "NET: malformed CLC_BOT_MOVE count %u (bots=%d max=%d)",
			(unsigned)count, (int)g_BotCount, (int)MAX_BOTS);
		return 1;
	}

	const bool authorized = srccl
		&& srccl->state == CLSTATE_GAME
		&& g_NetBotAuthorityClientId != NET_NULL_CLIENT
		&& srccl->id == g_NetBotAuthorityClientId;

	if (g_NetTick < 600 && (g_NetTick % 60) == 0) {
		sysLogPrintf(LOG_NOTE, "MATCH-TRACE: CLC_BOT_MOVE server recv count=%d tick=%d",
			(s32)count, g_NetTick);
	}

	for (u8 j = 0; j < count; j++) {
		const u8 aibotnum = netbufReadU8(src);
		const u32 syncid  = netbufReadU32(src);
		struct coord pos;
		netbufReadCoord(src, &pos);
		const f32 angle = netbufReadF32(src);
		s16 rooms[8];
		netbufReadRooms(src, rooms, ARRAYCOUNT(rooms));
		const f32 speedmultfwd  = netbufReadF32(src);
		const f32 speedmultside = netbufReadF32(src);
		const f32 speedtheta    = netbufReadF32(src);
		const s8  myaction      = netbufReadS8(src);
		const u8  actiontype    = netbufReadU8(src);
		/* v47 (S594h-B Slice 3): surface_up vec3 from authority. */
		const f32 newsurface_x  = netbufReadF32(src);
		const f32 newsurface_y  = netbufReadF32(src);
		const f32 newsurface_z  = netbufReadF32(src);

		if (src->error) {
			return src->error;
		}

		if (!authorized) {
			continue;
		}

		if (aibotnum >= g_BotCount || !g_MpBotChrPtrs[aibotnum]) {
			continue;
		}

		struct chrdata *chr = g_MpBotChrPtrs[aibotnum];
		if (!chr->prop || !chr->aibot) {
			continue;
		}

		struct prop  *prop  = chr->prop;
		struct aibot *aibot = chr->aibot;

		/* Update stub with authority data so the SVC_CHR_MOVE relay has valid fields */
		prop->syncid = syncid;
		prop->pos    = pos;

		chr->myaction   = myaction;
		chr->actiontype = actiontype;

		aibot->speedmultforwards  = speedmultfwd;
		aibot->speedmultsideways  = speedmultside;
		aibot->speedtheta         = speedtheta;

		/* v47 surface_up cache so SVC_CHR_MOVE relay carries it. */
		chr->surface_up[0] = newsurface_x;
		chr->surface_up[1] = newsurface_y;
		chr->surface_up[2] = newsurface_z;

		/* Rooms: copy until terminator */
		for (s32 ri = 0; ri < 8; ri++) {
			prop->rooms[ri] = rooms[ri];
			if (rooms[ri] < 0) {
				break;
			}
		}

		/* Store the angle — chrGetInverseTheta() is a stub that returns 0.0f on the
		 * server, so we cache it directly into aibot for use if ever needed. */
		aibot->roty = angle;
	}

	return src->error;
}

/* ========================================================================
 * R-3: Room networking (protocol v29)
 * ======================================================================== */

u32 netmsgSvcRoomListWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, SVC_ROOM_LIST);

	s32 count = roomGetActiveCount();
	netbufWriteU8(dst, (u8)count);

	for (s32 i = 0; i < count; i++) {
		hub_room_t *room = roomGetByIndex(i);
		if (!room) break;
		netbufWriteU8(dst, room->id);
		netbufWriteU8(dst, (u8)room->state);
		netbufWriteStr(dst, room->name);
		/* A timed-out member still owns exact room capacity until reconnect,
		 * expiry, or stage teardown. Advertise occupied capacity so the lobby
		 * cannot present a slot that ordinary roomJoin will correctly reject. */
		netbufWriteU8(dst, roomOccupiedCount(room));
		netbufWriteU8(dst, room->max_players);
		netbufWriteU8(dst, room->creator_client_id);
	}

	return dst->error;
}

u32 netmsgSvcRoomListRead(struct netbuf *src, struct netclient *srccl)
{
	u8 count = netbufReadU8(src);
	if (src->error) return src->error;

	g_RoomCacheCount = 0;

	for (u8 i = 0; i < count && i < ROOM_CACHE_MAX; i++) {
		room_cache_entry_t *entry = &g_RoomCache[i];
		entry->id                = netbufReadU8(src);
		entry->state             = netbufReadU8(src);
		const char *name         = netbufReadStr(src);
		if (name) {
			strncpy(entry->name, name, ROOM_NAME_MAX - 1);
			entry->name[ROOM_NAME_MAX - 1] = '\0';
		} else {
			entry->name[0] = '\0';
		}
		entry->client_count      = netbufReadU8(src);
		entry->max_players       = netbufReadU8(src);
		entry->creator_client_id = netbufReadU8(src);
		g_RoomCacheCount++;
	}

	/* Skip any extra rooms beyond cache capacity */
	for (u8 i = ROOM_CACHE_MAX; i < count; i++) {
		netbufReadU8(src);
		netbufReadU8(src);
		netbufReadStr(src);
		netbufReadU8(src);
		netbufReadU8(src);
		netbufReadU8(src);
	}

	if (src->error) return src->error;

	sysLogPrintf(LOG_NOTE, "NET: SVC_ROOM_LIST received (%d rooms)", (s32)count);
	return src->error;
}

u32 netmsgSvcRoomAssignWrite(struct netbuf *dst, u8 room_id)
{
	netbufWriteU8(dst, SVC_ROOM_ASSIGN);
	netbufWriteU8(dst, room_id);
	return dst->error;
}

u32 netmsgSvcRoomAssignRead(struct netbuf *src, struct netclient *srccl)
{
	u8 room_id = netbufReadU8(src);
	if (src->error) return src->error;

	g_LocalRoomId = room_id;

	if (room_id != 0xFF) {
		sysLogPrintf(LOG_NOTE, "NET: SVC_ROOM_ASSIGN: assigned to room %u", (unsigned)room_id);
#if !defined(PD_SERVER)
		extern void pdguiSetInRoom(s32 inRoom);
		pdguiSetInRoom(1);
#endif
	} else {
		sysLogPrintf(LOG_NOTE, "NET: SVC_ROOM_ASSIGN: returned to lounge");
#if !defined(PD_SERVER)
		extern void pdguiSetInRoom(s32 inRoom);
		pdguiSetInRoom(0);
#endif
	}

	return src->error;
}

/* ---- SVC_MUSIC_ADVANCE (v34 base, v40 adds match_clock_offset_ms) ---- */

u32 netmsgSvcMusicAdvanceWrite(struct netbuf *dst, const char *track_id, u32 match_clock_offset_ms)
{
	netbufWriteU8(dst, SVC_MUSIC_ADVANCE);
	netbufWriteStr(dst, track_id ? track_id : "");
	/* v40: authoritative track offset in ms since track-start. Clients
	 * compare to local position and converge via modMusicSetRate or a
	 * hard-seek if drift exceeds the lerp window. Field is mandatory
	 * on the wire from v40 onwards; mixed-version play is rejected at
	 * the auth handshake (NET_PROTOCOL_VER). */
	netbufWriteU32(dst, match_clock_offset_ms);
	return dst->error;
}

/* Forward decl: client sync receive entry point lives in audio.c. */
#if !defined(PD_SERVER)
extern void audioMusicSyncReceive(const char *track_id, u32 match_clock_offset_ms);
#endif

u32 netmsgSvcMusicAdvanceRead(struct netbuf *src, struct netclient *srccl)
{
	const char *track_str = netbufReadStr(src);
	u32 match_clock_offset_ms = netbufReadU32(src);
	if (src->error) return src->error;

	const char *track_id = track_str ? track_str : "";
	if (!track_id[0]) return src->error;

#if !defined(PD_SERVER)
	/* Issue 4b (2026-04-24): hand off to the sync layer. It decides
	 * whether this is a track-change (start fresh playback) or a
	 * drift-update (keep current track, update target offset). */
	audioMusicSyncReceive(track_id, match_clock_offset_ms);
#else
	(void)track_id;
	(void)match_clock_offset_ms;
#endif

	return src->error;
}

/* ---- Phase 2 SVC_ACHIEVEMENT_TOAST (protocol v41) ----
 *
 * Authoritative match host -> all clients in the host's room. The
 * receiver enqueues a TOAST_CATEGORY_SOCIAL popup gated by
 * socialNotifMaskGet so the user's settings checkbox actually mutes
 * achievement broadcasts. The actor_handle is the social handle of the
 * player who earned the achievement; it lets the toast UI link the
 * popup to the right friend record (e.g. for per-friend mute via
 * socialFriend.muted).
 */

u32 netmsgSvcAchievementToastWrite(struct netbuf *dst, u32 actor_handle,
                                    const char *achievement_text)
{
	netbufWriteU8(dst, SVC_ACHIEVEMENT_TOAST);
	netbufWriteU32(dst, actor_handle);
	netbufWriteStr(dst, achievement_text ? achievement_text : "");
	return dst->error;
}

u32 netmsgSvcAchievementToastRead(struct netbuf *src, struct netclient *srccl)
{
	u32 actor_handle = netbufReadU32(src);
	const char *txt  = netbufReadStr(src);
	if (src->error) return src->error;

	(void)srccl;

#if !defined(PD_SERVER)
	if (!txt || !txt[0]) return src->error;

	/* Build a "name: achievement" body. Look up friend / participant
	 * name from social store first; fall back to the handle hex on
	 * unknown actors so anonymous events still display sensibly. */
	const social_friend_t *f = socialFriendByHandle(actor_handle);
	char title[96];
	if (f && f->agent_name[0]) {
		snprintf(title, sizeof(title), "%s -- achievement", f->agent_name);
	} else if (actor_handle == socialMyHandle()) {
		snprintf(title, sizeof(title), "Achievement");
	} else {
		snprintf(title, sizeof(title), "0x%08x -- achievement", (unsigned)actor_handle);
	}
	(void)pdguiToastEnqueue(actor_handle, TOAST_CATEGORY_SOCIAL, title, txt);
#else
	(void)actor_handle;
	(void)txt;
#endif

	return src->error;
}

/* Convenience: server / authoritative host enqueues a broadcast to every
 * client in the local-server's match room. No-op when not the host. */
void netSendAchievementToast(u32 actor_handle, const char *achievement_text)
{
	if (g_NetMode != NETMODE_SERVER) return;
	if (!achievement_text || !achievement_text[0]) return;
	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcAchievementToastWrite(&g_NetMsgRel, actor_handle, achievement_text);
	netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL);
	sysLogPrintf(LOG_NOTE, "NET: SVC_ACHIEVEMENT_TOAST broadcast actor=0x%08x text=\"%s\"",
	             (unsigned)actor_handle, achievement_text);
}

/* ---- Phase 3 spectator wire (protocol v42) ---- */

#if !defined(PD_SERVER)
#include "spectator.h"
#endif
#include "game/mplayer/mplayer.h"
#include "game/mplayer/participant.h"
#include "game/chr.h"
#include "game/player.h"
#include "game/playermgr.h"

u32 netmsgClcSpectateRequestWrite(struct netbuf *dst, u32 my_handle)
{
	netbufWriteU8(dst, CLC_SPECTATE_REQUEST);
	netbufWriteU32(dst, my_handle);
	return dst->error;
}

u32 netmsgClcSpectateRequestRead(struct netbuf *src, struct netclient *srccl)
{
	u32 client_handle = netbufReadU32(src);
	if (src->error) return src->error;

	if (!srccl) return src->error;

	/* Promote this netclient to spectator. Skip the player slot
	 * iteration paths by setting CLFLAG_SPECTATOR. The slot was already
	 * allocated at CLC_AUTH; we leave it allocated so the netclient ->
	 * peer mapping survives, but every code path that iterates
	 * "match players" should now skip clients with this flag set. */
	netmsgCutsceneAuthorityRetireClient(srccl->id);
	srccl->flags |= CLFLAG_SPECTATOR;
	(void)client_handle; /* informational; trust authoritatively comes from CLC_AUTH */

	/* Reply with an accept token (the netclient.id is opaque + stable). */
	const u32 stream_token = (u32)SDL_GetTicks() ^ srccl->id;
	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcSpectateAckWrite(&g_NetMsgRel, 1, stream_token);
	netSend(srccl, &g_NetMsgRel, true, NETCHAN_CONTROL);

	sysLogPrintf(LOG_NOTE,
	             "NET: CLC_SPECTATE_REQUEST -> client %u promoted to spectator (handle=0x%08x)",
	             srccl->id, (unsigned)client_handle);
	return src->error;
}

u32 netmsgSvcSpectateAckWrite(struct netbuf *dst, u8 accepted, u32 stream_token)
{
	netbufWriteU8(dst, SVC_SPECTATE_ACK);
	netbufWriteU8(dst, accepted);
	netbufWriteU32(dst, stream_token);
	return dst->error;
}

u32 netmsgSvcSpectateAckRead(struct netbuf *src, struct netclient *srccl)
{
	u8  accepted = netbufReadU8(src);
	u32 stream_token = netbufReadU32(src);
	(void)stream_token;
	(void)srccl;
	if (src->error) return src->error;
#if !defined(PD_SERVER)
	if (!accepted) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_SPECTATE_ACK refused");
		spectatorStop();
	} else {
		sysLogPrintf(LOG_NOTE, "NET: SVC_SPECTATE_ACK accepted token=0x%08x",
		             (unsigned)stream_token);
	}
#else
	(void)accepted;
#endif
	return src->error;
}

u32 netmsgSvcStateFrameWrite(struct netbuf *dst, u32 host_handle, u32 frame_seq,
                              const void *participants_blob, u32 participants_count)
{
	netbufWriteU8(dst, SVC_STATE_FRAME);
	netbufWriteU32(dst, host_handle);
	netbufWriteU32(dst, frame_seq);
	const u32 cap = (participants_count > SPECTATE_FRAME_PARTICIPANTS_MAX)
	                ? (u32)SPECTATE_FRAME_PARTICIPANTS_MAX : participants_count;
	netbufWriteU8(dst, (u8)cap);

	const u8 *p = (const u8 *)participants_blob;
	const u32 per_block = 1 + 1 + 1 + 1   /* in_use + team + is_bot + pad   */
	                     + 2 + 2          /* score + deaths                 */
	                     + 4 + 4 + 4      /* pos[3]                         */
	                     + 4 + 4          /* angle theta + verta            */
	                     + 4              /* weapon_runtime_idx             */
	                     + SPECTATE_FRAME_NAME_MAX; /* name                  */
	(void)per_block;

	for (u32 i = 0; i < cap; i++) {
		const u8 *blk = p + (size_t)i * per_block;
		/* in_use / team / is_bot / pad */
		netbufWriteU8(dst, blk[0]);
		netbufWriteU8(dst, blk[1]);
		netbufWriteU8(dst, blk[2]);
		netbufWriteU8(dst, blk[3]);
		/* score / deaths (s16) */
		netbufWriteU16(dst, *(const u16 *)(blk + 4));
		netbufWriteU16(dst, *(const u16 *)(blk + 6));
		/* pos[3] (f32) */
		netbufWriteF32(dst, *(const f32 *)(blk + 8));
		netbufWriteF32(dst, *(const f32 *)(blk + 12));
		netbufWriteF32(dst, *(const f32 *)(blk + 16));
		/* angles */
		netbufWriteF32(dst, *(const f32 *)(blk + 20));
		netbufWriteF32(dst, *(const f32 *)(blk + 24));
		/* weapon_runtime_idx */
		netbufWriteU32(dst, *(const u32 *)(blk + 28));
		/* name (32 bytes, null-padded) */
		netbufWriteData(dst, blk + 32, SPECTATE_FRAME_NAME_MAX);
	}
	return dst->error;
}

u32 netmsgSvcStateFrameRead(struct netbuf *src, struct netclient *srccl)
{
	u32 host_handle = netbufReadU32(src);
	u32 frame_seq   = netbufReadU32(src);
	u8  count       = netbufReadU8(src);
	(void)frame_seq;
	(void)srccl;

	if (src->error) return src->error;
	if (count > SPECTATE_FRAME_PARTICIPANTS_MAX) {
		src->error = 1;
		return src->error;
	}

#if !defined(PD_SERVER)
	spectator_participant_t blob[SPECTATE_FRAME_PARTICIPANTS_MAX];
	memset(blob, 0, sizeof(blob));
	for (u32 i = 0; i < count; i++) {
		blob[i].in_use = netbufReadU8(src);
		blob[i].team   = netbufReadU8(src);
		blob[i].is_bot = netbufReadU8(src);
		(void)netbufReadU8(src); /* pad */
		blob[i].score  = (s16)netbufReadU16(src);
		blob[i].deaths = (s16)netbufReadU16(src);
		blob[i].pos[0] = netbufReadF32(src);
		blob[i].pos[1] = netbufReadF32(src);
		blob[i].pos[2] = netbufReadF32(src);
		blob[i].angle_theta = netbufReadF32(src);
		blob[i].angle_verta = netbufReadF32(src);
		blob[i].weapon_runtime_idx = netbufReadU32(src);
		netbufReadData(src, (u8 *)blob[i].name, SPECTATE_FRAME_NAME_MAX);
		blob[i].name[SPECTATE_FRAME_NAME_MAX - 1] = '\0';
	}
	if (!src->error) {
		spectatorIngestParticipantSnapshot(blob, (s32)count, host_handle);
	}
#else
	(void)host_handle; (void)count;
#endif
	return src->error;
}

/* Build + broadcast a state frame to every CLFLAG_SPECTATOR netclient.
 *
 * Source data: g_MpParticipants slot table (via mpIsParticipantActive),
 * with chr position + score + name pulled from the playermgr / chrdata
 * tables already maintained on the host. For Phase 3 we surface a
 * minimal snapshot (position + angles + score + name) -- weapon /
 * health / animation can extend later without a wire bump because the
 * frame layout has explicit field count via `count` and the participant
 * block size is fixed by SPECTATE_FRAME_NAME_MAX (so future extensions
 * either ride the next NET_PROTOCOL_VER bump or live in a parallel
 * SVC_STATE_FRAME_EXT). */
void netSendSpectateStateFrame(void)
{
	if (g_NetMode != NETMODE_SERVER) return;

	/* SPECTATOR_FANOUT_HZ rate gate (10 Hz / 100 ms). Hard-coded here
	 * so pd-server does not depend on the client-only spectator.h. */
	static u32 s_LastBroadcastMs = 0;
	const u32 now = SDL_GetTicks();
	if (now - s_LastBroadcastMs < 100u) return;
	s_LastBroadcastMs = now;

	/* Skip if no spectators are subscribed. */
	s32 nspec = 0;
	for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
		if (g_NetClients[i].state == CLSTATE_DISCONNECTED) continue;
		if (g_NetClients[i].flags & CLFLAG_SPECTATOR) { nspec++; }
	}
	if (nspec == 0) return;

	/* Static blob layout matches the one netmsgSvcStateFrameWrite expects. */
	#define BLK_SIZE 64
	static u8 s_Blob[SPECTATE_FRAME_PARTICIPANTS_MAX * BLK_SIZE];
	static u32 s_FrameSeq = 0;
	memset(s_Blob, 0, sizeof(s_Blob));
	u32 nfilled = 0;

	for (s32 i = 0; i < MAX_MPCHRS && nfilled < SPECTATE_FRAME_PARTICIPANTS_MAX; i++) {
		if (!mpIsParticipantActive(i)) continue;

		u8 *blk = s_Blob + (size_t)nfilled * BLK_SIZE;
		const MpParticipant *part = mpGetParticipant(i);
		s16 score = 0, deaths = 0;
		f32 pos_x = 0.0f, pos_y = 0.0f, pos_z = 0.0f;
		f32 ang_theta = 0.0f, ang_verta = 0.0f;
		const char *name = "";
		u8 is_bot = 0;
		u8 team = 0;

		if (part) {
			team = part->team;
			is_bot = (part->type == PARTICIPANT_BOT) ? 1 : 0;
			if (part->type == PARTICIPANT_BOT && part->config && part->config->name[0]) {
				name = part->config->name;
			} else if ((part->type == PARTICIPANT_LOCAL || part->type == PARTICIPANT_REMOTE) &&
			           part->client_id >= 0 && part->client_id < (s8)NET_MAX_CLIENTS) {
				const struct netclient *cl = &g_NetClients[part->client_id];
				name = cl->settings.name;
			}
			if (part->chr && part->chr->prop) {
				pos_x = part->chr->prop->pos.x;
				pos_y = part->chr->prop->pos.y;
				pos_z = part->chr->prop->pos.z;
			}
		}

		blk[0] = 1;                              /* in_use                 */
		blk[1] = team;                           /* team                   */
		blk[2] = is_bot;                         /* is_bot                 */
		blk[3] = 0;                              /* pad                    */
		*(s16 *)(blk + 4) = score;
		*(s16 *)(blk + 6) = deaths;
		*(f32 *)(blk + 8)  = pos_x;
		*(f32 *)(blk + 12) = pos_y;
		*(f32 *)(blk + 16) = pos_z;
		*(f32 *)(blk + 20) = ang_theta;
		*(f32 *)(blk + 24) = ang_verta;
		*(u32 *)(blk + 28) = 0; /* weapon_runtime_idx (Phase 3 follow-up) */
		strncpy((char *)(blk + 32), name ? name : "", SPECTATE_FRAME_NAME_MAX - 1);

		nfilled++;
	}

	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcStateFrameWrite(&g_NetMsgRel, (u32)0 /* host handle filled by client side */,
	                          s_FrameSeq, s_Blob, nfilled);
	s_FrameSeq++;

	/* Send only to spectator-subscribed clients. */
	for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
		struct netclient *cl = &g_NetClients[i];
		if (cl->state == CLSTATE_DISCONNECTED) continue;
		if (!(cl->flags & CLFLAG_SPECTATOR)) continue;
		netSend(cl, &g_NetMsgRel, false /* unreliable -- 10 Hz stream */, NETCHAN_DEFAULT);
	}
}

/* ---- Track 2d GPU swarm state sync (protocol v48, 2026-05-16, c3807) ---- */

#include "net/swarm_sync_quant.h"

/* Public extraction primitive lives in port/fast3d/swarm_gpu.cpp; declare
 * here so this TU links without pulling in the swarm_gpu header (which is
 * client-only by construction -- pd-server does NOT link swarm_gpu.cpp,
 * but the SEND path below is gated on g_NetDedicated == 0 so a pd-server
 * build never reaches the symbol). The decode + apply path also lives in
 * swarm_gpu.cpp (swarmGpuApplyRemoteState); we forward-declare it here.
 *
 * pd-server gating: the encode path (swarmGpuReadbackTextureRows) and the
 * apply path (swarmGpuApplyRemoteState) are #if-guarded out of pd-server
 * via PD_SERVER (see CMakeLists' server target's SRC_SERVER which excludes
 * fast3d/). On pd-server netSendGpuSwarmState short-circuits on
 * g_NetDedicated; netmsgSvcGpuSwarmStateRead reads + discards the bytes
 * (no apply call) so the wire stays in sync if a future code path ever
 * routes one of these through pd-server. */
#if !defined(PD_SERVER)
extern int  swarmGpuReadbackTextureRows(int row_start, int row_count,
                                         float *out_buf, int out_capacity);
extern void swarmGpuApplyRemoteState(const float *raw_rgba32f_buf, int count,
                                      int chunk_start);
#endif

/* Single-chunk wire writer. Format (all little-endian):
 *   u8  msgid       (SVC_GPUSWARM_STATE)
 *   u32 frame_idx
 *   u16 total_count
 *   u16 chunk_start
 *   u16 chunk_count
 *   chunk_count * SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT bytes
 *
 * Returns the netbuf's error code (0 on success). The caller is
 * responsible for netbufStartWrite + netSend around each call -- this
 * function only writes into the buffer.
 */
u32 netmsgSvcGpuSwarmStateWrite(struct netbuf *dst, u32 frame_idx,
                                 u16 total_count, u16 chunk_start,
                                 u16 chunk_count,
                                 const u8 *packed_chunk_bytes)
{
	netbufWriteU8(dst, SVC_GPUSWARM_STATE);
	netbufWriteU32(dst, frame_idx);
	netbufWriteU16(dst, total_count);
	netbufWriteU16(dst, chunk_start);
	netbufWriteU16(dst, chunk_count);
	if (chunk_count > 0 && packed_chunk_bytes != NULL) {
		const u32 payload_bytes =
			(u32)chunk_count * (u32)SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT;
		netbufWriteData(dst, packed_chunk_bytes, payload_bytes);
	}
	return dst->error;
}

/* Wire reader. Reads + sanity-checks the chunk header, slurps the packed
 * bytes into a static scratch, dequantizes into a stack float buffer, and
 * forwards to swarmGpuApplyRemoteState (client-side only). Server build
 * compiles out the apply call.
 *
 * Bounds: the static scratch is sized for SWARM_SYNC_CHUNK_BOTS_MAX bots;
 * chunks larger than that are rejected (the writer enforces the same cap
 * but a hostile / mis-versioned peer cannot blow our stack).
 */
u32 netmsgSvcGpuSwarmStateRead(struct netbuf *src, struct netclient *srccl)
{
	(void)srccl;

	const u32 frame_idx     = netbufReadU32(src);
	const u16 total_count   = netbufReadU16(src);
	const u16 chunk_start   = netbufReadU16(src);
	const u16 chunk_count   = netbufReadU16(src);

	if (src->error) return src->error;

	if (chunk_count > SWARM_SYNC_CHUNK_BOTS_MAX) {
		sysLogPrintf(LOG_WARNING,
			"NETMSG.GPUSWARM.RECV: chunk_count=%u exceeds cap=%u; dropping",
			(unsigned)chunk_count, (unsigned)SWARM_SYNC_CHUNK_BOTS_MAX);
		return 1;
	}

	if (chunk_count == 0) {
		/* Header-only no-op. Should not happen but be lenient. */
		sysLogPrintf(LOG_NOTE,
			"NETMSG.GPUSWARM.RECV: frame=%u total=%u start=%u count=0 (empty)",
			(unsigned)frame_idx, (unsigned)total_count, (unsigned)chunk_start);
		return src->error;
	}

	/* Slurp the packed payload. Static scratch keeps the netmsg.c BSS
	 * footprint deterministic (20 KB; the live GPU bot scratch in
	 * swarm_gpu.cpp is far bigger). */
	static u8 s_PackedScratch[SWARM_SYNC_CHUNK_BOTS_MAX
		* SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT];
	const u32 payload_bytes =
		(u32)chunk_count * (u32)SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT;
	netbufReadData(src, s_PackedScratch, payload_bytes);

	if (src->error) return src->error;

	/* Dequantize into a row-major float buffer the GL upload path
	 * understands. 20 floats / bot * SWARM_SYNC_CHUNK_BOTS_MAX. */
	static float s_DequantScratch[SWARM_SYNC_CHUNK_BOTS_MAX * 5 * 4];
	const int decoded = swarmSyncQuantDecode(s_PackedScratch,
		(int)chunk_count, s_DequantScratch);

	if (decoded <= 0) {
		sysLogPrintf(LOG_WARNING,
			"NETMSG.GPUSWARM.RECV: dequant failed (chunk_count=%u)",
			(unsigned)chunk_count);
		return src->error;
	}

#if !defined(PD_SERVER)
	swarmGpuApplyRemoteState(s_DequantScratch, decoded, (int)chunk_start);
#endif

	sysLogPrintf(LOG_NOTE,
		"NETMSG.GPUSWARM.RECV: frame=%u count=%u dequantized=%d start=%u total=%u",
		(unsigned)frame_idx, (unsigned)chunk_count, decoded,
		(unsigned)chunk_start, (unsigned)total_count);

	return src->error;
}

/* Listen-host broadcast helper. Called from swarmGpuStepAndApply on the
 * listen host after the per-frame compute dispatch + state-texture
 * readback. No-op on:
 *   - not in NETMODE_SERVER (we are a pure client or no networking at all)
 *   - g_NetDedicated == 1 (no GPU on pd-server)
 *   - no remote peer connected (the broadcast would just be a self-talk)
 *   - swarmGpuReadbackTextureRows returns 0 (GPU swarm not armed yet)
 *
 * Throttled via a static frame counter; broadcasts at every
 * SWARM_SYNC_THROTTLE_FRAMES'th call (10 Hz at 60 FPS).
 *
 * Linkage caveat: this function references swarmGpuReadbackTextureRows
 * which is NOT linked into pd-server. To keep pd-server's link clean,
 * the whole function body is wrapped in `#if !defined(PD_SERVER)`. The
 * pd-server stub is an empty no-op (the dispatcher never calls it
 * because g_NetMode on pd-server with no GPU swarm running stays in a
 * state where the swarm system itself doesn't tick).
 */
void netSendGpuSwarmState(void)
{
#if !defined(PD_SERVER)
	if (g_NetMode != NETMODE_SERVER) return;
	if (g_NetDedicated) return;
	/* This snapshot bypasses netEndFrame's shared buffers, so it must observe
	 * the same initial-stage publication barrier explicitly. Control traffic
	 * continues while peers load; entity state does not. */
	if (netServerStageReplicationBlocked()) return;

	/* Skip if no peer is actually connected. We allow listen-host with
	 * zero peers (no broadcast cost) but want to still tick the throttle
	 * counter so the first peer connection doesn't see a sudden burst. */
	bool any_peer = false;
	for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
		struct netclient *cl = &g_NetClients[i];
		if (cl == g_NetLocalClient) continue;
		if (cl->state >= CLSTATE_LOBBY && cl->peer) {
			any_peer = true;
			break;
		}
	}

	static u32 s_ThrottleCtr = 0;
	static u32 s_FrameSeq    = 0;
	s_ThrottleCtr++;
	if (s_ThrottleCtr < SWARM_SYNC_THROTTLE_FRAMES) return;
	s_ThrottleCtr = 0;

	if (!any_peer) {
		/* Throttle ticked; no peer to broadcast to. Bump frame_seq so
		 * the receiver-side sequence (if it ever exists) doesn't see
		 * collapsed indices when the first peer joins. */
		s_FrameSeq++;
		return;
	}

	/* Read all 5 rows from the GPU state texture. Static scratch sized
	 * for the maximum compute pool (SWARM_GPU_MAX = 4096 in
	 * swarm_gpu.cpp). 4096 * 5 * 4 floats = 320 KB BSS. */
	#define SWARM_SYNC_MAX_BOTS  4096
	static float s_TexScratch[SWARM_SYNC_MAX_BOTS * 5 * 4];
	const int texels_read = swarmGpuReadbackTextureRows(0, 5, s_TexScratch,
		(int)(sizeof(s_TexScratch) / sizeof(float)));
	if (texels_read <= 0) {
		/* GPU swarm not armed (no dispatch yet, or driver fallback).
		 * Silent skip -- this hits every frame until the first
		 * dispatch lands, which we don't want to log-spam. */
		return;
	}

	const int total_count = SWARM_SYNC_MAX_BOTS;
	/* The state texture is sized for the FULL pool (SWARM_GPU_MAX); only
	 * the first `count` columns of each row are populated each frame.
	 * The GPU swarm doesn't expose its `count` to this layer, so we
	 * broadcast the full pool. Receivers do the same work to dequant +
	 * upload; quiescent (zeroed) bots cost 20 bytes each on the wire.
	 *
	 * A future slice can plumb the live count through swarmGpuStateGet-
	 * Count() to trim broadcast size at low ladder ranges. For v1 we
	 * pay the worst-case ~80 KB / 100ms = ~800 KB/s (well under any
	 * reasonable LAN budget) and keep the wire format simple. */

	/* Encode + chunk + broadcast. Each chunk's packed bytes live in a
	 * static scratch sized for the chunk cap; we copy out and netSend in
	 * a tight loop. */
	static u8 s_PackedScratch[SWARM_SYNC_CHUNK_BOTS_MAX
		* SWARM_SYNC_QUANT_PACKED_BYTES_PER_BOT];

	int chunks_sent = 0;
	for (int chunk_start = 0; chunk_start < total_count;
			chunk_start += SWARM_SYNC_CHUNK_BOTS_MAX) {
		int chunk_count = total_count - chunk_start;
		if (chunk_count > SWARM_SYNC_CHUNK_BOTS_MAX) {
			chunk_count = SWARM_SYNC_CHUNK_BOTS_MAX;
		}

		/* Build a slice of the row-major texture buffer for THIS chunk.
		 * swarmSyncQuantEncode expects a 5-row buffer where each row
		 * is `count` texels * 4 floats. The source texture buffer's
		 * rows are total_count wide; we need to extract a sub-slice
		 * per row. Easiest is to build a temp slice buffer. Avoid the
		 * extra copy by encoding a per-row stride into the quant call
		 * -- but the API doesn't take strides yet, so for v1 we just
		 * copy the chunk's slice into a scratch. */
		static float s_ChunkSlice[SWARM_SYNC_CHUNK_BOTS_MAX * 5 * 4];
		for (int row = 0; row < 5; row++) {
			const float *src_row =
				s_TexScratch + row * total_count * 4 + chunk_start * 4;
			float *dst_row = s_ChunkSlice + row * chunk_count * 4;
			memcpy(dst_row, src_row,
				(size_t)chunk_count * 4 * sizeof(float));
		}

		const int packed_bytes = swarmSyncQuantEncode(s_ChunkSlice,
			chunk_count, s_PackedScratch);
		if (packed_bytes <= 0) continue;

		netbufStartWrite(&g_NetMsg);
		netmsgSvcGpuSwarmStateWrite(&g_NetMsg, s_FrameSeq,
			(u16)total_count, (u16)chunk_start, (u16)chunk_count,
			s_PackedScratch);
		netSend(NULL, &g_NetMsg, false /* unreliable */, NETCHAN_DEFAULT);
		chunks_sent++;
	}

	sysLogPrintf(LOG_NOTE,
		"NETMSG.GPUSWARM.SEND: frame=%u count=%d chunks=%d throttle_hz=%d",
		(unsigned)s_FrameSeq, total_count, chunks_sent,
		60 / SWARM_SYNC_THROTTLE_FRAMES);

	s_FrameSeq++;
#endif /* !PD_SERVER */
}

/**
 * Broadcast SVC_MUSIC_ADVANCE to all clients in a room (or all if room_id == 0xFF).
 * Called by the host on track change AND periodically (every ~2s) for drift
 * correction. match_clock_offset_ms is the elapsed milliseconds since the
 * current track started on the host (0 on a fresh track-change).
 */
void netMusicBroadcastAdvance(const char *track_id, u8 room_id, u32 match_clock_offset_ms)
{
	if (g_NetMode != NETMODE_SERVER || !track_id || !track_id[0]) return;

	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcMusicAdvanceWrite(&g_NetMsgRel, track_id, match_clock_offset_ms);

	if (room_id != 0xFF) {
		netSendToRoom(room_id, &g_NetMsgRel, true, NETCHAN_CONTROL);
	} else {
		netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL);
	}

	sysLogPrintf(LOG_NOTE, "NET: SVC_MUSIC_ADVANCE broadcast: '%s' offset=%u ms room=%u",
	             track_id, match_clock_offset_ms, (unsigned)room_id);
}

u32 netmsgClcRoomCreateWrite(struct netbuf *dst, const char *name, u8 access,
                              const char *password, u8 maxPlayers)
{
	netbufWriteU8(dst, CLC_ROOM_CREATE);
	netbufWriteStr(dst, name ? name : "");
	/* SEC-14: access + password + max_players on the wire. */
	netbufWriteU8(dst, access);
	netbufWriteStr(dst, password ? password : "");
	netbufWriteU8(dst, maxPlayers);
	return dst->error;
}

u32 netmsgClcRoomCreateRead(struct netbuf *src, struct netclient *srccl)
{
	const char *name = netbufReadStr(src);
	/* Copy name into a local buffer immediately — netbufReadStr returns a
	 * pointer into the shared netbuf, and subsequent reads can overwrite it. */
	char nameBuf[ROOM_NAME_MAX];
	if (name) {
		strncpy(nameBuf, name, sizeof(nameBuf) - 1);
		nameBuf[sizeof(nameBuf) - 1] = '\0';
	} else {
		nameBuf[0] = '\0';
	}
	const u8 accessRaw   = netbufReadU8(src);
	const char *password = netbufReadStr(src);
	char passwordBuf[32];
	if (password) {
		strncpy(passwordBuf, password, sizeof(passwordBuf) - 1);
		passwordBuf[sizeof(passwordBuf) - 1] = '\0';
	} else {
		passwordBuf[0] = '\0';
	}
	const u8 maxPlayers  = netbufReadU8(src);
	if (src->error) return src->error;

	if (g_NetMode != NETMODE_SERVER) return src->error;
	if (!srccl) return 1;

	/* SEC-13: rate-limit room mutations (1/sec/client). */
	if (!netmsgRoomRateAllow(srccl)) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_ROOM_CREATE rate-limited for client %u", srccl->id);
		return src->error;
	}

	/* Leave current room first if in one */
	if (srccl->room_id != 0xFF) {
		hub_room_t *old = roomGetById(srccl->room_id);
		netmsgCutsceneAuthorityRetireClient(srccl->id);
		if (old) roomLeave(old, srccl->id);
	}

	/* Generate name if not provided */
	char genName[ROOM_NAME_MAX];
	const char *finalName = nameBuf;
	if (!finalName[0]) {
		roomGenerateName(genName, sizeof(genName));
		finalName = genName;
	}

	/* SEC-14: validate access mode before room creation. */
	room_access_t access = ROOM_ACCESS_OPEN;
	if (accessRaw > ROOM_ACCESS_INVITE) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_ROOM_CREATE from client %u rejected — access=%u out of range",
			srccl->id, accessRaw);
		return 1;
	} else if (accessRaw == ROOM_ACCESS_PASSWORD) {
		/* Require a non-empty password for password rooms. */
		if (!passwordBuf[0]) {
			sysLogPrintf(LOG_WARNING, "NET: CLC_ROOM_CREATE from client %u rejected — password room without password",
				srccl->id);
			return 1;
		} else {
			access = ROOM_ACCESS_PASSWORD;
		}
	} else if (accessRaw == ROOM_ACCESS_INVITE) {
		access = ROOM_ACCESS_INVITE;
	}

	/* Clamp max_players to [1, HUB_MAX_CLIENTS].  0 = use default. */
	u8 maxp = maxPlayers;
	if (maxp == 0) maxp = HUB_MAX_CLIENTS;
	if (maxp > HUB_MAX_CLIENTS) maxp = HUB_MAX_CLIENTS;

	hub_room_t *room = roomCreateConfigured(finalName, maxp, access,
	                                         passwordBuf[0] ? passwordBuf : NULL,
	                                         srccl->id);
	if (!room) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_ROOM_CREATE failed — no free slots");
		return src->error;
	}

	srccl->room_id = room->id;

	sysLogPrintf(LOG_NOTE, "NET: CLC_ROOM_CREATE from client %u — created room %u \"%s\"",
	             srccl->id, (unsigned)room->id, room->name);

	/* Send room assignment to the creator */
	struct netbuf assignBuf;
	u8 assignData[8];
	assignBuf.data = assignData;
	assignBuf.size = sizeof(assignData);
	netbufStartWrite(&assignBuf);
	netmsgSvcRoomAssignWrite(&assignBuf, room->id);
	netSend(srccl, &assignBuf, true, NETCHAN_DEFAULT);

	/* SEC-13: coalesce room-list broadcast into end-of-frame flush. */
	netRoomListMarkDirty();

	return src->error;
}

u32 netmsgClcRoomJoinWrite(struct netbuf *dst, u8 room_id, const char *password)
{
	netbufWriteU8(dst, CLC_ROOM_JOIN);
	netbufWriteU8(dst, room_id);
	/* SEC-14: optional password (empty string for open rooms). */
	netbufWriteStr(dst, password ? password : "");
	return dst->error;
}

u32 netmsgClcRoomJoinRead(struct netbuf *src, struct netclient *srccl)
{
	u8 room_id = netbufReadU8(src);
	const char *password = netbufReadStr(src);
	/* Copy before downstream operations can clobber the netbuf read pointer. */
	char passwordBuf[32];
	if (password) {
		strncpy(passwordBuf, password, sizeof(passwordBuf) - 1);
		passwordBuf[sizeof(passwordBuf) - 1] = '\0';
	} else {
		passwordBuf[0] = '\0';
	}
	if (src->error) return src->error;

	if (g_NetMode != NETMODE_SERVER) return src->error;
	if (!srccl) return 1;

	/* SEC-13: rate-limit room mutations (1/sec/client). */
	if (!netmsgRoomRateAllow(srccl)) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_ROOM_JOIN rate-limited for client %u", srccl->id);
		return src->error;
	}

	hub_room_t *room = roomGetById(room_id);
	if (!room) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_ROOM_JOIN from client %u — room %u not found",
		             srccl->id, (unsigned)room_id);
		return src->error;
	}

	if (room->state != ROOM_STATE_LOBBY) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_ROOM_JOIN from client %u — room %u not in lobby state",
		             srccl->id, (unsigned)room_id);
		return src->error;
	}

	/* SEC-14: enforce password before modifying any state. */
	if (!roomCheckPassword(room, passwordBuf)) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_ROOM_JOIN from client %u — wrong password for room %u",
		             srccl->id, (unsigned)room_id);
		return src->error;
	}

	/* SEC-14: invite-only rooms can only be joined by the creator (for now). */
	if (room->access == ROOM_ACCESS_INVITE && srccl->id != room->creator_client_id) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_ROOM_JOIN from client %u — invite-only room %u rejected",
		             srccl->id, (unsigned)room_id);
		return src->error;
	}

	/* Leave current room first if in one */
	if (srccl->room_id != 0xFF) {
		hub_room_t *old = roomGetById(srccl->room_id);
		if (old) roomLeave(old, srccl->id);
	}

	if (!roomJoin(room, srccl->id)) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_ROOM_JOIN from client %u — room %u full",
		             srccl->id, (unsigned)room_id);
		return src->error;
	}

	srccl->room_id = room->id;

	sysLogPrintf(LOG_NOTE, "NET: CLC_ROOM_JOIN from client %u — joined room %u \"%s\"",
	             srccl->id, (unsigned)room->id, room->name);

	/* Send assignment to the joiner */
	struct netbuf assignBuf;
	u8 assignData[8];
	assignBuf.data = assignData;
	assignBuf.size = sizeof(assignData);
	netbufStartWrite(&assignBuf);
	netmsgSvcRoomAssignWrite(&assignBuf, room->id);
	netSend(srccl, &assignBuf, true, NETCHAN_DEFAULT);

	/* SEC-13: coalesce room-list broadcast into end-of-frame flush. */
	netRoomListMarkDirty();

	return src->error;
}

u32 netmsgClcRoomLeaveWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, CLC_ROOM_LEAVE);
	return dst->error;
}

u32 netmsgClcRoomLeaveRead(struct netbuf *src, struct netclient *srccl)
{
	if (src->error) return src->error;

	if (g_NetMode != NETMODE_SERVER) return src->error;
	if (!srccl) return 1;

	if (srccl->room_id == 0xFF) return src->error;

	/* SEC-13: rate-limit room mutations (1/sec/client). */
	if (!netmsgRoomRateAllow(srccl)) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_ROOM_LEAVE rate-limited for client %u", srccl->id);
		return src->error;
	}

	hub_room_t *room = roomGetById(srccl->room_id);
	netmsgCutsceneAuthorityRetireClient(srccl->id);
	if (room) {
		roomLeave(room, srccl->id);
	}

	srccl->room_id = 0xFF;

	sysLogPrintf(LOG_NOTE, "NET: CLC_ROOM_LEAVE from client %u — returned to lounge", srccl->id);

	/* Send assignment (lounge) to the leaver */
	struct netbuf assignBuf;
	u8 assignData[8];
	assignBuf.data = assignData;
	assignBuf.size = sizeof(assignData);
	netbufStartWrite(&assignBuf);
	netmsgSvcRoomAssignWrite(&assignBuf, 0xFF);
	netSend(srccl, &assignBuf, true, NETCHAN_DEFAULT);

	/* SEC-13: coalesce room-list broadcast into end-of-frame flush. */
	netRoomListMarkDirty();

	return src->error;
}

void netListenHostRoomLeave(void)
{
#if defined(PD_SERVER)
	return;
#else
	if (g_NetMode != NETMODE_SERVER || g_NetDedicated || !g_NetLocalClient) {
		return;
	}
	struct netclient *lc = g_NetLocalClient;
	if (lc->room_id == 0xFF) {
		return;
	}

	hub_room_t *room = roomGetById(lc->room_id);
	netmsgCutsceneAuthorityRetireClient(lc->id);
	if (room) {
		roomLeave(room, lc->id);
	}
	lc->room_id = 0xFF;
	g_LocalRoomId = 0xFF;
	extern void pdguiSetInRoom(s32 inRoom);
	pdguiSetInRoom(0);
	sysLogPrintf(LOG_NOTE, "NET: listen host left room — lounge (local path)");
	netRoomListMarkDirty();
#endif
}

void netBroadcastRoomList(void)
{
	if (g_NetMode != NETMODE_SERVER) return;

	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcRoomListWrite(&g_NetMsgRel);
	netSend(NULL, &g_NetMsgRel, true, NETCHAN_DEFAULT);
}

/* ========================================================================
 * CLC_STAGE_READY — client→server (U-10)
 *
 * Sent by pdmain only after the client reaches the real
 * SCENE_EVENT_STAGE_READY boundary. A reconnecting peer remains PREPARING,
 * with its preserved player and room reservation intact, until this message
 * lets the server publish the restored player and enqueue its targeted exact
 * state. Ordinary stage starts still use the same boundary for dedicated
 * bot-authority delegation.
 *
 * The dedicated bot-authority deadline is only an ACTIVE-stage re-election
 * aid; it never bypasses this post-load barrier.
 * ======================================================================== */

u32 netmsgClcStageReadyWrite(struct netbuf *dst)
{
	if (!dst || dst->error || g_NetStageEpoch == 0) {
		return 1;
	}
	netbufWriteU8(dst, CLC_STAGE_READY);
	netbufWriteU32(dst, g_NetStageEpoch);
	return dst->error;
}

u32 netmsgClcStageReadyRead(struct netbuf *src, struct netclient *srccl)
{
	enum net_restore_result restore_result;
	u32 ready_epoch;

	if (!src || !srccl || src->error) {
		return src && src->error ? src->error : 1;
	}
	ready_epoch = netbufReadU32(src);
	if (src->error) {
		return src->error;
	}
	if (ready_epoch == 0 || ready_epoch != g_NetStageEpoch) {
		sysLogPrintf(LOG_WARNING,
			"NET.STAGE.REPLICATION ready=rejected client=%u received_epoch=%u active_epoch=%u reason=stale_or_future",
			(unsigned)srccl->id, (unsigned)ready_epoch,
			(unsigned)g_NetStageEpoch);
		return 0;
	}

	if (srccl->reconnect_resync_pending) {
		if (srccl->state != CLSTATE_PREPARING) {
			sysLogPrintf(LOG_WARNING,
				"NET.RECONNECT.READY rejected client=%u state=%u pending=1",
				(unsigned)srccl->id, (unsigned)srccl->state);
			netServerKick(srccl, DISCONNECT_FILES);
			return 0;
		}
		restore_result = netServerCompleteReconnect(srccl);
		if (restore_result != NET_RESTORE_OK) {
			sysLogPrintf(LOG_ERROR,
				"NET.RECONNECT.READY rollback client=%u status=%s",
				(unsigned)srccl->id,
				netRestoreResultString(restore_result));
			netServerKick(srccl,
				netRestoreResultRequiresRetryableClose(restore_result)
					? DISCONNECT_TIMEOUT : DISCONNECT_FILES);
			return 0;
		}
	}

	if (srccl->state != CLSTATE_GAME) {
		sysLogPrintf(LOG_WARNING, "NET: ignored CLC_STAGE_READY from client %u (state=%u, not GAME)",
		             srccl->id, srccl->state);
		return 0;
	}

	{
		const bool already_ready = srccl->stage_ready;
		srccl->stage_ready = true;
		sysLogPrintf(LOG_NOTE,
			"NET.STAGE.REPLICATION ready client=%u epoch=%u room=%u duplicate=%u post_load=1",
			(unsigned)srccl->id, (unsigned)ready_epoch,
			(unsigned)srccl->room_id,
			already_ready ? 1u : 0u);
	}

	/* Listen hosting has no bot-authority handoff. The reconnect transaction,
	 * if any, was already committed above. */
	if (!g_NetDedicated || g_NetBotAuthorityDelegated) {
		return 0;
	}

	/* Count how many CLSTATE_GAME clients are ready. */
	s32 readyCount = 0, totalCount = 0;
	for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
		if (g_NetClients[ci].state == CLSTATE_GAME) {
			totalCount++;
			if (g_NetClients[ci].stage_ready) {
				readyCount++;
			}
		}
	}

	sysLogPrintf(LOG_NOTE, "NET: client %u stage ready (%d/%d)", srccl->id, readyCount, totalCount);

	if (totalCount > 0 && readyCount == totalCount) {
		/* All clients ready — delegate bot authority to the first CLSTATE_GAME client. */
		for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
			if (g_NetClients[ci].state == CLSTATE_GAME) {
				sysLogPrintf(LOG_NOTE, "NET: all clients ready, sending BOT_AUTHORITY to client %u ('%s')",
				             g_NetClients[ci].id, g_NetClients[ci].settings.name);
				netbufStartWrite(&g_NetMsgRel);
				netmsgSvcBotAuthorityWrite(&g_NetMsgRel);
				netSend(&g_NetClients[ci], &g_NetMsgRel, true, NETCHAN_DEFAULT);
				g_NetBotAuthorityClientId = g_NetClients[ci].id;
				g_NetBotAuthorityDelegated = true;
				g_NetStageReadyDeadline    = -1;
				break;
			}
		}
	}

	return src->error;
}

/* ========================================================================
 * R-5: Room settings + playlist sync (protocol v35 additive)
 *
 * SVC_ROOM_SETTINGS  0x78  server→room: match settings changed by leader
 * SVC_ROOM_PLAYLIST  0x79  server→room: mod playlist changed by leader
 * CLC_ROOM_SETTINGS_UPDATE 0x13  leader→server: push current match settings
 * CLC_ROOM_PLAYLIST_UPDATE 0x14  leader→server: push current mod playlist
 *
 * Wire format for settings payload (shared between SVC and CLC):
 *   numBots       u8
 *   timelimit     u8
 *   scorelimit    u8
 *   teamscorelimit u16
 *   options       u32
 *   scenario      u8
 *   weaponSetIdx  u8  (0xFF = custom)
 *   stage_id      str
 *
 * Wire format for playlist payload:
 *   playlist_str  str  (semicolon-delimited catalog IDs)
 * ======================================================================== */

/* ---- SVC_ROOM_SETTINGS ---- */

u32 netmsgSvcRoomSettingsWrite(struct netbuf *dst, u8 numBots, u8 timelimit,
                                u8 scorelimit, u16 teamscorelimit, u32 options,
                                u8 scenario, u8 weaponSetIndex, const char *stage_id)
{
	netbufWriteU8(dst, SVC_ROOM_SETTINGS);
	netbufWriteU8(dst, numBots);
	netbufWriteU8(dst, timelimit);
	netbufWriteU8(dst, scorelimit);
	netbufWriteU16(dst, teamscorelimit);
	netbufWriteU32(dst, options);
	netbufWriteU8(dst, scenario);
	netbufWriteU8(dst, weaponSetIndex);
	netbufWriteStr(dst, stage_id ? stage_id : "");
	return dst->error;
}

u32 netmsgSvcRoomSettingsRead(struct netbuf *src, struct netclient *srccl)
{
	u8  numBots        = netbufReadU8(src);
	u8  timelimit      = netbufReadU8(src);
	u8  scorelimit     = netbufReadU8(src);
	u16 teamscorelimit = netbufReadU16(src);
	u32 options        = netbufReadU32(src);
	u8  scenario       = netbufReadU8(src);
	u8  weaponSetIndex = netbufReadU8(src);
	const char *stage_id = netbufReadStr(src);
	if (src->error) return src->error;

#if !defined(PD_SERVER)
	/* Apply to local g_MatchConfig shadow so the room UI reflects host changes. */
	g_MatchConfig.timelimit      = timelimit;
	g_MatchConfig.scorelimit     = scorelimit;
	g_MatchConfig.teamscorelimit = teamscorelimit;
	matchConfigReplaceUserOptions(options, "SVC_ROOM_SETTINGS");
	g_MatchConfig.scenario       = scenario;
	g_MatchConfig.weaponSetIndex = (s8)weaponSetIndex;
	if (stage_id && stage_id[0]) {
		snprintf(g_MatchConfig.stage_id, sizeof(g_MatchConfig.stage_id), "%s", stage_id);
	}

	/* Rebuild slot array to match numBots.
	 * Slot 0 = local player, slots 1..numBots = bots (defaults only —
	 * full per-bot config is deferred to R-4 CLC_ROOM_SETTINGS). */
	s32 humanCount = matchConfigCountHumans();
	s32 clampedBots = (s32)numBots;
	s32 maxBots = matchConfigMaxBotsForHumans(humanCount);
	if (clampedBots > maxBots) clampedBots = maxBots;
	s32 oldNumSlots = g_MatchConfig.numSlots;
	g_MatchConfig.numSlots = (u8)(1 + clampedBots);
	g_MatchConfig.slots[0].type = SLOT_PLAYER;
	for (s32 i = 1; i <= clampedBots; i++) {
		if (g_MatchConfig.slots[i].type == SLOT_EMPTY) {
			g_MatchConfig.slots[i].type          = SLOT_BOT;
			g_MatchConfig.slots[i].botDifficulty = 2; /* NormalSim */
			g_MatchConfig.slots[i].team          =
				(options & MPOPTION_TEAMSENABLED)
					? matchConfigChooseBotTeam(2)
					: 0;
			g_MatchConfig.slots[i].name[0]       = '\0';
		}
	}
	/* S-12: clear stale bot slots beyond the new count when numBots decreases.
	 * Without this, SLOT_BOT entries at indices >= new numSlots retain stale
	 * data that any full-array scan could pick up as ghost bots. */
	for (s32 i = 1 + clampedBots; i < oldNumSlots && i < MATCH_MAX_SLOTS; i++) {
		g_MatchConfig.slots[i].type = SLOT_EMPTY;
		g_MatchConfig.slots[i].name[0] = '\0';
	}

	sysLogPrintf(LOG_NOTE,
	    "NET: SVC_ROOM_SETTINGS: numBots=%u tl=%u sc=%u opts=0x%08x wpn=%u stage='%s'",
	    numBots, timelimit, scorelimit, options, weaponSetIndex,
	    stage_id ? stage_id : "");
#else
	(void)numBots; (void)timelimit; (void)scorelimit; (void)teamscorelimit;
	(void)options; (void)scenario; (void)weaponSetIndex; (void)stage_id;
#endif
	return src->error;
}

/* ---- SVC_ROOM_PLAYLIST ---- */

u32 netmsgSvcRoomPlaylistWrite(struct netbuf *dst, const char *playlist_str)
{
	netbufWriteU8(dst, SVC_ROOM_PLAYLIST);
	netbufWriteStr(dst, playlist_str ? playlist_str : "");
	return dst->error;
}

u32 netmsgSvcRoomPlaylistRead(struct netbuf *src, struct netclient *srccl)
{
	const char *pl = netbufReadStr(src);
	if (src->error) return src->error;

#if !defined(PD_SERVER)
	audioClearModPlaylist();
	if (pl && pl[0]) {
		/* pl is semicolon-delimited: "mod:track1;mod:track2;..." */
		char buf[AUDIO_MAX_PLAYLIST * 65];
		snprintf(buf, sizeof(buf), "%s", pl);
		/* SEC-21: strtok_r avoids the shared-static-state hazard of strtok. */
		char *saveptr = NULL;
#if defined(_WIN32)
		char *tok = strtok_s(buf, ";", &saveptr);
#else
		char *tok = strtok_r(buf, ";", &saveptr);
#endif
		while (tok) {
			audioAddModPlaylistEntry(tok);
#if defined(_WIN32)
			tok = strtok_s(NULL, ";", &saveptr);
#else
			tok = strtok_r(NULL, ";", &saveptr);
#endif
		}
	}
	sysLogPrintf(LOG_NOTE, "NET: SVC_ROOM_PLAYLIST: %d tracks loaded",
	             audioGetModPlaylistCount());
#else
	(void)pl;
#endif
	return src->error;
}

/* ---- CLC_ROOM_SETTINGS_UPDATE ---- */

u32 netmsgClcRoomSettingsUpdateWrite(struct netbuf *dst, u8 numBots, u8 timelimit,
                                      u8 scorelimit, u16 teamscorelimit, u32 options,
                                      u8 scenario, u8 weaponSetIndex, const char *stage_id)
{
	netbufWriteU8(dst, CLC_ROOM_SETTINGS_UPDATE);
	netbufWriteU8(dst, numBots);
	netbufWriteU8(dst, timelimit);
	netbufWriteU8(dst, scorelimit);
	netbufWriteU16(dst, teamscorelimit);
	netbufWriteU32(dst, options);
	netbufWriteU8(dst, scenario);
	netbufWriteU8(dst, weaponSetIndex);
	netbufWriteStr(dst, stage_id ? stage_id : "");
	return dst->error;
}

u32 netmsgClcRoomSettingsUpdateRead(struct netbuf *src, struct netclient *srccl)
{
	u8  numBots        = netbufReadU8(src);
	u8  timelimit      = netbufReadU8(src);
	u8  scorelimit     = netbufReadU8(src);
	u16 teamscorelimit = netbufReadU16(src);
	u32 options        = netbufReadU32(src);
	u8  scenario       = netbufReadU8(src);
	u8  weaponSetIndex = netbufReadU8(src);
	const char *stage_id = netbufReadStr(src);
	if (src->error) return src->error;

	if (g_NetMode != NETMODE_SERVER) return src->error;
	if (!srccl) return 1;

	/* Only the room creator (leader) may push settings. */
	if (srccl->room_id == 0xFF) return src->error;
	hub_room_t *room = roomGetById(srccl->room_id);
	if (!room || room->creator_client_id != srccl->id) {
		sysLogPrintf(LOG_NOTE,
		    "NET: CLC_ROOM_SETTINGS_UPDATE from non-leader %u — ignored", srccl->id);
		return src->error;
	}

	/* SEC-12: Validate fields server-side before rebroadcast.
	 * A malicious leader can otherwise crash other clients with crafted values
	 * (out-of-bounds array indices, runaway timers, unknown stage IDs, etc.). */
	if (numBots > MAX_BOTS) {
		sysLogPrintf(LOG_WARNING,
		    "NET: CLC_ROOM_SETTINGS_UPDATE from %u rejected — numBots=%u > MAX_BOTS=%u",
		    srccl->id, numBots, (unsigned)MAX_BOTS);
		return src->error;
	}
	if (timelimit > 240) {
		sysLogPrintf(LOG_WARNING,
		    "NET: CLC_ROOM_SETTINGS_UPDATE from %u rejected — timelimit=%u > 240",
		    srccl->id, timelimit);
		return src->error;
	}
	if (scorelimit > 200) {
		sysLogPrintf(LOG_WARNING,
		    "NET: CLC_ROOM_SETTINGS_UPDATE from %u rejected — scorelimit=%u > 200",
		    srccl->id, scorelimit);
		return src->error;
	}
	if (teamscorelimit > 9999) {
		sysLogPrintf(LOG_WARNING,
		    "NET: CLC_ROOM_SETTINGS_UPDATE from %u rejected — teamscorelimit=%u > 9999",
		    srccl->id, teamscorelimit);
		return src->error;
	}
	if (scenario >= 16) {
		sysLogPrintf(LOG_WARNING,
		    "NET: CLC_ROOM_SETTINGS_UPDATE from %u rejected — scenario=%u >= 16",
		    srccl->id, scenario);
		return src->error;
	}
	if (weaponSetIndex != 0xFF && weaponSetIndex > WEAPONSET_CUSTOM) {
		sysLogPrintf(LOG_WARNING,
		    "NET: CLC_ROOM_SETTINGS_UPDATE from %u rejected — weaponSetIndex=0x%02x out of range",
		    srccl->id, weaponSetIndex);
		return src->error;
	}
	if (stage_id && stage_id[0]) {
		const asset_entry_t *st = assetCatalogResolve(stage_id);
		if (!st || (st->type != ASSET_ARENA && st->type != ASSET_MAP)) {
			sysLogPrintf(LOG_WARNING,
			    "NET: CLC_ROOM_SETTINGS_UPDATE from %u rejected — stage_id '%s' not a valid arena/map",
			    srccl->id, stage_id);
			return src->error;
		}
	}

	sysLogPrintf(LOG_NOTE,
	    "NET: CLC_ROOM_SETTINGS_UPDATE from leader %u: numBots=%u stage='%s'",
	    srccl->id, numBots, stage_id ? stage_id : "");

	for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
		struct netclient *ncl = &g_NetClients[ci];
		if (ncl == srccl) continue;
		if (ncl->state < CLSTATE_LOBBY) continue;
		if (ncl->room_id != srccl->room_id) continue;

		/* netSend resets the source buffer after queueing a packet, so rebuild
		 * the payload per recipient.  Use the normal reliable buffer instead
		 * of the old 256-byte stack packet so catalog ID growth fails loudly. */
		netbufStartWrite(&g_NetMsgRel);
		netmsgSvcRoomSettingsWrite(&g_NetMsgRel, numBots, timelimit, scorelimit,
		                           teamscorelimit, options, scenario,
		                           weaponSetIndex, stage_id);
		if (g_NetMsgRel.error) {
			sysLogPrintf(LOG_WARNING,
			             "NET: CLC_ROOM_SETTINGS_UPDATE failed to encode rebroadcast from client %u",
			             srccl->id);
			netbufStartWrite(&g_NetMsgRel);
			return 1;
		}
		netSend(ncl, &g_NetMsgRel, true, NETCHAN_CONTROL);
	}

	return src->error;
}

/* ---- CLC_ROOM_PLAYLIST_UPDATE ---- */

u32 netmsgClcRoomPlaylistUpdateWrite(struct netbuf *dst, const char *playlist_str)
{
	netbufWriteU8(dst, CLC_ROOM_PLAYLIST_UPDATE);
	netbufWriteStr(dst, playlist_str ? playlist_str : "");
	return dst->error;
}

u32 netmsgClcRoomPlaylistUpdateRead(struct netbuf *src, struct netclient *srccl)
{
	const char *pl = netbufReadStr(src);
	if (src->error) return src->error;

	if (g_NetMode != NETMODE_SERVER) return src->error;
	if (!srccl) return 1;

	if (srccl->room_id == 0xFF) return src->error;
	hub_room_t *room = roomGetById(srccl->room_id);
	if (!room || room->creator_client_id != srccl->id) return src->error;

	/* SEC-22: clamp client-supplied playlist to the rebroadcast buffer capacity
	 * before echoing it. netbufReadStr returns a pointer into the inbound
	 * packet — a hostile client can send a string right up to the receive
	 * cap, which could exceed our AUDIO_MAX_PLAYLIST*65 rebroadcast budget
	 * (and overflow the netbufWriteStr target on receivers). Capping here
	 * limits every server-rebroadcast payload to a known budget. */
	char clamped[AUDIO_MAX_PLAYLIST * 65 - 8];
	if (pl) {
		size_t n = strlen(pl);
		if (n >= sizeof(clamped)) {
			sysLogPrintf(LOG_WARNING,
			    "NET: CLC_ROOM_PLAYLIST_UPDATE from %u: oversized playlist (%zu bytes) — truncating to %zu",
			    srccl->id, n, sizeof(clamped) - 1);
			n = sizeof(clamped) - 1;
		}
		memcpy(clamped, pl, n);
		clamped[n] = '\0';
	} else {
		clamped[0] = '\0';
	}

	sysLogPrintf(LOG_NOTE,
	    "NET: CLC_ROOM_PLAYLIST_UPDATE from leader %u — rebroadcasting", srccl->id);

	for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
		struct netclient *ncl = &g_NetClients[ci];
		if (ncl == srccl) continue;
		if (ncl->state < CLSTATE_LOBBY) continue;
		if (ncl->room_id != srccl->room_id) continue;

		netbufStartWrite(&g_NetMsgRel);
		netmsgSvcRoomPlaylistWrite(&g_NetMsgRel, clamped);
		if (g_NetMsgRel.error) {
			sysLogPrintf(LOG_WARNING,
			             "NET: CLC_ROOM_PLAYLIST_UPDATE failed to encode rebroadcast from client %u",
			             srccl->id);
			netbufStartWrite(&g_NetMsgRel);
			return 1;
		}
		netSend(ncl, &g_NetMsgRel, true, NETCHAN_CONTROL);
	}

	return src->error;
}

/* ---- Convenience senders (called from C++ room screen / audio layer) ---- */

void netSendRoomSettingsUpdate(void)
{
	if (!g_NetLocalClient) return;

	u8 numBots = netmsgCountCurrentRoomBots();
	u8 wpnIdx = (g_MatchConfig.weaponSetIndex >= 0)
	            ? (u8)g_MatchConfig.weaponSetIndex : 0xFF;

	netbufStartWrite(&g_NetLocalClient->out);
	netmsgClcRoomSettingsUpdateWrite(
	    &g_NetLocalClient->out, numBots,
	    g_MatchConfig.timelimit, g_MatchConfig.scorelimit,
	    g_MatchConfig.teamscorelimit, matchConfigGetUserOptions(),
	    g_MatchConfig.scenario, wpnIdx, g_MatchConfig.stage_id);

	if (g_NetMode == NETMODE_CLIENT) {
		netSend(g_NetLocalClient, NULL, true, NETCHAN_CONTROL);
		return;
	}

	/* In-client listen host: run the same server handler as CLC without ENet. */
	if (g_NetMode == NETMODE_SERVER && !g_NetDedicated) {
		struct netbuf rb;
		netbufStartReadData(&rb, g_NetLocalClient->out.data, g_NetLocalClient->out.wp);
		(void)netbufReadU8(&rb);
		netmsgClcRoomSettingsUpdateRead(&rb, g_NetLocalClient);
		netbufStartWrite(&g_NetLocalClient->out);
	}
}

void netSendRoomPlaylistUpdate(void)
{
	if (!g_NetLocalClient) return;

	char pl[AUDIO_MAX_PLAYLIST * 65];
	netmsgBuildCurrentPlaylistString(pl, sizeof(pl));

	netbufStartWrite(&g_NetLocalClient->out);
	netmsgClcRoomPlaylistUpdateWrite(&g_NetLocalClient->out, pl);

	if (g_NetMode == NETMODE_CLIENT) {
		netSend(g_NetLocalClient, NULL, true, NETCHAN_CONTROL);
		return;
	}

	if (g_NetMode == NETMODE_SERVER && !g_NetDedicated) {
		struct netbuf rb;
		netbufStartReadData(&rb, g_NetLocalClient->out.data, g_NetLocalClient->out.wp);
		(void)netbufReadU8(&rb);
		netmsgClcRoomPlaylistUpdateRead(&rb, g_NetLocalClient);
		netbufStartWrite(&g_NetLocalClient->out);
	}
}

/* ============================================================================
 * MASTER-C2b: CLC_ADMIN dispatch + SVC_ADMIN reply.
 *
 * Wire format:
 *   CLC_ADMIN : u8 msgid, u8 subcode, str token, <subcode-specific args>
 *   SVC_ADMIN : u8 msgid, u8 response_code, str message
 *   (v39+: response_code may be ADMIN_RESP_RATE_LIMIT after repeated bad ADMIN_AUTH.)
 *
 * Authentication model:
 *   - ADMIN_AUTH sub compares the supplied token against the configured
 *     hash via serverAdminVerifyToken().  On success, cl->is_admin is set
 *     for the lifetime of the peer connection.  Subsequent messages from
 *     that peer bypass the token check (but the token field is still
 *     parsed so the wire shape is uniform).
 *   - All other subs require cl->is_admin; otherwise reply BAD_TOKEN.
 *
 * Declared forward here to avoid pulling server_bridge internals into the
 * client build.  Both binaries link a local implementation (server_bridge.c
 * on the server, pdgui_bridge.c on the client listen host). */
extern void netServerKickClient(s32 clientId, const char *reason);
extern void netServerBanClient(s32 clientId, const char *reason);

/* Helper: send an SVC_ADMIN reply with the given code and message. */
static void netmsgSvcAdminReply(struct netclient *cl, u8 code, const char *message)
{
	if (!cl || !cl->peer) return;
	netbufStartWrite(&cl->out);
	netbufWriteU8(&cl->out, SVC_ADMIN);
	netbufWriteU8(&cl->out, code);
	netbufWriteStr(&cl->out, message ? message : "");
	netSend(cl, NULL, true, NETCHAN_CONTROL);
}

u32 netmsgClcAdminRead(struct netbuf *src, struct netclient *srccl)
{
	if (g_NetMode != NETMODE_SERVER) {
		/* Silently ignore on clients — shouldn't happen under normal use. */
		/* Still need to drain the buffer to keep the decoder in sync, so
		 * read and discard each field by calling into the fast-path parser. */
	}

	const u8 subcode = netbufReadU8(src);
	const char *tokenStr = netbufReadStr(src);

	/* Copy token immediately — further reads on src may clobber the buffer. */
	char token[128];
	if (tokenStr) {
		strncpy(token, tokenStr, sizeof(token) - 1);
		token[sizeof(token) - 1] = '\0';
	} else {
		token[0] = '\0';
	}

	if (src->error) {
		sysLogPrintf(LOG_WARNING, "NET: malformed CLC_ADMIN from client %u", srccl->id);
		return 1;
	}

	if (g_NetMode != NETMODE_SERVER) {
		return 0;
	}

	/* ADMIN_AUTH: promote the peer to admin if the token hashes correctly. */
	if (subcode == ADMIN_SUB_AUTH) {
		u32 cidx = (u32)(srccl - g_NetClients);
		if (cidx > (u32)NET_MAX_CLIENTS) {
			return 1;
		}
		u32 now = SDL_GetTicks();
		char ipBuf[SERVER_BANS_ADDR_LEN];
		netmsgAdminExtractPeerIp(srccl, ipBuf, sizeof(ipBuf));

		int auth_ok = 0;
		if (serverAdminEnabled() && serverAdminVerifyToken(token)) {
			auth_ok = 1;
		}

		if (!auth_ok) {
			int limC = adminauthfail_on_failure(&s_AdminAuthFailClient[cidx], now);
			int limI = 0;
			if (ipBuf[0]) {
				u32 h = adminauth_ip_hash_str(ipBuf);
				limI = adminauthfail_on_failure(adminauth_ip_bucket(h), now);
			}
			if (limC || limI) {
				netmsgSvcAdminReply(srccl, ADMIN_RESP_RATE_LIMIT,
				                    "Too many failed admin authentication attempts.");
				if (srccl->peer) {
					netServerKick(srccl, DISCONNECT_ADMIN_AUTH);
				}
				sysLogPrintf(LOG_NOTE,
				             "NET: ADMIN_SUB_AUTH rate limit — disconnect client %u (peer IP %s)",
				             (unsigned)srccl->id, ipBuf[0] ? ipBuf : "?");
				return 0;
			}
			u32 *lastLog = &s_AdminAuthFailLastLogMs[cidx];
			if (*lastLog == 0 || now - *lastLog >= ADMIN_AUTH_FAIL_LOG_INTERVAL_MS) {
				*lastLog = now;
				if (!serverAdminEnabled()) {
					sysLogPrintf(LOG_WARNING, "NET: CLC_ADMIN from %u: RCON not configured",
					             srccl->id);
				} else {
					sysLogPrintf(LOG_WARNING, "NET: CLC_ADMIN auth failed from client %u (%s)",
					             srccl->id, srccl->settings.name);
				}
			}
			srccl->is_admin = false;
			netmsgSvcAdminReply(srccl, ADMIN_RESP_BAD_TOKEN,
			                    serverAdminEnabled() ? "Invalid admin token."
			                                         : "Admin RCON is not enabled on this server.");
			return 0;
		}

		netmsgAdminAuthRateReset(cidx);
		srccl->is_admin = true;
		sysLogPrintf(LOG_NOTE, "NET: admin authenticated client %u (%s)",
		             srccl->id, srccl->settings.name);
		netmsgSvcAdminReply(srccl, ADMIN_RESP_OK, "Admin authenticated.");
		return 0;
	}

	/* All other subcommands require prior authentication. */
	if (!srccl->is_admin) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_ADMIN sub=0x%02x from %u rejected: not authenticated",
		             (unsigned)subcode, srccl->id);
		netmsgSvcAdminReply(srccl, ADMIN_RESP_NOT_AUTH,
		                    "Not authenticated.  Send ADMIN_AUTH with a valid token first.");
		return 0;
	}

	switch (subcode) {

	case ADMIN_SUB_KICK: {
		const u8 targetId = netbufReadU8(src);
		const char *reason = netbufReadStr(src);
		if (src->error) return 1;
		if (targetId >= NET_MAX_CLIENTS || !g_NetClients[targetId].peer) {
			netmsgSvcAdminReply(srccl, ADMIN_RESP_NOT_FOUND, "No such client.");
			return 0;
		}
		struct netclient *tgt = &g_NetClients[targetId];
		sysLogPrintf(LOG_NOTE, "ADMIN: client %u kicked by admin %u — reason: %s",
		             (unsigned)targetId, (unsigned)srccl->id,
		             reason && reason[0] ? reason : "(none)");
		netServerKickClient((s32)targetId, reason && reason[0] ? reason : "kicked by admin");
		(void)tgt;
		netmsgSvcAdminReply(srccl, ADMIN_RESP_OK, "Kicked.");
		return 0;
	}

	case ADMIN_SUB_BAN: {
		const u8 targetId = netbufReadU8(src);
		const char *reason = netbufReadStr(src);
		char reasonBuf[SERVER_BANS_REASON_LEN];
		if (reason) {
			strncpy(reasonBuf, reason, sizeof(reasonBuf) - 1);
			reasonBuf[sizeof(reasonBuf) - 1] = '\0';
		} else {
			reasonBuf[0] = '\0';
		}
		if (src->error) return 1;
		if (targetId >= NET_MAX_CLIENTS || !g_NetClients[targetId].peer) {
			netmsgSvcAdminReply(srccl, ADMIN_RESP_NOT_FOUND, "No such client.");
			return 0;
		}
		struct netclient *tgt = &g_NetClients[targetId];

		/* Extract IP address from ENet peer and persist the ban. */
		char addrBuf[SERVER_BANS_ADDR_LEN];
		addrBuf[0] = '\0';
		if (tgt->peer) {
			const char *fmt = netFormatClientAddr(tgt);
			if (fmt) {
				/* fmt is "ip:port" — strip the port for ban matching. */
				const char *colon = strrchr(fmt, ':');
				const char *lbracket = strchr(fmt, '[');
				if (lbracket && colon > lbracket) {
					/* IPv6 form [addr]:port — keep the bracketed addr. */
					const char *rbracket = strchr(fmt, ']');
					if (rbracket && rbracket > lbracket + 1) {
						size_t n = (size_t)(rbracket - (lbracket + 1));
						if (n >= sizeof(addrBuf)) n = sizeof(addrBuf) - 1;
						memcpy(addrBuf, lbracket + 1, n);
						addrBuf[n] = '\0';
					}
				} else if (colon) {
					size_t n = (size_t)(colon - fmt);
					if (n >= sizeof(addrBuf)) n = sizeof(addrBuf) - 1;
					memcpy(addrBuf, fmt, n);
					addrBuf[n] = '\0';
				} else {
					strncpy(addrBuf, fmt, sizeof(addrBuf) - 1);
					addrBuf[sizeof(addrBuf) - 1] = '\0';
				}
			}
		}

		if (addrBuf[0]) {
			serverBansAdd(addrBuf, tgt->settings.name, reasonBuf);
		} else {
			sysLogPrintf(LOG_WARNING, "ADMIN: ban %u — could not resolve address; kicking without persisting ban",
			             (unsigned)targetId);
		}

		sysLogPrintf(LOG_NOTE, "ADMIN: client %u (%s @ %s) banned by admin %u — reason: %s",
		             (unsigned)targetId, tgt->settings.name,
		             addrBuf[0] ? addrBuf : "?", (unsigned)srccl->id,
		             reasonBuf[0] ? reasonBuf : "(none)");
		netServerKickClient((s32)targetId, reasonBuf[0] ? reasonBuf : "banned by admin");
		netmsgSvcAdminReply(srccl, ADMIN_RESP_OK, "Banned.");
		return 0;
	}

	case ADMIN_SUB_UNBAN: {
		const char *addr = netbufReadStr(src);
		if (src->error) return 1;
		if (!addr || !addr[0]) {
			netmsgSvcAdminReply(srccl, ADMIN_RESP_BAD_ARG, "Missing address.");
			return 0;
		}
		if (serverBansRemove(addr)) {
			sysLogPrintf(LOG_NOTE, "ADMIN: %s unbanned by admin %u", addr, (unsigned)srccl->id);
			netmsgSvcAdminReply(srccl, ADMIN_RESP_OK, "Unbanned.");
		} else {
			netmsgSvcAdminReply(srccl, ADMIN_RESP_NOT_FOUND, "Address not in ban list.");
		}
		return 0;
	}

	case ADMIN_SUB_LIST: {
		char payload[ADMIN_PAYLOAD_MAX];
		int off = 0;
		int trunc = 0;
		s32 nban = serverBansGetCount();
		if (nban <= 0) {
			netmsgAdminPayloadVfmt(payload, sizeof(payload), &off, &trunc, "%s", "(no bans)\n");
		} else {
			for (s32 bi = 0; bi < nban && !trunc; bi++) {
				const server_ban_entry_t *e = serverBansGetEntry(bi);
				if (!e) {
					break;
				}
				netmsgAdminPayloadVfmt(payload, sizeof(payload), &off, &trunc,
				                       "%s  %s  %s\n",
				                       e->addr,
				                       e->name[0] ? e->name : "?",
				                       e->reason[0] ? e->reason : "no reason");
			}
		}
		if (trunc && off >= 0 && (size_t)off < sizeof(payload)) {
			snprintf(payload + off, sizeof(payload) - (size_t)off, "\n(truncated)\n");
		}
		netbufStartWrite(&srccl->out);
		netbufWriteU8(&srccl->out, SVC_ADMIN);
		netbufWriteU8(&srccl->out, ADMIN_RESP_LIST);
		netbufWriteStr(&srccl->out, payload);
		netSend(srccl, NULL, true, NETCHAN_CONTROL);
		return 0;
	}

	case ADMIN_SUB_STATUS: {
		char payload[ADMIN_PAYLOAD_MAX];
		int off = 0;
		int trunc = 0;
		netmsgAdminPayloadVfmt(payload, sizeof(payload), &off, &trunc,
		                       "Players: %d / %d  Tick: %u  Mode: %d\n",
		                       g_NetNumClients, g_NetMaxClients,
		                       (unsigned)g_NetTick, (int)g_NetGameMode);
		for (s32 ci = 0; ci < NET_MAX_CLIENTS && !trunc; ci++) {
			struct netclient *ncl = &g_NetClients[ci];
			if (ncl->state == CLSTATE_DISCONNECTED) {
				continue;
			}
			const char *addr = ncl->peer ? netFormatClientAddr(ncl) : "<local>";
			netmsgAdminPayloadVfmt(payload, sizeof(payload), &off, &trunc,
			                       "  [%d] %s  state=%u  room=%u  addr=%s\n",
			                       (int)ci,
			                       ncl->settings.name[0] ? ncl->settings.name : "?",
			                       (unsigned)ncl->state,
			                       (unsigned)ncl->room_id,
			                       addr ? addr : "?");
		}
		if (trunc && off >= 0 && (size_t)off < sizeof(payload)) {
			snprintf(payload + off, sizeof(payload) - (size_t)off, "\n(truncated)\n");
		}
		netbufStartWrite(&srccl->out);
		netbufWriteU8(&srccl->out, SVC_ADMIN);
		netbufWriteU8(&srccl->out, ADMIN_RESP_STATUS);
		netbufWriteStr(&srccl->out, payload);
		netSend(srccl, NULL, true, NETCHAN_CONTROL);
		return 0;
	}

	default:
		sysLogPrintf(LOG_WARNING, "NET: CLC_ADMIN unknown subcode 0x%02x from %u",
		             (unsigned)subcode, (unsigned)srccl->id);
		netmsgSvcAdminReply(srccl, ADMIN_RESP_BAD_ARG, "Unknown admin subcode.");
		return 0;
	}
}
