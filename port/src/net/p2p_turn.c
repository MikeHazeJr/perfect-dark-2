/**
 * p2p_turn.c -- Tier 5: TURN-style relay through a friend with bandwidth.
 *
 * Per Mike's design (Section 2.4): "Any peer can act as a relay; selection
 * prefers highest measured upload-speed peer with bandwidth headroom."
 * This is custom and intentionally simpler than RFC 5766 -- the relay
 * runs inside another player's pd.exe, not at a public TURN server.
 *
 * Wire format (24 bytes header + N bytes payload):
 *
 *   off len  field
 *   ----------------------------
 *    0  5    magic "PDTRN"
 *    5  1    version (1)
 *    6  1    kind  (0=allocate, 1=allocate-ack, 2=relay, 3=hangup)
 *    7  1    _pad
 *    8  4    initiator handle
 *   12  4    target handle
 *   16  4    nonce
 *   20  4    payload_len  (only for kind=relay)
 *   24  N    payload
 *
 * The relayer pairs (initiator, target) by nonce. Each side sends `relay`
 * frames toward the relayer and the relayer rewrites src/dst and forwards.
 * Bandwidth-aware selection: registered relay candidates are sorted by
 * declared kbps; we pick the highest each Allocate.
 */

#include "net/p2p.h"
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

#define P2P_TURN_PORT       27104
#define P2P_TURN_MAGIC      "PDTRN"
#define P2P_TURN_MAGIC_LEN  5
#define P2P_TURN_HDR_LEN    24
#define P2P_TURN_VERSION    1
#define P2P_TURN_KIND_ALLOC     0
#define P2P_TURN_KIND_ALLOC_ACK 1
#define P2P_TURN_KIND_RELAY     2
#define P2P_TURN_KIND_HANGUP    3

#define TURN_MAX_RELAY_CANDS 16
#define TURN_MAX_INFLIGHT    16

typedef struct {
	u32 peer_handle;
	u32 ipv4;
	u16 port;
	u32 reported_kbps;
	u8  in_use;
} relay_cand_t;

typedef struct {
	u32 pair_id;
	u32 deadline_ms;
	u32 nonce;
	u32 relay_ipv4;
	u16 relay_port;
	u8  in_use;
	u8  awaiting_ack;
} turn_attempt_t;

static relay_cand_t s_Cands[TURN_MAX_RELAY_CANDS];
static turn_attempt_t s_Attempts[TURN_MAX_INFLIGHT];
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
	addr.sin_port = htons(P2P_TURN_PORT);
	if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		addr.sin_port = 0;
		if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
			closesocket(s_Sock);
			s_Sock = INVALID_SOCKET;
			return INVALID_SOCKET;
		}
	}
	s_SocketReady = 1;
	sysLogPrintf(LOG_NOTE, "P2P.TURN: relay socket ready");
	return s_Sock;
}

static relay_cand_t *findRelayCandByPeer(u32 peer_handle)
{
	for (s32 i = 0; i < TURN_MAX_RELAY_CANDS; i++) {
		if (s_Cands[i].in_use && s_Cands[i].peer_handle == peer_handle) return &s_Cands[i];
	}
	return NULL;
}

static relay_cand_t *bestRelayCand(void)
{
	relay_cand_t *best = NULL;
	for (s32 i = 0; i < TURN_MAX_RELAY_CANDS; i++) {
		if (!s_Cands[i].in_use) continue;
		if (s_Cands[i].reported_kbps == 0) continue;
		if (!best || s_Cands[i].reported_kbps > best->reported_kbps ||
			(s_Cands[i].reported_kbps == best->reported_kbps &&
			 s_Cands[i].peer_handle < best->peer_handle)) {
			best = &s_Cands[i];
		}
	}
	return best;
}

static turn_attempt_t *allocAttempt(void)
{
	for (s32 i = 0; i < TURN_MAX_INFLIGHT; i++) {
		if (!s_Attempts[i].in_use) return &s_Attempts[i];
	}
	return NULL;
}

static turn_attempt_t *findAttemptByNonce(u32 nonce)
{
	for (s32 i = 0; i < TURN_MAX_INFLIGHT; i++) {
		if (s_Attempts[i].in_use && s_Attempts[i].nonce == nonce) return &s_Attempts[i];
	}
	return NULL;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void p2pTurnRegisterRelayCandidate(u32 peer_handle, u32 ipv4, u16 port, u32 kbps)
{
	relay_cand_t *c = findRelayCandByPeer(peer_handle);
	if (kbps == 0 || ipv4 == 0 || port == 0) {
		if (c) { c->in_use = 0; }
		return;
	}
	if (!c) {
		for (s32 i = 0; i < TURN_MAX_RELAY_CANDS; i++) {
			if (!s_Cands[i].in_use) { c = &s_Cands[i]; break; }
		}
	}
	if (!c) return;
	c->in_use = 1;
	c->peer_handle = peer_handle;
	c->ipv4 = ipv4;
	c->port = port;
	c->reported_kbps = kbps;
	sysLogPrintf(LOG_NOTE,
	             "P2P.TURN: relay candidate peer=0x%08x kbps=%u",
	             (unsigned)peer_handle, (unsigned)kbps);
}

s32 p2pTurnStart(u32 pair_id)
{
	if (ensureSocket() == INVALID_SOCKET) {
		p2pInternalReportFailure(pair_id, P2P_TIER_TURN, "no socket");
		return -1;
	}

	relay_cand_t *r = bestRelayCand();
	if (!r) {
		p2pInternalReportFailure(pair_id, P2P_TIER_TURN, "no relay available");
		return -1;
	}

	turn_attempt_t *a = allocAttempt();
	if (!a) {
		p2pInternalReportFailure(pair_id, P2P_TIER_TURN, "table full");
		return -1;
	}

	a->in_use = 1;
	a->pair_id = pair_id;
	a->nonce = (u32)SDL_GetTicks() ^ ((u32)pair_id << 8);
	a->relay_ipv4 = r->ipv4;
	a->relay_port = r->port;
	a->deadline_ms = SDL_GetTicks() + P2P_TIER_TIMEOUT_MS;
	a->awaiting_ack = 1;

	/* Send Allocate. The relayer responds with Allocate-Ack. */
	u8 packet[P2P_TURN_HDR_LEN];
	memset(packet, 0, sizeof(packet));
	u8 *w = packet;
	memcpy(w, P2P_TURN_MAGIC, P2P_TURN_MAGIC_LEN); w += P2P_TURN_MAGIC_LEN;
	wU8(&w, P2P_TURN_VERSION);
	wU8(&w, P2P_TURN_KIND_ALLOC);
	wU8(&w, 0);
	wU32(&w, socialMyHandle());
	wU32(&w, r->peer_handle);  /* informational; relay reads target from per-relay rules */
	wU32(&w, a->nonce);
	wU32(&w, 0); /* payload_len */

	struct sockaddr_in dst;
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = htonl(r->ipv4);
	dst.sin_port = htons(r->port);
	(void)sendto(s_Sock, (const char *)packet, P2P_TURN_HDR_LEN, 0,
	             (struct sockaddr *)&dst, sizeof(dst));

	sysLogPrintf(LOG_NOTE,
	             "P2P.TURN: pair=%u allocate via relay 0x%08x (kbps=%u)",
	             (unsigned)pair_id, (unsigned)r->peer_handle,
	             (unsigned)r->reported_kbps);
	return 0;
}

void p2pTurnPoll(void)
{
	if (!s_SocketReady) return;

	for (;;) {
		u8 packet[1500];
		struct sockaddr_in src;
		socklen_t srclen = sizeof(src);
		int n = recvfrom(s_Sock, (char *)packet, sizeof(packet), 0,
		                 (struct sockaddr *)&src, &srclen);
		if (n <= 0) break;
		if (n < P2P_TURN_HDR_LEN) continue;
		if (memcmp(packet, P2P_TURN_MAGIC, P2P_TURN_MAGIC_LEN) != 0) continue;

		const u8 *r = packet + P2P_TURN_MAGIC_LEN;
		u8 ver  = rU8(&r);
		u8 kind = rU8(&r);
		(void)rU8(&r);
		u32 init_handle = rU32(&r);
		u32 targ_handle = rU32(&r);
		u32 nonce       = rU32(&r);
		u32 paylen      = rU32(&r);
		(void)init_handle;
		(void)targ_handle;
		(void)paylen;

		if (ver != P2P_TURN_VERSION) continue;

		if (kind == P2P_TURN_KIND_ALLOC_ACK) {
			turn_attempt_t *a = findAttemptByNonce(nonce);
			if (!a) continue;
			p2p_endpoint_t ep;
			memset(&ep, 0, sizeof(ep));
			ep.ipv4 = 0;
			ep.port = 0;
			ep.flags = P2P_EP_RELAYED;
			ep.relay_ipv4 = a->relay_ipv4;
			ep.relay_port = a->relay_port;
			p2pInternalReportSuccess(a->pair_id, P2P_TIER_TURN, &ep);
			a->in_use = 0;
		}
	}

	const u32 now_ms = SDL_GetTicks();
	for (s32 i = 0; i < TURN_MAX_INFLIGHT; i++) {
		turn_attempt_t *a = &s_Attempts[i];
		if (!a->in_use) continue;
		if (now_ms >= a->deadline_ms) {
			p2pInternalReportFailure(a->pair_id, P2P_TIER_TURN, "relay no ack");
			a->in_use = 0;
		}
	}
}
