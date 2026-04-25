/**
 * p2p_direct.c -- Tier 1: direct UDP probe.
 *
 * Sends a small probe to (hint_ipv4 : hint_port) and waits for the same
 * peer to acknowledge with a matching nonce. The probe is sent on the
 * private p2p socket so we never collide with active ENet traffic.
 *
 * Wire format (16 bytes):
 *
 *   off len  field
 *   ----------------------------
 *    0  5    magic "PDDIR"
 *    5  1    version (1)
 *    6  1    kind (0=probe, 1=ack)
 *    7  1    _pad
 *    8  4    sender handle
 *   12  4    nonce
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

#define P2P_DIRECT_PORT      27102
#define P2P_DIRECT_MAGIC     "PDDIR"
#define P2P_DIRECT_MAGIC_LEN 5
#define P2P_DIRECT_PKT_LEN   16
#define P2P_DIRECT_VERSION   1
#define P2P_DIRECT_KIND_PROBE 0
#define P2P_DIRECT_KIND_ACK   1

#define P2P_DIRECT_MAX_INFLIGHT 32

typedef struct {
	u32 pair_id;
	u32 peer_handle;
	u32 ipv4;
	u16 port;
	u32 nonce;
	u32 deadline_ms;
	u8  in_use;
} direct_attempt_t;

static direct_attempt_t s_Attempts[P2P_DIRECT_MAX_INFLIGHT];
static SOCKET s_Sock = INVALID_SOCKET;
static s32    s_SocketReady;

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static void wU8(u8 **p, u8 v)   { *(*p)++ = v; }
static void wU32(u8 **p, u32 v) {
	(*p)[0]=(u8)v; (*p)[1]=(u8)(v>>8); (*p)[2]=(u8)(v>>16); (*p)[3]=(u8)(v>>24); *p+=4;
}
static u8  rU8 (const u8 **p) { return *(*p)++; }
static u32 rU32(const u8 **p) {
	u32 v = ((u32)((*p)[0])      ) | ((u32)((*p)[1])<< 8) |
	        ((u32)((*p)[2]) << 16) | ((u32)((*p)[3])<<24); *p+=4; return v;
}

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

static SOCKET ensureSocket(void)
{
	if (s_SocketReady) return s_Sock;
	s_Sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (s_Sock == INVALID_SOCKET) return INVALID_SOCKET;
	int yes = 1;
	setsockopt(s_Sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
	socketSetNonblock(s_Sock);
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(P2P_DIRECT_PORT);
	if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		/* Port-in-use is fine -- bind ephemeral. */
		addr.sin_port = 0;
		if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
			closesocket(s_Sock);
			s_Sock = INVALID_SOCKET;
			return INVALID_SOCKET;
		}
	}
	s_SocketReady = 1;
	sysLogPrintf(LOG_NOTE, "P2P.DIRECT: probe socket ready");
	return s_Sock;
}

static direct_attempt_t *findFreeSlot(void)
{
	for (s32 i = 0; i < P2P_DIRECT_MAX_INFLIGHT; i++) {
		if (!s_Attempts[i].in_use) return &s_Attempts[i];
	}
	return NULL;
}

static direct_attempt_t *findByNonce(u32 nonce)
{
	for (s32 i = 0; i < P2P_DIRECT_MAX_INFLIGHT; i++) {
		if (s_Attempts[i].in_use && s_Attempts[i].nonce == nonce) return &s_Attempts[i];
	}
	return NULL;
}

static direct_attempt_t *findByPair(u32 pair_id)
{
	for (s32 i = 0; i < P2P_DIRECT_MAX_INFLIGHT; i++) {
		if (s_Attempts[i].in_use && s_Attempts[i].pair_id == pair_id) return &s_Attempts[i];
	}
	return NULL;
}

static void sendProbe(direct_attempt_t *a, u8 kind)
{
	SOCKET s = ensureSocket();
	if (s == INVALID_SOCKET) return;

	u8 packet[P2P_DIRECT_PKT_LEN];
	u8 *p = packet;
	memcpy(p, P2P_DIRECT_MAGIC, P2P_DIRECT_MAGIC_LEN); p += P2P_DIRECT_MAGIC_LEN;
	wU8(&p, P2P_DIRECT_VERSION);
	wU8(&p, kind);
	wU8(&p, 0);
	wU32(&p, socialMyHandle());
	wU32(&p, a->nonce);

	struct sockaddr_in dst;
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = htonl(a->ipv4);
	dst.sin_port = htons(a->port);
	(void)sendto(s, (const char *)packet, P2P_DIRECT_PKT_LEN, 0,
	             (struct sockaddr *)&dst, sizeof(dst));
}

/* -------------------------------------------------------------------------
 * Public entry point (called by p2p.c)
 * ------------------------------------------------------------------------- */

s32 p2pDirectStart(u32 pair_id, u32 ipv4, u16 port)
{
	if (ipv4 == 0 || port == 0) {
		p2pInternalReportFailure(pair_id, P2P_TIER_DIRECT, "no hint");
		return -1;
	}
	if (ensureSocket() == INVALID_SOCKET) {
		p2pInternalReportFailure(pair_id, P2P_TIER_DIRECT, "socket");
		return -1;
	}

	/* Replace any existing attempt for this pair. */
	direct_attempt_t *a = findByPair(pair_id);
	if (!a) a = findFreeSlot();
	if (!a) {
		p2pInternalReportFailure(pair_id, P2P_TIER_DIRECT, "table full");
		return -1;
	}

	a->in_use = 1;
	a->pair_id = pair_id;
	a->peer_handle = 0; /* filled when ack arrives */
	a->ipv4 = ipv4;
	a->port = port;
	a->nonce = (u32)SDL_GetTicks() ^ ((u32)pair_id << 16);
	a->deadline_ms = SDL_GetTicks() + P2P_TIER_TIMEOUT_MS;

	sendProbe(a, P2P_DIRECT_KIND_PROBE);
	return 0;
}

/* -------------------------------------------------------------------------
 * Tick (called from p2pTick via the orchestrator -- there's no hook
 * today, so we drain on socket-readiness via the existing tier-1 path
 * embedded inside the orchestrator. We still expose this entry for
 * future scheduling needs.)
 * ------------------------------------------------------------------------- */

void p2pDirectPoll(void)
{
	if (!s_SocketReady) return;

	for (;;) {
		u8 packet[64];
		struct sockaddr_in src;
		socklen_t srclen = sizeof(src);
		int n = recvfrom(s_Sock, (char *)packet, sizeof(packet), 0,
		                 (struct sockaddr *)&src, &srclen);
		if (n <= 0) break;
		if (n != P2P_DIRECT_PKT_LEN) continue;
		if (memcmp(packet, P2P_DIRECT_MAGIC, P2P_DIRECT_MAGIC_LEN) != 0) continue;

		const u8 *p = packet + P2P_DIRECT_MAGIC_LEN;
		u8 ver   = rU8(&p);
		u8 kind  = rU8(&p);
		(void)rU8(&p);
		u32 from_handle = rU32(&p);
		u32 nonce = rU32(&p);

		if (ver != P2P_DIRECT_VERSION) continue;
		if (socialBlockIsHandle(from_handle)) continue;

		if (kind == P2P_DIRECT_KIND_PROBE) {
			/* Reflect an ack to the source. */
			direct_attempt_t reply;
			memset(&reply, 0, sizeof(reply));
			reply.ipv4 = ntohl(src.sin_addr.s_addr);
			reply.port = ntohs(src.sin_port);
			reply.nonce = nonce;
			sendProbe(&reply, P2P_DIRECT_KIND_ACK);
			continue;
		}

		if (kind == P2P_DIRECT_KIND_ACK) {
			direct_attempt_t *a = findByNonce(nonce);
			if (!a) continue;
			p2p_endpoint_t ep;
			memset(&ep, 0, sizeof(ep));
			ep.ipv4 = a->ipv4;
			ep.port = a->port;
			ep.flags = P2P_EP_DIRECT;
			p2pInternalReportSuccess(a->pair_id, P2P_TIER_DIRECT, &ep);
			a->in_use = 0;
		}
	}

	const u32 now_ms = SDL_GetTicks();
	for (s32 i = 0; i < P2P_DIRECT_MAX_INFLIGHT; i++) {
		direct_attempt_t *a = &s_Attempts[i];
		if (!a->in_use) continue;
		if (now_ms >= a->deadline_ms) {
			p2pInternalReportFailure(a->pair_id, P2P_TIER_DIRECT, "no ack");
			a->in_use = 0;
		}
	}
}
