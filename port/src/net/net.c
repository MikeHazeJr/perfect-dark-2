#if defined(__linux__)
#define _GNU_SOURCE 1
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <SDL.h>
#include "platform.h"
#include "net/netenet.h"
#include "net/net.h"
#include "net/netbuf.h"
#include "net/netmsg.h"
#include "net/net_client_settings_wire.h"
#include "net/net_reconnect.h"
#include "net/netupnp.h"
#include "net/netstun.h"
#include "net/netholepunch.h"
#include "net/net_bandwidth.h"
#include "net/group_session.h"
#include "identity.h"
#include "net/netlobby.h"
#include "net/netdistrib.h"
#include "net/sessioncatalog.h"
#include "net/matchsetup.h"
#include "net/netmanifest.h"
#include "types.h"
#include "constants.h"
#include "data.h"
#include "bss.h"
#include "game/hudmsg.h"
#include "game/mplayer/participant.h"
#include "game/player.h"
#include "game/playermgr.h"
#include "game/bot.h"
#include "game/chr.h"
#include "game/bondgun.h"
#include "game/game_1531a0.h"
#include "game/game_0b0fd0.h"
#include "game/title.h"
#include "game/lv.h"
#include "game/menu.h"
#include "game/pdmode.h"
#include "game/mplayer/mplayer.h"
#include "lib/main.h"
#include "lib/vi.h"
#include "config.h"
#include "system.h"
#include "crashbreadcrumb.h"
#include "console.h"
#include "fs.h"
#include "romdata.h"
#include "utils.h"
#include "room.h"
#include "assetcatalog.h"
#include "player_identity.h"
#include "audio.h"
#include "sha256.h"
#include "server_bans.h"
#include "scene_transition.h"
#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__linux__)
#include <errno.h>
#include <sys/random.h>
#elif defined(__APPLE__)
#include <unistd.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#if !defined(PD_SERVER)
#include "input.h"
#include "inputctx.h"
#include "menupool.h"
#include "scene.h"
#endif

s32 g_NetMode = NETMODE_NONE;
u8  g_NetMatchRoomId = 0xFF; /* R-3: which room is currently starting/in match (0xFF = global) */
u8  g_NetCounterOpClientId = NET_NULL_CLIENT;
u8  g_NetBotAuthorityClientId = NET_NULL_CLIENT;

s32 g_NetHostLatch = false;
s32 g_NetJoinLatch = false;

u32 g_NetServerUpdateRate = 1;
u32 g_NetServerInRate = 128 * 1024;
u32 g_NetServerOutRate = 128 * 1024;
u32 g_NetServerPort = NET_DEFAULT_PORT;
s32 g_NetServerInfoQuery = true;

u32 g_NetClientUpdateRate = 1;
u32 g_NetClientInRate = 128 * 1024;
u32 g_NetClientOutRate = 128 * 1024;

u32 g_NetInterpTicks = 3;
char g_NetLastJoinAddr[NET_MAX_ADDR + 1] = "127.0.0.1:27100";

u32 g_NetTick = 0;
u32 g_NetNextSyncId = 1;
u32 g_NetFirstDynamicSyncId = 1;

s32 g_NetSimPacketLoss = 0;
s32 g_NetDebugDraw = 0;

u64 g_NetRngSeeds[2];
u32 g_NetRngLatch = 0;

s32 g_NetMaxClients = NET_MAX_CLIENTS;
s32 g_NetNumClients = 0;
struct netclient g_NetClients[NET_MAX_CLIENTS + 1]; // last is an extra temporary client
struct netclient *g_NetLocalClient = &g_NetClients[NET_MAX_CLIENTS];

struct netpreservedplayer g_NetPreservedPlayers[NET_MAX_CLIENTS];
s32 g_NetNumPreserved = 0;
struct netrecentserver g_NetRecentServers[NET_MAX_RECENT_SERVERS];
s32 g_NetNumRecentServers = 0;

/* The retry address and exact last-published settings remain in memory across
 * timeout teardown. The cookie stays in netmsg.c and is independently scoped
 * to the resolved ENet endpoint. */
static struct {
	bool valid;
	bool auth_accepted;
	char addr[NET_MAX_ADDR + 1];
	struct netclientsettings settings;
} s_NetReconnectAttempt;

/* Bot authority: true on the client designated to run bot AI and relay positions via CLC_BOT_MOVE.
 * Set by SVC_BOT_AUTHORITY (dedicated server games only); cleared on disconnect/stage-end. */
bool g_NetLocalBotAuthority = false;
bool g_NetPendingBotAuthority = false;

/* U-10/B-1104: Stage-ready handshake — every server waits for real remote
 * post-load readiness; dedicated mode also uses it for bot delegation. */
s32  g_NetStageReadyDeadline    = -1;   /* g_NetTick value at timeout; -1 = not waiting */
bool g_NetBotAuthorityDelegated = false; /* true once SVC_BOT_AUTHORITY sent this match */
u32  g_NetMatchSeed             = 0;    /* L2-4: server-generated seed for deterministic spawn pools */
u32  g_NetStageEpoch            = 0;    /* v58: exact published stage-session identity */

/* Async recent-server query state */
bool g_NetQueryInFlight = false;
static ENetSocket g_NetQuerySocket = ENET_SOCKET_NULL;
static uint32_t g_NetQueryStartMs = 0;
#define NET_QUERY_TIMEOUT_MS 2000

u8 g_NetGameMode = NETGAMEMODE_MP;
u8 g_NetCoopDifficulty = 0;
u8 g_NetCoopFriendlyFire = 0;
u8 g_NetCoopRadar = 1;

static u8 g_NetMsgBuf[NET_BUFSIZE];
struct netbuf g_NetMsg = { .data = g_NetMsgBuf, .size = sizeof(g_NetMsgBuf) };

static u8 g_NetMsgRelBuf[65536]; // 64KB reliable buffer (control/auth/lobby — no bot bulk data)
struct netbuf g_NetMsgRel = { .data = g_NetMsgRelBuf, .size = sizeof(g_NetMsgRelBuf) };

/* B-1104: A stage baseline has dedicated packet ownership. Shared reliable
 * storage is intentionally unavailable here because late control-plane
 * producers reset g_NetMsgRel around their direct sends. */
static u8 s_NetResyncTxnBuf[65536];

typedef struct net_stage_end_pending_s {
	bool active;
	u8 room_id;
	u8 mode;
} net_stage_end_pending_t;

static net_stage_end_pending_t s_NetStageEndPending;
/* Remains set through the end of the frame that publishes SVC_STAGE_END, even
 * when the first send succeeds immediately and clears the pending retry. This
 * prevents stale gameplay/spectator traffic from following the terminal
 * packet in that same frame. */
static bool s_NetStageEndTerminalFrame;

typedef enum net_stage_replication_phase_e {
	NET_STAGE_REPLICATION_INACTIVE = 0,
	NET_STAGE_REPLICATION_WAITING,
	NET_STAGE_REPLICATION_RELEASE,
	NET_STAGE_REPLICATION_ACTIVE,
} net_stage_replication_phase_e;

/* B-1104/SP-70/SP-71: CLSTATE_GAME means that the match transaction is
 * committed; it does not mean the authority or a remote endpoint has finished
 * loading the referenced world. The explicit phase distinguishes "no stage"
 * from "released", while the mask captures the exact peer-backed participants
 * that received this stage start. */
static net_stage_replication_phase_e s_NetStageReplicationPhase;
static u32 s_NetStageReplicationWaitMask;
static bool s_NetStageAuthorityReady;

static void netServerClearPreservedPlayers(const char *reason);
static void netServerDiscardPreservedPlayer(
	struct netpreservedplayer *pp, const char *reason);

/* c118 (2026-05-15): made non-static so the smoke-verify CLI fast-paths
 * --listen-bind / --connect-host in port/src/main.c can gate their
 * deferred-tick fire on netInit completion. The boot pool's
 * bootRunCatalogWork worker sets this to true inside netInit() once
 * enet_initialize succeeds; the main thread reads it from mainTick
 * after bootPoolWaitIdle has joined the worker. */
s32 g_NetInit = false;
static ENetHost *g_NetHost;
static ENetAddress g_NetLocalAddr;
static ENetAddress g_NetRemoteAddr;

/* B-1057: persisted lower-bound upload capability observed from real ENet
 * traffic. The estimate is deliberately passive: no synthetic packet burst,
 * third-party speed-test service, or configured-rate stand-in. */
static u32 s_NetUploadKbps;
static u32 s_NetUploadMeasuredAtUnix;
static net_upload_meter_t s_NetUploadMeter;

static u32 g_NetNextUpdate = 0;

static u32 g_NetReliableFrameLen = 0;
static u32 g_NetUnreliableFrameLen = 0;

s32 netParseAddr(ENetAddress *out, const char *str)
{
	char tmp[256] = { 0 };

	if (!str || !str[0]) {
		return false;
	}

	strncpy(tmp, str, sizeof(tmp) - 1);

	char *host = tmp;
	char *port = NULL;
	char *colon = strrchr(tmp, ':');

	// if there is a : in the address string, there could be a port value
	// otherwise it's an ip or hostname with default port
	if (colon > tmp) {
		if (tmp[0] == '[' && colon[-1] == ']' && isdigit(colon[1])) {
			// ipv6 with port: [ADDR]:PORT
			colon[-1] = '\0'; // terminate ip
			host = tmp + 1; // skip [
			port = colon + 1; // skip :
		} else if (isdigit(colon[1]) && strchr(host, ':') == colon) {
			// ipv4 or hostname with port
			colon[0] = '\0'; // terminate ip
			port = colon + 1; // skip :
		}
	}

	if (!host[0]) {
		return false;
	}

	const s32 portval = port ? atoi(port) : NET_DEFAULT_PORT;
	if (portval <= 0 || portval > 0xFFFF) {
		return false;
	}

	memset(out, 0, sizeof(*out));
	out->port = portval;

	if (isdigit(host[0]) || strchr(host, ':')) {
		// we stripped off the :PORT at this point, now check if this is an IP address
		if (enet_address_set_ip(out, host) == 0) {
			return true;
		}
	}

	// must be a domain name; do a lookup
	return (enet_address_set_hostname(out, host) == 0);
}

static s32 netStoredAddrIsValid(const char *addr)
{
	unsigned a = 0, b = 0, c = 0, d = 0, port = NET_DEFAULT_PORT;
	int consumed = 0;

	if (!addr || !addr[0]) {
		return false;
	}

	if (sscanf(addr, " %u.%u.%u.%u:%u %n", &a, &b, &c, &d, &port, &consumed) == 5) {
		/* Parsed with explicit port. */
	} else {
		consumed = 0;
		port = NET_DEFAULT_PORT;
		if (sscanf(addr, " %u.%u.%u.%u %n", &a, &b, &c, &d, &consumed) != 4) {
			return false;
		}
	}

	if (addr[consumed] != '\0') {
		return false;
	}
	if (a > 255 || b > 255 || c > 255 || d > 255) {
		return false;
	}
	if (port == 0 || port > 65535) {
		return false;
	}
	if (a == 0 && b == 0 && c == 0 && d == 0) {
		return false;
	}

	return true;
}

void netConfigSanitizeLoadedAddresses(void)
{
	if (g_NetLastJoinAddr[0] && !netStoredAddrIsValid(g_NetLastJoinAddr)) {
		sysLogPrintf(LOG_WARNING, "NET: clearing invalid stored LastJoinAddr");
		g_NetLastJoinAddr[0] = '\0';
	}

	s32 out = 0;
	for (s32 i = 0; i < g_NetNumRecentServers && i < NET_MAX_RECENT_SERVERS; i++) {
		if (!netStoredAddrIsValid(g_NetRecentServers[i].addr)) {
			if (g_NetRecentServers[i].addr[0]) {
				sysLogPrintf(LOG_WARNING, "NET: dropping invalid stored recent server entry %d", i);
			}
			continue;
		}

		if (out != i) {
			g_NetRecentServers[out] = g_NetRecentServers[i];
		}
		out++;
	}

	for (s32 i = out; i < NET_MAX_RECENT_SERVERS; i++) {
		memset(&g_NetRecentServers[i], 0, sizeof(g_NetRecentServers[i]));
	}
	g_NetNumRecentServers = out;
}

static const char *netFormatAddr(const ENetAddress *addr)
{
	/* H-4: Ping-pong buffers so two consecutive calls don't alias. */
	static char str[2][256];
	static int idx = 0;
	idx = !idx;
	char tmp[256];
	if (addr && enet_address_get_ip(addr, tmp, sizeof(tmp) - 1) == 0) {
		if (tmp[0]) {
			if (strchr(tmp, ':')) {
				// ipv6
				snprintf(str[idx], sizeof(str[idx]) - 1, "[%s]:%u", tmp, addr->port);
			} else {
				// ipv4
				snprintf(str[idx], sizeof(str[idx]) - 1, "%s:%u", tmp, addr->port);
			}
			return str[idx];
		}
	}
	return NULL;
}

static inline const char *netFormatPeerAddr(const ENetPeer *peer)
{
	return netFormatAddr(&peer->address);
}

const char *netFormatClientAddr(const struct netclient *cl)
{
	return cl->peer ? netFormatPeerAddr(cl->peer) : "<local>";
}

struct _ENetHost *netGetHost(void)
{
	return g_NetHost;
}

static u32 netUploadNowUnix(void)
{
	const time_t now = time(NULL);
	if (now <= 0 || (u64)now > 0xffffffffu) return 0;
	return (u32)now;
}

static void netUploadMeasurementReset(void)
{
	memset(&s_NetUploadMeter, 0, sizeof(s_NetUploadMeter));
	if (g_NetHost) {
		netUploadMeterReset(&s_NetUploadMeter, SDL_GetTicks(),
			g_NetHost->totalSentData);
	}
}

static void netUploadMeasurementTick(void)
{
	if (!g_NetHost) return;
	const u32 observed = netUploadMeterSample(&s_NetUploadMeter,
		SDL_GetTicks(), g_NetHost->totalSentData);
	if (observed == 0) return;

	const u32 now_unix = netUploadNowUnix();
	if (now_unix == 0) return;
	const u32 current = netUploadKbpsIfFresh(s_NetUploadKbps,
		s_NetUploadMeasuredAtUnix, now_unix);
	if (current != 0 && observed <= current) return;

	s_NetUploadKbps = observed;
	s_NetUploadMeasuredAtUnix = now_unix;
	sysLogPrintf(LOG_NOTE,
		"NET.BANDWIDTH: passive upload estimate=%u kbps source=enet bytes",
		(unsigned)s_NetUploadKbps);
}

u32 netUploadKbpsEstimate(void)
{
	const u32 now_unix = netUploadNowUnix();
	if (now_unix == 0) return 0;
	return netUploadKbpsIfFresh(s_NetUploadKbps,
		s_NetUploadMeasuredAtUnix, now_unix);
}

static inline void netClientReset(struct netclient *cl)
{
	if (cl->state >= CLSTATE_GAME && cl->player) {
		cl->player->client = NULL;
		cl->player->isremote = false;
	}
	if (cl->config && cl->config->client == cl) {
		cl->config->client = NULL;
	}
	memset(cl, 0, sizeof(*cl));
	cl->out.data = cl->out_data;
	cl->out.size = sizeof(cl->out_data);
	cl->id = cl - g_NetClients;
	cl->settings.team = 0xff;
	cl->room_id = 0xFF;
	cl->reconnect_preserved_index = NET_NULL_CLIENT;
	netmsgChatRateReset((u32)cl->id);
	netmsgRoomMutationRateReset((u32)cl->id);
	netmsgAdminAuthRateReset((u32)cl->id);
}

static inline void netClientResetAll(void)
{
	g_NetMaxClients = NET_MAX_CLIENTS;
	g_NetNumClients = 1; // always at least one client, which is us
	memset(&s_NetStageEndPending, 0, sizeof(s_NetStageEndPending));
	s_NetStageEndTerminalFrame = false;
	netmsgCutsceneAuthorityReset();
	for (u32 i = 0; i < NET_MAX_CLIENTS + 1; ++i) {
		netClientReset(&g_NetClients[i]);
	}
}

static inline void netClientRecordMove(struct netclient *cl, const struct player *pl)
{
	// make space in the move stack
	memmove(cl->outmove + 1, cl->outmove, sizeof(cl->outmove) - sizeof(*cl->outmove));

	struct netplayermove *move = &cl->outmove[0];

	move->tick = g_NetTick;
	move->crouchofs = pl->crouchoffset;
	move->leanofs = pl->swaytarget / 75.f;
	move->movespeed[0] = pl->speedforwards;
	move->movespeed[1] = pl->speedsideways;
	move->angles[0] = pl->vv_theta;
	move->angles[1] = pl->vv_verta;
  move->pos = (pl->prop) ? pl->prop->pos : pl->cam_pos;

	move->crosspos[0] = pl->crosspos[0];
	move->crosspos[1] = pl->crosspos[1];

	if (!pl->isremote) {
		// normalize crosspos x
		move->crosspos[0] -= (f32)(SCREEN_WIDTH_LO / 2);
		move->crosspos[0] = (f32)(SCREEN_WIDTH_LO / 2) + move->crosspos[0] * pl->aspect / SCREEN_ASPECT;
	}

	move->ucmd = pl->ucmd;

	if (g_NetMode == NETMODE_SERVER && pl->isremote && cl->inmove[0].tick) {
		// carry some of the client inputs over to the outmove
		move->ucmd |= (cl->inmove[0].ucmd & (UCMD_FIRE | UCMD_RELOAD | UCMD_AIMMODE | UCMD_EYESSHUT | UCMD_SELECT | UCMD_SELECT_DUAL));
		move->crosspos[0] = cl->inmove[0].crosspos[0];
		move->crosspos[1] = cl->inmove[0].crosspos[1];
	}

	if (pl->crouchpos == CROUCHPOS_DUCK) {
		move->ucmd |= UCMD_DUCK;
	} else if (pl->crouchpos == CROUCHPOS_SQUAT) {
		move->ucmd |= UCMD_SQUAT;
	}

	if (pl->gunctrl.switchtoweaponnum >= 0 && !pl->gunctrl.throwing) {
		move->ucmd |= UCMD_SELECT;
		move->weaponnum = pl->gunctrl.switchtoweaponnum;
	} else {
		move->weaponnum = -1;
	}

	if (pl->gunctrl.dualwielding && !pl->gunctrl.throwing) {
		move->ucmd |= UCMD_SELECT_DUAL;
		if ((move->ucmd ^ cl->outmove[1].ucmd) & UCMD_SELECT_DUAL) {
			move->ucmd |= UCMD_SELECT;
		}
	}

	const s32 oldnum = g_Vars.currentplayernum;
	setCurrentPlayerNum(cl->playernum);

	if (bgunIsUsingSecondaryFunction()) {
		move->ucmd |= UCMD_SECONDARY;
	}

	if (pl->insightaimmode) {
		move->ucmd |= UCMD_AIMMODE;
		move->zoomfov = currentPlayerGetGunZoomFov();
	} else {
		move->zoomfov = 0.f;
	}

	setCurrentPlayerNum(oldnum);

	if (cl != g_NetLocalClient && !cl->forcetick && (move->ucmd & UCMD_FL_FORCEMASK)) {
		cl->forcetick = move->tick;
		sysLogPrintf(LOG_NOTE, "NET: forcing client %u to move at tick %u", cl->id, cl->forcetick);
	}
}

static inline s32 netClientNeedReliableMove(const struct netclient *cl)
{
	const struct netplayermove *move = &cl->outmove[0];
	const struct netplayermove *moveprev = &cl->outmove[1];
	return !moveprev->tick || (g_NetMode == NETMODE_SERVER && cl->forcetick) ||
		(moveprev->ucmd & UCMD_IMPORTANT_MASK) != (move->ucmd & UCMD_IMPORTANT_MASK) ||
		(move->ucmd & UCMD_ACTIVATE);
}

static inline s32 netClientNeedMove(const struct netclient *cl)
{
	if (g_NetTick < g_NetNextUpdate) {
		return false;
	}
	const struct netplayermove *move = &cl->outmove[0];
	const struct netplayermove *moveprev = &cl->outmove[1];
	if (move->tick && cl->outmoveack >= move->tick) {
		return false;
	}
	const u8 *cmpa = (const u8 *)move + sizeof(move->tick);
	const u8 *cmpb = (const u8 *)moveprev + sizeof(move->tick);
	return (memcmp(cmpa, cmpb, sizeof(*move) - sizeof(move->tick)) != 0);
}

static inline void netClientReadConfig(struct netclient *cl, const s32 playernum)
{
	cl->settings.options = g_PlayerConfigsArray[playernum].options;
	/* Use PRIMARY catalog ID fields directly */
	if (g_PlayerConfigsArray[playernum].base.body_id[0]) {
		strncpy(cl->settings.body_id, g_PlayerConfigsArray[playernum].base.body_id, CATALOG_ID_LEN - 1);
		cl->settings.body_id[CATALOG_ID_LEN - 1] = '\0';
	} else {
		cl->settings.body_id[0] = '\0';
	}
	if (g_PlayerConfigsArray[playernum].base.head_id[0]) {
		strncpy(cl->settings.head_id, g_PlayerConfigsArray[playernum].base.head_id, CATALOG_ID_LEN - 1);
		cl->settings.head_id[CATALOG_ID_LEN - 1] = '\0';
	} else {
		cl->settings.head_id[0] = '\0';
	}
	cl->settings.team = g_PlayerConfigsArray[playernum].base.team;
	cl->settings.handicap = g_PlayerConfigsArray[playernum].handicap;
	cl->settings.fovy = g_PlayerExtCfg[playernum].fovy;
	cl->settings.fovzoommult = g_PlayerExtCfg[playernum].fovzoommult;
	// Identity profile is the authoritative name source on PC.
	// Fall back to g_GameFile.name (agent name from save) if identity is not
	// initialized — the client never calls identityInit(), so the profile is
	// always empty on the client side. Final fallback: legacy N64 config name.
	identity_profile_t *profile = identityGetActiveProfile();
	if (profile && profile->name[0]) {
		strncpy(cl->settings.name, profile->name, sizeof(cl->settings.name) - 1);
		cl->settings.name[sizeof(cl->settings.name) - 1] = '\0';
	} else if (g_GameFile.name[0]) {
		strncpy(cl->settings.name, g_GameFile.name, sizeof(cl->settings.name) - 1);
		cl->settings.name[sizeof(cl->settings.name) - 1] = '\0';
	} else {
		memcpy(cl->settings.name, g_PlayerConfigsArray[playernum].base.name, sizeof(cl->settings.name));
		char *newline = strrchr(cl->settings.name, '\n');
		if (newline) {
			*newline = '\0';
		}
	}
}

static net_client_settings_wire_status_e netClientPrepareCachedSettings(
		struct netclient *cl, net_client_settings_plan_t *out_plan,
		player_identity_status_e *out_identity_status)
{
	net_client_settings_input_t input;

	if (!cl || !out_plan) {
		return NET_CLIENT_SETTINGS_WIRE_INVALID_ARGUMENT;
	}
	input.options = cl->settings.options;
	input.body_id = cl->settings.body_id;
	input.head_id = cl->settings.head_id;
	input.team = cl->settings.team;
	input.handicap = cl->settings.handicap;
	input.fovy = cl->settings.fovy;
	input.fovzoommult = cl->settings.fovzoommult;
	input.name = cl->settings.name;
	return netClientSettingsPrepare(&input, out_plan, out_identity_status);
}

static void netClientCommitPreparedSettings(struct netclient *cl,
		const net_client_settings_plan_t *plan)
{
	if (!cl || !plan) return;
	cl->settings.options = plan->options;
	strncpy(cl->settings.body_id, plan->identity.body_id,
		sizeof(cl->settings.body_id) - 1);
	cl->settings.body_id[sizeof(cl->settings.body_id) - 1] = '\0';
	strncpy(cl->settings.head_id, plan->identity.head_id,
		sizeof(cl->settings.head_id) - 1);
	cl->settings.head_id[sizeof(cl->settings.head_id) - 1] = '\0';
	cl->settings.team = plan->team;
	cl->settings.handicap = plan->handicap;
	cl->settings.fovy = plan->fovy;
	cl->settings.fovzoommult = plan->fovzoommult;
	memcpy(cl->settings.name, plan->name, sizeof(cl->settings.name));
}

static inline void netFlushSendBuffers(void)
{
	if (g_NetMsgRel.wp) {
		if (g_NetMsgRel.error) {
			sysLogPrintf(LOG_WARNING, "NET: reliable out buffer overflow");
		}
		g_NetReliableFrameLen += g_NetMsgRel.wp;
		netSend(NULL, &g_NetMsgRel, true, NETCHAN_DEFAULT);
	}

	if (g_NetMsg.wp) {
		if (g_NetMsg.error) {
			sysLogPrintf(LOG_WARNING, "NET: unreliable out buffer overflow");
		}
		g_NetUnreliableFrameLen += g_NetMsg.wp;
		netSend(NULL, &g_NetMsg, false, NETCHAN_DEFAULT);
	}
}

static bool netServerStageReplicationPeer(const struct netclient *cl,
		u8 room_id)
{
	return cl && cl->peer && cl->state == CLSTATE_GAME
		&& !(cl->flags & CLFLAG_SPECTATOR) && cl->room_id == room_id;
}

bool netServerStageReplicationBlocked(void)
{
	/* Read-only by design. netEndFrame is the sole RELEASE -> ACTIVE owner so a
	 * direct gameplay producer cannot make publication order depend on its tick
	 * site. INACTIVE is blocked too: a prospective lobby mode is not a stage. */
	return g_NetMode == NETMODE_SERVER
		&& s_NetStageReplicationPhase != NET_STAGE_REPLICATION_ACTIVE;
}

static void netResetStageReplication(const char *reason,
		bool clear_epoch)
{
	const net_stage_replication_phase_e prior = s_NetStageReplicationPhase;

	s_NetStageReplicationPhase = NET_STAGE_REPLICATION_INACTIVE;
	s_NetStageReplicationWaitMask = 0;
	s_NetStageAuthorityReady = false;
	g_NetPendingResyncFlags = 0;
	if (clear_epoch) {
		g_NetStageEpoch = 0;
	}
	netNpcReplicationReset();

	if (prior != NET_STAGE_REPLICATION_INACTIVE) {
		sysLogPrintf(LOG_NOTE,
			"NET.STAGE.REPLICATION phase=inactive epoch=%u prior=%u reason=%s",
			(unsigned)g_NetStageEpoch, (unsigned)prior,
			reason ? reason : "unknown");
	}
}

static void netServerRollbackStageReplicationBarrier(u32 prior_epoch,
		const char *reason)
{
	s_NetStageReplicationPhase = NET_STAGE_REPLICATION_INACTIVE;
	s_NetStageReplicationWaitMask = 0;
	s_NetStageAuthorityReady = false;
	g_NetPendingResyncFlags = 0;
	g_NetStageEpoch = prior_epoch;
	netNpcReplicationReset();
	sysLogPrintf(LOG_WARNING,
		"NET.STAGE.REPLICATION rollback epoch=%u phase=inactive reason=%s",
		(unsigned)prior_epoch, reason ? reason : "unknown");
}

static bool netServerArmStageReplicationBarrier(const char *reason)
{
	u32 wait_mask = 0;

	if (g_NetMode != NETMODE_SERVER
			|| s_NetStageReplicationPhase != NET_STAGE_REPLICATION_INACTIVE) {
		sysLogPrintf(LOG_ERROR,
			"NET.STAGE.REPLICATION arm=rejected phase=%u mode=%u reason=%s",
			(unsigned)s_NetStageReplicationPhase, (unsigned)g_NetMode,
			reason ? reason : "unknown");
		return false;
	}

	for (s32 i = 0; i < g_NetMaxClients && i < NET_MAX_CLIENTS; i++) {
		struct netclient *cl = &g_NetClients[i];
		if (!netServerStageReplicationPeer(cl, g_NetMatchRoomId)) {
			continue;
		}
		cl->stage_ready = false;
		wait_mask |= 1u << (u32)i;
	}

	g_NetStageEpoch++;
	if (g_NetStageEpoch == 0) {
		g_NetStageEpoch = 1;
	}
	s_NetStageReplicationPhase = NET_STAGE_REPLICATION_WAITING;
	s_NetStageReplicationWaitMask = wait_mask;
	s_NetStageAuthorityReady = false;
	netNpcReplicationReset();
	/* Stage publication may be initiated late in netEndFrame. Retire every
	 * shared gameplay byte from the lobby/current stage before serializing the
	 * new SVC_STAGE_START control transaction. */
	netbufStartWrite(&g_NetMsg);
	netbufStartWrite(&g_NetMsgRel);
	sysLogPrintf(LOG_NOTE,
		"NET.STAGE.REPLICATION phase=waiting epoch=%u room=%u wait_mask=0x%08x authority_ready=0 reason=%s",
		(unsigned)g_NetStageEpoch, (unsigned)g_NetMatchRoomId,
		(unsigned)wait_mask, reason ? reason : "unknown");
	return true;
}

static bool netServerStageReplicationReady(void)
{
	const u32 waited_mask = s_NetStageReplicationWaitMask;
	u32 pending_mask = 0;
	u32 active_mask = 0;

	if (g_NetMode != NETMODE_SERVER) {
		return true;
	}
	if (s_NetStageReplicationPhase == NET_STAGE_REPLICATION_ACTIVE
			|| s_NetStageReplicationPhase == NET_STAGE_REPLICATION_RELEASE) {
		return true;
	}
	if (s_NetStageReplicationPhase != NET_STAGE_REPLICATION_WAITING) {
		return false;
	}

	for (s32 i = 0; i < g_NetMaxClients && i < NET_MAX_CLIENTS; i++) {
		const u32 bit = 1u << (u32)i;
		struct netclient *cl;

		if (!(s_NetStageReplicationWaitMask & bit)) {
			continue;
		}
		cl = &g_NetClients[i];
		/* A disconnected or transactionally removed peer no longer owns a wait
		 * obligation. A reconnect enters PREPARING and receives its own targeted
		 * post-load transaction rather than joining this initial barrier. */
		if (!netServerStageReplicationPeer(cl, g_NetMatchRoomId)) {
			continue;
		}
		active_mask |= bit;
		if (!cl->stage_ready) {
			pending_mask |= bit;
		}
	}

	if (pending_mask || !s_NetStageAuthorityReady) {
		return false;
	}

	s_NetStageReplicationPhase = NET_STAGE_REPLICATION_RELEASE;
	sysLogPrintf(LOG_NOTE,
		"NET.STAGE.REPLICATION phase=release epoch=%u room=%u ready_mask=0x%08x authority_ready=1 departed_mask=0x%08x",
		(unsigned)g_NetStageEpoch, (unsigned)g_NetMatchRoomId,
		(unsigned)active_mask, (unsigned)(waited_mask & ~active_mask));
	return true;
}

typedef u32 (*net_resync_writer_fn)(struct netbuf *dst);

/* Append one complete resync message or leave the destination byte-for-byte
 * writable at its prior boundary. The pending bit is cleared by the caller
 * only after the complete baseline is queued. */
static bool netAppendResyncTransaction(struct netbuf *dst,
		net_resync_writer_fn writer)
{
	const u32 wp_before = dst ? dst->wp : 0;
	const u32 error_before = dst ? dst->error : 1;

	if (!dst || !writer || error_before || writer(dst) != 0
			|| dst->error || dst->wp <= wp_before) {
		if (dst) {
			dst->wp = wp_before;
			dst->error = error_before;
		}
		return false;
	}

	return true;
}

static bool netAppendNpcResyncTransaction(struct netbuf *dst);

/* Build and queue the complete requested baseline as one reliable packet. No
 * individual ownership bit is consumed until ENet accepts the whole packet
 * for every peer in the match room. RELEASE may therefore retry without ever
 * exposing a partial baseline or allowing an incremental update to overtake
 * it. */
static bool netServerPublishPendingResyncs(void)
{
	const u8 requested = g_NetPendingResyncFlags;
	struct netbuf wire = {
		.data = s_NetResyncTxnBuf,
		.size = sizeof(s_NetResyncTxnBuf),
	};
	bool complete = true;
	u32 bytes;

	if (!requested) {
		if (s_NetStageReplicationPhase == NET_STAGE_REPLICATION_RELEASE) {
			sysLogPrintf(LOG_ERROR,
				"NET.STAGE.REPLICATION release=retry epoch=%u reason=missing-baseline",
				(unsigned)g_NetStageEpoch);
			return false;
		}
		return true;
	}
	if (g_NetMode != NETMODE_SERVER || !g_NetHost) {
		sysLogPrintf(LOG_WARNING,
			"NET: baseline publish unavailable flags=0x%x; request retained for retry",
			(unsigned)requested);
		return false;
	}

	netbufStartWrite(&wire);
	if (requested & NET_RESYNC_FLAG_CHRS) {
		sysLogPrintf(LOG_NOTE,
			"NET: sending chr resync to all clients (%u bots)", g_BotCount);
		if (!netAppendResyncTransaction(&wire, netmsgSvcChrResyncWrite)) {
			sysLogPrintf(LOG_WARNING,
				"NET: chr resync append failed; request retained for retry");
			complete = false;
		}
	}
	if (complete && (requested & NET_RESYNC_FLAG_PROPS)) {
		sysLogPrintf(LOG_NOTE, "NET: sending prop resync to all clients");
		if (!netAppendResyncTransaction(&wire, netmsgSvcPropResyncWrite)) {
			sysLogPrintf(LOG_WARNING,
				"NET: prop resync append failed; request retained for retry");
			complete = false;
		}
	}
	if (complete && (requested & NET_RESYNC_FLAG_SCORES)) {
		sysLogPrintf(LOG_NOTE, "NET: sending score resync to all clients");
		if (!netAppendResyncTransaction(&wire,
				netmsgSvcPlayerScoresWrite)) {
			sysLogPrintf(LOG_WARNING,
				"NET: score resync append failed; request retained for retry");
			complete = false;
		}
	}
	if (complete && (requested & NET_RESYNC_FLAG_NPCS)) {
		sysLogPrintf(LOG_NOTE,
			"NET: sending npc resync to all clients (%u npcs)", netNpcCount());
		if (!netAppendNpcResyncTransaction(&wire)) {
			sysLogPrintf(LOG_WARNING,
				"NET: npc resync group append failed; request retained for retry");
			complete = false;
		}
	}

	if (!complete || wire.error || wire.wp == 0) {
		netbufStartWrite(&wire);
		sysLogPrintf(LOG_WARNING,
			"NET: baseline transaction build failed flags=0x%x; request retained for retry",
			(unsigned)requested);
		return false;
	}

	bytes = wire.wp;
	if (netSendToRoom(g_NetMatchRoomId, &wire, true,
			NETCHAN_DEFAULT) != bytes) {
		sysLogPrintf(LOG_WARNING,
			"NET: baseline transaction queue failed flags=0x%x bytes=%u; request retained for retry",
			(unsigned)requested, (unsigned)bytes);
		return false;
	}

	g_NetReliableFrameLen += bytes;
	g_NetPendingResyncFlags &= (u8)~requested;
	if (requested & NET_RESYNC_FLAG_SCORES) {
		/* c3845 (2026-06-23): match-smoke milestone — the stage-start
		 * baseline carries a scores broadcast immediately. */
		sysLogPrintf(LOG_NOTE, "MATCH: scores replicated tick=%u", g_NetTick);
	}
	sysLogPrintf(LOG_NOTE,
		"NET.STAGE.REPLICATION baseline=queued epoch=%u room=%u flags=0x%x bytes=%u",
		(unsigned)g_NetStageEpoch, (unsigned)g_NetMatchRoomId,
		(unsigned)requested, (unsigned)bytes);
	return true;
}

/* The NPC snapshot and its mission-state companions are one correction unit.
 * Never expose a partial group, and never consume the retry flag on failure. */
static bool netAppendNpcResyncTransaction(struct netbuf *dst)
{
	const u32 wp_before = dst ? dst->wp : 0;
	const u32 error_before = dst ? dst->error : 1;
	bool complete = dst && !error_before && g_ObjectiveLastIndex <= 255;

	if (complete && netmsgSvcNpcResyncWrite(dst) != 0) {
		complete = false;
	}
	/* v58: the digest validates the exact snapshot immediately preceding it
	 * on this reliable ordered transaction. A standalone periodic checksum has
	 * no coherent sample time and is therefore forbidden. */
	if (complete && netmsgSvcNpcSyncWrite(dst) != 0) {
		complete = false;
	}
	if (complete && netmsgSvcStageFlagWrite(dst) != 0) {
		complete = false;
	}

	for (s32 i = 0; complete && i <= g_ObjectiveLastIndex; i++) {
		if (netmsgSvcObjStatusWrite(dst, (u8)i,
				(u8)g_ObjectiveStatuses[i]) != 0) {
			complete = false;
		}
	}

	if (!complete || dst->error || dst->wp <= wp_before) {
		if (dst) {
			dst->wp = wp_before;
			dst->error = error_before;
		}
		return false;
	}

	return true;
}

static inline const char *netGetDisconnectReason(const u32 reason)
{
	static const char *msgs[] = {
		"Unknown",
		"Server is shutting down",
		"Protocol or version mismatch",
		"Kicked by console",
		"You are banned on this server",
		"Connection timed out",
		"Server is full",
		"The game is already in progress",
		"Your files differ from the server's",
		"Player left the game",
		"Too many failed admin authentication attempts"
	};
	if (reason < (u32)ARRAYCOUNT(msgs)) {
		return msgs[reason];
	}
	return msgs[0];
}

/* ============================================================================
 * SEC-7 — DDoS query reflection mitigation
 *
 * The PDQM info query is a small (5 byte) UDP request with a fat (~256-512
 * byte) reply, giving an attacker ~50-100x amplification — a classic
 * reflection vector. We split the path into two stages:
 *
 *   Stage 1: client sends 5-byte query → server returns a 17-byte challenge
 *            (magic + 0xFFFFFFFF marker + 8-byte HMAC token bound to source
 *             address and a 30s time slot).
 *   Stage 2: client re-sends 13-byte query (magic + token) → server verifies
 *            and replies with the full info packet.
 *
 * The 8-byte token = SHA256(secret || addr || time_slot)[0..8]. The server
 * secret is generated once per process and never leaves memory. Tokens
 * commit the responder to one path-validated peer per 30s window. An
 * attacker spoofing the victim's IP gets the challenge sent to the victim
 * (who never asked) and cannot echo back from a different IP.
 *
 * Per-/24 rate limit (1 reply per second) is layered on top to cap server
 * cost under sustained probing from real (non-spoofed) IPs.
 * ============================================================================ */

static u8  s_QuerySecret[32];
static s32 s_QuerySecretInit = 0;
static struct {
	u32 subnet24;
	u32 last_ms;
} s_QueryRateTable[NET_QUERY_RATE_SLOTS];

static void netServerEnsureQuerySecret(void)
{
	if (s_QuerySecretInit) return;
	/* Mix several entropy sources through SHA-256.  None individually is
	 * cryptographically strong, but the truncated digest is what an
	 * attacker observes — no shortcut to recover the secret without
	 * solving SHA-256 preimages. */
	u32 seeds[8];
	seeds[0] = (u32)time(NULL);
	seeds[1] = (u32)enet_time_get();
	seeds[2] = (u32)(uintptr_t)&s_QuerySecret;
	seeds[3] = (u32)(uintptr_t)netServerEnsureQuerySecret;
	seeds[4] = (u32)rand();
	seeds[5] = (u32)rand();
	seeds[6] = (u32)rand();
	seeds[7] = (u32)clock();
	sha256Hash(seeds, sizeof(seeds), s_QuerySecret);
	s_QuerySecretInit = 1;
}

static u32 netServerAddrSubnet24(const ENetAddress *addr)
{
	/* IPv4-mapped: ipv4.ffff == 0xFFFF and ipv4.ip is the 4-byte v4 addr. */
	if (addr->ipv4.ffff == 0xFFFF) {
		u32 ip;
		memcpy(&ip, &addr->ipv4.ip, 4);
		return ip & 0xFFFFFF00u;
	}
	/* Pure IPv6: fold first 8 bytes (~/64) into a u32 stand-in. */
	u32 a = 0, b = 0;
	memcpy(&a, ((const u8 *)&addr->ipv6) + 0, 4);
	memcpy(&b, ((const u8 *)&addr->ipv6) + 4, 4);
	return a ^ b;
}

static void netServerComputeQueryToken(const ENetAddress *addr, u64 slot,
                                        u8 token[NET_QUERY_TOKEN_LEN])
{
	sha256_ctx ctx;
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Init(&ctx);
	sha256Update(&ctx, s_QuerySecret, sizeof(s_QuerySecret));
	sha256Update(&ctx, &addr->ipv6, sizeof(addr->ipv6));
	sha256Update(&ctx, &addr->port, sizeof(addr->port));
	sha256Update(&ctx, &slot, sizeof(slot));
	sha256Final(&ctx, digest);
	memcpy(token, digest, NET_QUERY_TOKEN_LEN);
}

static s32 netServerVerifyQueryToken(const ENetAddress *addr,
                                      const u8 token[NET_QUERY_TOKEN_LEN])
{
	netServerEnsureQuerySecret();
	const u64 now_slot = (u64)time(NULL) / (NET_QUERY_TIME_SLOT_MS / 1000u);
	u8 expected[NET_QUERY_TOKEN_LEN];
	netServerComputeQueryToken(addr, now_slot, expected);
	if (memcmp(token, expected, NET_QUERY_TOKEN_LEN) == 0) return 1;
	netServerComputeQueryToken(addr, now_slot - 1, expected);
	if (memcmp(token, expected, NET_QUERY_TOKEN_LEN) == 0) return 1;
	return 0;
}

static s32 netServerQueryRateAllow(const ENetAddress *addr)
{
	const u32 subnet = netServerAddrSubnet24(addr);
	const u32 now    = (u32)enet_time_get();
	/* Mix subnet to a slot index. Collisions are tolerated — a clashing
	 * subnet just shares the quota for one window. */
	u32 h = subnet;
	h ^= h >> 16; h *= 0x7feb352du;
	h ^= h >> 15; h *= 0x846ca68bu;
	h ^= h >> 16;
	const u32 slot = h & (NET_QUERY_RATE_SLOTS - 1u);
	if (s_QueryRateTable[slot].subnet24 == subnet) {
		if ((now - s_QueryRateTable[slot].last_ms) < NET_QUERY_RATE_WINDOW_MS) {
			return 0;
		}
		s_QueryRateTable[slot].last_ms = now;
		return 1;
	}
	/* Different subnet in the slot: claim if the existing entry is aged out
	 * or empty.  Otherwise pass through without updating (don't penalise
	 * the unrelated peer for a hash collision). */
	if (s_QueryRateTable[slot].subnet24 == 0
	    || (now - s_QueryRateTable[slot].last_ms) >= NET_QUERY_RATE_WINDOW_MS) {
		s_QueryRateTable[slot].subnet24 = subnet;
		s_QueryRateTable[slot].last_ms  = now;
	}
	return 1;
}

static void netServerSendQueryChallenge(ENetAddress *address)
{
	netServerEnsureQuerySecret();
	const u64 slot = (u64)time(NULL) / (NET_QUERY_TIME_SLOT_MS / 1000u);
	u8 token[NET_QUERY_TOKEN_LEN];
	netServerComputeQueryToken(address, slot, token);

	const u32 magic_len = (u32)(sizeof(NET_QUERY_MAGIC) - 1);
	u8 buf[16 + NET_QUERY_TOKEN_LEN];
	memcpy(buf, NET_QUERY_MAGIC, magic_len);
	const u32 marker = NET_QUERY_CHALLENGE_MARKER;
	memcpy(buf + magic_len, &marker, sizeof(marker));
	memcpy(buf + magic_len + sizeof(marker), token, NET_QUERY_TOKEN_LEN);

	ENetBuffer ebuf;
	ebuf.data       = buf;
	ebuf.dataLength = magic_len + sizeof(marker) + NET_QUERY_TOKEN_LEN;
	enet_socket_send(g_NetHost->socket, address, &ebuf, 1);
}

static void netServerQueryResponse(ENetAddress *address)
{
	static u8 data[512]; /* increased from 256 to fit external address field */
	static ENetBuffer ebuf;
	struct netbuf buf = { .data = data, .size = sizeof(data) };
	// bit 0: server is in an active match (not in lobby)
	// bit 1: team game enabled — set when MPOPTION_TEAMSENABLED is active.
	//        Future: extend with NETFLAG_COOP (bit 2) and NETFLAG_ANTIOP (bit 3) once
	//        cooperative/counter-operative campaign modes are wired to g_MpSetup.
	const u8 flags = (g_NetLocalClient && g_NetLocalClient->state > CLSTATE_LOBBY)
		| ((g_MpSetup.options & MPOPTION_TEAMSENABLED) ? (1 << 1) : 0);
	const char *modDir = fsGetModDir();
	if (!modDir) {
		modDir = "";
	}

	netbufStartWrite(&buf);
	netbufWriteData(&buf, NET_QUERY_MAGIC, sizeof(NET_QUERY_MAGIC) - 1);
	netbufWriteU16(&buf, 0); // space for size
	netbufWriteU32(&buf, NET_PROTOCOL_VER);
	netbufWriteU8(&buf, flags);
	netbufWriteU8(&buf, g_NetNumClients);
	netbufWriteU8(&buf, g_NetMaxClients);
	netbufWriteU8(&buf, g_StageNum);
	/* M0.1d: scenario as catalog ID string for server browser.
	 * Prefer g_MatchConfig.scenario_id, fall back to runtime resolution. */
	{
		const char *sid = g_MatchConfig.scenario_id[0]
			? g_MatchConfig.scenario_id
			: catalogGameModeIdByScenarioIndex((s32)g_MpSetup.scenario);
		netbufWriteStr(&buf, sid ? sid : "base:combat");
	}
	netbufWriteStr(&buf, g_NetLocalClient ? g_NetLocalClient->settings.name : "");
	netbufWriteStr(&buf, g_RomName);
	netbufWriteStr(&buf, modDir);

	/* Protocol 23+: advertise best known external address for hole punch / direct connect.
	 * Priority: UPnP (real external IP + mapped port) > STUN > empty string.
	 * Clients on older protocol versions ignore trailing fields they don't know about. */
	{
		char extAddr[NET_MAX_ADDR] = {0};
		if (netUpnpIsActive() && netUpnpGetExternalIP()[0]) {
			snprintf(extAddr, sizeof(extAddr), "%s:%u",
			         netUpnpGetExternalIP(), g_NetServerPort);
		} else if (stunGetStatus() == STUN_STATUS_SUCCESS) {
			snprintf(extAddr, sizeof(extAddr), "%s:%u",
			         stunGetExternalIP(), stunGetExternalPort());
		}
		netbufWriteStr(&buf, extAddr);
	}

	netbufWriteU16(&buf, 0); // space for checksum

	ebuf.data = buf.data;
	ebuf.dataLength = buf.wp;

	// rewrite size
	buf.wp = sizeof(NET_QUERY_MAGIC) - 1;
	netbufWriteU16(&buf, ebuf.dataLength);

	// calculate and rewrite checksum
	buf.wp = ebuf.dataLength - sizeof(u16);
	u16 crc = 0xFFFF;
	u16 x;
	for (u32 i = 0; i < buf.wp; ++i) {
		x = crc >> 8 ^ buf.data[i];
		x ^= x >> 4;
		crc += (crc << 8) ^ (x << 12) ^ (x << 5) ^ x;
	}
	netbufWriteU16(&buf, crc);

	enet_socket_send(g_NetHost->socket, address, &ebuf, 1);
}

static s32 netServerConnectionlessPacket(ENetEvent *event, ENetAddress *address, u8 *rxdata, s32 rxlen)
{
	if (rxdata && rxlen >= PUNCH_MAGIC_LEN) {
		const s32 magic_len = (s32)(sizeof(NET_QUERY_MAGIC) - 1);
		if (rxlen >= magic_len && !memcmp(rxdata, NET_QUERY_MAGIC, magic_len)) {
			/* SEC-7: per-/24 rate limit applies to both stages.  Drop
			 * silently when the budget is exceeded — anything we send
			 * here, including an error, would itself be amplification. */
			if (!netServerQueryRateAllow(address)) {
				return 1;
			}
			if (rxlen == magic_len) {
				/* Stage 1: bare query → issue a small challenge. */
				netServerSendQueryChallenge(address);
				return 1;
			}
			if (rxlen == magic_len + (s32)NET_QUERY_TOKEN_LEN) {
				/* Stage 2: token-echo → verify and send full response. */
				if (!netServerVerifyQueryToken(address, rxdata + magic_len)) {
					sysLogPrintf(LOG_WARNING | LOGFLAG_NOCON,
					    "NET: query from %s — bad/expired challenge token, ignoring",
					    netFormatAddr(address));
					return 1;
				}
				sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON,
				    "NET: query (authenticated) from %s, responding",
				    netFormatAddr(address));
				netServerQueryResponse(address);
				return 1;
			}
			/* Malformed query length — drop silently. */
			return 1;
		}
		if (!memcmp(rxdata, PUNCH_REQ_MAGIC, PUNCH_MAGIC_LEN)) {
			// UDP hole punch request — send 3 PUNCH_ACK packets to client's external address
			netServerHandlePunchReq(g_NetHost, address, rxdata, rxlen);
			return 1;
		}
	}
	// probably a normal packet, pass through to enet
	(void)event;
	return 0;
}

struct netclient *netClientForPlayerNum(s32 playernum)
{
	s32 slot = 0;
	for (s32 i = 0; i < g_NetMaxClients; ++i) {
		struct netclient *cl = &g_NetClients[i];
		if (cl->state >= CLSTATE_LOBBY) {
			if (slot == playernum) {
				return cl;
			}
			++slot;
		}
	}
	return NULL;
}

static char g_NetBindAddr[NET_MAX_ADDR + 1] = {0};
s32 g_NetDedicated = false;

void netInit(void)
{
	if (enet_initialize() < 0) {
		sysLogPrintf(LOG_ERROR, "NET: could not init ENet, disabling networking");
		return;
	}

	const s32 argmaxclients = sysArgGetInt("--maxclients", -1);
	if (argmaxclients > 0 && argmaxclients <= NET_MAX_CLIENTS) {
		g_NetMaxClients = argmaxclients;
	}

	const s32 argport = sysArgGetInt("--port", -1);
	if (argport > 0 && argport < 0x10000) {
		g_NetServerPort = argport;
	}

	/* --bind <ip> : bind server to a specific network interface (e.g., Hamachi IP) */
	const char *argbind = sysArgGetString("--bind");
	if (argbind) {
		strncpy(g_NetBindAddr, argbind, sizeof(g_NetBindAddr) - 1);
		sysLogPrintf(LOG_NOTE, "NET: will bind to %s", g_NetBindAddr);
	}

	const char *argjoin = sysArgGetString("--connect");
	if (argjoin) {
		strncpy(g_NetLastJoinAddr, argjoin, sizeof(g_NetLastJoinAddr) - 1);
		g_NetJoinLatch = true;
	}

	if (sysArgCheck("--host")) {
		g_NetHostLatch = true;
	}

	/* --dedicated : server-only mode, no local player needed */
	if (sysArgCheck("--dedicated")) {
		g_NetDedicated = true;
		g_NetHostLatch = true;
		sysLogPrintf(LOG_NOTE, "NET: dedicated server mode enabled");
	}

	stunInit();

	g_NetInit = true;
}

s32 netStartServer(u16 port, s32 maxclients)
{
	if (g_NetMode || !g_NetInit) {
		return -1;
	}

	/* Reserve the complete shipping participant domain before publishing an
	 * ENet listen host.  Lobby-start transactions must not discover that the
	 * authoritative roster cannot be represented after peers can connect. */
	if ((!g_MpParticipants.slots || g_MpParticipants.capacity < MAX_MPCHRS)
			&& !mpParticipantPoolResize(MAX_MPCHRS)) {
		sysLogPrintf(LOG_ERROR,
			"NET: could not reserve %d match participant slots before server start",
			MAX_MPCHRS);
		return -2;
	}

	memset(&g_NetLocalAddr, 0, sizeof(g_NetLocalAddr));
	g_NetLocalAddr.port = port;

	/* If --bind was specified, bind to that specific IP instead of all interfaces */
	if (g_NetBindAddr[0]) {
		if (enet_address_set_ip(&g_NetLocalAddr, g_NetBindAddr) == 0) {
			sysLogPrintf(LOG_NOTE, "NET: binding to %s:%u", g_NetBindAddr, port);
		} else {
			sysLogPrintf(LOG_WARNING, "NET: could not resolve bind address '%s', using all interfaces", g_NetBindAddr);
		}
	}

	g_NetHost = enet_host_create(&g_NetLocalAddr, maxclients, NETCHAN_COUNT, g_NetServerInRate, g_NetServerOutRate, 0);
	if (!g_NetHost) {
		sysLogPrintf(LOG_ERROR, "NET: could not create ENet host on port %u", port);
		return -3;
	}
	netUploadMeasurementReset();

	if (g_NetServerInfoQuery) {
		enet_host_set_intercept_callback(g_NetHost, netServerConnectionlessPacket);
	}

	netClientResetAll();
	g_NetMaxClients = maxclients;

	if (g_NetDedicated) {
		/* Dedicated server has no local player — slot 0 is free for real players. */
		g_NetLocalClient = NULL;
		g_NetNumClients = 0;
	} else {
		/* Listen server: the host occupies slot 0. */
		g_NetLocalClient = &g_NetClients[0];
		g_NetLocalClient->state = CLSTATE_LOBBY;
		netClientReadConfig(g_NetLocalClient, 0);
	}

	g_NetMode = NETMODE_SERVER;
	netResetStageReplication("server-start", true);
	netmsgClcAuthClearCookie();
	memset(&s_NetReconnectAttempt, 0, sizeof(s_NetReconnectAttempt));

	g_NetTick = 0;
	g_NetNextUpdate = 0;
	g_NetNextSyncId = 1;
	g_NetFirstDynamicSyncId = 1;
	netSyncIdMapClear(); // ensure map is empty from any previous session

	/* S300 / SP-14: belt-and-braces — make sure no room-scoped state
	 * survived an earlier session that terminated without a clean stage-end. */
	g_NetMatchRoomId = 0xFF;
	g_NetCounterOpClientId = NET_NULL_CLIENT;

	sysLogPrintf(LOG_NOTE, "NET: using protocol version %d", NET_PROTOCOL_VER);
	sysLogPrintf(LOG_NOTE, "NET: created server on port %u", port);

	/* Start async UPnP port forwarding (runs on background thread).
	 * Returns immediately — discovery takes 2-5 seconds on the thread.
	 * Poll netUpnpGetStatus() or check the lobby overlay for results. */
	netUpnpSetup(port);

	/* Start async STUN discovery (runs on background thread, binds port with
	 * SO_REUSEADDR so the NAT device maps the same external port as ENet).
	 * Returns immediately — poll stunGetStatus() to check progress.
	 * Also performs 2-probe NAT type detection for hole-punch viability. */
	stunDiscoverAsync(port);

	/* c3845: the room/hub subsystem is normally initialised by hubInit(), which
	 * the in-client listen host never calls -- so the room pool kept its zeroed
	 * BSS state (every slot ROOM_STATE_LOBBY=0, never ROOM_STATE_CLOSED=4) and
	 * findFreeSlot() reported "no free slots" for every roomCreate, breaking room
	 * hosting (not just the match smoke). Initialise it here at listen-host server
	 * start; roomsInit() self-guards against a double-init. */
	{
		extern void roomsInit(void);
		roomsInit();
	}
	lobbyInit();
	netDistribInit();

	return 0;
}

static void netServerPrepareInClientStageTransition(const char *reason)
{
#if !defined(PD_SERVER)
	/* B-1076: menuStop clears the legacy stack; the scene-transition owner
	 * then releases every PC pool slot and its input context before menuReset
	 * can make those owners unreachable. */
	menuStop();
	sceneStageTransitionPrepare(SCENE_STAGE_TRANSITION_RELEASE_MENU_POOL,
		reason);
#else
	(void)reason;
#endif
}

s32 netServerStageStart(void)
{
	u8 state_before[NET_MAX_CLIENTS];
	const u32 match_seed_before = g_NetMatchSeed;
	const u32 stage_epoch_before = g_NetStageEpoch;

	/* S301 Airbase diag: entry breadcrumb. If this fires but the
	 * corresponding "netServerStageStart called" log does not appear
	 * in pd-server.log, we bailed on the NETMODE_SERVER check (which
	 * indicates we reached this path on a listen-client or dedicated
	 * client — either wiring error). */
	crashBreadcrumbPush("netServerStageStart entry mode=%d dedicated=%d",
		(int)g_NetMode, (int)g_NetDedicated);

	if (g_NetMode != NETMODE_SERVER) {
		sysLogPrintf(LOG_WARNING,
			"MATCHSTART.DIAG: netServerStageStart bailed — g_NetMode=%d (not SERVER=%d)",
			(int)g_NetMode, (int)NETMODE_SERVER);
		return -1;
	}

	/* NOTE: do NOT guard on g_StageNum == STAGE_CITRAINING here.
	 * The server lobby runs on STAGE_CITRAINING, and mainChangeToStage() is
	 * deferred — g_StageNum still equals STAGE_CITRAINING when this function
	 * runs.  Guarding on it prevented SVC_STAGE_START from ever being sent,
	 * so clients never received the map-load signal.  This function is only
	 * called from the CLC_LOBBY_START handler after leader validation, so
	 * there is no need for a stage-num guard. */

	{
		int clientCount = 0;
		for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
			if (g_NetClients[ci].state == CLSTATE_LOBBY || g_NetClients[ci].state == CLSTATE_PREPARING) {
				clientCount++;
			}
		}
		sysLogPrintf(LOG_WARNING, "MATCH-START: netServerStageStart called, stage_id='%s', sending SVC_STAGE_START to %d clients",
		             g_MpSetup.stage_id, clientCount);
		/* S301 Airbase diag: same line under the MATCHSTART.DIAG: grep tag. */
		sysLogPrintf(LOG_NOTE,
			"MATCHSTART.DIAG: netServerStageStart stage='%s' clients=%d dedicated=%d room=0x%02x",
			g_MpSetup.stage_id, clientCount, (int)g_NetDedicated,
			(unsigned)g_NetMatchRoomId);
		if (!g_MpSetup.stage_id[0]) {
			sysLogPrintf(LOG_WARNING, "MATCH-START: WARNING stage_id is EMPTY at netServerStageStart");
			sysLogPrintf(LOG_WARNING,
				"MATCHSTART.DIAG: BAILED reason=empty_stage_id");
		}
	}

	/* R-3: Transition room members (or all clients if no room) to CLSTATE_GAME.
	 * Phase E: also transition CLSTATE_PREPARING clients (those who sent READY
	 * through the ready gate); declined clients are already back in CLSTATE_LOBBY
	 * and are intentionally left there as spectators. */
	for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
		state_before[ci] = g_NetClients[ci].state;
		if (g_NetClients[ci].state == CLSTATE_PREPARING) {
			if (g_NetMatchRoomId != 0xFF && g_NetClients[ci].room_id != g_NetMatchRoomId) {
				continue;
			}
			if (g_NetClients[ci].flags & CLFLAG_SPECTATOR) {
				continue;
			}
			g_NetClients[ci].state = CLSTATE_GAME;
		}
	}
	g_NetMatchSeed = (u32)(g_RngSeed ^ (g_RngSeed >> 32)) ^ g_NetTick;
	if (!netServerArmStageReplicationBarrier("combat-stage-start")) {
		for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
			g_NetClients[ci].state = state_before[ci];
		}
		g_NetMatchSeed = match_seed_before;
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.ROLLBACK server-stage reason=replication_lifecycle_rejected stage_started=0");
		return -2;
	}

	/* Typed identities, room-scoped roster, participant mask, and session
	 * references must all validate and serialize before mpStartMatch changes
	 * local stage state. The prepared packet is the exact committed roster. */
	if (netmsgSvcStageStartValidate() != 0) {
		for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
			g_NetClients[ci].state = state_before[ci];
		}
		g_NetMatchSeed = match_seed_before;
		netServerRollbackStageReplicationBarrier(stage_epoch_before,
			"combat-stage-preflight");
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.ROLLBACK server-stage reason=preflight_rejected stage_started=0");
		return -3;
	}
	netbufStartWrite(&g_NetMsgRel);
	if (netmsgServerStageStartWrite(&g_NetMsgRel,
			g_NetMatchRoomId) != 0) {
		for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
			g_NetClients[ci].state = state_before[ci];
		}
		g_NetMatchSeed = match_seed_before;
		netbufStartWrite(&g_NetMsgRel);
		netServerRollbackStageReplicationBarrier(stage_epoch_before,
			"combat-stage-write");
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.ROLLBACK server-stage reason=stage_write_or_authority_commit_rejected stage_started=0");
		return -4;
	}

	{
		const u32 stage_wire_len = g_NetMsgRel.wp;
		if (netSendToRoom(g_NetMatchRoomId, &g_NetMsgRel, true,
				NETCHAN_DEFAULT) != stage_wire_len) {
			for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
				g_NetClients[ci].state = state_before[ci];
			}
			g_NetMatchSeed = match_seed_before;
			netmsgCutsceneAuthorityReset();
			netServerRollbackStageReplicationBarrier(stage_epoch_before,
				"combat-stage-send");
			sysLogPrintf(LOG_ERROR,
				"PLAYER.INIT.ROLLBACK server-stage reason=stage_send_rejected stage_started=0");
			return -5;
		}
	}

	/* B-1039: The listen host is a real Combat Simulator participant too. The
	 * validated packet is queued first, then this synchronous local commit makes
	 * the authority consume the same immutable participant set before ENet is
	 * flushed. The dedicated build resolves mpStartMatch to its headless stub. */
	mpStartMatch();
	/* B-1076: the in-client server owns the same menu transition as an offline
	 * start and a receiving client. */
	netServerPrepareInClientStageTransition("server stage start combat");
	/* A published stage, not a prepared packet, consumes prior-round identity
	 * reservations. */
	netServerClearPreservedPlayers("combat_stage_start");

	extern s32 g_MainChangeToStageNum;
	sysLogPrintf(LOG_NOTE, "NET: === STAGE START === stage=0x%02x (g_StageNum=0x%02x, pending=0x%02x) scenario=%u clients=%d",
	             (g_MainChangeToStageNum >= 0) ? (u8)g_MainChangeToStageNum : g_StageNum,
	             g_StageNum, g_MainChangeToStageNum, g_MpSetup.scenario, g_NetNumClients);

	// Log each connected client's state for debugging
	for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
		if (g_NetClients[ci].state != CLSTATE_DISCONNECTED) {
			sysLogPrintf(LOG_NOTE, "NET:   client %u '%s' state=%u playernum=%u head='%s' body='%s' team=%u",
			             g_NetClients[ci].id, g_NetClients[ci].settings.name,
			             g_NetClients[ci].state, g_NetClients[ci].playernum,
			             g_NetClients[ci].settings.head_id, g_NetClients[ci].settings.body_id,
			             g_NetClients[ci].settings.team);
		}
	}

	if (g_NetMatchRoomId != 0xFF) {
		sysLogPrintf(LOG_NOTE,
			"MATCHSTART.DIAG: SVC_STAGE_START sent to room 0x%02x (combat sim)",
			(unsigned)g_NetMatchRoomId);
	} else {
		sysLogPrintf(LOG_NOTE,
			"MATCHSTART.DIAG: SVC_STAGE_START sent to lounge scope (combat sim)");
	}
	crashBreadcrumbPush("SVC_STAGE_START sent mode=%d room=0x%02x",
		(int)g_NetGameMode, (unsigned)g_NetMatchRoomId);

	/* c3845 (2026-06-23): two-process match-smoke milestone. Fires on the
	 * HOST as the authoritative stage-start signal is broadcast. */
	{
		s32 matchPlayers = 0;
		for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
			if (g_NetClients[ci].state == CLSTATE_GAME) {
				matchPlayers++;
			}
		}
		sysLogPrintf(LOG_NOTE,
			"MATCH: server stage start arena='%s' seed=0x%08x players=%d",
			g_MpSetup.stage_id, (unsigned)g_NetMatchSeed, matchPlayers);
	}

	/* Dedicated server: BOT_AUTHORITY is deferred until all clients confirm
	 * their stage is loaded via CLC_STAGE_READY, so that the authority client's
	 * pads/spawn-points are ready before botSpawnAll() fires.  mpStartMatch()
	 * above already allocated the dedicated stub state.  On a listen server the
	 * host runs full bot AI directly, so no relay is needed. */
	if (g_NetDedicated) {
		/* The common replication barrier already reset the exact stage peers.
		 * Dedicated mode additionally arms its bot-authority deadline. */
		g_NetBotAuthorityDelegated = false;
		g_NetBotAuthorityClientId  = NET_NULL_CLIENT;
		g_NetStageReadyDeadline    = (s32)(g_NetTick + 300); /* 300 ticks = 5 s at 60 fps */
		sysLogPrintf(LOG_NOTE, "NET: waiting for CLC_STAGE_READY from %d client(s), deadline tick %d",
		             g_NetNumClients, (int)g_NetStageReadyDeadline);
	}

	// Schedule a full state resync shortly after stage start.
	// Bots and props are created deterministically from the same RNG seed,
	// but a full resync ensures all clients converge even if timing differs.
	// L1-5: include SCORES so late-joining clients or clients that miss early
	// kills get current scores immediately rather than waiting for the next
	// 300-frame periodic broadcast (L1-2).
	g_NetPendingResyncFlags = NET_RESYNC_FLAG_CHRS | NET_RESYNC_FLAG_PROPS | NET_RESYNC_FLAG_SCORES;
	netPropSnapReset();

	sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE_START sent, resync flags=0x%x", g_NetPendingResyncFlags);
	return 0;
}

s32 netServerCoopStageStart(u8 stagenum, u8 difficulty)
{
	struct missionconfig mission_before;
	u8 state_before[NET_MAX_CLIENTS];
	u8 playernum_before[NET_MAX_CLIENTS];
	struct netclient *roster[MAX_PLAYERS];
	const char *stage_id;
	const asset_entry_t *stage_entry;
	u8 roster_count = 0;
	s32 anti_playernum = -1;
	s32 bond_before;
	s32 coop_before;
	s32 anti_before;
	u32 match_seed_before;
	u32 stage_epoch_before;

	if (g_NetMode != NETMODE_SERVER) {
		return -1;
	}
	if (g_NetGameMode != NETGAMEMODE_COOP
			&& g_NetGameMode != NETGAMEMODE_ANTI) {
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.ROLLBACK coop-stage reason=invalid_mode mode=%d",
			g_NetGameMode);
		return -2;
	}
	if (difficulty > DIFF_PD) {
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.ROLLBACK coop-stage reason=invalid_difficulty value=%u",
			(unsigned)difficulty);
		return -3;
	}
	/* The typed mission ID prepared by CLC_LOBBY_START is authoritative.
	 * Reverse lookup by stagenum crosses the solo/arena index domains and can
	 * select a different catalog entry that happens to share a legacy number. */
	stage_id = g_MissionConfig.stage_id;
	stage_entry = stage_id ? assetCatalogResolve(stage_id) : NULL;
	if (!stage_entry || !stage_entry->occupied || !stage_entry->enabled
			|| stage_entry->type != ASSET_MAP
			|| strcmp(stage_entry->id, stage_id) != 0
			|| stage_entry->ext.map.stagenum != stagenum) {
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.ROLLBACK coop-stage reason=invalid_stage stagenum=0x%02x",
			(unsigned)stagenum);
		return -4;
	}

	for (s32 i = 0; i < g_NetMaxClients; i++) {
		struct netclient *client = &g_NetClients[i];
		state_before[i] = client->state;
		playernum_before[i] = client->playernum;
		if ((client->state != CLSTATE_PREPARING
				&& client->state != CLSTATE_GAME)
				|| (client->flags & CLFLAG_SPECTATOR)
				|| client->room_id != g_NetMatchRoomId) {
			continue;
		}
		if (roster_count >= 2 || roster_count >= MAX_PLAYERS) {
			sysLogPrintf(LOG_ERROR,
				"PLAYER.INIT.ROLLBACK coop-stage reason=roster_exceeds_two");
			return -5;
		}
		roster[roster_count++] = client;
	}
	if (roster_count == 0 || roster[0]->id != 0) {
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.ROLLBACK coop-stage reason=missing_wire_client_zero players=%u",
			(unsigned)roster_count);
		return -6;
	}
	if (g_NetGameMode == NETGAMEMODE_ANTI) {
		if (roster_count != 2 || g_NetCounterOpClientId == NET_NULL_CLIENT) {
			sysLogPrintf(LOG_ERROR,
				"PLAYER.INIT.ROLLBACK coop-stage reason=invalid_counterop_roster players=%u antiClient=%u",
				(unsigned)roster_count, (unsigned)g_NetCounterOpClientId);
			return -7;
		}
		for (u8 i = 0; i < roster_count; i++) {
			if (roster[i]->id == g_NetCounterOpClientId) {
				anti_playernum = i;
				break;
			}
		}
		if (anti_playernum < 0 || anti_playernum == 0) {
			sysLogPrintf(LOG_ERROR,
				"PLAYER.INIT.ROLLBACK coop-stage reason=anti_client_not_distinct id=%u",
				(unsigned)g_NetCounterOpClientId);
			return -8;
		}
	}

	mission_before = g_MissionConfig;
	bond_before = g_Vars.bondplayernum;
	coop_before = g_Vars.coopplayernum;
	anti_before = g_Vars.antiplayernum;
	match_seed_before = g_NetMatchSeed;
	stage_epoch_before = g_NetStageEpoch;

	g_MissionConfig.stagenum = stagenum;
	strncpy(g_MissionConfig.stage_id, stage_entry->id,
		sizeof(g_MissionConfig.stage_id) - 1);
	g_MissionConfig.stage_id[sizeof(g_MissionConfig.stage_id) - 1] = '\0';
	g_MissionConfig.difficulty = difficulty;
	g_MissionConfig.iscoop = g_NetGameMode == NETGAMEMODE_COOP;
	g_MissionConfig.isanti = g_NetGameMode == NETGAMEMODE_ANTI;
	g_Vars.bondplayernum = 0;
	g_Vars.coopplayernum = g_NetGameMode == NETGAMEMODE_COOP
		&& roster_count == 2 ? 1 : -1;
	g_Vars.antiplayernum = anti_playernum;

	for (u8 i = 0; i < roster_count; i++) {
		roster[i]->playernum = i;
		roster[i]->state = CLSTATE_GAME;
	}

	/* L2-4: Generate match seed for deterministic spawn pools */
	g_NetMatchSeed = (u32)(g_RngSeed ^ (g_RngSeed >> 32)) ^ g_NetTick;
	if (!netServerArmStageReplicationBarrier("coop-stage-start")) {
		for (s32 i = 0; i < g_NetMaxClients; i++) {
			g_NetClients[i].state = state_before[i];
			g_NetClients[i].playernum = playernum_before[i];
		}
		g_MissionConfig = mission_before;
		g_Vars.bondplayernum = bond_before;
		g_Vars.coopplayernum = coop_before;
		g_Vars.antiplayernum = anti_before;
		g_NetMatchSeed = match_seed_before;
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.ROLLBACK coop-stage reason=replication_lifecycle_rejected globals_restored=1");
		return -9;
	}

	/* Serialize only after the exact roster, stage, identities, and roles are
	 * committed.  On rejection restore the launch-visible state so the room can
	 * safely return to lobby instead of continuing with a partial mission. */
	netbufStartWrite(&g_NetMsgRel);
	if (netmsgServerStageStartWrite(&g_NetMsgRel,
			g_NetMatchRoomId) != 0) {
		for (s32 i = 0; i < g_NetMaxClients; i++) {
			g_NetClients[i].state = state_before[i];
			g_NetClients[i].playernum = playernum_before[i];
		}
		g_MissionConfig = mission_before;
		g_Vars.bondplayernum = bond_before;
		g_Vars.coopplayernum = coop_before;
		g_Vars.antiplayernum = anti_before;
		g_NetMatchSeed = match_seed_before;
		netbufStartWrite(&g_NetMsgRel);
		netServerRollbackStageReplicationBarrier(stage_epoch_before,
			"coop-stage-write");
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.ROLLBACK coop-stage reason=stage_write_or_authority_commit_rejected globals_restored=1");
		return -10;
	}
	{
		const u32 stage_wire_len = g_NetMsgRel.wp;
		if (netSendToRoom(g_NetMatchRoomId, &g_NetMsgRel, true,
				NETCHAN_DEFAULT) != stage_wire_len) {
			for (s32 i = 0; i < g_NetMaxClients; i++) {
				g_NetClients[i].state = state_before[i];
				g_NetClients[i].playernum = playernum_before[i];
			}
			g_MissionConfig = mission_before;
			g_Vars.bondplayernum = bond_before;
			g_Vars.coopplayernum = coop_before;
			g_Vars.antiplayernum = anti_before;
			g_NetMatchSeed = match_seed_before;
			netmsgCutsceneAuthorityReset();
			netServerRollbackStageReplicationBarrier(stage_epoch_before,
				"coop-stage-send");
			sysLogPrintf(LOG_ERROR,
				"PLAYER.INIT.ROLLBACK coop-stage reason=stage_send_rejected globals_restored=1");
			return -11;
		}
	}

	netServerClearPreservedPlayers("coop_stage_start");
	for (s32 i = 0; i < g_NetMaxClients; ++i) {
		if (g_NetClients[i].room_id == g_NetMatchRoomId) {
			g_NetClients[i].flags &= ~CLFLAG_COOPREADY;
		}
	}

	// Log character selections for each player
	for (s32 i = 0; i < g_NetMaxClients; ++i) {
		struct netclient *cl = &g_NetClients[i];
		if (cl->state >= CLSTATE_LOBBY
				&& cl->room_id == g_NetMatchRoomId) {
			sysLogPrintf(LOG_NOTE, "NET: player %d (%s) body='%s' head='%s'",
				i, cl->settings.name, cl->settings.body_id, cl->settings.head_id);
		}
	}

	sysLogPrintf(LOG_NOTE, "NET: starting co-op stage 0x%02x difficulty %u with %u players",
		stagenum, difficulty, roster_count);
	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.COMMIT coop-stage players=%u bond=%d coop=%d anti=%d",
		(unsigned)roster_count, g_Vars.bondplayernum,
		g_Vars.coopplayernum, g_Vars.antiplayernum);

	g_NotLoadMod = true;
	romdataFileFreeForSolo();

	// Schedule a full state resync shortly after stage start (NPCs + props)
	g_NetPendingResyncFlags = NET_RESYNC_FLAG_NPCS | NET_RESYNC_FLAG_PROPS;
	netPropSnapReset();

	// Start the mission on the server. The in-client server must close the
	// complete menu owner before stage load; a dedicated process has no menu.
	netServerPrepareInClientStageTransition("server stage start coop");
	titleSetNextStage(stagenum);
	setNumPlayers(roster_count);
	lvSetDifficulty(difficulty);
	titleSetNextMode(TITLEMODE_SKIP);
	mainChangeToStage(stagenum);

#if VERSION >= VERSION_NTSC_1_0
	viBlack(true);
#endif
	return 0;
}

static bool netServerFlushCutsceneAuthorityPacket(const char *reason,
		bool append_stage_end, u8 stage_end_room, u8 stage_end_mode)
{
	u8 data[NET_CUTSCENE_AUTHORITY_PACKET_CAPACITY + 2u];
	struct netbuf wire = { .data = data, .size = sizeof(data) };
	u8 room_id = 0xff;
	u32 event_count = 0;
	netbufStartWrite(&wire);
	if (netmsgServerPrepareCutsceneAuthorityPacket(&wire, &room_id,
			&event_count) != 0) {
		sysLogPrintf(LOG_ERROR,
			"NET: CUTSCENE.AUTHORITY packet preflight failed reason=%s",
			reason ? reason : "unspecified");
		return false;
	}
	if (event_count == 0 && !append_stage_end) {
		return true;
	}
	if (append_stage_end) {
		if (event_count > 0 && room_id != stage_end_room) {
			sysLogPrintf(LOG_ERROR,
				"NET: CUTSCENE.AUTHORITY stage-end room conflict authority=%u teardown=%u",
				(unsigned)room_id, (unsigned)stage_end_room);
			return false;
		}
		room_id = stage_end_room;
		if (netmsgSvcStageEndWrite(&wire, stage_end_room,
				stage_end_mode) != 0) {
			sysLogPrintf(LOG_ERROR,
				"NET: CUTSCENE.AUTHORITY stage-end packet preflight failed reason=%s",
				reason ? reason : "unspecified");
			return false;
		}
	}
	const u32 bytes = wire.wp;
	/* Destination scope remains the match room, deliberately including a
	 * same-room observer. The frozen participant mask inside SVC_CUTSCENE owns
	 * gameplay identity; unrelated lounge peers and probe endpoints never do. */
	if (netSendToRoom(room_id, &wire, true, NETCHAN_DEFAULT) != bytes) {
		sysLogPrintf(LOG_ERROR,
			"NET: CUTSCENE.AUTHORITY room send failed reason=%s room=%u events=%u; idempotent retry retained",
			reason ? reason : "unspecified", (unsigned)room_id,
			(unsigned)event_count);
		return false;
	}
	if (event_count > 0
			&& !netmsgServerCommitCutsceneAuthorityPacket(event_count)) {
		sysLogPrintf(LOG_ERROR,
			"NET: CUTSCENE.AUTHORITY commit failed after send reason=%s events=%u",
			reason ? reason : "unspecified", (unsigned)event_count);
		return false;
	}
	return true;
}

static bool netServerFlushCutsceneAuthority(const char *reason)
{
	return netServerFlushCutsceneAuthorityPacket(reason, false, 0xff,
		NETGAMEMODE_MP);
}

static void netServerCommitPendingStageEnd(void)
{
	const u8 room_id = s_NetStageEndPending.room_id;
	const u8 mode = s_NetStageEndPending.mode;

	playerResetAllCutsceneStates();
	netmsgCutsceneAuthorityReset();
	if (g_NetLocalClient) {
		g_NetLocalClient->state = CLSTATE_LOBBY;
	}
	netmsgSvcStageEndCommit(room_id, mode);
	netResetStageReplication("stage-end-commit", true);
	/* A stage boundary invalidates the old stage snapshot. Release every exact
	 * room/client reservation only after the terminal packet is published, so a
	 * failed END send remains retryable against the still-live match. */
	netServerClearPreservedPlayers("stage_end_commit");
	g_NetMatchRoomId = 0xFF;
	g_NetCounterOpClientId = NET_NULL_CLIENT;
	sessionCatalogTeardown();
	memset(&s_NetStageEndPending, 0, sizeof(s_NetStageEndPending));
	sysLogPrintf(LOG_NOTE,
		"NET: SVC_STAGE_END published and committed, returning to lobby");
}

static bool netServerFlushPendingStageEnd(const char *reason)
{
	if (!s_NetStageEndPending.active) {
		return true;
	}
	if (!netServerFlushCutsceneAuthorityPacket(reason, true,
			s_NetStageEndPending.room_id, s_NetStageEndPending.mode)) {
		return false;
	}
	netServerCommitPendingStageEnd();
	return true;
}

void netServerStageEnd(void)
{
	if (g_NetMode != NETMODE_SERVER) {
		return;
	}
	if (s_NetStageEndPending.active) {
		s_NetStageEndTerminalFrame = true;
		(void)netServerFlushPendingStageEnd("stage-end-repeat");
		return;
	}
	if (!netmsgCutsceneAuthorityHasMatch()) {
		sysLogPrintf(LOG_NOTE,
			"NET: SVC_STAGE_END ignored without active match");
		return;
	}

	/* U-10: disarm the stage-ready handshake for the next match. */
	g_NetStageReadyDeadline    = -1;
	g_NetBotAuthorityDelegated = false;
	g_NetBotAuthorityClientId  = NET_NULL_CLIENT;
	netResetStageReplication("stage-end-pending", false);

	sysLogPrintf(LOG_NOTE, "NET: === STAGE END === game mode=%u tick=%u", g_NetGameMode, g_NetTick);
	/* A stage boundary is the terminal authority transition. Preserve the same
	 * ordered START -> ACCEPT -> END stream even when gameplay ends the stage
	 * directly instead of reaching playerEndCutscene first. */
	if (netmsgCutsceneAuthorityIsActive()
			&& !netmsgServerQueueCutsceneState(0,
				playerCutsceneGeneration(), NULL)) {
		sysLogPrintf(LOG_ERROR,
			"NET: CUTSCENE.AUTHORITY stage-end preflight rejected generation=%u",
			playerCutsceneGeneration());
		return;
	}
	/* The terminal authority packet supersedes any unrelated gameplay traffic
	 * accumulated earlier in this frame. A failed terminal send must not be
	 * followed by stale shared-buffer state on the same channel. */
	netbufStartWrite(&g_NetMsg);
	netbufStartWrite(&g_NetMsgRel);
	s_NetStageEndTerminalFrame = true;
	s_NetStageEndPending.active = true;
	s_NetStageEndPending.room_id = g_NetMatchRoomId;
	s_NetStageEndPending.mode = g_NetGameMode;
	if (!netServerFlushPendingStageEnd("stage-end")) {
		sysLogPrintf(LOG_WARNING,
			"NET: SVC_STAGE_END delivery retained for idempotent retry room=%u mode=%u",
			(unsigned)s_NetStageEndPending.room_id,
			(unsigned)s_NetStageEndPending.mode);
	}
}

void netServerKick(struct netclient *cl, const u32 reason)
{
	if (g_NetMode != NETMODE_SERVER) {
		return;
	}

	if (!cl || !cl->state || !cl->peer) {
		return;
	}
	/* ENet's disconnect datum belongs to the remote notification.  Once that
	 * reliable command is acknowledged, ENet reports data=0 to this sender's
	 * local disconnect event.  Latch the authoritative policy before entering
	 * ENet so timeout preservation and terminal cleanup cannot disagree across
	 * the two peers.  First writer wins if multiple subsystems race to close. */
	if (cl->server_disconnect_intent_pending) {
		sysLogPrintf(LOG_WARNING,
			"NET.DISCONNECT.INTENT duplicate client=%u retained=%u ignored=%u",
			(unsigned)cl->id,
			(unsigned)cl->server_disconnect_intent_reason,
			(unsigned)reason);
		return;
	}
	cl->server_disconnect_intent_reason = reason;
	cl->server_disconnect_intent_pending = true;
	sysLogPrintf(LOG_NOTE,
		"NET.DISCONNECT.INTENT latch client=%u reason=%u retryable=%d",
		(unsigned)cl->id, (unsigned)reason,
		netReconnectReasonIsRetryable(reason, DISCONNECT_TIMEOUT) != 0);

	enet_peer_disconnect(cl->peer, reason);
}

s32 netStartClient(const char *addr)
{
	u32 connect_data;
	s32 reconnecting;

	if (g_NetMode || !g_NetInit) {
		return -1;
	}

	if (!netParseAddr(&g_NetRemoteAddr, addr)) {
		sysLogPrintf(LOG_ERROR, "NET: `%s` is not a valid address", addr);
		return -2;
	}
	connect_data = netmsgClcAuthPrepareConnect(&g_NetRemoteAddr,
		NET_PROTOCOL_VER);
	if (connect_data == 0) {
		sysLogPrintf(LOG_ERROR,
			"NET: could not prepare authenticated connect data for %s", addr);
		return -3;
	}
	reconnecting = (connect_data & NET_RECONNECT_CONNECT_FLAG) != 0;

	memset(&g_NetLocalAddr, 0, sizeof(g_NetLocalAddr));
	g_NetHost = enet_host_create(&g_NetLocalAddr, 1, NETCHAN_COUNT, g_NetClientInRate, g_NetClientOutRate, 0);
	if (!g_NetHost) {
		sysLogPrintf(LOG_ERROR, "NET: could not create ENet host");
		return -3;
	}
	netUploadMeasurementReset();

	enet_host_set_intercept_callback(g_NetHost, NULL);

	// save the address since it appears to be valid
	strncpy(g_NetLastJoinAddr, addr, NET_MAX_ADDR - 1);
	g_NetLastJoinAddr[NET_MAX_ADDR - 1] = '\0';
	netRecentServerAdd(addr);

	// we'll use the whole array to store what we know of other clients
	netClientResetAll();

	// for now use last client struct
	g_NetLocalClient = &g_NetClients[NET_MAX_CLIENTS];

	sysLogPrintf(LOG_NOTE, "NET: using protocol version %d", NET_PROTOCOL_VER);
	sysLogPrintf(LOG_NOTE, "NET: connecting to %s...", addr);

	g_NetLocalClient->peer = enet_host_connect(g_NetHost, &g_NetRemoteAddr,
		NETCHAN_COUNT, connect_data);
	if (!g_NetLocalClient->peer) {
		sysLogPrintf(LOG_WARNING, "NET: could not connect to %s", addr);
		enet_host_destroy(g_NetHost);
		g_NetHost = NULL;
		return -4;
	}

	g_NetLocalClient->state = CLSTATE_CONNECTING;
	netClientReadConfig(g_NetLocalClient, 0);
	if (reconnecting && s_NetReconnectAttempt.valid) {
		g_NetLocalClient->settings = s_NetReconnectAttempt.settings;
		sysLogPrintf(LOG_NOTE,
			"NET.RECONNECT.PREPARE endpoint_match=1 client_settings_frozen=1");
	}

	g_NetMode = NETMODE_CLIENT;
	netResetStageReplication("client-start", true);

	g_NetTick = 0;
	g_NetNextUpdate = 0;
	g_NetNextSyncId = 1;
	g_NetFirstDynamicSyncId = 1;
	netSyncIdMapClear(); // ensure map is empty from any previous session

	sysLogPrintf(LOG_NOTE, "NET: waiting for response from %s...", addr);

	lobbyInit();
	/* B-1037: distribution is a two-sided protocol.  The server has always
	 * initialized its queue before advertising the catalog, but a joining
	 * client also owns receive slots, transfer-set state, and trust policy.
	 * Initialize that state for every client session before the first ENet
	 * event can deliver SVC_CATALOG_INFO or SVC_DISTRIB_BEGIN. */
	netDistribInit();

	return 0;
}

static s32 netDisconnectWithIntent(s32 retain_reconnect)
{
	if (!g_NetMode) {
		return -1;
	}

	const bool was_client = g_NetMode == NETMODE_CLIENT;
	const bool wasingame = g_NetLocalClient
		&& g_NetLocalClient->state == CLSTATE_GAME;
	const bool reconnect_in_flight = s_NetReconnectAttempt.valid;
	const bool retain_credential = retain_reconnect && was_client
		&& (wasingame || reconnect_in_flight)
		&& netmsgClcAuthReconnectAvailable(&g_NetRemoteAddr);
	if (retain_credential) {
		/* A first live timeout freezes the exact prior settings/address. A
		 * second timeout during auth, manifest transfer, or stage publication
		 * keeps that original candidate intact instead of clearing the only
		 * credential capable of reclaiming the reserved server slot. */
		if (!reconnect_in_flight) {
			s_NetReconnectAttempt.valid = true;
			s_NetReconnectAttempt.auth_accepted = false;
			s_NetReconnectAttempt.settings = g_NetLocalClient->settings;
			strncpy(s_NetReconnectAttempt.addr, g_NetLastJoinAddr,
				sizeof(s_NetReconnectAttempt.addr) - 1);
			s_NetReconnectAttempt.addr[
				sizeof(s_NetReconnectAttempt.addr) - 1] = '\0';
		}
	} else {
		memset(&s_NetReconnectAttempt, 0, sizeof(s_NetReconnectAttempt));
		netmsgClcAuthClearCookie();
	}

	matchConfigRestoreUserOptions("netDisconnect");
	if (g_NetMode == NETMODE_SERVER) {
		netServerClearPreservedPlayers("server_disconnect");
	}

	// stop responding to connectionless packets
	enet_host_set_intercept_callback(g_NetHost, NULL);

	for (s32 i = 0; i < NET_MAX_CLIENTS + 1; ++i) {
		if (g_NetClients[i].peer) {
			enet_peer_disconnect_now(g_NetClients[i].peer, DISCONNECT_SHUTDOWN);
		}
		netClientReset(&g_NetClients[i]);
	}

	g_NetLocalClient = &g_NetClients[NET_MAX_CLIENTS];
	memset(&s_NetStageEndPending, 0, sizeof(s_NetStageEndPending));
	s_NetStageEndTerminalFrame = false;
	netResetStageReplication("disconnect", true);
	netmsgCutsceneAuthorityReset();
	playerResetAllCutsceneStates();

	// flush pending packets
	enet_host_flush(g_NetHost);
	netUploadMeasurementTick();

	// service for a bit just to ensure disconnect gets to peer(s)
	enet_host_service(g_NetHost, NULL, 10);

	enet_host_destroy(g_NetHost);

	/* Clean up UPnP port mapping if we were the server */
	netUpnpTeardown();

	/* Cancel any in-progress STUN discovery */
	stunCancel();

	/* Reset hole punch waterfall state */
	netHolePunchReset();

	g_NetHost = NULL;
	netUploadMeasurementReset();
	g_NetMode = NETMODE_NONE;
	g_NetGameMode = NETGAMEMODE_MP;
	g_NetLocalBotAuthority = false;
	g_NetPendingBotAuthority = false;
	g_NetBotAuthorityClientId = NET_NULL_CLIENT;

#if !defined(PD_SERVER)
	/* D-003: release the per-match authority latch only after ENet ownership
	 * is fully gone. Presence must not keep advertising a stopped server. */
	groupSessionOnTransportDisconnected();
#endif

	/* v57: only a transport timeout from a live client retains the endpoint-
	 * scoped credential captured before reset. Every intentional or policy
	 * disconnect cleared it before transport teardown. */
	sysLogPrintf(LOG_NOTE,
		"NET.RECONNECT.TEARDOWN retryable=%d credential_retained=%d",
		retain_reconnect != 0, retain_credential != 0);

	/* S300 / SP-14: reset room-scoped state so a stale room id doesn't survive
	 * into the next hosting/client session. Server-side disconnect during a
	 * live match previously left g_NetMatchRoomId stuck until the next ready
	 * gate overwrote it; client-side value was already 0xFF via
	 * netmsgSvcStageEndRead, but if disconnect happened BEFORE SVC_STAGE_END
	 * arrived the client would also see a non-sentinel value. Same story for
	 * g_NetCounterOpClientId (co-op / anti role identity). Log the prior
	 * value when non-trivial so we can diagnose any unexpected leaks. */
	if (g_NetMatchRoomId != 0xFF || g_NetCounterOpClientId != NET_NULL_CLIENT) {
		sysLogPrintf(LOG_NOTE,
			"NET: disconnect-time reset — g_NetMatchRoomId %u -> 0xFF, g_NetCounterOpClientId %u -> NET_NULL_CLIENT",
			(unsigned)g_NetMatchRoomId, (unsigned)g_NetCounterOpClientId);
	}
	g_NetMatchRoomId = 0xFF;
	g_NetCounterOpClientId = NET_NULL_CLIENT;

	sysLogPrintf(LOG_CHAT, "NET: disconnected");

	sceneStageTransitionPrepare(
		SCENE_STAGE_TRANSITION_DISCONNECT |
		SCENE_STAGE_TRANSITION_RELEASE_MENU_POOL,
		"netDisconnect");

	if (wasingame && !g_AppQuitting) {
		// skip the "want to save" dialog for all players
		for (s32 i = 0; i < MAX_PLAYERS; ++i) {
			if (g_Vars.players[i]) {
				g_PlayerConfigsArray[i].options |= OPTION_ASKEDSAVEPLAYER;
			}
		}
		// end the stage immediately
		mainEndStage();
		// try to drop back to main menu with 1 player
		mpSetPaused(MPPAUSEMODE_UNPAUSED);
		/* B-12 Phase 3: reset the participant pool to one local player. */
		mpParticipantPoolInit(MAX_MPCHRS);
		mpAddParticipantAt(0, PARTICIPANT_LOCAL, 0, 0, 0);
		g_Vars.mplayerisrunning = false;
		g_Vars.normmplayerisrunning = false;
		g_Vars.lvmpbotlevel = 0;
		titleSetNextStage(STAGE_CITRAINING);
		setNumPlayers(1);
		titleSetNextMode(TITLEMODE_SKIP);
		/* B-End-Game-Crash: Clear the stale MP client manifest BEFORE the stage
		 * change.  mainChangeToStage(STAGE_CITRAINING) treats CITRAINING as a
		 * gameplay stage; if g_ClientManifest still has the MP match's entries
		 * at this point, mainChangeToStage takes the manifestMPTransition()
		 * branch and attempts to diff the torn-down MP manifest against
		 * whatever is loading for CI training, leading to an access violation
		 * during the manifest apply.  Same fix pattern as F-0.4 in
		 * pdguiEndscreenExitToMainMenu and L1-1 in netmsgSvcStageEndRead. */
		sceneStageChangeTo(STAGE_CITRAINING,
			SCENE_STAGE_TRANSITION_CLEAR_CLIENT_MANIFEST,
			"netDisconnect lobby return");
	}

	return 0;
}

s32 netDisconnect(void)
{
	return netDisconnectWithIntent(false);
}

s32 netClientReconnectAvailable(void)
{
	ENetAddress endpoint;

	if (g_NetMode != NETMODE_NONE || !s_NetReconnectAttempt.valid
			|| !s_NetReconnectAttempt.addr[0]
			|| !netParseAddr(&endpoint, s_NetReconnectAttempt.addr)) {
		return false;
	}
	return netmsgClcAuthReconnectAvailable(&endpoint);
}

s32 netClientReconnect(void)
{
	char addr[NET_MAX_ADDR + 1];

	if (!netClientReconnectAvailable()) {
		return -1;
	}
	memcpy(addr, s_NetReconnectAttempt.addr, sizeof(addr));
	return netStartClient(addr);
}

void netClientReconnectAuthAccepted(void)
{
	if (s_NetReconnectAttempt.valid) {
		s_NetReconnectAttempt.auth_accepted = true;
		sysLogPrintf(LOG_NOTE,
			"NET.RECONNECT.CLIENT auth=accepted retry_transaction_retained_until_server_commit=1");
	}

}

void netClientReconnectCommitAccepted(void)
{
	if (s_NetReconnectAttempt.valid) {
		if (!s_NetReconnectAttempt.auth_accepted) {
			sysLogPrintf(LOG_ERROR,
				"NET.RECONNECT.CLIENT commit=rejected auth=missing retry_transaction=retained");
			return;
		}
		sysLogPrintf(LOG_NOTE,
			"NET.RECONNECT.CLIENT transaction=complete post_load_ready=1 server_commit_ack=1 retry_transaction=retired");
		memset(&s_NetReconnectAttempt, 0, sizeof(s_NetReconnectAttempt));
	}
}

void netLocalStageLoaded(void)
{
	u8 payload[8];
	struct netbuf wire;
	u32 bytes;
	bool already_ready;

	if (g_NetMode == NETMODE_SERVER) {
		if (s_NetStageReplicationPhase != NET_STAGE_REPLICATION_WAITING
				&& s_NetStageReplicationPhase != NET_STAGE_REPLICATION_RELEASE) {
			return;
		}
		already_ready = s_NetStageAuthorityReady;
		s_NetStageAuthorityReady = true;
		if (g_NetLocalClient) {
			g_NetLocalClient->stage_ready = true;
		}
		sysLogPrintf(LOG_NOTE,
			"NET.STAGE.REPLICATION ready authority=server epoch=%u duplicate=%u post_load=1 stage=0x%02x",
			(unsigned)g_NetStageEpoch, already_ready ? 1u : 0u,
			(unsigned)g_StageNum);
		return;
	}

	if (g_NetMode != NETMODE_CLIENT || !g_NetLocalClient
			|| g_NetLocalClient->state != CLSTATE_GAME
			|| !g_NetLocalClient->peer || g_NetStageEpoch == 0
			|| g_NetLocalClient->stage_ready) {
		return;
	}

	memset(&wire, 0, sizeof(wire));
	wire.data = payload;
	wire.size = sizeof(payload);
	netbufStartWrite(&wire);
	if (netmsgClcStageReadyWrite(&wire) != 0 || wire.wp == 0) {
		sysLogPrintf(LOG_ERROR,
			"NET: CLC_STAGE_READY post-load serialization failed");
		enet_peer_disconnect(g_NetLocalClient->peer, DISCONNECT_TIMEOUT);
		return;
	}
	bytes = wire.wp;
	if (netSend(g_NetLocalClient, &wire, true, NETCHAN_DEFAULT) != bytes) {
		sysLogPrintf(LOG_ERROR,
			"NET: CLC_STAGE_READY post-load enqueue failed");
		enet_peer_disconnect(g_NetLocalClient->peer, DISCONNECT_TIMEOUT);
		return;
	}

	g_NetLocalClient->stage_ready = true;
	sysLogPrintf(LOG_NOTE,
		"NET: sent CLC_STAGE_READY epoch=%u post_load=1 stage=0x%02x",
		(unsigned)g_NetStageEpoch, (unsigned)g_StageNum);
}

/* Fill buf with len bytes from the OS CSPRNG (BCryptGenRandom / getrandom /
 * getentropy / /dev/urandom).  Called from the network thread / main loop only
 * (AUDIT-M3: no mutex — single-threaded net tick contract).  Returns 0 on success. */
static int netOsRandomBytes(u8 *buf, size_t len)
{
#if defined(_WIN32)
	NTSTATUS st = BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	return BCRYPT_SUCCESS(st) ? 0 : -1;
#elif defined(__linux__)
	size_t off = 0;
	while (off < len) {
		ssize_t r = getrandom(buf + off, len - off, 0);
		if (r < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		off += (size_t)r;
	}
	return 0;
#elif defined(__APPLE__)
	if (len > 256u) {
		return -1;
	}
	if (getentropy(buf, len) != 0) {
		return -1;
	}
	return 0;
#else
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) {
		return -1;
	}
	size_t off = 0;
	while (off < len) {
		ssize_t r = read(fd, buf + off, len - off);
		if (r < 0) {
			if (errno == EINTR) {
				continue;
			}
			close(fd);
			return -1;
		}
		if (r == 0) {
			close(fd);
			return -1;
		}
		off += (size_t)r;
	}
	close(fd);
	return 0;
#endif
}

/* MASTER-C3: Issue a fresh 128-bit identity cookie from the OS RNG. */
void netServerIssueCookie(u8 out[NET_AUTH_COOKIE_LEN])
{
	if (netOsRandomBytes(out, NET_AUTH_COOKIE_LEN) != 0) {
		sysLogPrintf(LOG_WARNING, "NET: netOsRandomBytes failed; mixing SHA-256 fallback");
		static sha256_ctx s_Ctx;
		static bool s_Seeded = false;
		if (!s_Seeded) {
			sha256Init(&s_Ctx);
			u64 seed = (u64)time(NULL) ^ (u64)SDL_GetPerformanceCounter();
			sha256Update(&s_Ctx, &seed, sizeof(seed));
			uintptr_t addr = (uintptr_t)&netServerIssueCookie;
			sha256Update(&s_Ctx, &addr, sizeof(addr));
			s_Seeded = true;
		}
		u64 salt = (u64)SDL_GetPerformanceCounter();
		sha256Update(&s_Ctx, &salt, sizeof(salt));
		u32 tick = g_NetTick;
		sha256Update(&s_Ctx, &tick, sizeof(tick));
		u8 digest[SHA256_DIGEST_SIZE];
		sha256_ctx snapshot = s_Ctx;
		sha256Final(&snapshot, digest);
		sha256Update(&s_Ctx, digest, sizeof(digest));
		memcpy(out, digest, NET_AUTH_COOKIE_LEN);
	}
}

void netServerPreservePlayer(struct netclient *cl)
{
	struct netpreservedplayer candidate;
	struct netpreservedplayer *pp;
	bool replacing;

	if (!cl || cl->id >= NET_MAX_CLIENTS || !cl->settings.name[0]
			|| cl->playernum >= MAX_PLAYERS || !cl->config || !cl->player
			|| !cl->player->prop || cl->player->prop->syncid == NET_NULL_PROP) {
		sysLogPrintf(LOG_WARNING,
			"NET.RECONNECT.PRESERVE rejected client=%d player=%d config=%d player_object=%d prop=%d",
			cl ? (s32)cl->id : -1, cl ? (s32)cl->playernum : -1,
			cl && cl->config, cl && cl->player,
			cl && cl->player && cl->player->prop);
		return;
	}

	/* Stable client id, not display name, owns the preservation record. */
	pp = netServerFindPreservedByClientId((u8)cl->id);
	if (!pp) {
		for (s32 i = 0; i < NET_MAX_CLIENTS; ++i) {
			if (!g_NetPreservedPlayers[i].active) {
				pp = &g_NetPreservedPlayers[i];
				break;
			}
		}
	}

	if (!pp) {
		sysLogPrintf(LOG_WARNING, "NET: no room to preserve player %s", cl->settings.name);
		return;
	}

	replacing = pp->active;
	memset(&candidate, 0, sizeof(candidate));
	strncpy(candidate.name, cl->settings.name, sizeof(candidate.name) - 1);
	candidate.name[sizeof(candidate.name) - 1] = '\0';
	memcpy(candidate.cookie, cl->auth_cookie, sizeof(candidate.cookie));
	candidate.client_id = (u8)cl->id;
	candidate.playernum = cl->playernum;
	candidate.room_id = cl->room_id;
	candidate.prop_syncid = cl->player->prop->syncid;
	candidate.settings = cl->settings;
	candidate.config = g_PlayerConfigsArray[cl->playernum];
	candidate.config.client = NULL;
	candidate.preserveframe = g_NetTick;
	/* Publish active last so readers never observe a partial snapshot. */
	*pp = candidate;
	pp->active = true;
	if (!replacing) {
		++g_NetNumPreserved;
	}

	sysLogPrintf(LOG_NOTE,
		"NET.RECONNECT.PRESERVE client=%u player=%u room=%u prop=%u team=%u body='%s' head='%s' points=%d deaths=%d snapshot=complete",
		(unsigned)pp->client_id, (unsigned)pp->playernum,
		(unsigned)pp->room_id, (unsigned)pp->prop_syncid,
		(unsigned)pp->settings.team, pp->settings.body_id,
		pp->settings.head_id, (int)pp->config.base.numpoints,
		(int)pp->config.base.numdeaths);
}

static void netServerDiscardPreservedPlayer(
		struct netpreservedplayer *pp, const char *reason)
{
	u8 client_id;
	u8 room_id;
	u8 preserved_index;
	hub_room_t *room;

	if (!pp || !pp->active) {
		return;
	}
	client_id = pp->client_id;
	room_id = pp->room_id;
	preserved_index = (u8)(pp - g_NetPreservedPlayers);

	/* Stop an authenticated peer that is still acquiring the manifest for this
	 * exact record. Once the record is gone, allowing PREPARING to linger could
	 * only produce an unfinishable transaction. */
	if (client_id < NET_MAX_CLIENTS) {
		struct netclient *pending = &g_NetClients[client_id];
		if (pending->peer
				&& pending->reconnect_preserved_index == preserved_index) {
			netServerKick(pending, DISCONNECT_LATE);
		}
	}

	if (room_id != 0xFF) {
		room = roomGetById(room_id);
		if (room) {
			roomReleaseReconnectReservation(room, client_id);
		}
	}
	memset(pp, 0, sizeof(*pp));
	if (g_NetNumPreserved > 0) {
		--g_NetNumPreserved;
	}
	sysLogPrintf(LOG_NOTE,
		"NET.RECONNECT.RESERVATION released client=%u room=%u reason=%s remaining=%d",
		(unsigned)client_id, (unsigned)room_id,
		reason ? reason : "unspecified", g_NetNumPreserved);
}

static void netServerClearPreservedPlayers(const char *reason)
{
	s32 released = 0;

	for (s32 i = 0; i < NET_MAX_CLIENTS; ++i) {
		if (g_NetPreservedPlayers[i].active) {
			netServerDiscardPreservedPlayer(&g_NetPreservedPlayers[i], reason);
			++released;
		}
	}
	/* Also erase inactive historical bytes and repair any stale count. */
	memset(g_NetPreservedPlayers, 0, sizeof(g_NetPreservedPlayers));
	g_NetNumPreserved = 0;
	if (released > 0) {
		netRoomListMarkDirty();
		sysLogPrintf(LOG_NOTE,
			"NET.RECONNECT.RESERVATION cleared count=%d reason=%s",
			released, reason ? reason : "unspecified");
	}
}

static void netServerExpirePreservedPlayers(void)
{
	if (g_NetNumPreserved <= 0 || (g_NetTick % 600) != 0) {
		return;
	}

	for (s32 i = 0; i < NET_MAX_CLIENTS; ++i) {
		struct netpreservedplayer *pp = &g_NetPreservedPlayers[i];
		if (!pp->active
				|| (g_NetTick - pp->preserveframe)
					<= NET_PRESERVE_TIMEOUT_FRAMES) {
			continue;
		}

		sysLogPrintf(LOG_NOTE,
			"NET: preserved player %s timed out room=%u client=%u",
			pp->name, (unsigned)pp->room_id, (unsigned)pp->client_id);
		netServerDiscardPreservedPlayer(pp, "timeout");
	}
}

struct netpreservedplayer *netServerFindPreservedByClientId(u8 client_id)
{
	if (client_id >= NET_NULL_CLIENT) {
		return NULL;
	}
	for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
		if (g_NetPreservedPlayers[i].active
				&& g_NetPreservedPlayers[i].client_id == client_id) {
			return &g_NetPreservedPlayers[i];
		}
	}
	return NULL;
}

s32 netServerMatchInProgress(void)
{
	if (g_NetMatchRoomId != 0xFF) {
		hub_room_t *room = roomGetById(g_NetMatchRoomId);
		if (room && (room->state == ROOM_STATE_LOADING
				|| room->state == ROOM_STATE_MATCH)) {
			return true;
		}
	}
	for (s32 i = 0; i < g_NetMaxClients; i++) {
		if (g_NetClients[i].state == CLSTATE_GAME) {
			return true;
		}
	}
	return false;
}

const char *netRestoreResultString(enum net_restore_result result)
{
	switch (result) {
	case NET_RESTORE_OK: return "ok";
	case NET_RESTORE_INVALID_ARGUMENT: return "invalid_argument";
	case NET_RESTORE_INACTIVE_RECORD: return "inactive_record";
	case NET_RESTORE_INVALID_PLAYER_SLOT: return "invalid_player_slot";
	case NET_RESTORE_MISSING_PLAYER: return "missing_player";
	case NET_RESTORE_SLOT_CONFLICT: return "slot_conflict";
	case NET_RESTORE_INVALID_IDENTITY: return "invalid_identity";
	case NET_RESTORE_SETTINGS_MISMATCH: return "settings_mismatch";
	case NET_RESTORE_ROOM_UNAVAILABLE: return "room_unavailable";
	case NET_RESTORE_MISSING_PROP: return "missing_prop";
	case NET_RESTORE_STAGE_WRITE_FAILED: return "stage_write_failed";
	case NET_RESTORE_STAGE_SEND_FAILED: return "stage_send_failed";
	case NET_RESTORE_PROP_MISMATCH: return "prop_mismatch";
	case NET_RESTORE_STATE_WRITE_FAILED: return "state_write_failed";
	case NET_RESTORE_STATE_SEND_FAILED: return "state_send_failed";
	default: return "unknown";
	}
}

s32 netRestoreResultRequiresRetryableClose(enum net_restore_result result)
{
	return result == NET_RESTORE_STAGE_WRITE_FAILED
		|| result == NET_RESTORE_STAGE_SEND_FAILED
		|| result == NET_RESTORE_STATE_WRITE_FAILED
		|| result == NET_RESTORE_STATE_SEND_FAILED;
}

static enum net_restore_result netServerRestoreReject(
		enum net_restore_result result, const struct netclient *cl,
		const struct netpreservedplayer *pp,
		player_identity_status_e identity_status)
{
	sysLogPrintf(LOG_ERROR,
		"PLAYER.INIT.ROLLBACK reconnect status=%s client=%d player=%d identity=%s globals_published=0",
		netRestoreResultString(result), cl ? (s32)cl->id : -1,
		pp ? (s32)pp->playernum : -1,
		playerIdentityStatusString(identity_status));
	return result;
}

s32 netServerReconnectSettingsMatch(
		const struct netpreservedplayer *pp,
		const net_client_settings_plan_t *plan, u8 sanitized_team)
{
	return pp && plan
		&& plan->options == pp->settings.options
		&& strcmp(plan->identity.body_id, pp->settings.body_id) == 0
		&& strcmp(plan->identity.head_id, pp->settings.head_id) == 0
		&& plan->team == pp->settings.team
		&& sanitized_team == pp->settings.team
		&& plan->handicap == pp->settings.handicap
		&& plan->fovy == pp->settings.fovy
		&& plan->fovzoommult == pp->settings.fovzoommult
		&& strcmp(plan->name, pp->settings.name) == 0;
}

struct net_restore_candidate {
	struct netpreservedplayer *preserved;
	struct player *player;
	struct mpplayerconfig config;
	player_identity_plan_t identity;
	player_identity_status_e identity_status;
	hub_room_t *room;
};

static enum net_restore_result netServerPrepareRestoreCandidate(
		struct netclient *cl, struct netpreservedplayer *pp,
		const net_client_settings_plan_t *settings_plan, u8 sanitized_team,
		struct net_restore_candidate *out)
{
	if (!cl || !cl->peer || !pp || !settings_plan || !out
			|| cl->id >= NET_MAX_CLIENTS
			|| cl->state != CLSTATE_PREPARING
			|| !(cl->flags & CLFLAG_ABSENT)
			|| !cl->reconnect_settings_pending
			|| cl->reconnect_preserved_index >= NET_MAX_CLIENTS
			|| pp != &g_NetPreservedPlayers[cl->reconnect_preserved_index]
			|| pp->client_id != cl->id) {
		return netServerRestoreReject(NET_RESTORE_INVALID_ARGUMENT, cl, pp,
			PLAYER_IDENTITY_INVALID_ARGUMENT);
	}
	memset(out, 0, sizeof(*out));
	out->preserved = pp;
	if (!pp->active) {
		return netServerRestoreReject(NET_RESTORE_INACTIVE_RECORD, cl, pp,
			PLAYER_IDENTITY_INVALID_ARGUMENT);
	}
	if (pp->playernum >= MAX_PLAYERS) {
		return netServerRestoreReject(NET_RESTORE_INVALID_PLAYER_SLOT, cl, pp,
			PLAYER_IDENTITY_INVALID_ARGUMENT);
	}
	if (!netServerReconnectSettingsMatch(pp, settings_plan, sanitized_team)) {
		return netServerRestoreReject(NET_RESTORE_SETTINGS_MISMATCH, cl, pp,
			PLAYER_IDENTITY_OK);
	}

	out->identity_status = playerIdentityPrepare(pp->settings.body_id,
		pp->settings.head_id, &out->identity);
	if (out->identity_status != PLAYER_IDENTITY_OK
			|| strcmp(pp->config.base.body_id, out->identity.body_id) != 0
			|| strcmp(pp->config.base.head_id, out->identity.head_id) != 0
			|| pp->config.base.mpbodynum != (out->identity.mp_body_index >= 0
				? (u8)out->identity.mp_body_index : 0xFF)
			|| pp->config.base.mpheadnum != (out->identity.mp_head_index >= 0
				? (u8)out->identity.mp_head_index : 0xFF)) {
		return netServerRestoreReject(NET_RESTORE_INVALID_IDENTITY, cl, pp,
			out->identity_status);
	}

	out->player = g_Vars.players[pp->playernum];
	if (!out->player) {
		return netServerRestoreReject(NET_RESTORE_MISSING_PLAYER, cl, pp,
			out->identity_status);
	}
	if (!out->player->prop) {
		return netServerRestoreReject(NET_RESTORE_MISSING_PROP, cl, pp,
			out->identity_status);
	}
	if (out->player->prop->syncid != pp->prop_syncid) {
		return netServerRestoreReject(NET_RESTORE_PROP_MISMATCH, cl, pp,
			out->identity_status);
	}

	for (s32 i = 0; i < g_NetMaxClients; i++) {
		struct netclient *other = &g_NetClients[i];
		if (other != cl && other->state == CLSTATE_GAME
				&& !(other->flags & CLFLAG_ABSENT)
				&& other->playernum == pp->playernum) {
			return netServerRestoreReject(NET_RESTORE_SLOT_CONFLICT, cl, pp,
				out->identity_status);
		}
	}

	if (pp->room_id != 0xFF) {
		out->room = roomGetById(pp->room_id);
		if (!out->room || !roomCanRejoin(out->room, (u8)cl->id)) {
			return netServerRestoreReject(NET_RESTORE_ROOM_UNAVAILABLE, cl, pp,
				out->identity_status);
		}
	}
	out->config = pp->config;
	out->config.client = cl;
	return NET_RESTORE_OK;
}

enum net_restore_result netServerRestorePreserved(struct netclient *cl,
		struct netpreservedplayer *pp,
		const net_client_settings_plan_t *settings_plan,
		u8 sanitized_team,
		struct netbuf *stage_wire,
		u32 *out_stage_len)
{
	struct net_restore_candidate candidate;
	struct mpplayerconfig config_before;
	struct netclientsettings settings_before;
	struct mpplayerconfig *client_config_before;
	struct player *client_player_before;
	struct netclient *player_client_before;
	u32 client_state_before;
	u32 client_flags_before;
	u8 client_playernum_before;
	u8 client_room_before;
	u8 player_newlife_before;
	bool player_remote_before;
	u8 auth_cookie_before[NET_AUTH_COOKIE_LEN];
	u32 stage_len;
	enum net_restore_result result;

	if (!stage_wire || !stage_wire->data || !out_stage_len
			|| stage_wire->size == 0 || (cl && cl->reconnect_resync_pending)) {
		return netServerRestoreReject(NET_RESTORE_INVALID_ARGUMENT, cl, pp,
			PLAYER_IDENTITY_INVALID_ARGUMENT);
	}
	*out_stage_len = 0;
	result = netServerPrepareRestoreCandidate(cl, pp, settings_plan,
		sanitized_team, &candidate);
	if (result != NET_RESTORE_OK) {
		return result;
	}

	config_before = g_PlayerConfigsArray[pp->playernum];
	settings_before = cl->settings;
	client_config_before = cl->config;
	client_player_before = cl->player;
	client_playernum_before = cl->playernum;
	client_room_before = cl->room_id;
	client_state_before = cl->state;
	client_flags_before = cl->flags;
	memcpy(auth_cookie_before, cl->auth_cookie, sizeof(auth_cookie_before));
	player_client_before = candidate.player->client;
	player_remote_before = candidate.player->isremote;
	player_newlife_before = candidate.player->dostartnewlife;

	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.PREFLIGHT reconnect client=%u player=%u room=%u prop=%u body='%s' head='%s' exact_settings=1 reservation_retained=1",
		(unsigned)cl->id, (unsigned)pp->playernum, (unsigned)pp->room_id,
		(unsigned)pp->prop_syncid, candidate.identity.body_id,
		candidate.identity.head_id);

	/* Publish only long enough to serialize the exact immutable roster. Room
	 * membership, preserved identity, and every gameplay linkage remain
	 * uncommitted until the receiver reports its real post-load boundary. */
	g_PlayerConfigsArray[pp->playernum] = candidate.config;
	cl->settings = pp->settings;
	cl->playernum = pp->playernum;
	cl->room_id = pp->room_id;
	cl->config = &g_PlayerConfigsArray[pp->playernum];
	cl->player = candidate.player;
	candidate.player->client = cl;
	candidate.player->isremote = true;
	cl->state = CLSTATE_GAME;
	cl->flags &= ~CLFLAG_ABSENT;
	memcpy(cl->auth_cookie, pp->cookie, sizeof(cl->auth_cookie));

	netbufStartWrite(stage_wire);
	if (netmsgSvcStageReplayWrite(stage_wire) != 0 || stage_wire->wp == 0) {
		result = NET_RESTORE_STAGE_WRITE_FAILED;
		goto restore_temporary_publication;
	}
	stage_len = stage_wire->wp;
	if (netSend(cl, stage_wire, true, NETCHAN_DEFAULT) != stage_len) {
		result = NET_RESTORE_STAGE_SEND_FAILED;
		goto restore_temporary_publication;
	}
	result = NET_RESTORE_OK;

restore_temporary_publication:
	g_PlayerConfigsArray[pp->playernum] = config_before;
	cl->settings = settings_before;
	cl->config = client_config_before;
	cl->player = client_player_before;
	cl->playernum = client_playernum_before;
	cl->room_id = client_room_before;
	cl->state = client_state_before;
	cl->flags = client_flags_before;
	memcpy(cl->auth_cookie, auth_cookie_before, sizeof(auth_cookie_before));
	candidate.player->client = player_client_before;
	candidate.player->isremote = player_remote_before;
	candidate.player->dostartnewlife = player_newlife_before;

	if (result != NET_RESTORE_OK) {
		netbufStartWrite(stage_wire);
		return netServerRestoreReject(result, cl, pp,
			candidate.identity_status);
	}

	cl->stage_ready = false;
	cl->reconnect_resync_pending = true;
	cl->reconnect_gameplay_witness_pending = false;
	*out_stage_len = stage_len;
	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.PREPARED reconnect client=%u player=%u room=%u prop=%u stage_bytes=%u reservation_retained=1 commit=pending_post_load",
		(unsigned)cl->id, (unsigned)pp->playernum, (unsigned)pp->room_id,
		(unsigned)pp->prop_syncid, (unsigned)stage_len);
	return NET_RESTORE_OK;
}

enum net_restore_result netServerCompleteReconnect(struct netclient *cl)
{
	struct net_restore_candidate candidate;
	struct netpreservedplayer *pp;
	struct mpplayerconfig config_before;
	struct netclientsettings settings_before;
	struct mpplayerconfig *client_config_before;
	struct player *client_player_before;
	struct netclient *player_client_before;
	hub_room_t room_before;
	u32 client_state_before;
	u32 client_flags_before;
	u8 client_playernum_before;
	u8 client_room_before;
	u8 player_newlife_before;
	bool player_remote_before;
	bool stage_ready_before;
	u8 auth_cookie_before[NET_AUTH_COOKIE_LEN];
	char reconnect_name[NET_MAX_NAME];
	s32 state_send_result;
	enum net_restore_result result;

	if (!cl || !cl->reconnect_resync_pending
			|| cl->reconnect_preserved_index >= NET_MAX_CLIENTS) {
		return netServerRestoreReject(NET_RESTORE_INVALID_ARGUMENT, cl, NULL,
			PLAYER_IDENTITY_INVALID_ARGUMENT);
	}
	pp = &g_NetPreservedPlayers[cl->reconnect_preserved_index];
	result = netServerPrepareRestoreCandidate(cl, pp,
		&cl->reconnect_settings_plan, cl->reconnect_sanitized_team, &candidate);
	if (result != NET_RESTORE_OK) {
		return result;
	}
	strncpy(reconnect_name, pp->name, sizeof(reconnect_name) - 1);
	reconnect_name[sizeof(reconnect_name) - 1] = '\0';

	if (candidate.room) {
		room_before = *candidate.room;
	}
	config_before = g_PlayerConfigsArray[pp->playernum];
	settings_before = cl->settings;
	client_config_before = cl->config;
	client_player_before = cl->player;
	client_playernum_before = cl->playernum;
	client_room_before = cl->room_id;
	client_state_before = cl->state;
	client_flags_before = cl->flags;
	stage_ready_before = cl->stage_ready;
	memcpy(auth_cookie_before, cl->auth_cookie, sizeof(auth_cookie_before));
	player_client_before = candidate.player->client;
	player_remote_before = candidate.player->isremote;
	player_newlife_before = candidate.player->dostartnewlife;

	if (candidate.room && !roomRejoin(candidate.room, (u8)cl->id)) {
		return netServerRestoreReject(NET_RESTORE_ROOM_UNAVAILABLE, cl, pp,
			candidate.identity_status);
	}
	g_PlayerConfigsArray[pp->playernum] = candidate.config;
	cl->settings = pp->settings;
	cl->playernum = pp->playernum;
	cl->room_id = pp->room_id;
	cl->config = &g_PlayerConfigsArray[pp->playernum];
	cl->player = candidate.player;
	candidate.player->client = cl;
	candidate.player->isremote = true;
	if (candidate.player->isdead) {
		candidate.player->dostartnewlife = true;
	}
	cl->state = CLSTATE_GAME;
	cl->flags &= ~CLFLAG_ABSENT;
	cl->stage_ready = true;
	memcpy(cl->auth_cookie, pp->cookie, sizeof(cl->auth_cookie));

	state_send_result = netServerSendReconnectState(cl);
	if (state_send_result != 0) {
		if (candidate.room) {
			*candidate.room = room_before;
		}
		g_PlayerConfigsArray[pp->playernum] = config_before;
		cl->settings = settings_before;
		cl->config = client_config_before;
		cl->player = client_player_before;
		cl->playernum = client_playernum_before;
		cl->room_id = client_room_before;
		cl->state = client_state_before;
		cl->flags = client_flags_before;
		cl->stage_ready = stage_ready_before;
		memcpy(cl->auth_cookie, auth_cookie_before,
			sizeof(auth_cookie_before));
		candidate.player->client = player_client_before;
		candidate.player->isremote = player_remote_before;
		candidate.player->dostartnewlife = player_newlife_before;
		return netServerRestoreReject(
			state_send_result == -1 || state_send_result == -3
				? NET_RESTORE_STATE_WRITE_FAILED
				: NET_RESTORE_STATE_SEND_FAILED,
			cl, pp,
			candidate.identity_status);
	}

	memset(pp, 0, sizeof(*pp));
	if (g_NetNumPreserved > 0) {
		--g_NetNumPreserved;
	}
	cl->reconnect_preserved_index = NET_NULL_CLIENT;
	cl->reconnect_settings_pending = false;
	cl->reconnect_manifest_phase = NET_RECONNECT_MANIFEST_NONE;
	cl->reconnect_manifest_hash = 0;
	netRoomListMarkDirty();
	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.COMMIT reconnect client=%u player=%u room=%u prop=%u team=%u body='%s' head='%s' points=%d deaths=%d cookie_preserved=1 post_load_ready=1 resync=targeted",
		(unsigned)cl->id, (unsigned)cl->playernum, (unsigned)cl->room_id,
		(unsigned)cl->player->prop->syncid, (unsigned)cl->settings.team,
		cl->settings.body_id, cl->settings.head_id,
		(int)cl->config->base.numpoints, (int)cl->config->base.numdeaths);
	sysLogPrintf(LOG_NOTE,
		"NET: %s (%u) reconnected player=%u room=%u stable_id=1 resync_once=1",
		reconnect_name, (unsigned)cl->id, (unsigned)cl->playernum,
		(unsigned)cl->room_id);
	netChatPrintf(NULL, "%s reconnected", reconnect_name);
	netmsgServerPublishAuthenticatedTopology();
	return NET_RESTORE_OK;
}

s32 netServerSendReconnectState(struct netclient *dstcl)
{
	struct netbuf wire;
	net_reconnect_snapshot_result_t snapshot_result;
	u8 *data;
	u32 bytes;

	if (g_NetMode != NETMODE_SERVER || !dstcl || !dstcl->peer
			|| dstcl->state != CLSTATE_GAME
			|| !dstcl->reconnect_resync_pending) {
		return -1;
	}

	data = (u8 *)malloc(NET_BUFSIZE);
	if (!data) {
		return -2;
	}
	memset(&wire, 0, sizeof(wire));
	wire.data = data;
	wire.size = NET_BUFSIZE;
	netbufStartWrite(&wire);
	if (netmsgSvcReconnectStateWrite(&wire, dstcl, &snapshot_result) != 0
			|| wire.wp == 0) {
		sysLogPrintf(LOG_ERROR,
			"NET.RECONNECT.RESYNC.FAIL target_client=%u status=%s subject_client=%d prop=%u prop_reason=%s prop_type=%d obj_type=%d model=%d scale=%g weapon=%d dual_weapon=%d dual_prop=%u parent=%u hidden=0x%08x hidden2=0x%02x attachment_mtx=%d objective=%d bytes=%u capacity=%u atomic=1",
			(unsigned)dstcl->id,
			netmsgReconnectSnapshotStatusString(snapshot_result.status),
			snapshot_result.client_id == NET_NULL_CLIENT
				? -1 : (s32)snapshot_result.client_id,
			(unsigned)snapshot_result.prop_syncid,
			netmsgReconnectPropStateStatusString(
				snapshot_result.prop_state_status),
			(int)snapshot_result.prop_type,
			(int)snapshot_result.object_type,
			(int)snapshot_result.model_num,
			(double)snapshot_result.model_scale,
			(int)snapshot_result.weapon_num,
			(int)snapshot_result.dual_weapon_num,
			(unsigned)snapshot_result.dual_prop_syncid,
			(unsigned)snapshot_result.parent_syncid,
			(unsigned)snapshot_result.object_hidden,
			(unsigned)snapshot_result.object_hidden2,
			(int)snapshot_result.attachment_mtx_index,
			(int)snapshot_result.objective_index,
			(unsigned)snapshot_result.bytes_written,
			(unsigned)snapshot_result.capacity);
		free(data);
		return -3;
	}
	bytes = wire.wp;
	if (netSend(dstcl, &wire, true, NETCHAN_DEFAULT) != bytes) {
		free(data);
		return -4;
	}
	free(data);

	dstcl->reconnect_resync_pending = false;
	dstcl->reconnect_gameplay_witness_pending = true;
	sysLogPrintf(LOG_NOTE,
		"NET.RECONNECT.RESYNC client=%u bytes=%u post_load_ready=1 targeted=1 ordered=1",
		(unsigned)dstcl->id, (unsigned)bytes);
	return 0;
}

static void netServerEvConnect(ENetPeer *peer, const u32 data)
{
	net_reconnect_connect_status_e connect_status;
	struct netpreservedplayer *pp = NULL;
	s32 reconnect = false;
	u8 reconnect_client_id = NET_NULL_CLIENT;

	sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON, "NET: incoming connection");

	connect_status = netReconnectConnectDataDecode(data, NET_PROTOCOL_VER,
		NET_NULL_CLIENT, &reconnect, &reconnect_client_id);
	if (connect_status != NET_RECONNECT_CONNECT_OK) {
		sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON,
			"NET: connection rejected: connect-data status=%s raw=%u expected_protocol=%u",
			netReconnectConnectStatusString(connect_status), data,
			NET_PROTOCOL_VER);
		enet_peer_disconnect(peer, DISCONNECT_VERSION);
		return;
	}

	/* MASTER-C2d: enforce persistent ban list before touching any client slot.
	 * Extract just the IP portion (without port) — serverBansIsBanned matches
	 * the bare address string.  This runs for every incoming connection, so
	 * keep it cheap: one linear scan, case-insensitive compare. */
	{
		char ip[SERVER_BANS_ADDR_LEN];
		ip[0] = '\0';
		if (enet_address_get_ip(&peer->address, ip, sizeof(ip) - 1) == 0 && ip[0]) {
			if (serverBansIsBanned(ip)) {
				sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON,
					"NET: connection from %s rejected: address is banned", ip);
				enet_peer_disconnect(peer, DISCONNECT_BANNED);
				return;
			}
		}
	}

	const bool ingame = netServerMatchInProgress() != 0;

	struct netclient *cl = NULL;

	/* Dedicated server: slot 0 is free for real players.
	 * Listen server: slot 0 is the host, start at 1. */
	s32 slotStart = g_NetDedicated ? 0 : 1;
	if (ingame) {
		if (!reconnect || reconnect_client_id < slotStart
				|| reconnect_client_id >= g_NetMaxClients) {
			sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON,
				"NET: connection rejected: live match requires a valid stable-slot reconnect hint");
			enet_peer_disconnect(peer, DISCONNECT_LATE);
			return;
		}
		pp = netServerFindPreservedByClientId(reconnect_client_id);
		if (!pp || g_NetClients[reconnect_client_id].state != CLSTATE_DISCONNECTED) {
			sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON,
				"NET: connection rejected: preserved stable slot %u is unavailable",
				(unsigned)reconnect_client_id);
			enet_peer_disconnect(peer, DISCONNECT_LATE);
			return;
		}
		cl = &g_NetClients[reconnect_client_id];
	} else {
		for (s32 i = slotStart; i < g_NetMaxClients; ++i) {
			if (!g_NetClients[i].state) {
				cl = &g_NetClients[i];
				break;
			}
		}
	}

	if (!cl) {
		sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON, "NET: connection rejected: server full");
		enet_peer_disconnect(peer, DISCONNECT_FULL);
		return;
	}

	/* M-7: Don't increment g_NetNumClients here — wait until CLC_AUTH succeeds.
	 * This prevents unauthenticated connections from counting toward the client limit. */

	netClientReset(cl);
	cl->state = CLSTATE_AUTH; // skip CLSTATE_CONNECTING, since we already know it connected
	cl->peer = peer;
	cl->flags = ingame ? CLFLAG_ABSENT : 0; // mark as pending reconnect if mid-game
	if (pp) {
		cl->reconnect_preserved_index = (u8)(pp - g_NetPreservedPlayers);
	}
	enet_peer_set_data(peer, cl);
	sysLogPrintf(LOG_NOTE,
		"NET: client slot %d assigned to peer reconnect=%d preserved_index=%u",
		(int)(cl - g_NetClients), reconnect != 0,
		(unsigned)cl->reconnect_preserved_index);
}

static void netServerEvDisconnect(struct netclient *cl,
		const u32 transport_reason)
{
	const net_reconnect_disconnect_plan_t disconnect_plan =
		netReconnectPlanServerDisconnect(transport_reason,
			cl && cl->server_disconnect_intent_pending,
			cl ? cl->server_disconnect_intent_reason : DISCONNECT_UNKNOWN);
	const u32 reason = disconnect_plan.effective_reason;

	if (disconnect_plan.used_server_intent) {
		cl->server_disconnect_intent_pending = false;
		cl->server_disconnect_intent_reason = DISCONNECT_UNKNOWN;
	}
	const bool authenticated = cl && cl->state >= CLSTATE_LOBBY;
	const bool retryable = netReconnectReasonIsRetryable(reason,
		DISCONNECT_TIMEOUT) != 0;
	bool reconnect_preserved = false;

	sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON,
		"NET: disconnect event from client %u reason=%u transport_reason=%u server_intent=%d retryable=%d",
		cl->id, reason, transport_reason,
		disconnect_plan.used_server_intent != 0, retryable != 0);
	netmsgCutsceneAuthorityRetireClient(cl->id);

	if (cl->peer) {
		enet_peer_reset(cl->peer);
	}

	// if client was in a game, preserve their identity and scores for reconnection,
	// then kill their character so it doesn't stand idle as a free target
	if (cl->state == CLSTATE_GAME && cl->settings.name[0]) {
		if (retryable) {
			netServerPreservePlayer(cl);
			reconnect_preserved =
				netServerFindPreservedByClientId((u8)cl->id) != NULL;
		}

		// kill the disconnected player's character (client is still linked,
		// so playerDie will broadcast SVC_PLAYER_STATS to all clients)
		if (cl->player && !cl->player->isdead && cl->playernum < MAX_PLAYERS) {
			const s32 prevplayernum = g_Vars.currentplayernum;
			setCurrentPlayerNum(cl->playernum);
			playerDie(true);
			setCurrentPlayerNum(prevplayernum);
			sysLogPrintf(LOG_NOTE, "NET: killed disconnected player %s character", cl->settings.name);
		}
	}

	/* Once an authenticated reconnect attempt ends for a policy/content/user
	 * reason, the client deliberately discards its credential. Release the
	 * matching server reservation in the same terminal path. An unauthenticated
	 * wrong-cookie probe can never reach this branch and therefore cannot evict
	 * the real owner's reservation. */
	if (!retryable && authenticated
			&& cl->reconnect_preserved_index < NET_MAX_CLIENTS) {
		const u8 preserved_index = cl->reconnect_preserved_index;
		/* This peer is already inside its ENet disconnect callback. Detach the
		 * transaction index before releasing the record so the general expiry
		 * helper does not try to disconnect the same reset peer a second time. */
		cl->reconnect_preserved_index = NET_NULL_CLIENT;
		netServerDiscardPreservedPlayer(
			&g_NetPreservedPlayers[preserved_index],
			"authenticated_terminal_disconnect");
	}

	/* R-3: Remove from room before reset (room_id cleared by netClientReset) */
	if (cl->room_id != 0xFF) {
		hub_room_t *room = roomGetById(cl->room_id);
		if (reconnect_preserved
				&& (!room || !roomLeaveForReconnect(room, (u8)cl->id))) {
			/* Preservation is useful only if every owned topology slot was
			 * reserved. Fail closed rather than publish a record that can never
			 * restore atomically. */
			netServerDiscardPreservedPlayer(
				netServerFindPreservedByClientId((u8)cl->id),
				"room_reservation_failed");
			reconnect_preserved = false;
			sysLogPrintf(LOG_ERROR,
				"PLAYER.INIT.ROLLBACK reconnect status=room_reservation_failed client=%u globals_published=0",
				(unsigned)cl->id);
		}
		if (room && !reconnect_preserved) {
			roomLeave(room, (u8)cl->id);
		}
	}

	if (cl->id == g_NetBotAuthorityClientId) {
		g_NetBotAuthorityClientId = NET_NULL_CLIENT;
		if (g_NetDedicated && cl->state == CLSTATE_GAME) {
			g_NetBotAuthorityDelegated = false;
			g_NetStageReadyDeadline = (s32)(g_NetTick + 120);
			sysLogPrintf(LOG_NOTE,
				"NET: bot authority client disconnected; re-election armed (deadline tick %d)",
				(int)g_NetStageReadyDeadline);
		}
	}

	if (cl->settings.name[0]) {
		sysLogPrintf(LOG_NOTE, "NET: client %u (%s) disconnected", cl->id, cl->settings.name);
		netChatPrintf(NULL, "%s disconnected", cl->settings.name);
	} else {
		sysLogPrintf(LOG_CHAT, "NET: client %u disconnected", cl->id);
	}

	netClientReset(cl);

	if (authenticated && g_NetNumClients > 0) {
		--g_NetNumClients;
	}

	/* R-3: Broadcast updated room list after client leaves */
	netBroadcastRoomList();
}

static void netServerEvReceive(struct netclient *cl)
{
	static u32 s_dispatchTraceCount = 0;
	u32 rc = 0;
	u8 msgid = 0;

	while (!rc && netbufReadLeft(&cl->in) > 0) {
		msgid = netbufReadU8(&cl->in);
		if (s_dispatchTraceCount < 10) {
			sysLogPrintf(LOG_NOTE, "MATCH-TRACE: server dispatch msgtype=0x%02x from client %d", msgid, cl->id);
			++s_dispatchTraceCount;
		}
		switch (msgid) {
			case CLC_NOP: rc = 0; break;
			case CLC_AUTH: rc = netmsgClcAuthRead(&cl->in, cl); break;
			case CLC_CHAT: rc = netmsgClcChatRead(&cl->in, cl); break;
			case CLC_MOVE: rc = netmsgClcMoveRead(&cl->in, cl); break;
			case CLC_SETTINGS: rc = netmsgClcSettingsRead(&cl->in, cl); break;
			case CLC_RESYNC_REQ: rc = netmsgClcResyncReqRead(&cl->in, cl); break;
			case CLC_COOP_READY: rc = netmsgClcCoopReadyRead(&cl->in, cl); break;
			case CLC_LOBBY_START:      rc = netmsgClcLobbyStartRead(&cl->in, cl); break;
			case CLC_CATALOG_DIFF:     rc = netmsgClcCatalogDiffRead(&cl->in, cl); break;
			/* Bot authority relay */
			case CLC_BOT_MOVE:         rc = netmsgClcBotMoveRead(&cl->in, cl); break;
			/* U-10: Stage-ready handshake */
			case CLC_STAGE_READY:      rc = netmsgClcStageReadyRead(&cl->in, cl); break;
			/* R-3: Room networking */
			case CLC_ROOM_CREATE:      rc = netmsgClcRoomCreateRead(&cl->in, cl); break;
			case CLC_ROOM_JOIN:        rc = netmsgClcRoomJoinRead(&cl->in, cl); break;
			case CLC_ROOM_LEAVE:       rc = netmsgClcRoomLeaveRead(&cl->in, cl); break;
			/* R-5: Room settings + playlist sync */
			case CLC_ROOM_SETTINGS_UPDATE: rc = netmsgClcRoomSettingsUpdateRead(&cl->in, cl); break;
			case CLC_ROOM_PLAYLIST_UPDATE: rc = netmsgClcRoomPlaylistUpdateRead(&cl->in, cl); break;
			/* MASTER-C2b: RCON admin channel */
			case CLC_ADMIN:            rc = netmsgClcAdminRead(&cl->in, cl); break;
			/* Phase 3 spectator (protocol v42) */
			case CLC_SPECTATE_REQUEST: rc = netmsgClcSpectateRequestRead(&cl->in, cl); break;
			/* v46 cutscene skip authority */
			case CLC_CUTSCENE_SKIP:    rc = netmsgClcCutsceneSkipRead(&cl->in, cl); break;
			/* v49 post-match lobby resync */
			case CLC_LOBBY_RESYNC:     rc = netmsgClcLobbyResyncRead(&cl->in, cl); break;
			/* Phase C: Match Startup Pipeline */
			case CLC_MANIFEST_STATUS:  rc = netmsgClcManifestStatusRead(&cl->in, cl); break;
			case CLC_LOBBY_CANCEL:     rc = netmsgClcLobbyCancelRead(&cl->in, cl); break;
			default:
				rc = 1;
				break;
		}
	}

	if (rc) {
		sysLogPrintf(LOG_WARNING , "NET: malformed or unknown message 0x%02x from client %u", msgid, cl->id);
	}
}

static void netClientEvConnect(const u32 data)
{
	sysLogPrintf(LOG_NOTE, "NET: connected to server, sending CLC_AUTH");

	g_NetLocalClient->state = CLSTATE_AUTH;

	// send auth request
	netbufStartWrite(&g_NetMsgRel);
	if (netmsgClcAuthWrite(&g_NetMsgRel) != 0
			|| netmsgClcSettingsWrite(&g_NetMsgRel) != 0) {
		sysLogPrintf(LOG_ERROR,
			"NET: client auth/settings preflight rejected; no handshake packet sent");
		netbufStartWrite(&g_NetMsgRel);
		if (g_NetLocalClient->peer) {
			enet_peer_disconnect(g_NetLocalClient->peer, DISCONNECT_FILES);
		}
		return;
	}
	netSend(g_NetLocalClient, &g_NetMsgRel, true, NETCHAN_CONTROL);
}

static void netClientEvDisconnect(const u32 reason)
{
	sysLogPrintf(LOG_CHAT, "NET: disconnected from server: %s (%u)", netGetDisconnectReason(reason), reason);
	netDisconnectWithIntent(netReconnectReasonIsRetryable(reason,
		DISCONNECT_TIMEOUT));
}

static void netClientEvReceive(struct netclient *cl)
{
	u32 rc = 0;
	u8 msgid = 0;

	while (!rc && netbufReadLeft(&cl->in) > 0) {
		msgid = netbufReadU8(&cl->in);
		switch (msgid) {
			case SVC_NOP: rc = 0; break;
			case SVC_AUTH: rc = netmsgSvcAuthRead(&cl->in, cl); break;
			case SVC_CHAT: rc = netmsgSvcChatRead(&cl->in, cl); break;
			case SVC_STAGE_START: rc = netmsgSvcStageStartRead(&cl->in, cl); break;
			case SVC_STAGE_END: rc = netmsgSvcStageEndRead(&cl->in, cl); break;
			case SVC_PLAYER_MOVE: rc = netmsgSvcPlayerMoveRead(&cl->in, cl); break;
			case SVC_PLAYER_STATS: rc = netmsgSvcPlayerStatsRead(&cl->in, cl); break;
			case SVC_PLAYER_SCORES: rc = netmsgSvcPlayerScoresRead(&cl->in, cl); break;
			case SVC_PROP_MOVE: rc = netmsgSvcPropMoveRead(&cl->in, cl); break;
			case SVC_PROP_SPAWN: rc = netmsgSvcPropSpawnRead(&cl->in, cl); break;
			case SVC_PROP_DAMAGE: rc = netmsgSvcPropDamageRead(&cl->in, cl); break;
			case SVC_PROP_PICKUP: rc = netmsgSvcPropPickupRead(&cl->in, cl); break;
			case SVC_PROP_USE: rc = netmsgSvcPropUseRead(&cl->in, cl); break;
			case SVC_PROP_DOOR: rc = netmsgSvcPropDoorRead(&cl->in, cl); break;
			case SVC_PROP_LIFT: rc = netmsgSvcPropLiftRead(&cl->in, cl); break;
			case SVC_CHR_DAMAGE: rc = netmsgSvcChrDamageRead(&cl->in, cl); break;
			case SVC_CHR_DISARM: rc = netmsgSvcChrDisarmRead(&cl->in, cl); break;
			case SVC_CHR_MOVE: rc = netmsgSvcChrMoveRead(&cl->in, cl); break;
			case SVC_CHR_STATE: rc = netmsgSvcChrStateRead(&cl->in, cl); break;
			case SVC_CHR_SYNC: rc = netmsgSvcChrSyncRead(&cl->in, cl); break;
			case SVC_CHR_RESYNC: rc = netmsgSvcChrResyncRead(&cl->in, cl); break;
			case SVC_PROP_SYNC: rc = netmsgSvcPropSyncRead(&cl->in, cl); break;
			case SVC_PROP_RESYNC: rc = netmsgSvcPropResyncRead(&cl->in, cl); break;
			case SVC_NPC_MOVE: rc = netmsgSvcNpcMoveRead(&cl->in, cl); break;
			case SVC_NPC_STATE: rc = netmsgSvcNpcStateRead(&cl->in, cl); break;
			case SVC_NPC_SYNC: rc = netmsgSvcNpcSyncRead(&cl->in, cl); break;
			case SVC_NPC_RESYNC: rc = netmsgSvcNpcResyncRead(&cl->in, cl); break;
			/* Bot authority relay */
			case SVC_BOT_AUTHORITY: rc = netmsgSvcBotAuthorityRead(&cl->in, cl); break;
			case SVC_STAGE_FLAG: rc = netmsgSvcStageFlagRead(&cl->in, cl); break;
			case SVC_OBJ_STATUS: rc = netmsgSvcObjStatusRead(&cl->in, cl); break;
			case SVC_ALARM: rc = netmsgSvcAlarmRead(&cl->in, cl); break;
			case SVC_CUTSCENE: rc = netmsgSvcCutsceneRead(&cl->in, cl); break;
			case SVC_CUTSCENE_SKIP: rc = netmsgSvcCutsceneSkipRead(&cl->in, cl); break;
			case SVC_RECONNECT_PROP_BEGIN: rc = netmsgSvcReconnectPropBeginRead(&cl->in, cl); break;
			case SVC_RECONNECT_PROP_STATE: rc = netmsgSvcReconnectPropStateRead(&cl->in, cl); break;
			case SVC_RECONNECT_PROP_END: rc = netmsgSvcReconnectPropEndRead(&cl->in, cl); break;
			case SVC_RECONNECT_INVENTORY: rc = netmsgSvcReconnectInventoryRead(&cl->in, cl); break;
			case SVC_RECONNECT_COMMIT: rc = netmsgSvcReconnectCommitRead(&cl->in, cl); break;
			case SVC_LOBBY_LEADER:   rc = netmsgSvcLobbyLeaderRead(&cl->in, cl); break;
			case SVC_LOBBY_STATE:    rc = netmsgSvcLobbyStateRead(&cl->in, cl); break;
			/* D3R-9: Network Distribution */
			case SVC_CATALOG_INFO:    rc = netmsgSvcCatalogInfoRead(&cl->in, cl); break;
			case SVC_DISTRIB_BEGIN:   rc = netmsgSvcDistribBeginRead(&cl->in, cl); break;
			case SVC_DISTRIB_CHUNK:   rc = netmsgSvcDistribChunkRead(&cl->in, cl); break;
			case SVC_DISTRIB_END:     rc = netmsgSvcDistribEndRead(&cl->in, cl); break;
			case SVC_LOBBY_KILL_FEED: rc = netmsgSvcLobbyKillFeedRead(&cl->in, cl); break;
			/* R-3: Room networking */
			case SVC_ROOM_LIST:        rc = netmsgSvcRoomListRead(&cl->in, cl); break;
			case SVC_ROOM_ASSIGN:      rc = netmsgSvcRoomAssignRead(&cl->in, cl); break;
			case SVC_MUSIC_ADVANCE:    rc = netmsgSvcMusicAdvanceRead(&cl->in, cl); break;
			case SVC_ACHIEVEMENT_TOAST: rc = netmsgSvcAchievementToastRead(&cl->in, cl); break;
			/* Phase 3 spectator (protocol v42) */
			case SVC_SPECTATE_ACK:      rc = netmsgSvcSpectateAckRead(&cl->in, cl); break;
			case SVC_STATE_FRAME:       rc = netmsgSvcStateFrameRead (&cl->in, cl); break;
			case SVC_GPUSWARM_STATE:    rc = netmsgSvcGpuSwarmStateRead(&cl->in, cl); break;
			/* R-5: Room settings + playlist sync */
			case SVC_ROOM_SETTINGS:    rc = netmsgSvcRoomSettingsRead(&cl->in, cl); break;
			case SVC_ROOM_PLAYLIST:    rc = netmsgSvcRoomPlaylistRead(&cl->in, cl); break;
			/* Phase C: Match Startup Pipeline */
			case SVC_MATCH_MANIFEST:   rc = netmsgSvcMatchManifestRead(&cl->in, cl); break;
			case SVC_MATCH_COUNTDOWN:  rc = netmsgSvcMatchCountdownRead(&cl->in, cl); break;
			case SVC_MATCH_CANCELLED:  rc = netmsgSvcMatchCancelledRead(&cl->in, cl); break;
			case SVC_SESSION_CATALOG:
				rc = netmsgSvcSessionCatalogRead(&cl->in, NULL);
				break;
			case SVC_ADMIN: {
				/* MASTER-C2b: admin reply.  We drain the response here on the
				 * client so the decoder stays in sync.  The payload is
				 * surfaced to the UI layer via a pair of extern hooks that
				 * the ImGui admin console subscribes to (if installed). */
				const u8 code = netbufReadU8(&cl->in);
				const char *msg = netbufReadStr(&cl->in);
				(void)code;
				if (msg && msg[0]) {
					sysLogPrintf(LOG_NOTE, "NET: SVC_ADMIN [%u]: %s", (unsigned)code, msg);
				}
				rc = cl->in.error;
				break;
			}
			default:
				rc = 1;
				break;
		}
	}

	if (rc) {
		sysLogPrintf(LOG_WARNING, "NET: malformed or unknown message 0x%02x from server", msgid);
		/* Stage publication is transactional on both peers. If a stage replay
		 * cannot be validated and applied, continuing on the old local stage
		 * after the server has reliably queued the new snapshot would create a
		 * split-brain client. Close with a terminal content/protocol reason; a
		 * reconnect credential is intentionally not retained for malformed
		 * authoritative state. */
		const bool reconnect_transaction = s_NetReconnectAttempt.valid;
		const bool reconnect_control = msgid == SVC_AUTH
			|| msgid == SVC_MATCH_MANIFEST
			|| msgid == SVC_SESSION_CATALOG
			|| msgid == SVC_ROOM_ASSIGN
			|| msgid == SVC_CATALOG_INFO
			|| msgid == SVC_DISTRIB_BEGIN
			|| msgid == SVC_DISTRIB_CHUNK
			|| msgid == SVC_DISTRIB_END;
		if ((msgid == SVC_STAGE_START
				|| (reconnect_transaction
					&& (reconnect_control || (cl && cl->stage_ready))))
				&& cl && cl->peer) {
			sysLogPrintf(LOG_ERROR,
				"NET.RECONNECT.CLIENT authoritative message rejected id=0x%02x reconnect=%u terminal=1",
				(unsigned)msgid, reconnect_transaction ? 1u : 0u);
			enet_peer_disconnect(cl->peer, DISCONNECT_FILES);
		}
	}
}

void netClientSyncRng(void)
{
	if (g_NetMode == NETMODE_CLIENT && g_NetRngLatch) {
		g_NetRngLatch = 0;
		g_RngSeed = g_NetRngSeeds[0];
		g_Rng2Seed = g_NetRngSeeds[1];
	}
}

s32 netClientSettingsChanged(void)
{
	const bool is_remote_client = g_NetMode == NETMODE_CLIENT;
	const bool is_listen_host = g_NetMode == NETMODE_SERVER && !g_NetDedicated;
	net_client_settings_plan_t plan;
	net_client_settings_wire_status_e status;
	player_identity_status_e identity_status;

	if (!g_NetLocalClient || (!is_remote_client && !is_listen_host)) {
		return -1;
	}

	/* The historical name predates in-client listen hosting. Both network
	 * roles own a local settings cache, but only a remote client has a server
	 * peer to notify. Refreshing the listen host in place keeps lobby resets
	 * and player-file changes on the same authoritative path without sending
	 * a client opcode through a null ENet peer. */
	netClientReadConfig(g_NetLocalClient, 0);
	status = netClientPrepareCachedSettings(g_NetLocalClient, &plan,
		&identity_status);
	if (status != NET_CLIENT_SETTINGS_WIRE_OK) {
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.PREFLIGHT local-settings=reject status=%s identity=%s team=%u handicap=%u",
			netClientSettingsWireStatusString(status),
			playerIdentityStatusString(identity_status),
			(unsigned)g_NetLocalClient->settings.team,
			(unsigned)g_NetLocalClient->settings.handicap);
		return -2;
	}
	netClientCommitPreparedSettings(g_NetLocalClient, &plan);
	if (is_listen_host) {
		return 0;
	}

	netbufStartWrite(&g_NetMsgRel);
	if (netmsgClcSettingsWrite(&g_NetMsgRel) != 0) {
		netbufStartWrite(&g_NetMsgRel);
		return -3;
	}
	return netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL) > 0 ? 0 : -4;
}

void netStartFrame(void)
{
	if (!g_NetMode) {
		return;
	}

	++g_NetTick;

	const bool isClient = (g_NetMode == NETMODE_CLIENT);

	/* Tick hole punch state machine — must run before enet_host_service() so that
	 * when CONN_PHASE_PUNCH transitions to CONN_PHASE_PUNCH_ENET the fresh peer is
	 * ready for the ENET_EVENT_CONNECT that may arrive this frame. */
	if (isClient) {
		netHolePunchClientTick(g_NetHost);
	}

	s32 polled = false;
	ENetEvent ev = { .type = ENET_EVENT_TYPE_NONE };
	while (!polled) {
		if (enet_host_check_events(g_NetHost, &ev) <= 0) {
			if (enet_host_service(g_NetHost, &ev, 1) <= 0) {
				break;
			}
			polled = true;
		}

		switch (ev.type) {
			case ENET_EVENT_TYPE_CONNECT:
				if (isClient) {
					netHolePunchOnConnect(); /* mark waterfall complete if active */
					netClientEvConnect(ev.data);
				} else if (ev.peer) {
					netServerEvConnect(ev.peer, ev.data);
				}
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
			case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
				if (isClient) {
					if (ev.type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT &&
					    netHolePunchHandleTimeout(g_NetHost)) {
						/* Timeout consumed by hole punch waterfall — skip normal disconnect */
						break;
					}
					netClientEvDisconnect(ev.type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT ? DISCONNECT_TIMEOUT : ev.data);
				} else if (ev.peer) {
					struct netclient *cl = enet_peer_get_data(ev.peer);
					if (cl) {
						netServerEvDisconnect(cl,
							ev.type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT
								? DISCONNECT_TIMEOUT : ev.data);
					} else {
						// No attached client — spurious disconnect from peer that never completed auth.
						// Do NOT decrement g_NetNumClients here: it was never incremented for this peer.
						sysLogPrintf(LOG_WARNING | LOGFLAG_NOCON, "NET: spurious disconnect (no attached client — peer never completed auth)");
					}
				}
				break;
			case ENET_EVENT_TYPE_RECEIVE:
				if (ev.peer) {
					struct netclient *cl = (g_NetMode == NETMODE_CLIENT) ? g_NetLocalClient : enet_peer_get_data(ev.peer);
					if (cl && cl->state) {
						if (ev.packet && ev.packet->data && ev.packet->dataLength) {
							netbufStartReadData(&cl->in, ev.packet->data, ev.packet->dataLength);
							if (isClient) {
								netClientEvReceive(cl);
							} else {
								netServerEvReceive(cl);
							}
							netbufReset(&cl->in);
						}
					} else if (!isClient) {
						sysLogPrintf(LOG_WARNING | LOGFLAG_NOCON, "NET: spurious receive (no attached client)");
					}
				}
				enet_packet_dispose(ev.packet);
				break;
			default:
				break;
		}
	}

	netbufStartWrite(&g_NetMsg);
	netbufStartWrite(&g_NetMsgRel);
}

void netEndFrame(void)
{
	const bool terminal_stage_end_frame = g_NetMode == NETMODE_SERVER
		&& (s_NetStageEndPending.active || s_NetStageEndTerminalFrame);
	bool stage_replication_ready = true;
	bool authority_pending_frame;
	bool authority_publication_failed = false;

	if (!g_NetMode) {
		return;
	}

	g_NetReliableFrameLen = 0;
	g_NetUnreliableFrameLen = 0;
	if (g_NetMode == NETMODE_SERVER && !terminal_stage_end_frame) {
		stage_replication_ready = netServerStageReplicationReady();
	}
	authority_pending_frame = g_NetMode == NETMODE_SERVER
		&& !terminal_stage_end_frame && stage_replication_ready
		&& s_NetStageReplicationPhase == NET_STAGE_REPLICATION_ACTIVE
		&& netmsgCutsceneAuthorityHasPendingEvents();

	/* Any authority event already pending at this boundary supersedes ordinary
	 * game messages accumulated since netStartFrame. Discard those shared
	 * buffers, then publish the dedicated authority packet first. This keeps a
	 * retained START/ACCEPT retry from being overtaken just as strictly as the
	 * terminal END + SVC_STAGE_END transaction. */
	if (terminal_stage_end_frame
			|| (g_NetMode == NETMODE_SERVER
				&& (s_NetStageReplicationPhase
					!= NET_STAGE_REPLICATION_ACTIVE))
			|| authority_pending_frame) {
		netbufStartWrite(&g_NetMsg);
		netbufStartWrite(&g_NetMsgRel);
	} else {
		// send whatever messages have accumulated so far
		netFlushSendBuffers();
	}
	if (terminal_stage_end_frame && s_NetStageEndPending.active) {
		(void)netServerFlushPendingStageEnd("end-frame-stage-end");
	} else if (authority_pending_frame
			&& !netServerFlushCutsceneAuthority("end-frame-priority")) {
		authority_publication_failed = true;
	}

	/* Phase 3 (v42): spectator host fan-out. Server-side only. The
	 * SPECTATOR_FANOUT_HZ cadence is enforced inside
	 * netSendSpectateStateFrame via a static last-broadcast-ms gate, so
	 * we can call this every tick on both `pd` (listen host) and
	 * `pd-server` (dedicated). No-op when no spectator clients are
	 * subscribed. */
	if (g_NetMode == NETMODE_SERVER && !terminal_stage_end_frame
			&& !authority_publication_failed
			&& s_NetStageReplicationPhase == NET_STAGE_REPLICATION_ACTIVE) {
		netSendSpectateStateFrame();
	}

	/* --- Client: send player move --- */
	if (g_NetMode == NETMODE_CLIENT
			&& g_NetLocalClient && g_NetLocalClient->state == CLSTATE_GAME
			&& g_NetLocalClient->player && g_NetLocalClient->player->prop) {
		if (g_NetTick > 100) {
			netClientRecordMove(g_NetLocalClient, g_NetLocalClient->player);
			const bool needrel = netClientNeedReliableMove(g_NetLocalClient);
			if (needrel || netClientNeedMove(g_NetLocalClient)) {
				netmsgClcMoveWrite(needrel ? &g_NetMsgRel : &g_NetMsg);
			}
		}
		if (g_NetNextUpdate <= g_NetTick) {
			g_NetNextUpdate = g_NetTick + g_NetClientUpdateRate;
		}
		// Flush any pending resync requests that were flagged during netStartFrame's recv dispatch.
		// Must be done here (netEndFrame) because netStartFrame resets g_NetMsgRel after dispatch,
		// making any direct write inside a recv handler silently dropped.
		if (g_NetPendingResyncReqFlags) {
			netmsgClcResyncReqWrite(&g_NetMsgRel, g_NetPendingResyncReqFlags);
			g_NetPendingResyncReqFlags = 0;
		}
	}

	/* Reservation lifetime is server state, not broadcast work. It must keep
	 * advancing even when the last remote client is currently disconnected. */
	if (g_NetMode == NETMODE_SERVER && !terminal_stage_end_frame) {
		netServerExpirePreservedPlayers();
	}

	/* --- Server: broadcast game state to all clients ---
	 * CRITICAL: This block must NOT be guarded by g_NetLocalClient — on a
	 * dedicated server g_NetLocalClient is NULL, so putting this inside a
	 * g_NetLocalClient guard makes the entire server broadcast path dead code.
	 * Instead, check g_NetMode == NETMODE_SERVER and use g_NetNumClients to
	 * know whether any clients are connected and need updates. */
	if (g_NetMode == NETMODE_SERVER && g_NetNumClients > 0
			&& !terminal_stage_end_frame
			&& !authority_publication_failed && stage_replication_ready) {
		/* RELEASE publishes only the complete fresh baseline below. Incremental
		 * player/bot/NPC traffic starts on the following ACTIVE frame, so an
		 * unreliable delta cannot overtake the reliable baseline. */
		if (s_NetStageReplicationPhase == NET_STAGE_REPLICATION_ACTIVE) {
		for (s32 i = 0; i < g_NetMaxClients; ++i) {
			struct netclient *cl = &g_NetClients[i];
			if (cl->state >= CLSTATE_GAME && cl->player) {
				netClientRecordMove(cl, cl->player);
				const bool needrel = netClientNeedReliableMove(cl);
				if (needrel || netClientNeedMove(cl)) {
					netmsgSvcPlayerMoveWrite(needrel ? &g_NetMsgRel : &g_NetMsg, cl);
				}
			}
		}

		// Broadcast bot/simulant positions to all clients every update frame
		if (g_NetNextUpdate <= g_NetTick) {
			if (g_NetTick < 600 && (g_NetTick % 60) == 0) {
				s32 validBots = 0;
				for (s32 bi = 0; bi < g_BotCount; ++bi) {
					if (g_MpBotChrPtrs[bi] && g_MpBotChrPtrs[bi]->prop && g_MpBotChrPtrs[bi]->aibot) {
						validBots++;
					}
				}
				sysLogPrintf(LOG_NOTE, "MATCH-TRACE: SVC_CHR_MOVE broadcast frame=%d bots=%d valid=%d",
					g_NetTick, g_BotCount, validBots);
			}
			for (s32 i = 0; i < g_BotCount; ++i) {
				struct chrdata *chr = g_MpBotChrPtrs[i];
				if (chr && chr->prop && chr->aibot) {
					netmsgSvcChrMoveWrite(&g_NetMsg, chr);
				}
			}

			// Send bot state less frequently (every 15 frames ~= 4 times/sec at 60fps)
			if ((g_NetTick % 15) == 0) {
				for (s32 i = 0; i < g_BotCount; ++i) {
					struct chrdata *chr = g_MpBotChrPtrs[i];
					if (chr && chr->prop && chr->aibot) {
						netmsgSvcChrStateWrite(&g_NetMsgRel, chr);
					}
				}
			}

			// Send desync detection checksum every 60 frames (~1/sec)
			if ((g_NetTick % 60) == 0 && g_BotCount > 0) {
				netmsgSvcChrSyncWrite(&g_NetMsgRel);
			}

			// Event-driven prop dirty check: snapshot comparison replaces CRC polling.
			// Only triggers SVC_PROP_RESYNC when hidden/damage state actually changed.
			if ((g_NetTick % 120) == 0 && g_Vars.mplayerisrunning && netPropDirtyCheck()) {
				g_NetPendingResyncFlags |= NET_RESYNC_FLAG_PROPS;
			}

			g_NetNextUpdate = g_NetTick + g_NetServerUpdateRate;
		}

		// Co-op NPC replication: broadcast NPC positions and state
		// NPCs are server-authoritative in co-op mode (AI runs only on server)
		if (g_NetGameMode == NETGAMEMODE_COOP || g_NetGameMode == NETGAMEMODE_ANTI) {
			static bool s_npcBroadcastStarted = false;
			if (!s_npcBroadcastStarted) {
				sysLogPrintf(LOG_NOTE, "NET: starting NPC broadcast with %u npcs", netNpcCount());
				s_npcBroadcastStarted = true;
			}

			// NPC position updates every 3 frames (~20 Hz)
			if ((g_NetTick % 3) == 0) {
				if (g_NumChrSlots > 0) {
					for (s32 i = 0; i < g_NumChrSlots; ++i) {
						struct chrdata *chr = &g_ChrSlots[i];
						if (netNpcIsReplicationReady(chr)) {
							netmsgSvcNpcMoveWrite(&g_NetMsg, chr);
						}
					}
				}
			}

			// NPC state updates every 30 frames (~2/sec)
			if ((g_NetTick % 30) == 0) {
				if (g_NumChrSlots > 0) {
					for (s32 i = 0; i < g_NumChrSlots; ++i) {
						struct chrdata *chr = &g_ChrSlots[i];
						if (netNpcIsReplicationReady(chr)) {
							netmsgSvcNpcStateWrite(&g_NetMsgRel, chr);
						}
					}
				}
			}
		}

		/* L1-2: Periodic score broadcast every 300 frames (~5 s at 60 fps).
		 * Score mutations are event-driven (SVC_PLAYER_STATS on each death), but
		 * a single dropped reliable packet causes permanent divergence until
		 * reconnect.  This gives a bounded correction window so no client can
		 * stay out of sync for more than 5 seconds. ~200 bytes per broadcast.
		 * Guard with mplayerisrunning so the broadcast stops at match end. */
		if ((g_NetTick % 300) == 0 && g_Vars.mplayerisrunning) {
			netmsgSvcPlayerScoresWrite(&g_NetMsgRel);
			/* c3845 (2026-06-23): two-process match-smoke milestone. Periodic
			 * score broadcast from the HOST proves score replication is live. */
			sysLogPrintf(LOG_NOTE, "MATCH: scores replicated tick=%u", g_NetTick);
		}
		}

	}

	/* SEC-13: room topology is control-plane state. It must remain publishable
	 * in the lobby and while a stage barrier suppresses gameplay. */
	if (g_NetMode == NETMODE_SERVER && !terminal_stage_end_frame
			&& !authority_publication_failed) {
		netRoomListFlushIfDirty();
	}

	/* Bot authority: relay bot positions to server even when local player is respawning.
	 * Outside the player->prop guard so it fires regardless of local player state. */
	if (g_NetMode == NETMODE_CLIENT && g_NetLocalClient
			&& g_NetLocalClient->state == CLSTATE_GAME
			&& g_NetLocalBotAuthority && g_BotCount > 0) {
		if (g_NetTick < 600 && (g_NetTick % 60) == 0) {
			sysLogPrintf(LOG_NOTE, "MATCH-TRACE: CLC_BOT_MOVE frame=%d bots=%d authority=%d",
				g_NetTick, g_BotCount, (s32)g_NetLocalBotAuthority);
		}
		netmsgClcBotMoveWrite(&g_NetMsg);
	}

	/* D3R-9: tick mod distribution (runs in lobby and in-game, server only) */
	if (g_NetMode == NETMODE_SERVER && !terminal_stage_end_frame
			&& !authority_publication_failed) {
		netDistribServerTick();
		/* Phase F: drive the match launch countdown (no-op until armed by readyGateCheck) */
		readyGateTickCountdown();
		/* v34: advance music playlist when track ends (host only, no-op if no playlist).
		 * Issue 4b (v40): same tick re-broadcasts the current offset every 2s
		 * for client drift correction. */
		audioNetworkMusicTick();

		/* U-10/B-1104: The deadline is only an ACTIVE-stage bot-authority
		 * re-election aid. It may never bypass the post-load barrier or delegate
		 * gameplay authority to an endpoint that has not acknowledged this stage. */
		if (g_NetDedicated && !g_NetBotAuthorityDelegated
				&& g_NetStageReadyDeadline >= 0
				&& (s32)g_NetTick >= g_NetStageReadyDeadline
				&& s_NetStageReplicationPhase == NET_STAGE_REPLICATION_ACTIVE) {
			struct netclient *candidate = NULL;
			bool reelected = false;
			s32 readyCount = 0;
			s32 totalCount = 0;
			for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
				struct netclient *cl = &g_NetClients[ci];
				if (cl->state == CLSTATE_GAME
						&& !(cl->flags & CLFLAG_SPECTATOR)) {
					totalCount++;
					if (cl->peer && cl->stage_ready) {
						readyCount++;
						if (!candidate) {
							candidate = cl;
						}
					}
				}
			}
			if (candidate) {
				u8 payload[8];
				struct netbuf wire = {
					.data = payload,
					.size = sizeof(payload),
				};
				u32 bytes;

				netbufStartWrite(&wire);
				if (netmsgSvcBotAuthorityWrite(&wire) == 0 && wire.wp > 0) {
					bytes = wire.wp;
					if (netSend(candidate, &wire, true,
							NETCHAN_DEFAULT) == bytes) {
						g_NetBotAuthorityClientId = candidate->id;
						g_NetBotAuthorityDelegated = true;
						g_NetStageReadyDeadline = -1;
						reelected = true;
						sysLogPrintf(LOG_NOTE,
							"NET: SVC_BOT_AUTHORITY re-elected client %u ('%s') ready=%d/%d bot_stubs=%u",
							(unsigned)candidate->id, candidate->settings.name,
							readyCount, totalCount, (unsigned)g_BotCount);
					}
				}
			}
			if (!reelected) {
				g_NetStageReadyDeadline = (s32)(g_NetTick + 120);
				sysLogPrintf(LOG_WARNING,
					"NET: BOT_AUTHORITY re-election deferred ready=%d/%d retry_tick=%d",
					readyCount, totalCount, (int)g_NetStageReadyDeadline);
			}
		}

	}

	/* Catch any event introduced by future end-frame producers before the
	 * shared buffers are flushed. On failure, retain the ordered authority
	 * queue and discard all ordinary output from this frame. */
	if (g_NetMode == NETMODE_SERVER && !terminal_stage_end_frame
			&& !authority_publication_failed
			&& s_NetStageReplicationPhase == NET_STAGE_REPLICATION_ACTIVE
			&& netmsgCutsceneAuthorityHasPendingEvents()
			&& !netServerFlushCutsceneAuthority("end-frame-late")) {
		authority_publication_failed = true;
		netbufStartWrite(&g_NetMsg);
		netbufStartWrite(&g_NetMsgRel);
	}

	/* Publish resync ownership only after every late control-plane producer and
	 * authority-priority check has finished. The dedicated packet cannot be
	 * erased by a subsequent g_NetMsgRel reset. RELEASE stays blocked on any
	 * build/queue failure and retries the complete baseline next frame. */
	if (g_NetMode == NETMODE_SERVER && !terminal_stage_end_frame
			&& !authority_publication_failed && stage_replication_ready
			&& (s_NetStageReplicationPhase == NET_STAGE_REPLICATION_ACTIVE
				|| s_NetStageReplicationPhase == NET_STAGE_REPLICATION_RELEASE)
			&& !netServerPublishPendingResyncs()
			&& s_NetStageReplicationPhase == NET_STAGE_REPLICATION_RELEASE) {
		authority_publication_failed = true;
		sysLogPrintf(LOG_WARNING,
			"NET.STAGE.REPLICATION release=retry epoch=%u reason=baseline-publish",
			(unsigned)g_NetStageEpoch);
	}

	/* A stage can be armed late by readyGateTickCountdown after the readiness
	 * snapshot at function entry. Consult the live phase here so no pre-arm
	 * shared bytes escape. INACTIVE and WAITING both suppress shared entity
	 * buffers; control-plane producers use their explicit direct sends. */
	if (terminal_stage_end_frame || authority_publication_failed
			|| (g_NetMode == NETMODE_SERVER
				&& s_NetStageReplicationPhase
					!= NET_STAGE_REPLICATION_ACTIVE
				&& s_NetStageReplicationPhase
					!= NET_STAGE_REPLICATION_RELEASE)) {
		netbufStartWrite(&g_NetMsg);
		netbufStartWrite(&g_NetMsgRel);
	} else {
		netFlushSendBuffers();
	}
	if (g_NetMode == NETMODE_SERVER && !terminal_stage_end_frame
			&& !authority_publication_failed
			&& s_NetStageReplicationPhase == NET_STAGE_REPLICATION_RELEASE) {
		s_NetStageReplicationPhase = NET_STAGE_REPLICATION_ACTIVE;
		s_NetStageReplicationWaitMask = 0;
		sysLogPrintf(LOG_NOTE,
			"NET.STAGE.REPLICATION phase=active epoch=%u room=%u fresh_release=1",
			(unsigned)g_NetStageEpoch, (unsigned)g_NetMatchRoomId);
	}

	enet_host_flush(g_NetHost);
	netUploadMeasurementTick();
	s_NetStageEndTerminalFrame = false;
}

u32 netSend(struct netclient *dstcl, struct netbuf *buf, const s32 reliable, const s32 chan)
{
	if (g_NetMode == NETMODE_CLIENT) {
		dstcl = g_NetLocalClient;
	}

	if (buf == NULL) {
		if (dstcl) {
			buf = &dstcl->out;
		} else {
			buf = reliable ? &g_NetMsgRel : &g_NetMsg;
		}
	}

	if (reliable || !g_NetSimPacketLoss || (rand() % g_NetSimPacketLoss) == 0) {
		const u32 flags = (reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
		ENetPacket *p = enet_packet_create(buf->data, buf->wp, flags);
		if (!p) {
			sysLogPrintf(LOG_ERROR, "NET: could not alloc %u bytes for packet", buf->wp);
			return 0;
		}

		if (dstcl == NULL) {
			enet_host_broadcast(g_NetHost, chan, p);
		} else if (dstcl->peer) {
			/* H-5: Check return value — on failure, ENet does not free the packet. */
			if (enet_peer_send(dstcl->peer, chan, p) < 0) {
				sysLogPrintf(LOG_WARNING, "NET: enet_peer_send failed (%u bytes, chan %d)", buf->wp, chan);
				enet_packet_destroy(p);
				netbufStartWrite(buf);
				return 0;
			}
		} else {
			/* c3845: a peer-less client is the in-client listen host's own local
			 * slot -- it has no ENet wire to itself, so enet_peer_send(NULL) would
			 * access-violate (the SVC_ROOM_ASSIGN-to-creator path during the
			 * headless match auto-start hit exactly this). The host applies its own
			 * authoritative state directly, so just drop the packet. */
			enet_packet_destroy(p);
		}
	}

	const u32 ret = buf->wp;

	netbufStartWrite(buf);

	return ret;
}

u32 netSendToRoom(u8 room_id, struct netbuf *buf, s32 reliable, s32 chan)
{
	if (!g_NetHost || !buf || !buf->wp) return 0;

	const u32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
	const u32 bytes = buf->wp;
	bool failed = false;

	for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
		struct netclient *cl = &g_NetClients[i];
		if (cl->state >= CLSTATE_LOBBY && cl->peer && cl->room_id == room_id) {
			ENetPacket *p = enet_packet_create(buf->data, buf->wp, flags);
			if (!p || enet_peer_send(cl->peer, chan, p) != 0) {
				failed = true;
				if (p) {
					enet_packet_destroy(p);
				}
			}
		}
	}

	netbufStartWrite(buf);
	return failed ? 0 : bytes;
}

struct net_player_allocation_plan {
	struct netclient *client;
	struct player *player;
	struct mpplayerconfig config;
	player_identity_plan_t identity;
	u8 playernum;
	bool remote;
};

const char *netPlayerAllocateResultString(enum net_player_allocate_result result)
{
	switch (result) {
	case NET_PLAYER_ALLOC_OK: return "ok";
	case NET_PLAYER_ALLOC_INVALID_MODE: return "invalid_mode";
	case NET_PLAYER_ALLOC_MISSING_LOCAL_CLIENT: return "missing_local_client";
	case NET_PLAYER_ALLOC_TOO_MANY_PLAYERS: return "too_many_players";
	case NET_PLAYER_ALLOC_INVALID_PLAYER_SLOT: return "invalid_player_slot";
	case NET_PLAYER_ALLOC_DUPLICATE_PLAYER_SLOT: return "duplicate_player_slot";
	case NET_PLAYER_ALLOC_MISSING_PLAYER_OBJECT: return "missing_player_object";
	case NET_PLAYER_ALLOC_INVALID_IDENTITY: return "invalid_identity";
	case NET_PLAYER_ALLOC_ROSTER_MISMATCH: return "roster_mismatch";
	default: return "unknown";
	}
}

static enum net_player_allocate_result netPlayersAllocationReject(
		enum net_player_allocate_result result, s32 client_id, s32 playernum,
		player_identity_status_e identity_status)
{
	sysLogPrintf(LOG_ERROR,
		"PLAYER.INIT.ROLLBACK phase=network status=%s client=%d player=%d identity=%s globals_published=0",
		netPlayerAllocateResultString(result), client_id, playernum,
		playerIdentityStatusString(identity_status));
	return result;
}

enum net_player_allocate_result netPlayersAllocate(
		struct player *const *candidates, s32 candidate_count)
{
	struct net_player_allocation_plan plans[MAX_PLAYERS];
	bool occupied_slots[MAX_PLAYERS] = { false };
	s32 plan_count = 0;
	s32 server_playernum = 0;
	s32 client_local_wire_playernum = -1;

	if (candidates == NULL || candidate_count <= 0) {
		return netPlayersAllocationReject(NET_PLAYER_ALLOC_MISSING_PLAYER_OBJECT,
			-1, -1, PLAYER_IDENTITY_INVALID_ARGUMENT);
	}
	if (candidate_count > MAX_PLAYERS) {
		return netPlayersAllocationReject(NET_PLAYER_ALLOC_TOO_MANY_PLAYERS,
			-1, candidate_count, PLAYER_IDENTITY_INVALID_ARGUMENT);
	}

	if (g_NetMode != NETMODE_SERVER && g_NetMode != NETMODE_CLIENT) {
		return netPlayersAllocationReject(NET_PLAYER_ALLOC_INVALID_MODE,
			-1, -1, PLAYER_IDENTITY_INVALID_ARGUMENT);
	}

	if (g_NetMode == NETMODE_CLIENT) {
		if (g_NetLocalClient == NULL) {
			return netPlayersAllocationReject(
				NET_PLAYER_ALLOC_MISSING_LOCAL_CLIENT, -1, -1,
				PLAYER_IDENTITY_INVALID_ARGUMENT);
		}
		client_local_wire_playernum = g_NetLocalClient->playernum;
	}

	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.PREFLIGHT phase=network mode=%d candidates=%d",
		g_NetMode, candidate_count);

	for (s32 i = 0; i < g_NetMaxClients; ++i) {
		struct netclient *cl = &g_NetClients[i];
		struct net_player_allocation_plan *plan;
		s32 playernum;
		player_identity_status_e identity_status;

		if (cl->state != CLSTATE_GAME || (cl->flags & CLFLAG_SPECTATOR)) {
			continue;
		}

		if (plan_count >= MAX_PLAYERS) {
			return netPlayersAllocationReject(NET_PLAYER_ALLOC_TOO_MANY_PLAYERS,
				(s32)cl->id, -1, PLAYER_IDENTITY_INVALID_ARGUMENT);
		}

		if (g_NetMode == NETMODE_SERVER) {
			playernum = server_playernum++;
		} else if (cl == g_NetLocalClient) {
			playernum = 0;
		} else if (cl == &g_NetClients[0]) {
			playernum = client_local_wire_playernum;
		} else {
			playernum = cl->playernum;
		}

		if (playernum < 0 || playernum >= candidate_count) {
			return netPlayersAllocationReject(
				NET_PLAYER_ALLOC_INVALID_PLAYER_SLOT, (s32)cl->id,
				playernum, PLAYER_IDENTITY_INVALID_ARGUMENT);
		}
		if (occupied_slots[playernum]) {
			return netPlayersAllocationReject(
				NET_PLAYER_ALLOC_DUPLICATE_PLAYER_SLOT, (s32)cl->id,
				playernum, PLAYER_IDENTITY_INVALID_ARGUMENT);
		}
		if (candidates[playernum] == NULL) {
			return netPlayersAllocationReject(
				NET_PLAYER_ALLOC_MISSING_PLAYER_OBJECT, (s32)cl->id,
				playernum, PLAYER_IDENTITY_INVALID_ARGUMENT);
		}

		plan = &plans[plan_count];
		memset(plan, 0, sizeof(*plan));
		identity_status = playerIdentityPrepare(cl->settings.body_id,
			cl->settings.head_id, &plan->identity);
		if (identity_status != PLAYER_IDENTITY_OK) {
			return netPlayersAllocationReject(NET_PLAYER_ALLOC_INVALID_IDENTITY,
				(s32)cl->id, playernum, identity_status);
		}

		plan->client = cl;
		plan->player = candidates[playernum];
		plan->playernum = (u8)playernum;
		plan->remote = cl != g_NetLocalClient;
		plan->config = g_PlayerConfigsArray[playernum];
		plan->config.base.mpbodynum = plan->identity.mp_body_index >= 0
			? (u8)plan->identity.mp_body_index : 0xFF;
		plan->config.base.mpheadnum = plan->identity.mp_head_index >= 0
			? (u8)plan->identity.mp_head_index : 0xFF;
		strncpy(plan->config.base.body_id, plan->identity.body_id,
			sizeof(plan->config.base.body_id) - 1);
		plan->config.base.body_id[sizeof(plan->config.base.body_id) - 1] = '\0';
		strncpy(plan->config.base.head_id, plan->identity.head_id,
			sizeof(plan->config.base.head_id) - 1);
		plan->config.base.head_id[sizeof(plan->config.base.head_id) - 1] = '\0';

		if (plan->remote) {
			plan->config.controlmode = CONTROLMODE_NA;
			snprintf(plan->config.base.name, sizeof(plan->config.base.name),
				"%s\n", cl->settings.name);
			plan->config.options = g_PlayerConfigsArray[0].options & OPTION_PAINTBALL;
			plan->config.options |= cl->settings.options & ~OPTION_PAINTBALL;
			plan->config.options &= ~(OPTION_AIMCONTROL | OPTION_LOOKAHEAD);
			plan->config.options |= OPTION_FORWARDPITCH | OPTION_ASKEDSAVEPLAYER;
		}
		plan->config.handicap = cl->settings.handicap;

		occupied_slots[playernum] = true;
		plan_count++;
	}

	if (plan_count != candidate_count) {
		return netPlayersAllocationReject(NET_PLAYER_ALLOC_ROSTER_MISMATCH,
			-1, plan_count, PLAYER_IDENTITY_INVALID_ARGUMENT);
	}

	for (s32 i = 0; i < plan_count; i++) {
		struct net_player_allocation_plan *plan = &plans[i];
		g_PlayerConfigsArray[plan->playernum] = plan->config;
	}

	for (s32 i = 0; i < plan_count; i++) {
		struct net_player_allocation_plan *plan = &plans[i];
		plan->client->playernum = plan->playernum;
		plan->client->config = &g_PlayerConfigsArray[plan->playernum];
		plan->client->config->client = plan->client;
		plan->client->player = plan->player;
		plan->player->client = plan->client;
		plan->player->isremote = plan->remote;
		sysLogPrintf(LOG_NOTE,
			"NET: allocated playernum=%d body='%s' head='%s' name=%s",
			plan->playernum, plan->identity.body_id, plan->identity.head_id,
			plan->client->settings.name);
	}

	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.COMMIT phase=network players=%d", plan_count);
	return NET_PLAYER_ALLOC_OK;
}

void netSyncIdsAllocate(void)
{
	u8 *free_slots;
	struct prop *prop;
	s32 free_count = 0;
	/* Setup props use their deterministic one-based prop-slot offset. Runtime
	 * props start strictly after the greatest setup ID. Build allocation truth
	 * from the free list so parented inventory/held-weapon children receive IDs
	 * too; active/paused-list-only scans silently omitted those props. */
	u32 max_initial_syncid = 0;
	g_NetNextSyncId = 1;
	g_NetFirstDynamicSyncId = 1;

	// don't allocate anything else if we're in lobby
	if (g_StageNum == STAGE_TITLE || g_StageNum == STAGE_CITRAINING) {
		return;
	}
	if (!g_Vars.props || g_Vars.maxprops <= 0) {
		sysLogPrintf(LOG_ERROR,
			"NET: cannot allocate sync IDs without a prop pool");
		netDisconnect();
		return;
	}
	free_slots = (u8 *)calloc((size_t)g_Vars.maxprops,
		sizeof(*free_slots));
	if (!free_slots) {
		sysLogPrintf(LOG_ERROR,
			"NET: cannot allocate sync-ID free-list preflight");
		netDisconnect();
		return;
	}
	prop = g_Vars.freeprops;
	while (prop && free_count < g_Vars.maxprops) {
		const uintptr_t pool_begin = (uintptr_t)g_Vars.props;
		const uintptr_t pool_end = pool_begin
			+ (size_t)g_Vars.maxprops * sizeof(*g_Vars.props);
		const uintptr_t current = (uintptr_t)prop;
		s32 slot;
		if (current < pool_begin || current >= pool_end
				|| (current - pool_begin) % sizeof(*g_Vars.props) != 0) {
			free(free_slots);
			sysLogPrintf(LOG_ERROR,
				"NET: malformed prop free list during sync-ID allocation");
			netDisconnect();
			return;
		}
		slot = (s32)((current - pool_begin) / sizeof(*g_Vars.props));
		if (free_slots[slot]) {
			free(free_slots);
			sysLogPrintf(LOG_ERROR,
				"NET: duplicate prop in free list during sync-ID allocation");
			netDisconnect();
			return;
		}
		free_slots[slot] = 1;
		free_count++;
		prop = prop->next;
	}
	if (prop) {
		free(free_slots);
		sysLogPrintf(LOG_ERROR,
			"NET: cyclic prop free list during sync-ID allocation");
		netDisconnect();
		return;
	}
	for (s32 i = 0; i < g_Vars.maxprops; ++i) {
		if (free_slots[i]) {
			g_Vars.props[i].syncid = 0;
			continue;
		}
		g_Vars.props[i].syncid = (u32)i + 1;
		max_initial_syncid = (u32)i + 1;
	}
	free(free_slots);

	// HACK: when we're a client, we'll need to swap our player and server player's props
	// because of what we do in netPlayersAllocate
	if (g_NetMode == NETMODE_CLIENT) {
		if (!g_NetLocalClient->player || !g_NetLocalClient->player->prop
				|| !g_NetClients[0].player
				|| !g_NetClients[0].player->prop) {
			sysLogPrintf(LOG_ERROR, "NET: no props allocated for players?");
			netDisconnect();
			return;
		}
		const u16 sid = g_NetClients[0].player->prop->syncid;
		g_NetClients[0].player->prop->syncid = g_NetLocalClient->player->prop->syncid;
		g_NetLocalClient->player->prop->syncid = sid;
	}

	g_NetFirstDynamicSyncId = max_initial_syncid + 1;
	g_NetNextSyncId = g_NetFirstDynamicSyncId;
	sysLogPrintf(LOG_NOTE,
		"NET: last initial syncid=%u first dynamic syncid=%u",
		(unsigned)max_initial_syncid,
		(unsigned)g_NetFirstDynamicSyncId);
	netSyncIdMapRebuild(); // rebuild O(1) lookup map after all syncids are finalised
}

void netChatPrintf(struct netclient *dst, const char *fmt, ...)
{
	char tmp[512];
	u8 bufdata[600];
	struct netbuf buf = { NULL };

	if (!g_NetMode || !g_NetLocalClient || g_NetLocalClient->state < CLSTATE_LOBBY) {
		return;
	}

	va_list args;
	va_start(args, fmt);
	vsnprintf(tmp, sizeof(tmp) - 1, fmt, args);
	va_end(args);

	buf.data = bufdata;
	buf.size = sizeof(bufdata);

	if (g_NetMode == NETMODE_SERVER) {
		sysLogPrintf(LOG_CHAT, "%s", tmp);
		netmsgSvcChatWrite(&buf, tmp);
	} else {
		netmsgClcChatWrite(&buf, tmp);
	}

	netSend(dst, &buf, true, NETCHAN_CONTROL);
}

void netChat(struct netclient *dst, const char *text)
{
	if (g_NetMode && g_NetLocalClient) {
		netChatPrintf(dst, "%s: %s", g_NetLocalClient->settings.name, text);
	}
}

Gfx *netDebugRender(Gfx *gdl)
{
	char tmp[384];

	if (!g_NetMode || !g_NetDebugDraw) {
		return gdl;
	}

	if (!g_CharsHandelGothicXs || !g_FontHandelGothicXs) {
		return gdl;
	}

	gdl = text0f153628(gdl);
	gSPSetExtraGeometryModeEXT(gdl++, G_ASPECT_LEFT_EXT);

	s32 x = 2;
	s32 y = viGetHeight() - 1 - 6*8;
	snprintf(tmp, sizeof(tmp), "Nettick: %u\nPing: %u\nSent: %u\nRecv: %u\nReliable frame: %u\nUnreliable frame: %u\n",
		g_NetTick, g_NetLocalClient->peer ? enet_peer_get_rtt(g_NetLocalClient->peer) : 0,
		enet_host_get_bytes_sent(g_NetHost), enet_host_get_bytes_received(g_NetHost),
		g_NetReliableFrameLen, g_NetUnreliableFrameLen);
	gdl = textRenderProjected(gdl, &x, &y, tmp, g_CharsHandelGothicXs, g_FontHandelGothicXs, 0x00ff00ff, viGetWidth(), viGetHeight(), 0, 0);

	gSPClearExtraGeometryModeEXT(gdl++, G_ASPECT_CENTER_EXT);
	gdl = text0f153780(gdl);

	return gdl;
}

/* SEC-7 (client side): detect a 17-byte challenge response and re-send the
 * query with the token appended. Returns 1 if the packet was a challenge
 * (handled here), 0 if it should fall through to the normal info parser. */
static s32 netClientHandleQueryChallenge(ENetSocket sock, const ENetAddress *addr,
                                          const u8 *data, s32 len)
{
	const s32 magic_len = (s32)(sizeof(NET_QUERY_MAGIC) - 1);
	if (len != magic_len + 4 + (s32)NET_QUERY_TOKEN_LEN) return 0;
	if (memcmp(data, NET_QUERY_MAGIC, magic_len) != 0) return 0;
	u32 marker;
	memcpy(&marker, data + magic_len, sizeof(marker));
	if (marker != NET_QUERY_CHALLENGE_MARKER) return 0;

	u8 buf[16 + NET_QUERY_TOKEN_LEN];
	memcpy(buf, NET_QUERY_MAGIC, magic_len);
	memcpy(buf + magic_len, data + magic_len + 4, NET_QUERY_TOKEN_LEN);

	ENetBuffer ebuf;
	ebuf.data       = buf;
	ebuf.dataLength = magic_len + NET_QUERY_TOKEN_LEN;
	enet_socket_send(sock, (ENetAddress *)addr, &ebuf, 1);
	return 1;
}

void netRecentServerAdd(const char *addr)
{
	if (!addr || !addr[0]) {
		return;
	}
	if (!netStoredAddrIsValid(addr)) {
		sysLogPrintf(LOG_WARNING, "NET: not storing invalid recent server address `%s`", addr);
		return;
	}

	// check if already in list
	for (s32 i = 0; i < g_NetNumRecentServers; ++i) {
		if (strncasecmp(g_NetRecentServers[i].addr, addr, NET_MAX_ADDR) == 0) {
			return; // already present
		}
	}

	// add to the list, evicting oldest if full
	struct netrecentserver *srv;
	if (g_NetNumRecentServers < NET_MAX_RECENT_SERVERS) {
		srv = &g_NetRecentServers[g_NetNumRecentServers++];
	} else {
		// shift everything down, newest at end
		memmove(&g_NetRecentServers[0], &g_NetRecentServers[1],
			sizeof(struct netrecentserver) * (NET_MAX_RECENT_SERVERS - 1));
		srv = &g_NetRecentServers[NET_MAX_RECENT_SERVERS - 1];
	}

	memset(srv, 0, sizeof(*srv));
	strncpy(srv->addr, addr, NET_MAX_ADDR - 1);
	srv->addr[NET_MAX_ADDR - 1] = '\0';
}

void netRecentServerUpdate(const char *addr, const u8 *data, s32 len)
{
	if (!addr || !data || len < 12) {
		return;
	}

	for (s32 i = 0; i < g_NetNumRecentServers; ++i) {
		struct netrecentserver *srv = &g_NetRecentServers[i];
		if (strncasecmp(srv->addr, addr, NET_MAX_ADDR) == 0) {
			struct netbuf buf = { 0 };
			u32 protocol = 0;
			u8 flags = 0;
			u8 numclients = 0;
			u8 maxclients = 0;
			u8 stagenum = 0;
			u8 scenario = 0;
			char scenario_id[CATALOG_ID_LEN] = "";
			char hostname[NET_MAX_NAME] = "";

			netbufStartReadData(&buf, data, len);

			// skip magic + size
			{
				u8 skip[8];
				netbufReadData(&buf, skip, sizeof(NET_QUERY_MAGIC) - 1);
			}
			netbufReadU16(&buf);

			protocol = netbufReadU32(&buf);
			flags = netbufReadU8(&buf);
			numclients = netbufReadU8(&buf);
			maxclients = netbufReadU8(&buf);
			stagenum = netbufReadU8(&buf);
			/* M0.1d: scenario as catalog ID string (v32+). */
			{
				const char *scid = netbufReadStr(&buf);
				if (scid && scid[0]) {
					strncpy(scenario_id, scid, sizeof(scenario_id) - 1);
					scenario_id[sizeof(scenario_id) - 1] = '\0';
					const asset_entry_t *gm = assetCatalogResolve(scenario_id);
					scenario = (gm && gm->type == ASSET_GAMEMODE) ? (u8)gm->ext.gamemode.mode_id : 0;
				}
			}
			const char *hostname_wire = netbufReadStr(&buf);
			if (hostname_wire) {
				strncpy(hostname, hostname_wire, sizeof(hostname) - 1);
				hostname[sizeof(hostname) - 1] = '\0';
			}

			if (buf.error) {
				sysLogPrintf(LOG_WARNING, "NET: ignoring malformed recent-server response from %s", addr);
				return;
			}

			srv->protocol = protocol;
			srv->flags = flags;
			srv->numclients = numclients;
			srv->maxclients = maxclients;
			srv->stagenum = stagenum;
			srv->scenario = scenario;
			strncpy(srv->scenario_id, scenario_id, sizeof(srv->scenario_id) - 1);
			srv->scenario_id[sizeof(srv->scenario_id) - 1] = '\0';
			strncpy(srv->hostname, hostname, NET_MAX_NAME - 1);
			srv->hostname[NET_MAX_NAME - 1] = '\0';
			srv->lastresponse = (u32)time(NULL);
			srv->online = true;
			return;
		}
	}
}

void netQueryRecentServers(void)
{
	if (!g_NetInit) {
		return;
	}

	// mark all as offline pending response
	for (s32 i = 0; i < g_NetNumRecentServers; ++i) {
		g_NetRecentServers[i].online = false;
	}

	ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
	if (sock == ENET_SOCKET_NULL) {
		return;
	}

	// set a short receive timeout so we don't block the game
	enet_socket_set_option(sock, ENET_SOCKOPT_RCVTIMEO, 200);

	for (s32 i = 0; i < g_NetNumRecentServers; ++i) {
		struct netrecentserver *srv = &g_NetRecentServers[i];
		ENetAddress addr;
		if (netParseAddr(&addr, srv->addr)) {
			ENetBuffer ebuf;
			ebuf.data = (void *)NET_QUERY_MAGIC;
			ebuf.dataLength = sizeof(NET_QUERY_MAGIC) - 1;
			enet_socket_send(sock, &addr, &ebuf, 1);
		}
	}

	/* try to receive responses (blocking up to 200ms per attempt).
	 * SEC-7: each server now answers in two packets — a small challenge,
	 * followed by the full info reply once we echo the token.  Allow up to
	 * 2x the server count so we don't drop full responses behind challenges. */
	for (s32 attempt = 0; attempt < (g_NetNumRecentServers * 2); ++attempt) {
		u8 rxdata[512];
		ENetBuffer rxbuf;
		ENetAddress rxaddr;
		rxbuf.data = rxdata;
		rxbuf.dataLength = sizeof(rxdata);
		s32 rxlen = enet_socket_receive(sock, &rxaddr, &rxbuf, 1);
		if (rxlen > 0) {
			/* SEC-7: handle two-stage handshake transparently. */
			if (netClientHandleQueryChallenge(sock, &rxaddr, rxdata, rxlen)) {
				continue;
			}
			const char *addrstr = netFormatAddr(&rxaddr);
			if (addrstr) {
				netRecentServerUpdate(addrstr, rxdata, rxlen);
			}
		} else {
			break; // no more responses
		}
	}

	enet_socket_destroy(sock);
}

void netQueryRecentServersAsync(void)
{
	if (!g_NetInit) {
		return;
	}

	// close any in-flight query socket from a previous call
	if (g_NetQuerySocket != ENET_SOCKET_NULL) {
		enet_socket_destroy(g_NetQuerySocket);
		g_NetQuerySocket = ENET_SOCKET_NULL;
	}
	g_NetQueryInFlight = false;

	// mark all servers offline pending fresh responses
	for (s32 i = 0; i < g_NetNumRecentServers; ++i) {
		g_NetRecentServers[i].online = false;
	}

	if (g_NetNumRecentServers == 0) {
		return;
	}

	ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
	if (sock == ENET_SOCKET_NULL) {
		return;
	}

	enet_socket_set_option(sock, ENET_SOCKOPT_NONBLOCK, 1);

	for (s32 i = 0; i < g_NetNumRecentServers; ++i) {
		ENetAddress addr;
		if (netParseAddr(&addr, g_NetRecentServers[i].addr)) {
			ENetBuffer ebuf;
			ebuf.data = (void *)NET_QUERY_MAGIC;
			ebuf.dataLength = sizeof(NET_QUERY_MAGIC) - 1;
			enet_socket_send(sock, &addr, &ebuf, 1);
		}
	}

	g_NetQuerySocket = sock;
	g_NetQueryStartMs = enet_time_get();
	g_NetQueryInFlight = true;
}

void netPollRecentServers(void)
{
	if (!g_NetQueryInFlight || g_NetQuerySocket == ENET_SOCKET_NULL) {
		return;
	}

	// drain all currently available packets without blocking
	u8 rxdata[512];
	ENetBuffer rxbuf;
	ENetAddress rxaddr;
	for (;;) {
		rxbuf.data = rxdata;
		rxbuf.dataLength = sizeof(rxdata);
		s32 rxlen = enet_socket_receive(g_NetQuerySocket, &rxaddr, &rxbuf, 1);
		if (rxlen <= 0) {
			break;
		}
		/* SEC-7: handle two-stage handshake transparently. */
		if (netClientHandleQueryChallenge(g_NetQuerySocket, &rxaddr, rxdata, rxlen)) {
			continue;
		}
		const char *addrstr = netFormatAddr(&rxaddr);
		if (addrstr) {
			netRecentServerUpdate(addrstr, rxdata, rxlen);
		}
	}

	// close socket once the response window has elapsed
	if (enet_time_get() - g_NetQueryStartMs >= NET_QUERY_TIMEOUT_MS) {
		enet_socket_destroy(g_NetQuerySocket);
		g_NetQuerySocket = ENET_SOCKET_NULL;
		g_NetQueryInFlight = false;
	}
}

PD_CONSTRUCTOR static void netConfigInit(void)
{
	/* S313 batch: network tuning rates + server port are no longer
	 * persisted to pd.ini.  Defaults in the g_Net*Rate variable
	 * initializers at file scope are the source of truth.  The server
	 * port can still be overridden at runtime via the `-port` CLI
	 * argument (see netStartServer).  Users who truly need different
	 * rates/interp ticks should adjust at build time rather than
	 * fiddling pd.ini -- these are tuning knobs for engine work, not
	 * user prefs.
	 *
	 * Retained here are per-machine state (LastJoinAddr) and history
	 * (RecentServer.*) which genuinely vary session-to-session, plus
	 * the ops-level AllowInfoQuery toggle for dedicated servers. */

	configRegisterString("Net.Client.LastJoinAddr", g_NetLastJoinAddr, NET_MAX_ADDR);

	/* Listen-server default port (game client “Host / Go online”). CLI `--port` still overrides in netInit(). */
	configRegisterUInt("Net.Server.Port", &g_NetServerPort, 1, 65535);

	configRegisterInt("Net.Server.AllowInfoQuery", &g_NetServerInfoQuery, 0, 1);
	configRegisterUInt("Net.UploadKbpsEstimate", &s_NetUploadKbps, 0,
		NET_UPLOAD_KBPS_MAX);
	configRegisterUInt("Net.UploadKbpsMeasuredAt", &s_NetUploadMeasuredAtUnix,
		0, 0xffffffffu);

	// register recent server fields for persistence
	static char recentAddrKeys[NET_MAX_RECENT_SERVERS][32];
	static char recentHostKeys[NET_MAX_RECENT_SERVERS][36];
	static char recentTimeKeys[NET_MAX_RECENT_SERVERS][36];
	for (s32 i = 0; i < NET_MAX_RECENT_SERVERS; ++i) {
		snprintf(recentAddrKeys[i], sizeof(recentAddrKeys[i]), "Net.RecentServer.%d", i);
		configRegisterString(recentAddrKeys[i], g_NetRecentServers[i].addr, NET_MAX_ADDR);
		snprintf(recentHostKeys[i], sizeof(recentHostKeys[i]), "Net.RecentServer.%d.Host", i);
		configRegisterString(recentHostKeys[i], g_NetRecentServers[i].hostname, NET_MAX_NAME - 1);
		snprintf(recentTimeKeys[i], sizeof(recentTimeKeys[i]), "Net.RecentServer.%d.Time", i);
		configRegisterUInt(recentTimeKeys[i], &g_NetRecentServers[i].lastresponse, 0, 0xFFFFFFFF);
	}
	configRegisterInt("Net.RecentServerCount", &g_NetNumRecentServers, 0, NET_MAX_RECENT_SERVERS);
}

/**
 * B-126 diagnostics — log a snapshot of all connected peer states.
 * Called from lv.c every 30s (1800 frames).  Covers the gap between the
 * silent-crash onset and the last heartbeat: if the process dies after this
 * fires, the log shows exactly what the net layer looked like 0–30s before.
 *
 * NET.WATCHDOG  — per-peer: name, CLSTATE, RTT, ms-since-last-packet, ENet peer state
 * NET.HEARTBEAT — aggregate: sent/recv bytes, packet counts, mode
 */
void netHeartbeatLog(void)
{
	ENetHost *host = g_NetHost; /* static in this TU — avoids the call overhead */
	s32 connected = 0;
	s32 i;

	if (host) {
		uint32_t now = host->serviceTime;

		for (i = 0; i < g_NetMaxClients; i++) {
			struct netclient *cl = &g_NetClients[i];

			if (cl->state == CLSTATE_DISCONNECTED || !cl->peer) {
				continue;
			}

			++connected;

			/* since_rx: ms since last ENet packet from this peer.
			 * Monotonic within a session; large value = peer gone silent. */
			uint32_t since_rx = (now >= cl->peer->lastReceiveTime)
				? (now - cl->peer->lastReceiveTime) : 0;

			sysLogPrintf(LOG_NOTE,
				"NET.WATCHDOG: cl[%d] \"%s\" clstate=%u rtt=%ums since_rx=%ums enet_state=%d",
				i, cl->settings.name, cl->state,
				(unsigned)cl->peer->roundTripTime, (unsigned)since_rx,
				(int)cl->peer->state);
		}
	}

	sysLogPrintf(LOG_NOTE,
		"NET.HEARTBEAT: mode=%d gamemode=%u clients=%d/%d connected=%d nettick=%u",
		g_NetMode, (unsigned)g_NetGameMode,
		g_NetNumClients, g_NetMaxClients, connected, g_NetTick);

	if (host) {
		sysLogPrintf(LOG_NOTE,
			"NET.HEARTBEAT: sent=%ukB/%upkts recv=%ukB/%upkts",
			host->totalSentData / 1024, host->totalSentPackets,
			host->totalReceivedData / 1024, host->totalReceivedPackets);
	}
}
