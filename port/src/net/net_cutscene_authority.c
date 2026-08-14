#include "net/net_cutscene_authority.h"

#include <string.h>

static u32 readLe32(const u8 *wire)
{
	return (u32)wire[0]
		| ((u32)wire[1] << 8)
		| ((u32)wire[2] << 16)
		| ((u32)wire[3] << 24);
}

static void writeLe32(u8 *wire, u32 value)
{
	wire[0] = (u8)value;
	wire[1] = (u8)(value >> 8);
	wire[2] = (u8)(value >> 16);
	wire[3] = (u8)(value >> 24);
}

net_cutscene_authority_status_t netCutsceneValidateRoster(
		const net_cutscene_participant_t *participants,
		size_t participant_count)
{
	u32 client_mask = 0;
	u8 player_mask = 0;

	if (!participants || participant_count == 0
			|| participant_count > NET_CUTSCENE_AUTHORITY_MAX_PLAYERS) {
		return NET_CUTSCENE_AUTHORITY_INVALID_ROSTER;
	}

	for (size_t i = 0; i < participant_count; i++) {
		const net_cutscene_participant_t *participant = &participants[i];
		if (participant->client_id >= NET_CUTSCENE_AUTHORITY_MAX_CLIENTS
				|| participant->runtime_playernum >=
					NET_CUTSCENE_AUTHORITY_MAX_PLAYERS) {
			return NET_CUTSCENE_AUTHORITY_INVALID_ROSTER;
		}
		const u32 client_bit = 1u << participant->client_id;
		const u8 player_bit = (u8)(1u << participant->runtime_playernum);
		if ((client_mask & client_bit) || (player_mask & player_bit)) {
			return NET_CUTSCENE_AUTHORITY_INVALID_ROSTER;
		}
		client_mask |= client_bit;
		player_mask |= player_bit;
	}

	return NET_CUTSCENE_AUTHORITY_OK;
}

const char *netCutsceneAuthorityStatusString(
		net_cutscene_authority_status_t status)
{
	switch (status) {
	case NET_CUTSCENE_AUTHORITY_OK: return "ok";
	case NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT: return "invalid_argument";
	case NET_CUTSCENE_AUTHORITY_INVALID_WIRE: return "invalid_wire";
	case NET_CUTSCENE_AUTHORITY_INVALID_ROSTER: return "invalid_roster";
	case NET_CUTSCENE_AUTHORITY_SOURCE_NOT_IN_GAME: return "source_not_in_game";
	case NET_CUTSCENE_AUTHORITY_NO_ACTIVE_CUTSCENE: return "no_active_cutscene";
	case NET_CUTSCENE_AUTHORITY_GENERATION_MISMATCH: return "generation_mismatch";
	case NET_CUTSCENE_AUTHORITY_REQUESTER_NOT_FOUND: return "requester_not_found";
	case NET_CUTSCENE_AUTHORITY_STATE_CONFLICT: return "state_conflict";
	default: return "unknown";
	}
}

s32 netCutsceneSkipRequestEncode(
		const net_cutscene_skip_request_t *request,
		u8 out[NET_CUTSCENE_SKIP_REQUEST_WIRE_SIZE])
{
	if (!request || !out
			|| request->requested_playernum >=
				NET_CUTSCENE_AUTHORITY_MAX_PLAYERS
			|| request->generation == 0) {
		return 0;
	}
	out[0] = request->requested_playernum;
	writeLe32(&out[1], request->generation);
	return 1;
}

net_cutscene_authority_status_t netCutsceneSkipRequestDecode(
		const u8 *wire, size_t wire_size,
		net_cutscene_skip_request_t *out)
{
	if (!wire || !out) {
		return NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT;
	}
	memset(out, 0, sizeof(*out));
	if (wire_size != NET_CUTSCENE_SKIP_REQUEST_WIRE_SIZE) {
		return NET_CUTSCENE_AUTHORITY_INVALID_WIRE;
	}
	out->requested_playernum = wire[0];
	out->generation = readLe32(&wire[1]);
	if (out->requested_playernum >= NET_CUTSCENE_AUTHORITY_MAX_PLAYERS
			|| out->generation == 0) {
		memset(out, 0, sizeof(*out));
		return NET_CUTSCENE_AUTHORITY_INVALID_WIRE;
	}
	return NET_CUTSCENE_AUTHORITY_OK;
}

s32 netCutsceneSkipAcceptEncode(
		const net_cutscene_skip_accept_t *accept,
		u8 out[NET_CUTSCENE_SKIP_ACCEPT_WIRE_SIZE])
{
	if (!accept || !out
			|| accept->requester_client_id >=
				NET_CUTSCENE_AUTHORITY_MAX_CLIENTS
			|| accept->generation == 0) {
		return 0;
	}
	out[0] = accept->requester_client_id;
	writeLe32(&out[1], accept->generation);
	return 1;
}

net_cutscene_authority_status_t netCutsceneSkipAcceptDecode(
		const u8 *wire, size_t wire_size,
		net_cutscene_skip_accept_t *out)
{
	if (!wire || !out) {
		return NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT;
	}
	memset(out, 0, sizeof(*out));
	if (wire_size != NET_CUTSCENE_SKIP_ACCEPT_WIRE_SIZE) {
		return NET_CUTSCENE_AUTHORITY_INVALID_WIRE;
	}
	out->requester_client_id = wire[0];
	out->generation = readLe32(&wire[1]);
	if (out->requester_client_id >= NET_CUTSCENE_AUTHORITY_MAX_CLIENTS
			|| out->generation == 0) {
		memset(out, 0, sizeof(*out));
		return NET_CUTSCENE_AUTHORITY_INVALID_WIRE;
	}
	return NET_CUTSCENE_AUTHORITY_OK;
}

s32 netCutsceneStateEncode(const net_cutscene_state_wire_t *state,
		u8 out[NET_CUTSCENE_STATE_WIRE_SIZE])
{
	if (!state || !out || state->active > 1 || state->client_mask == 0
			|| state->generation == 0) {
		return 0;
	}
	out[0] = state->active;
	writeLe32(&out[1], state->client_mask);
	writeLe32(&out[5], state->generation);
	return 1;
}

net_cutscene_authority_status_t netCutsceneStateDecode(
		const u8 *wire, size_t wire_size,
		net_cutscene_state_wire_t *out)
{
	if (!wire || !out) {
		return NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT;
	}
	memset(out, 0, sizeof(*out));
	if (wire_size != NET_CUTSCENE_STATE_WIRE_SIZE) {
		return NET_CUTSCENE_AUTHORITY_INVALID_WIRE;
	}
	out->active = wire[0];
	out->client_mask = readLe32(&wire[1]);
	out->generation = readLe32(&wire[5]);
	if (out->active > 1 || out->client_mask == 0
			|| out->generation == 0) {
		memset(out, 0, sizeof(*out));
		return NET_CUTSCENE_AUTHORITY_INVALID_WIRE;
	}
	return NET_CUTSCENE_AUTHORITY_OK;
}

s32 netCutsceneAuthorityNextGeneration(u32 current, u32 *out_generation)
{
	if (!out_generation) {
		return 0;
	}
	*out_generation = current;
	if (current == 0xffffffffu) {
		return 0;
	}
	*out_generation = current + 1;
	return 1;
}

net_cutscene_authority_status_t netCutsceneAuthoritySyncGeneration(
		u32 current, u32 authority_generation, u32 *out_generation)
{
	if (!out_generation) {
		return NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT;
	}
	*out_generation = current;
	if (authority_generation == 0) {
		return NET_CUTSCENE_AUTHORITY_INVALID_WIRE;
	}
	if (current != 0 && authority_generation < current) {
		return NET_CUTSCENE_AUTHORITY_GENERATION_MISMATCH;
	}
	*out_generation = authority_generation;
	return NET_CUTSCENE_AUTHORITY_OK;
}

net_cutscene_authority_status_t netCutscenePlanStateTransition(
		const net_cutscene_authority_tracker_t *current,
		const net_cutscene_state_wire_t *incoming,
		net_cutscene_authority_tracker_t *out_next,
		net_cutscene_authority_action_t *out_action)
{
	if (!current || !incoming || !out_next || !out_action) {
		return NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT;
	}
	*out_next = *current;
	*out_action = NET_CUTSCENE_AUTHORITY_IGNORE_STALE;
	if (current->phase > NET_CUTSCENE_AUTHORITY_PHASE_ENDED
			|| incoming->active > 1 || incoming->client_mask == 0
			|| incoming->generation == 0) {
		return NET_CUTSCENE_AUTHORITY_INVALID_WIRE;
	}

	if (current->phase == NET_CUTSCENE_AUTHORITY_PHASE_IDLE) {
		if (!incoming->active) {
			return NET_CUTSCENE_AUTHORITY_OK;
		}
		out_next->phase = NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE;
		out_next->client_mask = incoming->client_mask;
		out_next->generation = incoming->generation;
		*out_action = NET_CUTSCENE_AUTHORITY_APPLY;
		return NET_CUTSCENE_AUTHORITY_OK;
	}

	if (incoming->generation < current->generation) {
		return NET_CUTSCENE_AUTHORITY_OK;
	}
	if (incoming->generation == current->generation) {
		if (incoming->client_mask != current->client_mask) {
			return NET_CUTSCENE_AUTHORITY_STATE_CONFLICT;
		}
		if (current->phase == NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE) {
			if (incoming->active) {
				*out_action = NET_CUTSCENE_AUTHORITY_IGNORE_DUPLICATE;
				return NET_CUTSCENE_AUTHORITY_OK;
			}
			out_next->phase = NET_CUTSCENE_AUTHORITY_PHASE_ENDED;
			*out_action = NET_CUTSCENE_AUTHORITY_APPLY;
			return NET_CUTSCENE_AUTHORITY_OK;
		}
		if (!incoming->active) {
			*out_action = NET_CUTSCENE_AUTHORITY_IGNORE_DUPLICATE;
		}
		return NET_CUTSCENE_AUTHORITY_OK;
	}

	/* Reliable ordering cannot skip an END while the previous generation is
	 * active, and an END can never introduce a generation. */
	if (current->phase == NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE
			|| !incoming->active) {
		return NET_CUTSCENE_AUTHORITY_STATE_CONFLICT;
	}
	out_next->phase = NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE;
	out_next->client_mask = incoming->client_mask;
	out_next->generation = incoming->generation;
	*out_action = NET_CUTSCENE_AUTHORITY_APPLY;
	return NET_CUTSCENE_AUTHORITY_OK;
}

net_cutscene_authority_status_t netCutsceneClientMaskFromPlayerMask(
		u8 player_mask, const net_cutscene_participant_t *participants,
		size_t participant_count, u32 *out_client_mask)
{
	u32 client_mask = 0;
	u8 represented_players = 0;
	net_cutscene_authority_status_t roster_status;

	if (!out_client_mask) {
		return NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT;
	}
	*out_client_mask = 0;
	roster_status = netCutsceneValidateRoster(participants, participant_count);
	if (roster_status != NET_CUTSCENE_AUTHORITY_OK || player_mask == 0) {
		return roster_status == NET_CUTSCENE_AUTHORITY_OK
			? NET_CUTSCENE_AUTHORITY_INVALID_WIRE : roster_status;
	}

	for (size_t i = 0; i < participant_count; i++) {
		const u8 player_bit =
			(u8)(1u << participants[i].runtime_playernum);
		if (player_mask & player_bit) {
			represented_players |= player_bit;
			client_mask |= 1u << participants[i].client_id;
		}
	}
	if (represented_players != player_mask) {
		return NET_CUTSCENE_AUTHORITY_REQUESTER_NOT_FOUND;
	}
	*out_client_mask = client_mask;
	return NET_CUTSCENE_AUTHORITY_OK;
}

net_cutscene_authority_status_t netCutscenePlayerMaskFromClientMask(
		u32 client_mask, const net_cutscene_participant_t *participants,
		size_t participant_count, u8 *out_player_mask)
{
	u32 represented_clients = 0;
	u8 player_mask = 0;
	net_cutscene_authority_status_t roster_status;

	if (!out_player_mask) {
		return NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT;
	}
	*out_player_mask = 0;
	roster_status = netCutsceneValidateRoster(participants, participant_count);
	if (roster_status != NET_CUTSCENE_AUTHORITY_OK || client_mask == 0) {
		return roster_status == NET_CUTSCENE_AUTHORITY_OK
			? NET_CUTSCENE_AUTHORITY_INVALID_WIRE : roster_status;
	}

	for (size_t i = 0; i < participant_count; i++) {
		const u32 client_bit = 1u << participants[i].client_id;
		if (client_mask & client_bit) {
			represented_clients |= client_bit;
			player_mask |= (u8)(1u << participants[i].runtime_playernum);
		}
	}
	if (represented_clients != client_mask) {
		return NET_CUTSCENE_AUTHORITY_REQUESTER_NOT_FOUND;
	}
	*out_player_mask = player_mask;
	return NET_CUTSCENE_AUTHORITY_OK;
}

net_cutscene_authority_status_t netCutscenePlanServerSkip(
		const net_cutscene_skip_request_t *request,
		u8 source_client_id, u8 source_playernum, s32 source_in_game,
		s32 cutscene_active, u32 authoritative_generation,
		net_cutscene_skip_accept_t *out_accept,
		u8 *out_authoritative_playernum)
{
	if (!request || !out_accept || !out_authoritative_playernum) {
		return NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT;
	}
	memset(out_accept, 0, sizeof(*out_accept));
	*out_authoritative_playernum = 0xff;
	if (request->requested_playernum >= NET_CUTSCENE_AUTHORITY_MAX_PLAYERS
			|| request->generation == 0
			|| source_client_id >= NET_CUTSCENE_AUTHORITY_MAX_CLIENTS
			|| source_playernum >= NET_CUTSCENE_AUTHORITY_MAX_PLAYERS) {
		return NET_CUTSCENE_AUTHORITY_INVALID_WIRE;
	}
	if (source_in_game != 1) {
		return NET_CUTSCENE_AUTHORITY_SOURCE_NOT_IN_GAME;
	}
	if (cutscene_active != 1 || authoritative_generation == 0) {
		return NET_CUTSCENE_AUTHORITY_NO_ACTIVE_CUTSCENE;
	}
	if (request->generation != authoritative_generation) {
		return NET_CUTSCENE_AUTHORITY_GENERATION_MISMATCH;
	}

	out_accept->requester_client_id = source_client_id;
	out_accept->generation = authoritative_generation;
	*out_authoritative_playernum = source_playernum;
	return NET_CUTSCENE_AUTHORITY_OK;
}

net_cutscene_authority_status_t netCutscenePlanClientSkip(
		const net_cutscene_skip_accept_t *accept,
		const net_cutscene_participant_t *participants,
		size_t participant_count, s32 cutscene_active,
		u32 current_generation, u8 *out_runtime_playernum)
{
	if (!accept || !out_runtime_playernum) {
		return NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT;
	}
	*out_runtime_playernum = 0xff;
	if (accept->requester_client_id >= NET_CUTSCENE_AUTHORITY_MAX_CLIENTS
			|| accept->generation == 0) {
		return NET_CUTSCENE_AUTHORITY_INVALID_WIRE;
	}
	if (cutscene_active != 1 || current_generation == 0) {
		return NET_CUTSCENE_AUTHORITY_NO_ACTIVE_CUTSCENE;
	}
	if (accept->generation != current_generation) {
		return NET_CUTSCENE_AUTHORITY_GENERATION_MISMATCH;
	}

	/* Roster identity matters only for a result that can affect the current
	 * cutscene. This ordering lets reliable stale results drain harmlessly
	 * after teardown while malformed identity still fails closed when active. */
	const net_cutscene_authority_status_t roster_status =
		netCutsceneValidateRoster(participants, participant_count);
	if (roster_status != NET_CUTSCENE_AUTHORITY_OK) {
		return roster_status;
	}

	for (size_t i = 0; i < participant_count; i++) {
		if (participants[i].client_id == accept->requester_client_id) {
			*out_runtime_playernum = participants[i].runtime_playernum;
			return NET_CUTSCENE_AUTHORITY_OK;
		}
	}
	return NET_CUTSCENE_AUTHORITY_REQUESTER_NOT_FOUND;
}
