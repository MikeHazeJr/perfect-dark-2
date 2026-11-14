/*
 * tests/test_net_scenario_state_wire.cpp
 *
 * Behavior tests for the pure scenario state codec
 * (port/src/net/net_scenario_state_wire.c). The codec is the data half of the
 * host-authoritative scenario seam: the host encodes its scenario state, a
 * client decodes it and applies it only if every field is in range.
 *
 * Contract under test:
 *   - a valid state survives a round trip field for field;
 *   - every truncated payload is rejected and the caller's output is not
 *     touched;
 *   - every out-of-range field is rejected by both the encoder (nothing
 *     written) and the decoder (output not touched);
 *   - Combat and unknown scenario ids have no body and are rejected;
 *   - the decoder consumes exactly its own bytes.
 */

#include "catch.hpp"
#include "net/net_scenario_state_wire.h"

#include <cstring>
#include <initializer_list>
#include <limits>

extern "C" {
struct netbuf { u8 *data; u32 size, rp, wp, error; };
void netbufStartWrite(struct netbuf *);
void netbufStartReadData(struct netbuf *, const void *, u32);
u32 netbufWriteU8(struct netbuf *, const u8);
u32 netbufWriteU32(struct netbuf *, const u32);
u32 netbufWriteS8(struct netbuf *, const s8);
u32 netbufWriteS16(struct netbuf *, const s16);
u32 netbufWriteF32(struct netbuf *, const f32);
u8 netbufReadU8(struct netbuf *);
}

namespace {

net_scenario_state_t make_koh_state()
{
	net_scenario_state_t state;
	std::memset(&state, 0, sizeof(state));
	state.scenario = NET_SCENARIO_STATE_KINGOFTHEHILL;
	state.revision = 0x01020304u;
	state.body.koh.hill_index = 3;
	state.body.koh.hill_room = 41;
	state.body.koh.hill_pos[0] = 1250.5f;
	state.body.koh.hill_pos[1] = -86.25f;
	state.body.koh.hill_pos[2] = 0.125f;
	state.body.koh.occupied_team = 2;
	state.body.koh.elapsed240 = 3599;
	state.body.koh.move_hill = 0;
	state.body.koh.hill_time = 10;
	return state;
}

bool koh_equal(const net_scenario_state_t &a, const net_scenario_state_t &b)
{
	return a.scenario == b.scenario
		&& a.revision == b.revision
		&& a.body.koh.hill_index == b.body.koh.hill_index
		&& a.body.koh.hill_room == b.body.koh.hill_room
		&& a.body.koh.hill_pos[0] == b.body.koh.hill_pos[0]
		&& a.body.koh.hill_pos[1] == b.body.koh.hill_pos[1]
		&& a.body.koh.hill_pos[2] == b.body.koh.hill_pos[2]
		&& a.body.koh.occupied_team == b.body.koh.occupied_team
		&& a.body.koh.elapsed240 == b.body.koh.elapsed240
		&& a.body.koh.move_hill == b.body.koh.move_hill
		&& a.body.koh.hill_time == b.body.koh.hill_time;
}

/* Writes a King of the Hill payload field by field with the same primitives
 * the codec uses, without the codec's validation, so a test can put any
 * value on the wire regardless of byte order. */
u32 write_raw_koh(u8 *bytes, u32 capacity, const net_scenario_state_t &state)
{
	netbuf raw{bytes, capacity, 0, 0, 0};
	netbufStartWrite(&raw);
	netbufWriteU8(&raw, state.scenario);
	netbufWriteU32(&raw, state.revision);
	netbufWriteS16(&raw, state.body.koh.hill_index);
	netbufWriteS16(&raw, state.body.koh.hill_room);
	netbufWriteF32(&raw, state.body.koh.hill_pos[0]);
	netbufWriteF32(&raw, state.body.koh.hill_pos[1]);
	netbufWriteF32(&raw, state.body.koh.hill_pos[2]);
	netbufWriteS8(&raw, state.body.koh.occupied_team);
	netbufWriteS16(&raw, state.body.koh.elapsed240);
	netbufWriteU8(&raw, state.body.koh.move_hill);
	netbufWriteU8(&raw, state.body.koh.hill_time);
	REQUIRE(raw.error == 0);
	return raw.wp;
}

void require_rejected_everywhere(const net_scenario_state_t &bad,
		net_scenario_state_status_e expected)
{
	/* Encoder: refuses, flags the buffer, writes nothing. */
	u8 encoded[64]{};
	netbuf write{encoded, sizeof(encoded), 0, 0, 0};
	netbufStartWrite(&write);
	REQUIRE(netScenarioStateValidate(&bad) == expected);
	REQUIRE_FALSE(netScenarioStatePayloadWrite(&write, &bad));
	REQUIRE(write.error != 0);
	REQUIRE(write.wp == 0);

	/* Decoder: the same bytes arriving from a peer are refused and the
	 * caller's state is left exactly as it was. */
	u8 raw[64]{};
	const u32 length = write_raw_koh(raw, sizeof(raw), bad);
	netbuf read{};
	netbufStartReadData(&read, raw, length);
	net_scenario_state_t output;
	std::memset(&output, 0x5a, sizeof(output));
	const net_scenario_state_t before = output;
	REQUIRE(netScenarioStatePayloadRead(&read, &output) == expected);
	REQUIRE(read.error != 0);
	REQUIRE(std::memcmp(&before, &output, sizeof(output)) == 0);
}

} // namespace

TEST_CASE("Scenario state wire round-trips King of the Hill state field for field",
          "[net][scenario][wire][T-NETWORKING-011]")
{
	const net_scenario_state_t held = make_koh_state();

	net_scenario_state_t unchosen = make_koh_state();
	unchosen.revision = 0;
	unchosen.body.koh.hill_index = -1;
	unchosen.body.koh.hill_room = -1;
	unchosen.body.koh.hill_pos[0] = 0.0f;
	unchosen.body.koh.hill_pos[1] = 0.0f;
	unchosen.body.koh.hill_pos[2] = 0.0f;
	unchosen.body.koh.occupied_team = -1;
	unchosen.body.koh.elapsed240 = 0;
	unchosen.body.koh.move_hill = 1;
	unchosen.body.koh.hill_time = 255;

	net_scenario_state_t extremes = make_koh_state();
	extremes.revision = 0xffffffffu;
	extremes.body.koh.hill_index = NET_SCENARIO_KOH_MAX_HILLS - 1;
	extremes.body.koh.hill_room = 32767;
	extremes.body.koh.occupied_team = NET_SCENARIO_STATE_MAX_TEAMS - 1;
	extremes.body.koh.elapsed240 = 32767;

	for (const net_scenario_state_t &input : {held, unchosen, extremes}) {
		u8 bytes[64]{};
		netbuf write{bytes, sizeof(bytes), 0, 0, 0};
		netbufStartWrite(&write);
		REQUIRE(netScenarioStateValidate(&input) == NET_SCENARIO_STATE_OK);
		REQUIRE(netScenarioStatePayloadWrite(&write, &input));
		REQUIRE(write.error == 0);
		REQUIRE(write.wp == netScenarioStatePayloadSize(NET_SCENARIO_STATE_KINGOFTHEHILL));

		netbuf read{};
		netbufStartReadData(&read, bytes, write.wp);
		net_scenario_state_t output;
		std::memset(&output, 0x5a, sizeof(output));
		REQUIRE(netScenarioStatePayloadRead(&read, &output) == NET_SCENARIO_STATE_OK);
		REQUIRE(read.error == 0);
		REQUIRE(koh_equal(input, output));
	}
}

TEST_CASE("Scenario state wire rejects every truncated payload without touching the output",
          "[net][scenario][wire][T-NETWORKING-011]")
{
	const net_scenario_state_t input = make_koh_state();
	u8 bytes[64]{};
	netbuf write{bytes, sizeof(bytes), 0, 0, 0};
	netbufStartWrite(&write);
	REQUIRE(netScenarioStatePayloadWrite(&write, &input));

	for (u32 size = 0; size < write.wp; ++size) {
		netbuf read{};
		netbufStartReadData(&read, bytes, size);
		net_scenario_state_t output;
		std::memset(&output, 0x5a, sizeof(output));
		const net_scenario_state_t before = output;
		INFO("payload bytes available: " << size);
		REQUIRE(netScenarioStatePayloadRead(&read, &output) == NET_SCENARIO_STATE_TRUNCATED);
		REQUIRE(read.error != 0);
		REQUIRE(std::memcmp(&before, &output, sizeof(output)) == 0);
	}
}

TEST_CASE("Scenario state wire rejects each out-of-range King of the Hill field on both sides",
          "[net][scenario][wire][T-NETWORKING-011]")
{
	net_scenario_state_t bad;

	bad = make_koh_state();
	bad.body.koh.hill_index = NET_SCENARIO_KOH_MAX_HILLS;
	require_rejected_everywhere(bad, NET_SCENARIO_STATE_BAD_FIELD);

	bad = make_koh_state();
	bad.body.koh.hill_index = -2;
	require_rejected_everywhere(bad, NET_SCENARIO_STATE_BAD_FIELD);

	bad = make_koh_state();
	bad.body.koh.hill_room = -2;
	require_rejected_everywhere(bad, NET_SCENARIO_STATE_BAD_FIELD);

	bad = make_koh_state();
	bad.body.koh.occupied_team = NET_SCENARIO_STATE_MAX_TEAMS;
	require_rejected_everywhere(bad, NET_SCENARIO_STATE_BAD_FIELD);

	bad = make_koh_state();
	bad.body.koh.occupied_team = -2;
	require_rejected_everywhere(bad, NET_SCENARIO_STATE_BAD_FIELD);

	bad = make_koh_state();
	bad.body.koh.elapsed240 = -1;
	require_rejected_everywhere(bad, NET_SCENARIO_STATE_BAD_FIELD);

	bad = make_koh_state();
	bad.body.koh.move_hill = 2;
	require_rejected_everywhere(bad, NET_SCENARIO_STATE_BAD_FIELD);

	for (int axis = 0; axis < 3; ++axis) {
		bad = make_koh_state();
		bad.body.koh.hill_pos[axis] = std::numeric_limits<float>::quiet_NaN();
		require_rejected_everywhere(bad, NET_SCENARIO_STATE_BAD_FIELD);

		bad = make_koh_state();
		bad.body.koh.hill_pos[axis] = std::numeric_limits<float>::infinity();
		require_rejected_everywhere(bad, NET_SCENARIO_STATE_BAD_FIELD);

		bad = make_koh_state();
		bad.body.koh.hill_pos[axis] = -std::numeric_limits<float>::infinity();
		require_rejected_everywhere(bad, NET_SCENARIO_STATE_BAD_FIELD);
	}
}

TEST_CASE("Scenario state wire has no body for Combat or an unknown scenario id",
          "[net][scenario][wire][T-NETWORKING-011]")
{
	for (const u8 scenario : {(u8)NET_SCENARIO_STATE_COMBAT, (u8)6, (u8)255}) {
		INFO("scenario id: " << (unsigned)scenario);
		REQUIRE(netScenarioStatePayloadSize(scenario) == 0);

		net_scenario_state_t state = make_koh_state();
		state.scenario = scenario;
		REQUIRE(netScenarioStateValidate(&state) == NET_SCENARIO_STATE_BAD_SCENARIO);

		u8 encoded[64]{};
		netbuf write{encoded, sizeof(encoded), 0, 0, 0};
		netbufStartWrite(&write);
		REQUIRE_FALSE(netScenarioStatePayloadWrite(&write, &state));
		REQUIRE(write.error != 0);
		REQUIRE(write.wp == 0);

		u8 raw[64]{};
		const u32 length = write_raw_koh(raw, sizeof(raw), state);
		netbuf read{};
		netbufStartReadData(&read, raw, length);
		net_scenario_state_t output;
		std::memset(&output, 0x5a, sizeof(output));
		const net_scenario_state_t before = output;
		REQUIRE(netScenarioStatePayloadRead(&read, &output) == NET_SCENARIO_STATE_BAD_SCENARIO);
		REQUIRE(read.error != 0);
		REQUIRE(std::memcmp(&before, &output, sizeof(output)) == 0);
	}
}

TEST_CASE("Scenario state wire refuses an undersized buffer and null arguments cleanly",
          "[net][scenario][wire][T-NETWORKING-011]")
{
	const net_scenario_state_t input = make_koh_state();
	const u32 required = netScenarioStatePayloadSize(NET_SCENARIO_STATE_KINGOFTHEHILL);
	REQUIRE(required > 0);

	u8 small[64]{};
	netbuf write{small, required - 1, 0, 0, 0};
	netbufStartWrite(&write);
	REQUIRE_FALSE(netScenarioStatePayloadWrite(&write, &input));
	REQUIRE(write.error != 0);
	REQUIRE(write.wp == 0);

	u8 bytes[64]{};
	netbuf ok{bytes, sizeof(bytes), 0, 0, 0};
	netbufStartWrite(&ok);
	REQUIRE_FALSE(netScenarioStatePayloadWrite(nullptr, &input));
	REQUIRE_FALSE(netScenarioStatePayloadWrite(&ok, nullptr));
	REQUIRE(ok.wp == 0);
	REQUIRE(netScenarioStateValidate(nullptr) == NET_SCENARIO_STATE_BAD_ARGUMENT);

	net_scenario_state_t output;
	std::memset(&output, 0x5a, sizeof(output));
	const net_scenario_state_t before = output;
	REQUIRE(netScenarioStatePayloadRead(nullptr, &output) == NET_SCENARIO_STATE_BAD_ARGUMENT);
	REQUIRE(std::memcmp(&before, &output, sizeof(output)) == 0);
	netbuf read{};
	netbufStartReadData(&read, bytes, 0);
	REQUIRE(netScenarioStatePayloadRead(&read, nullptr) == NET_SCENARIO_STATE_BAD_ARGUMENT);
}

TEST_CASE("Scenario state wire consumes exactly its own bytes",
          "[net][scenario][wire][T-NETWORKING-011]")
{
	const net_scenario_state_t input = make_koh_state();
	u8 bytes[64]{};
	netbuf write{bytes, sizeof(bytes), 0, 0, 0};
	netbufStartWrite(&write);
	REQUIRE(netScenarioStatePayloadWrite(&write, &input));
	const u32 payload = write.wp;
	/* The next message in the same packet. */
	netbufWriteU8(&write, 0xa7);
	REQUIRE(write.error == 0);

	netbuf read{};
	netbufStartReadData(&read, bytes, write.wp);
	net_scenario_state_t output;
	std::memset(&output, 0, sizeof(output));
	REQUIRE(netScenarioStatePayloadRead(&read, &output) == NET_SCENARIO_STATE_OK);
	REQUIRE(read.rp == payload);
	REQUIRE(netbufReadU8(&read) == 0xa7);
	REQUIRE(read.error == 0);
}
