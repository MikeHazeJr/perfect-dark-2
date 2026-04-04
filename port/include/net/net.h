#ifndef _IN_NET_H
#define _IN_NET_H

#include "types.h"
#include "constants.h"
#include "net/netbuf.h"
#include "assetcatalog.h"

/* Forward declaration — avoids pulling enet.h into every translation unit */
typedef struct _ENetAddress ENetAddress;

#define NET_PROTOCOL_VER 29  /* v29: Room networking (R-3). SVC_ROOM_LIST, SVC_ROOM_ASSIGN,
                               * CLC_ROOM_CREATE, CLC_ROOM_JOIN, CLC_ROOM_LEAVE.
                               * Clients see room list, create/join rooms, match start is room-scoped.
                               * v28: SVC_BOT_AUTHORITY + CLC_BOT_MOVE for dedicated-server bot relay.
                               * v27: net_hash removed from wire; all asset identity uses catalog ID strings. */

#define NET_QUERY_MAGIC "PDQM\x01"

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

#define NET_MAX_RECENT_SERVERS 8
#define NET_PRESERVE_TIMEOUT_FRAMES (60 * 60 * 5) // 5 minutes at 60 fps

// co-op session modes
#define NETGAMEMODE_MP    0 // combat simulator (standard multiplayer)
#define NETGAMEMODE_COOP  1 // cooperative campaign
#define NETGAMEMODE_ANTI  2 // counter-operative campaign

struct netpreservedplayer {
	char name[NET_MAX_NAME];
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
	u8 stagenum;
	u8 scenario;
	char hostname[NET_MAX_NAME];
	u32 lastresponse; // system time of last response (0 = never)
	bool online;
};

extern struct netpreservedplayer g_NetPreservedPlayers[NET_MAX_CLIENTS];
extern s32 g_NetNumPreserved;
extern struct netrecentserver g_NetRecentServers[NET_MAX_RECENT_SERVERS];
extern s32 g_NetNumRecentServers;

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
	s8 weaponnum; // switch to this weapon if UCMD_SELECT is set
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

	u8 room_id; // hub room assignment (0xFF = in lounge, not in a room)

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

extern struct netbuf g_NetMsg;
extern struct netbuf g_NetMsgRel;

const char *netFormatClientAddr(const struct netclient *cl);

/* Parse an address string ("host:port" or "host") into an ENetAddress.
   Returns non-zero on success, 0 on failure. */
s32 netParseAddr(ENetAddress *out, const char *str);

/* Return the current ENet host handle (NULL if not connected/hosting). */
struct _ENetHost *netGetHost(void);

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
struct netpreservedplayer *netServerFindPreserved(const char *name);
void netServerRestorePreserved(struct netclient *cl, struct netpreservedplayer *pp);
void netRecentServerAdd(const char *addr);
void netRecentServerUpdate(const char *addr, const u8 *data, s32 len);
void netQueryRecentServers(void);
void netQueryRecentServersAsync(void);
void netPollRecentServers(void);
extern bool g_NetQueryInFlight;

Gfx *netDebugRender(Gfx *gdl);

#endif // _IN_NET_H
