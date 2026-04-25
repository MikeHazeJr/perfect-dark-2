/**
 * p2p_ice.c -- Tier 4: ICE-style candidate gathering and pair testing.
 *
 * Each pair maintains a candidate list. Sources:
 *   - host candidates: every NIC-local ipv4 we can enumerate (Phase 1
 *     focuses on the default NIC; multi-homed pair gathering is a
 *     phase-2 enhancement).
 *   - srflx candidate: STUN reflexive endpoint published by p2p_stun.c.
 *   - prflx candidates: peer-supplied via p2pIceAddPeerCandidate.
 *
 * The orchestrator's escalator drives entry to ICE via p2pIceStart. We
 * then probe every candidate in parallel using the same
 * p2p_direct.c-style probe packet. The first success wins; remaining
 * probes are abandoned.
 *
 * For Phase 1 we limit to ICE_MAX_CANDS candidates per pair. Real ICE
 * connectivity-checks include round-trip latency ranking; we approximate
 * by taking the first responder and recording RTT for diagnostics.
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
  #define closesocket close
  typedef int SOCKET;
  #define INVALID_SOCKET (-1)
#endif

#define P2P_ICE_PORT          27103
#define P2P_ICE_MAGIC         "PDICE"
#define P2P_ICE_MAGIC_LEN     5
#define P2P_ICE_PKT_LEN       16
#define P2P_ICE_VERSION       1
#define P2P_ICE_KIND_PROBE    0
#define P2P_ICE_KIND_ACK      1

#define ICE_MAX_PAIRS         32
#define ICE_MAX_CANDS         8

typedef struct {
	u32 ipv4;
	u16 port;
	u8  tried;
	u8  source; /* 0=host, 1=srflx, 2=prflx */
} ice_cand_t;

typedef struct {
	u32 pair_id;
	u32 peer_handle;
	u32 nonce_base;
	u32 deadline_ms;
	u8  in_use;
	u8  ncands;
	ice_cand_t cands[ICE_MAX_CANDS];
} ice_pair_t;

static ice_pair_t s_Pairs[ICE_MAX_PAIRS];
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
	addr.sin_port = htons(P2P_ICE_PORT);
	if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		addr.sin_port = 0;
		if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
			closesocket(s_Sock);
			s_Sock = INVALID_SOCKET;
			return INVALID_SOCKET;
		}
	}
	s_SocketReady = 1;
	sysLogPrintf(LOG_NOTE, "P2P.ICE: probe socket ready");
	return s_Sock;
}

static ice_pair_t *findPair(u32 pair_id)
{
	for (s32 i = 0; i < ICE_MAX_PAIRS; i++) {
		if (s_Pairs[i].in_use && s_Pairs[i].pair_id == pair_id) return &s_Pairs[i];
	}
	return NULL;
}

static ice_pair_t *allocPair(void)
{
	for (s32 i = 0; i < ICE_MAX_PAIRS; i++) {
		if (!s_Pairs[i].in_use) return &s_Pairs[i];
	}
	return NULL;
}

static void addCand(ice_pair_t *p, u32 ipv4, u16 port, u8 source)
{
	if (ipv4 == 0 || port == 0) return;
	for (u8 i = 0; i < p->ncands; i++) {
		if (p->cands[i].ipv4 == ipv4 && p->cands[i].port == port) return;
	}
	if (p->ncands >= ICE_MAX_CANDS) return;
	p->cands[p->ncands].ipv4 = ipv4;
	p->cands[p->ncands].port = port;
	p->cands[p->ncands].tried = 0;
	p->cands[p->ncands].source = source;
	p->ncands++;
}

static u32 getLocalNicIpv4(void)
{
	/* Best-effort: connect a UDP socket to a public address to pick the
	 * default NIC, then read the local sockaddr. Doesn't actually send. */
	SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == INVALID_SOCKET) return 0;
	struct sockaddr_in dst;
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = htonl(0x08080808); /* 8.8.8.8 */
	dst.sin_port = htons(53);
	if (connect(s, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
		closesocket(s);
		return 0;
	}
	struct sockaddr_in local;
	socklen_t llen = sizeof(local);
	if (getsockname(s, (struct sockaddr *)&local, &llen) != 0) {
		closesocket(s);
		return 0;
	}
	closesocket(s);
	return ntohl(local.sin_addr.s_addr);
}

static void sendProbe(ice_pair_t *p, ice_cand_t *c, u8 kind, u32 nonce)
{
	SOCKET s = ensureSocket();
	if (s == INVALID_SOCKET) return;

	u8 packet[P2P_ICE_PKT_LEN];
	u8 *w = packet;
	memcpy(w, P2P_ICE_MAGIC, P2P_ICE_MAGIC_LEN); w += P2P_ICE_MAGIC_LEN;
	wU8(&w, P2P_ICE_VERSION);
	wU8(&w, kind);
	wU8(&w, 0);
	wU32(&w, socialMyHandle());
	wU32(&w, nonce);

	struct sockaddr_in dst;
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = htonl(c->ipv4);
	dst.sin_port = htons(c->port);
	(void)sendto(s, (const char *)packet, P2P_ICE_PKT_LEN, 0,
	             (struct sockaddr *)&dst, sizeof(dst));

	(void)p;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

s32 p2pIceStart(u32 pair_id)
{
	if (ensureSocket() == INVALID_SOCKET) {
		p2pInternalReportFailure(pair_id, P2P_TIER_ICE, "no socket");
		return -1;
	}

	ice_pair_t *p = findPair(pair_id);
	if (!p) {
		p = allocPair();
		if (!p) {
			p2pInternalReportFailure(pair_id, P2P_TIER_ICE, "table full");
			return -1;
		}
	}

	memset(p, 0, sizeof(*p));
	p->in_use = 1;
	p->pair_id = pair_id;
	p->nonce_base = (u32)SDL_GetTicks() ^ ((u32)pair_id << 4);
	p->deadline_ms = SDL_GetTicks() + P2P_TIER_TIMEOUT_MS;

	/* Host candidate. */
	u32 host_ipv4 = getLocalNicIpv4();
	addCand(p, host_ipv4, P2P_ICE_PORT, 0);

	/* Server-reflexive candidate from STUN. */
	u32 srflx_ipv4 = p2pMyReflexiveIpv4();
	u16 srflx_port = p2pMyReflexivePort();
	addCand(p, srflx_ipv4, srflx_port, 1);

	if (p->ncands == 0) {
		p2pInternalReportFailure(pair_id, P2P_TIER_ICE, "no candidates");
		p->in_use = 0;
		return -1;
	}

	/* Probe each candidate. */
	for (u8 i = 0; i < p->ncands; i++) {
		p->cands[i].tried = 1;
		sendProbe(p, &p->cands[i], P2P_ICE_KIND_PROBE, p->nonce_base + i);
	}
	sysLogPrintf(LOG_NOTE, "P2P.ICE: pair=%u probing %u candidates",
	             (unsigned)pair_id, (unsigned)p->ncands);
	return 0;
}

s32 p2pIceAddPeerCandidate(u32 pair_id, u32 ipv4, u16 port)
{
	ice_pair_t *p = findPair(pair_id);
	if (!p) {
		p = allocPair();
		if (!p) return -1;
		memset(p, 0, sizeof(*p));
		p->in_use = 1;
		p->pair_id = pair_id;
		p->nonce_base = (u32)SDL_GetTicks() ^ ((u32)pair_id << 4);
		p->deadline_ms = SDL_GetTicks() + P2P_TIER_TIMEOUT_MS;
	}
	addCand(p, ipv4, port, 2);

	/* If we are already past p2pIceStart, immediately probe the new candidate. */
	for (u8 i = 0; i < p->ncands; i++) {
		if (!p->cands[i].tried) {
			p->cands[i].tried = 1;
			sendProbe(p, &p->cands[i], P2P_ICE_KIND_PROBE, p->nonce_base + i);
			break;
		}
	}
	return 0;
}

void p2pIcePoll(void)
{
	if (!s_SocketReady) return;

	for (;;) {
		u8 packet[64];
		struct sockaddr_in src;
		socklen_t srclen = sizeof(src);
		int n = recvfrom(s_Sock, (char *)packet, sizeof(packet), 0,
		                 (struct sockaddr *)&src, &srclen);
		if (n <= 0) break;
		if (n != P2P_ICE_PKT_LEN) continue;
		if (memcmp(packet, P2P_ICE_MAGIC, P2P_ICE_MAGIC_LEN) != 0) continue;

		const u8 *r = packet + P2P_ICE_MAGIC_LEN;
		u8 ver  = rU8(&r);
		u8 kind = rU8(&r);
		(void)rU8(&r);
		u32 from_handle = rU32(&r);
		u32 nonce = rU32(&r);
		(void)from_handle;

		if (ver != P2P_ICE_VERSION) continue;

		if (kind == P2P_ICE_KIND_PROBE) {
			ice_cand_t reply;
			memset(&reply, 0, sizeof(reply));
			reply.ipv4 = ntohl(src.sin_addr.s_addr);
			reply.port = ntohs(src.sin_port);
			ice_pair_t reply_pair;
			memset(&reply_pair, 0, sizeof(reply_pair));
			sendProbe(&reply_pair, &reply, P2P_ICE_KIND_ACK, nonce);
			continue;
		}

		if (kind == P2P_ICE_KIND_ACK) {
			for (s32 i = 0; i < ICE_MAX_PAIRS; i++) {
				ice_pair_t *p = &s_Pairs[i];
				if (!p->in_use) continue;
				/* Match nonce range to identify the candidate. */
				if (nonce < p->nonce_base) continue;
				u32 idx = nonce - p->nonce_base;
				if (idx >= p->ncands) continue;

				p2p_endpoint_t ep;
				memset(&ep, 0, sizeof(ep));
				ep.ipv4 = p->cands[idx].ipv4;
				ep.port = p->cands[idx].port;
				ep.flags = P2P_EP_ICE_PAIR;
				p2pInternalReportSuccess(p->pair_id, P2P_TIER_ICE, &ep);
				p->in_use = 0;
				break;
			}
		}
	}

	const u32 now_ms = SDL_GetTicks();
	for (s32 i = 0; i < ICE_MAX_PAIRS; i++) {
		ice_pair_t *p = &s_Pairs[i];
		if (!p->in_use) continue;
		if (now_ms >= p->deadline_ms) {
			p2pInternalReportFailure(p->pair_id, P2P_TIER_ICE, "ice timeout");
			p->in_use = 0;
		}
	}
}
