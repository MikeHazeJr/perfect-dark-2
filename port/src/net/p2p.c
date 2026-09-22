/**
 * p2p.c -- 6-tier peer-to-peer escalation orchestrator.
 *
 * Each pair walks tiers 0..5 in sequence. Per-tier soft timeout is
 * P2P_TIER_TIMEOUT_MS. Tier modules call p2pInternalReportSuccess when an
 * endpoint opens, or p2pInternalReportFailure to ask the orchestrator to
 * escalate.
 *
 * Pair table is a fixed-size array (P2P_MAX_PAIRS). For Phase 1 a 4-peer
 * mesh has 6 active pairs; we provision well above that to support
 * presence pings to the friend list and pending invites overlapping with
 * an active group.
 */

#include "net/p2p.h"
#include "presence.h"
#include "social.h"
#include "system.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define P2P_MAX_PAIRS 64
#define P2P_MAX_PENDING_CANDIDATE_SETS 32

typedef struct {
	u32              pair_id;
	u32              peer_handle;
	u32              hint_ipv4;
	u16              hint_port;
	p2p_pair_state_t state;
	p2p_tier_t       current_tier;
	p2p_tier_t       last_attempted_tier;
	u32              attempt_count;
	u32              start_ms;
	u32              tier_start_ms;
	u32              tier_attempt_ms;     /* when tier was last (re-)kicked */
	net_candidate_signal_retry_t candidate_signal_retry;
	p2p_endpoint_t   endpoint;
	char             last_error[96];
	u8               in_use;
} p2p_pair_t;

typedef struct {
	u32 peer_handle;
	net_candidate_set_t set;
	net_candidate_peer_state_t state;
	u8 in_use;
} p2p_pending_candidate_set_t;

static p2p_pair_t s_Pairs[P2P_MAX_PAIRS];
static p2p_pending_candidate_set_t s_PendingCandidateSets[
	P2P_MAX_PENDING_CANDIDATE_SETS];
static u32        s_NextPairId = 1;
static s32        s_Initialised;

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

const char *p2pTierName(p2p_tier_t t)
{
	switch (t) {
		case P2P_TIER_LAN:    return "LAN";
		case P2P_TIER_DIRECT: return "DIRECT";
		case P2P_TIER_STUN:   return "STUN";
		case P2P_TIER_UPNP:   return "UPNP";
		case P2P_TIER_ICE:    return "ICE";
		case P2P_TIER_TURN:   return "TURN";
		default:              return "NONE";
	}
}

const char *p2pTierUxLabel(p2p_tier_t t)
{
	switch (t) {
		case P2P_TIER_LAN:    return "Looking for LAN peers...";
		case P2P_TIER_DIRECT: return "Trying direct connection...";
		case P2P_TIER_STUN:   return "Trying NAT traversal...";
		case P2P_TIER_UPNP:   return "Trying NAT port mapping...";
		case P2P_TIER_ICE:    return "Negotiating connection candidates...";
		case P2P_TIER_TURN:   return "Trying relay...";
		default:              return "";
	}
}

u32 p2pNowMs(void) { return SDL_GetTicks(); }

static p2p_pair_t *findPair(u32 pair_id)
{
	if (pair_id == P2P_PAIR_ID_INVALID) return NULL;
	for (s32 i = 0; i < P2P_MAX_PAIRS; i++) {
		if (s_Pairs[i].in_use && s_Pairs[i].pair_id == pair_id) return &s_Pairs[i];
	}
	return NULL;
}

static p2p_pair_t *findPairByPeer(u32 peer_handle)
{
	for (s32 i = 0; i < P2P_MAX_PAIRS; i++) {
		if (s_Pairs[i].in_use && s_Pairs[i].peer_handle == peer_handle) {
			return &s_Pairs[i];
		}
	}
	return NULL;
}

static p2p_pair_t *allocPair(void)
{
	for (s32 i = 0; i < P2P_MAX_PAIRS; i++) {
		if (!s_Pairs[i].in_use) return &s_Pairs[i];
	}
	return NULL;
}

static void resetPair(p2p_pair_t *p)
{
	memset(p, 0, sizeof(*p));
}

static void enterTier(p2p_pair_t *p, p2p_tier_t tier)
{
	p->current_tier = tier;
	p->tier_start_ms = p2pNowMs();
	p->tier_attempt_ms = p->tier_start_ms;
	if ((s32)tier > (s32)p->last_attempted_tier) {
		p->last_attempted_tier = tier;
	}
	p->attempt_count++;
	sysLogPrintf(LOG_NOTE,
	             "P2P.NAT: pair=%u peer=0x%08x enter tier=%s",
	             (unsigned)p->pair_id, (unsigned)p->peer_handle,
	             p2pTierName(tier));

	switch (tier) {
		case P2P_TIER_LAN:    p2pLanTick(); break; /* tier 0 is passive; lookup handled in tick */
		case P2P_TIER_DIRECT:
			/* A presence hint is not an authenticated transport proof. Preserve
			 * the diagnostic tier, then require the signed ICE path. */
			p2pInternalReportFailure(p->pair_id, P2P_TIER_DIRECT,
				"legacy direct hint requires signed ICE proof");
			break;
		case P2P_TIER_STUN:   p2pStunStart(p->pair_id); break;
		case P2P_TIER_UPNP:   p2pUpnpStart(p->pair_id); break;
		case P2P_TIER_ICE:
			/* Candidate signaling has its own bounded schedule because probe
			 * retries cannot begin until the remote signed set arrives. */
			netCandidateSignalRetryInit(&p->candidate_signal_retry,
				p->tier_start_ms);
			if (netCandidateSignalRetryPoll(&p->candidate_signal_retry,
				p->tier_start_ms,
				p->tier_start_ms + P2P_TIER_TIMEOUT_MS) ==
				NET_CANDIDATE_SIGNAL_RETRY_SEND) {
				(void)presenceSendCandidateRefresh(p->peer_handle);
			}
			p2pIceStart(p->pair_id, p->peer_handle);
			break;
		case P2P_TIER_TURN:
			p2pInternalReportFailure(p->pair_id, P2P_TIER_TURN,
				"relay allocation proof is not authenticated");
			break;
		default: break;
	}
}

static void escalate(p2p_pair_t *p, const char *reason)
{
	p2p_tier_t next = (p2p_tier_t)((s32)p->current_tier + 1);
	sysLogPrintf(LOG_NOTE,
	             "P2P.NAT: pair=%u peer=0x%08x tier=%s -> tier=%s reason=%s",
	             (unsigned)p->pair_id, (unsigned)p->peer_handle,
	             p2pTierName(p->current_tier), p2pTierName(next),
	             reason ? reason : "timeout");

	if ((s32)next >= (s32)P2P_TIER_COUNT) {
		p->state = P2P_PAIR_FAILED;
		snprintf(p->last_error, sizeof(p->last_error),
		         "all tiers exhausted (%s)", reason ? reason : "");
		sysLogPrintf(LOG_WARNING,
		             "P2P.NAT: pair=%u peer=0x%08x FAILED -- %s",
		             (unsigned)p->pair_id, (unsigned)p->peer_handle,
		             p->last_error);
		return;
	}

	enterTier(p, next);
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void p2pInit(void)
{
	if (s_Initialised) return;
	memset(s_Pairs, 0, sizeof(s_Pairs));
	memset(s_PendingCandidateSets, 0, sizeof(s_PendingCandidateSets));
	s_NextPairId = 1;
	p2pLanStart();
	s_Initialised = 1;
	sysLogPrintf(LOG_NOTE, "P2P.NAT: layer initialised (max pairs=%d)",
	             (int)P2P_MAX_PAIRS);
}

void p2pShutdown(void)
{
	if (!s_Initialised) return;
	for (s32 i = 0; i < P2P_MAX_PAIRS; i++) {
		if (s_Pairs[i].in_use) p2pPairCancel(s_Pairs[i].pair_id);
	}
	p2pDirectShutdown();
	p2pStunShutdown();
	p2pUpnpShutdown();
	p2pIceShutdown();
	p2pTurnShutdown();
	p2pLanStop();
	memset(s_Pairs, 0, sizeof(s_Pairs));
	memset(s_PendingCandidateSets, 0, sizeof(s_PendingCandidateSets));
	s_Initialised = 0;
}

void p2pTick(void)
{
	if (!s_Initialised) return;

	p2pLanTick();
	p2pDirectPoll();
	p2pStunPoll();
	p2pUpnpPoll();
	p2pIcePoll();
	p2pTurnPoll();

	const u32 now = p2pNowMs();
	for (s32 i = 0; i < P2P_MAX_PAIRS; i++) {
		p2p_pair_t *p = &s_Pairs[i];
		if (!p->in_use) continue;
		if (p->state != P2P_PAIR_WORKING) continue;

		/* LAN discovery may refresh a hint, but an announcement is not an
		 * authenticated, target-bound reachability proof. */
		if (p->current_tier == P2P_TIER_LAN) {
			u32 ipv4 = 0; u16 port = 0;
			if (p2pLanLookup(p->peer_handle, &ipv4, &port)) {
				p->hint_ipv4 = ipv4;
				p->hint_port = port;
			}
		}
		if (p->current_tier == P2P_TIER_ICE &&
			netCandidateSignalRetryPoll(&p->candidate_signal_retry, now,
				p->tier_start_ms + P2P_TIER_TIMEOUT_MS) ==
				NET_CANDIDATE_SIGNAL_RETRY_SEND) {
			(void)presenceSendCandidateRefresh(p->peer_handle);
		}

		const u32 elapsed = now - p->tier_start_ms;
		if (elapsed >= P2P_TIER_TIMEOUT_MS) {
			escalate(p, "timeout");
		}
	}
}

u32 p2pPairBegin(u32 peer_handle, u32 hint_ipv4, u16 hint_port)
{
	if (!s_Initialised || peer_handle == 0) return P2P_PAIR_ID_INVALID;

	p2p_pair_t *exist = findPairByPeer(peer_handle);
	if (exist) {
		/* Refresh the hint if the caller now has a better one. */
		if (hint_ipv4 != 0 && exist->hint_ipv4 == 0) {
			exist->hint_ipv4 = hint_ipv4;
			exist->hint_port = hint_port;
		}
		return exist->pair_id;
	}

	p2p_pair_t *p = allocPair();
	if (!p) {
		sysLogPrintf(LOG_WARNING, "P2P.NAT: pair table full -- refusing peer=0x%08x",
		             (unsigned)peer_handle);
		return P2P_PAIR_ID_INVALID;
	}

	resetPair(p);
	p->in_use = 1;
	p->pair_id = s_NextPairId++;
	if (s_NextPairId == 0) s_NextPairId = 1;
	p->peer_handle = peer_handle;
	p->hint_ipv4 = hint_ipv4;
	p->hint_port = hint_port;
	p->state = P2P_PAIR_WORKING;
	p->current_tier = P2P_TIER_NONE;
	p->last_attempted_tier = P2P_TIER_NONE;
	p->start_ms = p2pNowMs();

	/* Candidate frames can arrive with presence pings before the caller has
	 * requested a pair. Keep the latest validated set bounded and attach it
	 * once the pair is created; ICE still performs its own validation. */
	for (s32 i = 0; i < P2P_MAX_PENDING_CANDIDATE_SETS; i++) {
		p2p_pending_candidate_set_t *pending = &s_PendingCandidateSets[i];
		if (!pending->in_use || pending->peer_handle != peer_handle) continue;
		if ((u32)time(NULL) <= pending->set.expires_unix_seconds) {
			(void)p2pIceApplyPeerCandidateSet(p->pair_id, &pending->set);
		}
		memset(pending, 0, sizeof(*pending));
		break;
	}

	enterTier(p, P2P_TIER_LAN);
	return p->pair_id;
}

static p2p_pending_candidate_set_t *findPendingCandidateSet(u32 peer_handle)
{
	for (s32 i = 0; i < P2P_MAX_PENDING_CANDIDATE_SETS; i++) {
		if (s_PendingCandidateSets[i].in_use &&
			s_PendingCandidateSets[i].peer_handle == peer_handle) {
			return &s_PendingCandidateSets[i];
		}
	}
	return NULL;
}

static p2p_pending_candidate_set_t *allocPendingCandidateSet(void)
{
	for (s32 i = 0; i < P2P_MAX_PENDING_CANDIDATE_SETS; i++) {
		if (!s_PendingCandidateSets[i].in_use) {
			return &s_PendingCandidateSets[i];
		}
	}
	return NULL;
}

s32 p2pPeerCandidateSetReceived(u32 peer_handle,
	const net_candidate_set_t *set)
{
	/* A zero local handle is not a wildcard. Do not retain or route any
	 * candidate until social identity has been initialized. */
	if (!s_Initialised || peer_handle == 0 || !set || socialMyHandle() == 0) {
		return 0;
	}
	const time_t now_time = time(NULL);
	if (now_time <= 0 || (u64)now_time > 0xffffffffu) return 0;
	const u32 now = (u32)now_time;
	net_candidate_set_t normalized;
	if (!netCandidateSetNormalize(set, peer_handle, socialMyHandle(),
		now, &normalized)) return 0;

	p2p_pair_t *pair = findPairByPeer(peer_handle);
	if (pair) {
		const u32 pair_id = pair->pair_id;
		s32 result = p2pIceApplyPeerCandidateSet(pair_id, &normalized);
		if (result == 3) p2pPairCancel(pair_id);
		return result;
	}

	p2p_pending_candidate_set_t *pending = findPendingCandidateSet(peer_handle);
	if (pending && pending->in_use && pending->state.expires_unix_seconds < now) {
		/* An expired epoch no longer protects live state or blocks a safe
		 * restart at generation 1. Never use it as a planner predecessor. */
		memset(pending, 0, sizeof(*pending));
		pending = NULL;
	}
	if (!pending) pending = allocPendingCandidateSet();
	if (!pending) return 0;

	net_candidate_update_t update = netCandidatePlanUpdate(
		pending->in_use ? &pending->state : NULL, &normalized);
	if (update == NET_CANDIDATE_UPDATE_REJECT) return 0;
	if (update == NET_CANDIDATE_UPDATE_DUPLICATE) return 2;

	pending->peer_handle = peer_handle;
	pending->set = normalized;
	netCandidatePeerStateCommit(&pending->state, &normalized);
	pending->in_use = 1;
	return update == NET_CANDIDATE_UPDATE_RETIRE ? 3 : 1;
}

void p2pPairCancel(u32 pair_id)
{
	p2p_pair_t *p = findPair(pair_id);
	if (!p) return;
	sysLogPrintf(LOG_NOTE, "P2P.NAT: pair=%u peer=0x%08x cancel",
	             (unsigned)p->pair_id, (unsigned)p->peer_handle);
	const u32 peer_handle = p->peer_handle;
	p2pDirectCancel(pair_id);
	p2pStunCancel(pair_id);
	p2pUpnpCancel(pair_id);
	p2pIceCancel(pair_id, peer_handle);
	p2pTurnCancel(pair_id);
	for (s32 i = 0; i < P2P_MAX_PENDING_CANDIDATE_SETS; i++) {
		if (s_PendingCandidateSets[i].in_use &&
			s_PendingCandidateSets[i].peer_handle == peer_handle) {
			memset(&s_PendingCandidateSets[i], 0,
				sizeof(s_PendingCandidateSets[i]));
		}
	}
	resetPair(p);
}

p2p_pair_state_t p2pPairGetState(u32 pair_id)
{
	p2p_pair_t *p = findPair(pair_id);
	return p ? p->state : P2P_PAIR_IDLE;
}

s32 p2pPairGetEndpoint(u32 pair_id, p2p_endpoint_t *out)
{
	p2p_pair_t *p = findPair(pair_id);
	if (!p || p->state != P2P_PAIR_OPEN) return 0;
	if (out) *out = p->endpoint;
	return 1;
}

s32 p2pPairDiag(u32 pair_id, p2p_pair_diag_t *out)
{
	p2p_pair_t *p = findPair(pair_id);
	if (!p || !out) return 0;
	memset(out, 0, sizeof(*out));
	out->pair_id = p->pair_id;
	out->peer_handle = p->peer_handle;
	out->state = p->state;
	out->current_tier = p->current_tier;
	out->last_attempted_tier = p->last_attempted_tier;
	out->attempt_count = p->attempt_count;
	const u32 now = p2pNowMs();
	out->ms_since_pair_start = now - p->start_ms;
	out->ms_in_current_tier  = now - p->tier_start_ms;
	out->endpoint = p->endpoint;
	memcpy(out->last_error, p->last_error, sizeof(out->last_error));
	return 1;
}

s32 p2pPairCount(void)
{
	s32 n = 0;
	for (s32 i = 0; i < P2P_MAX_PAIRS; i++) if (s_Pairs[i].in_use) n++;
	return n;
}

u32 p2pPairIdAt(s32 idx)
{
	s32 seen = 0;
	for (s32 i = 0; i < P2P_MAX_PAIRS; i++) {
		if (!s_Pairs[i].in_use) continue;
		if (seen == idx) return s_Pairs[i].pair_id;
		seen++;
	}
	return P2P_PAIR_ID_INVALID;
}

/* -------------------------------------------------------------------------
 * Internal callbacks
 * ------------------------------------------------------------------------- */

void p2pInternalReportSuccess(u32 pair_id, p2p_tier_t tier,
                              const p2p_endpoint_t *ep)
{
	p2p_pair_t *p = findPair(pair_id);
	if (!p || !ep) return;
	if (p->state != P2P_PAIR_WORKING) return;
	if (tier != P2P_TIER_ICE) {
		/* Discovery/gathering stages cannot bypass the signed candidate proof. */
		p2pInternalReportFailure(pair_id, tier,
			"tier cannot publish unauthenticated reachability");
		return;
	}
	if (p->current_tier != tier) {
		/* A late callback from a previously-escalated tier still counts as
		 * a working channel -- accept it but record the discrepancy. */
		sysLogPrintf(LOG_NOTE,
		             "P2P.NAT: pair=%u late success on tier=%s (was on %s)",
		             (unsigned)p->pair_id, p2pTierName(tier),
		             p2pTierName(p->current_tier));
	}
	p->endpoint = *ep;
	p->state = P2P_PAIR_OPEN;
	p->last_error[0] = '\0';
	const char *kind = (ep->flags & P2P_EP_RELAYED) ? "relay"
	                  : (ep->flags & P2P_EP_HOLE_PUNCH) ? "punch"
	                  : (ep->flags & P2P_EP_PORT_MAPPED) ? "mapped"
	                  : (ep->flags & P2P_EP_ICE_PAIR) ? "ice"
	                  : "direct";
	sysLogPrintf(LOG_NOTE,
	             "P2P.NAT: pair=%u peer=0x%08x OPEN via %s tier=%s endpoint=%u.%u.%u.%u:%u",
	             (unsigned)p->pair_id, (unsigned)p->peer_handle,
	             kind, p2pTierName(tier),
	             (ep->ipv4 >> 24) & 0xFF, (ep->ipv4 >> 16) & 0xFF,
	             (ep->ipv4 >>  8) & 0xFF, (ep->ipv4 >>  0) & 0xFF,
	             (unsigned)ep->port);
}

void p2pInternalReportFailure(u32 pair_id, p2p_tier_t tier,
                              const char *reason)
{
	p2p_pair_t *p = findPair(pair_id);
	if (!p) return;
	if (p->state != P2P_PAIR_WORKING) return;
	if (p->current_tier != tier) {
		/* Stale failure from a prior tier; ignore. */
		return;
	}
	if (reason && reason[0]) {
		strncpy(p->last_error, reason, sizeof(p->last_error) - 1);
		p->last_error[sizeof(p->last_error) - 1] = '\0';
	}
	escalate(p, reason);
}
