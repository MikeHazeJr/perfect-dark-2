/**
 * p2p_upnp.c -- Tier 3: UPnP / NAT-PMP port mapping.
 *
 * Wraps the existing port/src/net/netupnp.c worker thread. On tier-3
 * activation we ensure a port mapping for our local p2p socket port is
 * present at the local IGD. The peer can then reach us at
 * (external_ip, mapped_port). For Phase 1 the orchestrator publishes the
 * resulting endpoint as the candidate -- the actual probe still uses the
 * tier-1 direct-UDP packet format.
 *
 * UPnP is asymmetric: it only opens our side of the NAT. If the peer is
 * also behind NAT and has not mapped, this tier still helps because the
 * peer can initiate the punch toward our public endpoint.
 */

#include "net/p2p.h"
#include "net/netupnp.h"
#include "net/net.h"
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

static u32 parseIpv4(const char *s)
{
	if (!s || !*s) return 0;
	struct in_addr addr;
	if (inet_pton(AF_INET, s, &addr) != 1) return 0;
	return ntohl(addr.s_addr);
}

s32 p2pUpnpStart(u32 pair_id)
{
	upnp_attempt_t *a = findFreeSlot();
	if (!a) {
		p2pInternalReportFailure(pair_id, P2P_TIER_UPNP, "table full");
		return -1;
	}

	a->in_use = 1;
	a->pair_id = pair_id;
	a->deadline_ms = SDL_GetTicks() + P2P_TIER_TIMEOUT_MS;
	a->awaiting_upnp = 1;

	if (!s_UpnpKicked) {
		netUpnpSetup(g_NetServerPort ? (u16)g_NetServerPort : (u16)NET_DEFAULT_PORT);
		s_UpnpKicked = 1;
		sysLogPrintf(LOG_NOTE, "P2P.UPNP: kicked async port mapping");
	}

	if (netUpnpGetStatus() == UPNP_STATUS_SUCCESS) {
		const u32 ipv4 = parseIpv4(netUpnpGetExternalIP());
		const u16 port = g_NetServerPort ? (u16)g_NetServerPort : (u16)NET_DEFAULT_PORT;
		if (ipv4 != 0) {
			p2p_endpoint_t ep;
			memset(&ep, 0, sizeof(ep));
			ep.ipv4 = ipv4;
			ep.port = port;
			ep.flags = P2P_EP_PORT_MAPPED;
			p2pInternalReportSuccess(pair_id, P2P_TIER_UPNP, &ep);
			a->in_use = 0;
			return 0;
		}
	}

	return 0;
}

void p2pUpnpPoll(void)
{
	const u32 now_ms = SDL_GetTicks();
	const s32 status = netUpnpGetStatus();
	const u32 ipv4   = (status == UPNP_STATUS_SUCCESS)
	                     ? parseIpv4(netUpnpGetExternalIP()) : 0;

	for (s32 i = 0; i < P2P_UPNP_MAX_INFLIGHT; i++) {
		upnp_attempt_t *a = &s_Attempts[i];
		if (!a->in_use) continue;

		if (a->awaiting_upnp && status == UPNP_STATUS_SUCCESS && ipv4 != 0) {
			p2p_endpoint_t ep;
			memset(&ep, 0, sizeof(ep));
			ep.ipv4 = ipv4;
			ep.port = g_NetServerPort ? (u16)g_NetServerPort : (u16)NET_DEFAULT_PORT;
			ep.flags = P2P_EP_PORT_MAPPED;
			p2pInternalReportSuccess(a->pair_id, P2P_TIER_UPNP, &ep);
			a->in_use = 0;
			continue;
		}

		if (status == UPNP_STATUS_FAILED) {
			p2pInternalReportFailure(a->pair_id, P2P_TIER_UPNP, "router rejected mapping");
			a->in_use = 0;
			continue;
		}

		if (now_ms >= a->deadline_ms) {
			p2pInternalReportFailure(a->pair_id, P2P_TIER_UPNP, "upnp timeout");
			a->in_use = 0;
		}
	}
}
