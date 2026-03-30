/**
 * netstun.h -- STUN client for external address discovery (RFC 5389 subset).
 *
 * Runs on a background thread. Results are polled via stunGetStatus().
 * The server calls stunDiscoverAsync() when it starts hosting to discover
 * the external (NAT-facing) IP and port of the server's UDP socket.
 *
 * Thread pattern mirrors netupnp.c / upnpWorkerThread.
 */

#ifndef _IN_NETSTUN_H
#define _IN_NETSTUN_H

#include <PR/ultratypes.h>

#define STUN_STATUS_IDLE    0
#define STUN_STATUS_WORKING 1
#define STUN_STATUS_SUCCESS 2
#define STUN_STATUS_FAILED  3

#define STUN_NAT_UNKNOWN   0
#define STUN_NAT_CONE      1  /* hole punch viable */
#define STUN_NAT_SYMMETRIC 2  /* different external port per destination — hole punch will fail */

/* Initialize STUN state. Call once from netInit(). */
void stunInit(void);

/* Cancel any in-progress discovery and reset state. Call on shutdown. */
void stunShutdown(void);

/* Start async STUN discovery on the given local port.
   Returns immediately. Poll stunGetStatus() to check progress.
   On-demand only: call this when the server begins hosting, not at app launch. */
s32 stunDiscoverAsync(u16 localport);

/* Current state: STUN_STATUS_IDLE / WORKING / SUCCESS / FAILED */
s32 stunGetStatus(void);

/* NAT type detected during discovery.
   Valid after SUCCESS (or after 2 probes complete). */
s32 stunGetNatType(void);

/* Discovered external IP string (e.g. "203.0.113.5"). Empty string if not ready. */
const char *stunGetExternalIP(void);

/* Discovered external port in host byte order. 0 if not ready. */
u16 stunGetExternalPort(void);

/* Signal the worker thread to stop at its next cancel-check.
   Thread is detached; it will exit on its own. */
void stunCancel(void);

#endif /* _IN_NETSTUN_H */
