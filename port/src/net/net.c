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
#include "net/netupnp.h"
#include "net/netstun.h"
#include "net/netholepunch.h"
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
#include "audio.h"
#include "sha256.h"
#include "server_bans.h"
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

/* Bot authority: true on the client designated to run bot AI and relay positions via CLC_BOT_MOVE.
 * Set by SVC_BOT_AUTHORITY (dedicated server games only); cleared on disconnect/stage-end. */
bool g_NetLocalBotAuthority = false;
bool g_NetPendingBotAuthority = false;

/* U-10: Stage-ready handshake — server-side tracking (dedicated server only). */
s32  g_NetStageReadyDeadline    = -1;   /* g_NetTick value at timeout; -1 = not waiting */
bool g_NetBotAuthorityDelegated = false; /* true once SVC_BOT_AUTHORITY sent this match */
u32  g_NetMatchSeed             = 0;    /* L2-4: server-generated seed for deterministic spawn pools */

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
	cl->room_id = 0xFF;
	netmsgChatRateReset((u32)cl->id);
	netmsgRoomMutationRateReset((u32)cl->id);
	netmsgAdminAuthRateReset((u32)cl->id);
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
			: catalogIdByRuntime(ASSET_GAMEMODE, (s32)g_MpSetup.scenario);
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

	g_NetTick = 0;
	g_NetNextUpdate = 0;
	g_NetNextSyncId = 1;
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

	lobbyInit();
	netDistribInit();

	return 0;
}

void netServerStageStart(void)
{
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
		return;
	}

	/* NOTE: do NOT guard on g_StageNum == STAGE_CITRAINING here.
	 * The server lobby runs on STAGE_CITRAINING, and mainChangeToStage() is
	 * deferred — g_StageNum still equals STAGE_CITRAINING when this function
	 * runs.  Guarding on it prevented SVC_STAGE_START from ever being sent,
	 * so clients never received the map-load signal.  This function is only
	 * called from the CLC_LOBBY_START handler after leader validation, so
	 * there is no need for a stage-num guard. */

	// re-read the player config in case it changed
	if (g_NetLocalClient) {
		netClientReadConfig(g_NetLocalClient, 0);
	}

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
		if (g_NetClients[ci].state == CLSTATE_LOBBY ||
		    g_NetClients[ci].state == CLSTATE_PREPARING) {
			if (g_NetMatchRoomId != 0xFF && g_NetClients[ci].room_id != g_NetMatchRoomId) {
				continue;
			}
			g_NetClients[ci].state = CLSTATE_GAME;
		}
	}

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

	// clear preserved players from previous rounds
	memset(g_NetPreservedPlayers, 0, sizeof(g_NetPreservedPlayers));
	g_NetNumPreserved = 0;

	/* L2-4: Generate match seed from RNG for deterministic spawn pools.
	 * Both server and all clients will use this to build identical spawn
	 * point pools via spawnpool.c (future). */
	g_NetMatchSeed = (u32)(g_RngSeed ^ (g_RngSeed >> 32)) ^ g_NetTick;

	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcStageStartWrite(&g_NetMsgRel);
	if (g_NetMatchRoomId != 0xFF) {
		netSendToRoom(g_NetMatchRoomId, &g_NetMsgRel, true, NETCHAN_DEFAULT);
		sysLogPrintf(LOG_NOTE,
			"MATCHSTART.DIAG: SVC_STAGE_START sent to room 0x%02x (combat sim)",
			(unsigned)g_NetMatchRoomId);
	} else {
		netSend(NULL, &g_NetMsgRel, true, NETCHAN_DEFAULT);
		sysLogPrintf(LOG_NOTE,
			"MATCHSTART.DIAG: SVC_STAGE_START broadcast to all clients (combat sim)");
	}
	crashBreadcrumbPush("SVC_STAGE_START sent mode=%d room=0x%02x",
		(int)g_NetGameMode, (unsigned)g_NetMatchRoomId);

	/* Dedicated server: allocate minimal bot stubs.  BOT_AUTHORITY is now deferred
	 * until all clients confirm their stage is loaded via CLC_STAGE_READY, so that
	 * the authority client's pads/spawn-points are ready before botSpawnAll() fires.
	 * On a listen server the host runs full bot AI directly — no relay needed. */
	if (g_NetDedicated) {
		mpStartMatch(); /* server_stubs.c version: allocates stub chrdata/prop/aibot from heap */

		/* Reset per-client stage-ready flags and arm the handshake deadline (5 s). */
		for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
			g_NetClients[ci].stage_ready = false;
		}
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
}

void netServerCoopStageStart(u8 stagenum, u8 difficulty)
{
	if (g_NetMode != NETMODE_SERVER) {
		return;
	}

	// configure mission on the server side
	g_MissionConfig.stagenum = stagenum;
	/* Phase 2: populate PRIMARY catalog ID string field */
	{
		const char *cid = catalogIdByRuntime(ASSET_MAP, stagenum);
		if (cid) { strncpy(g_MissionConfig.stage_id, cid, sizeof(g_MissionConfig.stage_id) - 1); g_MissionConfig.stage_id[sizeof(g_MissionConfig.stage_id) - 1] = '\0'; }
		else { g_MissionConfig.stage_id[0] = '\0'; }
	}
	g_MissionConfig.difficulty = difficulty;
	g_MissionConfig.iscoop = (g_NetGameMode == NETGAMEMODE_COOP);
	g_MissionConfig.isanti = (g_NetGameMode == NETGAMEMODE_ANTI);

	// set up co-op player numbers
	g_Vars.bondplayernum = 0;
	if (g_NetGameMode == NETGAMEMODE_COOP) {
		g_Vars.coopplayernum = (g_NetNumClients > 1) ? 1 : -1;
		g_Vars.antiplayernum = -1;
		sysLogPrintf(LOG_NOTE,
			"GAMELOOP.COOP: server start coop stage=0x%02x clients=%u coopplayernum=%d",
			stagenum, g_NetNumClients, g_Vars.coopplayernum);
	} else {
		s32 antiPlayerNum = -1;
		bool resolvedFromWire = false;
		if (g_NetCounterOpClientId != NET_NULL_CLIENT) {
			for (s32 i = 0; i < g_NetMaxClients; i++) {
				struct netclient *ncl = &g_NetClients[i];
				if (ncl->id != g_NetCounterOpClientId) continue;
				if (ncl->state < CLSTATE_LOBBY) continue;
				if (ncl->playernum < MAX_PLAYERS) {
					antiPlayerNum = ncl->playernum;
					resolvedFromWire = true;
				}
				break;
			}
		}
		if (antiPlayerNum < 0) {
			/* S303: log the silent fallback so playtest logs show when
			 * anti-player identity routing degraded from explicit
			 * CLC_LOBBY_START antiClientId down to "assume slot 1".
			 * This path should be unreachable after v36 but belt-and-
			 * braces covers stale state or future regressions. */
			antiPlayerNum = (g_NetNumClients > 1) ? 1 : -1;
			sysLogPrintf(LOG_WARNING,
				"GAMELOOP.COUNTEROP: antiClientId unresolved (id=%u) — falling back to slot %d (clients=%u)",
				(unsigned)g_NetCounterOpClientId,
				antiPlayerNum, g_NetNumClients);
		} else {
			sysLogPrintf(LOG_NOTE,
				"GAMELOOP.COUNTEROP: server start anti stage=0x%02x clients=%u antiClientId=%u antiplayernum=%d%s",
				stagenum, g_NetNumClients,
				(unsigned)g_NetCounterOpClientId, antiPlayerNum,
				resolvedFromWire ? " (from wire)" : "");
		}
		g_Vars.coopplayernum = -1;
		g_Vars.antiplayernum = antiPlayerNum;
	}

	// re-read the player config
	if (g_NetLocalClient) {
		netClientReadConfig(g_NetLocalClient, 0);
		g_NetLocalClient->state = CLSTATE_GAME;
	}

	// clear preserved players from previous rounds
	memset(g_NetPreservedPlayers, 0, sizeof(g_NetPreservedPlayers));
	g_NetNumPreserved = 0;

	// clear coop ready flags
	for (s32 i = 0; i < g_NetMaxClients; ++i) {
		g_NetClients[i].flags &= ~CLFLAG_COOPREADY;
	}

	/* L2-4: Generate match seed for deterministic spawn pools */
	g_NetMatchSeed = (u32)(g_RngSeed ^ (g_RngSeed >> 32)) ^ g_NetTick;

	// broadcast stage start to all clients
	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcStageStartWrite(&g_NetMsgRel);
	netSend(NULL, &g_NetMsgRel, true, NETCHAN_DEFAULT);

	// Log character selections for each player
	for (s32 i = 0; i < g_NetMaxClients; ++i) {
		struct netclient *cl = &g_NetClients[i];
		if (cl->state >= CLSTATE_LOBBY) {
			sysLogPrintf(LOG_NOTE, "NET: player %d (%s) body='%s' head='%s'",
				i, cl->settings.name, cl->settings.body_id, cl->settings.head_id);
		}
	}

	sysLogPrintf(LOG_NOTE, "NET: starting co-op stage 0x%02x difficulty %u with %u players",
		stagenum, difficulty, g_NetNumClients);

	g_NotLoadMod = true;
	romdataFileFreeForSolo();

	// Schedule a full state resync shortly after stage start (NPCs + props)
	g_NetPendingResyncFlags = NET_RESYNC_FLAG_NPCS | NET_RESYNC_FLAG_PROPS;
	netPropSnapReset();

	// start the mission on the server
	menuStop();
#if !defined(PD_SERVER)
	/* Input context stack handles mouse capture when gameplay context becomes top. */
#endif
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

	/* U-10: disarm the stage-ready handshake for the next match. */
	g_NetStageReadyDeadline    = -1;
	g_NetBotAuthorityDelegated = false;
	g_NetBotAuthorityClientId  = NET_NULL_CLIENT;

	sysLogPrintf(LOG_NOTE, "NET: === STAGE END === game mode=%u tick=%u", g_NetGameMode, g_NetTick);

	if (g_NetLocalClient) {
		g_NetLocalClient->state = CLSTATE_LOBBY;
	}

	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcStageEndWrite(&g_NetMsgRel, g_NetMatchRoomId);
	netSend(NULL, &g_NetMsgRel, true, NETCHAN_DEFAULT);
	g_NetMatchRoomId = 0xFF;
	g_NetCounterOpClientId = NET_NULL_CLIENT;

	/* L-E3: tear down session catalog on server-side stage end */
	sessionCatalogTeardown();

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
	strncpy(g_NetLastJoinAddr, addr, NET_MAX_ADDR - 1);
	g_NetLastJoinAddr[NET_MAX_ADDR - 1] = '\0';
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
	netSyncIdMapClear(); // ensure map is empty from any previous session

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

	const bool wasingame = (g_NetLocalClient && g_NetLocalClient->state >= CLSTATE_GAME);

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

	/* Cancel any in-progress STUN discovery */
	stunCancel();

	/* Reset hole punch waterfall state */
	netHolePunchReset();

	g_NetHost = NULL;
	g_NetMode = NETMODE_NONE;
	g_NetGameMode = NETGAMEMODE_MP;
	g_NetLocalBotAuthority = false;
	g_NetPendingBotAuthority = false;
	g_NetBotAuthorityClientId = NET_NULL_CLIENT;

	/* MASTER-C3: drop the identity cookie so a fresh connect to any server
	 * begins as a new player.  Cookies are scoped to a single session —
	 * intentionally NOT persisted to disk (keeping them in-memory avoids
	 * cross-process replay attacks via shared config files). */
#if !defined(PD_SERVER)
	extern void netmsgClcAuthClearCookie(void);
	netmsgClcAuthClearCookie();
#endif

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

#if !defined(PD_SERVER)
	/* M-23-A / Priority K-b3: cascade-close the menu pool on every disconnect
	 * (lobby or in-game).  Disconnect paths don't go through menuPushRootDialog,
	 * so pool slots from lobby/room/mp-setup would survive and block reopen of
	 * the same type on return.  menupoolReleaseAll is idempotent and pops every
	 * owned ctx (including the unregistered-fallback after K-b1), so the legacy
	 * paired inputCtxPopDeferred(&g_CtxImGuiMenu) is no longer needed. */
	menupoolReleaseAll();
#endif

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
		manifestClear(&g_ClientManifest);
		mainChangeToStage(STAGE_CITRAINING);
	}

	return 0;
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
	pp->name[NET_MAX_NAME - 1] = '\0';
	pp->playernum = cl->playernum;
	pp->team = cl->settings.team;

	/* MASTER-C3: preserve the current cookie so only the real owner can restore. */
	memcpy(pp->cookie, cl->auth_cookie, NET_AUTH_COOKIE_LEN);

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

/* MASTER-C3: match on BOTH name AND cookie.  Constant-time compare on the
 * cookie so a name collision doesn't leak timing about the real owner's
 * cookie value. */
struct netpreservedplayer *netServerFindPreservedByCookie(const char *name,
	const u8 cookie[NET_AUTH_COOKIE_LEN])
{
	if (!name || !name[0] || !cookie) {
		return NULL;
	}

	/* Reject all-zero cookies explicitly — the wire default is zeros and
	 * matching a preserved record with zeros would be trivially bypassable
	 * if a slot were ever preserved with a zero cookie (shouldn't happen,
	 * but defence-in-depth). */
	u8 zeroOr = 0;
	for (s32 i = 0; i < NET_AUTH_COOKIE_LEN; i++) zeroOr |= cookie[i];
	if (!zeroOr) return NULL;

	struct netpreservedplayer *pp = netServerFindPreserved(name);
	if (!pp) return NULL;

	u8 diff = 0;
	for (s32 i = 0; i < NET_AUTH_COOKIE_LEN; i++) {
		diff |= (u8)(pp->cookie[i] ^ cookie[i]);
	}
	return (diff == 0) ? pp : NULL;
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
	{
		const asset_entry_t *be = assetCatalogResolve(cl->settings.body_id);
		const asset_entry_t *he = assetCatalogResolve(cl->settings.head_id);
		cfg->base.mpbodynum = be ? (u8)be->mp_index : 0;
		cfg->base.mpheadnum = he ? (u8)he->mp_index : 0;
		/* Phase 2: populate PRIMARY catalog ID string fields */
		strncpy(cfg->base.body_id, cl->settings.body_id, sizeof(cfg->base.body_id) - 1);
		cfg->base.body_id[sizeof(cfg->base.body_id) - 1] = '\0';
		strncpy(cfg->base.head_id, cl->settings.head_id, sizeof(cfg->base.head_id) - 1);
		cfg->base.head_id[sizeof(cfg->base.head_id) - 1] = '\0';
	}
	cfg->controlmode = CONTROLMODE_NA;
	snprintf(cfg->base.name, sizeof(cfg->base.name), "%s\n", cl->settings.name);
	cfg->options = g_PlayerConfigsArray[0].options & OPTION_PAINTBALL;
	cfg->options |= cl->settings.options & ~OPTION_PAINTBALL;
	cfg->options &= ~(OPTION_AIMCONTROL | OPTION_LOOKAHEAD);
	cfg->options |= OPTION_FORWARDPITCH | OPTION_ASKEDSAVEPLAYER;

	cl->state = CLSTATE_GAME;

	/* MASTER-C3: cl->auth_cookie was already refreshed in netmsgClcAuthRead
	 * before this call, and SVC_AUTH will ship the new cookie back to the
	 * client.  We clear the preserved slot so the freshly-issued cookie is
	 * what the client must present on any future reconnect. */

	// clear the preserved slot
	pp->active = false;
	--g_NetNumPreserved;

	sysLogPrintf(LOG_NOTE, "NET: restored player %s to playernum %u", cl->settings.name, pp->playernum);
}

static void netServerEvConnect(ENetPeer *peer, const u32 data)
{
	sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON, "NET: incoming connection");

	if (data != NET_PROTOCOL_VER) {
		sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON, "NET: connection rejected: protocol mismatch (got %u, expected %u)", data, NET_PROTOCOL_VER);
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

	const bool ingame = (g_NetLocalClient && g_NetLocalClient->state > CLSTATE_LOBBY);

	struct netclient *cl = NULL;

	/* Dedicated server: slot 0 is free for real players.
	 * Listen server: slot 0 is the host, start at 1. */
	s32 slotStart = g_NetDedicated ? 0 : 1;
	for (s32 i = slotStart; i < g_NetMaxClients; ++i) {
		if (!g_NetClients[i].state) {
			cl = &g_NetClients[i];
			break;
		}
	}

	if (!cl) {
		sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON, "NET: connection rejected: server full");
		enet_peer_disconnect(peer, DISCONNECT_FULL);
		return;
	}

	if (ingame && g_NetNumPreserved == 0) {
		sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON, "NET: connection rejected: game in progress, no preserved slots");
		enet_peer_disconnect(peer, DISCONNECT_LATE);
		return;
	}

	/* M-7: Don't increment g_NetNumClients here — wait until CLC_AUTH succeeds.
	 * This prevents unauthenticated connections from counting toward the client limit. */

	netClientReset(cl);
	cl->state = CLSTATE_AUTH; // skip CLSTATE_CONNECTING, since we already know it connected
	cl->peer = peer;
	cl->flags = ingame ? CLFLAG_ABSENT : 0; // mark as pending reconnect if mid-game
	enet_peer_set_data(peer, cl);
	sysLogPrintf(LOG_NOTE, "NET: client slot %d assigned to peer", (int)(cl - g_NetClients));
}

static void netServerEvDisconnect(struct netclient *cl)
{
	sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON, "NET: disconnect event from client %u", cl->id);

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

	/* R-3: Remove from room before reset (room_id cleared by netClientReset) */
	if (cl->room_id != 0xFF) {
		hub_room_t *room = roomGetById(cl->room_id);
		if (room) {
			roomLeave(room, cl->id);
		}
	}

	if (cl->id == g_NetBotAuthorityClientId) {
		g_NetBotAuthorityClientId = NET_NULL_CLIENT;
		if (g_NetDedicated && cl->state >= CLSTATE_GAME) {
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

	--g_NetNumClients;

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
			/* Bot authority relay */
			case SVC_BOT_AUTHORITY: rc = netmsgSvcBotAuthorityRead(&cl->in, cl); break;
			case SVC_STAGE_FLAG: rc = netmsgSvcStageFlagRead(&cl->in, cl); break;
			case SVC_OBJ_STATUS: rc = netmsgSvcObjStatusRead(&cl->in, cl); break;
			case SVC_ALARM: rc = netmsgSvcAlarmRead(&cl->in, cl); break;
			case SVC_CUTSCENE: rc = netmsgSvcCutsceneRead(&cl->in, cl); break;
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
						netServerEvDisconnect(cl);
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
	if (!g_NetMode) {
		return;
	}

	g_NetReliableFrameLen = 0;
	g_NetUnreliableFrameLen = 0;

	// send whatever messages have accumulated so far
	netFlushSendBuffers();

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

	/* --- Server: broadcast game state to all clients ---
	 * CRITICAL: This block must NOT be guarded by g_NetLocalClient — on a
	 * dedicated server g_NetLocalClient is NULL, so putting this inside a
	 * g_NetLocalClient guard makes the entire server broadcast path dead code.
	 * Instead, check g_NetMode == NETMODE_SERVER and use g_NetNumClients to
	 * know whether any clients are connected and need updates. */
	if (g_NetMode == NETMODE_SERVER && g_NetNumClients > 0) {
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

		/* L1-2: Periodic score broadcast every 300 frames (~5 s at 60 fps).
		 * Score mutations are event-driven (SVC_PLAYER_STATS on each death), but
		 * a single dropped reliable packet causes permanent divergence until
		 * reconnect.  This gives a bounded correction window so no client can
		 * stay out of sync for more than 5 seconds. ~200 bytes per broadcast.
		 * Guard with mplayerisrunning so the broadcast stops at match end. */
		if ((g_NetTick % 300) == 0 && g_Vars.mplayerisrunning) {
			netmsgSvcPlayerScoresWrite(&g_NetMsgRel);
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

		/* SEC-13: flush any pending SVC_ROOM_LIST broadcast (coalesces bursty
		 * CLC_ROOM_CREATE/JOIN/LEAVE into one broadcast per frame). */
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
	if (g_NetMode == NETMODE_SERVER) {
		netDistribServerTick();
		/* Phase F: drive the match launch countdown (no-op until armed by readyGateCheck) */
		readyGateTickCountdown();
		/* v34: advance music playlist when track ends (host only, no-op if no playlist).
		 * Issue 4b (v40): same tick re-broadcasts the current offset every 2s
		 * for client drift correction. */
		audioNetworkMusicTick();

		/* U-10: Stage-ready timeout — if not all clients reported ready within the
		 * deadline, delegate BOT_AUTHORITY to the first available CLSTATE_GAME client
		 * anyway so the match can proceed. */
		if (g_NetDedicated && !g_NetBotAuthorityDelegated
				&& g_NetStageReadyDeadline >= 0 && (s32)g_NetTick >= g_NetStageReadyDeadline) {
			s32 readyCount = 0, totalCount = 0;
			for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
				if (g_NetClients[ci].state == CLSTATE_GAME) {
					totalCount++;
					if (g_NetClients[ci].stage_ready) {
						readyCount++;
					}
				}
			}
			sysLogPrintf(LOG_NOTE, "NET: stage ready timeout, sending BOT_AUTHORITY anyway (ready: %d/%d)",
			             readyCount, totalCount);
			for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
				if (g_NetClients[ci].state == CLSTATE_GAME) {
					netbufStartWrite(&g_NetMsgRel);
					netmsgSvcBotAuthorityWrite(&g_NetMsgRel);
					netSend(&g_NetClients[ci], &g_NetMsgRel, true, NETCHAN_DEFAULT);
					g_NetBotAuthorityClientId = g_NetClients[ci].id;
					sysLogPrintf(LOG_NOTE, "NET: SVC_BOT_AUTHORITY (timeout) sent to client %u ('%s') — %u bot stubs ready",
					             g_NetClients[ci].id, g_NetClients[ci].settings.name, (u32)g_BotCount);
					break;
				}
			}
			g_NetBotAuthorityDelegated = true;
			g_NetStageReadyDeadline    = -1;
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
			/* H-5: Check return value — on failure, ENet does not free the packet. */
			if (enet_peer_send(dstcl->peer, chan, p) < 0) {
				sysLogPrintf(LOG_WARNING, "NET: enet_peer_send failed (%u bytes, chan %d)", buf->wp, chan);
				enet_packet_destroy(p);
			}
		}
	}

	const u32 ret = buf->wp;

	netbufStartWrite(buf);

	return ret;
}

void netSendToRoom(u8 room_id, struct netbuf *buf, s32 reliable, s32 chan)
{
	if (!g_NetHost || !buf || !buf->wp) return;

	const u32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;

	for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
		struct netclient *cl = &g_NetClients[i];
		if (cl->state >= CLSTATE_LOBBY && cl->peer && cl->room_id == room_id) {
			ENetPacket *p = enet_packet_create(buf->data, buf->wp, flags);
			if (p) {
				enet_peer_send(cl->peer, chan, p);
			}
		}
	}

	netbufStartWrite(buf);
}

/* Snapshot of remote player configs before netPlayersAllocate overwrites them.
 * Indexed by playernum (0..MAX_PLAYERS-1). Available for restoration if a match
 * fails to start after configs have been overwritten (e.g. on a SVC_STAGE_END
 * that arrives before the stage fully loads). */
static struct mpplayerconfig s_RemoteConfigBackups[MAX_PLAYERS];

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
			struct mpplayerconfig *cfg = &g_PlayerConfigsArray[cl->playernum];
			// Snapshot original config before overwrite; restore via s_RemoteConfigBackups[playernum]
			// if the match fails to start after this point.
			if (cl->playernum < MAX_PLAYERS) {
				s_RemoteConfigBackups[cl->playernum] = *cfg;
			}
			{
				const asset_entry_t *be = assetCatalogResolve(cl->settings.body_id);
				const asset_entry_t *he = assetCatalogResolve(cl->settings.head_id);
				cfg->base.mpbodynum = be ? (u8)be->runtime_index : 0;
				cfg->base.mpheadnum = he ? (u8)he->runtime_index : 0;
				/* Phase 2: populate PRIMARY catalog ID string fields */
				strncpy(cfg->base.body_id, cl->settings.body_id, sizeof(cfg->base.body_id) - 1);
				cfg->base.body_id[sizeof(cfg->base.body_id) - 1] = '\0';
				strncpy(cfg->base.head_id, cl->settings.head_id, sizeof(cfg->base.head_id) - 1);
				cfg->base.head_id[sizeof(cfg->base.head_id) - 1] = '\0';
			}
			cfg->controlmode = CONTROLMODE_NA;
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
		sysLogPrintf(LOG_NOTE, "NET: allocated playernum=%d body='%s' head='%s' name=%s",
			cl->playernum, cl->settings.body_id, cl->settings.head_id, cl->settings.name);
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
			/* M0.1d: scenario as catalog ID string (v32+). */
			{
				const char *scid = netbufReadStr(&buf);
				if (scid && scid[0]) {
					strncpy(srv->scenario_id, scid, sizeof(srv->scenario_id) - 1);
					srv->scenario_id[sizeof(srv->scenario_id) - 1] = '\0';
					const asset_entry_t *gm = assetCatalogResolve(scid);
					srv->scenario = (gm && gm->type == ASSET_GAMEMODE) ? (u8)gm->ext.gamemode.mode_id : 0;
				} else {
					srv->scenario_id[0] = '\0';
					srv->scenario = 0;
				}
			}
			const char *hostname = netbufReadStr(&buf);
			if (hostname) {
				strncpy(srv->hostname, hostname, NET_MAX_NAME - 1);
				srv->hostname[NET_MAX_NAME - 1] = '\0';
			}
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