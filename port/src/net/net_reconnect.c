#include "net/net_reconnect.h"

u32 netReconnectConnectDataEncode(u16 protocol, s32 reconnect,
		u8 client_id, u8 null_client_id)
{
	if (protocol == 0) {
		return 0;
	}
	if (!reconnect) {
		return (u32)protocol;
	}
	if (client_id >= null_client_id) {
		return 0;
	}
	return NET_RECONNECT_CONNECT_FLAG
		| ((u32)client_id << NET_RECONNECT_CLIENT_SHIFT)
		| (u32)protocol;
}

net_reconnect_connect_status_e netReconnectConnectDataDecode(u32 data,
		u16 expected_protocol, u8 null_client_id, s32 *out_reconnect,
		u8 *out_client_id)
{
	s32 reconnect;
	u8 client_id;
	u16 protocol;

	if (!out_reconnect || !out_client_id || expected_protocol == 0) {
		return NET_RECONNECT_CONNECT_INVALID_ARGUMENT;
	}
	if (data & NET_RECONNECT_RESERVED_MASK) {
		return NET_RECONNECT_CONNECT_RESERVED_BITS;
	}
	protocol = (u16)(data & NET_RECONNECT_PROTOCOL_MASK);
	if (protocol == 0) {
		return NET_RECONNECT_CONNECT_INVALID_PROTOCOL;
	}
	if (protocol != expected_protocol) {
		return NET_RECONNECT_CONNECT_PROTOCOL_MISMATCH;
	}
	reconnect = (data & NET_RECONNECT_CONNECT_FLAG) != 0;
	client_id = (u8)((data & NET_RECONNECT_CLIENT_MASK)
		>> NET_RECONNECT_CLIENT_SHIFT);
	if (!reconnect && client_id != 0) {
		return NET_RECONNECT_CONNECT_RESERVED_BITS;
	}
	if (reconnect && client_id >= null_client_id) {
		return NET_RECONNECT_CONNECT_INVALID_CLIENT;
	}
	*out_reconnect = reconnect;
	*out_client_id = reconnect ? client_id : null_client_id;
	return NET_RECONNECT_CONNECT_OK;
}

s32 netReconnectReasonIsRetryable(u32 reason, u32 timeout_reason)
{
	return reason == timeout_reason;
}

net_reconnect_disconnect_plan_t netReconnectPlanServerDisconnect(
		u32 transport_reason, s32 server_intent_pending,
		u32 server_intent_reason)
{
	net_reconnect_disconnect_plan_t plan;

	plan.used_server_intent = server_intent_pending != 0;
	plan.effective_reason = plan.used_server_intent
		? server_intent_reason : transport_reason;
	return plan;
}

net_reconnect_player_state_action_e netReconnectPlanPlayerState(
		s32 local_dead, s32 authoritative_dead, s32 applying_snapshot)
{
	const s32 was_dead = local_dead != 0;
	const s32 is_dead = authoritative_dead != 0;

	if (was_dead == is_dead) {
		return NET_RECONNECT_PLAYER_STATE_NONE;
	}
	if (!is_dead) {
		return NET_RECONNECT_PLAYER_STATE_START_NEW_LIFE;
	}
	return applying_snapshot
		? NET_RECONNECT_PLAYER_STATE_SNAPSHOT_DEATH
		: NET_RECONNECT_PLAYER_STATE_LIVE_DEATH;
}

net_reconnect_prop_placement_e netReconnectPlanPropPlacement(
		s32 has_parent, s32 wire_active)
{
	if (has_parent) {
		return wire_active
			? NET_RECONNECT_PROP_PLACEMENT_INVALID
			: NET_RECONNECT_PROP_PLACEMENT_ATTACHED;
	}
	return wire_active
		? NET_RECONNECT_PROP_PLACEMENT_ACTIVE
		: NET_RECONNECT_PROP_PLACEMENT_PAUSED;
}

s32 netReconnectWorldPropShouldSerialize(s32 pending_delete,
		s32 can_regenerate)
{
	return !pending_delete || can_regenerate;
}

const char *netReconnectConnectStatusString(
		net_reconnect_connect_status_e status)
{
	switch (status) {
	case NET_RECONNECT_CONNECT_OK: return "ok";
	case NET_RECONNECT_CONNECT_INVALID_ARGUMENT: return "invalid_argument";
	case NET_RECONNECT_CONNECT_INVALID_PROTOCOL: return "invalid_protocol";
	case NET_RECONNECT_CONNECT_INVALID_CLIENT: return "invalid_client";
	case NET_RECONNECT_CONNECT_RESERVED_BITS: return "reserved_bits";
	case NET_RECONNECT_CONNECT_PROTOCOL_MISMATCH: return "protocol_mismatch";
	default: return "unknown";
	}
}
