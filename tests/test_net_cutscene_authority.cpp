#include "catch.hpp"

extern "C" {
#include "net/net_cutscene_authority.h"
}

#include <cstring>

TEST_CASE("cutscene authority codecs are exact and reject malformed payloads",
		"[networking][b1089][cutscene][wire]")
{
	net_cutscene_skip_request_t request{3, 0x78563412u};
	u8 request_wire[NET_CUTSCENE_SKIP_REQUEST_WIRE_SIZE]{};
	REQUIRE(netCutsceneSkipRequestEncode(&request, request_wire) == 1);
	const u8 expected_request[] = {3, 0x12, 0x34, 0x56, 0x78};
	REQUIRE(std::memcmp(request_wire, expected_request,
		sizeof(expected_request)) == 0);

	net_cutscene_skip_request_t decoded_request{};
	REQUIRE(netCutsceneSkipRequestDecode(request_wire, sizeof(request_wire),
		&decoded_request) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(decoded_request.requested_playernum == 3);
	REQUIRE(decoded_request.generation == 0x78563412u);
	REQUIRE(netCutsceneSkipRequestDecode(request_wire,
		sizeof(request_wire) - 1, &decoded_request)
		== NET_CUTSCENE_AUTHORITY_INVALID_WIRE);
	request_wire[0] = NET_CUTSCENE_AUTHORITY_MAX_PLAYERS;
	REQUIRE(netCutsceneSkipRequestDecode(request_wire, sizeof(request_wire),
		&decoded_request) == NET_CUTSCENE_AUTHORITY_INVALID_WIRE);
	request.requested_playernum = 0;
	request.generation = 0;
	REQUIRE(netCutsceneSkipRequestEncode(&request, request_wire) == 0);
	std::memset(request_wire, 0, sizeof(request_wire));
	REQUIRE(netCutsceneSkipRequestDecode(request_wire, sizeof(request_wire),
		&decoded_request) == NET_CUTSCENE_AUTHORITY_INVALID_WIRE);

	net_cutscene_skip_accept_t accept{17, 0x01020304u};
	u8 accept_wire[NET_CUTSCENE_SKIP_ACCEPT_WIRE_SIZE]{};
	REQUIRE(netCutsceneSkipAcceptEncode(&accept, accept_wire) == 1);
	const u8 expected_accept[] = {17, 0x04, 0x03, 0x02, 0x01};
	REQUIRE(std::memcmp(accept_wire, expected_accept,
		sizeof(expected_accept)) == 0);
	net_cutscene_skip_accept_t decoded_accept{};
	REQUIRE(netCutsceneSkipAcceptDecode(accept_wire, sizeof(accept_wire),
		&decoded_accept) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(decoded_accept.requester_client_id == 17);
	REQUIRE(decoded_accept.generation == 0x01020304u);
	accept.requester_client_id = NET_CUTSCENE_AUTHORITY_MAX_CLIENTS;
	REQUIRE(netCutsceneSkipAcceptEncode(&accept, accept_wire) == 0);

	net_cutscene_state_wire_t state{1, 0x80000081u, 9};
	u8 state_wire[NET_CUTSCENE_STATE_WIRE_SIZE]{};
	REQUIRE(netCutsceneStateEncode(&state, state_wire) == 1);
	const u8 expected_state[] = {
		1, 0x81, 0x00, 0x00, 0x80, 0x09, 0x00, 0x00, 0x00
	};
	REQUIRE(std::memcmp(state_wire, expected_state,
		sizeof(expected_state)) == 0);
	net_cutscene_state_wire_t decoded_state{};
	REQUIRE(netCutsceneStateDecode(state_wire, sizeof(state_wire),
		&decoded_state) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(decoded_state.active == 1);
	REQUIRE(decoded_state.client_mask == 0x80000081u);
	REQUIRE(decoded_state.generation == 9);
	state_wire[0] = 2;
	REQUIRE(netCutsceneStateDecode(state_wire, sizeof(state_wire),
		&decoded_state) == NET_CUTSCENE_AUTHORITY_INVALID_WIRE);
	state = {0, 0, 9};
	REQUIRE(netCutsceneStateEncode(&state, state_wire) == 0);
}

TEST_CASE("cutscene authority maps stable client identity across local slot zero",
		"[networking][b1089][cutscene][identity]")
{
	const net_cutscene_participant_t server_roster[] = {
		{0, 0}, {7, 1},
	};
	u32 client_mask = 0;
	REQUIRE(netCutsceneClientMaskFromPlayerMask(0x03, server_roster, 2,
		&client_mask) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(client_mask == ((1u << 0) | (1u << 7)));

	const net_cutscene_participant_t client_roster[] = {
		{0, 1}, {7, 0},
	};
	u8 runtime_mask = 0;
	REQUIRE(netCutscenePlayerMaskFromClientMask(client_mask, client_roster, 2,
		&runtime_mask) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(runtime_mask == 0x03);

	net_cutscene_skip_accept_t local_accept{7, 4};
	u8 runtime_player = 0xff;
	REQUIRE(netCutscenePlanClientSkip(&local_accept, client_roster, 2, 1, 4,
		&runtime_player) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(runtime_player == 0);

	net_cutscene_skip_accept_t host_accept{0, 4};
	REQUIRE(netCutscenePlanClientSkip(&host_accept, client_roster, 2, 1, 4,
		&runtime_player) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(runtime_player == 1);
}

TEST_CASE("cutscene authority validates one exact frozen stage roster",
		"[networking][b1089][cutscene][roster]")
{
	const net_cutscene_participant_t valid[] = {{0, 0}, {31, 7}};
	REQUIRE(netCutsceneValidateRoster(valid, 2) ==
		NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(netCutsceneValidateRoster(nullptr, 2) ==
		NET_CUTSCENE_AUTHORITY_INVALID_ROSTER);
	REQUIRE(netCutsceneValidateRoster(valid, 0) ==
		NET_CUTSCENE_AUTHORITY_INVALID_ROSTER);

	const net_cutscene_participant_t duplicate_client[] = {{7, 0}, {7, 1}};
	REQUIRE(netCutsceneValidateRoster(duplicate_client, 2) ==
		NET_CUTSCENE_AUTHORITY_INVALID_ROSTER);
	const net_cutscene_participant_t duplicate_player[] = {{7, 0}, {8, 0}};
	REQUIRE(netCutsceneValidateRoster(duplicate_player, 2) ==
		NET_CUTSCENE_AUTHORITY_INVALID_ROSTER);
	const net_cutscene_participant_t invalid_client[] = {{32, 0}};
	REQUIRE(netCutsceneValidateRoster(invalid_client, 1) ==
		NET_CUTSCENE_AUTHORITY_INVALID_ROSTER);
	const net_cutscene_participant_t invalid_player[] = {{0, 8}};
	REQUIRE(netCutsceneValidateRoster(invalid_player, 1) ==
		NET_CUTSCENE_AUTHORITY_INVALID_ROSTER);

	net_cutscene_participant_t too_many[9]{};
	for (u8 i = 0; i < 9; i++) {
		too_many[i].client_id = i;
		too_many[i].runtime_playernum = i < 8 ? i : 0;
	}
	REQUIRE(netCutsceneValidateRoster(too_many, 9) ==
		NET_CUTSCENE_AUTHORITY_INVALID_ROSTER);
}

TEST_CASE("cutscene generations are authority-minted and synchronize monotonically",
		"[networking][b1089][cutscene][generation]")
{
	u32 generation = 0;
	REQUIRE(netCutsceneAuthorityNextGeneration(0, &generation) == 1);
	REQUIRE(generation == 1);
	REQUIRE(netCutsceneAuthorityNextGeneration(41, &generation) == 1);
	REQUIRE(generation == 42);
	REQUIRE(netCutsceneAuthorityNextGeneration(0xffffffffu, &generation) == 0);
	REQUIRE(generation == 0xffffffffu);
	REQUIRE(netCutsceneAuthorityNextGeneration(0, nullptr) == 0);

	REQUIRE(netCutsceneAuthoritySyncGeneration(0, 7, &generation) ==
		NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(generation == 7);
	REQUIRE(netCutsceneAuthoritySyncGeneration(7, 7, &generation) ==
		NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(generation == 7);
	REQUIRE(netCutsceneAuthoritySyncGeneration(7, 8, &generation) ==
		NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(generation == 8);
	REQUIRE(netCutsceneAuthoritySyncGeneration(8, 7, &generation) ==
		NET_CUTSCENE_AUTHORITY_GENERATION_MISMATCH);
	REQUIRE(generation == 8);
	REQUIRE(netCutsceneAuthoritySyncGeneration(8, 0, &generation) ==
		NET_CUTSCENE_AUTHORITY_INVALID_WIRE);
	REQUIRE(generation == 8);
	REQUIRE(netCutsceneAuthoritySyncGeneration(8, 9, nullptr) ==
		NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT);
}

TEST_CASE("cutscene authority state transitions are ordered and idempotent",
		"[networking][b1089][cutscene][state-machine]")
{
	net_cutscene_authority_tracker_t current{};
	net_cutscene_authority_tracker_t next{};
	net_cutscene_authority_action_t action = NET_CUTSCENE_AUTHORITY_APPLY;

	const net_cutscene_state_wire_t idle_end{0, 0x03u, 2};
	REQUIRE(netCutscenePlanStateTransition(&current, &idle_end, &next,
		&action) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(action == NET_CUTSCENE_AUTHORITY_IGNORE_STALE);
	REQUIRE(next.phase == NET_CUTSCENE_AUTHORITY_PHASE_IDLE);
	REQUIRE(next.client_mask == 0);
	REQUIRE(next.generation == 0);

	const net_cutscene_state_wire_t start{1, 0x03u, 2};
	REQUIRE(netCutscenePlanStateTransition(&current, &start, &next,
		&action) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(action == NET_CUTSCENE_AUTHORITY_APPLY);
	REQUIRE(next.phase == NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE);
	REQUIRE(next.client_mask == 0x03u);
	REQUIRE(next.generation == 2);
	current = next;

	REQUIRE(netCutscenePlanStateTransition(&current, &start, &next,
		&action) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(action == NET_CUTSCENE_AUTHORITY_IGNORE_DUPLICATE);
	REQUIRE(next.phase == NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE);

	const net_cutscene_state_wire_t conflicting_start{1, 0x05u, 2};
	REQUIRE(netCutscenePlanStateTransition(&current, &conflicting_start, &next,
		&action) == NET_CUTSCENE_AUTHORITY_STATE_CONFLICT);
	REQUIRE(next.phase == NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE);
	REQUIRE(next.client_mask == 0x03u);
	REQUIRE(next.generation == 2);

	const net_cutscene_state_wire_t stale_start{1, 0x03u, 1};
	REQUIRE(netCutscenePlanStateTransition(&current, &stale_start, &next,
		&action) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(action == NET_CUTSCENE_AUTHORITY_IGNORE_STALE);

	const net_cutscene_state_wire_t end{0, 0x03u, 2};
	REQUIRE(netCutscenePlanStateTransition(&current, &end, &next,
		&action) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(action == NET_CUTSCENE_AUTHORITY_APPLY);
	REQUIRE(next.phase == NET_CUTSCENE_AUTHORITY_PHASE_ENDED);
	current = next;

	REQUIRE(netCutscenePlanStateTransition(&current, &end, &next,
		&action) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(action == NET_CUTSCENE_AUTHORITY_IGNORE_DUPLICATE);
	REQUIRE(netCutscenePlanStateTransition(&current, &start, &next,
		&action) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(action == NET_CUTSCENE_AUTHORITY_IGNORE_STALE);

	const net_cutscene_state_wire_t impossible_end{0, 0x03u, 3};
	REQUIRE(netCutscenePlanStateTransition(&current, &impossible_end, &next,
		&action) == NET_CUTSCENE_AUTHORITY_STATE_CONFLICT);

	const net_cutscene_state_wire_t next_start{1, 0x03u, 3};
	REQUIRE(netCutscenePlanStateTransition(&current, &next_start, &next,
		&action) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(action == NET_CUTSCENE_AUTHORITY_APPLY);
	REQUIRE(next.phase == NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE);
	REQUIRE(next.generation == 3);
	current = next;

	const net_cutscene_state_wire_t overlapping_start{1, 0x03u, 4};
	REQUIRE(netCutscenePlanStateTransition(&current, &overlapping_start, &next,
		&action) == NET_CUTSCENE_AUTHORITY_STATE_CONFLICT);

	const net_cutscene_state_wire_t malformed{2, 0x03u, 4};
	REQUIRE(netCutscenePlanStateTransition(&current, &malformed, &next,
		&action) == NET_CUTSCENE_AUTHORITY_INVALID_WIRE);
	REQUIRE(netCutscenePlanStateTransition(nullptr, &start, &next, &action) ==
		NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT);
	REQUIRE(netCutscenePlanStateTransition(&current, nullptr, &next, &action) ==
		NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT);
	REQUIRE(netCutscenePlanStateTransition(&current, &start, nullptr, &action) ==
		NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT);
	REQUIRE(netCutscenePlanStateTransition(&current, &start, &next, nullptr) ==
		NET_CUTSCENE_AUTHORITY_INVALID_ARGUMENT);
}

TEST_CASE("cutscene authority accepts consecutive END then next-generation START",
		"[networking][b1089][b1090][cutscene][state-machine]")
{
	net_cutscene_authority_tracker_t current{
		NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE, 0x03u, 41u
	};
	net_cutscene_authority_tracker_t next{};
	net_cutscene_authority_action_t action = NET_CUTSCENE_AUTHORITY_IGNORE_STALE;

	const net_cutscene_state_wire_t overlapping_start{1, 0x03u, 42u};
	REQUIRE(netCutscenePlanStateTransition(&current, &overlapping_start, &next,
		&action) == NET_CUTSCENE_AUTHORITY_STATE_CONFLICT);

	const net_cutscene_state_wire_t end{0, 0x03u, 41u};
	REQUIRE(netCutscenePlanStateTransition(&current, &end, &next, &action) ==
		NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(action == NET_CUTSCENE_AUTHORITY_APPLY);
	REQUIRE(next.phase == NET_CUTSCENE_AUTHORITY_PHASE_ENDED);
	REQUIRE(next.client_mask == 0x03u);
	REQUIRE(next.generation == 41u);
	current = next;

	REQUIRE(netCutscenePlanStateTransition(&current, &overlapping_start, &next,
		&action) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(action == NET_CUTSCENE_AUTHORITY_APPLY);
	REQUIRE(next.phase == NET_CUTSCENE_AUTHORITY_PHASE_ACTIVE);
	REQUIRE(next.client_mask == 0x03u);
	REQUIRE(next.generation == 42u);
}

TEST_CASE("server cutscene planner trusts source identity and exact generation",
		"[networking][b1089][cutscene][authority]")
{
	const net_cutscene_skip_request_t request{0, 12};
	net_cutscene_skip_accept_t accept{};
	u8 authoritative_player = 0xff;

	REQUIRE(netCutscenePlanServerSkip(&request, 7, 1, 1, 1, 12,
		&accept, &authoritative_player) == NET_CUTSCENE_AUTHORITY_OK);
	REQUIRE(authoritative_player == 1);
	REQUIRE(accept.requester_client_id == 7);
	REQUIRE(accept.generation == 12);

	REQUIRE(netCutscenePlanServerSkip(&request, 7, 1, 0, 1, 12,
		&accept, &authoritative_player)
		== NET_CUTSCENE_AUTHORITY_SOURCE_NOT_IN_GAME);
	REQUIRE(netCutscenePlanServerSkip(&request, 7, 1, 1, 0, 12,
		&accept, &authoritative_player)
		== NET_CUTSCENE_AUTHORITY_NO_ACTIVE_CUTSCENE);
	REQUIRE(netCutscenePlanServerSkip(&request, 7, 1, 1, 1, 13,
		&accept, &authoritative_player)
		== NET_CUTSCENE_AUTHORITY_GENERATION_MISMATCH);
	REQUIRE(netCutscenePlanServerSkip(&request,
		NET_CUTSCENE_AUTHORITY_MAX_CLIENTS, 1, 1, 1, 12,
		&accept, &authoritative_player)
		== NET_CUTSCENE_AUTHORITY_INVALID_WIRE);
}

TEST_CASE("client cutscene planner rejects stale and ambiguous authority",
		"[networking][b1089][cutscene][validation]")
{
	const net_cutscene_skip_accept_t accept{7, 4};
	const net_cutscene_participant_t roster[] = {{0, 1}, {7, 0}};
	u8 runtime_player = 0xff;

	REQUIRE(netCutscenePlanClientSkip(&accept, roster, 2, 0, 4,
		&runtime_player) == NET_CUTSCENE_AUTHORITY_NO_ACTIVE_CUTSCENE);
	REQUIRE(netCutscenePlanClientSkip(&accept, nullptr, 0, 0, 4,
		&runtime_player) == NET_CUTSCENE_AUTHORITY_NO_ACTIVE_CUTSCENE);
	REQUIRE(netCutscenePlanClientSkip(&accept, roster, 2, 1, 5,
		&runtime_player) == NET_CUTSCENE_AUTHORITY_GENERATION_MISMATCH);
	REQUIRE(netCutscenePlanClientSkip(&accept, nullptr, 0, 1, 5,
		&runtime_player) == NET_CUTSCENE_AUTHORITY_GENERATION_MISMATCH);

	const net_cutscene_skip_accept_t unknown{8, 4};
	REQUIRE(netCutscenePlanClientSkip(&unknown, roster, 2, 1, 4,
		&runtime_player) == NET_CUTSCENE_AUTHORITY_REQUESTER_NOT_FOUND);

	const net_cutscene_participant_t duplicate_client[] = {{7, 0}, {7, 1}};
	REQUIRE(netCutscenePlanClientSkip(&accept, duplicate_client, 2, 1, 4,
		&runtime_player) == NET_CUTSCENE_AUTHORITY_INVALID_ROSTER);
	const net_cutscene_participant_t duplicate_player[] = {{7, 0}, {8, 0}};
	REQUIRE(netCutscenePlanClientSkip(&accept, duplicate_player, 2, 1, 4,
		&runtime_player) == NET_CUTSCENE_AUTHORITY_INVALID_ROSTER);

	u32 client_mask = 0;
	REQUIRE(netCutsceneClientMaskFromPlayerMask(0x04, roster, 2,
		&client_mask) == NET_CUTSCENE_AUTHORITY_REQUESTER_NOT_FOUND);
	u8 player_mask = 0;
	REQUIRE(netCutscenePlayerMaskFromClientMask(1u << 8, roster, 2,
		&player_mask) == NET_CUTSCENE_AUTHORITY_REQUESTER_NOT_FOUND);
}
