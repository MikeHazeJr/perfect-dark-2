/**
 * p2p_upnp.c -- Tier 3: UPnP mapping lifecycle handoff.
 *
 * UPnP only creates candidate provenance. It never reports a peer path open;
 * ICE remains responsible for a signed candidate exchange and connectivity
 * check. netupnp.c owns the single bounded mapping set and its lease cycle.
 */

#include "net/p2p.h"
#include "net/netupnp.h"
#include "system.h"

#include <SDL.h>
#include <stddef.h>
#include <string.h>

#define P2P_UPNP_MAX_INFLIGHT 16

typedef struct {
	u32 pair_id;
	u32 deadline_ms;
	u8  in_use;
	u8  awaiting_upnp;
} upnp_attempt_t;

static upnp_attempt_t s_Attempts[P2P_UPNP_MAX_INFLIGHT];
static s32 s_UpnpKicked;

static upnp_attempt_t *findFreeSlot(void)
{
	for (s32 i = 0; i < P2P_UPNP_MAX_INFLIGHT; i++) {
		if (!s_Attempts[i].in_use) return &s_Attempts[i];
	}
	return NULL;
}

static upnp_attempt_t *findByPair(u32 pair_id)
{
	for (s32 i = 0; i < P2P_UPNP_MAX_INFLIGHT; i++) {
		if (s_Attempts[i].in_use && s_Attempts[i].pair_id == pair_id) {
			return &s_Attempts[i];
		}
	}
	return NULL;
}

static s32 mappingSetReady(void)
{
	u32 ice_ipv4 = 0;
	u16 ice_port = 0;
	if (!p2pIceGetLocalHostCandidate(&ice_ipv4, &ice_port)) return 0;
	return netUpnpIsActive() && netUpnpGetStatus() == UPNP_STATUS_SUCCESS &&
		netUpnpGetOwnedMappedPort(NET_UPNP_OWNER_SOCIAL, ice_port) != 0;
}

s32 p2pUpnpStart(u32 pair_id)
{
	upnp_attempt_t *a = findByPair(pair_id);
	if (!a) a = findFreeSlot();
	if (!a) {
		p2pInternalReportFailure(pair_id, P2P_TIER_UPNP, "table full");
		return -1;
	}

	a->in_use = 1;
	a->pair_id = pair_id;
	a->deadline_ms = SDL_GetTicks() + P2P_TIER_TIMEOUT_MS;
	a->awaiting_upnp = 1;

	if (!s_UpnpKicked || !netUpnpIsActive()) {
		u32 ice_ipv4 = 0;
		u16 ice_port = 0;
		if (!p2pIceGetLocalHostCandidate(&ice_ipv4, &ice_port) ||
			netUpnpAcquire(NET_UPNP_OWNER_SOCIAL, ice_port) != 0) {
			p2pInternalReportFailure(pair_id, P2P_TIER_UPNP,
				"mapping start failed");
			a->in_use = 0;
			return -1;
		}
		s_UpnpKicked = 1;
		sysLogPrintf(LOG_NOTE,
			"P2P.UPNP: social owner requested bound ICE port %u only",
			(unsigned)ice_port);
	}

	if (mappingSetReady()) {
		/* Mapping is only candidate provenance. The peer candidate set and
		 * ICE probe must still establish the path. */
		p2pInternalReportFailure(pair_id, P2P_TIER_UPNP,
			"candidate mappings ready; continue to ICE");
		a->in_use = 0;
	}
	return 0;
}

void p2pUpnpPoll(void)
{
	netUpnpTick();
	const u32 now_ms = SDL_GetTicks();
	const s32 status = netUpnpGetStatus();

	for (s32 i = 0; i < P2P_UPNP_MAX_INFLIGHT; i++) {
		upnp_attempt_t *a = &s_Attempts[i];
		if (!a->in_use) continue;

		if (a->awaiting_upnp && status == UPNP_STATUS_SUCCESS &&
			mappingSetReady()) {
			p2pInternalReportFailure(a->pair_id, P2P_TIER_UPNP,
				"candidate mappings ready; continue to ICE");
			a->in_use = 0;
			continue;
		}

		if (status == UPNP_STATUS_FAILED) {
			p2pInternalReportFailure(a->pair_id, P2P_TIER_UPNP,
				"router rejected required mappings");
			a->in_use = 0;
			continue;
		}

		if (now_ms >= a->deadline_ms) {
			p2pInternalReportFailure(a->pair_id, P2P_TIER_UPNP, "upnp timeout");
			a->in_use = 0;
		}
	}
}

void p2pUpnpCancel(u32 pair_id)
{
	for (s32 i = 0; i < P2P_UPNP_MAX_INFLIGHT; i++) {
		if (s_Attempts[i].pair_id == pair_id) {
			memset(&s_Attempts[i], 0, sizeof(s_Attempts[i]));
		}
	}
}

void p2pUpnpShutdown(void)
{
	memset(s_Attempts, 0, sizeof(s_Attempts));
	netUpnpRelease(NET_UPNP_OWNER_SOCIAL);
	s_UpnpKicked = 0;
}
