/**
 * p2p_ice.c -- Tier 4: bounded signed-candidate pair testing.
 *
 * Candidate publication and authentication live in presence.c. This file
 * consumes only normalized signed sets and performs bounded UDP checks. The
 * probe/ack is a MAC-authenticated transport proof; it cannot create or
 * replace a D-003 match route.
 */

#include "net/p2p.h"
#include "net/net.h"
#include "net/netstun.h"
#include "social.h"
#include "system.h"

#include <SDL.h>
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
#include <errno.h>
#define closesocket close
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#endif

#define ICE_MAX_PAIRS 32
#define ICE_MAX_CANDS NET_CANDIDATE_MAX
#define ICE_MAX_LOCAL_CANDIDATE_TARGETS 64
#define ICE_STUN_SENDS_PER_SERVER 3u
#define ICE_STUN_RETRY_MS 250u
#define ICE_STUN_SERVER_TIMEOUT_MS 800u
#define ICE_STUN_OVERALL_TIMEOUT_MS 2400u

typedef struct {
	u32 ipv4;
	u16 port;
	u8 tried;
	u8 source;
} ice_cand_t;

typedef struct {
	u32 pair_id;
	u32 peer_handle;
	u32 nonce_base;
	u32 deadline_ms;
	u32 last_ack_nonce;
	net_candidate_probe_retry_t retry;
	u8 in_use;
	u8 started;
	u8 ncands;
	u8 _pad;
	net_candidate_peer_state_t remote;
	ice_cand_t cands[ICE_MAX_CANDS];
} ice_pair_t;

typedef struct {
	u32 target_handle;
	u8 in_use;
	u8 _pad[3];
	net_candidate_peer_state_t state;
	net_candidate_probe_replay_t replay[NET_CANDIDATE_PROBE_REPLAY_SLOTS];
} ice_local_candidate_target_t;

typedef struct {
	u32 server_index;
	u32 server_ipv4;
	u32 first_ipv4;
	u32 result_ipv4;
	u32 next_send_ms;
	u32 server_deadline_ms;
	u32 overall_deadline_ms;
	u16 server_port;
	u16 first_port;
	u16 result_port;
	u16 local_port;
	u8 transaction_id[STUN_TRANSACTION_ID_LEN];
	u8 request[STUN_BINDING_REQUEST_LEN];
	u8 send_count;
	u8 success_count;
	u8 active;
	u8 status;
	s32 nat_type;
} ice_stun_transaction_t;

static ice_pair_t s_Pairs[ICE_MAX_PAIRS];
static SOCKET s_Sock = INVALID_SOCKET;
static s32 s_SocketReady;
static ice_local_candidate_target_t s_LocalCandidateTargets[
	ICE_MAX_LOCAL_CANDIDATE_TARGETS];
static ice_stun_transaction_t s_Stun;

/* -------------------------------------------------------------------------
 * Socket helpers
 * ------------------------------------------------------------------------- */

static s32 socketSetNonblock(SOCKET s)
{
#ifdef _WIN32
	u_long mode = 1;
	return ioctlsocket(s, FIONBIO, &mode) == 0 ? 0 : -1;
#else
	int flags = fcntl(s, F_GETFL, 0);
	if (flags < 0) return -1;
	return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0 ? 0 : -1;
#endif
}

static s32 socketSetExclusive(SOCKET s)
{
#ifdef _WIN32
	int exclusive = 1;
	return setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
		(const char *)&exclusive, sizeof(exclusive)) == 0 ? 0 : -1;
#else
	(void)s;
	/* POSIX UDP binds are exclusive by default without address sharing. */
	return 0;
#endif
}

static s32 socketBindConflict(void)
{
#ifdef _WIN32
	const s32 error = WSAGetLastError();
	/* Windows reports an exclusive-owner collision as either of these. */
	return error == WSAEADDRINUSE || error == WSAEACCES;
#else
	return errno == EADDRINUSE;
#endif
}

static SOCKET ensureSocket(void)
{
	if (s_SocketReady) return s_Sock;
	s_Sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (s_Sock == INVALID_SOCKET) return INVALID_SOCKET;
	if (socketSetExclusive(s_Sock) != 0 || socketSetNonblock(s_Sock) != 0) {
		closesocket(s_Sock);
		s_Sock = INVALID_SOCKET;
		return INVALID_SOCKET;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(P2P_ICE_PORT);
	if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		if (!socketBindConflict()) {
			closesocket(s_Sock);
			s_Sock = INVALID_SOCKET;
			return INVALID_SOCKET;
		}
		/* A same-host client already owns the fixed endpoint. Keep exclusive
		 * ownership and bind a distinct ephemeral host candidate instead. */
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

static u32 getLocalNicIpv4(void)
{
	SOCKET socket_handle = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_handle == INVALID_SOCKET) return 0;
	struct sockaddr_in dst;
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = htonl(0x08080808u);
	dst.sin_port = htons(53);
	if (connect(socket_handle, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
		closesocket(socket_handle);
		return 0;
	}
	struct sockaddr_in local;
	socklen_t length = sizeof(local);
	if (getsockname(socket_handle, (struct sockaddr *)&local, &length) != 0) {
		closesocket(socket_handle);
		return 0;
	}
	u32 ipv4 = ntohl(local.sin_addr.s_addr);
	closesocket(socket_handle);
	return netCandidateIpv4IsUnicast(ipv4) ? ipv4 : 0;
}

static s32 getSocketEndpoint(u32 *out_ipv4, u16 *out_port)
{
	if (ensureSocket() == INVALID_SOCKET || !out_ipv4 || !out_port) return 0;
	struct sockaddr_in local;
	socklen_t length = sizeof(local);
	memset(&local, 0, sizeof(local));
	if (getsockname(s_Sock, (struct sockaddr *)&local, &length) != 0) return 0;
	*out_port = ntohs(local.sin_port);
	*out_ipv4 = ntohl(local.sin_addr.s_addr);
	if (*out_ipv4 == 0) *out_ipv4 = getLocalNicIpv4();
	return netCandidateIpv4IsUnicast(*out_ipv4) && *out_port != 0;
}

static s32 iceStunTimeReached(u32 now_ms, u32 target_ms)
{
	return (s32)(now_ms - target_ms) >= 0;
}

static s32 iceStunBeginServer(u32 now_ms)
{
	while (s_Stun.server_index < stunServerCount()) {
		if (stunResolveServerAt(s_Stun.server_index,
			&s_Stun.server_ipv4, &s_Stun.server_port) &&
			netCandidateRandomBytes(s_Stun.transaction_id,
				sizeof(s_Stun.transaction_id))) {
			stunBindingRequestBuild(s_Stun.transaction_id, s_Stun.request);
			s_Stun.send_count = 0;
			s_Stun.next_send_ms = now_ms;
			s_Stun.server_deadline_ms =
				now_ms + ICE_STUN_SERVER_TIMEOUT_MS;
			return 1;
		}
		s_Stun.server_index++;
	}
	s_Stun.active = 0;
	s_Stun.status = STUN_STATUS_FAILED;
	return 0;
}

static void iceStunSend(void)
{
	if (!s_Stun.active || s_Sock == INVALID_SOCKET ||
		s_Stun.server_ipv4 == 0 || s_Stun.server_port == 0) return;
	struct sockaddr_in destination;
	memset(&destination, 0, sizeof(destination));
	destination.sin_family = AF_INET;
	destination.sin_addr.s_addr = htonl(s_Stun.server_ipv4);
	destination.sin_port = htons(s_Stun.server_port);
	(void)sendto(s_Sock, (const char *)s_Stun.request,
		STUN_BINDING_REQUEST_LEN, 0,
		(struct sockaddr *)&destination, sizeof(destination));
	s_Stun.send_count++;
}

static s32 iceStunHandlePacket(const u8 *packet, s32 packet_len,
	const struct sockaddr_in *source)
{
	if (!s_Stun.active || !packet || !source ||
		ntohl(source->sin_addr.s_addr) != s_Stun.server_ipv4 ||
		ntohs(source->sin_port) != s_Stun.server_port) return 0;
	u32 ipv4 = 0;
	u16 port = 0;
	if (!stunBindingResponseParse(packet, packet_len,
		s_Stun.transaction_id, &ipv4, &port)) return 0;
	if (!netCandidateIpv4IsPublicRoute(ipv4) || port == 0) {
		s_Stun.active = 0;
		s_Stun.status = STUN_STATUS_FAILED;
		return 1;
	}

	if (s_Stun.success_count == 0) {
		s_Stun.first_ipv4 = ipv4;
		s_Stun.first_port = port;
		s_Stun.result_ipv4 = ipv4;
		s_Stun.result_port = port;
		s_Stun.success_count = 1;
		s_Stun.server_index++;
		(void)iceStunBeginServer(SDL_GetTicks());
		return 1;
	}

	s_Stun.success_count++;
	s_Stun.nat_type = ipv4 == s_Stun.first_ipv4 &&
		port == s_Stun.first_port ? STUN_NAT_CONE : STUN_NAT_SYMMETRIC;
	s_Stun.active = 0;
	s_Stun.status = STUN_STATUS_SUCCESS;
	return 1;
}

static void iceStunPollTransaction(u32 now_ms)
{
	if (!s_Stun.active) return;
	if (iceStunTimeReached(now_ms, s_Stun.overall_deadline_ms)) {
		s_Stun.active = 0;
		s_Stun.status = STUN_STATUS_FAILED;
		return;
	}
	if (iceStunTimeReached(now_ms, s_Stun.server_deadline_ms)) {
		s_Stun.server_index++;
		(void)iceStunBeginServer(now_ms);
		return;
	}
	if (s_Stun.send_count < ICE_STUN_SENDS_PER_SERVER &&
		iceStunTimeReached(now_ms, s_Stun.next_send_ms)) {
		iceStunSend();
		s_Stun.next_send_ms = now_ms + ICE_STUN_RETRY_MS;
	}
}

s32 p2pIceStunStart(void)
{
	u32 local_ipv4 = 0;
	u16 local_port = 0;
	if (!getSocketEndpoint(&local_ipv4, &local_port)) return -1;
	if (s_Stun.active) return 0;
	memset(&s_Stun, 0, sizeof(s_Stun));
	s_Stun.active = 1;
	s_Stun.status = STUN_STATUS_WORKING;
	s_Stun.local_port = local_port;
	s_Stun.overall_deadline_ms = SDL_GetTicks() +
		ICE_STUN_OVERALL_TIMEOUT_MS;
	if (!iceStunBeginServer(SDL_GetTicks())) return -1;
	iceStunPollTransaction(SDL_GetTicks());
	return 0;
}

s32 p2pIceStunGetStatus(u32 *out_ipv4, u16 *out_port,
	s32 *out_nat_type, u16 *out_local_port)
{
	if (out_ipv4) *out_ipv4 = s_Stun.result_ipv4;
	if (out_port) *out_port = s_Stun.result_port;
	if (out_nat_type) *out_nat_type = s_Stun.nat_type;
	if (out_local_port) *out_local_port = s_Stun.local_port;
	return s_Stun.status;
}

void p2pIceStunCancel(void)
{
	memset(&s_Stun, 0, sizeof(s_Stun));
}

/* -------------------------------------------------------------------------
 * Pair and proof helpers
 * ------------------------------------------------------------------------- */

static ice_pair_t *findPair(u32 pair_id)
{
	for (s32 i = 0; i < ICE_MAX_PAIRS; i++) {
		if (s_Pairs[i].in_use && s_Pairs[i].pair_id == pair_id) {
			return &s_Pairs[i];
		}
	}
	return NULL;
}

static ice_pair_t *findPairByPeer(u32 peer_handle)
{
	for (s32 i = 0; i < ICE_MAX_PAIRS; i++) {
		if (s_Pairs[i].in_use && s_Pairs[i].peer_handle == peer_handle) {
			return &s_Pairs[i];
		}
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

static u32 newNonceBase(u32 pair_id, u32 peer_handle)
{
	u8 random_bytes[4];
	u32 nonce = 0;
	if (netCandidateRandomBytes(random_bytes, sizeof(random_bytes))) {
		nonce = (u32)random_bytes[0] | ((u32)random_bytes[1] << 8) |
			((u32)random_bytes[2] << 16) | ((u32)random_bytes[3] << 24);
	}
	/* Leave room for the bounded candidate index without unsigned wrap. */
	nonce &= 0x7ffffff7u;
	if (nonce == 0) {
		nonce = ((u32)SDL_GetTicks() ^ (pair_id << 4) ^ peer_handle) &
			0x7ffffff7u;
	}
	return nonce ? nonce : 1;
}

static void addCand(ice_pair_t *pair, const net_candidate_t *candidate)
{
	if (!pair || !candidate || !netCandidateIpv4IsHostRoute(candidate->ipv4) ||
		candidate->port == 0 || pair->ncands >= ICE_MAX_CANDS) return;
	for (u8 i = 0; i < pair->ncands; i++) {
		/* Normalization rejects this before it reaches ICE. Keep the local
		 * guard endpoint-only so a malformed caller cannot make ICE silently
		 * test the first of two differently labelled endpoints. */
		if (pair->cands[i].ipv4 == candidate->ipv4 &&
			pair->cands[i].port == candidate->port) return;
	}
	pair->cands[pair->ncands].ipv4 = candidate->ipv4;
	pair->cands[pair->ncands].port = candidate->port;
	pair->cands[pair->ncands].tried = 0;
	pair->cands[pair->ncands].source = candidate->type;
	pair->ncands++;
}

static void rebuildRemoteCandidates(ice_pair_t *pair)
{
	pair->ncands = 0;
	if (!pair->remote.active || pair->remote.expires_unix_seconds == 0 ||
		(u32)time(NULL) > pair->remote.expires_unix_seconds) return;
	for (u8 i = 0; i < pair->remote.candidate_count; i++) {
		addCand(pair, &pair->remote.candidates[i]);
	}
}

static s32 probeSenderAllowed(u32 from_handle)
{
	return from_handle != 0 && from_handle != socialMyHandle() &&
		socialMyHandle() != 0 && !socialBlockIsHandle(from_handle) &&
		socialFriendByHandle(from_handle) != NULL;
}

static ice_local_candidate_target_t *findLocalCandidateTarget(
	u32 target_handle)
{
	if (target_handle == 0) return NULL;
	for (s32 i = 0; i < ICE_MAX_LOCAL_CANDIDATE_TARGETS; i++) {
		if (s_LocalCandidateTargets[i].in_use &&
			s_LocalCandidateTargets[i].target_handle == target_handle) {
			return &s_LocalCandidateTargets[i];
		}
	}
	return NULL;
}

static ice_local_candidate_target_t *allocLocalCandidateTarget(void)
{
	for (s32 i = 0; i < ICE_MAX_LOCAL_CANDIDATE_TARGETS; i++) {
		if (!s_LocalCandidateTargets[i].in_use) {
			return &s_LocalCandidateTargets[i];
		}
	}
	return NULL;
}

static s32 localCandidateMatches(const net_candidate_peer_state_t *state,
	u8 index, u32 ipv4, u16 port)
{
	return state && state->active && index < state->candidate_count &&
		state->candidates[index].ipv4 == ipv4 &&
		state->candidates[index].port == port;
}

static void sendPacketTo(const net_candidate_probe_t *probe,
	const u8 key[NET_CANDIDATE_CREDENTIAL_LEN], u32 ipv4, u16 port)
{
	if (!probe || !key || ensureSocket() == INVALID_SOCKET || port == 0) return;
	u8 packet[NET_CANDIDATE_PROBE_FRAME_LEN];
	if (!netCandidateProbeEncode(probe, key, packet)) return;
	struct sockaddr_in dst;
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = htonl(ipv4);
	dst.sin_port = htons(port);
	(void)sendto(s_Sock, (const char *)packet, sizeof(packet), 0,
		(struct sockaddr *)&dst, sizeof(dst));
}

static void sendProbe(ice_pair_t *pair, u8 index, u32 nonce)
{
	if (!pair || index >= pair->ncands || !pair->remote.active ||
		pair->remote.expires_unix_seconds == 0 ||
		(u32)time(NULL) > pair->remote.expires_unix_seconds) return;
	net_candidate_probe_t probe;
	memset(&probe, 0, sizeof(probe));
	probe.kind = NET_CANDIDATE_PROBE_KIND_PROBE;
	probe.sender_handle = socialMyHandle();
	probe.target_handle = pair->peer_handle;
	probe.candidate_owner_handle = pair->peer_handle;
	probe.generation = pair->remote.generation;
	probe.nonce = nonce;
	memcpy(probe.credential, pair->remote.credential,
		sizeof(probe.credential));
	probe.candidate_ipv4 = pair->cands[index].ipv4;
	probe.candidate_port = pair->cands[index].port;
	probe.candidate_index = index;
	sendPacketTo(&probe, pair->remote.credential,
		pair->cands[index].ipv4, pair->cands[index].port);
}

static void probeAll(ice_pair_t *pair)
{
	if (!pair) return;
	for (u8 i = 0; i < pair->ncands; i++) {
		pair->cands[i].tried = 1;
		sendProbe(pair, i, pair->nonce_base + i);
	}
}

static void startProbeSchedule(ice_pair_t *pair, u32 now_ms)
{
	if (!pair) return;
	netCandidateProbeRetryInit(&pair->retry, now_ms);
	if (pair->ncands > 0 &&
		netCandidateProbeRetryPoll(&pair->retry, now_ms,
			pair->deadline_ms) == NET_CANDIDATE_PROBE_RETRY_SEND) {
		probeAll(pair);
	}
}

static void sendAck(const net_candidate_probe_t *request,
	const struct sockaddr_in *source,
	const net_candidate_peer_state_t *local_state)
{
	if (!request || !source || !local_state || !local_state->active ||
		local_state->expires_unix_seconds == 0 ||
		(u32)time(NULL) > local_state->expires_unix_seconds) return;
	net_candidate_probe_t ack = *request;
	ack.kind = NET_CANDIDATE_PROBE_KIND_ACK;
	ack.sender_handle = socialMyHandle();
	ack.target_handle = request->sender_handle;
	ack.candidate_owner_handle = socialMyHandle();
	/* Keep the target's signed generation/credential in the ACK. The
	 * responder validates it against its active local set before replying. */
	sendPacketTo(&ack, request->credential,
		ntohl(source->sin_addr.s_addr), ntohs(source->sin_port));
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

s32 p2pIceGetLocalHostCandidate(u32 *out_ipv4, u16 *out_port)
{
	return getSocketEndpoint(out_ipv4, out_port);
}

void p2pIceSetLocalCandidateSet(const net_candidate_set_t *set)
{
	if (!set) {
		memset(s_LocalCandidateTargets, 0, sizeof(s_LocalCandidateTargets));
		return;
	}
	net_candidate_set_t normalized;
	const time_t now_time = time(NULL);
	if (now_time <= 0 || (u64)now_time > 0xffffffffu ||
		socialMyHandle() == 0 || set->sender_handle != socialMyHandle() ||
		!netCandidateSetNormalize(set, set->sender_handle,
		set->target_handle, (u32)now_time, &normalized)) {
		return;
	}
	ice_local_candidate_target_t *target =
		findLocalCandidateTarget(normalized.target_handle);
	if (target && target->state.generation != 0 &&
		target->state.expires_unix_seconds < (u32)now_time) {
		memset(target, 0, sizeof(*target));
	}
	if (!target) {
		target = allocLocalCandidateTarget();
		if (target) memset(target, 0, sizeof(*target));
	}
	if (!target) return;
	const u32 prior_generation = target->state.generation;
	const net_candidate_update_t update = netCandidatePlanUpdate(
		prior_generation != 0 ? &target->state : NULL, &normalized);
	if (update == NET_CANDIDATE_UPDATE_REJECT ||
		update == NET_CANDIDATE_UPDATE_DUPLICATE) return;
	if (prior_generation == 0 || prior_generation != normalized.generation) {
		/* A credential/generation epoch owns its replay cache. Same-generation
		 * issued-time refreshes must retain every accepted nonce. */
		memset(target->replay, 0, sizeof(target->replay));
	}
	target->in_use = 1;
	target->target_handle = normalized.target_handle;
	netCandidatePeerStateCommit(&target->state, &normalized);
}

s32 p2pIceStart(u32 pair_id, u32 peer_handle)
{
	if (peer_handle == 0 || socialMyHandle() == 0 ||
		ensureSocket() == INVALID_SOCKET) {
		p2pInternalReportFailure(pair_id, P2P_TIER_ICE, "no socket or peer");
		return -1;
	}
	ice_pair_t *pair = findPair(pair_id);
	if (!pair) {
		pair = allocPair();
		if (!pair) {
			p2pInternalReportFailure(pair_id, P2P_TIER_ICE, "table full");
			return -1;
		}
		memset(pair, 0, sizeof(*pair));
	}
	net_candidate_peer_state_t remote = pair->remote;
	memset(pair, 0, sizeof(*pair));
	pair->in_use = 1;
	pair->started = 1;
	pair->pair_id = pair_id;
	pair->peer_handle = peer_handle;
	pair->remote = remote;
	pair->nonce_base = newNonceBase(pair_id, peer_handle);
	pair->deadline_ms = SDL_GetTicks() + P2P_TIER_TIMEOUT_MS;
	rebuildRemoteCandidates(pair);
	if (pair->ncands == 0) {
		sysLogPrintf(LOG_NOTE,
			"P2P.ICE: pair=%u waiting for a signed candidate set",
			(unsigned)pair_id);
		return 0;
	}
	startProbeSchedule(pair, SDL_GetTicks());
	sysLogPrintf(LOG_NOTE, "P2P.ICE: pair=%u probing %u signed candidates",
		(unsigned)pair_id, (unsigned)pair->ncands);
	return 0;
}

s32 p2pIceApplyPeerCandidateSet(u32 pair_id,
	const net_candidate_set_t *set)
{
	ice_pair_t *pair = findPair(pair_id);
	if (!set || set->sender_handle == 0 || socialMyHandle() == 0 ||
		set->target_handle != socialMyHandle()) return 0;
	net_candidate_set_t normalized;
	const time_t now_time = time(NULL);
	if (now_time <= 0 || (u64)now_time > 0xffffffffu ||
		!netCandidateSetNormalize(set, set->sender_handle, socialMyHandle(),
		(u32)now_time, &normalized)) return 0;
	if (!pair) {
		pair = allocPair();
		if (!pair) return 0;
		memset(pair, 0, sizeof(*pair));
		pair->in_use = 1;
		pair->pair_id = pair_id;
		pair->peer_handle = normalized.sender_handle;
	}
	if (normalized.sender_handle != pair->peer_handle) return 0;
	const u32 now = (u32)now_time;
	if (pair->remote.generation != 0 &&
		pair->remote.expires_unix_seconds < now) {
		/* Expired state cannot block a safe restart epoch. */
		memset(&pair->remote, 0, sizeof(pair->remote));
	}
	net_candidate_update_t update = netCandidatePlanUpdate(
		pair->remote.generation != 0 ? &pair->remote : NULL, &normalized);
	if (update == NET_CANDIDATE_UPDATE_REJECT) return 0;
	if (update == NET_CANDIDATE_UPDATE_DUPLICATE) return 2;
	const s32 same_generation = pair->remote.generation != 0 &&
		pair->remote.generation == normalized.generation;
	netCandidatePeerStateCommit(&pair->remote, &normalized);
	if (!pair->started) return update == NET_CANDIDATE_UPDATE_RETIRE ? 3 : 1;
	if (same_generation) {
		/* A later issued time retransmits identical signed content. Preserve the
		 * current challenge, ACK replay state, deadline, and probe budget. */
		return 1;
	}
	pair->nonce_base = newNonceBase(pair->pair_id, pair->peer_handle);
	pair->last_ack_nonce = 0;
	rebuildRemoteCandidates(pair);
	if (pair->ncands == 0) {
		p2pInternalReportFailure(pair->pair_id, P2P_TIER_ICE,
			"peer candidate set retired or expired");
		pair->in_use = 0;
		return 3;
	}
	startProbeSchedule(pair, SDL_GetTicks());
	return update == NET_CANDIDATE_UPDATE_RETIRE ? 3 : 1;
}

void p2pIcePoll(void)
{
	if (!s_SocketReady) return;
	for (;;) {
		u8 packet[512];
		struct sockaddr_in source;
		socklen_t source_length = sizeof(source);
		int received = recvfrom(s_Sock, (char *)packet, sizeof(packet), 0,
			(struct sockaddr *)&source, &source_length);
		if (received <= 0) break;
		if (iceStunHandlePacket(packet, received, &source)) continue;
		if (received != (int)NET_CANDIDATE_PROBE_FRAME_LEN) continue;
		net_candidate_probe_t probe;
		if (!netCandidateProbeDecode(packet, (size_t)received, &probe) ||
			!probeSenderAllowed(probe.sender_handle)) continue;
		if (probe.kind == NET_CANDIDATE_PROBE_KIND_PROBE) {
			ice_local_candidate_target_t *local_target =
				findLocalCandidateTarget(probe.sender_handle);
			const net_candidate_peer_state_t *local_state =
				local_target ? &local_target->state : NULL;
			if (probe.target_handle != socialMyHandle() ||
				probe.candidate_owner_handle != socialMyHandle() ||
				!local_state || !local_state->active ||
				!netCandidateProbeValidate(&probe, packet, (size_t)received,
					probe.credential, probe.sender_handle, socialMyHandle(),
					socialMyHandle(), local_state->generation,
					local_state->credential,
					local_state->expires_unix_seconds,
					(u32)time(NULL), 0) ||
				!localCandidateMatches(local_state, probe.candidate_index,
					probe.candidate_ipv4, probe.candidate_port)) continue;
			const net_candidate_probe_replay_result_t replay =
				netCandidateProbeReplayRecord(local_target->replay,
					NET_CANDIDATE_PROBE_REPLAY_SLOTS, probe.nonce,
					ntohl(source.sin_addr.s_addr), ntohs(source.sin_port));
			if (replay == NET_CANDIDATE_PROBE_REPLAY_REJECT) continue;
			/* Duplicate from the same UDP source means the prior ACK was lost.
			 * Re-ACK idempotently; a moved nonce is rejected by replay state. */
			sendAck(&probe, &source, local_state);
			continue;
		}
		ice_pair_t *peer_pair = findPairByPeer(probe.sender_handle);
		if (!peer_pair || !peer_pair->started) continue;
		if (probe.kind != NET_CANDIDATE_PROBE_KIND_ACK ||
			!peer_pair->remote.active ||
			peer_pair->remote.expires_unix_seconds == 0 ||
			(u32)time(NULL) > peer_pair->remote.expires_unix_seconds ||
			probe.target_handle != socialMyHandle() ||
			probe.candidate_owner_handle != peer_pair->peer_handle ||
			!netCandidateProbeValidate(&probe, packet, (size_t)received,
				peer_pair->remote.credential, peer_pair->peer_handle,
				socialMyHandle(), peer_pair->peer_handle,
				peer_pair->remote.generation, peer_pair->remote.credential,
				peer_pair->remote.expires_unix_seconds, (u32)time(NULL),
				peer_pair->last_ack_nonce)) continue;
		if (probe.nonce < peer_pair->nonce_base) continue;
		u32 index = probe.nonce - peer_pair->nonce_base;
		if (index >= peer_pair->ncands ||
			!peer_pair->cands[index].tried || probe.candidate_index != index ||
			peer_pair->last_ack_nonce == probe.nonce ||
			!netCandidateProbeSourceMatches(&probe,
				ntohl(source.sin_addr.s_addr), ntohs(source.sin_port)) ||
			probe.candidate_ipv4 != peer_pair->cands[index].ipv4 ||
			probe.candidate_port != peer_pair->cands[index].port) continue;

		peer_pair->last_ack_nonce = probe.nonce;
		p2p_endpoint_t endpoint;
		memset(&endpoint, 0, sizeof(endpoint));
		/* Source and advertised candidate were proven equal above. */
		endpoint.ipv4 = ntohl(source.sin_addr.s_addr);
		endpoint.port = ntohs(source.sin_port);
		endpoint.flags = P2P_EP_ICE_PAIR;
		p2pInternalReportSuccess(peer_pair->pair_id, P2P_TIER_ICE,
			&endpoint);
		peer_pair->in_use = 0;
	}

	const u32 now_ms = SDL_GetTicks();
	iceStunPollTransaction(now_ms);
	for (s32 i = 0; i < ICE_MAX_PAIRS; i++) {
		ice_pair_t *pair = &s_Pairs[i];
		if (!pair->in_use || !pair->started) continue;
		if ((s32)(now_ms - pair->deadline_ms) >= 0) {
			p2pInternalReportFailure(pair->pair_id, P2P_TIER_ICE,
				"ice timeout");
			pair->in_use = 0;
			continue;
		}
		if (pair->ncands > 0 &&
			netCandidateProbeRetryPoll(&pair->retry, now_ms,
				pair->deadline_ms) == NET_CANDIDATE_PROBE_RETRY_SEND) {
			probeAll(pair);
		}
	}
}

void p2pIceCancel(u32 pair_id, u32 peer_handle)
{
	for (s32 i = 0; i < ICE_MAX_PAIRS; i++) {
		if (s_Pairs[i].pair_id == pair_id) {
			memset(&s_Pairs[i], 0, sizeof(s_Pairs[i]));
		}
	}
	ice_local_candidate_target_t *target =
		findLocalCandidateTarget(peer_handle);
	if (target) memset(target, 0, sizeof(*target));
}

void p2pIceShutdown(void)
{
	p2pIceStunCancel();
	memset(s_Pairs, 0, sizeof(s_Pairs));
	memset(s_LocalCandidateTargets, 0, sizeof(s_LocalCandidateTargets));
	if (s_Sock != INVALID_SOCKET) closesocket(s_Sock);
	s_Sock = INVALID_SOCKET;
	s_SocketReady = 0;
}
