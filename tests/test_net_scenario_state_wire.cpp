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
u32 netbufWriteU16(struct netbuf *, const u16);
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
	state.body.koh.contested = 1;
	state.body.koh.scoring_team = 1;
	state.body.koh.scores = 7;
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
		&& a.body.koh.hill_time == b.body.koh.hill_time
		&& a.body.koh.contested == b.body.koh.contested
		&& a.body.koh.scoring_team == b.body.koh.scoring_team
		&& a.body.koh.scores == b.body.koh.scores;
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
	netbufWriteU8(&raw, state.body.koh.contested);
	netbufWriteS8(&raw, state.body.koh.scoring_team);
	netbufWriteU16(&raw, state.body.koh.scores);
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
	unchosen.body.koh.contested = 0;
	unchosen.body.koh.scoring_team = -1;
	unchosen.body.koh.scores = 0;

	net_scenario_state_t extremes = make_koh_state();
	extremes.revision = 0xffffffffu;
	extremes.body.koh.hill_index = NET_SCENARIO_KOH_MAX_HILLS - 1;
	extremes.body.koh.hill_room = 32767;
	extremes.body.koh.occupied_team = NET_SCENARIO_STATE_MAX_TEAMS - 1;
	extremes.body.koh.elapsed240 = 32767;
	extremes.body.koh.scoring_team = NET_SCENARIO_STATE_MAX_TEAMS - 1;
	extremes.body.koh.scores = 0xffff;

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

	bad = make_koh_state();
	bad.body.koh.contested = 2;
	require_rejected_everywhere(bad, NET_SCENARIO_STATE_BAD_FIELD);

	bad = make_koh_state();
	bad.body.koh.scoring_team = NET_SCENARIO_STATE_MAX_TEAMS;
	require_rejected_everywhere(bad, NET_SCENARIO_STATE_BAD_FIELD);

	bad = make_koh_state();
	bad.body.koh.scoring_team = -2;
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

namespace {

net_scenario_state_t make_pac_state()
{
	net_scenario_state_t state;
	std::memset(&state, 0, sizeof(state));
	state.scenario = NET_SCENARIO_STATE_POPACAP;
	state.revision = 77;
	state.body.pac.victim = 5;
	state.body.pac.victim_serial = 200;
	state.body.pac.age240 = 4321;
	for (int i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; ++i) {
		state.body.pac.killcounts[i] = (s16)(i * 3);
		state.body.pac.survivalcounts[i] = (s16)(i % 7);
	}
	return state;
}

u32 write_raw_pac(u8 *bytes, u32 capacity, const net_scenario_state_t &state)
{
	netbuf raw{bytes, capacity, 0, 0, 0};
	netbufStartWrite(&raw);
	netbufWriteU8(&raw, state.scenario);
	netbufWriteU32(&raw, state.revision);
	netbufWriteS8(&raw, state.body.pac.victim);
	netbufWriteU8(&raw, state.body.pac.victim_serial);
	netbufWriteU16(&raw, state.body.pac.age240);
	for (int i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; ++i) {
		netbufWriteS16(&raw, state.body.pac.killcounts[i]);
	}
	for (int i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; ++i) {
		netbufWriteS16(&raw, state.body.pac.survivalcounts[i]);
	}
	REQUIRE(raw.error == 0);
	return raw.wp;
}

void require_pac_rejected_everywhere(const net_scenario_state_t &bad)
{
	u8 encoded[512]{};
	netbuf write{encoded, sizeof(encoded), 0, 0, 0};
	netbufStartWrite(&write);
	REQUIRE(netScenarioStateValidate(&bad) == NET_SCENARIO_STATE_BAD_FIELD);
	REQUIRE_FALSE(netScenarioStatePayloadWrite(&write, &bad));
	REQUIRE(write.error != 0);
	REQUIRE(write.wp == 0);

	u8 raw[512]{};
	const u32 length = write_raw_pac(raw, sizeof(raw), bad);
	netbuf read{};
	netbufStartReadData(&read, raw, length);
	net_scenario_state_t output;
	std::memset(&output, 0x5a, sizeof(output));
	const net_scenario_state_t before = output;
	REQUIRE(netScenarioStatePayloadRead(&read, &output) == NET_SCENARIO_STATE_BAD_FIELD);
	REQUIRE(read.error != 0);
	REQUIRE(std::memcmp(&before, &output, sizeof(output)) == 0);
}

} // namespace

TEST_CASE("Scenario state wire round-trips Pop a Cap state, every character slot included",
          "[net][scenario][wire][pac][T-NETWORKING-011]")
{
	const net_scenario_state_t held = make_pac_state();

	net_scenario_state_t none = make_pac_state();
	none.body.pac.victim = -1;
	none.body.pac.victim_serial = 0;
	none.body.pac.age240 = 0;

	net_scenario_state_t extremes = make_pac_state();
	extremes.body.pac.victim = NET_SCENARIO_STATE_MAX_CHRS - 1;
	extremes.body.pac.victim_serial = 255;
	extremes.body.pac.age240 = NET_SCENARIO_PAC_MAX_AGE240;
	extremes.body.pac.killcounts[NET_SCENARIO_STATE_MAX_CHRS - 1] = 32767;
	extremes.body.pac.survivalcounts[0] = 32767;

	for (const net_scenario_state_t &input : {held, none, extremes}) {
		u8 bytes[512]{};
		netbuf write{bytes, sizeof(bytes), 0, 0, 0};
		netbufStartWrite(&write);
		REQUIRE(netScenarioStatePayloadWrite(&write, &input));
		REQUIRE(write.wp == netScenarioStatePayloadSize(NET_SCENARIO_STATE_POPACAP));

		netbuf read{};
		netbufStartReadData(&read, bytes, write.wp);
		net_scenario_state_t output;
		std::memset(&output, 0x5a, sizeof(output));
		REQUIRE(netScenarioStatePayloadRead(&read, &output) == NET_SCENARIO_STATE_OK);
		REQUIRE(read.rp == write.wp);
		REQUIRE(output.scenario == input.scenario);
		REQUIRE(output.revision == input.revision);
		REQUIRE(output.body.pac.victim == input.body.pac.victim);
		REQUIRE(output.body.pac.victim_serial == input.body.pac.victim_serial);
		REQUIRE(output.body.pac.age240 == input.body.pac.age240);
		for (int i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; ++i) {
			REQUIRE(output.body.pac.killcounts[i] == input.body.pac.killcounts[i]);
			REQUIRE(output.body.pac.survivalcounts[i] == input.body.pac.survivalcounts[i]);
		}
	}
}

TEST_CASE("Scenario state wire rejects every truncated Pop a Cap payload without touching the output",
          "[net][scenario][wire][pac][T-NETWORKING-011]")
{
	const net_scenario_state_t input = make_pac_state();
	u8 bytes[512]{};
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
		REQUIRE(std::memcmp(&before, &output, sizeof(output)) == 0);
	}
}

TEST_CASE("Scenario state wire rejects each out-of-range Pop a Cap field on both sides",
          "[net][scenario][wire][pac][T-NETWORKING-011]")
{
	net_scenario_state_t bad;

	bad = make_pac_state();
	bad.body.pac.victim = NET_SCENARIO_STATE_MAX_CHRS;
	require_pac_rejected_everywhere(bad);

	bad = make_pac_state();
	bad.body.pac.victim = -2;
	require_pac_rejected_everywhere(bad);

	bad = make_pac_state();
	bad.body.pac.age240 = NET_SCENARIO_PAC_MAX_AGE240 + 1;
	require_pac_rejected_everywhere(bad);

	for (int slot : {0, 17, NET_SCENARIO_STATE_MAX_CHRS - 1}) {
		bad = make_pac_state();
		bad.body.pac.killcounts[slot] = -1;
		require_pac_rejected_everywhere(bad);

		bad = make_pac_state();
		bad.body.pac.survivalcounts[slot] = -1;
		require_pac_rejected_everywhere(bad);
	}
}

namespace {

net_scenario_state_t make_htb_state()
{
	net_scenario_state_t state;
	std::memset(&state, 0, sizeof(state));
	state.scenario = NET_SCENARIO_STATE_HOLDTHEBRIEFCASE;
	state.revision = 12;
	state.body.htb.holder = 3;
	state.body.htb.present = 1;
	state.body.htb.pos[0] = -512.5f;
	state.body.htb.pos[1] = 30.0f;
	state.body.htb.pos[2] = 2048.25f;
	return state;
}

u32 write_raw_htb(u8 *bytes, u32 capacity, const net_scenario_state_t &state)
{
	netbuf raw{bytes, capacity, 0, 0, 0};
	netbufStartWrite(&raw);
	netbufWriteU8(&raw, state.scenario);
	netbufWriteU32(&raw, state.revision);
	netbufWriteS8(&raw, state.body.htb.holder);
	netbufWriteU8(&raw, state.body.htb.present);
	netbufWriteF32(&raw, state.body.htb.pos[0]);
	netbufWriteF32(&raw, state.body.htb.pos[1]);
	netbufWriteF32(&raw, state.body.htb.pos[2]);
	REQUIRE(raw.error == 0);
	return raw.wp;
}

void require_htb_rejected_everywhere(const net_scenario_state_t &bad)
{
	u8 encoded[64]{};
	netbuf write{encoded, sizeof(encoded), 0, 0, 0};
	netbufStartWrite(&write);
	REQUIRE(netScenarioStateValidate(&bad) == NET_SCENARIO_STATE_BAD_FIELD);
	REQUIRE_FALSE(netScenarioStatePayloadWrite(&write, &bad));
	REQUIRE(write.wp == 0);

	u8 raw[64]{};
	const u32 length = write_raw_htb(raw, sizeof(raw), bad);
	netbuf read{};
	netbufStartReadData(&read, raw, length);
	net_scenario_state_t output;
	std::memset(&output, 0x5a, sizeof(output));
	const net_scenario_state_t before = output;
	REQUIRE(netScenarioStatePayloadRead(&read, &output) == NET_SCENARIO_STATE_BAD_FIELD);
	REQUIRE(std::memcmp(&before, &output, sizeof(output)) == 0);
}

} // namespace

TEST_CASE("Scenario state wire round-trips Hold the Briefcase state held, on the ground and absent",
          "[net][scenario][wire][htb][T-NETWORKING-011]")
{
	const net_scenario_state_t held = make_htb_state();

	net_scenario_state_t ground = make_htb_state();
	ground.body.htb.holder = -1;

	net_scenario_state_t absent = make_htb_state();
	absent.body.htb.holder = -1;
	absent.body.htb.present = 0;
	absent.body.htb.pos[0] = absent.body.htb.pos[1] = absent.body.htb.pos[2] = 0.0f;

	net_scenario_state_t last = make_htb_state();
	last.body.htb.holder = NET_SCENARIO_STATE_MAX_CHRS - 1;

	for (const net_scenario_state_t &input : {held, ground, absent, last}) {
		u8 bytes[64]{};
		netbuf write{bytes, sizeof(bytes), 0, 0, 0};
		netbufStartWrite(&write);
		REQUIRE(netScenarioStatePayloadWrite(&write, &input));
		REQUIRE(write.wp == netScenarioStatePayloadSize(NET_SCENARIO_STATE_HOLDTHEBRIEFCASE));

		netbuf read{};
		netbufStartReadData(&read, bytes, write.wp);
		net_scenario_state_t output;
		std::memset(&output, 0x5a, sizeof(output));
		REQUIRE(netScenarioStatePayloadRead(&read, &output) == NET_SCENARIO_STATE_OK);
		REQUIRE(read.rp == write.wp);
		REQUIRE(output.body.htb.holder == input.body.htb.holder);
		REQUIRE(output.body.htb.present == input.body.htb.present);
		REQUIRE(output.body.htb.pos[0] == input.body.htb.pos[0]);
		REQUIRE(output.body.htb.pos[1] == input.body.htb.pos[1]);
		REQUIRE(output.body.htb.pos[2] == input.body.htb.pos[2]);
	}
}

TEST_CASE("Scenario state wire rejects every truncated Hold the Briefcase payload",
          "[net][scenario][wire][htb][T-NETWORKING-011]")
{
	const net_scenario_state_t input = make_htb_state();
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
		REQUIRE(std::memcmp(&before, &output, sizeof(output)) == 0);
	}
}

TEST_CASE("Scenario state wire rejects each out-of-range Hold the Briefcase field on both sides",
          "[net][scenario][wire][htb][T-NETWORKING-011]")
{
	net_scenario_state_t bad;

	bad = make_htb_state();
	bad.body.htb.holder = NET_SCENARIO_STATE_MAX_CHRS;
	require_htb_rejected_everywhere(bad);

	bad = make_htb_state();
	bad.body.htb.holder = -2;
	require_htb_rejected_everywhere(bad);

	bad = make_htb_state();
	bad.body.htb.present = 2;
	require_htb_rejected_everywhere(bad);

	/* A holder for a briefcase that does not exist. */
	bad = make_htb_state();
	bad.body.htb.present = 0;
	require_htb_rejected_everywhere(bad);

	for (int axis = 0; axis < 3; ++axis) {
		bad = make_htb_state();
		bad.body.htb.pos[axis] = std::numeric_limits<float>::quiet_NaN();
		require_htb_rejected_everywhere(bad);

		bad = make_htb_state();
		bad.body.htb.pos[axis] = std::numeric_limits<float>::infinity();
		require_htb_rejected_everywhere(bad);
	}
}

namespace {

net_scenario_state_t make_htm_state()
{
	net_scenario_state_t state;
	std::memset(&state, 0, sizeof(state));
	state.scenario = NET_SCENARIO_STATE_HACKERCENTRAL;
	state.revision = 404;
	state.body.htm.uplink_holder = 2;
	state.body.htm.uplink_present = 1;
	state.body.htm.uplink_pos[0] = 10.0f;
	state.body.htm.uplink_pos[1] = -20.0f;
	state.body.htm.uplink_pos[2] = 30.5f;
	state.body.htm.dl_player = 2;
	state.body.htm.dl_terminal = 0;
	state.body.htm.player_in_range = 2;
	for (int i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; ++i) {
		state.body.htm.dltime240[i] = (u16)((i * 97) % (NET_SCENARIO_HTM_DOWNLOAD240 + 1));
		state.body.htm.numpoints[i] = (s16)(i % 4);
	}
	return state;
}

u32 write_raw_htm(u8 *bytes, u32 capacity, const net_scenario_state_t &state)
{
	netbuf raw{bytes, capacity, 0, 0, 0};
	netbufStartWrite(&raw);
	netbufWriteU8(&raw, state.scenario);
	netbufWriteU32(&raw, state.revision);
	netbufWriteS8(&raw, state.body.htm.uplink_holder);
	netbufWriteU8(&raw, state.body.htm.uplink_present);
	netbufWriteF32(&raw, state.body.htm.uplink_pos[0]);
	netbufWriteF32(&raw, state.body.htm.uplink_pos[1]);
	netbufWriteF32(&raw, state.body.htm.uplink_pos[2]);
	netbufWriteS8(&raw, state.body.htm.dl_player);
	netbufWriteS8(&raw, state.body.htm.dl_terminal);
	netbufWriteS8(&raw, state.body.htm.player_in_range);
	for (int i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; ++i) {
		netbufWriteU16(&raw, state.body.htm.dltime240[i]);
	}
	for (int i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; ++i) {
		netbufWriteS16(&raw, state.body.htm.numpoints[i]);
	}
	REQUIRE(raw.error == 0);
	return raw.wp;
}

void require_htm_rejected_everywhere(const net_scenario_state_t &bad)
{
	u8 encoded[512]{};
	netbuf write{encoded, sizeof(encoded), 0, 0, 0};
	netbufStartWrite(&write);
	REQUIRE(netScenarioStateValidate(&bad) == NET_SCENARIO_STATE_BAD_FIELD);
	REQUIRE_FALSE(netScenarioStatePayloadWrite(&write, &bad));
	REQUIRE(write.wp == 0);

	u8 raw[512]{};
	const u32 length = write_raw_htm(raw, sizeof(raw), bad);
	netbuf read{};
	netbufStartReadData(&read, raw, length);
	net_scenario_state_t output;
	std::memset(&output, 0x5a, sizeof(output));
	const net_scenario_state_t before = output;
	REQUIRE(netScenarioStatePayloadRead(&read, &output) == NET_SCENARIO_STATE_BAD_FIELD);
	REQUIRE(std::memcmp(&before, &output, sizeof(output)) == 0);
}

} // namespace

TEST_CASE("Scenario state wire round-trips Hacker Central state, every character slot included",
          "[net][scenario][wire][htm][T-NETWORKING-011]")
{
	const net_scenario_state_t downloading = make_htm_state();

	net_scenario_state_t idle = make_htm_state();
	idle.body.htm.uplink_holder = -1;
	idle.body.htm.dl_player = -1;
	idle.body.htm.dl_terminal = -1;
	idle.body.htm.player_in_range = -1;

	net_scenario_state_t edge = make_htm_state();
	edge.body.htm.dl_terminal = NET_SCENARIO_HTM_MAX_TERMINALS - 1;
	edge.body.htm.dltime240[NET_SCENARIO_STATE_MAX_CHRS - 1] = NET_SCENARIO_HTM_DOWNLOAD240;
	edge.body.htm.numpoints[0] = 32767;

	for (const net_scenario_state_t &input : {downloading, idle, edge}) {
		u8 bytes[512]{};
		netbuf write{bytes, sizeof(bytes), 0, 0, 0};
		netbufStartWrite(&write);
		REQUIRE(netScenarioStatePayloadWrite(&write, &input));
		REQUIRE(write.wp == netScenarioStatePayloadSize(NET_SCENARIO_STATE_HACKERCENTRAL));

		netbuf read{};
		netbufStartReadData(&read, bytes, write.wp);
		net_scenario_state_t output;
		std::memset(&output, 0x5a, sizeof(output));
		REQUIRE(netScenarioStatePayloadRead(&read, &output) == NET_SCENARIO_STATE_OK);
		REQUIRE(read.rp == write.wp);
		REQUIRE(output.body.htm.uplink_holder == input.body.htm.uplink_holder);
		REQUIRE(output.body.htm.uplink_present == input.body.htm.uplink_present);
		REQUIRE(output.body.htm.uplink_pos[2] == input.body.htm.uplink_pos[2]);
		REQUIRE(output.body.htm.dl_player == input.body.htm.dl_player);
		REQUIRE(output.body.htm.dl_terminal == input.body.htm.dl_terminal);
		REQUIRE(output.body.htm.player_in_range == input.body.htm.player_in_range);
		for (int i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; ++i) {
			REQUIRE(output.body.htm.dltime240[i] == input.body.htm.dltime240[i]);
			REQUIRE(output.body.htm.numpoints[i] == input.body.htm.numpoints[i]);
		}
	}
}

TEST_CASE("Scenario state wire rejects every truncated Hacker Central payload",
          "[net][scenario][wire][htm][T-NETWORKING-011]")
{
	const net_scenario_state_t input = make_htm_state();
	u8 bytes[512]{};
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
		REQUIRE(std::memcmp(&before, &output, sizeof(output)) == 0);
	}
}

TEST_CASE("Scenario state wire rejects each out-of-range Hacker Central field on both sides",
          "[net][scenario][wire][htm][T-NETWORKING-011]")
{
	net_scenario_state_t bad;

	bad = make_htm_state();
	bad.body.htm.uplink_holder = NET_SCENARIO_STATE_MAX_CHRS;
	require_htm_rejected_everywhere(bad);

	bad = make_htm_state();
	bad.body.htm.uplink_present = 0;
	require_htm_rejected_everywhere(bad);

	bad = make_htm_state();
	bad.body.htm.uplink_pos[1] = std::numeric_limits<float>::quiet_NaN();
	require_htm_rejected_everywhere(bad);

	bad = make_htm_state();
	bad.body.htm.dl_player = -2;
	require_htm_rejected_everywhere(bad);

	bad = make_htm_state();
	bad.body.htm.dl_terminal = NET_SCENARIO_HTM_MAX_TERMINALS;
	require_htm_rejected_everywhere(bad);

	/* A download without a terminal, and a terminal without a player. */
	bad = make_htm_state();
	bad.body.htm.dl_terminal = -1;
	require_htm_rejected_everywhere(bad);

	bad = make_htm_state();
	bad.body.htm.dl_player = -1;
	require_htm_rejected_everywhere(bad);

	bad = make_htm_state();
	bad.body.htm.player_in_range = NET_SCENARIO_STATE_MAX_CHRS;
	require_htm_rejected_everywhere(bad);

	bad = make_htm_state();
	bad.body.htm.dltime240[5] = NET_SCENARIO_HTM_DOWNLOAD240 + 1;
	require_htm_rejected_everywhere(bad);

	bad = make_htm_state();
	bad.body.htm.numpoints[NET_SCENARIO_STATE_MAX_CHRS - 1] = -1;
	require_htm_rejected_everywhere(bad);
}

namespace {

net_scenario_state_t make_ctc_state()
{
	net_scenario_state_t state;
	std::memset(&state, 0, sizeof(state));
	state.scenario = NET_SCENARIO_STATE_CAPTURETHECASE;
	state.revision = 9;
	const s8 teams[NET_SCENARIO_CTC_BASES] = { 3, 0, -1, -1 };
	for (int b = 0; b < NET_SCENARIO_CTC_BASES; ++b) {
		state.body.ctc.teamindexes[b] = teams[b];
		state.body.ctc.homepads[b] = b < 2 ? (s16)(100 + b) : (s16)-1;
		state.body.ctc.numspawnpads[b] = b < 2 ? 6 : 0;
		for (int j = 0; j < NET_SCENARIO_CTC_SPAWNPADS; ++j) {
			state.body.ctc.spawnpads[b][j] = b < 2 ? (s16)(200 + b * 10 + j) : (s16)-1;
		}
		state.body.ctc.holders[b] = b == 1 ? 4 : -1;
		state.body.ctc.present[b] = b < 2 ? 1 : 0;
		state.body.ctc.pos[b][0] = b * 100.0f;
		state.body.ctc.pos[b][1] = -5.0f;
		state.body.ctc.pos[b][2] = b * -50.0f;
	}
	return state;
}

u32 write_raw_ctc(u8 *bytes, u32 capacity, const net_scenario_state_t &state)
{
	netbuf raw{bytes, capacity, 0, 0, 0};
	netbufStartWrite(&raw);
	netbufWriteU8(&raw, state.scenario);
	netbufWriteU32(&raw, state.revision);
	for (int b = 0; b < NET_SCENARIO_CTC_BASES; ++b) {
		netbufWriteS8(&raw, state.body.ctc.teamindexes[b]);
		netbufWriteS16(&raw, state.body.ctc.homepads[b]);
		netbufWriteU8(&raw, state.body.ctc.numspawnpads[b]);
		for (int j = 0; j < NET_SCENARIO_CTC_SPAWNPADS; ++j) {
			netbufWriteS16(&raw, state.body.ctc.spawnpads[b][j]);
		}
	}
	for (int b = 0; b < NET_SCENARIO_CTC_BASES; ++b) {
		netbufWriteS8(&raw, state.body.ctc.holders[b]);
		netbufWriteU8(&raw, state.body.ctc.present[b]);
		netbufWriteF32(&raw, state.body.ctc.pos[b][0]);
		netbufWriteF32(&raw, state.body.ctc.pos[b][1]);
		netbufWriteF32(&raw, state.body.ctc.pos[b][2]);
	}
	REQUIRE(raw.error == 0);
	return raw.wp;
}

void require_ctc_rejected_everywhere(const net_scenario_state_t &bad)
{
	u8 encoded[512]{};
	netbuf write{encoded, sizeof(encoded), 0, 0, 0};
	netbufStartWrite(&write);
	REQUIRE(netScenarioStateValidate(&bad) == NET_SCENARIO_STATE_BAD_FIELD);
	REQUIRE_FALSE(netScenarioStatePayloadWrite(&write, &bad));
	REQUIRE(write.wp == 0);

	u8 raw[512]{};
	const u32 length = write_raw_ctc(raw, sizeof(raw), bad);
	netbuf read{};
	netbufStartReadData(&read, raw, length);
	net_scenario_state_t output;
	std::memset(&output, 0x5a, sizeof(output));
	const net_scenario_state_t before = output;
	REQUIRE(netScenarioStatePayloadRead(&read, &output) == NET_SCENARIO_STATE_BAD_FIELD);
	REQUIRE(std::memcmp(&before, &output, sizeof(output)) == 0);
}

} // namespace

TEST_CASE("Scenario state wire round-trips Capture the Case bases and cases",
          "[net][scenario][wire][ctc][T-NETWORKING-011]")
{
	const net_scenario_state_t input = make_ctc_state();
	u8 bytes[512]{};
	netbuf write{bytes, sizeof(bytes), 0, 0, 0};
	netbufStartWrite(&write);
	REQUIRE(netScenarioStatePayloadWrite(&write, &input));
	REQUIRE(write.wp == netScenarioStatePayloadSize(NET_SCENARIO_STATE_CAPTURETHECASE));

	netbuf read{};
	netbufStartReadData(&read, bytes, write.wp);
	net_scenario_state_t output;
	std::memset(&output, 0x5a, sizeof(output));
	REQUIRE(netScenarioStatePayloadRead(&read, &output) == NET_SCENARIO_STATE_OK);
	REQUIRE(read.rp == write.wp);
	for (int b = 0; b < NET_SCENARIO_CTC_BASES; ++b) {
		REQUIRE(output.body.ctc.teamindexes[b] == input.body.ctc.teamindexes[b]);
		REQUIRE(output.body.ctc.homepads[b] == input.body.ctc.homepads[b]);
		REQUIRE(output.body.ctc.numspawnpads[b] == input.body.ctc.numspawnpads[b]);
		for (int j = 0; j < NET_SCENARIO_CTC_SPAWNPADS; ++j) {
			REQUIRE(output.body.ctc.spawnpads[b][j] == input.body.ctc.spawnpads[b][j]);
		}
		REQUIRE(output.body.ctc.holders[b] == input.body.ctc.holders[b]);
		REQUIRE(output.body.ctc.present[b] == input.body.ctc.present[b]);
		REQUIRE(output.body.ctc.pos[b][0] == input.body.ctc.pos[b][0]);
		REQUIRE(output.body.ctc.pos[b][2] == input.body.ctc.pos[b][2]);
	}
}

TEST_CASE("Scenario state wire rejects every truncated Capture the Case payload",
          "[net][scenario][wire][ctc][T-NETWORKING-011]")
{
	const net_scenario_state_t input = make_ctc_state();
	u8 bytes[512]{};
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
		REQUIRE(std::memcmp(&before, &output, sizeof(output)) == 0);
	}
}

TEST_CASE("Scenario state wire rejects each out-of-range Capture the Case field on both sides",
          "[net][scenario][wire][ctc][T-NETWORKING-011]")
{
	net_scenario_state_t bad;

	bad = make_ctc_state();
	bad.body.ctc.teamindexes[2] = NET_SCENARIO_CTC_BASES;
	require_ctc_rejected_everywhere(bad);

	/* Two teams on one base. */
	bad = make_ctc_state();
	bad.body.ctc.teamindexes[2] = 3;
	require_ctc_rejected_everywhere(bad);

	bad = make_ctc_state();
	bad.body.ctc.homepads[0] = -2;
	require_ctc_rejected_everywhere(bad);

	bad = make_ctc_state();
	bad.body.ctc.numspawnpads[1] = NET_SCENARIO_CTC_SPAWNPADS + 1;
	require_ctc_rejected_everywhere(bad);

	/* A listed respawn pad that is not a pad, and a pad past the count. */
	bad = make_ctc_state();
	bad.body.ctc.spawnpads[0][3] = -1;
	require_ctc_rejected_everywhere(bad);

	bad = make_ctc_state();
	bad.body.ctc.spawnpads[3][0] = 77;
	require_ctc_rejected_everywhere(bad);

	bad = make_ctc_state();
	bad.body.ctc.holders[0] = NET_SCENARIO_STATE_MAX_CHRS;
	require_ctc_rejected_everywhere(bad);

	bad = make_ctc_state();
	bad.body.ctc.present[1] = 0;
	require_ctc_rejected_everywhere(bad);

	bad = make_ctc_state();
	bad.body.ctc.pos[2][1] = std::numeric_limits<float>::infinity();
	require_ctc_rejected_everywhere(bad);
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
