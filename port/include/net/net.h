#ifndef _IN_NET_H
#define _IN_NET_H

#include "types.h"
#include "constants.h"
#include "net/netbuf.h"
#include "assetcatalog.h"

/* Forward declaration — avoids pulling enet.h into every translation unit */
typedef struct _ENetAddress ENetAddress;

#define NET_PROTOCOL_VER 51  /* v51 (2026-06-17): c3849 Wave 7 cuts the
                              * weapon graph runtime over to product-default
                              * ON, retires the old user toggle plus its
                              * transient stage-start options bit, and relies
                              * on the auth handshake to reject mixed v50/v51
                              * play.
                              * v50 (2026-06-08): SVC_CATALOG_INFO is now
                              * batched as [u16 total_count][u16 batch_offset]
                              * [u16 count] plus id/category string rows, so
                              * large custom typed-archive packs are advertised
                              * without the old 256-entry truncation. CLC
                              * catalog diffs and CLC_MANIFEST_STATUS missing
                              * lists now use heap-backed u16 counts instead
                              * of fixed 256/u8 buffers, and distribution
                              * queues grow instead of dropping entries after
                              * 64 pending transfers. Mixed v49/v50 play is
                              * rejected at the ENet auth handshake.
                              * v49 (2026-05-17): CLC_LOBBY_RESYNC (0x18) so a
                              * client can ask the server to re-broadcast room
                              * assignment + lobby settings after a match ends.
                              * Closes the "MP post-match returns me to an
                              * unsynced room" bug. Server reply is a server-
                              * initiated SVC_ROOM_ASSIGN (existing 0x76)
                              * followed by the standard SVC_ROOM_SETTINGS /
                              * SVC_ROOM_PLAYLIST broadcasts so receivers do
                              * not need new SVC handling. Mixed v48/v49 play
                              * rejected at the ENet auth handshake.
                              * v48 (2026-05-16): Track 2d (c3807) GPU swarm
                              * state network sync. Adds SVC_GPUSWARM_STATE (0x6c)
                              * carrying the listen-host's RGBA32F state-texture
                              * readback, quantized to 20 bytes/bot (s16 cm pos,
                              * s16 cm/frame vel, s8 ratio surface_up, plus 5
                              * bytes of AI ints) and chunked to stay under ENet's
                              * per-packet practical ceiling (~64 KB). Wire frame
                              * header is [u32 frame_idx][u16 total_count]
                              * [u16 chunk_start][u16 chunk_count] before the
                              * packed bot bytes; total_count is the whole pool
                              * count, chunk_count is what's in THIS packet.
                              * Throttled to 10 Hz (every 6 frames @ 60 Hz) on
                              * the unreliable channel. Listen-host only (Mode B);
                              * dedicated servers do not originate. v47/v48 mixed
                              * play is rejected at the ENet auth handshake --
                              * v47 readers do not consume the new opcode and
                              * would mis-parse subsequent traffic.
                              * v47 (2026-05-01): S594h-B Slice 3 surface-normal
                              * locomotion wire sync. SVC_NPC_MOVE and
                              * SVC_BOT_AUTHORITY each gain a trailing
                              * 12-byte surface_up vec3 (3 x f32) carrying the
                              * authoritative chr local-up vector. Skedars
                              * (RACE_SKEDAR) and any chr with the per-chr
                              * SURFACE_LOCO_FLAG_PER_CHR_ENABLE override use
                              * this to align their model rotation to the
                              * floor surface normal across host/client.
                              * Receiver writes the wire vec3 directly into
                              * chr->surface_up; the per-tick sample +
                              * 8-frame blend continues to run locally so
                              * normal-change transitions look smooth even
                              * on packet-loss. Mixed v46/v47 play is rejected
                              * at the ENet auth handshake -- v46 readers do
                              * not consume the trailing 12 bytes and would
                              * mis-parse subsequent fields.
                              * v46 (2026-04-28): SEC-5 mandatory mod-transfer
                              * hash on SVC_DISTRIB_BEGIN, plus cutscene
                              * authority wire cleanup. SVC_DISTRIB_BEGIN now
                              * appends a 32-byte SHA-256 digest of the exact
                              * compressed PDCA archive bytes sent through
                              * SVC_DISTRIB_CHUNK. Clients reject zero digests
                              * at BEGIN and verify the digest before
                              * decompression/extraction at END. SVC_CUTSCENE
                              * now carries active + player_mask, and clients
                              * request server-side skips with
                              * CLC_CUTSCENE_SKIP (0x17) { playernum }. Mixed
                              * v45/v46 play is rejected at the ENet auth
                              * handshake because v45 clients do not consume
                              * the trailing digest or cutscene mask bytes.
                              * v45 (2026-04-27): spawn-weapon mode wire fields
                              * (SPECIFIC / RANDOM / FIESTA). SVC_STAGE_START
                              * gains a trailing u8 spawnWeaponMode + u8
                              * spawnWeaponNum after the existing spawn_weapon_id
                              * string; CLC_LOBBY_START gains a trailing u8
                              * spawnWeaponMode. The host's matchStart() rolls
                              * RANDOM once (every spawn uses the rolled value
                              * for the rest of the match) and arms FIESTA via
                              * the SPAWNWEAPON_FIESTA_SENTINEL (0xFE) integer
                              * so player.c / bot.c spawn sites roll fresh
                              * per-spawn. Mixed v44/v45 play is rejected at
                              * the ENet auth handshake (netServerEvConnect,
                              * net.c:1560) -- the v44 readers do not consume
                              * the trailing mode bytes and would mis-parse
                              * subsequent fields. See port/src/net/matchsetup.c
                              * spawnWeaponPickFromActiveSet for the eligible
                              * pool (active match set's 6 slots minus
                              * NONE/DISABLED/SHIELD).
                              * v44 (2026-04-26): hygiene bump for the
                              * Goldfinger 64 weapon + AllInOne arena
                              * cull. Wire format is structurally
                              * unchanged (weapon identity already
                              * crosses the wire as catalog session ref
                              * u16 per the v30 rule, not raw enum
                              * integers, so the enum-slot deletions are
                              * invisible on the wire). The bump is a
                              * coordinated "fresh build only" signal
                              * alongside MPSETUP_VERSION 1 -> 2. Mixed
                              * v43/v44 play is rejected at the ENet
                              * auth handshake (netServerEvConnect,
                              * net.c:1560) and at the presence
                              * proto_version check (group_session.c:212),
                              * so a post-cull server cannot accept a
                              * pre-cull client and vice-versa.
                              * v43 (2026-04-25): B-256 structural fix --
                              * SVC_PLAYER_STATS gains a trailing s8 attacker_id
                              * field carrying the dying chr's resolved
                              * mpPlayerGetIndex(lastattacker) at write time, or
                              * -1 sentinel when the attacker is unknown / not a
                              * player (suicide, out-of-bounds, NULL lastattacker).
                              * Receivers prefer the wire field over the local-
                              * resolve fallback at netmsg.c:2128 so MP killfeed
                              * and scoreboard attribution are no longer subject
                              * to local-state-resolution races in 2+ peer
                              * matches.  Mixed v42/v43 play is rejected at the
                              * ENet auth handshake (netServerEvConnect, see
                              * port/src/net/net.c:1560) and at the presence
                              * proto_version check (group_session.c:212), so
                              * receivers never see a v42 packet.
                              * v42 (2026-04-25): Phase 3+ wire-layer additive --
                              * (a) CLC_SPECTATE_REQUEST (0x16) + SVC_SPECTATE_ACK (0x6a)
                              *     + SVC_STATE_FRAME (0x6b). Spectator does not consume
                              *     a player slot: server sets CLFLAG_SPECTATOR on the
                              *     netclient, skips player iteration, and fans out
                              *     SVC_STATE_FRAME at SPECTATOR_FANOUT_HZ (10 Hz).
                              * (b) CLFLAG_SPECTATOR (1 << 2) flag bit on netclient.flags.
                              * (c) State frame body: u32 host_handle + u32 frame_seq
                              *     + u8 participant_count + per-participant block.
                              *     ~64 bytes per participant; ~10 KB/s outbound per
                              *     spectator at 16 participants * 10 Hz.
                              * v41 (2026-04-25): Phase 2 connectivity additive --
                              * SVC_ACHIEVEMENT_TOAST (0x69). Authoritative match
                              * host -> all clients in the match room. Carries
                              * an actor_handle (u32) plus a short utf-8
                              * achievement string (up to ACHIEVEMENT_TOAST_MAX
                              * bytes). Clients enqueue a TOAST_CATEGORY_SOCIAL
                              * popup gated by socialNotifMaskGet so the
                              * settings checkbox actually mutes them. Chat /
                              * file-transfer / presence Phase 2 work runs on
                              * dedicated UDP sockets and does NOT participate
                              * in the ENet wire (signed via Ed25519 there).
                              * v40: Issue 4b music speed-lerp -- SVC_MUSIC_ADVANCE
                              * gains a u32 match_clock_offset_ms field after the
                              * track_id string. Authoritative host computes the
                              * elapsed milliseconds since track-start and
                              * broadcasts it both on track change and as a
                              * periodic re-broadcast (every ~2s) for drift
                              * correction. Clients lerp playback rate in
                              * [0.97, 1.03] to converge, hard-seek if drift
                              * exceeds 5000 ms.
                              * v39: ADMIN_RESP_RATE_LIMIT (0x06) for failed ADMIN_AUTH
                               * rate-limit / lockout (SVC_ADMIN wire). v38: Multiple additive changes on one protocol bump.
                               *   (a) MASTER-C3 identity cookie — CLC_AUTH now carries a
                               *       16-byte reconnect cookie (zeros on first join); SVC_AUTH
                               *       returns the server-issued cookie.  Reconnect requires
                               *       both name AND cookie to match the preserved slot.
                               *   (b) SEC-14 room passwords — CLC_ROOM_CREATE gains access
                               *       mode + password string + max_players; CLC_ROOM_JOIN
                               *       gains a password string.  Server hashes + compares.
                               *   (c) CLC_ADMIN / SVC_ADMIN (0x15 / 0x68) RCON channel for
                               *       headless server operators (auth / kick / ban / unban /
                               *       list / status).
                               *   (d) SEC-7 (prior) — server query 2-stage handshake.
                               * Stage 1: client sends 5-byte PDQM query; server returns a
                               * 17-byte challenge (magic + 0xFFFFFFFF marker + 8-byte HMAC
                               * token). Stage 2: client re-sends 13-byte query (magic + token);
                               * server verifies and returns the full info packet. Cuts
                               * unauthenticated reflection amplification to <1x. Per-/24 rate
                               * limit is shared across both stages.
                               * v37: B-12 Phase 3 — chrslots u64 bitmask removed from struct mpsetup
                               * and from SVC_STAGE_START wire. Participant pool is the sole
                               * source of slot assignment; wire carries a derived 64-bit active
                               * mask over slots 0..MAX_MPCHRS-1 which the reader decodes into
                               * participant-pool entries directly (no legacy chrslots storage).
                               * BOT_SLOT_OFFSET constant removed; bot slots are MAX_PLAYERS..MAX_MPCHRS-1.
                               * v36: Counter-Op leader-selected anti player identity on wire
                               * (CLC_LOBBY_START + SVC_STAGE_START), plus co-op/anti launch
                               * path de-dup to a single mainChangeToStage callsite.
                               * v35: co-op/anti manifest pipeline, CLC_STAGE_READY for co-op,
                               * match_seed in SVC_STAGE_START for deterministic spawn pools.
                               * v34: playlist tracks in manifest, SVC_MUSIC_ADVANCE, per-client DL status.
                               * v33: A-7 mod audio network sync — SVC_STAGE_START includes mod_track_id
                               * string, ASSET_AUDIO in SVC_CATALOG_INFO distribution.
                               * v32: scenario identity uses catalog ID string on wire (CLC_LOBBY_START,
                               * SVC_STAGE_START, server query). scenario u8 replaced by str.
                               * v31: SVC_PROP_SPAWN modelnum on wire uses catalog session refs (u16)
                               * instead of raw s16 model index.  Bot body/head decode uses
                               * runtime_index directly (no intermediate mp-index conversion).
                               * v30: Weapon identity on wire uses catalog session refs (u16).
                               * v29: Room networking (R-3).
                               * v28: SVC_BOT_AUTHORITY + CLC_BOT_MOVE for dedicated-server bot relay.
                               * v27: net_hash removed from wire; all asset identity uses catalog ID strings. */

#define NET_QUERY_MAGIC "PDQM\x01"

/* SEC-7: challenge-response handshake for server info queries.
 * Server secret is generated once per process at netStartServer(). Tokens
 * are SHA-256(secret || client-addr || 30s-time-slot), truncated to 8 bytes.
 * The marker is an otherwise-invalid NET_PROTOCOL_VER value so clients and
 * servers can distinguish a challenge packet from a full info response by
 * looking at the first 4 bytes after the magic. */
#define NET_QUERY_TOKEN_LEN        8u
#define NET_QUERY_CHALLENGE_MARKER 0xFFFFFFFFu
#define NET_QUERY_TIME_SLOT_MS     30000u
#define NET_QUERY_RATE_WINDOW_MS   1000u
#define NET_QUERY_RATE_SLOTS       256u

#define NET_MAX_CLIENTS 32  /* max simultaneous connections; independent of MAX_PLAYERS (match slots) */
#define NET_MAX_NAME MAX_PLAYERNAME
#define NET_MAX_ADDR 256

#define NET_BUFSIZE 262144  /* 256KB — must handle 31+ bot broadcasts without overflow */
#define NET_CLIENT_BUFSIZE 16384  /* 16KB per-client outbound (server→individual client) */

#define NET_DEFAULT_PORT 27100

#define NET_NULL_CLIENT 0xFF
#define NET_NULL_PROP 0

#define NET_RESYNC_FLAG_CHRS   (1 << 0)
#define NET_RESYNC_FLAG_PROPS  (1 << 1)
#define NET_RESYNC_FLAG_SCORES (1 << 2)
#define NET_RESYNC_FLAG_NPCS   (1 << 3)

extern u8 g_NetPendingResyncFlags;    /* server: resync types to broadcast next netEndFrame */
extern u8 g_NetPendingResyncReqFlags; /* client: resync types to request from server next netEndFrame */

#define CLFLAG_ABSENT    (1 << 0) // player disconnected mid-game, slot preserved for reconnect
#define CLFLAG_COOPREADY (1 << 1) // client is ready to start co-op mission
#define CLFLAG_SPECTATOR (1 << 2) // v42: client is spectator-only; skip player iteration, fan out SVC_STATE_FRAME

#define NET_MAX_RECENT_SERVERS 8
#define NET_PRESERVE_TIMEOUT_FRAMES (60 * 60 * 5) // 5 minutes at 60 fps

/* MASTER-C3: Identity cookie length.  16 bytes (128 bits) of server-supplied
 * random bits.  Sent on every CLC_AUTH; zero-bytes = first-time join (no
 * reconnect intent). */
#define NET_AUTH_COOKIE_LEN 16

// co-op session modes
#define NETGAMEMODE_MP    0 // combat simulator (standard multiplayer)
#define NETGAMEMODE_COOP  1 // cooperative campaign
#define NETGAMEMODE_ANTI  2 // counter-operative campaign

struct netpreservedplayer {
	char name[NET_MAX_NAME];
	u8 cookie[NET_AUTH_COOKIE_LEN]; /* MASTER-C3: server-issued identity cookie */
	u8 playernum;
	u8 team;
	s16 killcounts[MAX_MPCHRS];
	s16 numdeaths;
	s16 numpoints;
	bool active;
	u32 preserveframe; // frame number when preserved, for timeout
};

struct netrecentserver {
	char addr[NET_MAX_ADDR + 1];
	u32 protocol;
	u8 flags;       // bit 0 = in-game
	u8 numclients;
	u8 maxclients;
	char stage_id[CATALOG_ID_LEN]; /* PRIMARY: catalog stage identity */
	u8 stagenum;               /* DEPRECATED: integer stage index. Use stage_id instead. */
	char scenario_id[CATALOG_ID_LEN]; /* M0.1d: PRIMARY catalog scenario identity */
	u8 scenario;               /* DEPRECATED: integer MPSCENARIO_*. Use scenario_id instead. */
	char hostname[NET_MAX_NAME];
	u32 lastresponse; // system time of last response (0 = never)
	bool online;
};

extern struct netpreservedplayer g_NetPreservedPlayers[NET_MAX_CLIENTS];
extern s32 g_NetNumPreserved;
extern struct netrecentserver g_NetRecentServers[NET_MAX_RECENT_SERVERS];
extern s32 g_NetNumRecentServers;
extern u8 g_NetCounterOpClientId; /* NET_NULL_CLIENT when not in Counter-Op */
extern u8 g_NetBotAuthorityClientId; /* NET_NULL_CLIENT when no authority delegated */

#define NETCHAN_DEFAULT  0
#define NETCHAN_CONTROL  1
#define NETCHAN_TRANSFER 2  /* D3R-9: dedicated reliable channel for mod distribution */
#define NETCHAN_COUNT    3

/* D3R-9: Network Distribution limits */
#define NET_DISTRIB_CHUNK_SIZE   (16 * 1024)          /* 16KB uncompressed per chunk */
#define NET_DISTRIB_MAX_COMP     (50 * 1024 * 1024)   /* 50MB max single component */
#define NET_DISTRIB_MAX_SESSION  (200 * 1024 * 1024)  /* 200MB max per session total */
#define NET_DISTRIB_COMP_NONE    0                    /* no compression */
#define NET_DISTRIB_COMP_DEFLATE 1                    /* zlib deflate */

#define DISCONNECT_UNKNOWN  0
#define DISCONNECT_SHUTDOWN 1
#define DISCONNECT_VERSION 2
#define DISCONNECT_KICKED 3
#define DISCONNECT_BANNED 4
#define DISCONNECT_TIMEOUT 5
#define DISCONNECT_FULL 6
#define DISCONNECT_LATE 7
#define DISCONNECT_FILES 8
#define DISCONNECT_LEAVE 9
#define DISCONNECT_ADMIN_AUTH 10  /* too many failed admin token attempts */

#define CLSTATE_DISCONNECTED 0
#define CLSTATE_CONNECTING 1
#define CLSTATE_AUTH 2
#define CLSTATE_LOBBY 3
#define CLSTATE_GAME 4
#define CLSTATE_PREPARING 5  /* received SVC_MATCH_MANIFEST; checking local catalog */

#define UCMD_FIRE (1 << 0)
#define UCMD_ACTIVATE (1 << 1)
#define UCMD_RELOAD (1 << 2)
#define UCMD_AIMMODE (1 << 3)
#define UCMD_DUCK (1 << 4)
#define UCMD_SQUAT (1 << 5)
#define UCMD_ZOOMIN (1 << 6)
#define UCMD_SELECT (1 << 7)
#define UCMD_SELECT_DUAL (1 << 8)
#define UCMD_EYESSHUT (1 << 9)
#define UCMD_SECONDARY (1 << 10)
#define UCMD_JUMP      (1 << 11)
#define UCMD_RESPAWN (1 << 27)
#define UCMD_CHAT (1 << 28)
#define UCMD_IMPORTANT_MASK (UCMD_FIRE | UCMD_ACTIVATE | UCMD_RELOAD | UCMD_AIMMODE | UCMD_SELECT | UCMD_SELECT_DUAL)
#define UCMD_FL_FORCEPOS (1 << 29)
#define UCMD_FL_FORCEANGLE (1 << 30)
#define UCMD_FL_FORCEGROUND (1 << 31)
#define UCMD_FL_FORCEMASK (UCMD_FL_FORCEPOS | UCMD_FL_FORCEANGLE | UCMD_FL_FORCEGROUND)

struct netplayermove {
	u32 tick; // g_NetTIck value when this struct was written; if 0, this struct is invalid
	u32 ucmd; // player commands (UCMD_)
	f32 leanofs; // analog lean value (-1 .. 1; equal to player->swaytarget / 75.f)
	f32 crouchofs; // analog crouch value (-90 for SQUAT, 0 for STAND; player->crouchofs)
	f32 zoomfov; // manual zoom fov for the current gun; synced only if UCMD_AIMING is set
	f32 movespeed[2]; // move inputs, [0] is forward, [1] is sideways; used mostly for animation
	f32 angles[2]; // view angles, [0] is theta, [1] is verta
	f32 crosspos[2]; // crosshair position in aiming mode; normalized to default aspect ratio
	char weapon_id[CATALOG_ID_LEN]; /* PRIMARY: catalog weapon identity for weapon switch */
	s8 weaponnum; /* DEPRECATED: integer weapon enum. Use weapon_id instead. Empty string = no change. */
	struct coord pos; // player position at g_NetTick == tick
};

struct netclient {
	struct _ENetPeer *peer;
	u32 id; // remote client number, server is always 0, even on clients
	u32 state; // CLSTATE_
	u32 flags; // CLFLAG_

	struct {
		char name[NET_MAX_NAME];
		u16 options;
		char body_id[CATALOG_ID_LEN]; /* canonical catalog asset ID, e.g. "base:dark_combat" */
		char head_id[CATALOG_ID_LEN]; /* canonical catalog asset ID, e.g. "base:head_dark_combat" */
		u8 team;
		f32 fovy;
		f32 fovzoommult;
	} settings;

	struct mpplayerconfig *config;
	struct player *player;
	u8 playernum;

	struct netplayermove outmove[2]; // last 2 outgoing player inputs, newest one first
	struct netplayermove inmove[2]; // last 2 incoming player inputs, newest one first
	u32 inmovetick; // last inmove tick which was applied to the player
	u32 outmoveack; // last acked outmove tick
	u32 forcetick; // tick on which the client's position was forced, or 0 if not forcing
	u32 lerpticks; // how many ticks we've been lerping the position

	u8 room_id;    // hub room assignment (0xFF = in lounge, not in a room)
	bool stage_ready; // server: true once this client sent CLC_STAGE_READY after stage load
	bool is_admin;    // MASTER-C2b: set after successful CLC_ADMIN ADMIN_AUTH on this peer
	u8 auth_cookie[NET_AUTH_COOKIE_LEN]; // MASTER-C3: server-issued identity cookie for this peer

	struct netbuf out; // outbound messages are written here, except broadcasts
	struct netbuf in; // incoming packets are fed here

	u8 out_data[NET_CLIENT_BUFSIZE]; // buffer for out
};

extern s32 g_NetMode;
extern u8 g_NetGameMode; // NETGAMEMODE_MP, NETGAMEMODE_COOP, NETGAMEMODE_ANTI
extern u8 g_NetCoopDifficulty; // DIFF_A, DIFF_SA, DIFF_PA (0, 1, 2) for co-op missions
extern u8 g_NetCoopFriendlyFire; // 0 = off, 1 = on
extern u8 g_NetCoopRadar; // 0 = off, 1 = on

extern s32 g_NetJoinLatch;
extern s32 g_NetHostLatch;
extern s32 g_NetDedicated; // --dedicated : server-only, no local player

// net frame, ticks at 60 fps, starts at 0 when the server is started
extern u32 g_NetTick;
extern u32 g_NetNextSyncId;

extern u64 g_NetRngSeeds[2];
extern u32 g_NetRngLatch;

extern u32 g_NetInterpTicks;
extern u32 g_NetServerPort;
extern char g_NetLastJoinAddr[NET_MAX_ADDR + 1];

extern s32 g_NetDebugDraw;

extern s32 g_NetMaxClients;
extern s32 g_NetNumClients;
extern struct netclient g_NetClients[NET_MAX_CLIENTS + 1]; // last is an extra temporary client
extern struct netclient *g_NetLocalClient;

/* Bot authority flag: true on the designated client that runs bot AI and relays positions
 * to the server via CLC_BOT_MOVE (dedicated server games only). Set on receipt of
 * SVC_BOT_AUTHORITY; cleared on disconnect and stage end. */
extern bool g_NetLocalBotAuthority;

/* U-10: Deferred bot authority — set true on SVC_BOT_AUTHORITY receipt, promoted to
 * g_NetLocalBotAuthority once stage load is confirmed (pads loaded, spawn points ready). */
extern bool g_NetPendingBotAuthority;

/* U-10: Stage-ready handshake state (server-side, dedicated server only).
 * g_NetStageReadyDeadline: g_NetTick value at which the server stops waiting and sends
 *   BOT_AUTHORITY regardless; -1 means not currently waiting.
 * g_NetBotAuthorityDelegated: set once SVC_BOT_AUTHORITY has been sent this match so
 *   the timeout path and the CLC_STAGE_READY handler don't double-send. */
extern s32  g_NetStageReadyDeadline;
extern bool g_NetBotAuthorityDelegated;

/* L2-4: Server-generated match seed distributed via SVC_STAGE_START.
 * Used by spawnpool.c (future) for deterministic spawn pool generation
 * that all clients agree on. Set by server before SVC_STAGE_START write;
 * read by clients in SVC_STAGE_START handler. */
extern u32  g_NetMatchSeed;

extern struct netbuf g_NetMsg;
extern struct netbuf g_NetMsgRel;

const char *netFormatClientAddr(const struct netclient *cl);

/* Parse an address string ("host:port" or "host") into an ENetAddress.
   Returns non-zero on success, 0 on failure. */
s32 netParseAddr(ENetAddress *out, const char *str);

/* Return the current ENet host handle (NULL if not connected/hosting). */
struct _ENetHost *netGetHost(void);

/* B-126 diagnostics: log NET.WATCHDOG + NET.HEARTBEAT snapshot for all peers.
 * Call every ~30s from lvTick() during active matches. */
void netHeartbeatLog(void);

void netInit(void);
s32 netDisconnect(void);
void netStartFrame(void);
void netEndFrame(void);

s32 netStartServer(u16 port, s32 maxclients);
s32 netStartClient(const char *addr);

u32 netSend(struct netclient *dstcl, struct netbuf *buf, const s32 reliable, const s32 chan);
void netSendToRoom(u8 room_id, struct netbuf *buf, s32 reliable, s32 chan);

void netChat(struct netclient *dst, const char *text);
void netChatPrintf(struct netclient *dst, const char *fmt, ...);

void netServerStageStart(void);
void netServerCoopStageStart(u8 stagenum, u8 difficulty);
void netServerStageEnd(void);
void netServerKick(struct netclient *cl, const u32 reason);

struct netclient *netClientForPlayerNum(s32 playernum);

void netClientSyncRng(void);
void netClientSettingsChanged(void);

void netPlayersAllocate(void);
void netSyncIdsAllocate(void);

void netServerPreservePlayer(struct netclient *cl);
/* MASTER-C3: Find a preserved player by name.  Advisory lookup — the caller
 * MUST additionally compare cookie bytes before restoring.  Returns NULL if
 * the preserved table has no matching name. */
struct netpreservedplayer *netServerFindPreserved(const char *name);
/* MASTER-C3: Find by BOTH name and cookie.  Used on reconnect — if either
 * check fails this returns NULL and the caller should treat the peer as a
 * fresh player (or reject if mid-game). */
struct netpreservedplayer *netServerFindPreservedByCookie(const char *name,
	const u8 cookie[NET_AUTH_COOKIE_LEN]);
void netServerRestorePreserved(struct netclient *cl, struct netpreservedplayer *pp);

/* MASTER-C3: Populate out with NET_AUTH_COOKIE_LEN cryptographically-unique
 * bytes drawn from SDL_GetPerformanceCounter mixed with a running hash.
 * Good enough for identity separation (not a KDF). */
void netServerIssueCookie(u8 out[NET_AUTH_COOKIE_LEN]);
void netConfigSanitizeLoadedAddresses(void);
void netRecentServerAdd(const char *addr);
void netRecentServerUpdate(const char *addr, const u8 *data, s32 len);
void netQueryRecentServers(void);
void netQueryRecentServersAsync(void);
void netPollRecentServers(void);
extern bool g_NetQueryInFlight;

Gfx *netDebugRender(Gfx *gdl);

#endif // _IN_NET_H
