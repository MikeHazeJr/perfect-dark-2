#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include "platform.h"
#include "net/netenet.h"
#include "net/net.h"
#include "net/netbuf.h"
#include "net/netmsg.h"
#include "net/netupnp.h"
#include "net/netlobby.h"
#include "types.h"
#include "constants.h"
#include "data.h"
#include "bss.h"
#include "game/hudmsg.h"
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
#include "console.h"
#include "fs.h"
#include "romdata.h"
#include "utils.h"

s32 g_NetMode = NETMODE_NONE;

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

u8 g_NetGameMode = NETGAMEMODE_MP;
u8 g_NetCoopDifficulty = 0;
u8 g_NetCoopFriendlyFire = 0;
u8 g_NetCoopRadar = 1;

static u8 g_NetMsgBuf[NET_BUFSIZE];
struct netbuf g_NetMsg = { .data = g_NetMsgBuf, .size = sizeof(g_NetMsgBuf) };

static u8 g_NetMsgRelBuf[NET_BUFSIZE * 4]; // reliable buffer can be reliably fragmented
struct netbuf g_NetMsgRel = { .data = g_NetMsgRelBuf, .size = sizeof(g_NetMsgRelBuf) };

static s32 g_NetInit = false;
static ENetHost *g_NetHost;
static ENetAddress g_NetLocalAddr;
static ENetAddress g_NetRemoteAddr;

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
	if (portval < 0 || portval > 0xFFFF) {
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

static const char *netFormatAddr(const ENetAddress *addr)
{
	static char str[256];
	char tmp[256];
	if (addr && enet_address_get_ip(addr, tmp, sizeof(tmp) - 1) == 0) {
		if (tmp[0]) {
			if (strchr(tmp, ':')) {
				// ipv6
				snprintf(str, sizeof(str) - 1, "[%s]:%u", tmp, addr->port);
			} else {
				// ipv4
				snprintf(str, sizeof(str) - 1, "%s:%u", tmp, addr->port);
			}
			return str;
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

static inline void netClientReset(struct netclient *cl)
{
	if (cl->state >= CLSTATE_GAME && cl->player) {
		cl->player->client = NULL;
		cl->player->isremote = false;
	}
	memset(cl, 0, sizeof(*cl));
	cl->out.data = cl->out_data;
	cl->out.size = sizeof(cl->out_data);
	cl->id = cl - g_NetClients;
	cl->settings.team = 0xff;
}

static inline void netClientResetAll(void)
{
	g_NetMaxClients = NET_MAX_CLIENTS;
	g_NetNumClients = 1; // always at least one client, which is us
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
	cl->settings.bodynum = g_PlayerConfigsArray[playernum].base.mpbodynum;
	cl->settings.headnum = g_PlayerConfigsArray[playernum].base.mpheadnum;
	cl->settings.team = g_PlayerConfigsArray[playernum].base.team;
	cl->settings.fovy = g_PlayerExtCfg[playernum].fovy;
	cl->settings.fovzoommult = g_PlayerExtCfg[playernum].fovzoommult;
	memcpy(cl->settings.name, g_PlayerConfigsArray[playernum].base.name, sizeof(cl->settings.name));
	// the \n will be readded in the playerconfig
	char *newline = strrchr(g_NetLocalClient->settings.name, '\n');
	if (newline) {
		*newline = '\0';
	}
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
		"Player left the game"
	};
	if (reason < (u32)ARRAYCOUNT(msgs)) {
		return msgs[reason];
	}
	return msgs[0];
}

static void netServerQueryResponse(ENetAddress *address)
{
	static u8 data[256];
	static ENetBuffer ebuf;
	struct netbuf buf = { .data = data, .size = sizeof(data) };
	const u8 flags = (g_NetLocalClient && g_NetLocalClient->state > CLSTATE_LOBBY)
		| (0 << 1); // TODO: this will indicate coop/anti/etc
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
	netbufWriteU8(&buf, g_MpSetup.scenario);
	netbufWriteStr(&buf, g_NetLocalClient ? g_NetLocalClient->settings.name : "");
	netbufWriteStr(&buf, g_RomName);
	netbufWriteStr(&buf, modDir);
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
	if (rxdata && rxlen >= 5) {
		if (!memcmp(rxdata, NET_QUERY_MAGIC, sizeof(NET_QUERY_MAGIC) - 1)) {
			// this is a query packet, respond with server status
			sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON, "NET: query request from %s, responding", netFormatAddr(address));
			netServerQueryResponse(address);
			return 1;
		}
	}
	// probably a normal packet, pass through to enet
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

	g_NetInit = true;
}

s32 netStartServer(u16 port, s32 maxclients)
{
	if (g_NetMode || !g_NetInit) {
		return -1;
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
		return -2;
	}

	if (g_NetServerInfoQuery) {
		enet_host_set_intercept_callback(g_NetHost, netServerConnectionlessPacket);
	}

	netClientResetAll();
	g_NetMaxClients = maxclients;

	// the server's local client is client 0
	g_NetLocalClient = &g_NetClients[0];
	g_NetLocalClient->state = CLSTATE_LOBBY; // local client doesn't need auth
	netClientReadConfig(g_NetLocalClient, 0);

	g_NetMode = NETMODE_SERVER;

	g_NetTick = 0;
	g_NetNextUpdate = 0;
	g_NetNextSyncId = 1;

	sysLogPrintf(LOG_NOTE, "NET: using protocol version %d", NET_PROTOCOL_VER);
	sysLogPrintf(LOG_NOTE, "NET: created server on port %u", port);

	/* Start async UPnP port forwarding (runs on background thread).
	 * Returns immediately — discovery takes 2-5 seconds on the thread.
	 * Poll netUpnpGetStatus() or check the lobby overlay for results. */
	netUpnpSetup(port);

	lobbyInit();

	return 0;
}

void netServerStageStart(void)
{
	if (g_NetMode != NETMODE_SERVER) {
		return;
	}

	if (g_StageNum == STAGE_TITLE || g_StageNum == STAGE_CITRAINING) {
		g_NetLocalClient->state = CLSTATE_LOBBY;
		return;
	}

	// re-read the player config in case it changed
	netClientReadConfig(g_NetLocalClient, 0);

	/* Transition ALL connected clients to CLSTATE_GAME.
	 * The server must do this before stage initialization code runs,
	 * because the init code assumes all clients are in GAME state.
	 * Remote clients will also transition on their end when they
	 * receive and process SVC_STAGE_START. */
	for (s32 ci = 0; ci <= NET_MAX_CLIENTS; ci++) {
		if (g_NetClients[ci].state == CLSTATE_LOBBY) {
			g_NetClients[ci].state = CLSTATE_GAME;
		}
	}

	sysLogPrintf(LOG_NOTE, "NET: === STAGE START === stage=0x%02x scenario=%u clients=%d",
	             g_StageNum, g_MpSetup.scenario, g_NetNumClients);

	// Log each connected client's state for debugging
	for (s32 ci = 0; ci <= NET_MAX_CLIENTS; ci++) {
		if (g_NetClients[ci].state != CLSTATE_DISCONNECTED) {
			sysLogPrintf(LOG_NOTE, "NET:   client %u '%s' state=%u playernum=%u head=%u body=%u team=%u",
			             g_NetClients[ci].id, g_NetClients[ci].settings.name,
			             g_NetClients[ci].state, g_NetClients[ci].playernum,
			             g_NetClients[ci].settings.headnum, g_NetClients[ci].settings.bodynum,
			             g_NetClients[ci].settings.team);
		}
	}

	// clear preserved players from previous rounds
	memset(g_NetPreservedPlayers, 0, sizeof(g_NetPreservedPlayers));
	g_NetNumPreserved = 0;

	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcStageStartWrite(&g_NetMsgRel);
	netSend(NULL, &g_NetMsgRel, true, NETCHAN_DEFAULT);

	// Schedule a full state resync shortly after stage start.
	// Bots and props are created deterministically from the same RNG seed,
	// but a full resync ensures all clients converge even if timing differs.
	g_NetPendingResyncFlags = NET_RESYNC_FLAG_CHRS | NET_RESYNC_FLAG_PROPS;

	sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE_START sent, resync flags=0x%x", g_NetPendingResyncFlags);
}

void netServerCoopStageStart(u8 stagenum, u8 difficulty)
{
	if (g_NetMode != NETMODE_SERVER) {
		return;
	}

	// configure mission on the server side
	g_MissionConfig.stagenum = stagenum;
	g_MissionConfig.difficulty = difficulty;
	g_MissionConfig.iscoop = (g_NetGameMode == NETGAMEMODE_COOP);
	g_MissionConfig.isanti = (g_NetGameMode == NETGAMEMODE_ANTI);

	// set up co-op player numbers
	g_Vars.bondplayernum = 0;
	if (g_NetGameMode == NETGAMEMODE_COOP) {
		g_Vars.coopplayernum = (g_NetNumClients > 1) ? 1 : -1;
		g_Vars.antiplayernum = -1;
	} else {
		g_Vars.coopplayernum = -1;
		g_Vars.antiplayernum = (g_NetNumClients > 1) ? 1 : -1;
	}

	// re-read the player config
	netClientReadConfig(g_NetLocalClient, 0);
	g_NetLocalClient->state = CLSTATE_GAME;

	// clear preserved players from previous rounds
	memset(g_NetPreservedPlayers, 0, sizeof(g_NetPreservedPlayers));
	g_NetNumPreserved = 0;

	// clear coop ready flags
	for (s32 i = 0; i < g_NetMaxClients; ++i) {
		g_NetClients[i].flags &= ~CLFLAG_COOPREADY;
	}

	// broadcast stage start to all clients
	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcStageStartWrite(&g_NetMsgRel);
	netSend(NULL, &g_NetMsgRel, true, NETCHAN_DEFAULT);

	// Log character selections for each player
	for (s32 i = 0; i < g_NetMaxClients; ++i) {
		struct netclient *cl = &g_NetClients[i];
		if (cl->state >= CLSTATE_LOBBY) {
			sysLogPrintf(LOG_NOTE, "NET: player %d (%s) body=%u head=%u",
				i, cl->settings.name, cl->settings.bodynum, cl->settings.headnum);
		}
	}

	sysLogPrintf(LOG_NOTE, "NET: starting co-op stage 0x%02x difficulty %u with %u players",
		stagenum, difficulty, g_NetNumClients);

	g_NotLoadMod = true;
	romdataFileFreeForSolo();

	// Schedule a full state resync shortly after stage start (NPCs + props)
	g_NetPendingResyncFlags = NET_RESYNC_FLAG_NPCS | NET_RESYNC_FLAG_PROPS;

	// start the mission on the server
	menuStop();
	titleSetNextStage(stagenum);
	setNumPlayers(g_NetNumClients > 1 ? 2 : 1);
	lvSetDifficulty(difficulty);
	titleSetNextMode(TITLEMODE_SKIP);
	mainChangeToStage(stagenum);

#if VERSION >= VERSION_NTSC_1_0
	viBlack(true);
#endif
}

void netServerStageEnd(void)
{
	if (g_NetMode != NETMODE_SERVER) {
		return;
	}

	sysLogPrintf(LOG_NOTE, "NET: === STAGE END === game mode=%u tick=%u", g_NetGameMode, g_NetTick);

	g_NetLocalClient->state = CLSTATE_LOBBY;

	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcStageEndWrite(&g_NetMsgRel);
	netSend(NULL, &g_NetMsgRel, true, NETCHAN_DEFAULT);

	sysLogPrintf(LOG_NOTE, "NET: SVC_STAGE_END sent, returning to lobby");
}

void netServerKick(struct netclient *cl, const u32 reason)
{
	if (g_NetMode != NETMODE_SERVER) {
		return;
	}

	if (!cl || !cl->state || !cl->peer) {
		return;
	}

	enet_peer_disconnect(cl->peer, reason);
}

s32 netStartClient(const char *addr)
{
	if (g_NetMode || !g_NetInit) {
		return -1;
	}

	if (!netParseAddr(&g_NetRemoteAddr, addr)) {
		sysLogPrintf(LOG_ERROR, "NET: `%s` is not a valid address", addr);
		return -2;
	}

	memset(&g_NetLocalAddr, 0, sizeof(g_NetLocalAddr));
	g_NetHost = enet_host_create(&g_NetLocalAddr, 1, NETCHAN_COUNT, g_NetClientInRate, g_NetClientOutRate, 0);
	if (!g_NetHost) {
		sysLogPrintf(LOG_ERROR, "NET: could not create ENet host");
		return -3;
	}

	enet_host_set_intercept_callback(g_NetHost, NULL);

	// save the address since it appears to be valid
	strncpy(g_NetLastJoinAddr, addr, NET_MAX_ADDR);
	netRecentServerAdd(addr);

	// we'll use the whole array to store what we know of other clients
	netClientResetAll();

	// for now use last client struct
	g_NetLocalClient = &g_NetClients[NET_MAX_CLIENTS];

	sysLogPrintf(LOG_NOTE, "NET: using protocol version %d", NET_PROTOCOL_VER);
	sysLogPrintf(LOG_NOTE, "NET: connecting to %s...", addr);

	g_NetLocalClient->peer = enet_host_connect(g_NetHost, &g_NetRemoteAddr, NETCHAN_COUNT, NET_PROTOCOL_VER);
	if (!g_NetLocalClient->peer) {
		sysLogPrintf(LOG_WARNING, "NET: could not connect to %s", addr);
		enet_host_destroy(g_NetHost);
		g_NetHost = NULL;
		return -4;
	}

	g_NetLocalClient->state = CLSTATE_CONNECTING;
	netClientReadConfig(g_NetLocalClient, 0);

	g_NetMode = NETMODE_CLIENT;

	g_NetTick = 0;
	g_NetNextUpdate = 0;
	g_NetNextSyncId = 1;

	sysLogPrintf(LOG_NOTE, "NET: waiting for response from %s...", addr);

	lobbyInit();

	return 0;
}

s32 netDisconnect(void)
{
	if (!g_NetMode) {
		return -1;
	}

	// stop responding to connectionless packets
	enet_host_set_intercept_callback(g_NetHost, NULL);

	const bool wasingame = (g_NetLocalClient->state >= CLSTATE_GAME);

	for (s32 i = 0; i < NET_MAX_CLIENTS + 1; ++i) {
		if (g_NetClients[i].peer) {
			enet_peer_disconnect_now(g_NetClients[i].peer, DISCONNECT_SHUTDOWN);
		}
		netClientReset(&g_NetClients[i]);
	}

	g_NetLocalClient = &g_NetClients[NET_MAX_CLIENTS];

	// flush pending packets
	enet_host_flush(g_NetHost);

	// service for a bit just to ensure disconnect gets to peer(s)
	enet_host_service(g_NetHost, NULL, 10);

	enet_host_destroy(g_NetHost);

	/* Clean up UPnP port mapping if we were the server */
	netUpnpTeardown();

	g_NetHost = NULL;
	g_NetMode = NETMODE_NONE;
	g_NetGameMode = NETGAMEMODE_MP;

	sysLogPrintf(LOG_CHAT, "NET: disconnected");

	if (wasingame) {
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
		g_MpSetup.chrslots = 1;
		g_Vars.mplayerisrunning = false;
		g_Vars.normmplayerisrunning = false;
		g_Vars.lvmpbotlevel = 0;
		titleSetNextStage(STAGE_CITRAINING);
		setNumPlayers(1);
		titleSetNextMode(TITLEMODE_SKIP);
		mainChangeToStage(STAGE_CITRAINING);
	}

	return 0;
}

void netServerPreservePlayer(struct netclient *cl)
{
	if (!cl || !cl->settings.name[0] || cl->playernum >= MAX_PLAYERS) {
		return;
	}

	// find an existing entry for this name or an empty slot
	struct netpreservedplayer *pp = NULL;
	for (s32 i = 0; i < NET_MAX_CLIENTS; ++i) {
		if (g_NetPreservedPlayers[i].active &&
			strncasecmp(g_NetPreservedPlayers[i].name, cl->settings.name, NET_MAX_NAME) == 0) {
			pp = &g_NetPreservedPlayers[i];
			break;
		}
	}
	if (!pp) {
		for (s32 i = 0; i < NET_MAX_CLIENTS; ++i) {
			if (!g_NetPreservedPlayers[i].active) {
				pp = &g_NetPreservedPlayers[i];
				++g_NetNumPreserved;
				break;
			}
		}
	}

	if (!pp) {
		sysLogPrintf(LOG_WARNING, "NET: no room to preserve player %s", cl->settings.name);
		return;
	}

	strncpy(pp->name, cl->settings.name, NET_MAX_NAME - 1);
	pp->playernum = cl->playernum;
	pp->team = cl->settings.team;

	// copy score data from the player config
	struct mpchrconfig *mpchr = &g_PlayerConfigsArray[cl->playernum].base;
	memcpy(pp->killcounts, mpchr->killcounts, sizeof(pp->killcounts));
	pp->numdeaths = mpchr->numdeaths;
	pp->numpoints = mpchr->numpoints;
	pp->active = true;
	pp->preserveframe = g_NetTick;

	sysLogPrintf(LOG_NOTE, "NET: preserved player %s (playernum %u, %d kills, %d deaths)",
		pp->name, pp->playernum, pp->numpoints, pp->numdeaths);
}

struct netpreservedplayer *netServerFindPreserved(const char *name)
{
	if (!name || !name[0]) {
		return NULL;
	}
	for (s32 i = 0; i < NET_MAX_CLIENTS; ++i) {
		if (g_NetPreservedPlayers[i].active &&
			strncasecmp(g_NetPreservedPlayers[i].name, name, NET_MAX_NAME) == 0) {
			return &g_NetPreservedPlayers[i];
		}
	}
	return NULL;
}

void netServerRestorePreserved(struct netclient *cl, struct netpreservedplayer *pp)
{
	if (!cl || !pp || pp->playernum >= MAX_PLAYERS) {
		return;
	}

	cl->playernum = pp->playernum;
	cl->settings.team = pp->team;

	// restore score data to the player config
	struct mpchrconfig *mpchr = &g_PlayerConfigsArray[pp->playernum].base;
	memcpy(mpchr->killcounts, pp->killcounts, sizeof(mpchr->killcounts));
	mpchr->numdeaths = pp->numdeaths;
	mpchr->numpoints = pp->numpoints;

	// re-link the player struct
	cl->config = &g_PlayerConfigsArray[pp->playernum];
	cl->config->client = cl;
	cl->player = g_Vars.players[pp->playernum];
	if (cl->player) {
		cl->player->client = cl;
		cl->player->isremote = true;

		// if the player was killed on disconnect, schedule a respawn
		if (cl->player->isdead) {
			cl->player->dostartnewlife = true;
			sysLogPrintf(LOG_NOTE, "NET: scheduling respawn for reconnected player %s", pp->name);
		}
	}

	// apply settings to the config
	struct mpplayerconfig *cfg = cl->config;
	cfg->controlmode = CONTROLMODE_NA;
	cfg->base.mpbodynum = cl->settings.bodynum;
	cfg->base.mpheadnum = cl->settings.headnum;
	snprintf(cfg->base.name, sizeof(cfg->base.name), "%s\n", cl->settings.name);
	cfg->options = g_PlayerConfigsArray[0].options & OPTION_PAINTBALL;
	cfg->options |= cl->settings.options & ~OPTION_PAINTBALL;
	cfg->options &= ~(OPTION_AIMCONTROL | OPTION_LOOKAHEAD);
	cfg->options |= OPTION_FORWARDPITCH | OPTION_ASKEDSAVEPLAYER;

	cl->state = CLSTATE_GAME;

	// clear the preserved slot
	pp->active = false;
	--g_NetNumPreserved;

	sysLogPrintf(LOG_NOTE, "NET: restored player %s to playernum %u", cl->settings.name, pp->playernum);
}

static void netServerEvConnect(ENetPeer *peer, const u32 data)
{
	const char *addrstr = netFormatPeerAddr(peer);

	sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON, "NET: connection attempt from %s", addrstr);

	if (data != NET_PROTOCOL_VER) {
		sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON, "NET: %s rejected: protocol mismatch", addrstr);
		enet_peer_disconnect(peer, DISCONNECT_VERSION);
		return;
	}

	const bool ingame = (g_NetLocalClient && g_NetLocalClient->state > CLSTATE_LOBBY);

	struct netclient *cl = NULL;

	// id 0 is the local client
	for (s32 i = 1; i < g_NetMaxClients; ++i) {
		if (!g_NetClients[i].state) {
			cl = &g_NetClients[i];
			break;
		}
	}

	if (!cl) {
		sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON, "NET: %s rejected: server is full", addrstr);
		enet_peer_disconnect(peer, DISCONNECT_FULL);
		return;
	}

	if (ingame && g_NetNumPreserved == 0) {
		sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON, "NET: %s rejected: game in progress, no preserved slots", addrstr);
		enet_peer_disconnect(peer, DISCONNECT_LATE);
		return;
	}

	++g_NetNumClients;

	netClientReset(cl);
	cl->state = CLSTATE_AUTH; // skip CLSTATE_CONNECTING, since we already know it connected
	cl->peer = peer;
	cl->flags = ingame ? CLFLAG_ABSENT : 0; // mark as pending reconnect if mid-game
	enet_peer_set_data(peer, cl);
}

static void netServerEvDisconnect(struct netclient *cl)
{
	sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON, "NET: disconnect event from %s", netFormatClientAddr(cl));

	if (cl->peer) {
		enet_peer_reset(cl->peer);
	}

	// if client was in a game, preserve their identity and scores for reconnection,
	// then kill their character so it doesn't stand idle as a free target
	if (cl->state >= CLSTATE_GAME && cl->settings.name[0]) {
		netServerPreservePlayer(cl);

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

	if (cl->settings.name[0]) {
		sysLogPrintf(LOG_NOTE, "NET: client %u (%s) disconnected", cl->id, cl->settings.name);
		netChatPrintf(NULL, "%s disconnected", cl->settings.name);
	} else {
		sysLogPrintf(LOG_CHAT, "NET: client %u disconnected", cl->id);
	}

	netClientReset(cl);

	--g_NetNumClients;
}

static void netServerEvReceive(struct netclient *cl)
{
	u32 rc = 0;
	u8 msgid = 0;

	while (!rc && netbufReadLeft(&cl->in) > 0) {
		msgid = netbufReadU8(&cl->in);
		switch (msgid) {
			case CLC_NOP: rc = 0; break;
			case CLC_AUTH: rc = netmsgClcAuthRead(&cl->in, cl); break;
			case CLC_CHAT: rc = netmsgClcChatRead(&cl->in, cl); break;
			case CLC_MOVE: rc = netmsgClcMoveRead(&cl->in, cl); break;
			case CLC_SETTINGS: rc = netmsgClcSettingsRead(&cl->in, cl); break;
			case CLC_RESYNC_REQ: rc = netmsgClcResyncReqRead(&cl->in, cl); break;
			case CLC_COOP_READY: rc = netmsgClcCoopReadyRead(&cl->in, cl); break;
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
	netmsgClcAuthWrite(&g_NetMsgRel);
	netmsgClcSettingsWrite(&g_NetMsgRel);
	netSend(g_NetLocalClient, &g_NetMsgRel, true, NETCHAN_CONTROL);
}

static void netClientEvDisconnect(const u32 reason)
{
	sysLogPrintf(LOG_CHAT, "NET: disconnected from server: %s (%u)", netGetDisconnectReason(reason), reason);
	netDisconnect();
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
			case SVC_STAGE_FLAG: rc = netmsgSvcStageFlagRead(&cl->in, cl); break;
			case SVC_OBJ_STATUS: rc = netmsgSvcObjStatusRead(&cl->in, cl); break;
			case SVC_ALARM: rc = netmsgSvcAlarmRead(&cl->in, cl); break;
			case SVC_CUTSCENE: rc = netmsgSvcCutsceneRead(&cl->in, cl); break;
			default:
				rc = 1;
				break;
		}
	}

	if (rc) {
		sysLogPrintf(LOG_WARNING, "NET: malformed or unknown message 0x%02x from server", msgid);
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

void netClientSettingsChanged(void)
{
	if (g_NetMode != NETMODE_CLIENT || !g_NetLocalClient) {
		return;
	}

	netClientReadConfig(g_NetLocalClient, 0);

	netbufStartWrite(&g_NetMsgRel);
	netmsgClcSettingsWrite(&g_NetMsgRel);
	netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL);
}

void netStartFrame(void)
{
	if (!g_NetMode) {
		return;
	}

	++g_NetTick;

	const bool isClient = (g_NetMode == NETMODE_CLIENT);
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
					netClientEvConnect(ev.data);
				} else if (ev.peer) {
					netServerEvConnect(ev.peer, ev.data);
				}
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
			case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
				if (isClient) {
					netClientEvDisconnect(ev.type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT ? DISCONNECT_TIMEOUT : ev.data);
				} else if (ev.peer) {
					struct netclient *cl = enet_peer_get_data(ev.peer);
					if (cl) {
						netServerEvDisconnect(cl);
					} else {
						// No attached client — spurious disconnect from peer that never completed auth.
						// Do NOT decrement g_NetNumClients here: it was never incremented for this peer.
						sysLogPrintf(LOG_WARNING | LOGFLAG_NOCON, "NET: disconnect from %s without attached client (ignored)", netFormatPeerAddr(ev.peer));
					}
				}
				break;
			case ENET_EVENT_TYPE_RECEIVE:
				if (ev.peer) {
					struct netclient *cl = (g_NetMode == NETMODE_CLIENT) ? g_NetLocalClient : enet_peer_get_data(ev.peer);
					if (cl && cl->state) {
						if (ev.packet->data && ev.packet->dataLength) {
							netbufStartReadData(&cl->in, ev.packet->data, ev.packet->dataLength);
							if (isClient) {
								netClientEvReceive(cl);
							} else {
								netServerEvReceive(cl);
							}
							netbufReset(&cl->in);
						}
					} else if (!isClient) {
						sysLogPrintf(LOG_WARNING | LOGFLAG_NOCON, "NET: receive from %s without attached client", netFormatPeerAddr(ev.peer));
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
	if (!g_NetMode) {
		return;
	}

	g_NetReliableFrameLen = 0;
	g_NetUnreliableFrameLen = 0;

	// send whatever messages have accumulated so far
	netFlushSendBuffers();

	if (g_NetLocalClient->state == CLSTATE_GAME && g_NetLocalClient->player && g_NetLocalClient->player->prop) {
		if (g_NetMode == NETMODE_CLIENT) {
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
		} else {
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

				// Send prop desync detection checksum every 120 frames (~2/sec)
				if ((g_NetTick % 120) == 0) {
					netmsgSvcPropSyncWrite(&g_NetMsgRel);
				}

				g_NetNextUpdate = g_NetTick + g_NetServerUpdateRate;
			}

			// Co-op NPC replication: broadcast NPC positions and state
			// NPCs are server-authoritative in co-op mode (AI runs only on server)
			if (g_NetGameMode == NETGAMEMODE_COOP || g_NetGameMode == NETGAMEMODE_ANTI) {
				// Ensure we're in GAME state to prevent sending NPCs during stage load
				if (g_NetLocalClient && g_NetLocalClient->state >= CLSTATE_GAME) {
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
								if (chr->prop && chr->prop->type == PROPTYPE_CHR && !chr->aibot) {
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
								if (chr->prop && chr->prop->type == PROPTYPE_CHR && !chr->aibot) {
									netmsgSvcNpcStateWrite(&g_NetMsgRel, chr);
								}
							}
						}
					}

					// NPC sync checksum every 120 frames (~0.5/sec)
					if ((g_NetTick % 120) == 0) {
						netmsgSvcNpcSyncWrite(&g_NetMsgRel);
					}
				}
			}

			// Expire preserved player slots after timeout
			if (g_NetNumPreserved > 0 && (g_NetTick % 600) == 0) {
				for (s32 i = 0; i < NET_MAX_CLIENTS; ++i) {
					if (g_NetPreservedPlayers[i].active
							&& (g_NetTick - g_NetPreservedPlayers[i].preserveframe) > NET_PRESERVE_TIMEOUT_FRAMES) {
						sysLogPrintf(LOG_NOTE, "NET: preserved player %s timed out", g_NetPreservedPlayers[i].name);
						g_NetPreservedPlayers[i].active = false;
						--g_NetNumPreserved;
					}
				}
			}

			// Handle pending resync requests from clients
			if (g_NetPendingResyncFlags) {
				if (g_NetPendingResyncFlags & NET_RESYNC_FLAG_CHRS) {
					sysLogPrintf(LOG_NOTE, "NET: sending chr resync to all clients (%u bots)", g_BotCount);
					netmsgSvcChrResyncWrite(&g_NetMsgRel);
				}
				if (g_NetPendingResyncFlags & NET_RESYNC_FLAG_PROPS) {
					sysLogPrintf(LOG_NOTE, "NET: sending prop resync to all clients");
					netmsgSvcPropResyncWrite(&g_NetMsgRel);
				}
				if (g_NetPendingResyncFlags & NET_RESYNC_FLAG_SCORES) {
					sysLogPrintf(LOG_NOTE, "NET: sending score resync to all clients");
					netmsgSvcPlayerScoresWrite(&g_NetMsgRel);
				}
				if (g_NetPendingResyncFlags & NET_RESYNC_FLAG_NPCS) {
					u32 npccount = netNpcCount();
					sysLogPrintf(LOG_NOTE, "NET: sending npc resync to all clients (%u npcs)", npccount);
					netmsgSvcNpcResyncWrite(&g_NetMsgRel);
					// Also sync co-op mission state (stage flags + objective statuses)
					netmsgSvcStageFlagWrite(&g_NetMsgRel);
					for (s32 i = 0; i <= g_ObjectiveLastIndex; ++i) {
						netmsgSvcObjStatusWrite(&g_NetMsgRel, (u8)i, (u8)g_ObjectiveStatuses[i]);
					}
				}
				g_NetPendingResyncFlags = 0;
			}
		}
	}

	// send position updates
	netFlushSendBuffers();

	enet_host_flush(g_NetHost);
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
		} else {
			enet_peer_send(dstcl->peer, chan, p);
		}
	}

	const u32 ret = buf->wp;

	netbufStartWrite(buf);

	return ret;
}

void netPlayersAllocate(void)
{
	s32 playernum = 0;

	if (g_NetMode == NETMODE_CLIENT) {
		// we always put the local player at index 0, even client-side
		// which means that clientside we have to put the server's player into our slot
		const s32 svplayernum = g_NetLocalClient->playernum;
		g_NetLocalClient->playernum = 0;
		g_NetClients[0].playernum = svplayernum;
	}

	for (s32 i = 0; i < g_NetMaxClients; ++i) {
		struct netclient *cl = &g_NetClients[i];
		if (cl->state < CLSTATE_LOBBY) {
			continue;
		}

		if (g_NetMode == NETMODE_SERVER) {
			// on the server allocate players sequentially
			cl->playernum = playernum++;
		}

		if (cl != g_NetLocalClient) {
			// disable controls for the remote pawns and set their settings
			// TODO: backup the player configs or something
			struct mpplayerconfig *cfg = &g_PlayerConfigsArray[cl->playernum];
			cfg->controlmode = CONTROLMODE_NA;
			cfg->base.mpbodynum = cl->settings.bodynum;
			cfg->base.mpheadnum = cl->settings.headnum;
			snprintf(cfg->base.name, sizeof(cfg->base.name), "%s\n", cl->settings.name);
			// take some of the options from our local player and others from the client
			cfg->options = g_PlayerConfigsArray[0].options & OPTION_PAINTBALL;
			cfg->options |= cl->settings.options & ~OPTION_PAINTBALL;
			// don't enable toggle aim, invert pitch or lookahead for remote players
			cfg->options &= ~(OPTION_AIMCONTROL | OPTION_LOOKAHEAD);
			cfg->options |= OPTION_FORWARDPITCH | OPTION_ASKEDSAVEPLAYER;
		}

		cl->config = &g_PlayerConfigsArray[cl->playernum];
		cl->config->client = cl;
		cl->config->handicap = 0x80;
		cl->player = g_Vars.players[cl->playernum];
		if (cl->player) {
			cl->player->client = cl;
			cl->player->isremote = (cl != g_NetLocalClient);
		}

		// Log player allocation
		sysLogPrintf(LOG_NOTE, "NET: allocated playernum=%d body=%d head=%d name=%s",
			cl->playernum, cl->settings.bodynum, cl->settings.headnum, cl->settings.name);
	}
}

void netSyncIdsAllocate(void)
{
	// allocate sync ids sequentially for all active or paused props
	g_NetNextSyncId = 1;

	// don't allocate anything else if we're in lobby
	if (g_StageNum == STAGE_TITLE || g_StageNum == STAGE_CITRAINING) {
		return;
	}

	// iterate active props first
	struct prop *prop = g_Vars.activeprops;
	while (prop && prop != g_Vars.pausedprops) {
		prop->syncid = prop - g_Vars.props + 1;
		if (prop->syncid > g_NetNextSyncId) {
			g_NetNextSyncId = prop->syncid;
		}
		prop = prop->next;
	}

	// then the paused props
	prop = g_Vars.pausedprops;
	while (prop) {
		prop->syncid = prop - g_Vars.props + 1;
		if (prop->syncid > g_NetNextSyncId) {
			g_NetNextSyncId = prop->syncid;
		}
		prop = prop->next;
	}

	// HACK: when we're a client, we'll need to swap our player and server player's props
	// because of what we do in netPlayersAllocate
	if (g_NetMode == NETMODE_CLIENT) {
		if (!g_NetLocalClient->player || !g_NetLocalClient->player->prop) {
			sysLogPrintf(LOG_ERROR, "NET: no props allocated for players?");
			netDisconnect();
			return;
		}
		const u16 sid = g_NetClients[0].player->prop->syncid;
		g_NetClients[0].player->prop->syncid = g_NetLocalClient->player->prop->syncid;
		g_NetLocalClient->player->prop->syncid = sid;
	}

	sysLogPrintf(LOG_NOTE, "NET: last initial syncid: %u", g_NetNextSyncId);
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

void netRecentServerAdd(const char *addr)
{
	if (!addr || !addr[0]) {
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
	strncpy(srv->addr, addr, NET_MAX_ADDR);
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
			netbufStartReadData(&buf, data, len);

			// skip magic + size
			{
				u8 skip[8];
				netbufReadData(&buf, skip, sizeof(NET_QUERY_MAGIC) - 1);
			}
			netbufReadU16(&buf);

			srv->protocol = netbufReadU32(&buf);
			srv->flags = netbufReadU8(&buf);
			srv->numclients = netbufReadU8(&buf);
			srv->maxclients = netbufReadU8(&buf);
			srv->stagenum = netbufReadU8(&buf);
			srv->scenario = netbufReadU8(&buf);
			char *hostname = netbufReadStr(&buf);
			if (hostname) {
				strncpy(srv->hostname, hostname, NET_MAX_NAME - 1);
			}
			srv->lastresponse = g_NetTick;
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

	// try to receive responses (blocking up to 200ms per attempt)
	for (s32 attempt = 0; attempt < g_NetNumRecentServers; ++attempt) {
		u8 rxdata[512];
		ENetBuffer rxbuf;
		ENetAddress rxaddr;
		rxbuf.data = rxdata;
		rxbuf.dataLength = sizeof(rxdata);
		s32 rxlen = enet_socket_receive(sock, &rxaddr, &rxbuf, 1);
		if (rxlen > 0) {
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

PD_CONSTRUCTOR static void netConfigInit(void)
{
	configRegisterUInt("Net.LerpTicks", &g_NetInterpTicks, 0, 600);

	configRegisterString("Net.Client.LastJoinAddr", g_NetLastJoinAddr, NET_MAX_ADDR);
	configRegisterUInt("Net.Client.InRate", &g_NetClientInRate, 0, 10 * 1024 * 1024);
	configRegisterUInt("Net.Client.OutRate", &g_NetClientOutRate, 0, 10 * 1024 * 1024);
	configRegisterUInt("Net.Client.UpdateFrames", &g_NetClientUpdateRate, 0, 60);

	configRegisterUInt("Net.Server.Port", &g_NetServerPort, 0, 0xFFFF);
	configRegisterUInt("Net.Server.InRate", &g_NetServerInRate, 0, 10 * 1024 * 1024);
	configRegisterUInt("Net.Server.OutRate", &g_NetServerOutRate, 0, 10 * 1024 * 1024);
	configRegisterUInt("Net.Server.UpdateFrames", &g_NetServerUpdateRate, 0, 60);
	configRegisterInt("Net.Server.AllowInfoQuery", &g_NetServerInfoQuery, 0, 1);

	// register recent server addresses for persistence
	static char recentKeys[NET_MAX_RECENT_SERVERS][32];
	for (s32 i = 0; i < NET_MAX_RECENT_SERVERS; ++i) {
		snprintf(recentKeys[i], sizeof(recentKeys[i]), "Net.RecentServer.%d", i);
		configRegisterString(recentKeys[i], g_NetRecentServers[i].addr, NET_MAX_ADDR);
	}
	configRegisterInt("Net.RecentServerCount", &g_NetNumRecentServers, 0, NET_MAX_RECENT_SERVERS);
}
