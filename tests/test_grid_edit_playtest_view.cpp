#include "catch.hpp"

#include "net/grid_edit_playtest_view.h"
#include "net/grid_edit_room.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>

/* The C netbuf header includes the game's C bool macro. */
struct netbuf {
	u8 *data;
	u32 size;
	u32 rp;
	u32 wp;
	u32 error;
};

namespace {

constexpr u32 kPrefix = 1;
constexpr u32 kRoomCount = kPrefix + 17;
constexpr u32 kActorCount = kRoomCount + 1;
constexpr u32 kViewHeader = 19;
constexpr u32 kCursorVisible = kPrefix + 9;
constexpr u32 kCursorX = kCursorVisible + 1;
constexpr grid_edit_wire_identity_t kIdentity = {3, 0x0123456789abcdefull};

grid_playtest_view_t exampleView()
{
	grid_playtest_view_t view = {};
	view.epoch = 0x12345678;
	view.serial = 19;
	view.room_count = GRID_PLAYTEST_VIEW_MAX_ROOMS;
	view.actor_count = GRID_PLAYTEST_VIEW_MAX_ACTORS;
	for (u16 i = 0; i < view.room_count; i++) {
		view.rooms[i] = {i, static_cast<u16>(i + 1), static_cast<u16>(i + 2), 65535};
	}
	for (u16 i = 0; i < view.actor_count; i++) {
		view.actors[i] = {i, 65535, static_cast<u8>(i % 2)};
	}
	return view;
}

void checkViewRefused(netbuf src)
{
	const auto before = src;
	grid_edit_wire_identity_t identity = {15, 49};
	auto view = exampleView();
	CHECK_FALSE(gridEditPlaytestViewRead(&src, &identity, &view));
	CHECK(src.rp == before.rp);
	CHECK(src.wp == before.wp);
	CHECK(src.error == before.error);
	CHECK(identity.room_id == 15);
	CHECK(identity.generation == 49);
	CHECK(view.epoch == 0x12345678);
	CHECK(view.serial == 19);
	CHECK(view.room_count == GRID_PLAYTEST_VIEW_MAX_ROOMS);
	CHECK(view.actor_count == GRID_PLAYTEST_VIEW_MAX_ACTORS);
	CHECK(view.actors[GRID_PLAYTEST_VIEW_MAX_ACTORS - 1].bot == 1);
}

void checkCursorRefused(netbuf src)
{
	const auto before = src;
	grid_edit_wire_identity_t identity = {15, 49};
	u8 visible = 1;
	f32 x = -3.0f, z = 4.0f;
	CHECK_FALSE(gridEditPlaytestCursorRead(&src, &identity, &visible, &x, &z));
	CHECK(src.rp == before.rp);
	CHECK(src.wp == before.wp);
	CHECK(src.error == before.error);
	CHECK(identity.room_id == 15);
	CHECK(identity.generation == 49);
	CHECK(visible == 1);
	CHECK(x == -3.0f);
	CHECK(z == 4.0f);
}

} // namespace

TEST_CASE("Grid playtest views round trip bounded presentation or an exact clear", "[grid-edit-playtest-view]")
{
	const bool clear = GENERATE(false, true);
	auto sent = exampleView();
	if (clear) sent.room_count = sent.actor_count = 0;
	std::array<u8, kPrefix + GRID_PLAYTEST_VIEW_WIRE_MAX + 1> bytes;
	bytes.fill(0xa5);
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), kPrefix, kPrefix, 0};
	REQUIRE(gridEditPlaytestViewWrite(&wire, &kIdentity, &sent));
	CHECK(wire.wp == kPrefix + (clear ? kViewHeader : GRID_PLAYTEST_VIEW_WIRE_MAX));
	CHECK(bytes[0] == 0xa5);
	CHECK(bytes[wire.wp] == 0xa5);
	CHECK(bytes[kPrefix] == 3);
	CHECK(bytes[kPrefix + 1] == 0xef);
	CHECK(bytes[kPrefix + 9] == 0x78);
	CHECK(bytes[kPrefix + 12] == 0x12);
	grid_edit_wire_identity_t identity = {};
	auto received = exampleView();
	REQUIRE(gridEditPlaytestViewRead(&wire, &identity, &received));
	CHECK(wire.rp == wire.wp);
	CHECK(identity.room_id == kIdentity.room_id);
	CHECK(identity.generation == kIdentity.generation);
	CHECK(received.epoch == sent.epoch);
	CHECK(received.serial == sent.serial);
	REQUIRE(received.room_count == sent.room_count);
	REQUIRE(received.actor_count == sent.actor_count);
	for (u32 i = 0; i < sent.room_count; i++) {
		CHECK(received.rooms[i].min_x == sent.rooms[i].min_x);
		CHECK(received.rooms[i].min_z == sent.rooms[i].min_z);
		CHECK(received.rooms[i].max_x == sent.rooms[i].max_x);
		CHECK(received.rooms[i].max_z == sent.rooms[i].max_z);
	}
	for (u32 i = 0; i < sent.actor_count; i++) {
		CHECK(received.actors[i].x == sent.actors[i].x);
		CHECK(received.actors[i].z == sent.actors[i].z);
		CHECK(received.actors[i].bot == sent.actors[i].bot);
	}
	if (clear) {
		CHECK(received.rooms[0].max_z == 0);
		CHECK(received.actors[0].z == 0);
	}
}

TEST_CASE("Grid playtest view errors preserve the buffer and prior view", "[grid-edit-playtest-view]")
{
	auto view = exampleView();
	std::array<u8, kPrefix + GRID_PLAYTEST_VIEW_WIRE_MAX + 1> bytes;
	bytes.fill(0xa5);
	const auto original = bytes;
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), kPrefix, kPrefix, 0};
	auto short_write = wire;
	short_write.size = kPrefix + GRID_PLAYTEST_VIEW_WIRE_MAX - 1;
	CHECK_FALSE(gridEditPlaytestViewWrite(&short_write, &kIdentity, &view));
	CHECK(short_write.wp == kPrefix);
	CHECK(short_write.error == 0);
	CHECK(bytes == original);
	REQUIRE(gridEditPlaytestViewWrite(&wire, &kIdentity, &view));
	for (u32 length : {0u, kViewHeader - 1, GRID_PLAYTEST_VIEW_WIRE_MAX - 1,
			GRID_PLAYTEST_VIEW_WIRE_MAX + 1}) {
		auto altered = wire;
		altered.wp = kPrefix + length;
		checkViewRefused(altered);
	}
	SECTION("invalid room identity") {
		bytes[kPrefix] = 0xff;
	}
	SECTION("zero epoch") {
		std::fill_n(bytes.begin() + kPrefix + 9, 4, 0);
	}
	SECTION("zero serial") {
		std::fill_n(bytes.begin() + kPrefix + 13, 4, 0);
	}
	SECTION("excess room count") {
		bytes[kRoomCount]++;
	}
	SECTION("half-clear view") {
		bytes[kActorCount] = 0;
	}
	SECTION("degenerate room") {
		bytes[kActorCount + 5] = bytes[kActorCount + 6] = 0;
	}
	SECTION("non-boolean bot flag") {
		bytes[wire.wp - 1] = 2;
	}
	checkViewRefused(wire);

	auto invalid = wire;
	invalid.wp = kPrefix;
	const auto before = bytes;
	view.actors[0].bot = 2;
	CHECK_FALSE(gridEditPlaytestViewWrite(&invalid, &kIdentity, &view));
	CHECK(invalid.wp == kPrefix);
	CHECK(bytes == before);
}

TEST_CASE("Grid exact world frames extend the bounded legacy presentation", "[grid-edit-playtest-view]")
{
	const u64 generation = GENERATE(0ull, 0xcafefd3022156641ull);
	auto sent = exampleView();
	sent.has_world_bounds = 1;
	sent.layout_generation = generation;
	sent.min_x = -320.5f;
	sent.min_z = 500.75f;
	sent.max_x = 2079.5f;
	sent.max_z = 3000.75f;
	std::array<u8, kPrefix + GRID_PLAYTEST_WORLD_VIEW_WIRE_MAX + 1> bytes;
	bytes.fill(0x5a);
	const auto blank = bytes;
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), kPrefix, kPrefix, 0};
	SECTION("writer refuses invalid bounds or capacity without altering the packet") {
		auto short_write = wire;
		short_write.size -= 2;
		CHECK_FALSE(gridEditPlaytestWorldViewWrite(&short_write, &kIdentity, &sent));
		CHECK(short_write.wp == kPrefix);
		sent.max_z = std::numeric_limits<f32>::max();
		CHECK_FALSE(gridEditPlaytestWorldViewWrite(&wire, &kIdentity, &sent));
		sent.max_z = 3000.75f;
		sent.max_x = sent.min_x;
		CHECK_FALSE(gridEditPlaytestWorldViewWrite(&wire, &kIdentity, &sent));
		CHECK(wire.wp == kPrefix);
		CHECK(bytes == blank);
	}
	SECTION("legacy prefix is byte identical and the footer is exact") {
		auto legacy = bytes;
		auto legacy_wire = wire;
		legacy_wire.data = legacy.data();
		REQUIRE(gridEditPlaytestViewWrite(&legacy_wire, &kIdentity, &sent));
		REQUIRE(gridEditPlaytestWorldViewWrite(&wire, &kIdentity, &sent));
		CHECK(wire.wp == kPrefix + GRID_PLAYTEST_WORLD_VIEW_WIRE_MAX);
		CHECK(std::equal(legacy.cbegin(), legacy.cbegin() + legacy_wire.wp, bytes.cbegin()));
		CHECK(bytes[legacy_wire.wp] == GRID_PLAYTEST_WORLD_VIEW_VERSION);
		CHECK(bytes[wire.wp] == 0x5a);

		auto view = exampleView();
		grid_edit_wire_identity_t identity = {15, 49};
		auto unread = wire;
		CHECK_FALSE(gridEditPlaytestViewRead(&unread, &identity, &view));
		CHECK_FALSE(gridEditPlaytestWorldViewRead(&legacy_wire, &identity, &view));
		CHECK(unread.rp == kPrefix);
		CHECK(legacy_wire.rp == kPrefix);
		CHECK(identity.generation == 49);
		CHECK_FALSE(view.has_world_bounds);

		const auto correct = bytes;
		const auto expected_size = wire.wp;
		const auto refuse = [&]() {
			CHECK_FALSE(gridEditPlaytestWorldViewRead(&wire, &identity, &view));
			CHECK(wire.rp == kPrefix);
			CHECK(wire.error == 0);
			CHECK(identity.room_id == 15);
			CHECK(identity.generation == 49);
			CHECK_FALSE(view.has_world_bounds);
		};
		for (u32 length = kPrefix; length < expected_size; length++) {
			wire.wp = length;
			refuse();
		}
		wire.wp = expected_size + 1;
		refuse();
		wire.wp = expected_size;
		bytes[legacy_wire.wp] = 0xff;
		refuse();
		bytes = correct;
		const auto first_bound = legacy_wire.wp + 1 + sizeof(u64);
		bytes[first_bound] = bytes[first_bound + 1] = 0;
		bytes[first_bound + 2] = 0x80;
		bytes[first_bound + 3] = 0x7f;
		refuse();
		bytes = correct;

		REQUIRE(gridEditPlaytestWorldViewRead(&wire, &identity, &view));
		CHECK(wire.rp == expected_size);
		CHECK(identity.generation == kIdentity.generation);
		CHECK(view.has_world_bounds == 1);
		CHECK(view.epoch == sent.epoch);
		CHECK(view.layout_generation == generation);
		CHECK(view.min_x == sent.min_x);
		CHECK(view.min_z == sent.min_z);
		CHECK(view.max_x == sent.max_x);
		CHECK(view.max_z == sent.max_z);
		grid_playtest_target_t matching = {view.epoch, view.layout_generation};
		CHECK(gridEditPlaytestTargetMatchesView(&matching, &view));
		matching.layout_generation++;
		CHECK_FALSE(gridEditPlaytestTargetMatchesView(&matching, &view));
		matching.layout_generation--;
		matching.epoch++;
		CHECK_FALSE(gridEditPlaytestTargetMatchesView(&matching, &view));
		matching.epoch--;
		view.has_world_bounds = 0;
		CHECK_FALSE(gridEditPlaytestTargetMatchesView(&matching, &view));
	}
}

TEST_CASE("Grid live map targets wrap only the existing exact placement commands", "[grid-edit-playtest-view]")
{
	const bool decoration = GENERATE(false, true);
	grid_edit_command_t sent = {};
	sent.sequence = 2;
	sent.expected_revision = 3;
	if (decoration) {
		sent.operation = GRID_EDIT_MOVE_DECORATION;
		std::strcpy(sent.data.move_decoration.record_id, "setup_0017");
		sent.data.move_decoration.x = -20.5f;
		sent.data.move_decoration.z = 7.25f;
	} else {
		sent.operation = GRID_EDIT_MOVE_SPAWN;
		std::strcpy(sent.data.move_spawn.spawn_id, "spawn_0000");
		sent.data.move_spawn.position[0] = -20.5f;
		sent.data.move_spawn.position[1] = 13.75f;
		sent.data.move_spawn.position[2] = 7.25f;
		sent.data.move_spawn.look[2] = 1.0f;
	}
	const grid_playtest_target_t stamp = {0x01234567, 0xfedcba9876543210ull};
	std::array<u8, kPrefix + GRID_PLAYTEST_TARGET_COMMAND_WIRE_MAX + 1> bytes;
	bytes.fill(0xa5);
	const auto blank = bytes;
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), kPrefix, kPrefix, 0};
	auto invalid = sent;
	invalid.operation = GRID_EDIT_SET_AMBIENT_LIGHT;
	CHECK_FALSE(gridEditPlaytestTargetCommandWrite(&wire, &kIdentity, &stamp, &invalid));
	auto zero_epoch = stamp;
	zero_epoch.epoch = 0;
	CHECK_FALSE(gridEditPlaytestTargetCommandWrite(&wire, &kIdentity, &zero_epoch, &sent));
	CHECK(wire.wp == kPrefix);
	CHECK(bytes == blank);

	REQUIRE(gridEditPlaytestTargetCommandWrite(&wire, &kIdentity, &stamp, &sent));
	CHECK(bytes[kPrefix] == GRID_PLAYTEST_TARGET_COMMAND_MARKER);
	CHECK(bytes[kPrefix + 2] == 0x67);
	CHECK(bytes[wire.wp] == 0xa5);
	const auto expected_size = wire.wp;
	grid_edit_wire_identity_t identity = {15, 49};
	grid_playtest_target_t decoded = {13, 17};
	grid_edit_command_t received = {};
	received.sequence = 11;
	auto direct = wire;
	CHECK_FALSE(gridEditCommandRead(&direct, &identity, &received));
	CHECK(direct.rp == kPrefix);
	const auto refuse = [&]() {
		CHECK_FALSE(gridEditPlaytestTargetCommandRead(&wire, &identity, &decoded, &received));
		CHECK(wire.rp == kPrefix);
		CHECK(wire.error == 0);
		CHECK(identity.generation == 49);
		CHECK(decoded.epoch == 13);
		CHECK(decoded.layout_generation == 17);
		CHECK(received.sequence == 11);
	};
	for (u32 length = kPrefix; length < expected_size; length++) {
		wire.wp = length;
		refuse();
	}
	wire.wp = expected_size + 1;
	refuse();
	wire.wp = expected_size;
	bytes[kPrefix + 1] = 0xff;
	refuse();
	bytes[kPrefix + 1] = GRID_PLAYTEST_WORLD_VIEW_VERSION;

	REQUIRE(gridEditPlaytestTargetCommandRead(&wire, &identity, &decoded, &received));
	CHECK(wire.rp == wire.wp);
	CHECK(identity.generation == kIdentity.generation);
	CHECK(decoded.epoch == stamp.epoch);
	CHECK(decoded.layout_generation == stamp.layout_generation);
	CHECK(gridEditCommandMatches(&received, &sent));
}

TEST_CASE("Grid playtest cursor includes finite world positions only while visible", "[grid-edit-playtest-view]")
{
	const auto infinity = std::numeric_limits<f32>::infinity();
	std::array<u8, kPrefix + GRID_PLAYTEST_CURSOR_WIRE_MAX + 1> bytes;
	bytes.fill(0xa5);
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), kPrefix, kPrefix, 0};
	const auto original = bytes;
	CHECK_FALSE(gridEditPlaytestCursorWrite(&wire, &kIdentity, 1, infinity, 50.0f));
	CHECK_FALSE(gridEditPlaytestCursorWrite(&wire, &kIdentity, 2, 1.0f, 50.0f));
	CHECK(wire.wp == kPrefix);
	CHECK(wire.error == 0);
	CHECK(bytes == original);

	REQUIRE(gridEditPlaytestCursorWrite(&wire, &kIdentity, 1, -500.25f, 150.5f));
	REQUIRE(wire.wp == kPrefix + GRID_PLAYTEST_CURSOR_WIRE_MAX);
	auto truncated = wire;
	truncated.wp--;
	checkCursorRefused(truncated);
	auto trailing = wire;
	trailing.wp++;
	checkCursorRefused(trailing);
	bytes[kCursorVisible] = 2;
	checkCursorRefused(wire);
	bytes[kCursorVisible] = 0;
	checkCursorRefused(wire); /* A hidden cursor cannot carry the old position. */
	bytes[kCursorVisible] = 1;
	auto nonfinite = bytes;
	nonfinite[kCursorX] = nonfinite[kCursorX + 1] = 0;
	nonfinite[kCursorX + 2] = 0x80;
	nonfinite[kCursorX + 3] = 0x7f;
	auto invalid = wire;
	invalid.data = nonfinite.data();
	checkCursorRefused(invalid);

	grid_edit_wire_identity_t identity = {};
	u8 visible = 0;
	f32 x = 0, z = 0;
	REQUIRE(gridEditPlaytestCursorRead(&wire, &identity, &visible, &x, &z));
	CHECK(identity.generation == kIdentity.generation);
	CHECK(visible == 1);
	CHECK(x == -500.25f);
	CHECK(z == 150.5f);
	wire.rp = wire.wp = kPrefix;
	const u8 previous_position_byte = bytes[kCursorX];
	REQUIRE(gridEditPlaytestCursorWrite(&wire, &kIdentity, 0, infinity, infinity));
	CHECK(wire.wp == kCursorX);
	CHECK(bytes[kCursorX] == previous_position_byte);
	REQUIRE(gridEditPlaytestCursorRead(&wire, &identity, &visible, &x, &z));
	CHECK(wire.rp == wire.wp);
	CHECK(visible == 0);
	CHECK(x == 0.0f);
	CHECK(z == 0.0f);
}

TEST_CASE("Only the ready Grid host accesses both independent visibility flags", "[grid-edit-playtest-view]")
{
	using Room = std::unique_ptr<grid_edit_room_t, decltype(&gridEditRoomFree)>;
	const auto path = (std::filesystem::path(PD_SOURCE_DIR)
		/ "examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena").string();
	u8 hash[SHA256_DIGEST_SIZE];
	char digest[SHA256_HEX_SIZE];
	REQUIRE(sha256HashFile(path.c_str(), hash) == 0);
	sha256ToHex(hash, digest);
	const Room host(gridEditRoomHostOpen(&kIdentity, 3, "example:tri_arena", path.c_str(), digest,
		nullptr, 0), gridEditRoomFree);
	REQUIRE(host != nullptr);
	s32 players = -1, creators = -1;
	CHECK_FALSE(gridEditRoomSetVisibility(host.get(), 1, 1));
	CHECK_FALSE(gridEditRoomGetVisibility(host.get(), &players, &creators));
	CHECK(players == -1);
	CHECK(creators == -1);
	grid_edit_room_invite_t invite = {};
	REQUIRE(gridEditRoomHostInvite(host.get(), 4, 5, &invite));
	const Room guest(gridEditRoomGuestOpen(&invite, 6, path.c_str(), nullptr, 0), gridEditRoomFree);
	REQUIRE(guest != nullptr);
	REQUIRE(gridEditRoomHostGrant(host.get(), 4, 5, &invite));
	REQUIRE(gridEditRoomGuestGrant(guest.get(), &invite, 6, 4));
	REQUIRE(gridEditRoomGetVisibility(host.get(), &players, &creators));
	CHECK(players == 0);
	CHECK(creators == 0);
	for (s32 p : {0, 1}) {
		for (s32 c : {0, 1}) {
			CAPTURE(p, c);
			REQUIRE(gridEditRoomSetVisibility(host.get(), p, c));
			CHECK_FALSE(gridEditRoomSetVisibility(guest.get(), !p, !c));
			REQUIRE(gridEditRoomGetVisibility(host.get(), &players, &creators));
			CHECK(players == p);
			CHECK(creators == c);
			CHECK_FALSE(gridEditRoomGetVisibility(guest.get(), &players, &creators));
			CHECK(players == p);
			CHECK(creators == c);
		}
	}
	REQUIRE(gridEditRoomHostDetachPeer(host.get(), 4, 5));
	CHECK_FALSE(gridEditRoomSetVisibility(host.get(), 0, 0));
	CHECK_FALSE(gridEditRoomGetVisibility(host.get(), &players, &creators));
}
