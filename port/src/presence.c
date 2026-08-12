/**
 * presence.c -- Always-on peer-to-peer presence ping/pong + invite queue.
 *
 * Phase 1 of the connectivity rollout. Single connectionless UDP socket
 * on port 27105. Frame format (signed; 196 bytes total):
 *
 *   off len  field
 *   ----------------------------
 *    0  5    magic "PDPRS"
 *    5  1    version (5 -- typed signed ENet match route added 2026-08-12)
 *    6  1    kind  (0=ping, 1=pong, 2=invite, 3=invite-resp, 4=bye,
 *                  5=match-route publish/clear)
 *    7  1    state (presence_state_t)
 *    8  4    sender handle
 *   12  4    target handle (0 = broadcast)
 *   16  2    netproto version
 *   18  1    invite_kind / invite_response (0/1)
 *   19  1    input_class (privacy-safe ACTIONMAP_INPUT_CLASS_*)
 *   20  4    nonce
 *   24 16    sender agent name (utf-8, null-padded)
 *   40 44    status blurb (utf-8, null-padded)
 *   84  4    passive upload estimate (kbps; zero = unavailable)
 *   88  4    match-route IPv4 (host order; zero for source-derived)
 *   92  2    match-route ENet listen port
 *   94  1    match-route type flags
 *   95  1    reserved zero
 *   96  4    match-route issue time (Unix seconds)
 *  100 32    sender Ed25519 pubkey (matches legacy pubkey handle or
 *            per-agent SHA256(pubkey||agent_name) handle)
 *  132 64    Ed25519 signature over bytes[0..132) + the domain string
 *  196
 *
 * Signature domain separator: "pd-presence-v5".
 *
 * The 196-byte frame is well within the 1500-byte unfragmented UDP MTU.
 */

#include "presence.h"
#include "social.h"
#include "identity.h"
#include "ed25519.h"
#include "chat.h"
#include "pdgui_toast.h"
#include "actionmap.h"
#include "net/p2p.h"
#include "net/net.h"
#include "net/group_session.h"
#include "net/net_bandwidth.h"
#include "system.h"
#include "smoke_harness.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

#define PRESENCE_PORT             27105
#define PRESENCE_MAGIC            "PDPRS"
#define PRESENCE_MAGIC_LEN        5
#define PRESENCE_VERSION          5  /* v5 (2026-08-12): signed typed ENet match route */
#define PRESENCE_BODY_LEN         132 /* bytes that the signature covers (0..132) */
#define PRESENCE_PUBKEY_OFFSET    100
#define PRESENCE_PUBKEY_LEN       32
#define PRESENCE_SIG_OFFSET       132
#define PRESENCE_SIG_LEN          64
#define PRESENCE_FRAME_LEN        196
#define PRESENCE_AGENT_OFFSET     24
#define PRESENCE_AGENT_LEN        SOCIAL_AGENTNAME_MAX
#define PRESENCE_STATUS_OFFSET    (PRESENCE_AGENT_OFFSET + PRESENCE_AGENT_LEN)
#define PRESENCE_UPLOAD_KBPS_OFFSET 84
#define PRESENCE_MATCH_ROUTE_IPV4_OFFSET 88
#define PRESENCE_MATCH_ROUTE_PORT_OFFSET 92
#define PRESENCE_MATCH_ROUTE_FLAGS_OFFSET 94
#define PRESENCE_MATCH_ROUTE_ISSUED_OFFSET 96
#define PRESENCE_BLURB_LEN        64
#define PRESENCE_STATUS_LEN       (PRESENCE_UPLOAD_KBPS_OFFSET - PRESENCE_STATUS_OFFSET)
#define PRESENCE_SIG_DOMAIN       "pd-presence-v5"
#define PRESENCE_SIG_DOMAIN_LEN   14
#define PRESENCE_ENDPOINT_TTL_S   300 /* 5 minutes -- decision: connectivity-phase1-decisions.md */

#define PRESENCE_KIND_PING        0
#define PRESENCE_KIND_PONG        1
#define PRESENCE_KIND_INVITE      2
#define PRESENCE_KIND_INVITE_RESP 3
#define PRESENCE_KIND_BYE         4
#define PRESENCE_KIND_MATCH_ROUTE 5

#define PRESENCE_PING_INTERVAL_MS 30000  /* ping each friend every 30 s */
#define PRESENCE_PONG_FRESH_MS    60000  /* pong'd within last 60 s = online */
#define PRESENCE_RATE_WINDOW_MS    5000  /* per-source minimum ping interval */
#define PRESENCE_RATE_BUCKETS       64
#define PRESENCE_PEER_CAP           SOCIAL_FRIENDS_MAX
#define PRESENCE_PENDING_CAP        16
#define PRESENCE_INVITE_CAP         16
#define PRESENCE_INVITE_TTL_MS    180000 /* 3 min */
#define PRESENCE_INVITE_TIMEOUT_MS 5000

typedef struct {
	u32 handle;
	u32 last_recv_ms;
} rate_bucket_t;

typedef struct {
	u32 handle;
	u32 added_ms;
	u8  in_use;
} pending_invite_t;

typedef struct {
	u32 handle;
	u32 last_ping_ms;
} ping_schedule_t;

static SOCKET s_Sock = INVALID_SOCKET;
static s32    s_SocketReady;
static u16    s_LocalPort = PRESENCE_PORT;
static s32    s_LocalPortRequired;

static presence_state_t s_LocalState = PRESENCE_OFFLINE;
static char   s_LocalBlurb[PRESENCE_BLURB_LEN];
static u32    s_LocalAgentRecord_ms;
static net_match_route_t s_LocalMatchRoute;

static presence_peer_t  s_Peers[PRESENCE_PEER_CAP];
static s32              s_NumPeers;

static rate_bucket_t    s_RateBuckets[PRESENCE_RATE_BUCKETS];

static pending_invite_t s_Pending[PRESENCE_PENDING_CAP];

static presence_invite_t s_Inbox[PRESENCE_INVITE_CAP];
static s32               s_NumInbox;

static ping_schedule_t  s_Schedule[PRESENCE_PEER_CAP];
static s32              s_NumScheduled;

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

const char *presenceStateName(presence_state_t s)
{
	switch (s) {
		case PRESENCE_OFFLINE:        return "offline";
		case PRESENCE_BOOTSTRAP:      return "bootstrap";
		case PRESENCE_ONLINE_IDLE:    return "online";
		case PRESENCE_IN_MATCH:       return "in-match";
		case PRESENCE_IN_MISSION:     return "in-mission";
		case PRESENCE_SPECTATING:     return "spectating";
		case PRESENCE_APPEAR_OFFLINE: return "appear-offline";
		default: return "?";
	}
}

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

static void copyFixedString(char *out, u32 outsize, const u8 *src, u32 srclen)
{
	if (!out || outsize == 0) return;
	u32 n = 0;
	if (src) {
		while (n + 1 < outsize && n < srclen && src[n] != '\0') {
			out[n] = (char)src[n];
			n++;
		}
	}
	out[n] = '\0';
}

static void writeFixedString(u8 *dst, u32 dstlen, const char *src)
{
	if (!dst || dstlen == 0) return;
	memset(dst, 0, dstlen);
	if (!src || !src[0]) return;
	size_t n = strlen(src);
	if (n > dstlen - 1) n = dstlen - 1;
	memcpy(dst, src, n);
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
	addr.sin_port = htons(s_LocalPort);
	if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		if (s_LocalPortRequired) {
			closesocket(s_Sock);
			s_Sock = INVALID_SOCKET;
			sysLogPrintf(LOG_ERROR,
				"PRESENCE: required smoke port %u unavailable",
				(unsigned)s_LocalPort);
			return INVALID_SOCKET;
		}
		addr.sin_port = 0;
		if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
			closesocket(s_Sock);
			s_Sock = INVALID_SOCKET;
			return INVALID_SOCKET;
		}
	}
	s_SocketReady = 1;
	sysLogPrintf(LOG_NOTE, "PRESENCE: socket bound on UDP %u", (unsigned)s_LocalPort);
	return s_Sock;
}

static presence_peer_t *findPeer(u32 handle)
{
	for (s32 i = 0; i < s_NumPeers; i++) {
		if (s_Peers[i].handle == handle) return &s_Peers[i];
	}
	return NULL;
}

static presence_peer_t *touchPeer(u32 handle)
{
	presence_peer_t *p = findPeer(handle);
	if (p) return p;
	if (s_NumPeers >= PRESENCE_PEER_CAP) return NULL;
	p = &s_Peers[s_NumPeers++];
	memset(p, 0, sizeof(*p));
	p->handle = handle;
	p->state = PRESENCE_OFFLINE;
	return p;
}

static s32 acceptFromHandle(u32 handle)
{
	if (handle == 0 || handle == socialMyHandle()) return 0;
	if (socialBlockIsHandle(handle)) return 0;
	if (socialFriendByHandle(handle)) return 1;
	for (s32 i = 0; i < PRESENCE_PENDING_CAP; i++) {
		if (s_Pending[i].in_use && s_Pending[i].handle == handle) return 1;
	}
	return 0;
}

static s32 rateLimitAllow(u32 handle)
{
	const u32 now = SDL_GetTicks();
	rate_bucket_t *empty = NULL;
	rate_bucket_t *oldest = &s_RateBuckets[0];
	for (s32 i = 0; i < PRESENCE_RATE_BUCKETS; i++) {
		rate_bucket_t *b = &s_RateBuckets[i];
		if (b->handle == handle) {
			if (now - b->last_recv_ms < PRESENCE_RATE_WINDOW_MS) return 0;
			b->last_recv_ms = now;
			return 1;
		}
		if (!b->handle && !empty) empty = b;
		if (b->last_recv_ms < oldest->last_recv_ms) oldest = b;
	}
	rate_bucket_t *slot = empty ? empty : oldest;
	slot->handle = handle;
	slot->last_recv_ms = now;
	return 1;
}

/* Sign a frame body (the first PRESENCE_BODY_LEN bytes) plus the domain
 * separator. The domain prevents signature reuse across protocols that
 * happen to share the same byte prefix. */
static s32 signFrame(const u8 *body, u8 *outSig)
{
	if (!body || !outSig) return 0;
	if (!identityGetPubkey()) return 0;
	u8 buf[PRESENCE_BODY_LEN + PRESENCE_SIG_DOMAIN_LEN];
	memcpy(buf, body, PRESENCE_BODY_LEN);
	memcpy(buf + PRESENCE_BODY_LEN, PRESENCE_SIG_DOMAIN, PRESENCE_SIG_DOMAIN_LEN);
	return identitySign(buf, sizeof(buf), outSig);
}

static s32 verifyFrame(const u8 *body, const u8 *sig, const u8 *pubkey)
{
	if (!body || !sig || !pubkey) return 0;
	u8 buf[PRESENCE_BODY_LEN + PRESENCE_SIG_DOMAIN_LEN];
	memcpy(buf, body, PRESENCE_BODY_LEN);
	memcpy(buf + PRESENCE_BODY_LEN, PRESENCE_SIG_DOMAIN, PRESENCE_SIG_DOMAIN_LEN);
	return ed25519Verify(sig, buf, sizeof(buf), pubkey) == 1 ? 1 : 0;
}

static void sendFrame(u32 ipv4, u16 port, u8 kind, u32 target_handle,
                       u32 nonce, u8 invite_kind, const char *status_blurb)
{
	if (!s_SocketReady) return;
	const u8 *mypub = identityGetPubkey();
	if (!mypub) return; /* no keypair = nothing to sign */

	u8 packet[PRESENCE_FRAME_LEN];
	memset(packet, 0, sizeof(packet));
	u8 *w = packet;
	memcpy(w, PRESENCE_MAGIC, PRESENCE_MAGIC_LEN); w += PRESENCE_MAGIC_LEN;
	wU8(&w, PRESENCE_VERSION);
	wU8(&w, kind);
	wU8(&w, (u8)s_LocalState);
	wU32(&w, socialMyHandle());
	wU32(&w, target_handle);
	wU16(&w, NET_PROTOCOL_VER);
	wU16(&w, (u16)(((u16)actionmapGetLastInputClass() << 8) | invite_kind));
	/* Route updates and explicit clears use the signed nonce as a monotonic
	 * within-process sequence for same-second ordering/replay rejection. */
	const u32 frame_nonce = s_LocalMatchRoute.port != 0 ||
		kind == PRESENCE_KIND_MATCH_ROUTE ? SDL_GetTicks() : nonce;
	wU32(&w, frame_nonce);
	writeFixedString(packet + PRESENCE_AGENT_OFFSET, PRESENCE_AGENT_LEN,
	                 socialMyAgentName());
	writeFixedString(packet + PRESENCE_STATUS_OFFSET, PRESENCE_STATUS_LEN,
	                 status_blurb);
	u8 *upload_w = packet + PRESENCE_UPLOAD_KBPS_OFFSET;
	/* Once a group invite freezes its election input, every subsequent signed
	 * frame (including MATCH_ROUTE) must repeat that same claim. Otherwise a
	 * route publication before ENet traffic exists can overwrite the peer with
	 * a zero live estimate and split the pre-connect election. Outside an
	 * election this accessor simply samples the current persisted estimate. */
	const u32 upload_kbps = groupSessionLocalElectionKbps();
	wU32(&upload_w, upload_kbps);
	if (s_LocalMatchRoute.port != 0) {
		net_match_route_t route = s_LocalMatchRoute;
		const time_t now_time = time(NULL);
		if (now_time <= 0 || (u64)now_time > 0xffffffffu) return;
		route.issued_unix_seconds = (u32)now_time;
		u8 *route_ipv4_w = packet + PRESENCE_MATCH_ROUTE_IPV4_OFFSET;
		u8 *route_port_w = packet + PRESENCE_MATCH_ROUTE_PORT_OFFSET;
		u8 *route_issued_w = packet + PRESENCE_MATCH_ROUTE_ISSUED_OFFSET;
		wU32(&route_ipv4_w, route.ipv4);
		wU16(&route_port_w, route.port);
		packet[PRESENCE_MATCH_ROUTE_FLAGS_OFFSET] = route.flags;
		wU32(&route_issued_w, route.issued_unix_seconds);
	} else if (kind == PRESENCE_KIND_MATCH_ROUTE) {
		/* A clear is a separately signed/fresh route update: zero route fields
		 * plus a nonzero issue time. */
		u8 *route_issued_w = packet + PRESENCE_MATCH_ROUTE_ISSUED_OFFSET;
		const time_t now_time = time(NULL);
		if (now_time <= 0 || (u64)now_time > 0xffffffffu) return;
		wU32(&route_issued_w, (u32)now_time);
	}
	memcpy(packet + PRESENCE_PUBKEY_OFFSET, mypub, PRESENCE_PUBKEY_LEN);

	if (!signFrame(packet, packet + PRESENCE_SIG_OFFSET)) {
		sysLogPrintf(LOG_WARNING, "PRESENCE: failed to sign outbound frame");
		return;
	}

	struct sockaddr_in dst;
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = htonl(ipv4);
	dst.sin_port = htons(port);
	(void)sendto(s_Sock, (const char *)packet, PRESENCE_FRAME_LEN, 0,
	             (struct sockaddr *)&dst, sizeof(dst));
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

/* Mike directive 2026-05-17: presence + social-hub stays cold until an
 * agent profile is loaded. The keypair-derived handle that drives
 * outgoing pings is per-agent in spirit (per the connect-code-is-agent-
 * specific contract), so emitting pings before the agent is selected
 * would announce a placeholder identity to friends. Cold state =
 * PRESENCE_BOOTSTRAP; tick early-returns; outbound socket allocated but
 * idle. Flipped by presenceMarkAgentLoaded() called from prefsAgentLoad. */
static s32 s_AgentConfirmed = 0;

void presenceInit(void)
{
	s_LocalPort = PRESENCE_PORT;
	s_LocalPortRequired = 0;
	const char *debug_port = sysArgGetString("--debug-presence-port");
	if (debug_port && debug_port[0] && smokeHarnessIsActive()) {
		char *end = NULL;
		const long parsed = strtol(debug_port, &end, 10);
		if (end && *end == '\0' && parsed > 0 && parsed <= 65535) {
			s_LocalPort = (u16)parsed;
			s_LocalPortRequired = 1;
			sysLogPrintf(LOG_NOTE,
				"PRESENCE: smoke-local port override=%u",
				(unsigned)s_LocalPort);
		} else {
			sysLogPrintf(LOG_WARNING,
				"PRESENCE: invalid --debug-presence-port '%s' ignored",
				debug_port);
		}
	}
	memset(s_Peers, 0, sizeof(s_Peers));
	memset(s_RateBuckets, 0, sizeof(s_RateBuckets));
	memset(s_Pending, 0, sizeof(s_Pending));
	memset(s_Inbox, 0, sizeof(s_Inbox));
	memset(s_Schedule, 0, sizeof(s_Schedule));
	s_NumPeers = 0;
	s_NumInbox = 0;
	s_NumScheduled = 0;
	s_LocalBlurb[0] = '\0';
	memset(&s_LocalMatchRoute, 0, sizeof(s_LocalMatchRoute));
	s_LocalState = PRESENCE_BOOTSTRAP;
	s_AgentConfirmed = 0;
	(void)ensureSocket();

	/* Seed schedule from current friend list. */
	const s32 nf = socialFriendCount();
	for (s32 i = 0; i < nf && s_NumScheduled < PRESENCE_PEER_CAP; i++) {
		const social_friend_t *f = socialFriendAt(i);
		if (!f) continue;
		s_Schedule[s_NumScheduled].handle = f->handle;
		s_Schedule[s_NumScheduled].last_ping_ms = 0;
		s_NumScheduled++;
	}

	/* Stay in PRESENCE_BOOTSTRAP until the agent-load callback flips the
	 * gate. Visibility setting still applies on the eventual flip. */
	sysLogPrintf(LOG_NOTE, "PRESENCE: initialised state=%s scheduled=%d (awaiting agent load)",
	             presenceStateName(s_LocalState), (int)s_NumScheduled);
}

void presenceMarkAgentLoaded(void)
{
	if (s_AgentConfirmed) return;
	s_AgentConfirmed = 1;
	if (socialVisibilityGet() == SOCIAL_VIS_APPEAR_OFFLINE) {
		s_LocalState = PRESENCE_APPEAR_OFFLINE;
	} else {
		/* Start in online-idle; mainChangeToStage flips to IN_MATCH
		 * when (gameplay stage AND looksLikeMP OR mission_active).
		 * Earlier version stage-checked here too but mistook
		 * STAGE_CITRAINING (the OG main-menu backdrop) for an active
		 * match. Mike playtest 2026-05-17 confirmed the regression. */
		s_LocalState = PRESENCE_ONLINE_IDLE;
	}
	sysLogPrintf(LOG_NOTE,
	             "PRESENCE: agent loaded -- state=%s, social hub activity enabled",
	             presenceStateName(s_LocalState));
}

s32 presenceIsAgentLoaded(void) { return s_AgentConfirmed; }

void presenceShutdown(void)
{
	if (!s_SocketReady) return;

	/* Send BYE to every cached peer endpoint. */
	for (s32 i = 0; i < s_NumPeers; i++) {
		presence_peer_t *p = &s_Peers[i];
		if (p->cached_ipv4 == 0 || p->cached_port == 0) continue;
		sendFrame(p->cached_ipv4, p->cached_port, PRESENCE_KIND_BYE,
		          p->handle, 0, 0, s_LocalBlurb);
	}

	closesocket(s_Sock);
	s_Sock = INVALID_SOCKET;
	s_SocketReady = 0;
	s_LocalState = PRESENCE_OFFLINE;
}

void presenceSetLocalState(presence_state_t s)
{
	if (s == s_LocalState) return;
	s_LocalState = s;
	sysLogPrintf(LOG_NOTE, "PRESENCE: local state -> %s", presenceStateName(s));
}

presence_state_t presenceGetLocalState(void) { return s_LocalState; }

void presenceSetLocalBlurb(const char *b)
{
	if (!b) { s_LocalBlurb[0] = '\0'; return; }
	strncpy(s_LocalBlurb, b, PRESENCE_BLURB_LEN - 1);
	s_LocalBlurb[PRESENCE_BLURB_LEN - 1] = '\0';
}

const char *presenceGetLocalBlurb(void) { return s_LocalBlurb; }

/* -------------------------------------------------------------------------
 * Inbound dispatch
 * ------------------------------------------------------------------------- */

static void recordPong(u32 handle, u8 state, u16 proto, u8 input_class,
                        u32 upload_kbps,
                        u32 src_ipv4, u16 src_port, const char *agent,
                        const char *status_blurb,
						const net_match_route_t *match_route,
						s32 clear_match_route, u32 route_issued,
						u32 route_nonce)
{
	presence_peer_t *p = touchPeer(handle);
	if (!p) return;
	const presence_state_t prev_state = p->state;
	const u32              prev_pong  = p->last_pong_ms;

	p->last_pong_ms = SDL_GetTicks();
	p->state = (presence_state_t)state;
	p->proto_version = proto;
	p->input_class = input_class;
	p->upload_kbps = upload_kbps <= NET_UPLOAD_KBPS_MAX ? upload_kbps : 0;
	p->cached_ipv4 = src_ipv4;
	p->cached_port = src_port;
	if (match_route || clear_match_route) {
		const s32 newer = p->match_route_latest_issued_unix_seconds == 0 ||
			route_issued > p->match_route_latest_issued_unix_seconds ||
			(route_issued == p->match_route_latest_issued_unix_seconds &&
				(s32)(route_nonce - p->match_route_latest_nonce) > 0);
		if (newer) {
			p->match_route_latest_issued_unix_seconds = route_issued;
			p->match_route_latest_nonce = route_nonce;
			if (match_route) {
				p->match_route = *match_route;
				p->match_route_received_ms = SDL_GetTicks();
			} else {
				memset(&p->match_route, 0, sizeof(p->match_route));
				p->match_route_received_ms = 0;
			}
		}
	}
	if (status_blurb) {
		strncpy(p->status_blurb, status_blurb, sizeof(p->status_blurb) - 1);
		p->status_blurb[sizeof(p->status_blurb) - 1] = '\0';
	}
	const social_friend_t *f = socialFriendByHandle(handle);
	if (f) {
		if (agent && agent[0]) {
			socialFriendUpdateAgentName(f->connect_code, agent);
			f = socialFriendByHandle(handle);
			if (!f) return;
		}
		socialFriendTouchSeen(f->connect_code);
		/* Refresh the persistent endpoint cache (Section 3 endpoint
		 * resolution flow). TTL is 5 minutes; design decision logged in
		 * connectivity-phase1-decisions.md. */
		socialFriendUpdateEndpoint(handle, src_ipv4, src_port,
		                           PRESENCE_ENDPOINT_TTL_S);

		/* Toast on offline->online transition (SOCIAL category, Q8).
		 * "Offline" here means we either have no prior pong record or
		 * the last record was the explicit OFFLINE state. */
		const presence_state_t cur = (presence_state_t)state;
		const bool was_offline = (prev_pong == 0) ||
		                          (prev_state == PRESENCE_OFFLINE) ||
		                          (prev_state == PRESENCE_APPEAR_OFFLINE);
		const bool now_online = (cur == PRESENCE_ONLINE_IDLE) ||
		                         (cur == PRESENCE_IN_MATCH) ||
		                         (cur == PRESENCE_IN_MISSION) ||
		                         (cur == PRESENCE_SPECTATING);
		if (was_offline && now_online) {
			char title[96];
			char body[160];
			snprintf(title, sizeof(title), "%s came online",
			          f->agent_name[0] ? f->agent_name : "A friend");
			snprintf(body, sizeof(body), "%s",
			          presenceStateName(cur));
			(void)pdguiToastEnqueue(handle, TOAST_CATEGORY_SOCIAL, title, body);
			char syslog[128];
			snprintf(syslog, sizeof(syslog), "%s came online",
			          f->agent_name[0] ? f->agent_name : "friend");
			(void)chatHistoryAppendSystem(handle, syslog);
		}
	}
	groupSessionUpdateKbps(handle, p->upload_kbps);
}

static void enqueueInvite(u32 from_handle, u8 kind, const char *agent)
{
	/* Drop duplicates from the same handle within a short window. */
	for (s32 i = 0; i < s_NumInbox; i++) {
		if (s_Inbox[i].from_handle == from_handle && s_Inbox[i].kind == kind) {
			s_Inbox[i].received_ms = SDL_GetTicks();
			return;
		}
	}

	/* Toast (INVITES category, Q8). Per-friend mute is checked inside
	 * pdguiToastEnqueue. */
	{
		const social_friend_t *f = socialFriendByHandle(from_handle);
		const char *display_agent = (f && f->agent_name[0]) ? f->agent_name :
		                              (agent && *agent ? agent : "Someone");
		const char *kind_text = (kind == PRESENCE_INVITE_KIND_MATCH) ? "to play"
		                       : (kind == PRESENCE_INVITE_KIND_GROUP) ? "to a group"
		                       : "to a listening room";
		char title[96], body[160];
		snprintf(title, sizeof(title), "%s invited you", display_agent);
		snprintf(body, sizeof(body), "Tap the sidebar to Accept or Decline (%s)",
		          kind_text);
		(void)pdguiToastEnqueue(from_handle, TOAST_CATEGORY_INVITES, title, body);
	}

	if (s_NumInbox >= PRESENCE_INVITE_CAP) {
		/* Drop oldest. */
		memmove(&s_Inbox[0], &s_Inbox[1],
		        (size_t)(PRESENCE_INVITE_CAP - 1) * sizeof(presence_invite_t));
		s_NumInbox = PRESENCE_INVITE_CAP - 1;
	}
	presence_invite_t *e = &s_Inbox[s_NumInbox++];
	memset(e, 0, sizeof(*e));
	e->from_handle = from_handle;
	e->kind = kind;
	e->received_ms = SDL_GetTicks();
	if (agent) {
		strncpy(e->from_agent, agent, sizeof(e->from_agent) - 1);
	}
	sysLogPrintf(LOG_NOTE, "PRESENCE: invite from 0x%08x kind=%u",
	             (unsigned)from_handle, (unsigned)kind);
}

static void drainReceive(void)
{
	for (;;) {
		u8 packet[512];
		struct sockaddr_in src;
		socklen_t srclen = sizeof(src);
		int n = recvfrom(s_Sock, (char *)packet, sizeof(packet), 0,
		                 (struct sockaddr *)&src, &srclen);
		if (n <= 0) break;
		if (n != PRESENCE_FRAME_LEN) continue;
		if (memcmp(packet, PRESENCE_MAGIC, PRESENCE_MAGIC_LEN) != 0) continue;

		const u8 *p = packet + PRESENCE_MAGIC_LEN;
		u8 ver   = rU8(&p);
		u8 kind  = rU8(&p);
		u8 state = rU8(&p);
		u32 from_handle = rU32(&p);
		u32 to_handle   = rU32(&p);
		u16 proto       = rU16(&p);
		u16 input_meta  = rU16(&p);
		u8 invite_kind  = (u8)(input_meta & 0xff);
		u8 input_class  = (u8)((input_meta >> 8) & 0xff);
		u32 nonce       = rU32(&p);
		const u8 *upload_r = packet + PRESENCE_UPLOAD_KBPS_OFFSET;
		u32 upload_kbps = rU32(&upload_r);
		if (upload_kbps > NET_UPLOAD_KBPS_MAX) upload_kbps = 0;
		net_match_route_t wire_route;
		memset(&wire_route, 0, sizeof(wire_route));
		const u8 *route_ipv4_r = packet + PRESENCE_MATCH_ROUTE_IPV4_OFFSET;
		const u8 *route_port_r = packet + PRESENCE_MATCH_ROUTE_PORT_OFFSET;
		const u8 *route_issued_r = packet + PRESENCE_MATCH_ROUTE_ISSUED_OFFSET;
		wire_route.ipv4 = rU32(&route_ipv4_r);
		wire_route.port = rU16(&route_port_r);
		wire_route.flags = packet[PRESENCE_MATCH_ROUTE_FLAGS_OFFSET];
		wire_route._pad = packet[PRESENCE_MATCH_ROUTE_FLAGS_OFFSET + 1];
		wire_route.issued_unix_seconds = rU32(&route_issued_r);

		if (ver != PRESENCE_VERSION) continue;
		if (to_handle != 0 && to_handle != socialMyHandle()) continue;
		if (!acceptFromHandle(from_handle)) continue;
		if ((kind == PRESENCE_KIND_PING || kind == PRESENCE_KIND_PONG) &&
			!rateLimitAllow(from_handle)) continue;

		const u8 *sender_pub = packet + PRESENCE_PUBKEY_OFFSET;
		const u8 *sender_sig = packet + PRESENCE_SIG_OFFSET;
		char agent[SOCIAL_AGENTNAME_MAX];
		char status_blurb[PRESENCE_STATUS_LEN];
		copyFixedString(agent, sizeof(agent),
		                packet + PRESENCE_AGENT_OFFSET, PRESENCE_AGENT_LEN);
		copyFixedString(status_blurb, sizeof(status_blurb),
		                packet + PRESENCE_STATUS_OFFSET, PRESENCE_STATUS_LEN);

		/* Step 1: handle / key bind (cheap, drops obvious spoofs first). */
		if (!socialHandleBindsPubkeyForAgent(from_handle, sender_pub, agent)) {
			sysLogPrintf(LOG_WARNING,
			             "PRESENCE: drop -- handle 0x%08x does not bind sender pubkey",
			             (unsigned)from_handle);
			continue;
		}

		/* Step 2: signature verify against sender pubkey. */
		if (!verifyFrame(packet, sender_sig, sender_pub)) {
			sysLogPrintf(LOG_WARNING,
			             "PRESENCE: drop -- bad signature from handle 0x%08x",
			             (unsigned)from_handle);
			continue;
		}

		/* Step 3: TOFU pubkey lock against the friend record. */
		s32 bind = socialFriendBindPubkey(from_handle, sender_pub);
		if (bind < 0) {
			sysLogPrintf(LOG_WARNING,
			             "PRESENCE: drop -- handle 0x%08x pubkey changed from cached "
			             "(possible identity rotation, ignoring until user re-adds)",
			             (unsigned)from_handle);
			continue;
		}

		const u32 src_ipv4 = ntohl(src.sin_addr.s_addr);
		const u16 src_port = ntohs(src.sin_port);
		net_match_route_t normalized_route;
		const net_match_route_t *route_ptr = NULL;
		const s32 route_payload_present = wire_route.ipv4 != 0 ||
			wire_route.port != 0 || wire_route.flags != 0 ||
			wire_route._pad != 0;
		const s32 route_clear = kind == PRESENCE_KIND_MATCH_ROUTE &&
			!route_payload_present && wire_route.issued_unix_seconds != 0;
		if (route_payload_present) {
			const time_t now_time = time(NULL);
			if (now_time > 0 && (u64)now_time <= 0xffffffffu &&
				netMatchRouteNormalize(&wire_route, src_ipv4,
					(u32)now_time, &normalized_route)) {
				route_ptr = &normalized_route;
				sysLogPrintf(LOG_NOTE,
					"PRESENCE: accepted signed match route authority=0x%08x flags=0x%02x port=%u fresh=1",
					(unsigned)from_handle, (unsigned)normalized_route.flags,
					(unsigned)normalized_route.port);
			} else {
				sysLogPrintf(LOG_WARNING,
					"PRESENCE: rejected invalid signed match route from handle 0x%08x",
					(unsigned)from_handle);
				continue;
			}
		} else if (route_clear) {
			const time_t now_time = time(NULL);
			if (now_time <= 0 || (u64)now_time > 0xffffffffu ||
				!netMatchRouteTimestampIsFresh(wire_route.issued_unix_seconds,
					(u32)now_time)) {
				sysLogPrintf(LOG_WARNING,
					"PRESENCE: rejected stale signed match route clear from handle 0x%08x",
					(unsigned)from_handle);
				continue;
			}
		} else if (wire_route.issued_unix_seconds != 0 ||
			kind == PRESENCE_KIND_MATCH_ROUTE) {
			sysLogPrintf(LOG_WARNING,
				"PRESENCE: rejected malformed signed match route update from handle 0x%08x",
				(unsigned)from_handle);
			continue;
		}

		switch (kind) {
			case PRESENCE_KIND_PING: {
				/* Reply pong. */
				recordPong(from_handle, state, proto, input_class, upload_kbps, src_ipv4, src_port,
				           agent, status_blurb, route_ptr, 0,
				           wire_route.issued_unix_seconds, nonce);
				sendFrame(src_ipv4, src_port, PRESENCE_KIND_PONG, from_handle,
				          nonce, 0, s_LocalBlurb);
				break;
			}
			case PRESENCE_KIND_PONG: {
				recordPong(from_handle, state, proto, input_class, upload_kbps, src_ipv4, src_port,
				           agent, status_blurb, route_ptr, 0,
				           wire_route.issued_unix_seconds, nonce);
				break;
			}
			case PRESENCE_KIND_INVITE: {
				/* Cache sender endpoint for the upcoming p2p path. */
				recordPong(from_handle, state, proto, input_class, upload_kbps, src_ipv4, src_port,
				           agent, status_blurb, route_ptr, 0,
				           wire_route.issued_unix_seconds, nonce);
				const social_friend_t *f = socialFriendByHandle(from_handle);
				const char *display_agent = (f && f->agent_name[0]) ? f->agent_name : agent;
				enqueueInvite(from_handle, invite_kind, display_agent);
				break;
			}
			case PRESENCE_KIND_INVITE_RESP: {
				recordPong(from_handle, state, proto, input_class, upload_kbps, src_ipv4, src_port,
				           agent, status_blurb, route_ptr, 0,
				           wire_route.issued_unix_seconds, nonce);
				/* The low byte of input_meta encodes the response: 1 =
				 * accepted, 0 = declined. group_session moves the peer to
				 * RESOLVING (kicks p2p) or FAILED (REJECTED). */
				groupSessionOnInviteResponse(from_handle, invite_kind ? 1 : 0);
				break;
			}
			case PRESENCE_KIND_MATCH_ROUTE: {
				recordPong(from_handle, state, proto, input_class, upload_kbps,
					src_ipv4, src_port, agent, status_blurb, route_ptr,
					route_clear, wire_route.issued_unix_seconds, nonce);
				break;
			}
			case PRESENCE_KIND_BYE: {
				presence_peer_t *peer = findPeer(from_handle);
				if (peer) {
					peer->state = PRESENCE_OFFLINE;
					peer->last_pong_ms = 0;
					memset(&peer->match_route, 0, sizeof(peer->match_route));
					peer->match_route_received_ms = 0;
				}
				groupSessionDropPeer(from_handle);
				break;
			}
			default: break;
		}
	}
}

/* -------------------------------------------------------------------------
 * Outgoing schedule
 * ------------------------------------------------------------------------- */

static void scheduleSync(void)
{
	/* Add any new friends not yet in schedule. */
	const s32 nf = socialFriendCount();
	for (s32 i = 0; i < nf; i++) {
		const social_friend_t *f = socialFriendAt(i);
		if (!f) continue;
		s32 found = 0;
		for (s32 j = 0; j < s_NumScheduled; j++) {
			if (s_Schedule[j].handle == f->handle) { found = 1; break; }
		}
		if (!found && s_NumScheduled < PRESENCE_PEER_CAP) {
			s_Schedule[s_NumScheduled].handle = f->handle;
			s_Schedule[s_NumScheduled].last_ping_ms = 0;
			s_NumScheduled++;
		}
	}
}

/* Resolve the best known endpoint for a friend handle, in this order:
 *   1. Persistent social store cache, if non-zero AND TTL not expired.
 *   2. In-memory presence peer cache (within same session).
 *   3. T0 LAN broadcast cache (same subnet only).
 * Returns 1 + writes ipv4/port if found, 0 otherwise. Implements the
 * endpoint-resolution flow documented in design Section 2.3. */
static s32 resolveEndpoint(u32 handle, u32 *out_ipv4, u16 *out_port)
{
	if (socialFriendGetEndpoint(handle, out_ipv4, out_port) == 1) {
		if (*out_port == 0) *out_port = PRESENCE_PORT;
		return 1;
	}
	const presence_peer_t *p = presencePeerByHandle(handle);
	if (p && p->cached_ipv4 != 0) {
		*out_ipv4 = p->cached_ipv4;
		*out_port = p->cached_port ? p->cached_port : PRESENCE_PORT;
		return 1;
	}
	u32 lan_ipv4 = 0; u16 lan_port = 0;
	if (p2pLanLookup(handle, &lan_ipv4, &lan_port)) {
		*out_ipv4 = lan_ipv4;
		/* P2P.LAN advertises the NAT/probe discovery port. Presence has
		 * its own fixed socket, so use the LAN cache for the peer IP only
		 * until an actual presence frame gives us a source port. */
		*out_port = PRESENCE_PORT;
		(void)lan_port;
		return 1;
	}
	return 0;
}

static void sendPingTo(u32 handle)
{
	const social_friend_t *f = socialFriendByHandle(handle);
	if (!f) return;

	u32 ipv4 = 0; u16 port = 0;
	if (!resolveEndpoint(handle, &ipv4, &port)) return;

	const u32 nonce = (u32)SDL_GetTicks() ^ handle;
	sendFrame(ipv4, port, PRESENCE_KIND_PING, handle, nonce, 0, s_LocalBlurb);
}

void presenceTick(void)
{
	if (!s_SocketReady) return;

	/* Mike directive 2026-05-17: hold all outbound presence activity
	 * until an agent has been loaded so the published connect-code /
	 * handle reflects the active agent identity. We still drain the
	 * inbound socket (peer pings, BYEs) so the receive path doesn't
	 * back up on a long pre-agent boot delay. */
	if (!s_AgentConfirmed) {
		drainReceive();
		return;
	}

	if (socialVisibilityGet() == SOCIAL_VIS_APPEAR_OFFLINE) {
		s_LocalState = PRESENCE_APPEAR_OFFLINE;
	}

	drainReceive();
	scheduleSync();

	const u32 now = SDL_GetTicks();
	for (s32 i = 0; i < s_NumScheduled; i++) {
		ping_schedule_t *e = &s_Schedule[i];
		if (now - e->last_ping_ms < PRESENCE_PING_INTERVAL_MS && e->last_ping_ms != 0) continue;
		e->last_ping_ms = now;

		/* Skip pinging when we are appear-offline (Q7). */
		if (s_LocalState == PRESENCE_APPEAR_OFFLINE) continue;

		sendPingTo(e->handle);
	}

	/* Prune stale invites. */
	s32 w = 0;
	for (s32 r = 0; r < s_NumInbox; r++) {
		if (now - s_Inbox[r].received_ms < PRESENCE_INVITE_TTL_MS) {
			if (w != r) s_Inbox[w] = s_Inbox[r];
			w++;
		}
	}
	s_NumInbox = w;

	/* Prune pending invitees. */
	for (s32 i = 0; i < PRESENCE_PENDING_CAP; i++) {
		if (s_Pending[i].in_use && (now - s_Pending[i].added_ms) > PRESENCE_INVITE_TTL_MS) {
			s_Pending[i].in_use = 0;
		}
	}

	/* Drop offline peers from cache after staleness window. */
	for (s32 i = 0; i < s_NumPeers; i++) {
		presence_peer_t *peer = &s_Peers[i];
		if (peer->last_pong_ms && (now - peer->last_pong_ms) > PRESENCE_PONG_FRESH_MS) {
			peer->state = PRESENCE_OFFLINE;
		}
	}

	(void)s_LocalAgentRecord_ms;
}

/* -------------------------------------------------------------------------
 * Read accessors
 * ------------------------------------------------------------------------- */

const presence_peer_t *presencePeerByHandle(u32 handle)
{
	return findPeer(handle);
}

s32 presencePeerIsOnline(u32 handle)
{
	const presence_peer_t *p = findPeer(handle);
	if (!p || !p->last_pong_ms) return 0;
	const u32 now = SDL_GetTicks();
	if ((now - p->last_pong_ms) > PRESENCE_PONG_FRESH_MS) return 0;
	return p->state != PRESENCE_OFFLINE && p->state != PRESENCE_APPEAR_OFFLINE;
}

s32 presencePeerMatchRoute(u32 handle, net_match_route_t *out_route)
{
	const presence_peer_t *p = findPeer(handle);
	if (!p || !out_route || p->match_route.port == 0 ||
		p->match_route_received_ms == 0) {
		return 0;
	}
	if ((SDL_GetTicks() - p->match_route_received_ms) >
		NET_MATCH_ROUTE_FRESH_MS) {
		return 0;
	}
	const time_t now_time = time(NULL);
	if (now_time <= 0 || (u64)now_time > 0xffffffffu ||
		!netMatchRouteTimestampIsFresh(p->match_route.issued_unix_seconds,
			(u32)now_time)) {
		return 0;
	}
	*out_route = p->match_route;
	return 1;
}

void presencePublishMatchRoute(void)
{
	if (!s_SocketReady) return;
	for (s32 i = 0; i < s_NumPeers; i++) {
		presence_peer_t *p = &s_Peers[i];
		u32 ipv4 = 0;
		u16 port = 0;
		if (p->handle == 0 || !resolveEndpoint(p->handle, &ipv4, &port)) {
			continue;
		}
		sendFrame(ipv4, port, PRESENCE_KIND_MATCH_ROUTE, p->handle,
			(u32)SDL_GetTicks() ^ p->handle, 0, s_LocalBlurb);
	}
}

s32 presenceSetLocalMatchRoute(const net_match_route_t *route)
{
	if (!route) return -1;
	const time_t now_time = time(NULL);
	if (now_time <= 0 || (u64)now_time > 0xffffffffu) return -1;

	net_match_route_t wire = *route;
	wire.issued_unix_seconds = (u32)now_time;
	wire._pad = 0;
	net_match_route_t normalized;
	const u32 verified_source = wire.flags == NET_MATCH_ROUTE_SOURCE_DERIVED
		? 0x7f000001u : 0;
	if (!netMatchRouteNormalize(&wire, verified_source,
			wire.issued_unix_seconds, &normalized)) {
		return -1;
	}
	if (wire.flags == NET_MATCH_ROUTE_SOURCE_DERIVED) {
		normalized.ipv4 = 0;
	}
	s_LocalMatchRoute = normalized;
	s_LocalMatchRoute.issued_unix_seconds = wire.issued_unix_seconds;
	presencePublishMatchRoute();
	return 0;
}

void presenceClearLocalMatchRoute(void)
{
	const s32 had_route = s_LocalMatchRoute.port != 0;
	memset(&s_LocalMatchRoute, 0, sizeof(s_LocalMatchRoute));
	if (had_route) {
		sysLogPrintf(LOG_NOTE, "PRESENCE: local signed match route cleared");
		presencePublishMatchRoute();
	}
}

/* -------------------------------------------------------------------------
 * Pending invite allowlist
 * ------------------------------------------------------------------------- */

s32 presencePendingInviteAdd(u32 handle)
{
	if (handle == 0) return -1;
	for (s32 i = 0; i < PRESENCE_PENDING_CAP; i++) {
		if (s_Pending[i].in_use && s_Pending[i].handle == handle) {
			s_Pending[i].added_ms = SDL_GetTicks();
			return 0;
		}
	}
	for (s32 i = 0; i < PRESENCE_PENDING_CAP; i++) {
		if (!s_Pending[i].in_use) {
			s_Pending[i].in_use = 1;
			s_Pending[i].handle = handle;
			s_Pending[i].added_ms = SDL_GetTicks();
			return 0;
		}
	}
	return -1;
}

void presencePendingInviteRemove(u32 handle)
{
	for (s32 i = 0; i < PRESENCE_PENDING_CAP; i++) {
		if (s_Pending[i].in_use && s_Pending[i].handle == handle) {
			s_Pending[i].in_use = 0;
			return;
		}
	}
}

/* -------------------------------------------------------------------------
 * Outbound invite
 * ------------------------------------------------------------------------- */

s32 presenceSendInvite(u32 friend_handle, u8 kind)
{
	if (friend_handle == 0) return -1;
	const social_friend_t *f = socialFriendByHandle(friend_handle);
	if (!f) return -1;

	u32 ipv4 = 0; u16 port = 0;
	if (!resolveEndpoint(friend_handle, &ipv4, &port)) return -1;

	const u32 nonce = (u32)SDL_GetTicks() ^ friend_handle;
	/* Track the invite in the group session.  The actual p2p pair starts
	 * when the friend's INVITE_RESP arrives -- racing to open the pair
	 * before they accept would burn LAN/STUN attempts on a peer who may
	 * decline.  groupSessionOnInviteResponse owns the pair-open path. */
	if (groupSessionRecordSentInvite(friend_handle) != 0) return -1;
	sendFrame(ipv4, port, PRESENCE_KIND_INVITE, friend_handle, nonce, kind,
	          s_LocalBlurb);
	sysLogPrintf(LOG_NOTE,
	             "PRESENCE: invite sent handle=0x%08x kind=%u via %u.%u.%u.%u:%u",
	             (unsigned)friend_handle, (unsigned)kind,
	             (ipv4 >> 24) & 0xFF, (ipv4 >> 16) & 0xFF,
	             (ipv4 >>  8) & 0xFF, (ipv4 >>  0) & 0xFF,
	             (unsigned)port);
	return 0;
}

/* -------------------------------------------------------------------------
 * Inbox
 * ------------------------------------------------------------------------- */

s32 presenceInviteCount(void) { return s_NumInbox; }

const presence_invite_t *presenceInviteAt(s32 idx)
{
	if (idx < 0 || idx >= s_NumInbox) return NULL;
	return &s_Inbox[idx];
}

s32 presenceInviteAccept(s32 idx)
{
	if (idx < 0 || idx >= s_NumInbox) return -1;
	const presence_invite_t e = s_Inbox[idx];

	/* Establish the shared initiator and perform pre-connect authority
	 * election before sending the acceptance. If this client is elected, the
	 * response itself can carry its freshly started signed ENet route. */
	if (groupSessionAcceptInvite(e.from_handle) != 0) {
		return -1;
	}

	u32 ipv4 = 0; u16 port = 0;
	(void)resolveEndpoint(e.from_handle, &ipv4, &port);

	const u32 nonce = (u32)SDL_GetTicks() ^ e.from_handle;
	if (ipv4 != 0) {
		sendFrame(ipv4, port, PRESENCE_KIND_INVITE_RESP, e.from_handle, nonce, 1,
		          s_LocalBlurb);
	}

	/* Remove from inbox. */
	memmove(&s_Inbox[idx], &s_Inbox[idx + 1],
	        (size_t)(s_NumInbox - idx - 1) * sizeof(presence_invite_t));
	s_NumInbox--;
	memset(&s_Inbox[s_NumInbox], 0, sizeof(presence_invite_t));
	return 0;
}

s32 presenceInviteDecline(s32 idx)
{
	if (idx < 0 || idx >= s_NumInbox) return -1;
	const presence_invite_t *e = &s_Inbox[idx];

	u32 ipv4 = 0; u16 port = 0;
	if (resolveEndpoint(e->from_handle, &ipv4, &port)) {
		const u32 nonce = (u32)SDL_GetTicks() ^ e->from_handle;
		sendFrame(ipv4, port, PRESENCE_KIND_INVITE_RESP, e->from_handle, nonce, 0,
		          s_LocalBlurb);
	}

	memmove(&s_Inbox[idx], &s_Inbox[idx + 1],
	        (size_t)(s_NumInbox - idx - 1) * sizeof(presence_invite_t));
	s_NumInbox--;
	memset(&s_Inbox[s_NumInbox], 0, sizeof(presence_invite_t));
	return 0;
}
