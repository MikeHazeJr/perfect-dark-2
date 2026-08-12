#ifndef _IN_NET_MATCH_ROUTE_H
#define _IN_NET_MATCH_ROUTE_H

#include "PR/ultratypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_MATCH_ROUTE_SOURCE_DERIVED 0x01u
#define NET_MATCH_ROUTE_STUN           0x02u
#define NET_MATCH_ROUTE_UPNP           0x04u
#define NET_MATCH_ROUTE_FLAG_MASK      0x07u
#define NET_MATCH_ROUTE_FRESH_SECONDS  90u
#define NET_MATCH_ROUTE_CLOCK_SKEW_SECONDS 5u
#define NET_MATCH_ROUTE_FRESH_MS       (NET_MATCH_ROUTE_FRESH_SECONDS * 1000u)

typedef struct net_match_route_s {
	u32 ipv4;  /* host byte order; zero only on signed source-derived wire route */
	u16 port;
	u8  flags;
	u8  _pad;
	u32 issued_unix_seconds;
} net_match_route_t;

typedef enum net_match_transport_state_e {
	NET_MATCH_TRANSPORT_NONE   = 0,
	NET_MATCH_TRANSPORT_SERVER = 1,
	NET_MATCH_TRANSPORT_CLIENT = 2,
} net_match_transport_state_t;

typedef enum net_match_action_e {
	NET_MATCH_ACTION_WAIT         = 0,
	NET_MATCH_ACTION_START_SERVER = 1,
	NET_MATCH_ACTION_START_CLIENT = 2,
	NET_MATCH_ACTION_ACTIVE       = 3,
	NET_MATCH_ACTION_CONFLICT     = 4,
} net_match_action_t;

/** Parse a dotted IPv4 address into host byte order. */
s32 netMatchRouteParseIpv4(const char *text, u32 *out_ipv4);

/** Validate the signed issue time used by both route updates and clears. */
s32 netMatchRouteTimestampIsFresh(u32 issued_unix_seconds,
		u32 now_unix_seconds);

/**
 * Validate and normalize a signed route. A source-derived route must carry a
 * zero IPv4 on the wire and is resolved only from the verified packet source.
 * STUN/UPnP routes must carry an explicit unicast IPv4. The signed frame's
 * issue time is checked against the caller-provided current Unix time. The
 * age limit is inclusive; a small future skew is also accepted.
 */
s32 netMatchRouteNormalize(const net_match_route_t *wire,
		u32 verified_source_ipv4, u32 now_unix_seconds,
		net_match_route_t *out);

s32 netMatchRouteEqual(const net_match_route_t *a,
		const net_match_route_t *b);

/** Format a normalized route for the existing ENet connection API. */
s32 netMatchRouteFormat(const net_match_route_t *route,
		char *out, size_t out_size);

/**
 * Pure authority transport decision. The caller freezes the elected authority
 * into the latch before either ENet start; later decisions must keep matching
 * that same authority.
 */
net_match_action_t netMatchRoutePlanAction(u32 local_handle,
		u32 authority_handle, u32 latched_authority_handle,
		net_match_transport_state_t transport_state, s32 route_ready);

#ifdef __cplusplus
}
#endif

#endif
