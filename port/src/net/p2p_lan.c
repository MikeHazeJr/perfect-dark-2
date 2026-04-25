/**
 * p2p_lan.c -- Tier 0: LAN UDP broadcast announcer + listener.
 *
 * Same-subnet peers exchange tiny announcement datagrams so the
 * orchestrator can find each other without any internet traffic. Cheaper
 * and more reliable than STUN punch when applicable -- the design doc
 * lists this as the cheapest tier, attempted first.
 *
 * Wire format (all little-endian, 32 bytes total):
 *
 *   off len  field
 *   ----------------------------
 *    0  5    magic "PDLAN"
 *    5  1    version (1)
 *    6  1    flags  (bit 0: bye-bye, bit 1: appear-offline)
 *    7  1    _pad
 *    8  4    sender handle (u32)
 *   12  4    listen ipv4 (host order; 0 = auto-resolve from packet src)
 *   16  2    listen port (host order)
 *   18  2    netproto version
 *   20  4    nonce (uniqueness, sender SDL_GetTicks)
 *   24  8    _reserved
 *
 * Cache: a fixed table of recently-seen peers keyed by handle, with a
 * 15-second TTL so departed peers fall off naturally.
 */

#include "net/p2p.h"
#include "net/net.h"
#include "social.h"
#include "system.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #define closesocket close
  typedef int SOCKET;
  #define INVALID_SOCKET (-1)
  #define SOCKET_ERROR   (-1)
#endif

#define P2P_LAN_PORT          27101 /* one above NET_DEFAULT_PORT */
#define P2P_LAN_MAGIC         "PDLAN"
#define P2P_LAN_MAGIC_LEN     5
#define P2P_LAN_VERSION       1
#define P2P_LAN_PACKET_SIZE   32
#define P2P_LAN_ANNOUNCE_MS   3000  /* re-announce every 3s */
#define P2P_LAN_TTL_MS        15000 /* drop unseen peers after 15s */
#define P2P_LAN_CACHE_MAX     64

#define P2P_LAN_FLAG_BYE      0x01
#define P2P_LAN_FLAG_OFFLINE  0x02

typedef struct {
	u32 handle;
	u32 ipv4;       /* host order */
	u16 port;       /* host order */
	u32 last_seen_ms;
	u8  in_use;
} lan_peer_t;

static lan_peer_t s_Cache[P2P_LAN_CACHE_MAX];
static SOCKET s_Sock = INVALID_SOCKET;
static u32    s_LastAnnounceMs;
static s32    s_Running;
static u16    s_LocalPort;

/* -------------------------------------------------------------------------
 * Endian-stable little-endian write/read helpers (the existing netbuf
 * layer is reliable-stream oriented; we use plain memcpy since these are
 * connectionless 32-byte datagrams).
 * ------------------------------------------------------------------------- */

static void wU8(u8 **p, u8 v)   { *(*p)++ = v; }
static void wU16(u8 **p, u16 v) { (*p)[0]=(u8)v; (*p)[1]=(u8)(v>>8); *p+=2; }
static void wU32(u8 **p, u32 v) {
	(*p)[0]=(u8)v; (*p)[1]=(u8)(v>>8); (*p)[2]=(u8)(v>>16); (*p)[3]=(u8)(v>>24); *p+=4;
}
static u8  rU8 (const u8 **p) { return *(*p)++; }
static u16 rU16(const u8 **p) {
	u16 v = (u16)((*p)[0]) | ((u16)((*p)[1])<<8); *p+=2; return v;
}
static u32 rU32(const u8 **p) {
	u32 v = ((u32)((*p)[0])      ) | ((u32)((*p)[1])<< 8) |
	        ((u32)((*p)[2]) << 16) | ((u32)((*p)[3])<<24); *p+=4; return v;
}

/* -------------------------------------------------------------------------
 * Socket primitives
 * ------------------------------------------------------------------------- */

static s32 socketSetNonblock(SOCKET s)
{
#ifdef _WIN32
	u_long mode = 1;
	return ioctlsocket(s, FIONBIO, &mode) == 0 ? 0 : -1;
#else
	int fl = fcntl(s, F_GETFL, 0);
	if (fl < 0) return -1;
	return fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0 ? 0 : -1;
#endif
}

static s32 socketSetBroadcast(SOCKET s)
{
	int yes = 1;
	if (setsockopt(s, SOL_SOCKET, SO_BROADCAST,
	               (const char *)&yes, sizeof(yes)) != 0) return -1;
	return 0;
}

static s32 socketSetReuse(SOCKET s)
{
	int yes = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
	               (const char *)&yes, sizeof(yes)) != 0) return -1;
	return 0;
}

/* -------------------------------------------------------------------------
 * Cache
 * ------------------------------------------------------------------------- */

static void cachePrune(u32 now_ms)
{
	for (s32 i = 0; i < P2P_LAN_CACHE_MAX; i++) {
		if (s_Cache[i].in_use && (now_ms - s_Cache[i].last_seen_ms) > P2P_LAN_TTL_MS) {
			s_Cache[i].in_use = 0;
		}
	}
}

static void cacheRecord(u32 handle, u32 ipv4, u16 port)
{
	if (handle == 0) return;
	const u32 now_ms = SDL_GetTicks();

	/* Update existing entry first. */
	for (s32 i = 0; i < P2P_LAN_CACHE_MAX; i++) {
		if (s_Cache[i].in_use && s_Cache[i].handle == handle) {
			s_Cache[i].ipv4 = ipv4;
			s_Cache[i].port = port;
			s_Cache[i].last_seen_ms = now_ms;
			return;
		}
	}

	/* Insert into a free slot. */
	for (s32 i = 0; i < P2P_LAN_CACHE_MAX; i++) {
		if (!s_Cache[i].in_use) {
			s_Cache[i].in_use = 1;
			s_Cache[i].handle = handle;
			s_Cache[i].ipv4 = ipv4;
			s_Cache[i].port = port;
			s_Cache[i].last_seen_ms = now_ms;
			return;
		}
	}

	/* Evict oldest. */
	s32 oldest = 0;
	for (s32 i = 1; i < P2P_LAN_CACHE_MAX; i++) {
		if (s_Cache[i].last_seen_ms < s_Cache[oldest].last_seen_ms) oldest = i;
	}
	s_Cache[oldest].handle = handle;
	s_Cache[oldest].ipv4 = ipv4;
	s_Cache[oldest].port = port;
	s_Cache[oldest].last_seen_ms = now_ms;
}

static void cacheRemove(u32 handle)
{
	for (s32 i = 0; i < P2P_LAN_CACHE_MAX; i++) {
		if (s_Cache[i].in_use && s_Cache[i].handle == handle) {
			s_Cache[i].in_use = 0;
			return;
		}
	}
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

s32 p2pLanStart(void)
{
	if (s_Running) return 0;

	memset(s_Cache, 0, sizeof(s_Cache));

	s_Sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (s_Sock == INVALID_SOCKET) {
		sysLogPrintf(LOG_WARNING, "P2P.LAN: socket() failed");
		return -1;
	}

	socketSetReuse(s_Sock);
	socketSetBroadcast(s_Sock);
	socketSetNonblock(s_Sock);

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(P2P_LAN_PORT);

	if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		sysLogPrintf(LOG_WARNING, "P2P.LAN: bind(%u) failed", (unsigned)P2P_LAN_PORT);
		closesocket(s_Sock);
		s_Sock = INVALID_SOCKET;
		return -1;
	}

	s_LocalPort = P2P_LAN_PORT;
	s_LastAnnounceMs = 0;
	s_Running = 1;
	sysLogPrintf(LOG_NOTE, "P2P.LAN: listener bound on UDP %u", (unsigned)s_LocalPort);
	return 0;
}

void p2pLanStop(void)
{
	if (!s_Running) return;

	/* Send a goodbye announce so neighbours prune us promptly. */
	u8 packet[P2P_LAN_PACKET_SIZE];
	memset(packet, 0, sizeof(packet));
	u8 *p = packet;
	memcpy(p, P2P_LAN_MAGIC, P2P_LAN_MAGIC_LEN); p += P2P_LAN_MAGIC_LEN;
	wU8(&p, P2P_LAN_VERSION);
	wU8(&p, P2P_LAN_FLAG_BYE);
	wU8(&p, 0); /* pad */
	wU32(&p, socialMyHandle());
	wU32(&p, 0);
	wU16(&p, s_LocalPort);
	wU16(&p, NET_PROTOCOL_VER);
	wU32(&p, SDL_GetTicks());

	struct sockaddr_in dst;
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	dst.sin_port = htons(P2P_LAN_PORT);
	(void)sendto(s_Sock, (const char *)packet, P2P_LAN_PACKET_SIZE, 0,
	             (struct sockaddr *)&dst, sizeof(dst));

	closesocket(s_Sock);
	s_Sock = INVALID_SOCKET;
	s_Running = 0;
	sysLogPrintf(LOG_NOTE, "P2P.LAN: listener stopped");
}

s32 p2pLanIsRunning(void) { return s_Running; }

s32 p2pLanLookup(u32 peer_handle, u32 *out_ipv4, u16 *out_port)
{
	if (peer_handle == 0) return 0;
	const u32 now_ms = SDL_GetTicks();
	cachePrune(now_ms);
	for (s32 i = 0; i < P2P_LAN_CACHE_MAX; i++) {
		if (s_Cache[i].in_use && s_Cache[i].handle == peer_handle) {
			if (out_ipv4) *out_ipv4 = s_Cache[i].ipv4;
			if (out_port) *out_port = s_Cache[i].port;
			return 1;
		}
	}
	return 0;
}

/* -------------------------------------------------------------------------
 * Tick: announce + drain receive queue
 * ------------------------------------------------------------------------- */

static void sendAnnounce(u8 flags)
{
	u8 packet[P2P_LAN_PACKET_SIZE];
	memset(packet, 0, sizeof(packet));
	u8 *p = packet;
	memcpy(p, P2P_LAN_MAGIC, P2P_LAN_MAGIC_LEN); p += P2P_LAN_MAGIC_LEN;
	wU8(&p, P2P_LAN_VERSION);
	wU8(&p, flags);
	wU8(&p, 0); /* pad */
	wU32(&p, socialMyHandle());
	wU32(&p, 0); /* listen ipv4 -- 0 means use packet src */
	wU16(&p, s_LocalPort);
	wU16(&p, NET_PROTOCOL_VER);
	wU32(&p, SDL_GetTicks());

	struct sockaddr_in dst;
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	dst.sin_port = htons(P2P_LAN_PORT);
	(void)sendto(s_Sock, (const char *)packet, P2P_LAN_PACKET_SIZE, 0,
	             (struct sockaddr *)&dst, sizeof(dst));
}

static void drainReceive(void)
{
	for (;;) {
		u8 packet[256];
		struct sockaddr_in src;
		socklen_t srclen = sizeof(src);
		int n = recvfrom(s_Sock, (char *)packet, sizeof(packet), 0,
		                 (struct sockaddr *)&src, &srclen);
		if (n <= 0) break;
		if (n != P2P_LAN_PACKET_SIZE) continue;
		if (memcmp(packet, P2P_LAN_MAGIC, P2P_LAN_MAGIC_LEN) != 0) continue;

		const u8 *p = packet + P2P_LAN_MAGIC_LEN;
		u8 ver   = rU8(&p);
		u8 flags = rU8(&p);
		(void)rU8(&p); /* pad */
		u32 handle = rU32(&p);
		u32 listen_ipv4 = rU32(&p);
		u16 listen_port = rU16(&p);
		u16 proto = rU16(&p);
		(void)rU32(&p);

		if (ver != P2P_LAN_VERSION) continue;
		/* Ignore our own broadcast. */
		if (handle == socialMyHandle()) continue;
		/* Drop blocked peers immediately. */
		if (socialBlockIsHandle(handle)) continue;
		/* Drop appear-offline peers (they explicitly opt out of presence). */
		if (flags & P2P_LAN_FLAG_OFFLINE) continue;

		const u32 src_ipv4 = ntohl(src.sin_addr.s_addr);
		const u32 ip = (listen_ipv4 != 0) ? listen_ipv4 : src_ipv4;
		const u16 port = listen_port != 0 ? listen_port : (u16)ntohs(src.sin_port);

		if (flags & P2P_LAN_FLAG_BYE) {
			cacheRemove(handle);
			continue;
		}

		/* Drop datagrams whose protocol version is incompatible with ours.
		 * We still cache the peer so the caller can surface the version
		 * mismatch UX (Q14) -- record the IP but flag with a sentinel by
		 * leaving handle but setting port=0 would be ambiguous, so we just
		 * drop here. The version-mismatch UX is exercised at invite time
		 * via a separate path. */
		if (proto != NET_PROTOCOL_VER) continue;

		cacheRecord(handle, ip, port);
	}
}

void p2pLanTick(void)
{
	if (!s_Running) return;

	drainReceive();

	const u32 now_ms = SDL_GetTicks();
	if (s_LastAnnounceMs == 0 || (now_ms - s_LastAnnounceMs) >= P2P_LAN_ANNOUNCE_MS) {
		s_LastAnnounceMs = now_ms;
		social_visibility_t v = socialVisibilityGet();
		u8 flags = 0;
		if (v == SOCIAL_VIS_APPEAR_OFFLINE) flags |= P2P_LAN_FLAG_OFFLINE;
		sendAnnounce(flags);
	}

	cachePrune(now_ms);
}
