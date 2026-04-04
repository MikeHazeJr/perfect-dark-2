#include <string.h>
#include <stdio.h>
#include "types.h"
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
#include "romdata.h"
#include "lib/vi.h"
#include "fs.h"
#include "console.h"
#include "net/net.h"
#include "net/netbuf.h"
#include "net/netmsg.h"
#include "net/netlobby.h"
#include "net/netdistrib.h"
#include "net/netmanifest.h"
#include "net/matchsetup.h"
#include "net/sessioncatalog.h"
#include "room.h"
#include "scenario_save.h"
#include "assetcatalog.h"
#if !defined(PD_SERVER)
#include "modelcatalog.h"
#include "game/mplayer/scenarios.h"
#include "input.h"
#endif
#include "identity.h"
#include "utils.h"
#if !defined(PD_SERVER)
#include "pdgui.h"
#include "pdmain.h"
#endif
#include <SDL.h>

/* Desync detection and resync constants */
#define NET_DESYNC_THRESHOLD   3   // consecutive desyncs before requesting resync
#define NET_RESYNC_COOLDOWN    300 // minimum frames between resync requests (~5 sec at 60fps)

/* Phase E: Ready gate — timeout before forcing SVC_STAGE_START (30s at 60fps) */
#define READY_GATE_TIMEOUT_TICKS 1800

/* Chat rate limiter: max 5 messages per 2-second window per client.
 * Keyed by client index (0..NET_MAX_CLIENTS). */
#define CHAT_RATE_MAX_MSGS   5
#define CHAT_RATE_WINDOW_MS  2000u

struct chatrate {
	u32 timestamps[CHAT_RATE_MAX_MSGS]; /* circular ring of send times (ms) */
	u32 head;                           /* next slot to overwrite */
};
static struct chatrate s_ChatRate[NET_MAX_CLIENTS + 1];

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
	/* Phase F: countdown */
	s32 countdown_active;    /* 1 during the 3-second pre-launch countdown */
	u32 countdown_next_tick; /* g_NetTick at which to decrement + re-broadcast */
	u8  countdown_secs;      /* current countdown value (3→2→1→0) */
	/* R-3: which room this ready gate belongs to */
	u8  room_id;             /* 0xFF = global (no room), else room slot index */
} s_ReadyGate;

/* Phase F: Client-side countdown display state.
 * Updated by netmsgSvcMatchCountdownRead().  UI reads this to display
 * "Match starting in N..." during MANIFEST_PHASE_LOADING. */
struct match_countdown_state g_MatchCountdownState;

/* Phase F: Client-side cancellation state.
 * Updated by netmsgSvcMatchCancelledRead(). UI reads this to display
 * "[Name] cancelled the match start". */
struct match_cancelled_state g_MatchCancelledState;

/* Desync tracking state (client-side) */
static u32 g_NetChrDesyncCount = 0;
static u32 g_NetChrResyncLastReq = 0;
static u32 g_NetPropDesyncCount = 0;
static u32 g_NetPropResyncLastReq = 0;
static u32 g_NetNpcDesyncCount = 0;
static u32 g_NetNpcResyncLastReq = 0;

/* Server-side resync request tracking (set by CLC_RESYNC_REQ, consumed by netEndFrame in net.c) */
u8 g_NetPendingResyncFlags = 0;

/* Prop syncid → prop* lookup map.
 * syncids are assigned as (prop - g_Vars.props + 1), so they're 1-indexed array offsets.
 * Direct indexing gives O(1) lookup; 2048 exceeds any expected maxprops value by a large margin.
 * Map entries are validated by checking prop->syncid == syncid before returning. */
#define NET_PROP_MAP_SIZE 2048
static struct prop *s_PropBySyncId[NET_PROP_MAP_SIZE];

void netSyncIdMapClear(void)
{
    memset(s_PropBySyncId, 0, sizeof(s_PropBySyncId));
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

/* Client-side resync request tracking (set by SVC_CHR/PROP/NPC_SYNC handlers on desync, consumed by netEndFrame).
 * These flags CANNOT be written directly to g_NetMsgRel inside netStartFrame() recv handlers because
 * netStartFrame() resets g_NetMsgRel after the event loop — any write during dispatch is silently dropped.
 * The fix mirrors the server-side pattern: set a flag in the handler, write the message in netEndFrame. */
u8 g_NetPendingResyncReqFlags = 0;

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
	netbufWriteData(buf, gset, sizeof(*gset));
	return buf->error;
}

static inline u32 netbufReadGset(struct netbuf *buf, struct gset *gset)
{
	netbufReadData(buf, gset, sizeof(*gset));
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
	netbufReadCoord(buf, &in->pos);
	if (in->ucmd & UCMD_AIMMODE) {
		in->zoomfov = netbufReadF32(buf);
	} else {
		in->zoomfov = 0.f;
	}
	return buf->error;
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

	// Fast path: O(1) direct lookup via s_PropBySyncId map (built by netSyncIdMapRebuild)
	if (syncid < NET_PROP_MAP_SIZE) {
		struct prop *p = s_PropBySyncId[syncid];
		if (p && p->syncid == syncid) {
			return p;
		}
	}

	// Slow path: linear scan fallback for syncids beyond map range or stale map
	for (s32 i = 0; i < g_Vars.maxprops; ++i) {
		if (g_Vars.props[i].syncid == syncid) {
			return &g_Vars.props[i];
		}
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

u32 netmsgClcAuthWrite(struct netbuf *dst)
{
	const char *modDir = fsGetModDir();
	if (!modDir) {
		modDir = "";
	}

	// Use identity profile name (authoritative for PC); fall back to settings name
	const char *name = g_NetLocalClient->settings.name;
	identity_profile_t *profile = identityGetActiveProfile();
	if (profile && profile->name[0]) {
		name = profile->name;
	}

	netbufWriteU8(dst, CLC_AUTH);
	netbufWriteStr(dst, name);
	netbufWriteU32(dst, utilCrc32(g_RomName)); // CRC32 of ROM identifier — avoids leaking the name string and is more compact
	netbufWriteStr(dst, modDir);
	netbufWriteU8(dst, (u8)PLAYERCOUNT()); // number of local (splitscreen) players on this client

	return dst->error;
}

u32 netmsgClcAuthRead(struct netbuf *src, struct netclient *srccl)
{
	if (srccl->state != CLSTATE_AUTH) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_AUTH from client %u, who is not in CLSTATE_AUTH", srccl->id);
		return 1;
	}

	char *name = netbufReadStr(src);
	const u32 romCrc = netbufReadU32(src); // CRC32 of client's g_RomName
	const char *modDir = netbufReadStr(src);
	const u8 players = netbufReadU8(src);

	if (src->error) {
		sysLogPrintf(LOG_WARNING, "NET: malformed CLC_AUTH from client %u", srccl->id);
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

		if (modDir && modDir[0] == '\0') {
			modDir = NULL;
		}

		const char *myModDir = fsGetModDir();
		if ((!myModDir != !modDir) || (myModDir && modDir && strcasecmp(modDir, myModDir) != 0)) {
			sysLogPrintf(LOG_WARNING, "NET: CLC_AUTH: client %u has the wrong mod, disconnecting", srccl->id);
			netServerKick(srccl, DISCONNECT_FILES);
			return src->error;
		}
	}

	// zero-init client settings as a baseline (remote will send CLC_SETTINGS after CLC_AUTH)
	if (g_NetLocalClient) {
		srccl->settings = g_NetLocalClient->settings;
	} else {
		memset(&srccl->settings, 0, sizeof(srccl->settings));
		srccl->settings.team = 0xff;
	}
	strncpy(srccl->settings.name, name ? name : "Player", sizeof(srccl->settings.name) - 1);
	srccl->settings.name[sizeof(srccl->settings.name) - 1] = '\0';

	sysLogPrintf(LOG_NOTE, "NET: CLC_AUTH from client %u (%s), responding", srccl->id, srccl->settings.name);

	// check if this is a mid-game reconnection
	struct netpreservedplayer *pp = NULL;
	const bool ingame = (g_NetLocalClient && g_NetLocalClient->state >= CLSTATE_GAME);
	if (ingame) {
		pp = netServerFindPreserved(name);
		if (!pp) {
			// no preserved slot found — reject late join
			sysLogPrintf(LOG_NOTE, "NET: %s rejected: no preserved slot for mid-game join", name);
			netServerKick(srccl, DISCONNECT_LATE);
			return src->error;
		}
	}

	srccl->state = CLSTATE_LOBBY;

	/* Send SVC_AUTH first so the client transitions to CLSTATE_LOBBY before receiving
	 * any further messages. Catalog info must come after auth — the client must be
	 * authenticated before it can meaningfully respond to catalog requests. */
	netbufStartWrite(&srccl->out);
	netmsgSvcAuthWrite(&srccl->out, srccl);
	netSend(srccl, NULL, true, NETCHAN_CONTROL);

	/* D3R-9: send catalog info so the client can diff and request missing components.
	 * Sent after SVC_AUTH so the client is in CLSTATE_LOBBY when it processes this. */
	netDistribServerSendCatalogInfo(srccl);

	if (pp) {
		// reconnecting player: restore identity, scores, and send full state
		netServerRestorePreserved(srccl, pp);
		srccl->flags &= ~CLFLAG_ABSENT;

		// send stage start so the client loads the stage
		netbufStartWrite(&srccl->out);
		netmsgSvcStageStartWrite(&srccl->out);
		netSend(srccl, NULL, true, NETCHAN_DEFAULT);

		// schedule full resync for next frame
		g_NetPendingResyncFlags |= NET_RESYNC_FLAG_CHRS | NET_RESYNC_FLAG_PROPS | NET_RESYNC_FLAG_SCORES;
		if (g_NetGameMode == NETGAMEMODE_COOP || g_NetGameMode == NETGAMEMODE_ANTI) {
			g_NetPendingResyncFlags |= NET_RESYNC_FLAG_NPCS;
		}

		sysLogPrintf(LOG_NOTE, "NET: %s (%u) reconnected (playernum %u)", srccl->settings.name, srccl->id, srccl->playernum);
		netChatPrintf(NULL, "%s reconnected", name);
	} else {
		sysLogPrintf(LOG_NOTE, "NET: %s (%u) joined", srccl->settings.name, srccl->id);
		netChatPrintf(NULL, "%s joined", name);
	}

	/* Update lobby state and broadcast SVC_LOBBY_LEADER to all lobby clients.
	 * This tells each client who the current leader is (critical for the joining
	 * client to know if they are the leader, and for existing clients to know if
	 * the leader changed).  lobbyUpdate() must run first to elect the leader. */
	lobbyUpdate();
	if (g_Lobby.leaderSlot != 0xFF && g_Lobby.leaderSlot < g_Lobby.numPlayers) {
		u8 leaderClientId = g_Lobby.players[g_Lobby.leaderSlot].clientId;
		for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
			struct netclient *ncl = &g_NetClients[ci];
			if (ncl->state >= CLSTATE_LOBBY) {
				netbufStartWrite(&ncl->out);
				netmsgSvcLobbyLeaderWrite(&ncl->out, leaderClientId);
				netSend(ncl, NULL, true, NETCHAN_CONTROL);
			}
		}
		sysLogPrintf(LOG_NOTE, "NET: broadcast SVC_LOBBY_LEADER: client %u is leader", leaderClientId);
	}

	/* R-3: Send initial room list to the newly authenticated client */
	netBroadcastRoomList();

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

	if (srccl->state != CLSTATE_GAME) {
		// silently ignore
		return src->error;
	}

	srccl->outmoveack = outmoveack;

	if (!src->error) {
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
	}

	return src->error;
}

u32 netmsgClcSettingsWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, CLC_SETTINGS);
	netbufWriteU16(dst, g_NetLocalClient->settings.options);
	/* SA-3: body/head as session IDs (0 if catalog not yet active — server skips update) */
	catalogWriteAssetRef(dst, sessionCatalogGetId(g_NetLocalClient->settings.body_id));
	catalogWriteAssetRef(dst, sessionCatalogGetId(g_NetLocalClient->settings.head_id));
	netbufWriteU8(dst, g_NetLocalClient->settings.team);
	netbufWriteF32(dst, g_NetLocalClient->settings.fovy);
	netbufWriteF32(dst, g_NetLocalClient->settings.fovzoommult);
	/* Use identity profile name (same check as CLC_AUTH) so CLC_SETTINGS
	 * doesn't overwrite the correct identity name on the server with a stale
	 * legacy fallback when netClientReadConfig ran before identity was ready. */
	{
		const char *name = g_NetLocalClient->settings.name;
		identity_profile_t *profile = identityGetActiveProfile();
		if (profile && profile->name[0]) {
			name = profile->name;
			/* Keep settings.name in sync so future CLC_SETTINGS sends are consistent. */
			strncpy(g_NetLocalClient->settings.name, name, sizeof(g_NetLocalClient->settings.name) - 1);
			g_NetLocalClient->settings.name[sizeof(g_NetLocalClient->settings.name) - 1] = '\0';
		}
		netbufWriteStr(dst, name);
	}
	return dst->error;
}

u32 netmsgClcSettingsRead(struct netbuf *src, struct netclient *srccl)
{
	const u16 options = netbufReadU16(src);
	/* SA-3: body/head arrive as session IDs; 0 means catalog not yet active, skip update */
	const u16 body_session = catalogReadAssetRef(src);
	const u16 head_session = catalogReadAssetRef(src);
	const u8 team = netbufReadU8(src);
	const f32 fovy = netbufReadF32(src);
	const f32 fovzoommult = netbufReadF32(src);
	char *name = netbufReadStr(src);

	if (src->error) {
		sysLogPrintf(LOG_WARNING, "NET: malformed CLC_SETTINGS from client %u", srccl->id);
		netServerKick(srccl, DISCONNECT_KICKED);
		return 1;
	}

	if (srccl->settings.name[0] && strncmp(srccl->settings.name, name, MAX_PLAYERNAME) != 0) {
		netChatPrintf(NULL, "%s is now known as %s", srccl->settings.name, name);
	}

	/* Resolve body/head session IDs to catalog string IDs.
	 * Session 0 = catalog not yet active (lobby phase) — keep existing id. */
	if (body_session > 0) {
		const asset_entry_t *be = sessionCatalogLocalResolve(body_session);
		if (be) {
			if (strncmp(srccl->settings.body_id, be->id, CATALOG_ID_LEN) != 0) {
				sysLogPrintf(LOG_NOTE, "NET: CLC_SETTINGS client %u body '%s' -> '%s'",
				             srccl->id, srccl->settings.body_id, be->id);
			}
			strncpy(srccl->settings.body_id, be->id, CATALOG_ID_LEN - 1);
			srccl->settings.body_id[CATALOG_ID_LEN - 1] = '\0';
		}
	}
	if (head_session > 0) {
		const asset_entry_t *he = sessionCatalogLocalResolve(head_session);
		if (he) {
			if (strncmp(srccl->settings.head_id, he->id, CATALOG_ID_LEN) != 0) {
				sysLogPrintf(LOG_NOTE, "NET: CLC_SETTINGS client %u head '%s' -> '%s'",
				             srccl->id, srccl->settings.head_id, he->id);
			}
			strncpy(srccl->settings.head_id, he->id, CATALOG_ID_LEN - 1);
			srccl->settings.head_id[CATALOG_ID_LEN - 1] = '\0';
		}
	}

	strncpy(srccl->settings.name, name, sizeof(srccl->settings.name) - 1);
	srccl->settings.name[sizeof(srccl->settings.name) - 1] = '\0';
	srccl->settings.options = options;
	srccl->settings.fovy = fovy;
	srccl->settings.fovzoommult = fovzoommult;

	// apply team change if in-game
	if (team != srccl->settings.team && srccl->state >= CLSTATE_GAME && srccl->config) {
		sysLogPrintf(LOG_NOTE, "NET: client %u (%s) switched to team %u", srccl->id, srccl->settings.name, team);
		netChatPrintf(NULL, "%s switched teams", srccl->settings.name);
		srccl->config->base.team = team;
	}
	srccl->settings.team = team;

	return src->error;
}

/* server -> client */

u32 netmsgSvcAuthWrite(struct netbuf *dst, struct netclient *authcl)
{
	netbufWriteU8(dst, SVC_AUTH);
	netbufWriteU8(dst, authcl - g_NetClients);
	netbufWriteU8(dst, g_NetMaxClients);
	netbufWriteU32(dst, g_NetTick);
	return dst->error;
}

u32 netmsgSvcAuthRead(struct netbuf *src, struct netclient *srccl)
{
	if (g_NetLocalClient->state != CLSTATE_AUTH) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_AUTH from server but we're not in AUTH state");
		return 1;
	}

	const u8 id = netbufReadU8(src);
	const u8 maxclients = netbufReadU8(src);
	g_NetTick = netbufReadU32(src);
	if (g_NetLocalClient->in.error || id == NET_NULL_CLIENT || id >= NET_MAX_CLIENTS
			|| maxclients == 0 || maxclients > NET_MAX_CLIENTS) {
		sysLogPrintf(LOG_WARNING, "NET: malformed SVC_AUTH from server (id=%u maxclients=%u)", id, maxclients);
		return 1;
	}

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
	 * a later disconnect reopens at the top level, not "Online Play". */
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
	char tmp[1024];
	const char *msg = netbufReadStr(src);
	if (msg && !src->error) {
		sysLogPrintf(LOG_CHAT, "%s", msg);
	}
	return src->error;
}


u32 netmsgSvcStageStartWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, SVC_STAGE_START);

	netbufWriteU32(dst, g_NetTick);

	netbufWriteU64(dst, g_RngSeed);
	netbufWriteU64(dst, g_Rng2Seed);

	/* SA-3: stage as session ID. session_id=0 signals "return to lobby" on the client.
	 * Use g_MpSetup.stage_id (the primary catalog ID) directly — no reverse-resolve
	 * from stagenum needed.  The dedicated server has no ASSET_MAP entries so
	 * stagenum-based lookups fail there; catalog IDs work everywhere. */
	if (g_MpSetup.stage_id[0] == '\0') {
		sysLogPrintf(LOG_WARNING, "MATCH-START: WARNING stage_id is EMPTY at SVC_STAGE_START write");
		catalogWriteAssetRef(dst, 0);
		return dst->error;
	}
	{
		u16 sid = sessionCatalogGetId(g_MpSetup.stage_id);
		if (!sid) {
			sysLogPrintf(LOG_WARNING, "SVC_STARTGAME: stage '%s' not in session catalog",
			             g_MpSetup.stage_id);
			catalogWriteAssetRef(dst, 0);
		} else {
			catalogWriteAssetRef(dst, sid);
		}
		sysLogPrintf(LOG_WARNING, "MATCH-START: writing SVC_STAGE_START, stage_session=%d, stage_id='%s'", (int)sid, g_MpSetup.stage_id);
	}

	// game settings
	netbufWriteU8(dst, g_NetGameMode);

	if (g_NetGameMode == NETGAMEMODE_COOP || g_NetGameMode == NETGAMEMODE_ANTI) {
		// co-op / counter-op mission settings
		netbufWriteU8(dst, g_MissionConfig.difficulty);
		netbufWriteU8(dst, g_NetCoopFriendlyFire);
		netbufWriteU8(dst, g_NetCoopRadar);
	} else {
		// combat simulator settings
		netbufWriteU8(dst, g_MpSetup.scenario);
		netbufWriteU8(dst, g_MpSetup.scorelimit);
		netbufWriteU8(dst, g_MpSetup.timelimit);
		netbufWriteU16(dst, g_MpSetup.teamscorelimit);
		netbufWriteU64(dst, g_MpSetup.chrslots);  /* u64 since protocol v21: 8 players + 32 bots */
		netbufWriteU32(dst, g_MpSetup.options);
		/* SA-3: weapons as session IDs (NUM_MPWEAPONSLOTS u16 entries) */
		{
			s32 wi;
			for (wi = 0; wi < NUM_MPWEAPONSLOTS; wi++) {
				if (g_MpSetup.weapons[wi] == 0) {
					catalogWriteAssetRef(dst, 0);
				} else {
					const char *wcanon = catalogResolveWeaponByGameId(
						(s32)g_MpSetup.weapons[wi]);
					if (wcanon) {
						catalogWriteAssetRef(dst, sessionCatalogGetId(wcanon));
					} else {
						catalogWriteAssetRef(dst, 0);
					}
				}
			}
		}
	}

	// who the fuck is in the game
	netbufWriteU8(dst, g_NetNumClients);
	for (s32 i = 0; i < g_NetMaxClients; ++i) {
		struct netclient *ncl = &g_NetClients[i];
		if (ncl->state) {
			if (ncl->config) {
				ncl->settings.team = ncl->config->base.team;
			}
			netbufWriteU8(dst, ncl->id);
			netbufWriteU8(dst, ncl->playernum);
			netbufWriteU8(dst, ncl->settings.team);
			netbufWriteU16(dst, ncl->settings.options);
			/* SA-3: body/head as session IDs */
			catalogWriteAssetRef(dst, sessionCatalogGetId(ncl->settings.body_id));
			catalogWriteAssetRef(dst, sessionCatalogGetId(ncl->settings.head_id));
			netbufWriteF32(dst, ncl->settings.fovy);
			netbufWriteF32(dst, ncl->settings.fovzoommult);
			netbufWriteStr(dst, ncl->settings.name);
			memset(ncl->inmove, 0, sizeof(ncl->inmove));
			memset(ncl->outmove, 0, sizeof(ncl->outmove));
			ncl->lerpticks = 0;
			ncl->outmoveack = 0;
			ncl->state = CLSTATE_GAME;
		}
	}

	/* Serialize per-bot configs (combat sim only; bots don't apply to co-op). */
	if (g_NetGameMode != NETGAMEMODE_COOP && g_NetGameMode != NETGAMEMODE_ANTI) {
		for (s32 botidx = 0; botidx < MAX_BOTS; botidx++) {
			if (!(g_MpSetup.chrslots & (1ull << (botidx + BOT_SLOT_OFFSET)))) {
				continue;
			}
			struct mpbotconfig *bc = &g_BotConfigsArray[botidx];
			netbufWriteStr(dst, bc->base.name);

			/* SA-3 / FIX-7: bot body/head as session IDs.
			 * bc->base.mpbodynum is a g_MpBodies[] position (0..62), NOT a
			 * g_HeadsAndBodies[] index.  catalogResolveBodyByMpIndex() converts
			 * correctly before looking up the catalog entry. */
			{
				const char *body_canon = catalogResolveBodyByMpIndex((s32)bc->base.mpbodynum);
				catalogWriteAssetRef(dst, body_canon ? sessionCatalogGetId(body_canon) : 0);
			}
			{
				const char *head_canon = catalogResolveHeadByMpIndex((s32)bc->base.mpheadnum);
				catalogWriteAssetRef(dst, head_canon ? sessionCatalogGetId(head_canon) : 0);
			}

			netbufWriteU8(dst, bc->difficulty);
			netbufWriteU8(dst, bc->type);
		}
	}

	return dst->error;
}

u32 netmsgSvcStageStartRead(struct netbuf *src, struct netclient *srccl)
{
	if (srccl->state != CLSTATE_LOBBY && srccl->state != CLSTATE_GAME
	    && srccl->state != CLSTATE_PREPARING) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_STAGE from server but we're in state %u", srccl->state);
		return 1;
	}

	g_NetTick = netbufReadU32(src);

	g_NetRngSeeds[0] = netbufReadU64(src);
	g_NetRngSeeds[1] = netbufReadU64(src);
	g_NetRngLatch = true;

	/* SA-3: stage as session ID; 0 = return to lobby */
	const u16 stage_session = catalogReadAssetRef(src);
	sysLogPrintf(LOG_WARNING, "MATCH-START: client received SVC_STAGE_START, stage_session=%d", (int)stage_session);
	u8 stagenum = 0;
	if (stage_session == 0) {
		sysLogPrintf(LOG_WARNING, "MATCH-START: WARNING stage_session is 0 — aborting");
		return 1;  /* malformed stage start — stop processing */
	}
	{
		catalog_stage_result_t sr;
		if (catalogResolveStageBySession(stage_session, &sr)) {
			stagenum = (u8)sr.stagenum;
			sysLogPrintf(LOG_WARNING, "MATCH-START: stage_session=%d resolved to stagenum=0x%x", (int)stage_session, (unsigned)stagenum);
		} else {
			sysLogPrintf(LOG_WARNING, "NET: SVC_STAGE unknown stage session %u", (unsigned)stage_session);
			return 1;
		}
	}

	const u8 mode = netbufReadU8(src);
	g_NetGameMode = mode;

	if (mode == NETGAMEMODE_COOP || mode == NETGAMEMODE_ANTI) {
		// co-op / counter-op mission settings
		g_MissionConfig.stagenum = stagenum;
		g_MissionConfig.difficulty = netbufReadU8(src);
		g_MissionConfig.iscoop = (mode == NETGAMEMODE_COOP);
		g_MissionConfig.isanti = (mode == NETGAMEMODE_ANTI);
		g_NetCoopFriendlyFire = netbufReadU8(src);
		g_NetCoopRadar = netbufReadU8(src);
	} else {
		// combat simulator settings
		g_MpSetup.stagenum = stagenum;
		g_MpSetup.scenario = netbufReadU8(src);
		g_MpSetup.scorelimit = netbufReadU8(src);
		g_MpSetup.timelimit = netbufReadU8(src);
		g_MpSetup.teamscorelimit = netbufReadU16(src);
		g_MpSetup.chrslots = netbufReadU64(src);  /* u64 since protocol v21 */
		g_MpSetup.options = netbufReadU32(src);
		/* SA-3: weapons as session IDs */
		{
			s32 wi;
			for (wi = 0; wi < NUM_MPWEAPONSLOTS; wi++) {
				const u16 wsession = catalogReadAssetRef(src);
				if (wsession == 0) {
					g_MpSetup.weapons[wi] = 0;
				} else {
					catalog_weapon_result_t wr;
					if (catalogResolveWeaponBySession(wsession, &wr)) {
						g_MpSetup.weapons[wi] = (u8)wr.weapon_num;
					} else {
						g_MpSetup.weapons[wi] = 0;
					}
				}
			}
		}
		snprintf(g_MpSetup.name, sizeof(g_MpSetup.name), "server");
	}

	if (src->error) {
		sysLogPrintf(LOG_WARNING, "NET: malformed SVC_STAGE from server");
		return 1;
	}

	// read players
	const u8 numplayers = netbufReadU8(src);
	if (src->error || !numplayers || numplayers > g_NetMaxClients + 1) {
		sysLogPrintf(LOG_WARNING, "NET: malformed SVC_STAGE from server");
		return 2;
	}

	for (u8 i = 0; i < numplayers; ++i) {
		const u8 id = netbufReadU8(src);
		if (id >= NET_MAX_CLIENTS + 1) {
			sysLogPrintf(LOG_WARNING, "NET: SVC_STAGE invalid client id %u", id);
			return 1;
		}
		struct netclient *ncl = &g_NetClients[id];
		ncl->playernum = netbufReadU8(src);
		if (ncl->playernum >= MAX_PLAYERS) {
			sysLogPrintf(LOG_WARNING, "NET: SVC_STAGE invalid playernum %u", ncl->playernum);
			return 1;
		}
		ncl->settings.team = netbufReadU8(src);
		if (ncl != g_NetLocalClient) {
			/* SA-3: body/head as session IDs */
			u16 body_session;
			u16 head_session;
			ncl->id = id;
			ncl->settings.options = netbufReadU16(src);
			body_session = catalogReadAssetRef(src);
			head_session = catalogReadAssetRef(src);
			ncl->settings.fovy = netbufReadF32(src);
			ncl->settings.fovzoommult = netbufReadF32(src);
			{
				char *name = netbufReadStr(src);
				if (name) {
					strncpy(ncl->settings.name, name, sizeof(ncl->settings.name) - 1);
					ncl->settings.name[sizeof(ncl->settings.name) - 1] = '\0';
				} else {
					sysLogPrintf(LOG_WARNING, "NET: malformed SVC_STAGE from server");
					return 3;
				}
			}
			if (body_session > 0) {
				const asset_entry_t *be = sessionCatalogLocalResolve(body_session);
				if (be) {
					strncpy(ncl->settings.body_id, be->id, CATALOG_ID_LEN - 1);
					ncl->settings.body_id[CATALOG_ID_LEN - 1] = '\0';
				}
			}
			if (head_session > 0) {
				const asset_entry_t *he = sessionCatalogLocalResolve(head_session);
				if (he) {
					strncpy(ncl->settings.head_id, he->id, CATALOG_ID_LEN - 1);
					ncl->settings.head_id[CATALOG_ID_LEN - 1] = '\0';
				}
			}
		} else {
			/* skip our own settings except for team and playernum */
			netbufReadU16(src);
			catalogReadAssetRef(src); /* body_session */
			catalogReadAssetRef(src); /* head_session */
			netbufReadF32(src);
			netbufReadF32(src);
			netbufReadStr(src);
		}
		ncl->state = CLSTATE_GAME;
		ncl->player = NULL;
	}

	if (src->error) {
		return src->error;
	}

	// set teams on the player configs, but swap teams with the server player
	// because we haven't swapped player numbers yet (see netPlayersAlloc)
	for (u32 i = 0; i < NET_MAX_CLIENTS; ++i) {
		struct netclient *ncl = &g_NetClients[i];
		if (ncl->state) {
			u32 playernum = 0;
			if (ncl->id == 0) {
				playernum = g_NetLocalClient->playernum;
			} else if (ncl == g_NetLocalClient) {
				playernum = g_NetClients[0].playernum;
			} else {
				playernum = ncl->playernum;
			}
			if (playernum >= MAX_PLAYERS) {
				sysLogPrintf(LOG_WARNING, "NET: SvcStageStartRead invalid playernum %u for client %u", playernum, i);
				continue;
			}
			g_PlayerConfigsArray[playernum].base.team = ncl->settings.team;
		}
	}

	g_NetNumClients = numplayers;

	if (mode == NETGAMEMODE_COOP || mode == NETGAMEMODE_ANTI) {
		sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE from server: co-op stage 0x%02x difficulty %u with %u players",
			stagenum, g_MissionConfig.difficulty, numplayers);

		// set up co-op player numbers
		g_Vars.bondplayernum = 0;
		if (mode == NETGAMEMODE_COOP) {
			g_Vars.coopplayernum = (numplayers > 1) ? 1 : -1;
			g_Vars.antiplayernum = -1;
		} else {
			g_Vars.coopplayernum = -1;
			g_Vars.antiplayernum = (numplayers > 1) ? 1 : -1;
		}

		/* Dismiss countdown overlay before changing stage. */
		memset(&g_MatchCountdownState, 0, sizeof(g_MatchCountdownState));
		menuStop();
#if !defined(PD_SERVER)
		inputLockMouse(1);  /* B-92 sibling: co-op/anti SVC_STAGE — pdguiIsActive() deferred SDL lock */
		pdmainSetInputMode(INPUTMODE_GAMEPLAY);
#endif

		g_NotLoadMod = true;
		romdataFileFreeForSolo();

		titleSetNextStage(stagenum);
		setNumPlayers(numplayers);
		lvSetDifficulty(g_MissionConfig.difficulty);
		titleSetNextMode(TITLEMODE_SKIP);
		sysLogPrintf(LOG_WARNING, "MATCH-START: calling mainChangeToStage, stagenum=0x%x (co-op)", (unsigned)stagenum);
		mainChangeToStage(stagenum);

#if VERSION >= VERSION_NTSC_1_0
		viBlack(true);
#endif
	} else {
		sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE from server: going to stage 0x%02x with %u players chrslots=0x%llx",
			g_MpSetup.stagenum, numplayers, (unsigned long long)g_MpSetup.chrslots);

#if !defined(PD_SERVER)
		/* Apply catalog-validated body/head to player config arrays.
		 * matchStart() does this for offline/listen-server mode; we must mirror it
		 * here because bodyreset() has already NULLed g_HeadsAndBodies[].modeldef
		 * and nothing else reloads it on the client path — playerTickChrBody()
		 * would crash on frame 0 dereferencing a NULL modeldef. */
		for (u32 pcl = 0; pcl < NET_MAX_CLIENTS; ++pcl) {
			struct netclient *pncl = &g_NetClients[pcl];
			if (pncl->state == CLSTATE_GAME) {
				u32 pnum = 0;
				if (pncl->id == 0) {
					pnum = g_NetLocalClient->playernum;
				} else if (pncl == g_NetLocalClient) {
					pnum = g_NetClients[0].playernum;
				} else {
					pnum = pncl->playernum;
				}
				if (pnum < MAX_PLAYERS) {
					/* SA-3: resolve catalog string IDs → runtime indices */
					const asset_entry_t *be = assetCatalogResolve(pncl->settings.body_id);
					const asset_entry_t *he = assetCatalogResolve(pncl->settings.head_id);
					s32 rawBody = be ? (s32)be->runtime_index : 0;
					s32 safeHead = he ? (s32)he->runtime_index : 0;
					g_PlayerConfigsArray[pnum].base.mpbodynum = (u8)catalogGetSafeBodyPaired(rawBody, &safeHead);
					g_PlayerConfigsArray[pnum].base.mpheadnum = (u8)catalogGetSafeHead(safeHead);
					sysLogPrintf(LOG_NOTE, "NET: player %u body='%s'->%u head='%s'->%u",
						pnum, pncl->settings.body_id,
						g_PlayerConfigsArray[pnum].base.mpbodynum,
						pncl->settings.head_id,
						g_PlayerConfigsArray[pnum].base.mpheadnum);
				}
			}
		}

		/* Receive per-bot configs from wire; resolve catalog net_hash → local runtime_index.
		 * The write side (netmsgSvcStageStartWrite) serialises name/body_hash/head_hash/
		 * difficulty/type for every active bot slot.  We resolve the CRC32 hashes through
		 * the asset catalog so local mpbodynum/mpheadnum are correct regardless of whether
		 * the client and server have the same mod load order. */
		for (s32 botidx = 0; botidx < MAX_BOTS; botidx++) {
			if (!(g_MpSetup.chrslots & (1ull << (botidx + BOT_SLOT_OFFSET)))) {
				continue;
			}
			/* SA-3: bot body/head as session IDs */
			const char *botname = netbufReadStr(src);
			const u16 body_session = catalogReadAssetRef(src);
			const u16 head_session = catalogReadAssetRef(src);
			const u8 difficulty = netbufReadU8(src);
			const u8 bottype = netbufReadU8(src);
			if (src->error) {
				sysLogPrintf(LOG_WARNING, "NET: malformed SVC_STAGE bot config for bot %d", botidx);
				return src->error;
			}
			if (botname) {
				strncpy(g_BotConfigsArray[botidx].base.name, botname,
				        sizeof(g_BotConfigsArray[botidx].base.name) - 1);
				g_BotConfigsArray[botidx].base.name[sizeof(g_BotConfigsArray[botidx].base.name) - 1] = '\0';
			}
			g_BotConfigsArray[botidx].difficulty = difficulty;
			g_BotConfigsArray[botidx].type = bottype;
			{
				const asset_entry_t *be = sessionCatalogLocalResolve(body_session);
				if (be && be->type == ASSET_BODY) {
					g_BotConfigsArray[botidx].base.mpbodynum = (u8)be->runtime_index;
				}
			}
			{
				const asset_entry_t *he = sessionCatalogLocalResolve(head_session);
				if (he && he->type == ASSET_HEAD) {
					g_BotConfigsArray[botidx].base.mpheadnum = (u8)he->runtime_index;
				}
			}
			/* Final catalog safety-clamp: ensures modeldef is valid on first frame. */
			{
				s32 botSafeHead = (s32)g_BotConfigsArray[botidx].base.mpheadnum;
				g_BotConfigsArray[botidx].base.mpbodynum = (u8)catalogGetSafeBodyPaired(
					(s32)g_BotConfigsArray[botidx].base.mpbodynum, &botSafeHead);
				g_BotConfigsArray[botidx].base.mpheadnum = (u8)catalogGetSafeHead(botSafeHead);
			}
			sysLogPrintf(LOG_NOTE, "NET: bot %d name='%s' body=%u head=%u (sessions %u/%u)",
				botidx, g_BotConfigsArray[botidx].base.name,
				g_BotConfigsArray[botidx].base.mpbodynum,
				g_BotConfigsArray[botidx].base.mpheadnum,
				(unsigned)body_session, (unsigned)head_session);
		}
#endif /* !PD_SERVER */

		/* Populate the participant pool from the server-supplied chrslots bitmask
		 * before mpStartMatch() is called.  mpStartMatch rebuilds participants
		 * for NETMODE_SERVER but does nothing for NETMODE_CLIENT, so the pool
		 * would be empty and mpHasSimulants() would return false — bots would
		 * never spawn on the client side. */
		mpParticipantsFromLegacyChrslots(g_MpSetup.chrslots);

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
		inputLockMouse(1);  /* B-92 sibling: MP SVC_STAGE — pdguiIsActive() deferred SDL lock */
		pdmainSetInputMode(INPUTMODE_GAMEPLAY);
#endif
	}

	return 0;
}

u32 netmsgSvcStageEndWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, SVC_STAGE_END);
	netbufWriteU8(dst, (u8)g_NetGameMode);

	sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE_END write mode=%u", g_NetGameMode);

	if (g_NetGameMode == NETGAMEMODE_COOP || g_NetGameMode == NETGAMEMODE_ANTI) {
		// Co-op/anti: keep player linkages intact for endscreen display.
		// The endscreen needs g_Vars.bond, g_Vars.coop, and player stats.
		// Client state transitions happen via mainEndStage -> endscreenPushCoop.
	} else {
		// MP: unlink players and return clients to lobby
		for (s32 i = 0; i < g_NetMaxClients; ++i) {
			struct netclient *ncl = &g_NetClients[i];
			if (ncl->state) {
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

	return dst->error;
}

u32 netmsgSvcStageEndRead(struct netbuf *src, struct netclient *srccl)
{
	u8 mode = netbufReadU8(src);

	if (src->error) {
		return src->error;
	}

	sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE_END read mode=%u path=%s", mode,
		(mode == NETGAMEMODE_COOP || mode == NETGAMEMODE_ANTI) ? "co-op" : "mp");

	if (mode == NETGAMEMODE_COOP || mode == NETGAMEMODE_ANTI) {
		// Co-op/anti: transition to endscreen with players intact.
		// Disable further objective re-evaluation on client
		// (server has already determined mission outcome).
		objectivesDisableChecking();
		mainEndStage();
	} else {
		// MP: unlink clients and end match
		for (s32 i = 0; i < g_NetMaxClients; ++i) {
			struct netclient *ncl = &g_NetClients[i];
			if (ncl->state) {
				ncl->state = CLSTATE_DISCONNECTED;
				ncl->player = NULL;
				ncl->config = NULL;
			}
		}

		g_NetLocalClient->state = CLSTATE_LOBBY;

		g_NumReasonsToEndMpMatch = 1;
		mainEndStage();
	}

	/* SA-1: tear down session catalog on match end (client-side). */
	sessionCatalogTeardown();

	/* Bot authority relinquished at match end — next match will re-assign */
	g_NetLocalBotAuthority = false;

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

u32 netmsgSvcPlayerStatsWrite(struct netbuf *dst, struct netclient *actcl)
{
	if (actcl->state < CLSTATE_GAME || !actcl->player || !actcl->player->prop) {
		return dst->error;
	}
	const struct player *pl = actcl->player;
	const u8 flags = (pl->isdead != 0) | (pl->gunctrl.dualwielding << 1) |
		(pl->hands[0].inuse << 2) | (pl->hands[1].inuse << 3);
	netbufWriteU8(dst, SVC_PLAYER_STATS);
	netbufWriteU8(dst, actcl->id);
	netbufWriteU8(dst, flags);
	netbufWriteS8(dst, pl->gunctrl.weaponnum);
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

	return dst->error;
}

u32 netmsgSvcPlayerStatsRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 clid = netbufReadU8(src);
	const u8 flags = netbufReadU8(src);
	const s8 newweaponnum = netbufReadS8(src);
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

	const s32 prevplayernum = g_Vars.currentplayernum;

	if (actcl->playernum >= MAX_PLAYERS) {
		sysLogPrintf(LOG_WARNING, "NET: SvcPlayerStatsRead invalid playernum %d", actcl->playernum);
		return 1;
	}

	setCurrentPlayerNum(actcl->playernum);

	const bool newisdead = (flags & (1 << 0)) != 0;
	if (!pl->isdead && newisdead) {
		s16 shooter;
		if (pl->prop->chr->lastshooter >= 0 && pl->prop->chr->timeshooter > 0) {
			shooter = pl->prop->chr->lastshooter;
		} else {
			shooter = g_Vars.currentplayernum;
		}
		playerDieByShooter(shooter, true);
	} else if (pl->isdead && !newisdead) {
		playerStartNewLife();
	}

	const bool dualwielding = (flags & (1 << 1)) != 0;
	if (!pl->isdead && newweaponnum >= WEAPON_UNARMED && newweaponnum <= WEAPON_SUICIDEPILL
			&& (newweaponnum != pl->gunctrl.weaponnum || dualwielding != pl->gunctrl.dualwielding)) {
		pl->gunctrl.dualwielding = dualwielding;
		bgunEquipWeapon(newweaponnum);
	}

	setCurrentPlayerNum(prevplayernum);

	return src->error;
}

u32 netmsgSvcPropMoveWrite(struct netbuf *dst, struct prop *prop, struct coord *initrot)
{
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
			netbufWriteS16(dst, prop->weapon->base.modelnum);
			netbufWriteU8(dst, prop->weapon->weaponnum);
			netbufWriteS8(dst, prop->weapon->dualweaponnum);
			netbufWriteS8(dst, prop->weapon->unk5d);
			netbufWriteS8(dst, prop->weapon->unk5e);
			netbufWriteU8(dst, prop->weapon->gunfunc);
			netbufWriteS16(dst, prop->weapon->timer240);
			break;
		case PROPTYPE_OBJ:
			// we already send most of the important obj stuff below, so
			netbufWriteS16(dst, prop->obj->modelnum);
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
		netbufWriteU32(dst, prop->obj->hidden);
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

	if (srccl->state < CLSTATE_GAME) {
		return 1;
	}

	struct prop *prop = (type == PROPTYPE_OBJ && objtype == OBJTYPE_AUTOGUN) ? NULL : propAllocate();

	if (type == PROPTYPE_WEAPON) {
		const s16 modelnum = netbufReadS16(src);
		const u8 weaponnum = netbufReadU8(src);
		const s8 dualweaponnum = netbufReadS8(src);
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
			0,                      // flags3
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
		const s16 modelnum = netbufReadS16(src);
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
				if ((type == PROPTYPE_WEAPON && (prop->obj->projectile->flags & PROJECTILEFLAG_00000002)) || objtype == OBJTYPE_AUTOGUN) {
					// this is a thrown projectile, play throw sound
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
	netbufWriteU8(dst, SVC_PROP_DAMAGE);
	netbufWritePropPtr(dst, prop);
	netbufWriteCoord(dst, pos);
	netbufWriteF32(dst, prop->obj->damage);
	netbufWriteF32(dst, damage);
	netbufWriteS8(dst, weaponnum);
	netbufWriteS8(dst, playernum);
	netbufWriteU32(dst, prop->obj->hidden & ~(OBJHFLAG_PROJECTILE | OBJHFLAG_EMBEDDED));
	return dst->error;
}

u32 netmsgSvcPropDamageRead(struct netbuf *src, struct netclient *srccl)
{
	struct prop *prop = netbufReadPropPtr(src);
	struct coord pos; netbufReadCoord(src, &pos);
	const f32 damagepre = netbufReadF32(src);
	const f32 damage = netbufReadF32(src);
	const s8 weaponnum = netbufReadS8(src);
	const s8 playernum = netbufReadS8(src);
	const u32 hidden = netbufReadHidden(src);
	if (srccl->state < CLSTATE_GAME) {
		return src->error;
	}
	if (prop && prop->obj && prop->type != PROPTYPE_PLAYER && prop->type != PROPTYPE_CHR && !src->error
			&& weaponnum >= WEAPON_UNARMED && weaponnum <= WEAPON_SUICIDEPILL) {
		prop->obj->damage = damagepre;
		prop->obj->hidden = hidden | (prop->obj->hidden & (OBJHFLAG_PROJECTILE | OBJHFLAG_EMBEDDED));
		objDamage(prop->obj, -damage, &pos, weaponnum, playernum);
	}
	return src->error;
}

u32 netmsgSvcPropPickupWrite(struct netbuf *dst, struct netclient *actcl, struct prop *prop, const s32 tickop)
{
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

	struct doorobj *door = prop->door;

	netbufWriteU8(dst, SVC_PROP_DOOR);
	netbufWritePropPtr(dst, prop);
	netbufWriteU8(dst, usercl ? usercl->id : NET_NULL_CLIENT);
	netbufWriteS8(dst, door->mode);
	netbufWriteU32(dst, door->base.flags);
	netbufWriteU32(dst, door->base.hidden);

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

	struct liftobj *lift = (struct liftobj *)prop->obj;

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
	netbufWriteU8(dst, weaponnum);
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
	const u8 weaponnum = netbufReadU8(src);
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
		if (weaponnum < WEAPON_UNARMED || weaponnum > WEAPON_SUICIDEPILL) {
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

	if (weaponHasFlag(weaponnum, WEAPONFLAG_UNDROPPABLE) || weaponnum > WEAPON_RCP45 || weaponnum <= WEAPON_UNARMED) {
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
	netbufWriteS8(dst, aibot->weaponnum);
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
	const s8 weaponnum = netbufReadS8(src);
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
	if (weaponnum >= WEAPON_UNARMED && weaponnum <= WEAPON_SUICIDEPILL) {
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

/**
 * Compute a rolling checksum over active props that have syncids.
 * Covers autoguns, doors, lifts, and hover vehicles — the key dynamic
 * entity types whose state must agree between server and clients.
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
	u32 propcount = 0;
	u32 checksum = netPropSyncChecksum(&propcount);

	netbufWriteU8(dst, SVC_PROP_SYNC);
	netbufWriteU32(dst, g_NetTick);
	netbufWriteU16(dst, (u16)propcount);
	netbufWriteU32(dst, checksum);
	return dst->error;
}

u32 netmsgSvcPropSyncRead(struct netbuf *src, struct netclient *srccl)
{
	const u32 tick = netbufReadU32(src);
	const u16 serverCount = netbufReadU16(src);
	const u32 serverCrc = netbufReadU32(src);

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	u32 localCount = 0;
	u32 localCrc = netPropSyncChecksum(&localCount);

	if (serverCount != localCount) {
		sysLogPrintf(LOG_WARNING, "NET: prop sync count mismatch at tick %u: server=%u local=%u",
			tick, serverCount, localCount);
		g_NetPropDesyncCount++;
	} else if (localCrc != serverCrc) {
		sysLogPrintf(LOG_WARNING, "NET: prop desync detected at tick %u: server=0x%08x local=0x%08x (%u props)",
			tick, serverCrc, localCrc, localCount);
		g_NetPropDesyncCount++;
	} else {
		g_NetPropDesyncCount = 0;
	}

	// After consecutive desyncs, request full resync from server.
	// Same pending-flag pattern as chr sync above — direct write here would be dropped by netStartFrame.
	if (g_NetPropDesyncCount >= NET_DESYNC_THRESHOLD &&
		(g_NetTick - g_NetPropResyncLastReq) > NET_RESYNC_COOLDOWN) {
		sysLogPrintf(LOG_WARNING, "NET: requesting prop resync after %u consecutive desyncs", g_NetPropDesyncCount);
		g_NetPendingResyncReqFlags |= NET_RESYNC_FLAG_PROPS;
		g_NetPropResyncLastReq = g_NetTick;
		g_NetPropDesyncCount = 0;
	}

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
			sysLogPrintf(LOG_NOTE, "NET: all clients ready, starting co-op mission");
			netServerCoopStageStart(g_MissionConfig.stagenum, g_MissionConfig.difficulty);
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
		netbufWriteS8(dst, aibot->weaponnum);
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
		s8 weaponnum = netbufReadS8(src);
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
		if (weaponnum >= WEAPON_UNARMED && weaponnum <= WEAPON_SUICIDEPILL) {
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
				netbufWriteU32(dst, obj->hidden);

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

	// Reset desync counter — we just got a fresh full state
	g_NetPropDesyncCount = 0;

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
 * NPC identification: prop->type == PROPTYPE_CHR && chr->aibot == NULL
 * ======================================================================== */

/* Helper: check if a chrdata is an active NPC (not a bot, not a player) */
static inline bool netIsNpc(struct chrdata *chr)
{
	return chr && chr->prop && chr->prop->type == PROPTYPE_CHR && !chr->aibot;
}

/* ========================================================================
 * SVC_NPC_MOVE - Server-authoritative NPC position update
 * Sent every few frames for NPCs in co-op mode.
 * Syncs position, facing angle, rooms, and action state.
 * ======================================================================== */

u32 netmsgSvcNpcMoveWrite(struct netbuf *dst, struct chrdata *chr)
{
	if (!netIsNpc(chr)) {
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

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	if (!prop || !prop->chr || prop->type != PROPTYPE_CHR) {
		return src->error;
	}

	struct chrdata *chr = prop->chr;

	// Apply server-authoritative position
	chr->prevpos = prop->pos;
	prop->pos = newpos;
	chrSetLookAngle(chr, newangle);
	chr->myaction = myaction;
	chr->actiontype = actiontype;

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
	if (!netIsNpc(chr)) {
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

	if (!prop || !prop->chr || prop->type != PROPTYPE_CHR) {
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
 * SVC_NPC_SYNC - Periodic checksum of all NPC states for desync detection
 * Server sends a compact checksum every N frames in co-op mode.
 * ======================================================================== */

u32 netNpcCount(void)
{
	u32 count = 0;
	for (s32 i = 0; i < g_NumChrSlots; ++i) {
		if (netIsNpc(&g_ChrSlots[i])) {
			++count;
		}
	}
	return count;
}

static u32 netNpcSyncChecksum(void)
{
	u32 crc = 0;
	for (s32 i = 0; i < g_NumChrSlots; ++i) {
		struct chrdata *chr = &g_ChrSlots[i];
		if (!netIsNpc(chr)) {
			continue;
		}
		u32 px = *(u32 *)&chr->prop->pos.x;
		u32 py = *(u32 *)&chr->prop->pos.y;
		u32 pz = *(u32 *)&chr->prop->pos.z;
		u32 dm = *(u32 *)&chr->damage;
		crc ^= px ^ (py << 7) ^ (pz << 13) ^ (dm << 19) ^ ((u32)chr->myaction << 24);
		crc = (crc << 5) | (crc >> 27); // rotate
	}
	return crc;
}

u32 netmsgSvcNpcSyncWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, SVC_NPC_SYNC);
	netbufWriteU32(dst, g_NetTick);
	netbufWriteU16(dst, (u16)netNpcCount());
	netbufWriteU32(dst, netNpcSyncChecksum());
	return dst->error;
}

u32 netmsgSvcNpcSyncRead(struct netbuf *src, struct netclient *srccl)
{
	const u32 tick = netbufReadU32(src);
	const u16 npccount = netbufReadU16(src);
	const u32 serverCrc = netbufReadU32(src);

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	u32 localCount = netNpcCount();
	if (npccount != localCount) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_NPC_SYNC desync detected at tick %u: server has %u npcs, we have %u",
			tick, npccount, localCount);
		g_NetNpcDesyncCount++;
	} else {
		u32 localCrc = netNpcSyncChecksum();
		if (localCrc != serverCrc) {
			sysLogPrintf(LOG_WARNING, "NET: SVC_NPC_SYNC checksum mismatch at tick %u: server=0x%08x local=0x%08x",
				tick, serverCrc, localCrc);
			g_NetNpcDesyncCount++;
		} else {
			g_NetNpcDesyncCount = 0;
		}
	}

	// After consecutive desyncs, request full resync from server.
	// Same pending-flag pattern as chr/prop sync above — direct write here would be dropped by netStartFrame.
	if (g_NetNpcDesyncCount >= NET_DESYNC_THRESHOLD &&
		(g_NetTick - g_NetNpcResyncLastReq) > NET_RESYNC_COOLDOWN) {
		sysLogPrintf(LOG_WARNING, "NET: requesting npc resync after %u consecutive desyncs", g_NetNpcDesyncCount);
		g_NetPendingResyncReqFlags |= NET_RESYNC_FLAG_NPCS;
		g_NetNpcResyncLastReq = g_NetTick;
		g_NetNpcDesyncCount = 0;
	}

	return src->error;
}

/* ========================================================================
 * SVC_NPC_RESYNC - Full NPC state dump for correction after desync
 * ======================================================================== */

u32 netmsgSvcNpcResyncWrite(struct netbuf *dst)
{
	u16 count = (u16)netNpcCount();

	sysLogPrintf(LOG_NOTE, "NET: SVC_NPC_RESYNC write %u npcs", count);

	netbufWriteU8(dst, SVC_NPC_RESYNC);
	netbufWriteU32(dst, g_NetTick);
	netbufWriteU16(dst, count);

	for (s32 i = 0; i < g_NumChrSlots; ++i) {
		struct chrdata *chr = &g_ChrSlots[i];
		if (!netIsNpc(chr)) {
			continue;
		}

		struct prop *prop = chr->prop;

		const u8 flags = (chrIsDead(chr) ? (1 << 0) : 0)
			| ((chr->hidden & CHRHFLAG_CLOAKED) ? (1 << 1) : 0)
			| ((chr->target >= 0) ? (1 << 2) : 0);

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
		if (chr->target >= 0 && chr->target < g_Vars.maxprops) {
			netbufWritePropPtr(dst, &g_Vars.props[chr->target]);
		} else {
			netbufWriteU32(dst, 0);
		}
	}

	return dst->error;
}

u32 netmsgSvcNpcResyncRead(struct netbuf *src, struct netclient *srccl)
{
	const u32 tick = netbufReadU32(src);
	const u16 npccount = netbufReadU16(src);

	if (src->error) {
		return src->error;
	}

	sysLogPrintf(LOG_NOTE, "NET: SVC_NPC_RESYNC read %u npcs at tick %u, desync resolved", npccount, tick);

	for (u16 i = 0; i < npccount; ++i) {
		struct prop *prop = netbufReadPropPtr(src);

		// Position and orientation
		struct coord pos;
		netbufReadCoord(src, &pos);
		f32 angle = netbufReadF32(src);
		RoomNum rooms[8] = { -1 };
		netbufReadRooms(src, rooms, ARRAYCOUNT(rooms));

		// Actions
		s8 myaction = netbufReadS8(src);
		u8 actiontype = netbufReadU8(src);

		// State
		u8 flags = netbufReadU8(src);
		f32 damage = netbufReadF32(src);
		f32 maxdamage = netbufReadF32(src);
		u8 alertness = netbufReadU8(src);
		s8 team = netbufReadS8(src);
		u32 chrflags = netbufReadU32(src);
		u32 hidden = netbufReadU32(src);
		u8 fadealpha = netbufReadU8(src);

		// Target
		struct prop *targetprop = netbufReadPropPtr(src);

		if (src->error || srccl->state < CLSTATE_GAME) {
			return src->error;
		}

		if (!prop || !prop->chr || prop->type != PROPTYPE_CHR) {
			continue;
		}

		struct chrdata *chr = prop->chr;

		// Apply full state correction
		chr->prevpos = prop->pos;
		prop->pos = pos;
		chrSetLookAngle(chr, angle);

		for (s32 r = 0; r < ARRAYCOUNT(prop->rooms); ++r) {
			prop->rooms[r] = rooms[r];
			if (rooms[r] < 0) break;
		}

		chr->myaction = myaction;
		chr->actiontype = actiontype;
		chr->damage = damage;
		chr->maxdamage = maxdamage;
		chr->alertness = alertness;
		chr->team = team;
		chr->chrflags = chrflags;
		chr->hidden = hidden;
		chr->fadealpha = fadealpha;

		if (targetprop) {
			// Bounds check: ensure targetprop is within valid props array range
			if (targetprop >= g_Vars.props && targetprop < &g_Vars.props[g_Vars.maxprops]) {
				chr->target = targetprop - g_Vars.props;
			} else {
				sysLogPrintf(LOG_WARNING, "NET: NPC resync received invalid target prop pointer");
				chr->target = -1;
			}
		} else {
			chr->target = -1;
		}
	}

	// Reset desync counter
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
 * SVC_CUTSCENE - Server-authoritative cutscene state sync (co-op only)
 * Sent when a cutscene starts or ends on the server.
 * Client locks/unlocks input and sets g_InCutscene flag.
 * Camera sync is not included — client freezes during cutscene (MVP).
 * ======================================================================== */

u32 netmsgSvcCutsceneWrite(struct netbuf *dst, u8 active)
{
	netbufWriteU8(dst, SVC_CUTSCENE);
	netbufWriteU8(dst, active);
	return dst->error;
}

u32 netmsgSvcCutsceneRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 active = netbufReadU8(src);

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	sysLogPrintf(LOG_NOTE, "NET: SVC_CUTSCENE read active=%u", active);

	g_InCutscene = active ? 1 : 0;
	/* Setting g_InCutscene is sufficient — the input gating macro in constants.h
	 * checks this flag and suppresses player input automatically. */

	return src->error;
}

/* ========================================================================
 * CLC_LOBBY_START - Lobby leader requests match start
 *
 * Sent by the lobby leader client to the dedicated server when they've
 * chosen a game mode and are ready to start. The server validates that
 * the sender is actually the lobby leader, then starts the match.
 *
 * Payload (v27+): gamemode (u8), stage_id (str catalog ID),
 *          difficulty (u8), numSims (u8), simType (u8),
 *          timelimit (u8), options (u32), scenario (u8), scorelimit (u8), teamscorelimit (u16),
 *          weaponSetIndex (u8, 0xFF = custom/default),
 *          weapons (str[NUM_MPWEAPONSLOTS]) — per-slot ASSET_WEAPON catalog ID string
 *          Per-bot (repeated numSims times): name (str), body_id (str), head_id (str),
 *          botDifficulty (u8), botType (u8)
 *
 * v27: all asset references are catalog ID strings — no u32 net_hash on wire.
 * stage_id written from g_MatchConfig.stage_id (Single Source of Truth).
 * Server resolves stage_id → stagenum and weapon IDs → weapon_id via assetCatalogResolve().
 * ======================================================================== */

u32 netmsgClcLobbyStartWrite(struct netbuf *dst, u8 gamemode, u8 stagenum, u8 difficulty, u8 numSims, u8 simType, u8 timelimit, u32 options, u8 scenario, u8 scorelimit, u16 teamscorelimit, u8 weaponSetIndex)
{
	(void)stagenum; /* stage identity comes from g_MatchConfig.stage_id, not stagenum */
	netbufWriteU8(dst, CLC_LOBBY_START);
	netbufWriteU8(dst, gamemode);
	/* C-1: write arena as catalog ID string — Single Source of Truth from matchconfig.
	 * g_MatchConfig.stage_id is set by the arena picker UI (M-2 fix, S129).
	 * Server reads this string and resolves via assetCatalogResolve(). */
	if (!g_MatchConfig.stage_id[0]) {
		sysLogPrintf(LOG_WARNING, "MATCH-START: WARNING stage_id is EMPTY at CLC_LOBBY_START write");
	}
	sysLogPrintf(LOG_WARNING, "MATCH-START: client sending CLC_LOBBY_START, stage_id='%s'", g_MatchConfig.stage_id);
	netbufWriteStr(dst, g_MatchConfig.stage_id[0] ? g_MatchConfig.stage_id : "");
	netbufWriteU8(dst, difficulty);
	netbufWriteU8(dst, numSims);
	netbufWriteU8(dst, simType);
	netbufWriteU8(dst, timelimit);
	netbufWriteU32(dst, options);
	netbufWriteU8(dst, scenario);
	netbufWriteU8(dst, scorelimit);
	netbufWriteU16(dst, teamscorelimit);
	netbufWriteU8(dst, weaponSetIndex);
	/* C-1: per-slot weapon catalog ID string.
	 * catalogResolveWeaponByGameId() returns the catalog ID string ("base:falcon2" etc.)
	 * or NULL for empty/unset slots. Server resolves string → weapon_id via assetCatalogResolve(). */
	{
		s32 wi;
		for (wi = 0; wi < NUM_MPWEAPONSLOTS; wi++) {
			if (g_MpSetup.weapons[wi] == 0) {
				netbufWriteStr(dst, "");
			} else {
				const char *wcanon = catalogResolveWeaponByGameId(
					(s32)g_MpSetup.weapons[wi]);
				netbufWriteStr(dst, wcanon ? wcanon : "");
			}
		}
	}

	/* Per-bot config: iterate bot slots in g_MatchConfig in order.
	 * Count must equal numSims; extra slots are skipped, missing ones
	 * write empty defaults so the server always reads exactly numSims entries.
	 *
	 * Catalog-ID-native: send body_id/head_id strings directly — the server
	 * resolves them via assetCatalogResolve() + catalogBodynumToMpBodyIdx()
	 * to obtain the correct mpbodynum/mpheadnum at the last moment.
	 * No integer-domain conversion on the client send path. */
	s32 botIdx = 0;
	for (s32 si = 0; si < g_MatchConfig.numSlots && botIdx < (s32)numSims; si++) {
		if (g_MatchConfig.slots[si].type == SLOT_BOT) {
			const struct matchslot *sl = &g_MatchConfig.slots[si];
			netbufWriteStr(dst, sl->name[0] ? sl->name : "Bot");
			netbufWriteStr(dst, sl->body_id[0] ? sl->body_id : "base:dark_combat");
			netbufWriteStr(dst, sl->head_id[0] ? sl->head_id : "base:head_dark_combat");
			netbufWriteU8(dst, sl->botDifficulty);
			netbufWriteU8(dst, sl->botType);
			botIdx++;
		}
	}
	/* Pad any missing slots with defaults (shouldn't happen in practice). */
	for (; botIdx < (s32)numSims; botIdx++) {
		netbufWriteStr(dst, "Bot");
		netbufWriteStr(dst, "base:dark_combat");
		netbufWriteStr(dst, "base:head_dark_combat");
		netbufWriteU8(dst, 2);  /* BOTDIFF_NORMAL */
		netbufWriteU8(dst, simType);
	}

	/* Phase D.2/D.3: host-built manifest embedded in CLC_LOBBY_START.
	 * The server receives this manifest, supplements it with other players'
	 * body/head (from their CLC_SETTINGS), and broadcasts SVC_MATCH_MANIFEST. */
	{
		match_manifest_t hostManifest;
		memset(&hostManifest, 0, sizeof(hostManifest));
		/* Sync stage into g_MpSetup.stage_id so manifestBuildForHost includes it.
		 * g_MatchConfig.stage_id is set by the UI; g_MpSetup.stage_id is what
		 * the manifest builder and SVC_STAGE_START write both read. */
		if (g_MatchConfig.stage_id[0]) {
			strncpy(g_MpSetup.stage_id, g_MatchConfig.stage_id, sizeof(g_MpSetup.stage_id) - 1);
			g_MpSetup.stage_id[sizeof(g_MpSetup.stage_id) - 1] = '\0';
		}
		manifestBuildForHost(&hostManifest);
		manifestSerialize(dst, &hostManifest);
		manifestFree(&hostManifest);
	}

	return dst->error;
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
	netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL);
	sysLogPrintf(LOG_NOTE, "NET: SVC_MATCH_COUNTDOWN %u/%u phase=%u secs=%u",
	             (unsigned)ready_count, (unsigned)s_ReadyGate.total_count,
	             (unsigned)phase, (unsigned)countdown_secs);
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
		sysLogPrintf(LOG_NOTE, "NET: countdown complete — launching match (stage %u room %u)",
		             (unsigned)s_ReadyGate.stagenum, (unsigned)s_ReadyGate.room_id);
		s_ReadyGate.active           = 0;
		s_ReadyGate.countdown_active = 0;

		hub_room_t *room = (s_ReadyGate.room_id != 0xFF) ? roomGetById(s_ReadyGate.room_id) : roomGetById(0);
		if (room) {
			roomTransition(room, ROOM_STATE_LOADING);
		}

		/* R-3: Set the match room before calling netServerStageStart so it can scope broadcasts */
		extern u8 g_NetMatchRoomId;
		g_NetMatchRoomId = s_ReadyGate.room_id;

		mainChangeToStage(s_ReadyGate.stagenum);
		netServerStageStart();
	} else {
		s_ReadyGate.countdown_next_tick = g_NetTick + 60;
	}
}

/* Phase F: Abort the ready gate — cancel the countdown, reset all preparing clients
 * back to CLSTATE_LOBBY, and broadcast SVC_MATCH_CANCELLED to all connected clients. */
static void readyGateAbort(const char *canceller_name)
{
	s32 i;
	hub_room_t *room;

	if (!s_ReadyGate.active) {
		return;
	}

	sysLogPrintf(LOG_NOTE, "NET: ready gate aborted by '%s'",
	             canceller_name ? canceller_name : "?");

	s_ReadyGate.active           = 0;
	s_ReadyGate.countdown_active = 0;
	s_ReadyGate.expected_mask    = 0;
	s_ReadyGate.ready_mask       = 0;
	s_ReadyGate.declined_mask    = 0;

	/* Return all preparing clients to the lobby */
	for (i = 0; i < NET_MAX_CLIENTS; i++) {
		if (g_NetClients[i].state == CLSTATE_PREPARING) {
			g_NetClients[i].state = CLSTATE_LOBBY;
		}
	}

	/* Return room to lobby state */
	room = roomGetById(0);
	if (room) {
		roomTransition(room, ROOM_STATE_LOBBY);
	}

	/* Broadcast the cancellation so all clients can show the message */
	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcMatchCancelledWrite(&g_NetMsgRel, canceller_name ? canceller_name : "?");
	netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL);
}

u32 netmsgClcLobbyStartRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 gamemode        = netbufReadU8(src);
	/* C-2: read arena as catalog ID string (v27+), resolve via assetCatalogResolve().
	 * No fallback — if the string doesn't resolve, stagenum=0 and an error is logged.
	 * stage_id is also stored in g_MatchConfig for server-side reference. */
	{
		char *stage_id_str = netbufReadStr(src);
		const char *stage_id = stage_id_str ? stage_id_str : "";
		const asset_entry_t *stage_entry = stage_id[0] ? assetCatalogResolve(stage_id) : NULL;
		if (stage_id[0] && !stage_entry) {
			sysLogPrintf(LOG_ERROR,
			             "NET: CLC_LOBBY_START arena '%s' not in catalog -- rejecting", stage_id);
		}
		g_MpSetup.stagenum = stage_entry ? (u8)stage_entry->ext.arena.stagenum : 0;
		strncpy(g_MatchConfig.stage_id, stage_id, sizeof(g_MatchConfig.stage_id) - 1);
		g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
		/* Also populate g_MpSetup.stage_id — used by SVC_STAGE_START write and
		 * the server-side manifest builder (manifestBuild fallback path). */
		strncpy(g_MpSetup.stage_id, stage_id, sizeof(g_MpSetup.stage_id) - 1);
		g_MpSetup.stage_id[sizeof(g_MpSetup.stage_id) - 1] = '\0';
		if (!g_MpSetup.stage_id[0]) {
			sysLogPrintf(LOG_WARNING, "MATCH-START: WARNING stage_id is EMPTY at CLC_LOBBY_START read");
		}
		sysLogPrintf(LOG_WARNING, "MATCH-START: server received CLC_LOBBY_START, stage_id='%s'", g_MpSetup.stage_id);
	}
	const u8 difficulty      = netbufReadU8(src);
	const u8 numSims         = netbufReadU8(src);
	const u8 simType         = netbufReadU8(src);
	const u8 timelimit       = netbufReadU8(src);
	const u32 options        = netbufReadU32(src);
	const u8 scenario        = netbufReadU8(src);
	const u8 scorelimit      = netbufReadU8(src);
	const u16 teamscorelimit = netbufReadU16(src);
	const u8 weaponSetIndex  = netbufReadU8(src);
	/* C-2: per-slot weapon catalog ID string — resolve each to weapon_id.
	 * No fallback: if a non-empty string can't resolve, log error and use 0 (no weapon). */
	{
		s32 wi;
		for (wi = 0; wi < NUM_MPWEAPONSLOTS; wi++) {
			char *wid_str = netbufReadStr(src);
			const char *wid = wid_str ? wid_str : "";
			if (wid[0]) {
				const asset_entry_t *we = assetCatalogResolve(wid);
				if (we) {
					g_MpSetup.weapons[wi] = (u8)we->ext.weapon.weapon_id;
				} else {
					sysLogPrintf(LOG_ERROR,
					             "NET: CLC_LOBBY_START weapon slot %d '%s' not in catalog -- skipping",
					             wi, wid);
					g_MpSetup.weapons[wi] = 0;
				}
			} else {
				g_MpSetup.weapons[wi] = 0;
			}
		}
	}
	(void)weaponSetIndex; /* retained for logging/future use */

	if (src->error) {
		return src->error;
	}

	/* Only process on server */
	if (g_NetMode != NETMODE_SERVER) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_LOBBY_START received but not server");
		return src->error;
	}

	/* R-3: Validate sender is in a room and is the room creator (room leader).
	 * The room creator is the only one who can start the match for that room.
	 * Also accept from global lobby leader as fallback for backward compat. */
	lobbyUpdate();
	bool isLeader = false;
	hub_room_t *startRoom = NULL;

	if (srccl->room_id != 0xFF) {
		startRoom = roomGetById(srccl->room_id);
		if (startRoom && startRoom->creator_client_id == srccl->id) {
			isLeader = true;
		}
	}

	/* Fallback: if not in a room, check global lobby leader (backward compat) */
	if (!isLeader && srccl->room_id == 0xFF) {
		u8 leaderSlot = lobbyGetLeader();
		if (leaderSlot < LOBBY_MAX_PLAYERS) {
			struct lobbyplayer *lp = &g_Lobby.players[leaderSlot];
			if (lp->active && &g_NetClients[lp->clientId] == srccl) {
				isLeader = true;
			}
		}
		/* Fallback: first connected lobby client if no leader assigned yet */
		if (!isLeader && leaderSlot == 0xFF) {
			for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
				struct netclient *ncl = &g_NetClients[ci];
				if (ncl == g_NetLocalClient) continue;
				if (ncl->state < CLSTATE_LOBBY) continue;
				isLeader = (ncl == srccl);
				break;
			}
		}
	}

	if (!isLeader) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_LOBBY_START rejected from client %d (%s) — not room creator",
		             srccl->id, srccl->settings.name);
		return src->error;
	}

	sysLogPrintf(LOG_NOTE, "NET: CLC_LOBBY_START from leader %s: gamemode=%u stage='%s'(num=%u) diff=%u tl=%u opt=0x%08x",
	             srccl->settings.name, gamemode, g_MatchConfig.stage_id, (unsigned)g_MpSetup.stagenum,
	             difficulty, timelimit, (unsigned)options);

	/* Apply settings */
	g_NetGameMode = gamemode;

	/* Start the match based on game mode.
	 * For Combat Sim: load the requested stage and start.
	 * For Co-op/Counter-op: load the solo stage and start. */
	if (gamemode == 0) {
		/* Combat Simulator
		 *
		 * Configure g_MpSetup fully before netServerStageStart(), which
		 * broadcasts SVC_STAGE_START containing g_MpSetup.chrslots and
		 * each client's playernum.  Without this, clients receive a
		 * zero chrslots and no slot assignments, so mpStartMatch() never
		 * spawns anyone. */
		/* g_MpSetup.stagenum already set above from arena catalog ID resolution */
		g_MpSetup.scenario        = scenario;
		g_MpSetup.timelimit       = timelimit; /* 0..59 = minutes, >=60 = unlimited */
		g_MpSetup.scorelimit      = scorelimit;
		g_MpSetup.teamscorelimit  = teamscorelimit;
		g_MpSetup.options         = options;
		/* g_MpSetup.weapons[] populated above by FIX-4 per-slot weapon catalog ID string resolution. */

		/* Assign sequential playernums and build chrslots bitmask.
		 * Bits 0..n-1 of chrslots represent the n connected players. */
		g_MpSetup.chrslots = 0;
		s32 pnum = 0;
		for (s32 ci = 0; ci < NET_MAX_CLIENTS && pnum < MAX_PLAYERS; ci++) {
			struct netclient *ncl = &g_NetClients[ci];
			if (ncl->state == CLSTATE_LOBBY || ncl->state == CLSTATE_GAME) {
				ncl->playernum     = (u8)pnum;
				g_MpSetup.chrslots |= (u64)1 << pnum;
				sysLogPrintf(LOG_NOTE, "NET: assigned playernum %d to client %d (%s)",
				             pnum, ci, ncl->settings.name);
				pnum++;
			}
		}
		/* Add simulant (bot) slots: bits 8..8+numSims-1 in chrslots.
		 * MAX_BOTS = 32, MAX_PLAYERS = 8 so bots live in bits 8..39.
		 * numSims is clamped to MAX_BOTS to prevent overflow. */
		u8 clampedSims = (numSims > MAX_BOTS) ? MAX_BOTS : numSims;
		for (s32 bi = 0; bi < clampedSims; bi++) {
			s32 slot = MAX_PLAYERS + bi;
			g_MpSetup.chrslots |= (u64)1 << slot;
			/* Catalog-ID-native: read body_id/head_id strings, resolve to
			 * mpbodynum/mpheadnum here at the server read site — the ONLY
			 * integer-domain conversion point for bot characters. */
			const char *botName    = netbufReadStr(src);
			const char *body_id    = netbufReadStr(src); /* catalog ID e.g. "base:dark_combat" */
			const char *head_id    = netbufReadStr(src); /* catalog ID e.g. "base:head_dark_combat" */
			const u8 botDifficulty = netbufReadU8(src);
			const u8 botType       = netbufReadU8(src);
			if (src->error) {
				/* Malformed — fall back to global simType, normal difficulty */
				g_BotConfigsArray[bi].type       = simType;
				g_BotConfigsArray[bi].difficulty = 2;
				continue;
			}
			g_BotConfigsArray[bi].type       = botType;
			g_BotConfigsArray[bi].difficulty = botDifficulty;
			/* Resolve catalog IDs → mpbodynum/mpheadnum (last-moment conversion). */
			g_BotConfigsArray[bi].base.mpbodynum = 0; /* MPBODY_DARK_COMBAT default */
			g_BotConfigsArray[bi].base.mpheadnum = 0; /* MPHEAD_DARK_COMBAT default */
			if (body_id && body_id[0]) {
				const asset_entry_t *be = assetCatalogResolve(body_id);
				if (be && be->type == ASSET_BODY) {
					const s32 mpb = catalogBodynumToMpBodyIdx(be->runtime_index);
					if (mpb >= 0) g_BotConfigsArray[bi].base.mpbodynum = (u8)mpb;
				}
			}
			if (head_id && head_id[0]) {
				const asset_entry_t *he = assetCatalogResolve(head_id);
				if (he && he->type == ASSET_HEAD) {
					const s32 mph = catalogHeadnumToMpHeadIdx(he->runtime_index);
					if (mph >= 0) g_BotConfigsArray[bi].base.mpheadnum = (u8)mph;
				}
			}
			if (botName && botName[0]) {
				strncpy(g_BotConfigsArray[bi].base.name, botName, sizeof(g_BotConfigsArray[bi].base.name) - 1);
				g_BotConfigsArray[bi].base.name[sizeof(g_BotConfigsArray[bi].base.name) - 1] = '\0';
			} else {
				g_BotConfigsArray[bi].base.name[0] = '\0';
			}
			sysLogPrintf(LOG_NOTE, "NET: bot %d: name='%s' body_id='%s' head_id='%s' mpbody=%u mphead=%u type=%u diff=%u",
			             bi,
			             g_BotConfigsArray[bi].base.name[0] ? g_BotConfigsArray[bi].base.name : "(empty)",
			             body_id ? body_id : "", head_id ? head_id : "",
			             g_BotConfigsArray[bi].base.mpbodynum,
			             g_BotConfigsArray[bi].base.mpheadnum,
			             botType, botDifficulty);
		}
		g_Lobby.settings.numSimulants = clampedSims;
		sysLogPrintf(LOG_NOTE, "NET: Combat Sim setup: stage=0x%02x chrslots=0x%llx players=%d sims=%d type=%d",
		             (unsigned)g_MpSetup.stagenum, (unsigned long long)g_MpSetup.chrslots, pnum, clampedSims, simType);

		/* Phase D.3: receive host-built manifest from CLC_LOBBY_START payload,
		 * then supplement with other connected players' body/head (which the
		 * server knows from their CLC_SETTINGS but the host does not).
		 * Falls back to server-side manifestBuild() if deserialization fails. */
		manifestClear(&g_ServerManifest);
		{
			s32 deserOk = (manifestDeserialize(src, &g_ServerManifest) == 0);
			if (!deserOk || src->error) {
				sysLogPrintf(LOG_WARNING,
				             "NET: CLC_LOBBY_START host manifest missing or corrupt — falling back to server-side build"
				             " (deserOk=%d bufError=%d rp=%u wp=%u size=%u)",
				             deserOk, (s32)src->error, (unsigned)src->rp, (unsigned)src->wp, (unsigned)src->size);
				manifestBuild(&g_ServerManifest, NULL, NULL);
			} else {
				/* Supplement: add other players' body/head from their stored settings.
				 * Slot assignment mirrors manifestBuild(): sequential CLSTATE_LOBBY clients.
				 * The host is already in the manifest (slot 0); other players get subsequent
				 * slots.  Dedup-by-net_hash prevents duplicates. */
				u8 supp_slot = 0;
				for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
					const struct netclient *ncl = &g_NetClients[ci];
					if (ncl->state != CLSTATE_LOBBY && ncl->state != CLSTATE_GAME) {
						continue;
					}
					if (ncl == srccl) {
						/* Host already included in received manifest */
						supp_slot++;
						continue;
					}
					{
						const asset_entry_t *be = assetCatalogResolve(ncl->settings.body_id);
						if (be) {
							manifestAddEntry(&g_ServerManifest, be->id,
							                 MANIFEST_TYPE_BODY, supp_slot);
						}
					}
					{
						const asset_entry_t *he = assetCatalogResolve(ncl->settings.head_id);
						if (he) {
							manifestAddEntry(&g_ServerManifest, he->id,
							                 MANIFEST_TYPE_HEAD, supp_slot);
						}
					}
					supp_slot++;
				}
				/* D.5: extract stage catalog ID from manifest STAGE entry and
				 * confirm it matches the stagenum resolved from the arena hash.
				 * Log a warning on mismatch so misconfigurations are detectable. */
				for (s32 mi = 0; mi < (s32)g_ServerManifest.num_entries; mi++) {
					const match_manifest_entry_t *me = &g_ServerManifest.entries[mi];
					if (me->type == MANIFEST_TYPE_STAGE) {
						const asset_entry_t *se = assetCatalogResolve(me->id);
						if (se) {
							const u8 manifest_stagenum = (u8)se->ext.map.stagenum;
							if (manifest_stagenum != g_MpSetup.stagenum) {
								sysLogPrintf(LOG_WARNING,
								             "NET: D.5 stage mismatch: arena ID→0x%02x, manifest→0x%02x ('%s') — using arena ID",
								             (unsigned)g_MpSetup.stagenum, (unsigned)manifest_stagenum, me->id);
							}
						}
						break;
					}
				}
				manifestComputeHash(&g_ServerManifest);
			}
		}
		manifestLog(&g_ServerManifest);

		/* SA-1: build session catalog from manifest entries. */
		sessionCatalogBuild(&g_ServerManifest);
		sessionCatalogLogMapping();

		/* R-3: Determine which room this match belongs to */
		const u8 matchRoomId = srccl->room_id;

		/* Phase C: broadcast SVC_MATCH_MANIFEST to room members (or all if no room).
		 * Phase E will gate on CLC_MANIFEST_STATUS responses instead. */
		netbufStartWrite(&g_NetMsgRel);
		netmsgSvcMatchManifestWrite(&g_NetMsgRel, &g_ServerManifest);
		if (matchRoomId != 0xFF) {
			netSendToRoom(matchRoomId, &g_NetMsgRel, true, NETCHAN_CONTROL);
		} else {
			netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL);
		}
		sysLogPrintf(LOG_NOTE, "NET: SVC_MATCH_MANIFEST broadcast (hash=0x%08x entries=%u room=%u)",
		             (unsigned)g_ServerManifest.manifest_hash,
		             (unsigned)g_ServerManifest.num_entries,
		             (unsigned)matchRoomId);

		/* SA-1: broadcast session catalog to room clients. */
		sessionCatalogBroadcast();

		/* Phase E: enter ready gate — transition room members from LOBBY to PREPARING,
		 * initialize tracker, broadcast initial countdown.
		 * SVC_STAGE_START is deferred until all clients respond READY (or timeout). */
		{
			u32 expected_mask = 0;
			u8  total_count   = 0;
			for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
				if (g_NetClients[ci].state == CLSTATE_LOBBY) {
					/* R-3: Only transition clients in the same room */
					if (matchRoomId != 0xFF && g_NetClients[ci].room_id != matchRoomId) {
						continue;
					}
					g_NetClients[ci].state = CLSTATE_PREPARING;
					expected_mask |= (1u << ci);
					total_count++;
				}
			}

			s_ReadyGate.active        = 1;
			s_ReadyGate.expected_mask = expected_mask;
			s_ReadyGate.ready_mask    = 0;
			s_ReadyGate.declined_mask = 0;
			s_ReadyGate.deadline_ticks = g_NetTick + READY_GATE_TIMEOUT_TICKS;
			s_ReadyGate.stagenum      = g_MpSetup.stagenum;
			s_ReadyGate.total_count   = total_count;
			s_ReadyGate.room_id       = matchRoomId;

			hub_room_t *room = (matchRoomId != 0xFF) ? roomGetById(matchRoomId) : roomGetById(0);
			if (room) {
				roomTransition(room, ROOM_STATE_PREPARING);
			}

			if (total_count == 0) {
				/* No clients in lobby; fire immediately */
				s_ReadyGate.active = 0;
				if (room) {
					roomTransition(room, ROOM_STATE_LOADING);
				}
				extern u8 g_NetMatchRoomId;
				g_NetMatchRoomId = matchRoomId;
				mainChangeToStage(g_MpSetup.stagenum);
				netServerStageStart();
			} else {
				readyGateBroadcastCountdown(MANIFEST_PHASE_CHECKING);
				sysLogPrintf(LOG_NOTE,
				             "NET: ready gate active — %u clients CLSTATE_PREPARING deadline=%u",
				             (unsigned)total_count, (unsigned)s_ReadyGate.deadline_ticks);
			}
		}
	} else {
		/* Co-op or Counter-op — uses mission config */
		g_MissionConfig.stagenum = g_MpSetup.stagenum;
		g_MissionConfig.difficulty = difficulty;
		mainChangeToStage(g_MpSetup.stagenum);
		netServerCoopStageStart(g_MpSetup.stagenum, difficulty);
	}

	/* Return 0 regardless of src->error: the buffer overflow on CLC_LOBBY_START
	 * (31 bots × ~57 bytes exceeds the 1440-byte client out buffer) causes src->error
	 * to be set after successful processing, producing a false "malformed 0x08" warning.
	 * The message was handled correctly; don't abort the dispatch loop. */
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

	sysLogPrintf(LOG_NOTE, "NET: SVC_LOBBY_STATE: mode=%u stage=%u status=%u",
	             gamemode, stagenum, status);

	g_NetGameMode = gamemode;
	g_Lobby.settings.scenario = gamemode;
	g_Lobby.settings.stagenum = stagenum;
	g_Lobby.inGame = (status >= 2) ? 1 : 0;

	return src->error;
}

/* ============================================================
 * D3R-9: Network Distribution (protocol v20)
 * ============================================================ */

/* ---- Catalog entry collector (shared by SvcCatalogInfoWrite) ---- */

#define CATALOG_COLLECT_MAX 256

static const asset_entry_t *s_CatalogCollectBuf[CATALOG_COLLECT_MAX];
static s32 s_CatalogCollectN = 0;

static void catalogInfoCollectCb(const asset_entry_t *e, void *ud)
{
	(void)ud;
	if (!e->bundled && e->enabled && s_CatalogCollectN < CATALOG_COLLECT_MAX) {
		s_CatalogCollectBuf[s_CatalogCollectN++] = e;
	}
}

/* ---- SVC_CATALOG_INFO ---- */

u32 netmsgSvcCatalogInfoWrite(struct netbuf *dst)
{
	/* Collect all non-bundled enabled entries from the catalog */
	static const asset_type_e s_types[] = {
		ASSET_MAP, ASSET_CHARACTER, ASSET_SKIN, ASSET_BOT_VARIANT,
		ASSET_WEAPON, ASSET_TEXTURES, ASSET_SFX, ASSET_MUSIC,
		ASSET_PROP, ASSET_VEHICLE, ASSET_MISSION, ASSET_UI,
		ASSET_NONE  /* sentinel */
	};

	s_CatalogCollectN = 0;
	for (s32 ti = 0; s_types[ti] != ASSET_NONE; ti++) {
		assetCatalogIterateByType(s_types[ti], catalogInfoCollectCb, NULL);
	}

	netbufWriteU8(dst, SVC_CATALOG_INFO);
	netbufWriteU16(dst, (u16)s_CatalogCollectN);
	for (s32 i = 0; i < s_CatalogCollectN; i++) {
		const asset_entry_t *e = s_CatalogCollectBuf[i];
		/* v27: no net_hash on wire — catalog ID string only. */
		netbufWriteStr(dst, e->id);
		netbufWriteStr(dst, e->category);
	}
	return dst->error;
}

u32 netmsgSvcCatalogInfoRead(struct netbuf *src, struct netclient *srccl)
{
	(void)srccl;
	u16 count = netbufReadU16(src);
	if (count > CATALOG_COLLECT_MAX) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_CATALOG_INFO count %u exceeds limit", count);
		return 1;
	}

	/* v27: no net_hash on wire — collect catalog ID strings only. */
	char ids[CATALOG_COLLECT_MAX][64];
	char cats[CATALOG_COLLECT_MAX][64];

	for (u16 i = 0; i < count; i++) {
		char *id  = netbufReadStr(src);
		char *cat = netbufReadStr(src);
		strncpy(ids[i],  id  ? id  : "", 63);  ids[i][63]  = '\0';
		strncpy(cats[i], cat ? cat : "", 63);  cats[i][63] = '\0';
	}

	if (src->error) return src->error;

	netDistribClientHandleCatalogInfo((const char (*)[64])ids,
	                                  (const char (*)[64])cats,
	                                  count);
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
	if (count > 256) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_CATALOG_DIFF count %u exceeds limit", count);
		return 1;
	}

	/* v27: catalog ID strings only — no u32 net_hash on wire. */
	char missing_ids[256][CATALOG_ID_LEN];
	for (u16 i = 0; i < count; i++) {
		char *id = netbufReadStr(src);
		strncpy(missing_ids[i], id ? id : "", CATALOG_ID_LEN - 1);
		missing_ids[i][CATALOG_ID_LEN - 1] = '\0';
	}

	if (src->error) return src->error;

	netDistribServerHandleDiff(srccl, (const char (*)[CATALOG_ID_LEN])missing_ids, count, temporary);
	return src->error;
}

/* ---- SVC_DISTRIB_BEGIN ---- */

u32 netmsgSvcDistribBeginWrite(struct netbuf *dst, const char *catalog_id,
                                const char *category, u32 total_chunks, u32 archive_bytes)
{
	netbufWriteU8(dst, SVC_DISTRIB_BEGIN);
	/* v27: catalog ID string is the component identity — no net_hash on wire. */
	netbufWriteStr(dst, catalog_id);
	netbufWriteStr(dst, category);
	netbufWriteU32(dst, total_chunks);
	netbufWriteU32(dst, archive_bytes);
	return dst->error;
}

u32 netmsgSvcDistribBeginRead(struct netbuf *src, struct netclient *srccl)
{
	(void)srccl;
	/* v27: catalog ID string only — no u32 net_hash. */
	char *catalog_id  = netbufReadStr(src);
	char *category    = netbufReadStr(src);
	u32  total_chunks = netbufReadU32(src);
	u32  archive_bytes = netbufReadU32(src);

	if (src->error) return src->error;

	if (!catalog_id || !catalog_id[0]) {
		sysLogPrintf(LOG_WARNING, "NET: SVC_DISTRIB_BEGIN: empty catalog_id");
		return 1;
	}

	netDistribClientHandleBegin(catalog_id,
	                             category ? category : "",
	                             total_chunks, archive_bytes, 0);
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
	char *catalog_id = netbufReadStr(src);
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
	char *catalog_id = netbufReadStr(src);
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
	char *attacker = netbufReadStr(src);
	char *victim   = netbufReadStr(src);
	char *weapon   = netbufReadStr(src);
	u8    flags    = netbufReadU8(src);

	if (src->error) return src->error;

	netDistribClientHandleKillFeed(
		attacker ? attacker : "",
		victim   ? victim   : "",
		weapon   ? weapon   : "",
		flags);
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
 * Per-entry format: u32 net_hash, u8 type, u8 slot_index, str id,
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
		return 1;
	}

	sysLogPrintf(LOG_NOTE, "NET: SVC_MATCH_MANIFEST hash=0x%08x entries=%u",
	             (unsigned)manifest_hash, (unsigned)g_ClientManifest.num_entries);

	/* Phase C: check local catalog against manifest, send CLC_MANIFEST_STATUS */
	manifestCheck(&g_ClientManifest);

	return src->error;
}

/* ---- CLC_MANIFEST_STATUS ---- */

u32 netmsgClcManifestStatusWrite(struct netbuf *dst, u32 manifest_hash, u8 status,
                                  const char (*missing_ids)[CATALOG_ID_LEN], u8 num_missing)
{
	netbufWriteU8(dst, CLC_MANIFEST_STATUS);
	netbufWriteU32(dst, manifest_hash);
	netbufWriteU8(dst, status);
	netbufWriteU8(dst, num_missing);

	/* v27: catalog ID strings only — no u32 net_hash on wire. */
	for (u8 i = 0; i < num_missing; i++) {
		netbufWriteStr(dst, missing_ids ? missing_ids[i] : "");
	}

	return dst->error;
}

u32 netmsgClcManifestStatusRead(struct netbuf *src, struct netclient *srccl)
{
	const u32 manifest_hash = netbufReadU32(src);
	const u8  status        = netbufReadU8(src);
	const u8  num_missing   = netbufReadU8(src);

	if (src->error) {
		sysLogPrintf(LOG_WARNING, "NET: malformed CLC_MANIFEST_STATUS header");
		return 1;
	}

	sysLogPrintf(LOG_NOTE, "NET: CLC_MANIFEST_STATUS from client %u: hash=0x%08x status=%u missing=%u",
	             srccl ? srccl->id : 0xFF,
	             (unsigned)manifest_hash, (unsigned)status, (unsigned)num_missing);

	/* v27: catalog ID strings only — no u32 net_hash on wire.
	 * num_missing is u8 (0-255) — bounded by wire protocol. */
	char missing_ids[256][CATALOG_ID_LEN];
	u8   missing_count = 0;
	for (s32 mi = 0; mi < (s32)num_missing; mi++) {
		char *id = netbufReadStr(src);
		if (src->error) {
			sysLogPrintf(LOG_WARNING, "NET: CLC_MANIFEST_STATUS malformed at missing[%d]", mi);
			return 1;
		}
		sysLogPrintf(LOG_NOTE, "NET: MANIFEST MISSING[%d] id=%s", mi, id ? id : "(null)");
		strncpy(missing_ids[missing_count], id ? id : "", CATALOG_ID_LEN - 1);
		missing_ids[missing_count][CATALOG_ID_LEN - 1] = '\0';
		missing_count++;
	}

	/* Phase E: ready gate — update per-client readiness and check for completion.
	 * Dispatch on status: READY marks done, NEED_ASSETS queues Phase D transfer
	 * (client will send READY again after transfer), DECLINE removes from match. */
	if (s_ReadyGate.active && srccl) {
		s32 ci = (s32)(srccl - g_NetClients);
		if (ci >= 0 && ci < NET_MAX_CLIENTS &&
		    (s_ReadyGate.expected_mask & (1u << ci))) {

			if (status == MANIFEST_STATUS_READY) {
				s_ReadyGate.ready_mask |= (1u << ci);
				sysLogPrintf(LOG_NOTE, "NET: ready gate: client %d READY (%u/%u)",
				             ci, (unsigned)readyGatePopcount(s_ReadyGate.ready_mask),
				             (unsigned)s_ReadyGate.total_count);
				readyGateBroadcastCountdown(MANIFEST_PHASE_CHECKING);

			} else if (status == MANIFEST_STATUS_NEED_ASSETS) {
				/* Phase D: queue missing components; client re-sends READY when done */
				if (missing_count > 0) {
					sysLogPrintf(LOG_NOTE,
					             "NET: ready gate: client %d NEED_ASSETS (%u missing), queuing transfer",
					             ci, (unsigned)missing_count);
					netDistribServerHandleDiff(srccl,
					                           (const char (*)[CATALOG_ID_LEN])missing_ids,
					                           (u16)missing_count, 1);
				}
				readyGateBroadcastCountdown(MANIFEST_PHASE_TRANSFERRING);

			} else if (status == MANIFEST_STATUS_DECLINE) {
				s_ReadyGate.declined_mask |= (1u << ci);
				srccl->state = CLSTATE_LOBBY;  /* spectator — excluded from match */
				sysLogPrintf(LOG_NOTE, "NET: ready gate: client %d DECLINED (spectate)", ci);
				readyGateBroadcastCountdown(MANIFEST_PHASE_CHECKING);
			}

			readyGateCheck();
		}
	}

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
	sessionCatalogReceive(src);
	return src->error;
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

	/* Only valid during the MANIFEST_PHASE_LOADING countdown */
	if (!s_ReadyGate.active || !s_ReadyGate.countdown_active) {
		sysLogPrintf(LOG_NOTE, "NET: CLC_LOBBY_CANCEL from '%s' but no countdown active — ignored",
		             srccl->settings.name[0] ? srccl->settings.name : "?");
		return src->error;
	}

	/* Only clients that are currently preparing can cancel */
	if (srccl->state != CLSTATE_PREPARING) {
		return src->error;
	}

	sysLogPrintf(LOG_NOTE, "NET: CLC_LOBBY_CANCEL from '%s' — aborting countdown",
	             srccl->settings.name[0] ? srccl->settings.name : "?");

	readyGateAbort(srccl->settings.name[0] ? srccl->settings.name : "Player");

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
	char *name;

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

	/* Record who cancelled so the UI can display the message */
	g_MatchCancelledState.active = 1;
	if (name) {
		strncpy(g_MatchCancelledState.name, name, MATCH_CANCEL_NAME_LEN - 1);
		g_MatchCancelledState.name[MATCH_CANCEL_NAME_LEN - 1] = '\0';
	} else {
		g_MatchCancelledState.name[0] = '\0';
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

	g_NetLocalBotAuthority = true;
	sysLogPrintf(LOG_NOTE, "NET: SVC_BOT_AUTHORITY received — this client runs bot AI and relays positions");
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
	}

	return dst->error;
}

u32 netmsgClcBotMoveRead(struct netbuf *src, struct netclient *srccl)
{
	(void)srccl;

	const u8 count = netbufReadU8(src);

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

		if (src->error) {
			return src->error;
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
		netbufWriteU8(dst, room->client_count);
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

u32 netmsgClcRoomCreateWrite(struct netbuf *dst, const char *name)
{
	netbufWriteU8(dst, CLC_ROOM_CREATE);
	netbufWriteStr(dst, name ? name : "");
	return dst->error;
}

u32 netmsgClcRoomCreateRead(struct netbuf *src, struct netclient *srccl)
{
	const char *name = netbufReadStr(src);
	if (src->error) return src->error;

	if (g_NetMode != NETMODE_SERVER) return src->error;

	/* Leave current room first if in one */
	if (srccl->room_id != 0xFF) {
		hub_room_t *old = roomGetById(srccl->room_id);
		if (old) roomLeave(old, srccl->id);
	}

	/* Generate name if not provided */
	char genName[ROOM_NAME_MAX];
	if (!name || name[0] == '\0') {
		roomGenerateName(genName, sizeof(genName));
		name = genName;
	}

	hub_room_t *room = roomCreateConfigured(name, 32, ROOM_ACCESS_OPEN, NULL, srccl->id);
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

	/* Broadcast updated room list to everyone */
	netBroadcastRoomList();

	return src->error;
}

u32 netmsgClcRoomJoinWrite(struct netbuf *dst, u8 room_id)
{
	netbufWriteU8(dst, CLC_ROOM_JOIN);
	netbufWriteU8(dst, room_id);
	return dst->error;
}

u32 netmsgClcRoomJoinRead(struct netbuf *src, struct netclient *srccl)
{
	u8 room_id = netbufReadU8(src);
	if (src->error) return src->error;

	if (g_NetMode != NETMODE_SERVER) return src->error;

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

	/* Broadcast updated room list */
	netBroadcastRoomList();

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

	if (srccl->room_id == 0xFF) return src->error;

	hub_room_t *room = roomGetById(srccl->room_id);
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

	/* Broadcast updated room list */
	netBroadcastRoomList();

	return src->error;
}

void netBroadcastRoomList(void)
{
	if (g_NetMode != NETMODE_SERVER) return;

	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcRoomListWrite(&g_NetMsgRel);
	netSend(NULL, &g_NetMsgRel, true, NETCHAN_DEFAULT);
}
