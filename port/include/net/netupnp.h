#ifndef _IN_NETUPNP_H
#define _IN_NETUPNP_H

#include <PR/ultratypes.h>

/* UPnP status values */
#define UPNP_STATUS_IDLE      0
#define UPNP_STATUS_WORKING   1
#define UPNP_STATUS_SUCCESS   2
#define UPNP_STATUS_FAILED    3

typedef enum net_upnp_owner_e {
	NET_UPNP_OWNER_MATCH_AUTHORITY = 0,
	NET_UPNP_OWNER_SOCIAL = 1,
	NET_UPNP_OWNER_COUNT = 2,
} net_upnp_owner_t;

/* Pure lifecycle events used by the synchronized owner and its tests. */
#define NET_UPNP_LIFECYCLE_BEGIN_SETUP 1
#define NET_UPNP_LIFECYCLE_BEGIN_RENEW 2
#define NET_UPNP_LIFECYCLE_WORKER_SUCCESS 3
#define NET_UPNP_LIFECYCLE_WORKER_FAILURE 4
#define NET_UPNP_LIFECYCLE_TEARDOWN 5

/* Return the next status and whether an existing lease must be preserved.
 * Inline keeps this pure contract available to the self-contained test
 * runner without pulling router I/O into pd-tests. */
static inline s32 netUpnpLifecycleTransition(s32 state, s32 event,
	s32 has_live_lease, s32 *out_state, s32 *out_preserve_old)
{
	if (!out_state || !out_preserve_old ||
		has_live_lease < 0 || has_live_lease > 1) return 0;
	*out_preserve_old = 0;
	switch (event) {
	case NET_UPNP_LIFECYCLE_BEGIN_SETUP:
	case NET_UPNP_LIFECYCLE_BEGIN_RENEW:
		if (state != UPNP_STATUS_IDLE && state != UPNP_STATUS_SUCCESS) return 0;
		*out_state = UPNP_STATUS_WORKING;
		*out_preserve_old = has_live_lease;
		return 1;
	case NET_UPNP_LIFECYCLE_WORKER_SUCCESS:
		if (state != UPNP_STATUS_WORKING) return 0;
		*out_state = UPNP_STATUS_SUCCESS;
		return 1;
	case NET_UPNP_LIFECYCLE_WORKER_FAILURE:
		if (state != UPNP_STATUS_WORKING) return 0;
		*out_state = has_live_lease ? UPNP_STATUS_SUCCESS : UPNP_STATUS_FAILED;
		*out_preserve_old = has_live_lease;
		return 1;
	case NET_UPNP_LIFECYCLE_TEARDOWN:
		*out_state = UPNP_STATUS_IDLE;
		return 1;
	default:
		return 0;
	}
}

/* Start async UPnP port forwarding (returns immediately, runs on thread) */
s32 netUpnpSetup(u16 port);

/* Owner-scoped mapping requests. The live router set is the synchronized
 * union; releasing one owner cannot remove another owner's verified lease. */
s32 netUpnpAcquire(net_upnp_owner_t owner, u16 port);
void netUpnpRelease(net_upnp_owner_t owner);

/* Renew active leases and remove stale mappings when the lease expires. */
void netUpnpTick(void);

/* Remove UPnP port mapping on shutdown */
void netUpnpTeardown(void);

/* Get external/public IP (empty if unavailable) */
const char *netUpnpGetExternalIP(void);

/* Returns non-zero if UPnP mapping is active */
s32 netUpnpIsActive(void);

/* Get current status: IDLE, WORKING, SUCCESS, or FAILED */
s32 netUpnpGetStatus(void);

/* Return the mapped external port for a requested local port, or zero. */
u16 netUpnpGetMappedPort(u16 requested_port);
u16 netUpnpGetOwnedMappedPort(net_upnp_owner_t owner, u16 requested_port);

#endif
