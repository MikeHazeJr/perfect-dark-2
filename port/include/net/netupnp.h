#ifndef _IN_NETUPNP_H
#define _IN_NETUPNP_H

#include <PR/ultratypes.h>

/* UPnP status values */
#define UPNP_STATUS_IDLE      0
#define UPNP_STATUS_WORKING   1
#define UPNP_STATUS_SUCCESS   2
#define UPNP_STATUS_FAILED    3

/* Start async UPnP port forwarding (returns immediately, runs on thread) */
s32 netUpnpSetup(u16 port);

/* Remove UPnP port mapping on shutdown */
void netUpnpTeardown(void);

/* Get external/public IP (empty if unavailable) */
const char *netUpnpGetExternalIP(void);

/* Returns non-zero if UPnP mapping is active */
s32 netUpnpIsActive(void);

/* Get current status: IDLE, WORKING, SUCCESS, or FAILED */
s32 netUpnpGetStatus(void);

#endif
