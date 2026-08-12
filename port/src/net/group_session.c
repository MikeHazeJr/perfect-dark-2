/**
 * group_session.c -- mesh + authority election + invite-to-match handoff.
 *
 * Per Mike's debugging principles applied to this writer:
 *
 *  - Single writer per peer state field (this file only). Presence
 *    invite hooks call public API; the orchestrator's p2p layer
 *    informs us via state polls in groupSessionTick. No silent
 *    cross-module mutation.
 *  - Up + down stack logging: every transition tags the source
 *    (`GROUP.SESSION:`) so a follow-up "why is this peer stuck?" trace
 *    can grep the cause without re-deriving it.
 */

#include "net/group_session.h"
#include "net/net_bandwidth.h"
#include "net/p2p.h"
#include "net/net.h"
#include "net/netholepunch.h"
#include "net/netstun.h"
#include "net/netupnp.h"
#include "social.h"
#include "presence.h"
#include "system.h"
#include "smoke_harness.h"
#include "updateversion.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static group_session_t s_Session;
static s32             s_Initialised;
static net_match_route_t s_PublishedRoute;

typedef enum debug_friend_play_role_e {
	DEBUG_FRIEND_PLAY_NONE = 0,
	DEBUG_FRIEND_PLAY_INITIATOR = 1,
	DEBUG_FRIEND_PLAY_INVITEE = 2,
} debug_friend_play_role_t;

static debug_friend_play_role_t s_DebugFriendPlayRole;
static u32 s_DebugFriendPlayPeer;
static u32 s_DebugFriendPlayUploadKbps;
static u32 s_DebugFriendPlayReadyMs;
static u8 s_DebugFriendPlayUploadSet;
static u8 s_DebugFriendPlayAutoInvite;
static u8 s_DebugFriendPlayAutoAccept;
static u8 s_DebugFriendPlayActionDone;
static u8 s_DebugFriendPlayFailHost;
static u8 s_DebugFriendPlayFailClient;

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static group_peer_t *findPeer(u32 handle)
{
	if (!s_Initialised) return NULL;
	for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; i++) {
		if (s_Session.peers[i].handle == handle && handle != 0) {
			return &s_Session.peers[i];
		}
	}
	return NULL;
}

static u32 sampleLocalElectionKbps(void)
{
	return s_DebugFriendPlayUploadSet ? s_DebugFriendPlayUploadKbps :
		netUploadKbpsEstimate();
}

static void debugFriendPlayInit(void)
{
	s_DebugFriendPlayRole = DEBUG_FRIEND_PLAY_NONE;
	s_DebugFriendPlayPeer = 0;
	s_DebugFriendPlayUploadKbps = 0;
	s_DebugFriendPlayUploadSet = 0;
	s_DebugFriendPlayAutoInvite = 0;
	s_DebugFriendPlayAutoAccept = 0;
	s_DebugFriendPlayActionDone = 0;
	s_DebugFriendPlayFailHost = 0;
	s_DebugFriendPlayFailClient = 0;
	s_DebugFriendPlayReadyMs = SDL_GetTicks();

	const char *role = sysArgGetString("--debug-friend-play-role");
	if (!role || !role[0]) return;
	if (!smokeHarnessIsActive()) {
		sysLogPrintf(LOG_WARNING,
			"FRIENDPLAY: debug controls ignored outside smoke harness");
		return;
	}
	if (!strcmp(role, "initiator")) {
		s_DebugFriendPlayRole = DEBUG_FRIEND_PLAY_INITIATOR;
	} else if (!strcmp(role, "invitee")) {
		s_DebugFriendPlayRole = DEBUG_FRIEND_PLAY_INVITEE;
	} else {
		sysLogPrintf(LOG_WARNING,
			"FRIENDPLAY: invalid role '%s'; controls disabled", role);
		return;
	}

	const social_friend_t *peer = socialFriendAt(0);
	if (!peer || peer->handle == 0) {
		sysLogPrintf(LOG_ERROR,
			"FRIENDPLAY: role=%s has no fixture peer; controls disabled", role);
		s_DebugFriendPlayRole = DEBUG_FRIEND_PLAY_NONE;
		return;
	}
	s_DebugFriendPlayPeer = peer->handle;
	s_DebugFriendPlayAutoInvite = sysArgCheck("--debug-friend-play-auto-invite") ? 1 : 0;
	s_DebugFriendPlayAutoAccept = sysArgCheck("--debug-friend-play-auto-accept") ? 1 : 0;
	const char *upload = sysArgGetString("--debug-friend-play-upload-kbps");
	if (upload && upload[0]) {
		char *end = NULL;
		const unsigned long parsed = strtoul(upload, &end, 10);
		if (end && *end == '\0' && parsed <= NET_UPLOAD_KBPS_MAX) {
			s_DebugFriendPlayUploadKbps = (u32)parsed;
			s_DebugFriendPlayUploadSet = 1;
		}
	}
	const char *fail = sysArgGetString("--debug-friend-play-fail-start");
	if (fail && !strcmp(fail, "host")) s_DebugFriendPlayFailHost = 1;
	if (fail && !strcmp(fail, "client")) s_DebugFriendPlayFailClient = 1;
	sysLogPrintf(LOG_NOTE,
		"FRIENDPLAY: role=%s ready local=0x%08x peer=0x%08x upload_kbps=%u auto_invite=%u auto_accept=%u",
		role, (unsigned)socialMyHandle(), (unsigned)s_DebugFriendPlayPeer,
		(unsigned)sampleLocalElectionKbps(),
		(unsigned)s_DebugFriendPlayAutoInvite,
		(unsigned)s_DebugFriendPlayAutoAccept);
}

static void debugFriendPlayTick(void)
{
	const u32 action_delay_ms =
		s_DebugFriendPlayRole == DEBUG_FRIEND_PLAY_INITIATOR ? 6000u : 250u;
	if (s_DebugFriendPlayRole == DEBUG_FRIEND_PLAY_NONE ||
		s_DebugFriendPlayActionDone ||
		SDL_GetTicks() - s_DebugFriendPlayReadyMs < action_delay_ms) {
		return;
	}
	if (s_DebugFriendPlayRole == DEBUG_FRIEND_PLAY_INITIATOR &&
		s_DebugFriendPlayAutoInvite) {
		const s32 rc = presenceSendInvite(s_DebugFriendPlayPeer,
			PRESENCE_INVITE_KIND_MATCH);
		sysLogPrintf(rc == 0 ? LOG_NOTE : LOG_ERROR,
			"FRIENDPLAY: role=initiator invite sent peer=0x%08x rc=%d",
			(unsigned)s_DebugFriendPlayPeer, (int)rc);
		s_DebugFriendPlayActionDone = 1;
	} else if (s_DebugFriendPlayRole == DEBUG_FRIEND_PLAY_INVITEE &&
		s_DebugFriendPlayAutoAccept && presenceInviteCount() > 0) {
		const presence_invite_t *invite = presenceInviteAt(0);
		const u32 from = invite ? invite->from_handle : 0;
		const s32 rc = presenceInviteAccept(0);
		sysLogPrintf(rc == 0 ? LOG_NOTE : LOG_ERROR,
			"FRIENDPLAY: role=invitee invite accepted peer=0x%08x rc=%d",
			(unsigned)from, (int)rc);
		s_DebugFriendPlayActionDone = 1;
	}
}

static group_peer_t *allocPeer(u32 handle)
{
	if (!s_Initialised || handle == 0) return NULL;
	group_peer_t *existing = findPeer(handle);
	if (existing) return existing;
	for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; i++) {
		if (s_Session.peers[i].handle == 0) {
			memset(&s_Session.peers[i], 0, sizeof(group_peer_t));
			s_Session.peers[i].handle = handle;
			s_Session.peers[i].entered_state_ms = SDL_GetTicks();
			s_Session.in_session = 1;
			return &s_Session.peers[i];
		}
	}
	return NULL;
}

static void enterState(group_peer_t *p, group_peer_state_t s)
{
	if (!p) return;
	if (p->state == s) return;
	const group_peer_state_t prev = p->state;
	p->state = s;
	p->entered_state_ms = SDL_GetTicks();
	sysLogPrintf(LOG_NOTE,
	             "GROUP.SESSION: peer 0x%08x %s -> %s",
	             (unsigned)p->handle,
	             groupPeerStateName(prev),
	             groupPeerStateName(s));
}

static void enterFailure(group_peer_t *p, group_fail_reason_t reason)
{
	if (!p) return;
	p2pTurnRegisterRelayCandidate(p->handle, 0, 0, 0);
	p->last_kbps = 0;
	p->last_kbps_ms = 0;
	p->fail = reason;
	enterState(p, GROUP_PEER_FAILED);
	sysLogPrintf(LOG_WARNING,
	             "GROUP.SESSION: peer 0x%08x FAILED reason=%s",
	             (unsigned)p->handle,
	             groupFailReasonText(reason));
}

static s32 hasAcceptedPeer(void)
{
	for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; i++) {
		const group_peer_state_t state = s_Session.peers[i].state;
		if (s_Session.peers[i].handle != 0 &&
			(state == GROUP_PEER_RESOLVING || state == GROUP_PEER_CONNECTED)) {
			return 1;
		}
	}
	return 0;
}

static s32 routeIdentityEqual(const net_match_route_t *a,
		const net_match_route_t *b)
{
	return a && b && a->ipv4 == b->ipv4 && a->port == b->port &&
		a->flags == b->flags;
}

static s32 publishLocalRoute(const net_match_route_t *route)
{
	if (!route) return 0;
	if (routeIdentityEqual(route, &s_PublishedRoute)) return 1;
	if (presenceSetLocalMatchRoute(route) != 0) {
		sysLogPrintf(LOG_ERROR,
			"GROUP.SESSION: failed to publish typed authority route");
		return 0;
	}
	s_PublishedRoute = *route;
	sysLogPrintf(LOG_NOTE,
		"GROUP.SESSION: published typed match route authority=0x%08x flags=0x%02x port=%u",
		(unsigned)s_Session.latched_authority_handle,
		(unsigned)route->flags, (unsigned)route->port);
	return 1;
}

static s32 refreshLocalAuthorityRoute(void)
{
	if (s_Session.transport_state != NET_MATCH_TRANSPORT_SERVER ||
		s_Session.latched_authority_handle != socialMyHandle()) {
		return 0;
	}

	net_match_route_t route;
	memset(&route, 0, sizeof(route));
	route.port = (u16)g_NetServerPort;
	route.flags = NET_MATCH_ROUTE_SOURCE_DERIVED;

	u32 explicit_ipv4 = 0;
	if (netUpnpIsActive() && netUpnpGetExternalIP()[0] &&
		netMatchRouteParseIpv4(netUpnpGetExternalIP(), &explicit_ipv4)) {
		route.ipv4 = explicit_ipv4;
		route.flags = NET_MATCH_ROUTE_UPNP;
	} else if (stunGetStatus() == STUN_STATUS_SUCCESS &&
		stunGetDiscoveryPort() == (u16)g_NetServerPort &&
		stunGetNatType() == STUN_NAT_CONE &&
		stunGetExternalIP()[0] && stunGetExternalPort() != 0 &&
		netMatchRouteParseIpv4(stunGetExternalIP(), &explicit_ipv4)) {
		route.ipv4 = explicit_ipv4;
		route.port = stunGetExternalPort();
		route.flags = NET_MATCH_ROUTE_STUN;
	}
	return publishLocalRoute(&route);
}

/* -------------------------------------------------------------------------
 * Public lifecycle
 * ------------------------------------------------------------------------- */

void groupSessionInit(void)
{
	if (s_Initialised) return;
	memset(&s_Session, 0, sizeof(s_Session));
	memset(&s_PublishedRoute, 0, sizeof(s_PublishedRoute));
	s_Initialised = 1;
	debugFriendPlayInit();
	sysLogPrintf(LOG_NOTE, "GROUP.SESSION: initialised (max peers=%d)",
	             (int)GROUP_SESSION_MAX_PEERS);
}

void groupSessionShutdown(void)
{
	if (!s_Initialised) return;
	for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; i++) {
		if (s_Session.peers[i].handle && s_Session.peers[i].pair_id) {
			p2pPairCancel(s_Session.peers[i].pair_id);
		}
		if (s_Session.peers[i].handle) {
			p2pTurnRegisterRelayCandidate(s_Session.peers[i].handle, 0, 0, 0);
		}
	}
	presenceClearLocalMatchRoute();
	memset(&s_Session, 0, sizeof(s_Session));
	memset(&s_PublishedRoute, 0, sizeof(s_PublishedRoute));
	s_Initialised = 0;
}

const group_session_t *groupSessionGet(void) { return &s_Session; }

u32 groupSessionAuthorityHandle(void) { return s_Session.authority_handle; }

s32 groupSessionIsLocalAuthority(void) { return s_Session.is_local_authority ? 1 : 0; }

/* -------------------------------------------------------------------------
 * Authority election (Section 2.1)
 *
 * Single writer for s_Session.authority_*. Per Mike's bad-value triage
 * principle: never infer the authority from any other source -- always
 * recompute through this function. Callers that change kbps or peer
 * state finish by calling groupSessionRecomputeAuthority.
 * ------------------------------------------------------------------------- */

void groupSessionRecomputeAuthority(void)
{
	if (!s_Initialised) return;
	if (s_Session.latched_authority_handle != 0) {
		s_Session.authority_handle = s_Session.latched_authority_handle;
		s_Session.is_local_authority =
			s_Session.latched_authority_handle == socialMyHandle();
		return;
	}

	/* Candidates: local + every peer that accepted this match invite. The
	 * election must finish before either ENet transport starts; auxiliary
	 * candidate-probe success is deliberately not an eligibility gate. */
	const u32 now_ms = SDL_GetTicks();
	const s32 have_accepted_peer = hasAcceptedPeer();
	if (!s_Session.election_input_latched) {
		s_Session.local_kbps = sampleLocalElectionKbps();
	}
	net_authority_candidate_t candidates[GROUP_SESSION_MAX_PEERS + 1];
	memset(candidates, 0, sizeof(candidates));
	candidates[0].handle = socialMyHandle();
	candidates[0].kbps = s_Session.local_kbps;
	candidates[0].eligible = candidates[0].handle != 0 && have_accepted_peer;
	candidates[0].is_local = 1;
	for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; i++) {
		const group_peer_t *p = &s_Session.peers[i];
		const s32 fresh = p->last_kbps_ms != 0 &&
			(now_ms - p->last_kbps_ms) <= GROUP_KBPS_FRESH_MS;
		candidates[i + 1].handle = p->handle;
		candidates[i + 1].kbps = fresh ? p->last_kbps : 0;
		candidates[i + 1].eligible = p->handle != 0 &&
			(p->state == GROUP_PEER_RESOLVING ||
			 p->state == GROUP_PEER_CONNECTED);
	}

	net_authority_choice_t choice;
	const s32 have_choice = netBandwidthChooseAuthority(candidates,
		GROUP_SESSION_MAX_PEERS + 1, s_Session.initiator_handle, &choice);
	const u32 best_handle = have_choice ? choice.handle : 0;
	const u32 best_kbps = have_choice ? choice.kbps : 0;
	const u8 best_idx = !have_choice || choice.is_local ? 0xFF :
		(u8)(choice.index - 1);

	const u8  prev_local = s_Session.is_local_authority;
	const u32 prev_h     = s_Session.authority_handle;

	s_Session.authority_handle    = best_handle;
	s_Session.authority_idx       = best_idx;
	s_Session.is_local_authority  = have_choice && choice.is_local ? 1 : 0;

	if (prev_h != s_Session.authority_handle ||
	    prev_local != s_Session.is_local_authority) {
		sysLogPrintf(LOG_NOTE,
		             "GROUP.SESSION: authority elected handle=0x%08x kbps=%u local=%d",
		             (unsigned)s_Session.authority_handle,
		             (unsigned)best_kbps,
		             (int)s_Session.is_local_authority);
	}
}

void groupSessionUpdateKbps(u32 handle, u32 kbps)
{
	group_peer_t *p = findPeer(handle);
	if (!p) return;
	if (kbps > NET_UPLOAD_KBPS_MAX) kbps = 0;
	const s32 changed = p->last_kbps != kbps;
	p->last_kbps = kbps;
	p->last_kbps_ms = SDL_GetTicks();
	/* Feed the TURN selection too (it picks the highest reported kbps). */
	if (p->state == GROUP_PEER_CONNECTED && p->ipv4 != 0 && p->port != 0) {
		p2pTurnRegisterRelayCandidate(handle, p->ipv4, p->port, kbps);
	}
	if (changed) groupSessionRecomputeAuthority();
}

u32 groupSessionLocalElectionKbps(void)
{
	return s_Session.election_input_latched ? s_Session.local_kbps :
		sampleLocalElectionKbps();
}

static void recordTransportFailure(const char *action, s32 rc)
{
	s_Session.transport_state = NET_MATCH_TRANSPORT_NONE;
	s_Session.transport_attempted = 1;
	s_Session.transport_failed = 1;
	s_Session.transport_result = (s8)rc;
	s_Session.latched_authority_handle = 0;
	presenceClearLocalMatchRoute();
	memset(&s_PublishedRoute, 0, sizeof(s_PublishedRoute));
	sysLogPrintf(LOG_ERROR,
		"GROUP.MATCH: %s failed rc=%d; transport rolled back",
		action ? action : "startup", (int)rc);
	sysLogPrintf(LOG_NOTE, "GROUP.MATCH: authority latch cleared; no retry");
}

static s32 latchPreconnectAuthority(void)
{
	if (s_Session.latched_authority_handle != 0) {
		return s_Session.latched_authority_handle ==
			s_Session.authority_handle;
	}
	if (s_Session.authority_handle == 0) return 0;

	s_Session.latched_authority_handle = s_Session.authority_handle;
	sysLogPrintf(LOG_NOTE,
		"GROUP.MATCH: pre-connect authority latched handle=0x%08x local=%d",
		(unsigned)s_Session.latched_authority_handle,
		(int)(s_Session.latched_authority_handle == socialMyHandle()));
	return 1;
}

static void driveMatchTransport(void)
{
	if (!s_Initialised || !hasAcceptedPeer()) return;
	if (s_Session.transport_state == NET_MATCH_TRANSPORT_SERVER) {
		(void)refreshLocalAuthorityRoute();
		return;
	}
	if (s_Session.transport_state == NET_MATCH_TRANSPORT_CLIENT ||
		s_Session.transport_attempted) {
		return;
	}
	if (!latchPreconnectAuthority()) return;

	const u32 local_handle = socialMyHandle();
	const u32 authority_handle = s_Session.latched_authority_handle;
	net_match_route_t route;
	memset(&route, 0, sizeof(route));
	const s32 route_ready = authority_handle != 0 &&
		authority_handle != local_handle &&
		presencePeerMatchRoute(authority_handle, &route);
	const net_match_action_t action = netMatchRoutePlanAction(local_handle,
		authority_handle, s_Session.latched_authority_handle,
		(net_match_transport_state_t)s_Session.transport_state, route_ready);

	if (action == NET_MATCH_ACTION_START_SERVER) {
		s_Session.transport_attempted = 1;
		s_Session.transport_attempt_count++;
		sysLogPrintf(LOG_NOTE,
			"GROUP.MATCH: server start attempt authority=0x%08x count=%u",
			(unsigned)authority_handle,
			(unsigned)s_Session.transport_attempt_count);
		const s32 rc = s_DebugFriendPlayFailHost ? -90 :
			netStartServer((u16)g_NetServerPort, g_NetMaxClients);
		if (rc != 0) {
			recordTransportFailure("listen-host start", rc);
			return;
		}
		s_Session.transport_state = NET_MATCH_TRANSPORT_SERVER;
		s_Session.transport_result = 0;
		s_Session.latched_authority_handle = authority_handle;
		sysLogPrintf(LOG_NOTE,
			"GROUP.MATCH: authority latched handle=0x%08x transport=listen-host",
			(unsigned)authority_handle);
		if (!refreshLocalAuthorityRoute()) {
			(void)netDisconnect();
			recordTransportFailure("signed route publication", -3);
		}
		return;
	}

	if (action == NET_MATCH_ACTION_START_CLIENT) {
		char addr[64];
		if (!netMatchRouteFormat(&route, addr, sizeof(addr))) {
			recordTransportFailure("typed route format", -2);
			return;
		}
		s_Session.transport_attempted = 1;
		s_Session.transport_attempt_count++;
		sysLogPrintf(LOG_NOTE,
			"GROUP.MATCH: join attempt authority=0x%08x route_kind=match-server flags=0x%02x count=%u",
			(unsigned)authority_handle, (unsigned)route.flags,
			(unsigned)s_Session.transport_attempt_count);
		const s32 rc = s_DebugFriendPlayFailClient ? -91 :
			netStartClientWithHolePunch(addr);
		if (rc != 0) {
			recordTransportFailure("client join", rc);
			return;
		}
		s_Session.transport_state = NET_MATCH_TRANSPORT_CLIENT;
		s_Session.transport_result = 0;
		s_Session.latched_authority_handle = authority_handle;
		sysLogPrintf(LOG_NOTE,
			"GROUP.MATCH: authority latched handle=0x%08x transport=client route_flags=0x%02x",
			(unsigned)authority_handle, (unsigned)route.flags);
		return;
	}

	if (action == NET_MATCH_ACTION_CONFLICT) {
		recordTransportFailure("authority conflict", -1);
	}
}

/* -------------------------------------------------------------------------
 * Invite hooks
 * ------------------------------------------------------------------------- */

s32 groupSessionRecordSentInvite(u32 invitee_handle)
{
	if (!s_Initialised || invitee_handle == 0) return -1;
	if (socialBlockIsHandle(invitee_handle)) return -1;
	group_peer_t *p = allocPeer(invitee_handle);
	if (!p) {
		sysLogPrintf(LOG_WARNING,
		             "GROUP.SESSION: peer table full -- cannot track invite to 0x%08x",
		             (unsigned)invitee_handle);
		return -1;
	}
	if (p->state == GROUP_PEER_UNKNOWN || p->state == GROUP_PEER_FAILED) {
		p->fail = GROUP_FAIL_NONE;
		enterState(p, GROUP_PEER_INVITED);
	}
	if (s_Session.initiator_handle == 0) {
		s_Session.initiator_handle = socialMyHandle();
	}
	if (!s_Session.election_input_latched) {
		s_Session.local_kbps = sampleLocalElectionKbps();
		s_Session.election_input_latched = 1;
	}
	const presence_peer_t *pp = presencePeerByHandle(invitee_handle);
	if (pp) groupSessionUpdateKbps(invitee_handle, pp->upload_kbps);
	return 0;
}

s32 groupSessionAcceptInvite(u32 inviter_handle)
{
	if (!s_Initialised || inviter_handle == 0) return -1;
	if (socialBlockIsHandle(inviter_handle)) return -1;

	group_peer_t *p = allocPeer(inviter_handle);
	if (!p) return -1;
	p->fail = GROUP_FAIL_NONE;
	if (s_Session.initiator_handle == 0) {
		s_Session.initiator_handle = inviter_handle;
	}
	if (!s_Session.election_input_latched) {
		s_Session.local_kbps = sampleLocalElectionKbps();
		s_Session.election_input_latched = 1;
	}

	/* Pre-flight version check: if the friend's last presence pong reported
	 * a different protocol, trip Q14 mismatch UX without consuming a p2p slot. */
	const presence_peer_t *pp = presencePeerByHandle(inviter_handle);
	if (pp) groupSessionUpdateKbps(inviter_handle, pp->upload_kbps);
	if (pp && pp->proto_version != 0 && pp->proto_version != NET_PROTOCOL_VER) {
		p->their_proto = pp->proto_version;
		strncpy(p->their_agent,
		        socialFriendByHandle(inviter_handle) ?
		            socialFriendByHandle(inviter_handle)->agent_name : "",
		        sizeof(p->their_agent) - 1);
		enterFailure(p, GROUP_FAIL_VERSION_MISMATCH);
		return -1;
	}

	/* Resolve initial endpoint hint from cache. */
	u32 hint_ipv4 = 0; u16 hint_port = 0;
	if (!socialFriendGetEndpoint(inviter_handle, &hint_ipv4, &hint_port)) {
		if (pp && pp->cached_ipv4 != 0) {
			hint_ipv4 = pp->cached_ipv4;
			hint_port = pp->cached_port;
		}
	}

	enterState(p, GROUP_PEER_RESOLVING);
	groupSessionRecomputeAuthority();
	driveMatchTransport();
	const u32 pid = p2pPairBegin(inviter_handle, hint_ipv4, hint_port);
	if (pid == 0) {
		p->probe_failed = 1;
		sysLogPrintf(LOG_WARNING,
			"GROUP.SESSION: auxiliary probe could not start for peer 0x%08x",
			(unsigned)inviter_handle);
	} else {
		p->pair_id = pid;
	}
	return 0;
}

void groupSessionOnInviteResponse(u32 from_handle, s32 accepted)
{
	group_peer_t *p = findPeer(from_handle);
	if (!p) return;
	if (!accepted) {
		enterFailure(p, GROUP_FAIL_REJECTED);
		return;
	}
	if (p->state != GROUP_PEER_INVITED) return;

	/* Same path as accept-side: kick a p2p pair if we haven't already. */
	const presence_peer_t *pp = presencePeerByHandle(from_handle);
	if (pp) groupSessionUpdateKbps(from_handle, pp->upload_kbps);
	u32 hint_ipv4 = 0; u16 hint_port = 0;
	if (!socialFriendGetEndpoint(from_handle, &hint_ipv4, &hint_port)) {
		if (pp && pp->cached_ipv4 != 0) {
			hint_ipv4 = pp->cached_ipv4;
			hint_port = pp->cached_port;
		}
	}
	enterState(p, GROUP_PEER_RESOLVING);
	groupSessionRecomputeAuthority();
	driveMatchTransport();
	const u32 pid = p2pPairBegin(from_handle, hint_ipv4, hint_port);
	if (pid == 0) {
		p->probe_failed = 1;
		sysLogPrintf(LOG_WARNING,
			"GROUP.SESSION: auxiliary probe could not start for peer 0x%08x",
			(unsigned)from_handle);
	} else {
		p->pair_id = pid;
	}
}

void groupSessionDropPeer(u32 handle)
{
	group_peer_t *p = findPeer(handle);
	if (!p) return;
	if (p->pair_id) {
		p2pPairCancel(p->pair_id);
		p->pair_id = 0;
	}
	p2pTurnRegisterRelayCandidate(handle, 0, 0, 0);
	memset(p, 0, sizeof(group_peer_t));
	if (s_Session.initiator_handle == handle) {
		s_Session.initiator_handle = 0;
	}

	/* Recount session-occupancy. */
	s32 any = 0;
	for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; i++) {
		if (s_Session.peers[i].handle != 0) { any = 1; break; }
	}
	s_Session.in_session = (u8)any;
	groupSessionRecomputeAuthority();
}

void groupSessionOnTransportDisconnected(void)
{
	if (!s_Initialised) return;
	const u8 had_transport = s_Session.transport_state != NET_MATCH_TRANSPORT_NONE ||
		s_Session.latched_authority_handle != 0;
	presenceClearLocalMatchRoute();
	memset(&s_PublishedRoute, 0, sizeof(s_PublishedRoute));
	s_Session.latched_authority_handle = 0;
	s_Session.transport_state = NET_MATCH_TRANSPORT_NONE;
	/* A disconnect releases ownership but does not silently start a second
	 * server/join transaction on the next group tick. A fresh invite/session
	 * is the explicit retry boundary. */
	if (had_transport) {
		s_Session.transport_attempted = 1;
		s_Session.transport_failed = 1;
		s_Session.transport_result = -4;
	}
	groupSessionRecomputeAuthority();
	sysLogPrintf(LOG_NOTE,
		"GROUP.MATCH: transport ownership released; automatic retry suppressed");
}

/* -------------------------------------------------------------------------
 * Tick: drive RESOLVING peers to CONNECTED via p2p state polls
 * ------------------------------------------------------------------------- */

static void onPairOpen(group_peer_t *p, const p2p_endpoint_t *ep)
{
	/* Probe endpoints belong only to auxiliary social/group reachability. They
	 * are never formatted as ENet match-server routes. Relay descriptors remain
	 * relay descriptors and likewise cannot enter the match handoff. */
	const s32 relayed = (ep->flags & P2P_EP_RELAYED) != 0;
	const u32 conn_ipv4 = relayed ? ep->relay_ipv4 : ep->ipv4;
	const u16 conn_port = relayed ? ep->relay_port : ep->port;

	p->ipv4 = conn_ipv4;
	p->port = conn_port;
	p->probe_failed = 0;
	enterState(p, GROUP_PEER_CONNECTED);
	if (p->last_kbps != 0 && p->last_kbps_ms != 0 &&
		(SDL_GetTicks() - p->last_kbps_ms) <= GROUP_KBPS_FRESH_MS) {
		p2pTurnRegisterRelayCandidate(p->handle, p->ipv4, p->port,
			p->last_kbps);
	}

	sysLogPrintf(LOG_NOTE,
		"GROUP.SESSION: auxiliary probe open peer=0x%08x flags=0x%02x; ENet route unchanged",
		(unsigned)p->handle, (unsigned)ep->flags);
	groupSessionRecomputeAuthority();
}

void groupSessionTick(void)
{
	if (!s_Initialised) return;
	debugFriendPlayTick();

	const u32 now_ms = SDL_GetTicks();
	const u32 current_local_kbps = sampleLocalElectionKbps();
	s32 authority_dirty = !s_Session.election_input_latched &&
		s_Session.local_kbps != current_local_kbps;

	for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; i++) {
		group_peer_t *p = &s_Session.peers[i];
		if (p->handle == 0) continue;
		if (p->last_kbps_ms != 0 &&
			(now_ms - p->last_kbps_ms) > GROUP_KBPS_FRESH_MS) {
			p2pTurnRegisterRelayCandidate(p->handle, 0, 0, 0);
			p->last_kbps = 0;
			p->last_kbps_ms = 0;
			authority_dirty = 1;
		}
		if (p->state != GROUP_PEER_RESOLVING) continue;
		if (p->pair_id == 0) continue;

		const p2p_pair_state_t st = p2pPairGetState(p->pair_id);
		if (st == P2P_PAIR_OPEN) {
			p2p_endpoint_t ep;
			if (p2pPairGetEndpoint(p->pair_id, &ep)) {
				onPairOpen(p, &ep);
				p2pPairCancel(p->pair_id);
				p->pair_id = 0;
			}
		} else if (st == P2P_PAIR_FAILED) {
			/* Candidate-probe exhaustion does not invalidate a separately
			 * signed ENet route. Keep the accepted peer eligible and record
			 * only the auxiliary failure. */
			const presence_peer_t *pp = presencePeerByHandle(p->handle);
			if (pp && pp->proto_version != 0 && pp->proto_version != NET_PROTOCOL_VER) {
				p->their_proto = pp->proto_version;
				enterFailure(p, GROUP_FAIL_VERSION_MISMATCH);
			} else {
				p->probe_failed = 1;
				sysLogPrintf(LOG_WARNING,
					"GROUP.SESSION: auxiliary probe exhausted peer=0x%08x; waiting on typed match route",
					(unsigned)p->handle);
			}
			p2pPairCancel(p->pair_id);
			p->pair_id = 0;
		}
	}
	if (authority_dirty) groupSessionRecomputeAuthority();
	driveMatchTransport();
}

/* -------------------------------------------------------------------------
 * UX helpers
 * ------------------------------------------------------------------------- */

const char *groupPeerStateName(group_peer_state_t s)
{
	switch (s) {
		case GROUP_PEER_UNKNOWN:   return "UNKNOWN";
		case GROUP_PEER_INVITED:   return "INVITED";
		case GROUP_PEER_RESOLVING: return "RESOLVING";
		case GROUP_PEER_CONNECTED: return "CONNECTED";
		case GROUP_PEER_FAILED:    return "FAILED";
		default: return "?";
	}
}

const char *groupFailReasonText(group_fail_reason_t r)
{
	switch (r) {
		case GROUP_FAIL_NONE:             return "none";
		case GROUP_FAIL_VERSION_MISMATCH: return "version mismatch";
		case GROUP_FAIL_NETWORK_BLOCKED:  return "network blocked";
		case GROUP_FAIL_PEER_OFFLINE:     return "peer offline";
		case GROUP_FAIL_REJECTED:         return "invite rejected";
		case GROUP_FAIL_INTERNAL:         return "internal";
		default: return "?";
	}
}

void groupSessionFormatVersionMismatch(const group_peer_t *peer,
                                        char *out, u32 outsize)
{
	if (!out || outsize == 0) return;
	if (!peer) { out[0] = '\0'; return; }

	/* Q14 verbatim format:
	 *   "Invite failed. Version info (you: 0.1.0, smarch: 0.0.165 (old))"
	 *
	 * We compare against the protocol version (a proxy for "old" here --
	 * if their proto is < ours they are old; if >, we are). The release
	 * version string is what the user sees, but only the protocol carries
	 * via presence; report the proto delta in parens. */
	const u16 mine = NET_PROTOCOL_VER;
	const u16 theirs = peer->their_proto;
	const char *who_old = (theirs < mine) ? "their" :
	                      (theirs > mine) ? "yours" : "neither";

	snprintf(out, outsize,
	         "Invite failed. Version info (you: %s [proto v%u], %s: proto v%u %s%s)",
	         VERSION_STRING,
	         (unsigned)mine,
	         peer->their_agent[0] ? peer->their_agent : "friend",
	         (unsigned)theirs,
	         who_old[0] ? "(" : "",
	         who_old[0] ? (theirs < mine ? "old)" : theirs > mine ? "you're old)" : ")") : "");
}
