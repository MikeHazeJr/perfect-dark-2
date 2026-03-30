/**
 * netholepunch.h -- UDP hole punch for NAT traversal.
 *
 * Implements a connection waterfall for clients behind NAT:
 *   Phase 1 (DIRECT):     Direct ENet connect (3s timeout)
 *   Phase 2 (PUNCH):      Send PUNCH_REQ, wait up to 500ms for PUNCH_ACK
 *   Phase 3 (PUNCH_ENET): Re-attempt ENet connect after hole punch (5s timeout)
 *
 * The server receives PUNCH_REQ and replies with 3 PUNCH_ACK packets at
 * 50ms intervals to punch a hole in the client's NAT.
 *
 * State machine is driven from netStartFrame() — no extra threads.
 * Integrate by calling netStartClientWithHolePunch() instead of netStartClient()
 * and wiring the three hook points in netStartFrame().
 *
 * Phase 4 (NAT diagnostics): uses STUN NAT type from netstun.h to provide
 * targeted error messages when hole punching fails.
 */

#ifndef _IN_NETHOLEPUNCH_H
#define _IN_NETHOLEPUNCH_H

#include <PR/ultratypes.h>

/* Connection waterfall phases */
#define CONN_PHASE_IDLE       0   /* no waterfall active */
#define CONN_PHASE_DIRECT     1   /* waiting for direct ENet connect */
#define CONN_PHASE_PUNCH      2   /* PUNCH_REQ sent, waiting for PUNCH_ACK */
#define CONN_PHASE_PUNCH_ENET 3   /* re-attempting ENet after hole punch */
#define CONN_PHASE_DONE       4   /* terminal: success or failure */

/* Wire protocol magic bytes (5 bytes each) */
#define PUNCH_REQ_MAGIC "PDPH\x01"
#define PUNCH_ACK_MAGIC "PDPH\x02"
#define PUNCH_MAGIC_LEN 5

/* Wire sizes (bytes):
 *  PUNCH_REQ: 5 magic + 4 ext_ip + 2 ext_port + 4 proto_ver = 15
 *  PUNCH_ACK: 5 magic + 4 srv_lan_ip + 2 srv_port + 1 nat_viable = 12
 */
#define PUNCH_REQ_LEN 15
#define PUNCH_ACK_LEN 12

/* Timeout constants (milliseconds) */
#define PUNCH_DIRECT_TIMEOUT_MS 3000
#define PUNCH_ACK_TIMEOUT_MS     500
#define PUNCH_ENET_TIMEOUT_MS   5000

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations for ENet types */
struct _ENetHost;
struct _ENetAddress;

/* -------------------------------------------------------------------------
 * Server-side API
 * ---------------------------------------------------------------------- */

/**
 * Handle an incoming PUNCH_REQ connectionless packet.
 * Call from netServerConnectionlessPacket() when "PDPH\x01" magic is detected.
 *
 * host    : the ENet host — its socket is used to send PUNCH_ACK replies.
 * addr    : socket-level source address of the datagram (authoritative external
 *           address of the client — use this, not the payload field).
 * rxdata  : raw datagram bytes (including the 5-byte magic prefix).
 * rxlen   : total datagram length.
 *
 * Sends 3 PUNCH_ACK packets to addr at 50ms intervals to open the client's NAT.
 */
void netServerHandlePunchReq(struct _ENetHost *host, const struct _ENetAddress *addr,
                              const u8 *rxdata, s32 rxlen);

/* -------------------------------------------------------------------------
 * Client-side API
 * ---------------------------------------------------------------------- */

/**
 * Start the connection waterfall to addr.
 * Replaces a direct netStartClient() call when hole punching may be needed.
 * Internally calls netStartClient(), sets a 3s peer timeout, then arms the
 * waterfall state machine.
 *
 * Returns same codes as netStartClient(): 0 on success, negative on error.
 */
s32 netStartClientWithHolePunch(const char *addr);

/**
 * ENet intercept callback — install on the client host to catch PUNCH_ACK.
 * Signature matches ENetInterceptCallback (see enet.h).
 * Returns 1 if the packet was a PUNCH_ACK (consumed), 0 otherwise.
 */
s32 netHolePunchClientIntercept(struct _ENetEvent *event, struct _ENetAddress *addr,
                                 u8 *rxdata, s32 rxlen);

/**
 * Called from netStartFrame() when ENET_EVENT_TYPE_DISCONNECT_TIMEOUT fires
 * on a client. Decides whether to intercept (start punch phase) or let normal
 * disconnect handling proceed.
 *
 * host : the ENet host (used to send PUNCH_REQ on the same socket).
 * Returns 1 if the event was consumed by the waterfall (skip netClientEvDisconnect).
 * Returns 0 if normal disconnect handling should proceed.
 */
s32 netHolePunchHandleTimeout(struct _ENetHost *host);

/**
 * Called from netStartFrame() when ENET_EVENT_TYPE_CONNECT fires on a client.
 * Marks the waterfall as complete (CONN_PHASE_DONE).
 */
void netHolePunchOnConnect(void);

/**
 * Tick the hole punch state machine once per frame.
 * Call from netStartFrame() when g_NetMode == NETMODE_CLIENT, before the
 * ENet event loop.
 *
 * host : the ENet host (used to reconnect after hole punch).
 */
void netHolePunchClientTick(struct _ENetHost *host);

/**
 * Current waterfall phase (CONN_PHASE_*).
 * Used by UI to distinguish "Connecting..." from "Trying NAT traversal...".
 */
s32 netGetConnectionPhase(void);

/**
 * Error message describing why the hole punch failed (Phase 4 diagnostics).
 * Returns an empty string if the connection succeeded or is still in progress.
 */
const char *netHolePunchGetError(void);

/**
 * Reset all hole punch state. Called from netDisconnect().
 */
void netHolePunchReset(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_NETHOLEPUNCH_H */
