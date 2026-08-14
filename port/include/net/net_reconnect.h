/**
 * net_reconnect.h -- Pure v57 ENet connect-data contract for reconnects.
 *
 * A fresh client continues to send the protocol version as the complete
 * 32-bit ENet connect datum.  A reconnect additionally carries the stable
 * server-assigned client id that owns its preserved slot.  The hint is not an
 * authenticator: CLC_AUTH still has to prove the endpoint-scoped 128-bit
 * cookie before any preserved state can be restored.
 */

#ifndef PD_NET_RECONNECT_H
#define PD_NET_RECONNECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <PR/ultratypes.h>

#define NET_RECONNECT_CONNECT_FLAG 0x80000000u
#define NET_RECONNECT_CLIENT_SHIFT 16u
#define NET_RECONNECT_PROTOCOL_MASK 0x0000ffffu
#define NET_RECONNECT_CLIENT_MASK 0x00ff0000u
#define NET_RECONNECT_RESERVED_MASK 0x7f000000u

typedef enum net_reconnect_connect_status_e {
	NET_RECONNECT_CONNECT_OK = 0,
	NET_RECONNECT_CONNECT_INVALID_ARGUMENT,
	NET_RECONNECT_CONNECT_INVALID_PROTOCOL,
	NET_RECONNECT_CONNECT_INVALID_CLIENT,
	NET_RECONNECT_CONNECT_RESERVED_BITS,
	NET_RECONNECT_CONNECT_PROTOCOL_MISMATCH,
} net_reconnect_connect_status_e;

typedef struct net_reconnect_disconnect_plan_s {
	u32 effective_reason;
	s32 used_server_intent;
} net_reconnect_disconnect_plan_t;

/* Fresh connections remain byte-for-byte compatible with the historical
 * datum. Reconnect ids must be below null_client_id. Returns zero on invalid
 * input; protocol zero is never a valid shipping handshake. */
u32 netReconnectConnectDataEncode(u16 protocol, s32 reconnect,
	u8 client_id, u8 null_client_id);

/* Decode and validate one connect datum without publishing either output on
 * failure. expected_protocol is the exact shipping protocol for this binary. */
net_reconnect_connect_status_e netReconnectConnectDataDecode(u32 data,
	u16 expected_protocol, u8 null_client_id, s32 *out_reconnect,
	u8 *out_client_id);

/* Only an actual transport timeout is retryable. Server shutdown, kick, ban,
 * version/content failure, explicit leave, and unknown reasons terminate the
 * cookie lifecycle. */
s32 netReconnectReasonIsRetryable(u32 reason, u32 timeout_reason);

/* ENet sends disconnect data to the remote peer, but an acknowledged
 * disconnect initiated locally completes with event data zero on the sender.
 * The server therefore owns a separate first-writer intent latch.  A latched
 * server reason wins over the transport observation, including if the
 * transport times out while a terminal kick is already in flight. */
net_reconnect_disconnect_plan_t netReconnectPlanServerDisconnect(
	u32 transport_reason, s32 server_intent_pending,
	u32 server_intent_reason);

/* Reconnect publishes durable authority state, not objects that have already
 * entered terminal teardown. A deleting setup object that can regenerate is
 * retained because its pending transition to GONE owns future gameplay; a
 * non-regenerating delete is authoritative absence and the receiver's exact
 * set reconciliation removes any pristine-stage counterpart. */
s32 netReconnectWorldPropShouldSerialize(s32 pending_delete,
	s32 can_regenerate);

const char *netReconnectConnectStatusString(
	net_reconnect_connect_status_e status);

#ifdef __cplusplus
}
#endif

#endif /* PD_NET_RECONNECT_H */
