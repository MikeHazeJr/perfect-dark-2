#include "net/net_match_route.h"

#include <stdio.h>
#include <string.h>

static s32 isExplicitUnicastIpv4(u32 ipv4)
{
	const u8 first = (u8)(ipv4 >> 24);
	if (ipv4 == 0 || ipv4 == 0xffffffffu) return 0;
	if (first >= 224u) return 0;
	return 1;
}

s32 netMatchRouteParseIpv4(const char *text, u32 *out_ipv4)
{
	unsigned a = 0;
	unsigned b = 0;
	unsigned c = 0;
	unsigned d = 0;
	char trailing = '\0';

	if (!text || !text[0] || !out_ipv4) return 0;
	if (sscanf(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &trailing) != 4) {
		return 0;
	}
	if (a > 255u || b > 255u || c > 255u || d > 255u) return 0;
	*out_ipv4 = ((u32)a << 24) | ((u32)b << 16) | ((u32)c << 8) | (u32)d;
	return 1;
}

s32 netMatchRouteTimestampIsFresh(u32 issued_unix_seconds,
		u32 now_unix_seconds)
{
	if (issued_unix_seconds == 0) return 0;
	if (issued_unix_seconds > now_unix_seconds) {
		return issued_unix_seconds - now_unix_seconds <=
			NET_MATCH_ROUTE_CLOCK_SKEW_SECONDS;
	}
	return now_unix_seconds - issued_unix_seconds <=
		NET_MATCH_ROUTE_FRESH_SECONDS;
}

s32 netMatchRouteNormalize(const net_match_route_t *wire,
		u32 verified_source_ipv4, u32 now_unix_seconds,
		net_match_route_t *out)
{
	if (!wire || !out || wire->port == 0 || wire->_pad != 0 ||
		!netMatchRouteTimestampIsFresh(wire->issued_unix_seconds,
			now_unix_seconds)) return 0;
	if ((wire->flags & ~NET_MATCH_ROUTE_FLAG_MASK) != 0) return 0;
	if (wire->flags != NET_MATCH_ROUTE_SOURCE_DERIVED &&
		wire->flags != NET_MATCH_ROUTE_STUN &&
		wire->flags != NET_MATCH_ROUTE_UPNP) {
		return 0;
	}

	net_match_route_t normalized = *wire;
	normalized._pad = 0;
	if (wire->flags == NET_MATCH_ROUTE_SOURCE_DERIVED) {
		if (wire->ipv4 != 0 || !isExplicitUnicastIpv4(verified_source_ipv4)) {
			return 0;
		}
		normalized.ipv4 = verified_source_ipv4;
	} else if (!isExplicitUnicastIpv4(wire->ipv4)) {
		return 0;
	}

	*out = normalized;
	return 1;
}

s32 netMatchRouteEqual(const net_match_route_t *a,
		const net_match_route_t *b)
{
	return a && b && a->ipv4 == b->ipv4 && a->port == b->port &&
		a->flags == b->flags;
}

s32 netMatchRouteFormat(const net_match_route_t *route,
		char *out, size_t out_size)
{
	if (!route || !out || out_size == 0 || route->port == 0 ||
		!isExplicitUnicastIpv4(route->ipv4)) {
		return 0;
	}
	const int written = snprintf(out, out_size, "%u.%u.%u.%u:%u",
		(unsigned)((route->ipv4 >> 24) & 0xffu),
		(unsigned)((route->ipv4 >> 16) & 0xffu),
		(unsigned)((route->ipv4 >> 8) & 0xffu),
		(unsigned)(route->ipv4 & 0xffu), (unsigned)route->port);
	return written > 0 && (size_t)written < out_size;
}

net_match_action_t netMatchRoutePlanAction(u32 local_handle,
		u32 authority_handle, u32 latched_authority_handle,
		net_match_transport_state_t transport_state, s32 route_ready)
{
	if (local_handle == 0 || authority_handle == 0) {
		return NET_MATCH_ACTION_WAIT;
	}
	if (latched_authority_handle != 0 &&
		latched_authority_handle != authority_handle) {
		return NET_MATCH_ACTION_CONFLICT;
	}

	if (local_handle == authority_handle) {
		if (transport_state == NET_MATCH_TRANSPORT_NONE) {
			return NET_MATCH_ACTION_START_SERVER;
		}
		return transport_state == NET_MATCH_TRANSPORT_SERVER
			? NET_MATCH_ACTION_ACTIVE : NET_MATCH_ACTION_CONFLICT;
	}

	if (transport_state == NET_MATCH_TRANSPORT_NONE) {
		return route_ready ? NET_MATCH_ACTION_START_CLIENT
			: NET_MATCH_ACTION_WAIT;
	}
	return transport_state == NET_MATCH_TRANSPORT_CLIENT &&
		latched_authority_handle == authority_handle
		? NET_MATCH_ACTION_ACTIVE : NET_MATCH_ACTION_CONFLICT;
}
