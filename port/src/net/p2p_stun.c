/**
 * p2p_stun.c -- Tier 2: STUN candidate gathering.
 *
 * STUN discovery is a candidate source, not a successful peer path. A
 * reflexive endpoint is exposed only when it came from the active ICE
 * listen port and a cone NAT result. In particular, the netstun port-zero
 * probe is never accepted as candidate or D-003 route provenance.
 */

#include "net/p2p.h"
#include "net/net.h"
#include "net/netstun.h"
#include "system.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
#endif

#define P2P_STUN_DISCOVERY_TTL_MS 60000

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
static u16 s_LastDiscoveryPort;
static s32 s_LastNatType;
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
		if (s_Attempts[i].in_use && s_Attempts[i].pair_id == pair_id) {
			return &s_Attempts[i];
		}
	}
	return NULL;
}

static s32 getIcePort(u16 *out_port)
{
	u32 ipv4 = 0;
	return out_port && p2pIceGetLocalHostCandidate(&ipv4, out_port);
}

static s32 reflexiveIsFreshForIce(u32 *out_ipv4, u16 *out_port)
{
	u16 ice_port = 0;
	if (!getIcePort(&ice_port) || s_LastNatType != STUN_NAT_CONE ||
		s_LastDiscoveryPort == 0 || s_LastDiscoveryPort != ice_port ||
		!s_LastReflexiveIpv4 || !s_LastReflexivePort ||
		(SDL_GetTicks() - s_LastStunAtMs) > P2P_STUN_DISCOVERY_TTL_MS) {
		return 0;
	}
	if (out_ipv4) *out_ipv4 = s_LastReflexiveIpv4;
	if (out_port) *out_port = s_LastReflexivePort;
	return 1;
}

/* Only the successful, provenance-checked poll path may publish a reflexive
 * endpoint. Keeping this helper private prevents arbitrary callers from
 * bypassing the active ICE-port and cone-NAT checks. */
static s32 publishReflexiveFromStun(u32 ipv4, u16 port)
{
	if (!netCandidateIpv4IsPublicRoute(ipv4) || port == 0) {
		s_LastReflexiveIpv4 = 0;
		s_LastReflexivePort = 0;
		s_LastStunAtMs = 0;
		return -1;
	}
	s_LastReflexiveIpv4 = ipv4;
	s_LastReflexivePort = port;
	s_LastStunAtMs = SDL_GetTicks();
	sysLogPrintf(LOG_NOTE,
		"P2P.STUN: published reflexive %u.%u.%u.%u:%u",
		(ipv4 >> 24) & 0xFF, (ipv4 >> 16) & 0xFF,
		(ipv4 >> 8) & 0xFF, ipv4 & 0xFF, (unsigned)port);
	return 0;
}

s32 p2pStunGetReflexiveCandidate(u32 *out_ipv4, u16 *out_port)
{
	return reflexiveIsFreshForIce(out_ipv4, out_port);
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

	u16 ice_port = 0;
	if (!getIcePort(&ice_port)) {
		p2pInternalReportFailure(pair_id, P2P_TIER_STUN, "no ICE listen port");
		a->in_use = 0;
		return -1;
	}

	/* A server's existing STUN result belongs to the ENet route and must
	 * remain untouched. D-003 route publication requires that exact port. */
	if (g_NetMode == NETMODE_SERVER && g_NetServerPort != 0 &&
		stunGetStatus() == STUN_STATUS_SUCCESS &&
		stunGetDiscoveryPort() == (u16)g_NetServerPort &&
		stunGetDiscoveryPort() != ice_port) {
		p2pInternalReportFailure(pair_id, P2P_TIER_STUN,
			"ENet STUN result is route-owned");
		a->in_use = 0;
		return -1;
	}
	if (stunGetStatus() == STUN_STATUS_WORKING) {
		p2pInternalReportFailure(pair_id, P2P_TIER_STUN,
			"route STUN worker owns a different socket");
		a->in_use = 0;
		return -1;
	}

	u32 ipv4 = 0;
	u16 port = 0;
	if (reflexiveIsFreshForIce(&ipv4, &port)) {
		/* Gathering completed; it does not open a peer path. Let the
		 * orchestrator continue to UPnP/ICE where the signed peer set is used. */
		p2pInternalReportFailure(pair_id, P2P_TIER_STUN,
			"fresh STUN candidate ready");
		a->in_use = 0;
		return 0;
	}

	if (!s_StunActive) {
		if (p2pIceStunStart() != 0) {
			p2pInternalReportFailure(pair_id, P2P_TIER_STUN,
				"STUN discovery start failed");
			a->in_use = 0;
			return -1;
		}
		s_StunActive = 1;
		s_LastStunAtMs = SDL_GetTicks();
		s_LastDiscoveryPort = ice_port;
		sysLogPrintf(LOG_NOTE,
			"P2P.STUN: kicked async discovery on ICE port %u for pair=%u",
			(unsigned)ice_port, (unsigned)pair_id);
	}
	a->awaiting_stun = 1;
	return 0;
}

void p2pStunPoll(void)
{
	const u32 now_ms = SDL_GetTicks();

	if (s_StunActive) {
		u32 ipv4 = 0;
		u16 port = 0;
		u16 discovery_port = 0;
		s32 nat_type = STUN_NAT_UNKNOWN;
		s32 status = p2pIceStunGetStatus(&ipv4, &port, &nat_type,
			&discovery_port);
		if (status == STUN_STATUS_SUCCESS) {
			u16 ice_port = 0;
			s_LastDiscoveryPort = discovery_port;
			s_LastNatType = nat_type;
			s32 have_ice_port = getIcePort(&ice_port);
			s_StunActive = 0;
			if (have_ice_port && s_LastDiscoveryPort == ice_port &&
				nat_type == STUN_NAT_CONE &&
				netCandidateIpv4IsPublicRoute(ipv4) && port != 0) {
				publishReflexiveFromStun(ipv4, port);
				sysLogPrintf(LOG_NOTE,
					"P2P.STUN: same-socket candidate ready %u.%u.%u.%u:%u nat=%d",
					(ipv4 >> 24) & 0xff, (ipv4 >> 16) & 0xff,
					(ipv4 >> 8) & 0xff, ipv4 & 0xff,
					(unsigned)port, (int)nat_type);
			} else {
				s_LastReflexiveIpv4 = 0;
				s_LastReflexivePort = 0;
				sysLogPrintf(LOG_WARNING,
					"P2P.STUN: result rejected as unavailable ICE port, non-cone, or wrong-port provenance");
			}
		} else if (status == STUN_STATUS_FAILED) {
			s_StunActive = 0;
			s_LastReflexiveIpv4 = 0;
			s_LastReflexivePort = 0;
			s_LastNatType = STUN_NAT_UNKNOWN;
			sysLogPrintf(LOG_WARNING, "P2P.STUN: discovery failed");
		}
	}

	for (s32 i = 0; i < P2P_STUN_MAX_INFLIGHT; i++) {
		stun_attempt_t *a = &s_Attempts[i];
		if (!a->in_use) continue;

		if (a->awaiting_stun && !s_StunActive) {
			/* The candidate is now available to presence; STUN itself has not
			 * established a peer path, so always advance instead of publishing
			 * an unverified endpoint as P2P_EP_HOLE_PUNCH. */
			p2pInternalReportFailure(a->pair_id, P2P_TIER_STUN,
				reflexiveIsFreshForIce(NULL, NULL)
					? "STUN candidate gathered"
					: "STUN candidate unavailable");
			a->in_use = 0;
			continue;
		}

		if (now_ms >= a->deadline_ms) {
			p2pInternalReportFailure(a->pair_id, P2P_TIER_STUN, "stun timeout");
			a->in_use = 0;
		}
	}
}

void p2pStunCancel(u32 pair_id)
{
	for (s32 i = 0; i < P2P_STUN_MAX_INFLIGHT; i++) {
		if (s_Attempts[i].pair_id == pair_id) {
			memset(&s_Attempts[i], 0, sizeof(s_Attempts[i]));
		}
	}
	for (s32 i = 0; i < P2P_STUN_MAX_INFLIGHT; i++) {
		if (s_Attempts[i].in_use) return;
	}
	p2pIceStunCancel();
	s_StunActive = 0;
}

void p2pStunShutdown(void)
{
	memset(s_Attempts, 0, sizeof(s_Attempts));
	p2pIceStunCancel();
	s_LastStunAtMs = 0;
	s_LastReflexiveIpv4 = 0;
	s_LastReflexivePort = 0;
	s_LastDiscoveryPort = 0;
	s_LastNatType = STUN_NAT_UNKNOWN;
	s_StunActive = 0;
}
