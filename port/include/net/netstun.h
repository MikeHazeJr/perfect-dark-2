/**
 * netstun.h -- STUN client for external address discovery (RFC 5389 subset).
 *
 * ICE uses the transport-neutral codec/resolver helpers with its owned socket.
 * The legacy route-discovery facade fails closed until ENet exposes an owned
 * send/receive demux; it never binds a second socket to the ENet port.
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

#define STUN_BINDING_REQUEST_LEN 20u
#define STUN_TRANSACTION_ID_LEN 12u

/* Initialize STUN state. Call once from netInit(). */
void stunInit(void);

/* Cancel any in-progress discovery and reset state. Call on shutdown. */
void stunShutdown(void);

/* Legacy route-discovery entry point. Returns -1 and FAILED because a numeric
 * port alone is not source provenance; ENet must own the future transaction. */
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

/* Local UDP port used by the discovery that owns the current result.
 * D-003 route publication must match this to the ENet listen port so a later
 * ephemeral P2P probe result cannot be mislabeled as a match-server route. */
u16 stunGetDiscoveryPort(void);

/* Compatibility no-op; there is no detached or joinable route worker. */
void stunCancel(void);

/* Transport-neutral RFC 5389 helpers. The P2P candidate path uses these with
 * its already-bound ICE socket; they never create or bind a socket. */
void stunBindingRequestBuild(const u8 transaction_id[STUN_TRANSACTION_ID_LEN],
		u8 out_request[STUN_BINDING_REQUEST_LEN]);
s32 stunBindingResponseParse(const u8 *packet, s32 packet_len,
		const u8 transaction_id[STUN_TRANSACTION_ID_LEN],
		u32 *out_ipv4, u16 *out_port);
s32 stunResolveServerAt(u32 server_index, u32 *out_ipv4, u16 *out_port);
u32 stunServerCount(void);

#endif /* _IN_NETSTUN_H */
