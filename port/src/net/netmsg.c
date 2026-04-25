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
#include "crashbreadcrumb.h"
#include "romdata.h"
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
#include "net/matchsetup.h"
#include "net/sessioncatalog.h"
#include "room.h"
#include "scenario_save.h"
#include "assetcatalog.h"
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
#endif
#include <SDL.h>
#include <stdarg.h>

/* Desync detection and resync constants */
#define NET_DESYNC_THRESHOLD   3   // consecutive desyncs before requesting resync
#define NET_RESYNC_COOLDOWN    300 // minimum frames between resync requests (~5 sec at 60fps)

/* Phase E: Ready gate — timeout before forcing SVC_STAGE_START (30s at 60fps) */
#define READY_GATE_TIMEOUT_TICKS 1800

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
} s_ReadyGate;

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
	if (in->weaponnum < WEAPON_NONE || in->weaponnum > WEAPON_SUICIDEPILL) {
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

/* MASTER-C3: Client-side identity cookie storage.
 * Cleared on disconnect (netDisconnect calls netmsgClcAuthClearCookie).
 * The server hands us this value in SVC_AUTH; we echo it back on reconnect
 * inside the same process.  All-zero means "fresh join, no preserve intent". */
#if !defined(PD_SERVER)
static u8 s_AuthCookie[NET_AUTH_COOKIE_LEN];

void netmsgClcAuthClearCookie(void)
{
	memset(s_AuthCookie, 0, sizeof(s_AuthCookie));
}

void netmsgClcAuthStoreCookie(const u8 cookie[NET_AUTH_COOKIE_LEN])
{
	memcpy(s_AuthCookie, cookie, sizeof(s_AuthCookie));
}
#endif

u32 netmsgClcAuthWrite(struct netbuf *dst)
{
	const char *modDir = fsGetModDir();
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

	/* MASTER-C3: include the previously-issued cookie (zeros on first join).
	 * Server uses it to disambiguate reconnect vs fresh join, defeating the
	 * name-spoof preserved-slot hijack described in SEC-3. */
#if !defined(PD_SERVER)
	netbufWriteData(dst, s_AuthCookie, NET_AUTH_COOKIE_LEN);
#else
	/* Server code path — should never be taken (server doesn't write CLC_AUTH). */
	u8 zeros[NET_AUTH_COOKIE_LEN];
	memset(zeros, 0, sizeof(zeros));
	netbufWriteData(dst, zeros, NET_AUTH_COOKIE_LEN);
#endif

	return dst->error;
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

	/* MASTER-C3: issue a fresh cookie for this peer BEFORE checking the preserved
	 * table.  If the client is reconnecting it will present the previously-issued
	 * cookie in suppliedCookie; if we accept the restore, the NEW cookie replaces
	 * the old one in the preserved record (one-time use for defence-in-depth). */
	netServerIssueCookie(srccl->auth_cookie);

	// check if this is a mid-game reconnection
	struct netpreservedplayer *pp = NULL;
	const bool ingame = (g_NetLocalClient && g_NetLocalClient->state >= CLSTATE_GAME);
	if (ingame) {
		/* Check for all-zero cookie (fresh join with preserved-name collision). */
		bool cookieZero = true;
		for (s32 ci = 0; ci < NET_AUTH_COOKIE_LEN; ci++) {
			if (suppliedCookie[ci]) { cookieZero = false; break; }
		}

		if (!cookieZero) {
			/* MASTER-C3: reconnect requires both name AND cookie match. */
			pp = netServerFindPreservedByCookie(name, suppliedCookie);
			if (!pp) {
				/* Name may exist in preserved table but cookie doesn't match — that's
				 * exactly the hijack attempt SEC-3 warns about.  Log it and reject. */
				struct netpreservedplayer *nameOnly = netServerFindPreserved(name);
				if (nameOnly) {
					sysLogPrintf(LOG_WARNING,
						"NET: CLC_AUTH cookie mismatch for preserved name '%s' — possible hijack attempt from client %u",
						name, srccl->id);
				}
				netServerKick(srccl, DISCONNECT_LATE);
				return src->error;
			}
		} else {
			/* Fresh-join cookie during an in-progress match: legacy path rejects late
			 * joins, so deny regardless of whether a preserved slot exists.  Only
			 * cookie-bearing reconnects are allowed. */
			sysLogPrintf(LOG_NOTE, "NET: %s rejected: mid-game join without cookie", name);
			netServerKick(srccl, DISCONNECT_LATE);
			return src->error;
		}
	}

	srccl->state = CLSTATE_LOBBY;
	++g_NetNumClients; /* M-7: Increment only after successful auth (moved from netServerEvConnect). */

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
	const char *name = netbufReadStr(src);

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
	/* MASTER-C3: ship the server-issued identity cookie to the client.  The
	 * client echoes this back on any subsequent reconnect to reclaim its
	 * preserved slot. */
	netbufWriteData(dst, authcl->auth_cookie, NET_AUTH_COOKIE_LEN);
	return dst->error;
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

#if !defined(PD_SERVER)
	/* Persist the cookie so the next CLC_AUTH (on reconnect) can present it. */
	extern void netmsgClcAuthStoreCookie(const u8 cookie[NET_AUTH_COOKIE_LEN]);
	netmsgClcAuthStoreCookie(cookie);
#endif

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
	const char *msg = netbufReadStr(src);
	if (msg && !src->error) {
		sysLogPrintf(LOG_CHAT, "%s", msg);
	}
	return src->error;
}


u32 netmsgSvcStageStartWrite(struct netbuf *dst)
{
	/* S301 Airbase diag: capture every SVC_STAGE_START send with breadcrumb
	 * so the Airbase "no SVC_STAGE_START" repro shows whether the send
	 * even began. Tagged MATCHSTART.DIAG so it can be grepped separately
	 * from the existing MATCH-START: warnings. */
	sysLogPrintf(LOG_NOTE,
		"MATCHSTART.DIAG: SVC_STAGE_START write begin tick=%u seed=0x%llx matchSeed=%u "
		"stage_id='%s' mode=%d",
		(unsigned)g_NetTick, (unsigned long long)g_RngSeed, (unsigned)g_NetMatchSeed,
		g_MpSetup.stage_id, (int)g_NetGameMode);
	crashBreadcrumbPush("SVC_STAGE_START.write stage='%s' mode=%d tick=%u",
		g_MpSetup.stage_id, (int)g_NetGameMode, (unsigned)g_NetTick);

	netbufWriteU8(dst, SVC_STAGE_START);

	netbufWriteU32(dst, g_NetTick);

	netbufWriteU64(dst, g_RngSeed);
	netbufWriteU64(dst, g_Rng2Seed);
	/* L2-4: match_seed for deterministic spawn pool generation.
	 * All clients receive this and store in g_NetMatchSeed so future
	 * spawnpool.c can produce identical spawn pools on every machine. */
	netbufWriteU32(dst, g_NetMatchSeed);

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
		/* v36: authoritative anti player slot for Counter-Op.
		 * NET_NULL_CLIENT / 0xFF means "none / unused". */
		netbufWriteU8(dst, g_Vars.antiplayernum >= 0 ? (u8)g_Vars.antiplayernum : NET_NULL_CLIENT);
	} else {
		// combat simulator settings
		/* M0.1d: scenario as catalog ID string. Prefer g_MatchConfig.scenario_id
		 * (PRIMARY), fall back to runtime resolution from integer. */
		{
			const char *sid = g_MatchConfig.scenario_id[0]
				? g_MatchConfig.scenario_id
				: catalogIdByRuntime(ASSET_GAMEMODE, (s32)g_MpSetup.scenario);
			netbufWriteStr(dst, sid ? sid : "base:combat");
		}
		netbufWriteU8(dst, g_MpSetup.scorelimit);
		netbufWriteU8(dst, g_MpSetup.timelimit);
		netbufWriteU16(dst, g_MpSetup.teamscorelimit);
		/* v37: active-slot bitmap derived from the participant pool.
		 * Bits 0..MAX_PLAYERS-1 = players, MAX_PLAYERS..MAX_MPCHRS-1 = bots. */
		netbufWriteU64(dst, mpParticipantsEncodeActiveMask());
		netbufWriteU32(dst, g_MpSetup.options);
		/* SA-3: weapons as session IDs (NUM_MPWEAPONSLOTS u16 entries) */
		{
			s32 wi;
			for (wi = 0; wi < NUM_MPWEAPONSLOTS; wi++) {
				if (g_MpSetup.weapons[wi] == 0) {
					catalogWriteAssetRef(dst, 0);
				} else {
					const char *wcanon = catalogIdByRuntime(
						ASSET_WEAPON, (s32)g_MpSetup.weapons[wi]);
					if (wcanon) {
						catalogWriteAssetRef(dst, sessionCatalogGetId(wcanon));
					} else {
						catalogWriteAssetRef(dst, 0);
					}
				}
			}
		}
		/* B-125: spawn_weapon_id as catalog ID string — clients need this
		 * to resolve spawnWeaponNum on their side for player spawn. */
		netbufWriteStr(dst, g_MatchConfig.spawn_weapon_id[0]
			? g_MatchConfig.spawn_weapon_id : "");

		/* A-7: mod track ID for network-synced mod audio.
		 * Host resolves one track from the playlist (shuffle/sequential)
		 * and sends that single ID so all clients play the same music.
		 * Server build has no audio state — write empty string. */
#if !defined(PD_SERVER)
		{
			const char *wireTrack = "";
			if (audioGetModPlaylistCount() > 0) {
				wireTrack = audioPickNextPlaylistTrack();
			} else {
				wireTrack = audioGetModTrackId();
			}
			netbufWriteStr(dst, wireTrack ? wireTrack : "");
		}
#else
		netbufWriteStr(dst, "");
#endif
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
			netbufWriteU8(dst, g_PlayerConfigsArray[ncl->playernum].handicap); /* U-9: per-player handicap */
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
		/* Pre-build bot array index → g_MatchConfig.slots[] index mapping.
		 * matchConfigAddBot() appends sequentially, so with 1 player in slot 0
		 * bots occupy slots 1, 2, 3... — NOT starting at MAX_PLAYERS. */
		s32 botSlotMap[MATCH_MAX_SLOTS];
		s32 botMapCount = 0;
		for (s32 si = 0; si < g_MatchConfig.numSlots && botMapCount < MATCH_MAX_SLOTS; si++) {
			if (g_MatchConfig.slots[si].type == SLOT_BOT) {
				botSlotMap[botMapCount++] = si;
			}
		}

		for (s32 botidx = 0; botidx < MAX_BOTS; botidx++) {
			if (!mpIsParticipantActive(botidx + MAX_PLAYERS)) {
				continue;
			}
			struct mpbotconfig *bc = &g_BotConfigsArray[botidx];
			netbufWriteStr(dst, bc->base.name);

			/* SA-3 / FIX-7: bot body/head as session IDs.
			 * On dedicated server, mpbodynum is 0 (unresolved) so we use the
			 * catalog ID strings from g_MatchConfig.slots[] (populated from
			 * CLC_LOBBY_START). On client/listen server, fall back to mpbodynum
			 * resolution for backward compat. */
			{
				const char *body_canon = NULL;
				const char *head_canon = NULL;

				/* Map bot array index to actual slot position */
				s32 slotIdx = (botidx < botMapCount) ? botSlotMap[botidx] : -1;
				if (slotIdx >= 0 && g_MatchConfig.slots[slotIdx].body_id[0]) {
					body_canon = g_MatchConfig.slots[slotIdx].body_id;
				}
				if (slotIdx >= 0 && g_MatchConfig.slots[slotIdx].head_id[0]) {
					head_canon = g_MatchConfig.slots[slotIdx].head_id;
				}

				/* Fallback: resolve from mpbodynum/mpheadnum via cached lookup. */
				if (!body_canon) {
					body_canon = catalogIdByRuntime(ASSET_BODY, (s32)bc->base.mpbodynum);
				}
				if (!head_canon) {
					head_canon = catalogIdByRuntime(ASSET_HEAD, (s32)bc->base.mpheadnum);
				}

				catalogWriteAssetRef(dst, body_canon ? sessionCatalogGetId(body_canon) : 0);
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

	if (srccl->state != CLSTATE_LOBBY && srccl->state != CLSTATE_GAME
	    && srccl->state != CLSTATE_PREPARING) {
		sysLogPrintf(LOG_WARNING,
			"MATCHSTART.DIAG: SVC_STAGE reject — client state=%u not in LOBBY/GAME/PREPARING",
			srccl->state);
		return 1;
	}

	g_NetTick = netbufReadU32(src);

	g_NetRngSeeds[0] = netbufReadU64(src);
	g_NetRngSeeds[1] = netbufReadU64(src);
	g_NetRngLatch = true;
	/* L2-4: match_seed for deterministic spawn pools */
	g_NetMatchSeed = netbufReadU32(src);

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
	u8 antiPlayerNumWire = NET_NULL_CLIENT;
	g_NetGameMode = mode;

	/* Phase 2: resolve stage catalog ID for all paths below */
	const char *resolved_stage_id = "";
	{
		catalog_stage_result_t tmp_sr;
		if (catalogResolveStageBySession(stage_session, &tmp_sr) && tmp_sr.entry) {
			resolved_stage_id = tmp_sr.entry->id;
		}
	}

	if (mode == NETGAMEMODE_COOP || mode == NETGAMEMODE_ANTI) {
		// co-op / counter-op mission settings
		g_MissionConfig.stagenum = stagenum;
		/* Phase 2: populate PRIMARY catalog ID string field */
		strncpy(g_MissionConfig.stage_id, resolved_stage_id, sizeof(g_MissionConfig.stage_id) - 1);
		g_MissionConfig.stage_id[sizeof(g_MissionConfig.stage_id) - 1] = '\0';
		g_MissionConfig.difficulty = netbufReadU8(src);
		g_MissionConfig.iscoop = (mode == NETGAMEMODE_COOP);
		g_MissionConfig.isanti = (mode == NETGAMEMODE_ANTI);
		g_NetCoopFriendlyFire = netbufReadU8(src);
		g_NetCoopRadar = netbufReadU8(src);
		antiPlayerNumWire = netbufReadU8(src);
	} else {
		// combat simulator settings
		g_MpSetup.stagenum = stagenum;
		/* Phase 2: populate PRIMARY catalog ID string field */
		strncpy(g_MpSetup.stage_id, resolved_stage_id, sizeof(g_MpSetup.stage_id) - 1);
		g_MpSetup.stage_id[sizeof(g_MpSetup.stage_id) - 1] = '\0';
		/* M0.1d: scenario as catalog ID string (v32+). */
		{
			const char *scid_str = netbufReadStr(src);
			const char *scid = scid_str ? scid_str : "";
			if (scid[0]) {
				const asset_entry_t *gm = assetCatalogResolve(scid);
				if (gm && gm->type == ASSET_GAMEMODE) {
					g_MpSetup.scenario = (u8)gm->ext.gamemode.mode_id;
				} else {
					sysLogPrintf(LOG_ERROR,
						"NET: SVC_STAGE_START scenario '%s' not in catalog — defaulting to combat",
						scid);
					g_MpSetup.scenario = 0;
				}
			} else {
				g_MpSetup.scenario = 0;
			}
		}
		g_MpSetup.scorelimit = netbufReadU8(src);
		g_MpSetup.timelimit = netbufReadU8(src);
		g_MpSetup.teamscorelimit = netbufReadU16(src);
		{
			/* v37: active-slot bitmap → participant pool. */
			u64 active_mask = netbufReadU64(src);
			mpParticipantsDecodeActiveMask(active_mask);
		}
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
		/* B-125: read spawn_weapon_id and resolve to spawnWeaponNum. */
		{
			const char *swid_str = netbufReadStr(src);
			const char *swid = swid_str ? swid_str : "";
			if (swid[0]) {
				strncpy(g_MatchConfig.spawn_weapon_id, swid, sizeof(g_MatchConfig.spawn_weapon_id) - 1);
				g_MatchConfig.spawn_weapon_id[sizeof(g_MatchConfig.spawn_weapon_id) - 1] = '\0';
				const asset_entry_t *swe = assetCatalogResolve(swid);
				if (swe && swe->type == ASSET_WEAPON) {
					s32 mpw = swe->ext.weapon.weapon_id;
					if (mpw > 0 && mpw < NUM_MPWEAPONS) {
						g_MatchConfig.spawnWeaponNum = catalogGetMpWeaponNum(mpw);
					} else {
						g_MatchConfig.spawnWeaponNum = 0xFF;
					}
				} else {
					g_MatchConfig.spawnWeaponNum = 0xFF;
				}
			} else {
				g_MatchConfig.spawn_weapon_id[0] = '\0';
				g_MatchConfig.spawnWeaponNum = 0xFF;
			}
			sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE_START spawn weapon '%s' → weaponnum=%d",
				g_MatchConfig.spawn_weapon_id[0] ? g_MatchConfig.spawn_weapon_id : "(random)",
				(s32)g_MatchConfig.spawnWeaponNum);
		}

		/* A-7: read host's mod track ID for network-synced mod audio.
		 * If non-empty, the client will use this mod track at match start
		 * instead of their own selection — host's music is authoritative.
		 * Server build discards this field (no audio state). */
		{
			const char *modtrack_str = netbufReadStr(src);
#if !defined(PD_SERVER)
			const char *modtrack = modtrack_str ? modtrack_str : "";
			if (modtrack[0]) {
				const asset_entry_t *ae = assetCatalogResolve(modtrack);
				if (ae && ae->ext.audio.file_path[0]) {
					audioSetModTrackId(modtrack);
					sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE_START mod track '%s' from host",
						modtrack);
				} else {
					sysLogPrintf(LOG_WARNING, "NET: SVC_STAGE_START: mod track '%s' not available (late join?) — skipping",
						modtrack);
				}
			}
			/* If empty, keep client's own mod track setting — host has no mod music */
#else
			(void)modtrack_str;
#endif
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
			g_PlayerConfigsArray[ncl->playernum].handicap = netbufReadU8(src); /* U-9: per-player handicap */
			body_session = catalogReadAssetRef(src);
			head_session = catalogReadAssetRef(src);
			ncl->settings.fovy = netbufReadF32(src);
			ncl->settings.fovzoommult = netbufReadF32(src);
			{
				const char *name = netbufReadStr(src);
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
			/* skip our own settings except for team, playernum, and handicap */
			netbufReadU16(src);
			g_PlayerConfigsArray[ncl->playernum].handicap = netbufReadU8(src); /* U-9: leader may have changed it */
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
			u32 playernum = ncl->playernum;
			if (ncl->id == 0) {
				if (!g_NetLocalClient) {
					sysLogPrintf(LOG_WARNING, "NET: SvcStageStartRead missing local client for server-slot remap");
					continue;
				}
				playernum = g_NetLocalClient->playernum;
			} else if (g_NetLocalClient && ncl == g_NetLocalClient) {
				playernum = g_NetClients[0].playernum;
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
			s32 antiPlayerNum = (antiPlayerNumWire == NET_NULL_CLIENT)
				? ((numplayers > 1) ? 1 : -1)
				: (s32)antiPlayerNumWire;
			if (antiPlayerNum >= MAX_PLAYERS) {
				antiPlayerNum = (numplayers > 1) ? 1 : -1;
			}
			g_Vars.coopplayernum = -1;
			g_Vars.antiplayernum = antiPlayerNum;
		}

		/* Dismiss countdown overlay before changing stage. */
		memset(&g_MatchCountdownState, 0, sizeof(g_MatchCountdownState));
		menuStop();
#if !defined(PD_SERVER)
		/* Phase 2 / Priority K-b3: pool slot cleanup is sufficient.
		 * menupoolReleaseAll pops every owned ctx (including unregistered-
		 * fallback after K-b1), so the legacy paired
		 * inputCtxPopDeferred(&g_CtxImGuiMenu) is no longer needed. */
		menupoolReleaseAll();
#endif

		g_NotLoadMod = true;
		romdataFileFreeForSolo();

		titleSetNextStage(stagenum);
		setNumPlayers(numplayers);
		lvSetDifficulty(g_MissionConfig.difficulty);
		titleSetNextMode(TITLEMODE_SKIP);
		sysLogPrintf(LOG_WARNING, "MATCH-START: calling mainChangeToStage, stagenum=0x%x (co-op)", (unsigned)stagenum);
		mainChangeToStage(stagenum);

#if !defined(PD_SERVER)
		/* L2-2: Notify server that this co-op client's stage is loaded.
		 * Mirrors the CombatSim CLC_STAGE_READY at line ~1307.
		 * On dedicated servers this enables bot authority delegation;
		 * on listen servers it confirms the client is ready. */
		if (g_NetMode == NETMODE_CLIENT && g_NetLocalClient) {
			sysLogPrintf(LOG_NOTE, "NET: sending CLC_STAGE_READY (co-op)");
			netbufStartWrite(&g_NetMsgRel);
			netmsgClcStageReadyWrite(&g_NetMsgRel);
			netSend(g_NetLocalClient, &g_NetMsgRel, true, NETCHAN_DEFAULT);
		}
#endif

#if VERSION >= VERSION_NTSC_1_0
		viBlack(true);
#endif
	} else {
		sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE from server: going to stage 0x%02x with %u players activeMask=0x%llx",
			g_MpSetup.stagenum, numplayers,
			(unsigned long long)mpParticipantsEncodeActiveMask());

#if !defined(PD_SERVER)
		/* Apply catalog-validated body/head to player config arrays.
		 * matchStart() does this for offline/listen-server mode; we must mirror it
		 * here because bodyreset() has already NULLed all modeldef pointers
		 * and nothing else reloads them on the client path — playerTickChrBody()
		 * would crash on frame 0 dereferencing a NULL modeldef. */
		for (u32 pcl = 0; pcl < NET_MAX_CLIENTS; ++pcl) {
			struct netclient *pncl = &g_NetClients[pcl];
			if (pncl->state == CLSTATE_GAME) {
				u32 pnum = pncl->playernum;
				if (pncl->id == 0) {
					if (!g_NetLocalClient) {
						sysLogPrintf(LOG_WARNING, "NET: SvcStageStartRead missing local client for body/head remap");
						continue;
					}
					pnum = g_NetLocalClient->playernum;
				} else if (g_NetLocalClient && pncl == g_NetLocalClient) {
					pnum = g_NetClients[0].playernum;
				}
				if (pnum < MAX_PLAYERS) {
					/* Phase 8: validate catalog IDs, derive mp indices from entry */
					const char *vbody = catalogValidateBodyId(pncl->settings.body_id);
					const char *vhead = catalogValidateHeadId(pncl->settings.head_id);
					const asset_entry_t *be = assetCatalogResolve(vbody);
					const asset_entry_t *he = assetCatalogResolve(vhead);
					g_PlayerConfigsArray[pnum].base.mpbodynum = (be && be->mp_index >= 0) ? (u8)be->mp_index : 0;
					g_PlayerConfigsArray[pnum].base.mpheadnum = (he && he->mp_index >= 0) ? (u8)he->mp_index : 0;
					/* PRIMARY catalog ID string fields */
					strncpy(g_PlayerConfigsArray[pnum].base.body_id, vbody, sizeof(g_PlayerConfigsArray[pnum].base.body_id) - 1);
					g_PlayerConfigsArray[pnum].base.body_id[sizeof(g_PlayerConfigsArray[pnum].base.body_id) - 1] = '\0';
					strncpy(g_PlayerConfigsArray[pnum].base.head_id, vhead, sizeof(g_PlayerConfigsArray[pnum].base.head_id) - 1);
					g_PlayerConfigsArray[pnum].base.head_id[sizeof(g_PlayerConfigsArray[pnum].base.head_id) - 1] = '\0';
					sysLogPrintf(LOG_NOTE, "NET: player %u body='%s'->%u head='%s'->%u",
						pnum, pncl->settings.body_id,
						g_PlayerConfigsArray[pnum].base.mpbodynum,
						pncl->settings.head_id,
						g_PlayerConfigsArray[pnum].base.mpheadnum);
				}
			}
		}

		/* Receive per-bot configs from wire as catalog/session references.
		 * The write side (netmsgSvcStageStartWrite) serialises name + body/head
		 * session refs + difficulty/type for every active bot slot. */
		for (s32 botidx = 0; botidx < MAX_BOTS; botidx++) {
			if (!mpIsParticipantActive(botidx + MAX_PLAYERS)) {
				continue;
			}
			/* Zero-init body/head each iteration so stale values never leak */
			g_BotConfigsArray[botidx].base.mpbodynum = 0;
			g_BotConfigsArray[botidx].base.mpheadnum = 0;
			g_BotConfigsArray[botidx].base.body_id[0] = '\0';
			g_BotConfigsArray[botidx].base.head_id[0] = '\0';

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

			/* Phase 8: resolve session → catalog ID, validate, derive mp_index */
			{
				const asset_entry_t *be = sessionCatalogLocalResolve(body_session);
				const asset_entry_t *he = sessionCatalogLocalResolve(head_session);
				const char *vbody = (be && be->type == ASSET_BODY) ? catalogValidateBodyId(be->id) : "base:dark_combat";
				const char *vhead = (he && he->type == ASSET_HEAD) ? catalogValidateHeadId(he->id) : "base:head_dark_combat";
				const asset_entry_t *vbe = assetCatalogResolve(vbody);
				const asset_entry_t *vhe = assetCatalogResolve(vhead);
				g_BotConfigsArray[botidx].base.mpbodynum = (vbe && vbe->mp_index >= 0) ? (u8)vbe->mp_index : 0;
				g_BotConfigsArray[botidx].base.mpheadnum = (vhe && vhe->mp_index >= 0) ? (u8)vhe->mp_index : 0;
				strncpy(g_BotConfigsArray[botidx].base.body_id, vbody, sizeof(g_BotConfigsArray[botidx].base.body_id) - 1);
				g_BotConfigsArray[botidx].base.body_id[sizeof(g_BotConfigsArray[botidx].base.body_id) - 1] = '\0';
				strncpy(g_BotConfigsArray[botidx].base.head_id, vhead, sizeof(g_BotConfigsArray[botidx].base.head_id) - 1);
				g_BotConfigsArray[botidx].base.head_id[sizeof(g_BotConfigsArray[botidx].base.head_id) - 1] = '\0';
			}
			sysLogPrintf(LOG_NOTE, "NET: bot %d name='%s' body='%s'->%u head='%s'->%u (sessions %u/%u)",
				botidx, g_BotConfigsArray[botidx].base.name,
				sessionCatalogLocalResolve(body_session) ? sessionCatalogLocalResolve(body_session)->id : "?",
				g_BotConfigsArray[botidx].base.mpbodynum,
				sessionCatalogLocalResolve(head_session) ? sessionCatalogLocalResolve(head_session)->id : "?",
				g_BotConfigsArray[botidx].base.mpheadnum,
				(unsigned)body_session, (unsigned)head_session);
		}
#endif /* !PD_SERVER */

		/* Participant pool was already populated above (mpParticipantsDecodeActiveMask
		 * on the active-slot bitmap read from the wire). mpStartMatch does NOT
		 * rebuild participants for NETMODE_CLIENT, so the pool must be authoritative
		 * on arrival — otherwise mpHasSimulants() would return false and bots would
		 * never spawn on the client side. */

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
		menupoolReleaseAll();
		/* U-10: Notify server that this client's stage is loaded and ready for bot authority.
		 * Sent here (after mpStartMatch + scenarioInitProps) as the earliest reliable point
		 * where the client's stage geometry and pads are in flight.  The 60-frame gate in
		 * bot.c provides defence-in-depth for any remaining async load on the game thread. */
		if (g_NetMode == NETMODE_CLIENT && g_NetLocalClient) {
			sysLogPrintf(LOG_NOTE, "NET: sending CLC_STAGE_READY");
			netbufStartWrite(&g_NetMsgRel);
			netmsgClcStageReadyWrite(&g_NetMsgRel);
			netSend(g_NetLocalClient, &g_NetMsgRel, true, NETCHAN_DEFAULT);
		}
#endif
	}

	return 0;
}

u32 netmsgSvcStageEndWrite(struct netbuf *dst, u8 room_id)
{
	netbufWriteU8(dst, SVC_STAGE_END);
	netbufWriteU8(dst, (u8)g_NetGameMode);

	sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE_END write mode=%u room=%u", g_NetGameMode, room_id);

	if (g_NetGameMode == NETGAMEMODE_COOP || g_NetGameMode == NETGAMEMODE_ANTI) {
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
	manifestClear(&g_ClientManifest);

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
 * The catalog stores weapon_id as MPWEAPON_* constants (0x01-0x2f),
 * but the game engine uses WEAPON_* enums (different numbering).
 * catalogGetMpWeaponNum(idx) maps MPWEAPON_* → WEAPON_*.
 * These helpers bridge the two domains. */

#if !defined(PD_SERVER)
/* Convert WEAPON_* enum → MPWEAPON_* index by scanning catalog. */
static s32 weaponToMpWeapon(s32 weaponnum)
{
	for (s32 i = 1; i < NUM_MPWEAPONS; i++) {
		if (catalogGetMpWeaponNum(i) == weaponnum) {
			return i;
		}
	}
	return -1;
}

/* Convert MPWEAPON_* index → WEAPON_* enum via catalog. */
static s32 mpWeaponToWeapon(s32 mpweaponnum)
{
	if (mpweaponnum >= 0 && mpweaponnum < NUM_MPWEAPONS) {
		return catalogGetMpWeaponNum(mpweaponnum);
	}
	return WEAPON_UNARMED;
}
#endif

static void netWriteWeaponRef(struct netbuf *dst, s32 weaponnum)
{
#if !defined(PD_SERVER)
	s32 mpw = weaponToMpWeapon(weaponnum);
	const char *wid = (mpw >= 0) ? catalogIdByRuntime(ASSET_WEAPON, mpw) : NULL;
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
		return mpWeaponToWeapon(wr.weapon_num);
	}
#endif
	return WEAPON_UNARMED;
}

/* Write a model number (g_ModelStates[] index) as a session catalog u16 ref.
 * All g_ModelStates entries are registered as ASSET_MODEL in the catalog. */
static void netWriteModelRef(struct netbuf *dst, s32 modelnum)
{
	const char *id = catalogIdByRuntime(ASSET_MODEL, modelnum);
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

	const s32 prevplayernum = g_Vars.currentplayernum;

	if (actcl->playernum >= MAX_PLAYERS) {
		sysLogPrintf(LOG_WARNING, "NET: SvcPlayerStatsRead invalid playernum %d", actcl->playernum);
		return 1;
	}

	setCurrentPlayerNum(actcl->playernum);

	const bool newisdead = (flags & (1 << 0)) != 0;
	if (!pl->isdead && newisdead) {
		/* B-256 third-site closure (2026-04-25): the original 7fbc5833
		 * commit fixed chr.c (bot pit-fall path) and player.c (playerDie
		 * symmetric path) but missed this network-replicated death path
		 * that drives MP killfeed/scoreboard for non-self deaths in 2+
		 * peer matches. lastshooter / timeshooter are dead fields (only
		 * ever written to -1); the live attribution data is in
		 * lastattacker, set by chraction.c on damage. Mirror the chr.c
		 * shape exactly: resolve via mpPlayerGetIndex, fall back to the
		 * current player on unknown / -1.
		 *
		 * If 2+ peer playtest still shows mis-attribution after this
		 * site lands, the structural answer is encoding attacker_id
		 * directly in SVC_PLAYER_STATS (v42 -> v43 protocol bump).
		 * Out of scope for this patch. */
		s16 shooter;
		if (pl->prop->chr->lastattacker) {
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
	netPropMarkDirty(prop->syncid);
	netPropSnapUpdate(prop);
	netbufWriteU8(dst, SVC_PROP_DAMAGE);
	netbufWritePropPtr(dst, prop);
	netbufWriteCoord(dst, pos);
	netbufWriteF32(dst, prop->obj->damage);
	netbufWriteF32(dst, damage);
	netWriteWeaponRef(dst, weaponnum);
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
	const s32 weaponnum = netReadWeaponRef(src);
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

/*
 * Event-driven dirty flag tracking for prop state sync.
 *
 * The write functions (SvcPropMove, SvcPropDoor, etc.) mark a prop dirty
 * when they send state. The 120-tick PROP_SYNC heartbeat only CRCs dirty
 * props, then clears the flags. If nothing changed since the last heartbeat,
 * the scan and the message are skipped entirely (O(1) vs O(N_props)).
 *
 * Must match NET_PROP_MAP_SIZE: syncids are (prop - g_Vars.props + 1) so
 * they can reach maxprops which is bounded by the 2048-slot sync ID map.
 * A smaller value here silently drops dirty marks for high-index props.
 */
#define NET_PROP_DIRTY_MAXSYNCID NET_PROP_MAP_SIZE

static u8  s_PropDirtyFlags[NET_PROP_DIRTY_MAXSYNCID];
static s32 s_PropDirtyCount = 0;

void netPropMarkDirty(u32 syncid)
{
	if (syncid > 0 && syncid < NET_PROP_DIRTY_MAXSYNCID) {
		if (!s_PropDirtyFlags[syncid]) {
			s_PropDirtyFlags[syncid] = 1;
			s_PropDirtyCount++;
		}
	}
}

static void netPropDirtyBitsClear(void)
{
	if (s_PropDirtyCount > 0) {
		memset(s_PropDirtyFlags, 0, sizeof(s_PropDirtyFlags));
		s_PropDirtyCount = 0;
	}
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
 * Payload (v36+): gamemode (u8), stage_id (str catalog ID),
 *          difficulty (u8), antiClientId (u8, NET_NULL_CLIENT when unused),
 *          numSims (u8), simType (u8),
 *          timelimit (u8), options (u32), scenario_id (str catalog ID), scorelimit (u8), teamscorelimit (u16),
 *          weaponSetIndex (u8, 0xFF = custom/default),
 *          weapons (str[NUM_MPWEAPONSLOTS]) — per-slot ASSET_WEAPON catalog ID string
 *          Per-bot (repeated numSims times): name (str), body_id (str), head_id (str),
 *          botDifficulty (u8), botType (u8)
 *
 * v27: all asset references are catalog ID strings — no u32 net_hash on wire.
 * stage_id written from g_MatchConfig.stage_id (Single Source of Truth).
 * Server resolves stage_id → stagenum and weapon IDs → weapon_id via assetCatalogResolve().
 * ======================================================================== */

u32 netmsgClcLobbyStartWrite(struct netbuf *dst, u8 gamemode, u8 stagenum, u8 difficulty, u8 antiClientId, u8 numSims, u8 simType, u8 timelimit, u32 options, u8 scenario, u8 scorelimit, u16 teamscorelimit, u8 weaponSetIndex)
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
	netbufWriteU8(dst, antiClientId);
	netbufWriteU8(dst, numSims);
	netbufWriteU8(dst, simType);
	netbufWriteU8(dst, timelimit);
	netbufWriteU32(dst, options);
	/* M0.1d: scenario as catalog ID string (PRIMARY). Integer scenario parameter
	 * is still accepted for backward compat but we send the catalog ID. */
	if (g_MatchConfig.scenario_id[0]) {
		netbufWriteStr(dst, g_MatchConfig.scenario_id);
	} else {
		/* Fallback: resolve from integer */
		const char *sid = catalogIdByRuntime(ASSET_GAMEMODE, (s32)scenario);
		netbufWriteStr(dst, sid ? sid : "base:combat");
	}
	netbufWriteU8(dst, scorelimit);
	netbufWriteU16(dst, teamscorelimit);
	netbufWriteU8(dst, weaponSetIndex);
	/* M0.1c: per-slot weapon catalog ID — prefer weapon_ids[] (PRIMARY),
	 * fall back to runtime resolution from g_MpSetup.weapons[]. */
	{
		s32 wi;
		for (wi = 0; wi < NUM_MPWEAPONSLOTS; wi++) {
			if (g_MatchConfig.weapon_ids[wi][0]) {
				netbufWriteStr(dst, g_MatchConfig.weapon_ids[wi]);
			} else if (g_MpSetup.weapons[wi] == 0) {
				netbufWriteStr(dst, "");
			} else {
				const char *wcanon = catalogIdByRuntime(
					ASSET_WEAPON, (s32)g_MpSetup.weapons[wi]);
				netbufWriteStr(dst, wcanon ? wcanon : "");
			}
		}
	}

	/* B-125: spawn_weapon_id as catalog ID string (PRIMARY).
	 * Matches weapon_ids[] pattern above — sent as string, resolved on server. */
	netbufWriteStr(dst, g_MatchConfig.spawn_weapon_id[0]
		? g_MatchConfig.spawn_weapon_id : "");

	/* U-9: per-player handicap bytes — one per player slot */
	for (s32 hi = 0; hi < MAX_PLAYERS; hi++) {
		netbufWriteU8(dst, g_PlayerConfigsArray[hi].handicap);
	}

	/* Per-bot config: iterate bot slots in g_MatchConfig in order.
	 * Count must equal numSims; extra slots are skipped, missing ones
	 * write empty defaults so the server always reads exactly numSims entries.
	 *
	 * Catalog-ID-native: send body_id/head_id strings directly — the server
	 * resolves them via assetCatalogResolve() → runtime_index, then stores
	 * directly as mpbodynum/mpheadnum via entry->mp_index.
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

/* Bug B: forward declaration — readyGateAbort is defined below but called from
 * readyGateTickCountdown's defensive room-missing check. */
static void readyGateAbort(const char *canceller_name);

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
		sysLogPrintf(LOG_NOTE, "NET: countdown complete -- launching match (stage %u room %u mode %u)",
		             (unsigned)s_ReadyGate.stagenum, (unsigned)s_ReadyGate.room_id,
		             (unsigned)s_ReadyGate.game_mode);
		s_ReadyGate.active           = 0;
		s_ReadyGate.countdown_active = 0;

		hub_room_t *room = (s_ReadyGate.room_id != 0xFF) ? roomGetById(s_ReadyGate.room_id) : roomGetById(0);
		if (room) {
			roomTransition(room, ROOM_STATE_LOADING);
		}

		/* R-3: Set the match room before calling netServerStageStart so it can scope broadcasts */
		extern u8 g_NetMatchRoomId;
		g_NetMatchRoomId = s_ReadyGate.room_id;

		/* L2-1: Branch launch path by game mode.  CombatSim uses
		 * netServerStageStart(); co-op/anti uses netServerCoopStageStart()
		 * which sets up g_MissionConfig and player numbers differently. */
		if (s_ReadyGate.game_mode == NETGAMEMODE_COOP ||
		    s_ReadyGate.game_mode == NETGAMEMODE_ANTI) {
			netServerCoopStageStart(s_ReadyGate.stagenum, s_ReadyGate.difficulty);
		} else {
			mainChangeToStage(s_ReadyGate.stagenum);
			netServerStageStart();
		}
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
	room = roomGetById(s_ReadyGate.room_id);
	if (room) {
		roomTransition(room, ROOM_STATE_LOBBY);
	}

	/* Broadcast the cancellation so all clients can show the message */
	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcMatchCancelledWrite(&g_NetMsgRel, canceller_name ? canceller_name : "?");
	netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL);
}

/* Bug B: called from room teardown paths.  If the gate targets this room
 * (or any room when gate is active with room_id == 0xFF — co-op), abort
 * and broadcast SVC_MATCH_CANCELLED. */
void netReadyGateAbortForRoom(u8 room_id, const char *reason)
{
	if (!s_ReadyGate.active) return;
	if (s_ReadyGate.room_id != 0xFF && s_ReadyGate.room_id != room_id) return;
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

u32 netmsgClcLobbyStartRead(struct netbuf *src, struct netclient *srccl)
{
	u8 gamemode              = netbufReadU8(src);
	/* SEC-26: clamp untrusted wire value to declared enum range.
	 * Out-of-range bytes would silently index g_NetGameMode beyond its
	 * intended states; warn and fall back to standard MP. */
	if (gamemode != NETGAMEMODE_MP && gamemode != NETGAMEMODE_COOP &&
	    gamemode != NETGAMEMODE_ANTI) {
		sysLogPrintf(LOG_WARNING,
		    "NET: CLC_LOBBY_START from client %u: invalid gamemode=%u — defaulting to MP",
		    srccl->id, (unsigned)gamemode);
		gamemode = NETGAMEMODE_MP;
	}
	/* C-2: read arena as catalog ID string (v27+), resolve via assetCatalogResolve().
	 * No fallback — if the string doesn't resolve, stagenum=0 and an error is logged.
	 * stage_id is also stored in g_MatchConfig for server-side reference. */
	{
		const char *stage_id_str = netbufReadStr(src);
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
	const u8 antiClientId    = netbufReadU8(src);
	const u8 numSims         = netbufReadU8(src);
	const u8 simType         = netbufReadU8(src);
	const u8 timelimit       = netbufReadU8(src);
	const u32 options        = netbufReadU32(src);
	/* M0.1d: read scenario as catalog ID string (v32+), resolve to integer. */
	u8 scenario = 0;
	{
		const char *scenario_str = netbufReadStr(src);
		const char *scid = scenario_str ? scenario_str : "";
		if (scid[0]) {
			const asset_entry_t *gm = assetCatalogResolve(scid);
			if (gm && gm->type == ASSET_GAMEMODE) {
				scenario = (u8)gm->ext.gamemode.mode_id;
			} else {
				sysLogPrintf(LOG_ERROR,
					"NET: CLC_LOBBY_START scenario '%s' not in catalog — defaulting to combat",
					scid);
			}
			strncpy(g_MatchConfig.scenario_id, scid, sizeof(g_MatchConfig.scenario_id) - 1);
			g_MatchConfig.scenario_id[sizeof(g_MatchConfig.scenario_id) - 1] = '\0';
		} else {
			g_MatchConfig.scenario_id[0] = '\0';
		}
	}
	const u8 scorelimit      = netbufReadU8(src);
	const u16 teamscorelimit = netbufReadU16(src);
	const u8 weaponSetIndex  = netbufReadU8(src);
	/* C-2: per-slot weapon catalog ID string — resolve each to weapon_id.
	 * No fallback: if a non-empty string can't resolve, log error and use 0 (no weapon). */
	{
		s32 wi;
		for (wi = 0; wi < NUM_MPWEAPONSLOTS; wi++) {
			const char *wid_str = netbufReadStr(src);
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

	/* B-125: read spawn_weapon_id catalog string and resolve to spawnWeaponNum.
	 * Mirrors matchStart() resolution logic for the network path. */
	{
		const char *swid_str = netbufReadStr(src);
		const char *swid = swid_str ? swid_str : "";
		if (swid[0]) {
			strncpy(g_MatchConfig.spawn_weapon_id, swid, sizeof(g_MatchConfig.spawn_weapon_id) - 1);
			g_MatchConfig.spawn_weapon_id[sizeof(g_MatchConfig.spawn_weapon_id) - 1] = '\0';
			const asset_entry_t *swe = assetCatalogResolve(swid);
			if (swe && swe->type == ASSET_WEAPON) {
				s32 mpw = swe->ext.weapon.weapon_id;
				if (mpw > 0 && mpw < NUM_MPWEAPONS) {
					g_MatchConfig.spawnWeaponNum = catalogGetMpWeaponNum(mpw);
				} else {
					g_MatchConfig.spawnWeaponNum = 0xFF;
				}
			} else {
				sysLogPrintf(LOG_WARNING,
					"NET: CLC_LOBBY_START spawn_weapon_id '%s' not in catalog — defaulting to Random", swid);
				g_MatchConfig.spawnWeaponNum = 0xFF;
			}
		} else {
			g_MatchConfig.spawn_weapon_id[0] = '\0';
			g_MatchConfig.spawnWeaponNum = 0xFF;
		}
		sysLogPrintf(LOG_NOTE, "NET: CLC_LOBBY_START spawn weapon '%s' → weaponnum=%d",
			g_MatchConfig.spawn_weapon_id[0] ? g_MatchConfig.spawn_weapon_id : "(random)",
			(s32)g_MatchConfig.spawnWeaponNum);
	}

	/* U-9: per-player handicap bytes */
	for (s32 hi = 0; hi < MAX_PLAYERS; hi++) {
		g_PlayerConfigsArray[hi].handicap = netbufReadU8(src);
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
		/* Fallback: if no explicit leader exists yet, allow any lobby sender. */
		if (!isLeader && leaderSlot == 0xFF) {
			isLeader = (srccl->state >= CLSTATE_LOBBY);
		}
	}

	if (!isLeader) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_LOBBY_START rejected from client %d (%s) — not room creator",
		             srccl->id, srccl->settings.name);
		return src->error;
	}

	sysLogPrintf(LOG_NOTE, "NET: CLC_LOBBY_START from leader %s: gamemode=%u stage='%s'(num=%u) diff=%u antiClient=%u tl=%u opt=0x%08x",
	             srccl->settings.name, gamemode, g_MatchConfig.stage_id, (unsigned)g_MpSetup.stagenum,
	             difficulty, (unsigned)antiClientId, timelimit, (unsigned)options);

	/* Apply settings */
	g_NetGameMode = gamemode;
	g_NetCounterOpClientId = NET_NULL_CLIENT;
	if (gamemode == NETGAMEMODE_ANTI) {
		g_NetCounterOpClientId = antiClientId;
		if (antiClientId == NET_NULL_CLIENT || antiClientId >= NET_MAX_CLIENTS) {
			sysLogPrintf(LOG_WARNING,
			             "NET: CLC_LOBBY_START rejected — invalid Counter-Op anti client id %u",
			             (unsigned)antiClientId);
			return src->error;
		}
		{
			struct netclient *antiCl = &g_NetClients[antiClientId];
			if (antiCl->state < CLSTATE_LOBBY) {
				sysLogPrintf(LOG_WARNING,
				             "NET: CLC_LOBBY_START rejected — anti client %u not lobby-ready (state=%u)",
				             (unsigned)antiClientId, (unsigned)antiCl->state);
				return src->error;
			}
			if (srccl->room_id != 0xFF && antiCl->room_id != srccl->room_id) {
				sysLogPrintf(LOG_WARNING,
				             "NET: CLC_LOBBY_START rejected — anti client %u not in leader room %u",
				             (unsigned)antiClientId, (unsigned)srccl->room_id);
				return src->error;
			}
		}
	}

	/* Start the match based on game mode.
	 * For Combat Sim: load the requested stage and start.
	 * For Co-op/Counter-op: load the solo stage and start. */
	if (gamemode == 0) {
		/* Combat Simulator
		 *
		 * Configure g_MpSetup fully before netServerStageStart(), which
		 * broadcasts SVC_STAGE_START. The participant pool carries the slot
		 * layout (v37). Without populating it here, clients receive a zero
		 * active-slot mask and mpStartMatch() never spawns anyone. */
		/* g_MpSetup.stagenum already set above from arena catalog ID resolution */
		g_MpSetup.scenario        = scenario;
		g_MpSetup.timelimit       = timelimit; /* 0..59 = minutes, >=60 = unlimited */
		g_MpSetup.scorelimit      = scorelimit;
		g_MpSetup.teamscorelimit  = teamscorelimit;
		g_MpSetup.options         = options;
		/* g_MpSetup.weapons[] populated above by FIX-4 per-slot weapon catalog ID string resolution. */

		/* Assign sequential playernums and populate the participant pool.
		 * Slots 0..n-1 represent the n connected players (B-12 Phase 3). */
		mpParticipantPoolInit(MAX_MPCHRS);
		s32 pnum = 0;
		for (s32 ci = 0; ci < NET_MAX_CLIENTS && pnum < MAX_PLAYERS; ci++) {
			struct netclient *ncl = &g_NetClients[ci];
			if (ncl->state == CLSTATE_LOBBY || ncl->state == CLSTATE_GAME) {
				ncl->playernum = (u8)pnum;
				mpAddParticipantAt(pnum, PARTICIPANT_REMOTE, 0, (s8)ci, 0);
				sysLogPrintf(LOG_NOTE, "NET: assigned playernum %d to client %d (%s)",
				             pnum, ci, ncl->settings.name);
				pnum++;
			}
		}
		/* M-S2: clear gap slots between players (0..pnum-1) and bots (MAX_PLAYERS+)
		 * so stale data from previous matches doesn't leak through. */
		g_MatchConfig.numSlots = (u8)pnum;
		for (s32 gap = pnum; gap < MAX_PLAYERS; gap++) {
			memset(&g_MatchConfig.slots[gap], 0, sizeof(g_MatchConfig.slots[gap]));
		}

		/* Add simulant (bot) slots at MAX_PLAYERS..MAX_PLAYERS+numSims-1.
		 * Issue E: clamp against BOTH MAX_BOTS (g_BotConfigsArray cap) AND
		 * MATCH_PARTICIPANT_CAP - pnum (CHR.TICK combined-count ceiling).
		 * A malicious/stale client could send numSims=32 with 1 connected
		 * player, which used to produce 33 total participants and crash the
		 * engine at slot=32 chrnum=5033.  The combined clamp mirrors
		 * matchConfigMaxBotsForHumans() on the client. */
		s32 bycap = (s32)MATCH_PARTICIPANT_CAP - pnum;
		if (bycap < 0) bycap = 0;
		s32 maxSims = bycap < MAX_BOTS ? bycap : MAX_BOTS;
		u8 clampedSims = (numSims > (u8)maxSims) ? (u8)maxSims : numSims;
		if (clampedSims != numSims) {
			sysLogPrintf(LOG_NOTE,
			             "NET: CLC_LOBBY_START sims clamped %u -> %u "
			             "(humans=%d, cap=%d, MAX_BOTS=%d)",
			             (unsigned)numSims, (unsigned)clampedSims,
			             pnum, (int)MATCH_PARTICIPANT_CAP, (int)MAX_BOTS);
		}
		for (s32 bi = 0; bi < clampedSims; bi++) {
			s32 slot = MAX_PLAYERS + bi;
			mpAddParticipantAt(slot, PARTICIPANT_BOT, 0, -1, 0xFF);
			/* Catalog-ID-native: read body_id/head_id strings, resolve to
			 * mpbodynum/mpheadnum here at the server read site — the ONLY
			 * integer-domain conversion point for bot characters. */
			const char *botName    = netbufReadStr(src);
			const char *body_id    = netbufReadStr(src); /* catalog ID e.g. "base:dark_combat" */
			const char *head_id    = netbufReadStr(src); /* catalog ID e.g. "base:head_dark_combat" */
			const u8 botDifficulty = netbufReadU8(src);
			const u8 botType       = netbufReadU8(src);
			/* L-S3: NULL guard on netbufReadStr results */
			if (!botName) botName = "";
			if (!body_id) body_id = "";
			if (!head_id) head_id = "";
			if (src->error) {
				/* Malformed — fall back to global simType, normal difficulty */
				g_BotConfigsArray[bi].type       = simType;
				g_BotConfigsArray[bi].difficulty = 2;
				continue;
			}
			g_BotConfigsArray[bi].type       = botType;
			g_BotConfigsArray[bi].difficulty = botDifficulty;

			/* FIX-PLAYTEST-1: Store body_id/head_id into g_MatchConfig.slots[]
			 * so SVC_STAGE_START write can find them.  Without this, the write
			 * path falls back to "base:dark_combat" →
			 * dark_combat for every bot. */
			if (slot < MATCH_MAX_SLOTS) {
				g_MatchConfig.slots[slot].type = SLOT_BOT;
				g_MatchConfig.slots[slot].botType = botType;
				g_MatchConfig.slots[slot].botDifficulty = botDifficulty;
				if (body_id && body_id[0]) {
					strncpy(g_MatchConfig.slots[slot].body_id, body_id,
					        sizeof(g_MatchConfig.slots[slot].body_id) - 1);
					g_MatchConfig.slots[slot].body_id[sizeof(g_MatchConfig.slots[slot].body_id) - 1] = '\0';
				} else {
					g_MatchConfig.slots[slot].body_id[0] = '\0';
				}
				if (head_id && head_id[0]) {
					strncpy(g_MatchConfig.slots[slot].head_id, head_id,
					        sizeof(g_MatchConfig.slots[slot].head_id) - 1);
					g_MatchConfig.slots[slot].head_id[sizeof(g_MatchConfig.slots[slot].head_id) - 1] = '\0';
				} else {
					g_MatchConfig.slots[slot].head_id[0] = '\0';
				}
				if (botName && botName[0]) {
					strncpy(g_MatchConfig.slots[slot].name, botName,
					        sizeof(g_MatchConfig.slots[slot].name) - 1);
					g_MatchConfig.slots[slot].name[sizeof(g_MatchConfig.slots[slot].name) - 1] = '\0';
				}
				if (slot >= g_MatchConfig.numSlots) {
					g_MatchConfig.numSlots = (u8)(slot + 1);
				}
			}

			/* Phase 8: validate catalog IDs, derive mp_index from entry */
#ifndef PD_SERVER
			{
				const char *vbody = catalogValidateBodyId(body_id);
				const char *vhead = catalogValidateHeadId(head_id);
				const asset_entry_t *be = assetCatalogResolve(vbody);
				const asset_entry_t *he = assetCatalogResolve(vhead);
				g_BotConfigsArray[bi].base.mpbodynum =
					(be && be->mp_index >= 0) ? (u8)be->mp_index : 0;
				g_BotConfigsArray[bi].base.mpheadnum =
					(he && he->mp_index >= 0) ? (u8)he->mp_index : 0;
				/* Phase 2: populate PRIMARY catalog ID string fields */
				if (body_id && body_id[0]) {
					strncpy(g_BotConfigsArray[bi].base.body_id, body_id, sizeof(g_BotConfigsArray[bi].base.body_id) - 1);
					g_BotConfigsArray[bi].base.body_id[sizeof(g_BotConfigsArray[bi].base.body_id) - 1] = '\0';
				} else {
					g_BotConfigsArray[bi].base.body_id[0] = '\0';
				}
				if (head_id && head_id[0]) {
					strncpy(g_BotConfigsArray[bi].base.head_id, head_id, sizeof(g_BotConfigsArray[bi].base.head_id) - 1);
					g_BotConfigsArray[bi].base.head_id[sizeof(g_BotConfigsArray[bi].base.head_id) - 1] = '\0';
				} else {
					g_BotConfigsArray[bi].base.head_id[0] = '\0';
				}
			}
#endif
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
		sysLogPrintf(LOG_NOTE, "NET: Combat Sim setup: stage=0x%02x activeMask=0x%llx players=%d sims=%d type=%d",
		             (unsigned)g_MpSetup.stagenum,
		             (unsigned long long)mpParticipantsEncodeActiveMask(),
		             pnum, clampedSims, simType);

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
			s_ReadyGate.game_mode     = NETGAMEMODE_MP;
			s_ReadyGate.difficulty    = 0;
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
		/* L2-1: Co-op or Counter-op -- uses mission config.
		 * Now routed through the manifest pipeline + ready gate, matching
		 * the CombatSim path.  Previously this was an instant start that
		 * bypassed all asset verification.
		 *
		 * Flow: build manifest -> broadcast SVC_MATCH_MANIFEST -> enter
		 * ready gate -> countdown -> netServerCoopStageStart(). */
		g_MissionConfig.stagenum = g_MpSetup.stagenum;
		strncpy(g_MissionConfig.stage_id, g_MpSetup.stage_id, sizeof(g_MissionConfig.stage_id) - 1);
		g_MissionConfig.stage_id[sizeof(g_MissionConfig.stage_id) - 1] = '\0';
		g_MissionConfig.difficulty = difficulty;

		/* Build manifest for co-op mission: stage + player bodies/heads + mods.
		 * manifestBuild() reads g_MpSetup.stage_id, g_NetClients[], g_MatchConfig.slots[],
		 * and modmgrGetCount/GetMod -- all already populated by CLC_LOBBY_START parsing.
		 * S303 NOTE: unlike the Combat Sim branch above (which consumes the
		 * host-provided CLC_LOBBY_START manifest payload), co-op/anti
		 * deliberately rebuilds from the server's own g_NetClients view.
		 * This means host-side mod entries present in the payload but not
		 * yet reflected in server state would be silently dropped — log
		 * the inputs so discrepancies are visible in pd-server.log. */
		manifestBuild(&g_ServerManifest, NULL, NULL);
		sysLogPrintf(LOG_NOTE,
			"GAMELOOP.%s: server-side manifest built entries=%d hash=0x%08x stage='%s' clients=%u",
			(gamemode == NETGAMEMODE_COOP) ? "COOP" : "COUNTEROP",
			g_ServerManifest.num_entries, g_ServerManifest.manifest_hash,
			g_MpSetup.stage_id[0] ? g_MpSetup.stage_id : "(empty)",
			g_NetNumClients);
		sysLogPrintf(LOG_NOTE, "NET: co-op manifest built: %d entries, hash=0x%08x",
		             g_ServerManifest.num_entries, g_ServerManifest.manifest_hash);

		/* Broadcast manifest + session catalog to all clients */
		sessionCatalogBuild(&g_ServerManifest);
		sessionCatalogBroadcast();
		{
			netbufStartWrite(&g_NetMsgRel);
			netmsgSvcMatchManifestWrite(&g_NetMsgRel, &g_ServerManifest);
			netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL);
		}

		/* Enter the ready gate -- reuse same state machine as CombatSim */
		{
			u32 expected_mask = 0;
			u8  total_count   = 0;
			for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
				if (g_NetClients[ci].state == CLSTATE_LOBBY) {
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
			s_ReadyGate.game_mode     = g_NetGameMode;
			s_ReadyGate.difficulty    = difficulty;
			s_ReadyGate.room_id       = 0xFF; /* co-op is global, no room scoping */

			if (total_count == 0) {
				/* No clients -- fire immediately */
				s_ReadyGate.active = 0;
				netServerCoopStageStart(g_MpSetup.stagenum, difficulty);
			} else {
				readyGateBroadcastCountdown(MANIFEST_PHASE_CHECKING);
				sysLogPrintf(LOG_NOTE,
				             "NET: co-op ready gate active -- %u clients CLSTATE_PREPARING deadline=%u",
				             (unsigned)total_count, (unsigned)s_ReadyGate.deadline_ticks);
			}
		}
	}

	/* H-S1: NET_CLIENT_BUFSIZE is now 16KB (was 1440) — large enough for
	 * 40 slots × ~57 bytes + header.  Buffer overflow no longer occurs. */
	return src->error;
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
	/* Collect all non-bundled enabled entries from the catalog.
	 * A-7: ASSET_AUDIO added for mod audio network distribution.
	 * S-9: ASSET_SKIN already present — skin mods distributed via same pipeline. */
	static const asset_type_e s_types[] = {
		ASSET_MAP, ASSET_CHARACTER, ASSET_SKIN, ASSET_BOT_VARIANT,
		ASSET_WEAPON, ASSET_TEXTURES, ASSET_SFX, ASSET_MUSIC,
		ASSET_PROP, ASSET_VEHICLE, ASSET_MISSION, ASSET_UI,
		ASSET_AUDIO,
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
		const char *id  = netbufReadStr(src);
		const char *cat = netbufReadStr(src);
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
		const char *id = netbufReadStr(src);
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
	const char *catalog_id  = netbufReadStr(src);
	const char *category    = netbufReadStr(src);
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
		const char *id = netbufReadStr(src);
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
	}

	return dst->error;
}

u32 netmsgClcBotMoveRead(struct netbuf *src, struct netclient *srccl)
{
	const u8 count = netbufReadU8(src);
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

	/* SEC-13: rate-limit room mutations (1/sec/client). */
	if (!netmsgRoomRateAllow(srccl)) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_ROOM_CREATE rate-limited for client %u", srccl->id);
		return src->error;
	}

	/* Leave current room first if in one */
	if (srccl->room_id != 0xFF) {
		hub_room_t *old = roomGetById(srccl->room_id);
		if (old) roomLeave(old, srccl->id);
	}

	/* Generate name if not provided */
	char genName[ROOM_NAME_MAX];
	const char *finalName = nameBuf;
	if (!finalName[0]) {
		roomGenerateName(genName, sizeof(genName));
		finalName = genName;
	}

	/* SEC-14: validate access mode.  Invite-only is declared but not yet
	 * implemented — treat as OPEN server-side so clients that try it don't
	 * get silently locked out. */
	room_access_t access = ROOM_ACCESS_OPEN;
	if (accessRaw == ROOM_ACCESS_PASSWORD) {
		/* Require a non-empty password for password rooms. */
		if (!passwordBuf[0]) {
			sysLogPrintf(LOG_WARNING, "NET: CLC_ROOM_CREATE from client %u: password room with empty password — downgrading to OPEN", srccl->id);
			access = ROOM_ACCESS_OPEN;
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

	if (srccl->room_id == 0xFF) return src->error;

	/* SEC-13: rate-limit room mutations (1/sec/client). */
	if (!netmsgRoomRateAllow(srccl)) {
		sysLogPrintf(LOG_WARNING, "NET: CLC_ROOM_LEAVE rate-limited for client %u", srccl->id);
		return src->error;
	}

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
 * Sent by the client immediately after mpStartMatch() completes in the
 * SVC_STAGE_START handler.  When the server receives this from every
 * CLSTATE_GAME client it sends SVC_BOT_AUTHORITY to the first ready client,
 * ensuring the authority client's stage geometry and pad list are in flight
 * before botSpawnAll() is triggered.
 *
 * A 5-second / 300-frame timeout in netEndFrame() delegates authority anyway
 * if a slow client never responds, so the match is never permanently blocked.
 * ======================================================================== */

u32 netmsgClcStageReadyWrite(struct netbuf *dst)
{
	netbufWriteU8(dst, CLC_STAGE_READY);
	return dst->error;
}

u32 netmsgClcStageReadyRead(struct netbuf *src, struct netclient *srccl)
{
	if (src->error) {
		return src->error;
	}

	/* Only meaningful on a dedicated server during a match startup. */
	if (!g_NetDedicated || g_NetBotAuthorityDelegated) {
		return 0;
	}

	if (srccl->state != CLSTATE_GAME) {
		sysLogPrintf(LOG_WARNING, "NET: ignored CLC_STAGE_READY from client %u (state=%u, not GAME)",
		             srccl->id, srccl->state);
		return 0;
	}

	srccl->stage_ready = true;

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
	g_MatchConfig.options        = options;
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

	/* Rebroadcast as SVC_ROOM_SETTINGS to all other room members. */
	u8 bcastData[256];
	struct netbuf bcast;
	bcast.data = bcastData;
	bcast.size = sizeof(bcastData);
	netbufStartWrite(&bcast);
	netmsgSvcRoomSettingsWrite(&bcast, numBots, timelimit, scorelimit,
	                           teamscorelimit, options, scenario,
	                           weaponSetIndex, stage_id);

	for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
		struct netclient *ncl = &g_NetClients[ci];
		if (ncl == srccl) continue;
		if (ncl->state < CLSTATE_LOBBY) continue;
		if (ncl->room_id != srccl->room_id) continue;
		netSend(ncl, &bcast, true, NETCHAN_CONTROL);
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

	u8 bcastData[AUDIO_MAX_PLAYLIST * 65 + 4];
	struct netbuf bcast;
	bcast.data = bcastData;
	bcast.size = sizeof(bcastData);
	netbufStartWrite(&bcast);
	netmsgSvcRoomPlaylistWrite(&bcast, clamped);

	for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
		struct netclient *ncl = &g_NetClients[ci];
		if (ncl == srccl) continue;
		if (ncl->state < CLSTATE_LOBBY) continue;
		if (ncl->room_id != srccl->room_id) continue;
		netSend(ncl, &bcast, true, NETCHAN_CONTROL);
	}

	return src->error;
}

/* ---- Convenience senders (called from C++ room screen / audio layer) ---- */

void netSendRoomSettingsUpdate(void)
{
	if (!g_NetLocalClient) return;

	u8 numBots = 0;
	for (s32 i = 1; i < g_MatchConfig.numSlots; i++) {
		if (g_MatchConfig.slots[i].type == SLOT_BOT) numBots++;
	}
	u8 wpnIdx = (g_MatchConfig.weaponSetIndex >= 0)
	            ? (u8)g_MatchConfig.weaponSetIndex : 0xFF;

	netbufStartWrite(&g_NetLocalClient->out);
	netmsgClcRoomSettingsUpdateWrite(
	    &g_NetLocalClient->out, numBots,
	    g_MatchConfig.timelimit, g_MatchConfig.scorelimit,
	    g_MatchConfig.teamscorelimit, g_MatchConfig.options,
	    g_MatchConfig.scenario, wpnIdx, g_MatchConfig.stage_id);

	if (g_NetMode == NETMODE_CLIENT) {
		netSend(g_NetLocalClient, NULL, true, NETCHAN_CONTROL);
		return;
	}

	/* In-client listen host: run the same server handler as CLC without ENet. */
	if (g_NetMode == NETMODE_SERVER && !g_NetDedicated) {
		struct netbuf rb;
		netbufStartReadData(&rb, g_NetLocalClient->out.data, g_NetLocalClient->out.wp);
		netmsgClcRoomSettingsUpdateRead(&rb, g_NetLocalClient);
		netbufStartWrite(&g_NetLocalClient->out);
	}
}

void netSendRoomPlaylistUpdate(void)
{
	if (!g_NetLocalClient) return;

	/* Serialize current playlist to semicolon-delimited string. */
	char pl[AUDIO_MAX_PLAYLIST * 65];
	s32  pos = 0;
	pl[0] = '\0';
	s32 n = audioGetModPlaylistCount();
	for (s32 i = 0; i < n; i++) {
		const char *id = audioGetModPlaylistEntry(i);
		if (!id || !id[0]) continue;
		if (pos > 0 && pos < (s32)sizeof(pl) - 1) pl[pos++] = ';';
		s32 left = (s32)sizeof(pl) - pos - 1;
		if (left <= 0) break;
		s32 len = (s32)strlen(id);
		if (len > left) len = left;
		memcpy(pl + pos, id, (size_t)len);
		pos += len;
	}
	pl[pos] = '\0';

	netbufStartWrite(&g_NetLocalClient->out);
	netmsgClcRoomPlaylistUpdateWrite(&g_NetLocalClient->out, pl);

	if (g_NetMode == NETMODE_CLIENT) {
		netSend(g_NetLocalClient, NULL, true, NETCHAN_CONTROL);
		return;
	}

	if (g_NetMode == NETMODE_SERVER && !g_NetDedicated) {
		struct netbuf rb;
		netbufStartReadData(&rb, g_NetLocalClient->out.data, g_NetLocalClient->out.wp);
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
					enet_peer_disconnect(srccl->peer, DISCONNECT_ADMIN_AUTH);
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
