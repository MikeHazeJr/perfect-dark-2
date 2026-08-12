#include "catch.hpp"

extern "C" {
#include "net/net_match_route.h"
}

#include <cstring>

TEST_CASE("match route accepts only one typed server-route source",
		"[networking][d-003][b1058]")
{
	const u32 now = 1700000000u;
	net_match_route_t route{};
	net_match_route_t normalized{};

	route.port = 27100;
	route.flags = NET_MATCH_ROUTE_SOURCE_DERIVED;
	route.issued_unix_seconds = now;
	REQUIRE(netMatchRouteNormalize(&route, 0x7f000001u, now,
		&normalized) == 1);
	REQUIRE(normalized.ipv4 == 0x7f000001u);
	REQUIRE(normalized.port == 27100);
	REQUIRE(normalized.issued_unix_seconds == now);
	REQUIRE(normalized._pad == 0);
	route._pad = 0xa5;
	REQUIRE(netMatchRouteNormalize(&route, 0x7f000001u, now,
		&normalized) == 0);
	route._pad = 0;

	route.ipv4 = 0x08080808u;
	REQUIRE(netMatchRouteNormalize(&route, 0x7f000001u, now,
		&normalized) == 0);

	route.flags = NET_MATCH_ROUTE_STUN;
	REQUIRE(netMatchRouteNormalize(&route, 0x7f000001u, now,
		&normalized) == 1);
	route.flags = NET_MATCH_ROUTE_UPNP;
	REQUIRE(netMatchRouteNormalize(&route, 0x7f000001u, now,
		&normalized) == 1);
	route.flags = NET_MATCH_ROUTE_STUN | NET_MATCH_ROUTE_UPNP;
	REQUIRE(netMatchRouteNormalize(&route, 0x7f000001u, now,
		&normalized) == 0);
	route.flags = 0x80u;
	REQUIRE(netMatchRouteNormalize(&route, 0x7f000001u, now,
		&normalized) == 0);
}

TEST_CASE("match route enforces issue-time freshness and clock skew",
		"[networking][d-003][b1058]")
{
	const u32 now = 1700000000u;
	net_match_route_t route{};
	net_match_route_t normalized{};
	route.ipv4 = 0xcb007107u;
	route.port = 27100;
	route.flags = NET_MATCH_ROUTE_STUN;

	route.issued_unix_seconds = 0;
	REQUIRE(netMatchRouteTimestampIsFresh(0, now) == 0);
	REQUIRE(netMatchRouteNormalize(&route, 0, now, &normalized) == 0);

	route.issued_unix_seconds = now - NET_MATCH_ROUTE_FRESH_SECONDS;
	REQUIRE(netMatchRouteTimestampIsFresh(route.issued_unix_seconds, now) == 1);
	REQUIRE(netMatchRouteNormalize(&route, 0, now, &normalized) == 1);

	route.issued_unix_seconds = now - NET_MATCH_ROUTE_FRESH_SECONDS - 1;
	REQUIRE(netMatchRouteTimestampIsFresh(route.issued_unix_seconds, now) == 0);
	REQUIRE(netMatchRouteNormalize(&route, 0, now, &normalized) == 0);

	route.issued_unix_seconds = now + NET_MATCH_ROUTE_CLOCK_SKEW_SECONDS;
	REQUIRE(netMatchRouteTimestampIsFresh(route.issued_unix_seconds, now) == 1);
	REQUIRE(netMatchRouteNormalize(&route, 0, now, &normalized) == 1);

	route.issued_unix_seconds = now + NET_MATCH_ROUTE_CLOCK_SKEW_SECONDS + 1;
	REQUIRE(netMatchRouteTimestampIsFresh(route.issued_unix_seconds, now) == 0);
	REQUIRE(netMatchRouteNormalize(&route, 0, now, &normalized) == 0);
}

TEST_CASE("match route rejects unusable addresses and formats normalized routes",
		"[networking][d-003][b1058]")
{
	u32 ipv4 = 0;
	REQUIRE(netMatchRouteParseIpv4("203.0.113.7", &ipv4) == 1);
	REQUIRE(ipv4 == 0xcb007107u);
	REQUIRE(netMatchRouteParseIpv4("203.0.113.256", &ipv4) == 0);
	REQUIRE(netMatchRouteParseIpv4("203.0.113.7:27100", &ipv4) == 0);

	net_match_route_t route{};
	route.ipv4 = 0xcb007107u;
	route.port = 27100;
	route.flags = NET_MATCH_ROUTE_STUN;
	route.issued_unix_seconds = 1700000000u;
	net_match_route_t normalized{};
	REQUIRE(netMatchRouteNormalize(&route, 0, 1700000000u,
		&normalized) == 1);
	char text[64]{};
	REQUIRE(netMatchRouteFormat(&normalized, text, sizeof(text)) == 1);
	REQUIRE(std::strcmp(text, "203.0.113.7:27100") == 0);

	route.ipv4 = 0xe0000001u;
	REQUIRE(netMatchRouteNormalize(&route, 0, 1700000000u,
		&normalized) == 0);
	route.ipv4 = 0xffffffffu;
	REQUIRE(netMatchRouteNormalize(&route, 0, 1700000000u,
		&normalized) == 0);
	route.ipv4 = 0xcb007107u;
	route.port = 0;
	REQUIRE(netMatchRouteNormalize(&route, 0, 1700000000u,
		&normalized) == 0);
}

TEST_CASE("authority planner starts exactly one transport and latches its owner",
		"[networking][d-003][b1058]")
{
	REQUIRE(netMatchRoutePlanAction(10, 0, 0,
		NET_MATCH_TRANSPORT_NONE, 0) == NET_MATCH_ACTION_WAIT);
	REQUIRE(netMatchRoutePlanAction(10, 10, 0,
		NET_MATCH_TRANSPORT_NONE, 0) == NET_MATCH_ACTION_START_SERVER);
	REQUIRE(netMatchRoutePlanAction(10, 10, 10,
		NET_MATCH_TRANSPORT_NONE, 0) == NET_MATCH_ACTION_START_SERVER);
	REQUIRE(netMatchRoutePlanAction(10, 10, 10,
		NET_MATCH_TRANSPORT_SERVER, 0) == NET_MATCH_ACTION_ACTIVE);
	REQUIRE(netMatchRoutePlanAction(10, 20, 0,
		NET_MATCH_TRANSPORT_NONE, 0) == NET_MATCH_ACTION_WAIT);
	REQUIRE(netMatchRoutePlanAction(10, 20, 0,
		NET_MATCH_TRANSPORT_NONE, 1) == NET_MATCH_ACTION_START_CLIENT);
	REQUIRE(netMatchRoutePlanAction(10, 20, 20,
		NET_MATCH_TRANSPORT_NONE, 1) == NET_MATCH_ACTION_START_CLIENT);
	REQUIRE(netMatchRoutePlanAction(10, 20, 10,
		NET_MATCH_TRANSPORT_NONE, 1) == NET_MATCH_ACTION_CONFLICT);
	REQUIRE(netMatchRoutePlanAction(10, 20, 20,
		NET_MATCH_TRANSPORT_CLIENT, 1) == NET_MATCH_ACTION_ACTIVE);
	REQUIRE(netMatchRoutePlanAction(10, 20, 30,
		NET_MATCH_TRANSPORT_CLIENT, 1) == NET_MATCH_ACTION_CONFLICT);
	REQUIRE(netMatchRoutePlanAction(10, 20, 20,
		NET_MATCH_TRANSPORT_SERVER, 1) == NET_MATCH_ACTION_CONFLICT);
}
