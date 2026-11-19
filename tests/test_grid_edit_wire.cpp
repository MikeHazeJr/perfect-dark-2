#include "catch.hpp"

#include "net/grid_edit_wire.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

/* The C netbuf header includes the game's C bool macro. */
struct netbuf {
	u8 *data;
	u32 size;
	u32 rp;
	u32 wp;
	u32 error;
};

namespace {

constexpr u32 kPrefix = 3;
constexpr u32 kRoom = 0;
constexpr u32 kGeneration = 1;
constexpr u32 kSequence = 9;
constexpr u32 kExpectedRevision = 17;
constexpr u32 kOperation = 25;
constexpr u32 kPayload = 26;
constexpr u32 kPosition = kPayload + 10;
constexpr u32 kLook = kPosition + 3 * 4;
constexpr grid_edit_wire_identity_t kIdentity = {3, 0x0123456789abcdefull};

grid_edit_command_t spawnMove()
{
	grid_edit_command_t command = {};
	command.sequence = 0x1020304050607080ull;
	command.expected_revision = 0x1122334455667788ull;
	command.operation = GRID_EDIT_MOVE_SPAWN;
	std::strcpy(command.data.move_spawn.spawn_id, "spawn_0042");
	command.data.move_spawn.position[0] = 20.25f;
	command.data.move_spawn.position[1] = -12.5f;
	command.data.move_spawn.position[2] = -0.0f;
	command.data.move_spawn.look[2] = -4.0f;
	return command;
}

grid_edit_command_t ambientLight()
{
	grid_edit_command_t command = {};
	command.sequence = 0x1020304050607080ull;
	command.expected_revision = 0x1122334455667788ull;
	command.operation = GRID_EDIT_SET_AMBIENT_LIGHT;
	command.data.ambient_light = {1, 143};
	return command;
}

void checkRejectedCommand(netbuf &wire)
{
	const netbuf before = wire;
	grid_edit_wire_identity_t identity = {7, 471};
	auto command = ambientLight();
	CHECK_FALSE(gridEditCommandRead(&wire, &identity, &command));
	CHECK(wire.rp == before.rp);
	CHECK(wire.wp == before.wp);
	CHECK(wire.error == before.error);
	CHECK(identity.room_id == 7);
	CHECK(identity.generation == 471);
	CHECK(command.sequence == ambientLight().sequence);
	CHECK(command.expected_revision == ambientLight().expected_revision);
	CHECK(command.operation == GRID_EDIT_SET_AMBIENT_LIGHT);
	CHECK(command.data.ambient_light.brightness == 143);
}

void checkRejectedDelta(netbuf &wire)
{
	const netbuf before = wire;
	grid_edit_wire_identity_t identity = {7, 471};
	grid_edit_delta_t delta = {99, 57, ambientLight()};
	CHECK_FALSE(gridEditDeltaRead(&wire, &identity, &delta));
	CHECK(wire.rp == before.rp);
	CHECK(wire.wp == before.wp);
	CHECK(wire.error == before.error);
	CHECK(identity.room_id == 7);
	CHECK(identity.generation == 471);
	CHECK(delta.revision == 99);
	CHECK(delta.author_id == 57);
	CHECK(delta.command.operation == GRID_EDIT_SET_AMBIENT_LIGHT);
	CHECK(delta.command.data.ambient_light.brightness == 143);
}

} // namespace

TEST_CASE("Grid commands use an exact little-endian payload without a sender field", "[net][grid-edit][wire]")
{
	std::array<u8, kPrefix + GRID_EDIT_COMMAND_WIRE_MAX + 1> bytes;
	bytes.fill(0xa5);
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), kPrefix, kPrefix, 0};
	const auto sent = ambientLight();
	REQUIRE(gridEditCommandWrite(&wire, &kIdentity, &sent));
	constexpr std::array<u8, 28> payload = {
		0x03, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
		0x80, 0x70, 0x60, 0x50, 0x40, 0x30, 0x20, 0x10,
		0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
		GRID_EDIT_SET_AMBIENT_LIGHT, 0x01, 0x8f
	};
	REQUIRE(wire.wp == kPrefix + payload.size());
	CHECK(std::equal(payload.begin(), payload.end(), bytes.begin() + kPrefix));
	CHECK(bytes[kPrefix - 1] == 0xa5);
	CHECK(bytes[wire.wp] == 0xa5);

	grid_edit_wire_identity_t identity = {};
	grid_edit_command_t received = {};
	REQUIRE(gridEditCommandRead(&wire, &identity, &received));
	CHECK(wire.rp == wire.wp);
	CHECK(identity.room_id == kIdentity.room_id);
	CHECK(identity.generation == kIdentity.generation);
	CHECK(received.sequence == sent.sequence);
	CHECK(received.expected_revision == sent.expected_revision);
	REQUIRE(received.operation == GRID_EDIT_SET_AMBIENT_LIGHT);
	CHECK(received.data.ambient_light.enabled == 1);
	CHECK(received.data.ambient_light.brightness == 143);
}

TEST_CASE("Grid spawn move payload preserves stable ID, coordinates and exact look", "[net][grid-edit][wire]")
{
	std::array<u8, kPrefix + GRID_EDIT_COMMAND_WIRE_MAX> bytes = {};
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), kPrefix, kPrefix, 0};
	const auto sent = spawnMove();
	REQUIRE(gridEditCommandWrite(&wire, &kIdentity, &sent));
	REQUIRE(wire.wp == bytes.size());

	grid_edit_wire_identity_t identity = {};
	grid_edit_command_t received = {};
	REQUIRE(gridEditCommandRead(&wire, &identity, &received));
	CHECK(wire.rp == wire.wp);
	CHECK(identity.generation == kIdentity.generation);
	CHECK(received.sequence == sent.sequence);
	CHECK(received.expected_revision == sent.expected_revision);
	REQUIRE(received.operation == GRID_EDIT_MOVE_SPAWN);
	const auto &move = received.data.move_spawn;
	CHECK(std::strcmp(move.spawn_id, "spawn_0042") == 0);
	CHECK(move.position[0] == 20.25f);
	CHECK(move.position[1] == -12.5f);
	CHECK(move.position[2] == 0.0f);
	CHECK(std::signbit(move.position[2]));
	CHECK(move.look[2] == -4.0f);
}

TEST_CASE("Grid command failures preserve packet memory, cursors and outputs", "[net][grid-edit][wire]")
{
	std::array<u8, kPrefix + GRID_EDIT_COMMAND_WIRE_MAX + 1> bytes;
	bytes.fill(0xa5);
	const auto unchanged = bytes;
	const auto sent = spawnMove();
	for (u32 capacity = 0; capacity < GRID_EDIT_COMMAND_WIRE_MAX; capacity++) {
		CAPTURE(capacity);
		netbuf write = {bytes.data(), kPrefix + capacity, kPrefix, kPrefix, 0};
		CHECK_FALSE(gridEditCommandWrite(&write, &kIdentity, &sent));
		CHECK(write.wp == kPrefix);
		CHECK(write.error == 0);
		CHECK(bytes == unchanged);
	}
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), kPrefix, kPrefix, 0};
	REQUIRE(gridEditCommandWrite(&wire, &kIdentity, &sent));
	const auto encoded = bytes;
	for (u32 size = 0; size < GRID_EDIT_COMMAND_WIRE_MAX; size++) {
		CAPTURE(size);
		netbuf truncated = {bytes.data(), static_cast<u32>(bytes.size()), kPrefix, kPrefix + size, 0};
		checkRejectedCommand(truncated);
	}
	netbuf extra = {bytes.data(), static_cast<u32>(bytes.size()), kPrefix, static_cast<u32>(bytes.size()), 0};
	checkRejectedCommand(extra);

	SECTION("room sentinel") {
		bytes[kPrefix + kRoom] = 0xff;
	}
	SECTION("zero generation") {
		std::fill_n(bytes.begin() + kPrefix + kGeneration, 8, 0);
	}
	SECTION("zero command sequence") {
		std::fill_n(bytes.begin() + kPrefix + kSequence, 8, 0);
	}
	SECTION("zero expected revision") {
		std::fill_n(bytes.begin() + kPrefix + kExpectedRevision, 8, 0);
	}
	SECTION("unrecognized operation") {
		bytes[kPrefix + kOperation] = 0xff;
	}
	SECTION("embedded NUL in spawn ID") {
		bytes[kPrefix + kPayload + 6] = 0;
	}
	SECTION("infinite position") {
		const std::array<u8, 4> infinity = {0, 0, 0x80, 0x7f};
		std::copy(infinity.begin(), infinity.end(), bytes.begin() + kPrefix + kPosition);
	}
	SECTION("NaN look") {
		const std::array<u8, 4> nan = {0, 0, 0xc0, 0x7f};
		std::copy(nan.begin(), nan.end(), bytes.begin() + kPrefix + kLook);
	}
	SECTION("degenerate look") {
		std::fill_n(bytes.begin() + kPrefix + kLook, 12, 0);
	}
	REQUIRE(bytes != encoded);
	checkRejectedCommand(wire);
}

TEST_CASE("Grid command writers validate native edit values before writing any bytes", "[net][grid-edit][wire]")
{
	std::array<u8, kPrefix + GRID_EDIT_COMMAND_WIRE_MAX> bytes;
	bytes.fill(0xa5);
	const auto unchanged = bytes;
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), kPrefix, kPrefix, 0};
	auto identity = kIdentity;
	auto command = spawnMove();
	SECTION("lounge is no Grid room") {
		identity.room_id = 0xff;
	}
	SECTION("zero generation") {
		identity.generation = 0;
	}
	SECTION("noncanonical spawn ID") {
		std::memset(command.data.move_spawn.spawn_id, 'a', sizeof(command.data.move_spawn.spawn_id));
	}
	SECTION("nonfinite position") {
		command.data.move_spawn.position[0] = std::numeric_limits<f32>::infinity();
	}
	SECTION("near-zero look") {
		command.data.move_spawn.look[2] = 1e-8f;
	}
	SECTION("nonboolean ambient flag") {
		command = ambientLight();
		command.data.ambient_light.enabled = 2;
	}
	SECTION("out-of-range brightness") {
		command = ambientLight();
		command.data.ambient_light.brightness = 256;
	}
	CHECK_FALSE(gridEditCommandWrite(&wire, &identity, &command));
	CHECK(wire.wp == kPrefix);
	CHECK(wire.error == 0);
	CHECK(bytes == unchanged);
}

TEST_CASE("Grid ambient-light reader verifies its boolean flag", "[net][grid-edit][wire]")
{
	std::array<u8, GRID_EDIT_COMMAND_WIRE_MAX> bytes = {};
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), 0, 0, 0};
	const auto command = ambientLight();
	REQUIRE(gridEditCommandWrite(&wire, &kIdentity, &command));
	bytes[kPayload] = 2;
	checkRejectedCommand(wire);
}

TEST_CASE("Grid authoritative deltas bind the author and exact next committed revision", "[net][grid-edit][wire]")
{
	std::array<u8, kPrefix + GRID_EDIT_DELTA_WIRE_MAX + 1> bytes;
	bytes.fill(0xa5);
	const auto unchanged = bytes;
	const auto command = spawnMove();
	const grid_edit_delta_t sent = {command.expected_revision + 1, 0xfedcba9876543210ull, command};

	netbuf too_small = {bytes.data(), static_cast<u32>(bytes.size() - 2), kPrefix, kPrefix, 0};
	CHECK_FALSE(gridEditDeltaWrite(&too_small, &kIdentity, &sent));
	CHECK(too_small.wp == kPrefix);
	CHECK(too_small.error == 0);
	CHECK(bytes == unchanged);

	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), kPrefix, kPrefix, 0};
	auto invalid = sent;
	invalid.author_id = 0;
	CHECK_FALSE(gridEditDeltaWrite(&wire, &kIdentity, &invalid));
	invalid = sent;
	invalid.revision++;
	CHECK_FALSE(gridEditDeltaWrite(&wire, &kIdentity, &invalid));
	CHECK(wire.wp == kPrefix);
	CHECK(wire.error == 0);
	CHECK(bytes == unchanged);
	REQUIRE(gridEditDeltaWrite(&wire, &kIdentity, &sent));
	REQUIRE(wire.wp == bytes.size() - 1);
	CHECK(bytes[wire.wp] == 0xa5);

	for (u32 size = 0; size < GRID_EDIT_DELTA_WIRE_MAX; size++) {
		CAPTURE(size);
		netbuf truncated = {bytes.data(), static_cast<u32>(bytes.size()), kPrefix, kPrefix + size, 0};
		checkRejectedDelta(truncated);
	}
	checkRejectedCommand(wire);
	auto readahead = wire;
	readahead.wp++;
	checkRejectedDelta(readahead);

	const auto encoded = bytes;
	std::fill_n(bytes.begin() + kPrefix + 9 + 8, 8, 0);
	checkRejectedDelta(wire);
	bytes = encoded;
	bytes[kPrefix + 9]++;
	checkRejectedDelta(wire);
	bytes = encoded;

	grid_edit_wire_identity_t identity = {};
	grid_edit_delta_t received = {};
	REQUIRE(gridEditDeltaRead(&wire, &identity, &received));
	CHECK(wire.rp == wire.wp);
	CHECK(identity.room_id == kIdentity.room_id);
	CHECK(identity.generation == kIdentity.generation);
	CHECK(received.revision == sent.revision);
	CHECK(received.author_id == sent.author_id);
	CHECK(received.command.sequence == command.sequence);
	CHECK(received.command.expected_revision == command.expected_revision);
	REQUIRE(received.command.operation == GRID_EDIT_MOVE_SPAWN);
	CHECK(std::strcmp(received.command.data.move_spawn.spawn_id, "spawn_0042") == 0);
}
