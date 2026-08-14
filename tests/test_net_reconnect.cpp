#include "catch.hpp"

extern "C" {
#include "net/net_reconnect.h"
}

TEST_CASE("reconnect connect data preserves fresh protocol and stable slot hint",
		"[net][reconnect][b1064]")
{
	constexpr u16 protocol = 57;
	constexpr u8 null_client = 0xff;
	s32 reconnect = -1;
	u8 client = 0;

	const u32 fresh = netReconnectConnectDataEncode(protocol, 0, 0,
		null_client);
	REQUIRE(fresh == protocol);
	REQUIRE(netReconnectConnectDataDecode(fresh, protocol, null_client,
		&reconnect, &client) == NET_RECONNECT_CONNECT_OK);
	REQUIRE(reconnect == 0);
	REQUIRE(client == null_client);

	const u32 retry = netReconnectConnectDataEncode(protocol, 1, 7,
		null_client);
	REQUIRE((retry & NET_RECONNECT_CONNECT_FLAG) != 0);
	REQUIRE(netReconnectConnectDataDecode(retry, protocol, null_client,
		&reconnect, &client) == NET_RECONNECT_CONNECT_OK);
	REQUIRE(reconnect == 1);
	REQUIRE(client == 7);
}

TEST_CASE("reconnect connect data rejects malformed hints without output mutation",
		"[net][reconnect][b1064]")
{
	constexpr u16 protocol = 57;
	constexpr u8 null_client = 0xff;
	s32 reconnect = 9;
	u8 client = 9;

	REQUIRE(netReconnectConnectDataEncode(0, 0, 0, null_client) == 0);
	REQUIRE(netReconnectConnectDataEncode(protocol, 1, null_client,
		null_client) == 0);
	REQUIRE(netReconnectConnectDataDecode(protocol + 1, protocol,
		null_client, &reconnect, &client)
		== NET_RECONNECT_CONNECT_PROTOCOL_MISMATCH);
	REQUIRE(reconnect == 9);
	REQUIRE(client == 9);
	REQUIRE(netReconnectConnectDataDecode(
		NET_RECONNECT_CONNECT_FLAG | NET_RECONNECT_RESERVED_MASK | protocol,
		protocol, null_client, &reconnect, &client)
		== NET_RECONNECT_CONNECT_RESERVED_BITS);
	REQUIRE(reconnect == 9);
	REQUIRE(client == 9);
	REQUIRE(netReconnectConnectDataDecode(
		NET_RECONNECT_CONNECT_FLAG | ((u32)null_client <<
			NET_RECONNECT_CLIENT_SHIFT) | protocol,
		protocol, null_client, &reconnect, &client)
		== NET_RECONNECT_CONNECT_INVALID_CLIENT);
}

TEST_CASE("only timeout disconnects retain reconnect authority",
		"[net][reconnect][b1064]")
{
	constexpr u32 timeout = 5;
	REQUIRE(netReconnectReasonIsRetryable(timeout, timeout) == 1);
	REQUIRE(netReconnectReasonIsRetryable(0, timeout) == 0);
	REQUIRE(netReconnectReasonIsRetryable(1, timeout) == 0);
	REQUIRE(netReconnectReasonIsRetryable(3, timeout) == 0);
	REQUIRE(netReconnectReasonIsRetryable(8, timeout) == 0);
}

TEST_CASE("server disconnect intent survives sender-local ENet reason loss",
		"[net][reconnect][b1064][b1092]")
{
	constexpr u32 unknown = 0;
	constexpr u32 shutdown = 1;
	constexpr u32 banned = 4;
	constexpr u32 timeout = 5;

	const net_reconnect_disconnect_plan_t remote_timeout =
		netReconnectPlanServerDisconnect(timeout, 0, unknown);
	REQUIRE(remote_timeout.effective_reason == timeout);
	REQUIRE(remote_timeout.used_server_intent == 0);

	const net_reconnect_disconnect_plan_t local_timeout =
		netReconnectPlanServerDisconnect(unknown, 1, timeout);
	REQUIRE(local_timeout.effective_reason == timeout);
	REQUIRE(local_timeout.used_server_intent == 1);

	/* A terminal first-writer intent remains terminal even if transport loss
	 * races the disconnect acknowledgement. */
	const net_reconnect_disconnect_plan_t terminal_race =
		netReconnectPlanServerDisconnect(timeout, 1, banned);
	REQUIRE(terminal_race.effective_reason == banned);
	REQUIRE(netReconnectReasonIsRetryable(terminal_race.effective_reason,
		timeout) == 0);

	const net_reconnect_disconnect_plan_t remote_shutdown =
		netReconnectPlanServerDisconnect(shutdown, 0, timeout);
	REQUIRE(remote_shutdown.effective_reason == shutdown);
}

TEST_CASE("reconnect world snapshots publish durable lifecycle state",
		"[net][reconnect][b1064][b1096]")
{
	REQUIRE(netReconnectWorldPropShouldSerialize(0, 0) == 1);
	REQUIRE(netReconnectWorldPropShouldSerialize(0, 1) == 1);
	/* A terminal deletion is authoritative absence, not a transient object row. */
	REQUIRE(netReconnectWorldPropShouldSerialize(1, 0) == 0);
	/* Regenerating setup objects retain their pending GONE transition. */
	REQUIRE(netReconnectWorldPropShouldSerialize(1, 1) == 1);
}
