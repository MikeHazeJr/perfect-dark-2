/**
 * net_client_settings_wire.c -- Transactional v55 CLC_SETTINGS codec.
 */

#include <float.h>
#include <stddef.h>
#include <string.h>

#include "net/netbuf.h"
#include "net/net_client_settings_wire.h"

_Static_assert(NET_CLIENT_SETTINGS_NAME_CAPACITY == MAX_PLAYERNAME,
	"v55 settings name capacity must match NET_MAX_NAME/MAX_PLAYERNAME");

static size_t netClientSettingsStringWireSize(const char *value)
{
	return sizeof(u16) + strlen(value) + 1;
}

static s32 netClientSettingsFinite(f32 value)
{
	return value >= -FLT_MAX && value <= FLT_MAX;
}

net_client_settings_wire_status_e netClientSettingsPrepare(
		const net_client_settings_input_t *input,
		net_client_settings_plan_t *out_plan,
		player_identity_status_e *out_identity_status)
{
	net_client_settings_plan_t candidate;
	player_identity_status_e identity_status;
	size_t name_length;

	if (out_identity_status) {
		*out_identity_status = PLAYER_IDENTITY_INVALID_ARGUMENT;
	}
	if (!input || !out_plan || !input->name) {
		return NET_CLIENT_SETTINGS_WIRE_INVALID_ARGUMENT;
	}

	memset(&candidate, 0, sizeof(candidate));
	identity_status = playerIdentityPrepare(input->body_id, input->head_id,
		&candidate.identity);
	if (out_identity_status) {
		*out_identity_status = identity_status;
	}
	if (identity_status != PLAYER_IDENTITY_OK) {
		return NET_CLIENT_SETTINGS_WIRE_INVALID_IDENTITY;
	}
	if (input->team >= MAX_TEAMS) {
		return NET_CLIENT_SETTINGS_WIRE_INVALID_TEAM;
	}
	if (input->handicap == 0) {
		return NET_CLIENT_SETTINGS_WIRE_INVALID_HANDICAP;
	}
	if (!netClientSettingsFinite(input->fovy)
			|| input->fovy < 5.0f || input->fovy > 175.0f
			|| !netClientSettingsFinite(input->fovzoommult)
			|| input->fovzoommult <= 0.0f) {
		return NET_CLIENT_SETTINGS_WIRE_INVALID_FOV;
	}
	name_length = strnlen(input->name, sizeof(candidate.name));
	if (name_length == 0 || name_length >= sizeof(candidate.name)) {
		return NET_CLIENT_SETTINGS_WIRE_INVALID_NAME;
	}

	candidate.options = input->options;
	candidate.team = input->team;
	candidate.handicap = input->handicap;
	candidate.fovy = input->fovy;
	candidate.fovzoommult = input->fovzoommult;
	memcpy(candidate.name, input->name, name_length + 1);
	*out_plan = candidate;
	return NET_CLIENT_SETTINGS_WIRE_OK;
}

net_client_settings_wire_status_e netClientSettingsWireWrite(
		struct netbuf *dst,
		u8 opcode,
		const net_client_settings_input_t *input,
		player_identity_status_e *out_identity_status)
{
	net_client_settings_plan_t plan;
	net_client_settings_wire_status_e status;
	size_t required;

	if (!dst) {
		return NET_CLIENT_SETTINGS_WIRE_INVALID_ARGUMENT;
	}
	status = netClientSettingsPrepare(input, &plan, out_identity_status);
	if (status != NET_CLIENT_SETTINGS_WIRE_OK) {
		return status;
	}

	required = sizeof(u8) + sizeof(u16)
		+ netClientSettingsStringWireSize(plan.identity.body_id)
		+ netClientSettingsStringWireSize(plan.identity.head_id)
		+ sizeof(u8) + sizeof(u8) + sizeof(f32) + sizeof(f32)
		+ netClientSettingsStringWireSize(plan.name);
	if (dst->error || dst->wp > dst->size
			|| required > (size_t)netbufWriteLeft(dst)) {
		return NET_CLIENT_SETTINGS_WIRE_BUFFER_TOO_SMALL;
	}

	netbufWriteU8(dst, opcode);
	netbufWriteU16(dst, plan.options);
	netbufWriteStr(dst, plan.identity.body_id);
	netbufWriteStr(dst, plan.identity.head_id);
	netbufWriteU8(dst, plan.team);
	netbufWriteU8(dst, plan.handicap);
	netbufWriteF32(dst, plan.fovy);
	netbufWriteF32(dst, plan.fovzoommult);
	netbufWriteStr(dst, plan.name);
	return dst->error ? NET_CLIENT_SETTINGS_WIRE_BUFFER_TOO_SMALL
		: NET_CLIENT_SETTINGS_WIRE_OK;
}

net_client_settings_wire_status_e netClientSettingsWireRead(
		struct netbuf *src,
		net_client_settings_plan_t *out_plan,
		player_identity_status_e *out_identity_status)
{
	net_client_settings_input_t input;
	const char *body_id;
	const char *head_id;
	const char *name;

	if (!src || !out_plan) {
		return NET_CLIENT_SETTINGS_WIRE_INVALID_ARGUMENT;
	}

	input.options = netbufReadU16(src);
	body_id = netbufReadStr(src);
	head_id = netbufReadStr(src);
	input.team = netbufReadU8(src);
	input.handicap = netbufReadU8(src);
	input.fovy = netbufReadF32(src);
	input.fovzoommult = netbufReadF32(src);
	name = netbufReadStr(src);
	if (src->error || !body_id || !head_id || !name) {
		return NET_CLIENT_SETTINGS_WIRE_MALFORMED;
	}
	input.body_id = body_id;
	input.head_id = head_id;
	input.name = name;
	return netClientSettingsPrepare(&input, out_plan, out_identity_status);
}

u8 netClientSettingsEffectiveHandicap(u8 requested, s32 in_match,
		u8 committed)
{
	return in_match ? committed : requested;
}

const char *netClientSettingsWireStatusString(
		net_client_settings_wire_status_e status)
{
	switch (status) {
	case NET_CLIENT_SETTINGS_WIRE_OK: return "ok";
	case NET_CLIENT_SETTINGS_WIRE_INVALID_ARGUMENT: return "invalid_argument";
	case NET_CLIENT_SETTINGS_WIRE_INVALID_IDENTITY: return "invalid_identity";
	case NET_CLIENT_SETTINGS_WIRE_INVALID_TEAM: return "invalid_team";
	case NET_CLIENT_SETTINGS_WIRE_INVALID_HANDICAP: return "invalid_handicap";
	case NET_CLIENT_SETTINGS_WIRE_INVALID_FOV: return "invalid_fov";
	case NET_CLIENT_SETTINGS_WIRE_INVALID_NAME: return "invalid_name";
	case NET_CLIENT_SETTINGS_WIRE_BUFFER_TOO_SMALL: return "buffer_too_small";
	case NET_CLIENT_SETTINGS_WIRE_MALFORMED: return "malformed";
	default: return "unknown";
	}
}
