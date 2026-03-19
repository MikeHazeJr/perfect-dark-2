/**
 * netupnp.h -- Automatic UPnP port forwarding for the network server.
 *
 * Call netUpnpSetup(port) when starting a server to automatically
 * forward the port through the router. Call netUpnpTeardown() on shutdown.
 */

#ifndef _IN_NETUPNP_H
#define _IN_NETUPNP_H

#include <PR/ultratypes.h>

/* Attempt UPnP port forwarding. Returns 0 on success, -1 on failure. */
s32 netUpnpSetup(u16 port);

/* Remove UPnP port mapping on shutdown. */
void netUpnpTeardown(void);

/* Get the external/public IP discovered via UPnP (empty if unavailable). */
const char *netUpnpGetExternalIP(void);

/* Returns non-zero if UPnP mapping is active. */
s32 netUpnpIsActive(void);

#endif /* _IN_NETUPNP_H */
