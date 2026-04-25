/**
 * p2p_stun.c -- Tier 2: STUN-assisted reflexive endpoint gathering.
 *
 * Wraps the existing port/src/net/netstun.c worker thread. On tier-2
 * activation we kick a STUN discovery for our local p2p socket port (or
 * reuse a recent successful result), then publish the reflexive endpoint
 * into the LAN cache so any peer that already saw us locally can also
 * reach us across the internet using the same announcement format.
 *
 * Phase 1 keeps signaling out-of-process: friend connect-codes carry the
 * peer's last-known reflexive endpoint via the existing presence packets
 * (Section 3.5). For peers we have never met, the STUN result alone is
 * not enough -- the orchestrator records the reflexive address and falls
 * through to the ICE / TURN tiers when no inbound contact arrives.
 */

#include "net/p2p.h"
#include "net/netstun.h"
#include "system.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
#endif

#define P2P_STUN_DISCOVERY_TTL_MS 60000  /* result lives for 1 minute */

typedef struct {
	u32 pair_id;
	u32 deadline_ms;
	u8  in_use;
	u8  awaiting_stun;
} stun_attempt_t;

#define P2P_STUN_MAX_INFLIGHT 16

static stun_attempt_t s_Attempts[P2P_STUN_MAX_INFLIGHT];

static u32 s_LastStunAtMs;
static u32 s_LastReflexiveIpv4;
static u16 s_LastReflexivePort;
static s32 s_StunActive;

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static stun_attempt_t *findFreeSlot(void)
{
	for (s32 i = 0; i < P2P_STUN_MAX_INFLIGHT; i++) {
		if (!s_Attempts[i].in_use) return &s_Attempts[i];
	}
	return NULL;
}

static stun_attempt_t *findByPair(u32 pair_id)
{
	for (s32 i = 0; i < P2P_STUN_MAX_INFLIGHT; i++) {
		if (s_Attempts[i].in_use && s_Attempts[i].pair_id == pair_id) return &s_Attempts[i];
	}
	return NULL;
}

static u32 parseIpv4(const char *s)
{
	if (!s || !*s) return 0;
	struct in_addr addr;
#ifdef _WIN32
	if (inet_pton(AF_INET, s, &addr) != 1) return 0;
#else
	if (inet_pton(AF_INET, s, &addr) != 1) return 0;
#endif
	return ntohl(addr.s_addr);
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

s32 p2pPublishMyReflexive(u32 ipv4, u16 port)
{
	s_LastReflexiveIpv4 = ipv4;
	s_LastReflexivePort = port;
	s_LastStunAtMs = SDL_GetTicks();
	if (ipv4 != 0 && port != 0) {
		sysLogPrintf(LOG_NOTE,
		             "P2P.STUN: published reflexive %u.%u.%u.%u:%u",
		             (ipv4 >> 24) & 0xFF, (ipv4 >> 16) & 0xFF,
		             (ipv4 >>  8) & 0xFF, (ipv4 >>  0) & 0xFF,
		             (unsigned)port);
	}
	return 0;
}

u32 p2pMyReflexiveIpv4(void) { return s_LastReflexiveIpv4; }
u16 p2pMyReflexivePort(void) { return s_LastReflexivePort; }

s32 p2pStunStart(u32 pair_id)
{
	stun_attempt_t *a = findByPair(pair_id);
	if (!a) a = findFreeSlot();
	if (!a) {
		p2pInternalReportFailure(pair_id, P2P_TIER_STUN, "table full");
		return -1;
	}

	a->in_use = 1;
	a->pair_id = pair_id;
	a->deadline_ms = SDL_GetTicks() + P2P_TIER_TIMEOUT_MS;
	a->awaiting_stun = 0;

	const u32 now_ms = SDL_GetTicks();
	if (s_LastReflexiveIpv4 != 0 && (now_ms - s_LastStunAtMs) < P2P_STUN_DISCOVERY_TTL_MS) {
		/* Already have a fresh reflexive; treat as soft-success so the
		 * orchestrator can move forward. ICE will still try to combine
		 * this with the peer's reflexive. */
		p2p_endpoint_t ep;
		memset(&ep, 0, sizeof(ep));
		ep.ipv4 = s_LastReflexiveIpv4;
		ep.port = s_LastReflexivePort;
		ep.flags = P2P_EP_HOLE_PUNCH;
		p2pInternalReportSuccess(pair_id, P2P_TIER_STUN, &ep);
		a->in_use = 0;
		return 0;
	}

	/* Kick a fresh discovery if none is active. */
	if (!s_StunActive) {
		stunDiscoverAsync(0); /* port 0 -> netstun's own ephemeral test socket */
		s_StunActive = 1;
		s_LastStunAtMs = now_ms;
		sysLogPrintf(LOG_NOTE, "P2P.STUN: kicked async discovery for pair=%u",
		             (unsigned)pair_id);
	}
	a->awaiting_stun = 1;
	return 0;
}

void p2pStunPoll(void)
{
	const u32 now_ms = SDL_GetTicks();

	if (s_StunActive) {
		s32 status = stunGetStatus();
		if (status == STUN_STATUS_SUCCESS) {
			s_LastReflexiveIpv4 = parseIpv4(stunGetExternalIP());
			s_LastReflexivePort = stunGetExternalPort();
			s_StunActive = 0;
			s_LastStunAtMs = now_ms;
			sysLogPrintf(LOG_NOTE,
			             "P2P.STUN: discovery success ip=%s port=%u nat=%d",
			             stunGetExternalIP(), (unsigned)s_LastReflexivePort,
			             (int)stunGetNatType());
		} else if (status == STUN_STATUS_FAILED) {
			s_StunActive = 0;
			sysLogPrintf(LOG_WARNING, "P2P.STUN: discovery failed");
		}
	}

	for (s32 i = 0; i < P2P_STUN_MAX_INFLIGHT; i++) {
		stun_attempt_t *a = &s_Attempts[i];
		if (!a->in_use) continue;

		if (a->awaiting_stun && s_LastReflexiveIpv4 != 0) {
			p2p_endpoint_t ep;
			memset(&ep, 0, sizeof(ep));
			ep.ipv4 = s_LastReflexiveIpv4;
			ep.port = s_LastReflexivePort;
			ep.flags = P2P_EP_HOLE_PUNCH;
			p2pInternalReportSuccess(a->pair_id, P2P_TIER_STUN, &ep);
			a->in_use = 0;
			continue;
		}

		if (now_ms >= a->deadline_ms) {
			p2pInternalReportFailure(a->pair_id, P2P_TIER_STUN, "stun timeout");
			a->in_use = 0;
		}
	}
}
