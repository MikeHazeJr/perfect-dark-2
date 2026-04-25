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
#include "net/p2p.h"
#include "net/net.h"
#include "social.h"
#include "presence.h"
#include "system.h"
#include "updateversion.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static group_session_t s_Session;
static s32             s_Initialised;

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
	p->fail = reason;
	enterState(p, GROUP_PEER_FAILED);
	sysLogPrintf(LOG_WARNING,
	             "GROUP.SESSION: peer 0x%08x FAILED reason=%s",
	             (unsigned)p->handle,
	             groupFailReasonText(reason));
}

/* -------------------------------------------------------------------------
 * Public lifecycle
 * ------------------------------------------------------------------------- */

void groupSessionInit(void)
{
	if (s_Initialised) return;
	memset(&s_Session, 0, sizeof(s_Session));
	s_Initialised = 1;
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
	}
	memset(&s_Session, 0, sizeof(s_Session));
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

	/* Candidates: local + every CONNECTED peer. Authority must be
	 * locally-confirmed reachable. RESOLVING peers are skipped (their
	 * kbps figure may be stale or absent). */
	u32 best_handle = socialMyHandle();
	u32 best_kbps   = 0;       /* placeholder for local kbps until measurement is wired */
	u8  best_idx    = 0xFF;    /* 0xFF means "local is authority" */

	for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; i++) {
		const group_peer_t *p = &s_Session.peers[i];
		if (p->handle == 0) continue;
		if (p->state != GROUP_PEER_CONNECTED) continue;
		if (p->last_kbps > best_kbps) {
			best_kbps = p->last_kbps;
			best_handle = p->handle;
			best_idx = (u8)i;
		}
	}

	const u8  prev_local = s_Session.is_local_authority;
	const u32 prev_h     = s_Session.authority_handle;

	s_Session.authority_handle    = best_handle;
	s_Session.authority_idx       = best_idx;
	s_Session.is_local_authority  = (best_idx == 0xFF) ? 1 : 0;

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
	if (p->last_kbps == kbps) return;
	p->last_kbps = kbps;
	/* Feed the TURN selection too (it picks the highest reported kbps). */
	p2pTurnRegisterRelayCandidate(handle, p->ipv4, p->port, kbps);
	groupSessionRecomputeAuthority();
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
	return 0;
}

s32 groupSessionAcceptInvite(u32 inviter_handle)
{
	if (!s_Initialised || inviter_handle == 0) return -1;
	if (socialBlockIsHandle(inviter_handle)) return -1;

	group_peer_t *p = allocPeer(inviter_handle);
	if (!p) return -1;
	p->fail = GROUP_FAIL_NONE;

	/* Pre-flight version check: if the friend's last presence pong reported
	 * a different protocol, trip Q14 mismatch UX without consuming a p2p slot. */
	const presence_peer_t *pp = presencePeerByHandle(inviter_handle);
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

	const u32 pid = p2pPairBegin(inviter_handle, hint_ipv4, hint_port);
	if (pid == 0) {
		enterFailure(p, GROUP_FAIL_INTERNAL);
		return -1;
	}
	p->pair_id = pid;
	enterState(p, GROUP_PEER_RESOLVING);
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
	u32 hint_ipv4 = 0; u16 hint_port = 0;
	if (!socialFriendGetEndpoint(from_handle, &hint_ipv4, &hint_port)) {
		if (pp && pp->cached_ipv4 != 0) {
			hint_ipv4 = pp->cached_ipv4;
			hint_port = pp->cached_port;
		}
	}
	const u32 pid = p2pPairBegin(from_handle, hint_ipv4, hint_port);
	if (pid == 0) {
		enterFailure(p, GROUP_FAIL_INTERNAL);
		return;
	}
	p->pair_id = pid;
	enterState(p, GROUP_PEER_RESOLVING);
}

void groupSessionDropPeer(u32 handle)
{
	group_peer_t *p = findPeer(handle);
	if (!p) return;
	if (p->pair_id) {
		p2pPairCancel(p->pair_id);
		p->pair_id = 0;
	}
	memset(p, 0, sizeof(group_peer_t));

	/* Recount session-occupancy. */
	s32 any = 0;
	for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; i++) {
		if (s_Session.peers[i].handle != 0) { any = 1; break; }
	}
	s_Session.in_session = (u8)any;
	groupSessionRecomputeAuthority();
}

/* -------------------------------------------------------------------------
 * Tick: drive RESOLVING peers to CONNECTED via p2p state polls
 * ------------------------------------------------------------------------- */

static void onPairOpen(group_peer_t *p, const p2p_endpoint_t *ep)
{
	p->ipv4 = ep->ipv4;
	p->port = ep->port;
	enterState(p, GROUP_PEER_CONNECTED);

	/* Hand off to the existing match-start flow.  Format the resolved
	 * endpoint as "ip:port" and call netStartClient if we are the
	 * joining peer. The joining peer is by default whichever side
	 * accepted the invite (presence.c sets in_session = 1 just before
	 * us). For Phase 1 we let netStartClient happen unconditionally on
	 * the first successful pair -- the existing CLC_AUTH lobby flow
	 * handles the rest. */
	char addr[64];
	snprintf(addr, sizeof(addr), "%u.%u.%u.%u:%u",
	         (unsigned)((ep->ipv4 >> 24) & 0xFF),
	         (unsigned)((ep->ipv4 >> 16) & 0xFF),
	         (unsigned)((ep->ipv4 >>  8) & 0xFF),
	         (unsigned)((ep->ipv4 >>  0) & 0xFF),
	         (unsigned)ep->port);

	/* If we are not yet talking to anyone via ENet, become the joining
	 * peer. If a netStartClient is already in flight, this is harmless
	 * (returns immediately). The actual lobby-state transitions are
	 * owned by net.c. */
	extern s32 g_NetMode;
	if (g_NetMode == 0) {
		s32 rc = netStartClient(addr);
		sysLogPrintf(LOG_NOTE,
		             "GROUP.SESSION: handing peer 0x%08x to netStartClient(%s) rc=%d",
		             (unsigned)p->handle, addr, (int)rc);
	}

	groupSessionRecomputeAuthority();
}

void groupSessionTick(void)
{
	if (!s_Initialised) return;

	for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; i++) {
		group_peer_t *p = &s_Session.peers[i];
		if (p->handle == 0) continue;
		if (p->state != GROUP_PEER_RESOLVING) continue;
		if (p->pair_id == 0) continue;

		const p2p_pair_state_t st = p2pPairGetState(p->pair_id);
		if (st == P2P_PAIR_OPEN) {
			p2p_endpoint_t ep;
			if (p2pPairGetEndpoint(p->pair_id, &ep)) {
				onPairOpen(p, &ep);
			}
		} else if (st == P2P_PAIR_FAILED) {
			/* If presence ever showed a version mismatch, surface that
			 * specifically; otherwise the failure is treated as
			 * network-blocked (Section 2.4 unsolvable residue). */
			const presence_peer_t *pp = presencePeerByHandle(p->handle);
			if (pp && pp->proto_version != 0 && pp->proto_version != NET_PROTOCOL_VER) {
				p->their_proto = pp->proto_version;
				enterFailure(p, GROUP_FAIL_VERSION_MISMATCH);
			} else {
				enterFailure(p, GROUP_FAIL_NETWORK_BLOCKED);
			}
			p2pPairCancel(p->pair_id);
			p->pair_id = 0;
		}
	}
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
