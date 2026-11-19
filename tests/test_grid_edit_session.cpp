#include "catch.hpp"

#include "grid_edit_session.h"
#include "modarchive.h"
#include "net/grid_edit_snapshot.h"
#include "scenario_stage_rules.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>

/* The production C netbuf header includes the project's bool macro. */
struct netbuf {
	u8 *data;
	u32 size;
	u32 rp;
	u32 wp;
	u32 error;
};

namespace {

constexpr u64 kHost = 1;
constexpr u64 kCreator = 2;
constexpr u64 kPlayer = 3;
constexpr u64 kOutsider = 4;

using NativeDoc = std::unique_ptr<forge_level_doc_t, decltype(&forgeLevelDocFree)>;
using EditSession = std::unique_ptr<grid_edit_session_t, decltype(&gridEditSessionFree)>;
using MemberBytes = std::unique_ptr<void, decltype(&std::free)>;

NativeDoc readExampleDocument()
{
	const std::string path = (std::filesystem::path(PD_SOURCE_DIR)
		/ "examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena").string();
	std::unique_ptr<mod_archive_t, decltype(&modArchiveClose)> archive(
		modArchiveOpen(path.c_str()), modArchiveClose);
	REQUIRE(archive);
	const s32 scenario_index = modArchiveFindEntry(archive.get(),
		"dependencies/assets/scenarios/tri_scenario.pdscenario");
	REQUIRE(scenario_index >= 0);
	u32 scenario_size = 0;
	MemberBytes scenario(modArchiveExtractAlloc(archive.get(), scenario_index, &scenario_size), std::free);
	REQUIRE(scenario);

	std::array<std::string, FORGE_LEVEL_MEMBER_COUNT> members;
	forge_level_texts_t texts = {};
	for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; i++) {
		const char *name = forgeLevelDocMemberName(i);
		REQUIRE(name);
		CAPTURE(name);
		u32 size = 0;
		MemberBytes bytes(modArchiveExtractMemAlloc(scenario.get(), scenario_size, name, &size), std::free);
		if (!bytes && forgeLevelDocMemberOptional(i) && modArchiveLastError() == MODARCHIVE_ERR_NOTFOUND) continue;
		REQUIRE(bytes);
		members[i].assign(static_cast<const char *>(bytes.get()), size);
		texts.text[i] = members[i].data();
		texts.size[i] = members[i].size();
	}
	char err[256] = {};
	NativeDoc opened(forgeLevelDocRead(&texts, err, sizeof(err)), forgeLevelDocFree);
	INFO(err);
	REQUIRE(opened);
	return opened;
}

EditSession startSession(const forge_level_doc_t *opened)
{
	EditSession session(gridEditSessionCreate(kHost, opened), gridEditSessionFree);
	REQUIRE(session);
	return session;
}

NativeDoc snapshot(grid_edit_session_t *session, u64 expected_revision)
{
	u64 revision = 0;
	NativeDoc doc(gridEditSessionCloneDocument(session, kHost, &revision), forgeLevelDocFree);
	REQUIRE(doc);
	CHECK(revision == expected_revision);
	return doc;
}

std::string memberText(forge_level_doc_t *doc, s32 member)
{
	size_t size = 0;
	const char *text = forgeLevelDocMemberText(doc, member, &size);
	return text ? std::string(text, size) : std::string();
}

grid_edit_command_t moveSpawn(u64 sequence, u64 revision, f32 x)
{
	grid_edit_command_t command = {};
	command.sequence = sequence;
	command.expected_revision = revision;
	command.operation = GRID_EDIT_MOVE_SPAWN;
	std::strcpy(command.data.move_spawn.spawn_id, "spawn_0000");
	command.data.move_spawn.position[0] = x;
	command.data.move_spawn.position[2] = -50.0f;
	command.data.move_spawn.look[2] = -1.0f;
	return command;
}

grid_edit_command_t setAmbientLight(u64 sequence, u64 revision, s32 brightness)
{
	grid_edit_command_t command = {};
	command.sequence = sequence;
	command.expected_revision = revision;
	command.operation = GRID_EDIT_SET_AMBIENT_LIGHT;
	command.data.ambient_light = {1, brightness};
	return command;
}

grid_edit_resource_t spawnResource(const char *id)
{
	grid_edit_resource_t resource = {};
	resource.kind = GRID_EDIT_RESOURCE_SPAWN;
	std::snprintf(resource.spawn_id, sizeof(resource.spawn_id), "%s", id);
	return resource;
}

} // namespace

TEST_CASE("Grid edits commit in document order with independent creator sequences", "[grid-edit-session]")
{
	const NativeDoc opened = readExampleDocument();
	const EditSession session = startSession(opened.get());
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kCreator, GRID_EDIT_ROLE_CREATOR));

	const auto first = moveSpawn(1, 1, 100.0f);
	const auto concurrent = moveSpawn(1, 1, -120.0f);
	grid_edit_receipt_t receipt = {};
	REQUIRE(gridEditSessionApply(session.get(), kHost, &first, &receipt, nullptr, 0) == GRID_EDIT_APPLIED);
	CHECK(receipt.revision == 2);

	CHECK(gridEditSessionApply(session.get(), kCreator, &concurrent, &receipt, nullptr, 0) == GRID_EDIT_STALE);
	CHECK(receipt.revision == 2);
	auto retry = concurrent;
	retry.expected_revision = 2;
	REQUIRE(gridEditSessionApply(session.get(), kCreator, &retry, &receipt, nullptr, 0) == GRID_EDIT_APPLIED);
	CHECK(receipt.revision == 3);

	CHECK(gridEditSessionApply(session.get(), kHost, &first, &receipt, nullptr, 0) == GRID_EDIT_DUPLICATE);
	CHECK(receipt.revision == 2);
	const auto different = moveSpawn(1, 1, 110.0f);
	CHECK(gridEditSessionApply(session.get(), kHost, &different, &receipt, nullptr, 0) == GRID_EDIT_SEQUENCE_REUSED);
	CHECK(receipt.revision == 0);
	const auto skipped = moveSpawn(3, 3, 150.0f);
	CHECK(gridEditSessionApply(session.get(), kHost, &skipped, &receipt, nullptr, 0) == GRID_EDIT_OUT_OF_ORDER);
	CHECK(receipt.revision == 0);

	const NativeDoc after_concurrent = snapshot(session.get(), 3);
	forge_level_spawn_t spawn = {};
	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocGetSpawn(after_concurrent.get(), 0, &spawn));
	CHECK(std::string(spawn.spawn_id) == "spawn_0000");
	REQUIRE(forgeLevelDocFindPad(after_concurrent.get(), spawn.pad_ref, &pad));
	CHECK(pad.position[0] == -120.0f);

	const auto second = moveSpawn(2, 3, 150.0f);
	REQUIRE(gridEditSessionApply(session.get(), kHost, &second, &receipt, nullptr, 0) == GRID_EDIT_APPLIED);
	CHECK(receipt.revision == 4);
	auto old = first;
	old.expected_revision = 4;
	CHECK(gridEditSessionApply(session.get(), kHost, &old, &receipt, nullptr, 0) == GRID_EDIT_OUT_OF_ORDER);

	const NativeDoc committed = snapshot(session.get(), 4);
	REQUIRE(forgeLevelDocGetSpawn(committed.get(), 0, &spawn));
	CHECK(std::string(spawn.spawn_id) == "spawn_0000");
	REQUIRE(forgeLevelDocFindPad(committed.get(), spawn.pad_ref, &pad));
	CHECK(pad.position[0] == 150.0f);
	CHECK(pad.look[2] == -1.0f);
	CHECK(forgeLevelDocPadCount(committed.get()) == 2);
	CHECK(forgeLevelDocPadCount(opened.get()) == 1);
	CHECK(memberText(committed.get(), FORGE_LEVEL_MEMBER_OBJECTS) == memberText(opened.get(), FORGE_LEVEL_MEMBER_OBJECTS));

	char err[256] = {};
	const f32 position[3] = {200.0f, 0.0f, 0.0f};
	const f32 look[3] = {1.0f, 0.0f, 0.0f};
	REQUIRE(forgeLevelDocMoveSpawn(committed.get(), spawn.spawn_id, position, look, err, sizeof(err)));
	const NativeDoc still_committed = snapshot(session.get(), 4);
	REQUIRE(forgeLevelDocFindPad(still_committed.get(), spawn.pad_ref, &pad));
	CHECK(pad.position[0] == 150.0f);
}

TEST_CASE("Grid rejects malformed and invalid native edits without changing the draft", "[grid-edit-session]")
{
	const NativeDoc opened = readExampleDocument();
	scenario_stage_atmosphere_t original = {};
	original.has_sky = 1;
	original.sky_rgb[0] = 42;
	char err[256] = {};
	REQUIRE(forgeLevelDocSetAtmosphere(opened.get(), &original, err, sizeof(err)));
	const EditSession session = startSession(opened.get());

	auto malformed = moveSpawn(1, 1, 100.0f);
	std::memset(malformed.data.move_spawn.spawn_id, 'a', sizeof(malformed.data.move_spawn.spawn_id));
	CHECK(gridEditSessionApply(session.get(), kHost, &malformed, nullptr, err, sizeof(err)) == GRID_EDIT_INVALID);

	auto bad_move = moveSpawn(1, 1, 100.0f);
	bad_move.data.move_spawn.look[2] = 0.0f;
	CHECK(gridEditSessionApply(session.get(), kHost, &bad_move, nullptr, err, sizeof(err)) == GRID_EDIT_INVALID);
	CHECK(err[0] != '\0');

	grid_edit_command_t lighting = {};
	lighting.sequence = 1;
	lighting.expected_revision = 1;
	lighting.operation = GRID_EDIT_SET_AMBIENT_LIGHT;
	lighting.data.ambient_light = {1, 256};
	CHECK(gridEditSessionApply(session.get(), kHost, &lighting, nullptr, err, sizeof(err)) == GRID_EDIT_INVALID);
	CHECK(err[0] != '\0');

	const NativeDoc rolled_back = snapshot(session.get(), 1);
	for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; i++) {
		CAPTURE(i);
		CHECK(memberText(rolled_back.get(), i) == memberText(opened.get(), i));
	}

	lighting.data.ambient_light.brightness = 143;
	grid_edit_receipt_t receipt = {};
	INFO(err);
	REQUIRE(gridEditSessionApply(session.get(), kHost, &lighting, &receipt, err, sizeof(err)) == GRID_EDIT_APPLIED);
	CHECK(receipt.revision == 2);
	CHECK(gridEditSessionApply(session.get(), kHost, &lighting, &receipt, err, sizeof(err)) == GRID_EDIT_DUPLICATE);
	CHECK(receipt.revision == 2);
	const NativeDoc committed = snapshot(session.get(), 2);
	scenario_stage_atmosphere_t edited = {};
	REQUIRE(forgeLevelDocGetAtmosphere(committed.get(), &edited));
	CHECK(edited.has_sky == 1);
	CHECK(edited.sky_rgb[0] == 42);
	CHECK(edited.has_ambient == 1);
	CHECK(edited.ambient_light == 143);
}

TEST_CASE("Grid decoration moves preserve height and facing through the native snapshot",
	"[grid-edit-session][grid-decoration]")
{
	const NativeDoc opened = readExampleDocument();
	const f32 position[3] = {40.0f, 72.0f, 48.0f};
	const f32 facing[3] = {1.0f, 0.0f, 0.0f};
	char id[GRID_EDIT_SPAWN_ID_CAP] = {};
	char err[256] = {};
	REQUIRE(forgeLevelDocAddDecoration(opened.get(), "example:tri_mesh", position, facing,
		id, sizeof(id), err, sizeof(err)));
	const EditSession session = startSession(opened.get());
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kCreator, GRID_EDIT_ROLE_CREATOR));
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kPlayer, GRID_EDIT_ROLE_PLAYER));
	CHECK(gridEditSessionDecorationCount(session.get(), kCreator) == 1);
	CHECK(gridEditSessionDecorationCount(session.get(), kPlayer) == 0);
	grid_edit_move_decoration_t start = {};
	REQUIRE(gridEditSessionGetDecorationPose(session.get(), kCreator, 0, &start));
	CHECK(std::string(start.record_id) == id);
	CHECK(start.x == position[0]);
	CHECK(start.z == position[2]);
	CHECK_FALSE(gridEditSessionFindDecorationPose(session.get(), kPlayer, id, &start));

	grid_edit_command_t command = {};
	command.sequence = command.expected_revision = 1;
	command.operation = GRID_EDIT_MOVE_DECORATION;
	command.data.move_decoration = start;
	command.data.move_decoration.x = 62.5f;
	command.data.move_decoration.z = 53.25f;
	const grid_edit_wire_identity_t identity = {3, 7};
	u8 bytes[GRID_EDIT_DELTA_WIRE_MAX] = {};
	netbuf wire = {bytes, sizeof(bytes), 0, 0, 0};
	REQUIRE(gridEditCommandWrite(&wire, &identity, &command));
	CHECK(wire.wp == 44);
	grid_edit_wire_identity_t decoded_identity = {};
	grid_edit_command_t decoded = {};
	auto truncated = wire;
	--truncated.wp;
	CHECK_FALSE(gridEditCommandRead(&truncated, &decoded_identity, &decoded));
	CHECK(truncated.rp == 0);
	CHECK(decoded.sequence == 0);
	REQUIRE(gridEditCommandRead(&wire, &decoded_identity, &decoded));
	CHECK(decoded_identity.generation == identity.generation);
	CHECK(gridEditCommandMatches(&command, &decoded));

	auto invalid = decoded;
	invalid.data.move_decoration.z = std::numeric_limits<f32>::infinity();
	CHECK_FALSE(gridEditCommandWrite(&wire, &identity, &invalid));
	CHECK(gridEditSessionApply(session.get(), kCreator, &invalid, nullptr, err, sizeof(err)) == GRID_EDIT_INVALID);
	invalid = decoded;
	std::strcpy(invalid.data.move_decoration.record_id, "setup_0000");
	CHECK(gridEditSessionApply(session.get(), kCreator, &invalid, nullptr, err, sizeof(err)) == GRID_EDIT_INVALID);
	CHECK(gridEditSessionApply(session.get(), kPlayer, &decoded, nullptr, err, sizeof(err)) == GRID_EDIT_DENIED);
	grid_edit_receipt_t receipt = {};
	REQUIRE(gridEditSessionApply(session.get(), kCreator, &decoded, &receipt, err, sizeof(err)) == GRID_EDIT_APPLIED);
	CHECK(receipt.revision == 2);
	CHECK(gridEditSessionApply(session.get(), kCreator, &decoded, &receipt, err, sizeof(err)) == GRID_EDIT_DUPLICATE);
	CHECK(receipt.revision == 2);

	grid_edit_delta_t delta = {};
	size_t delta_count = 0;
	REQUIRE(gridEditSessionReadDeltas(session.get(), kCreator, 1, &delta, 1, &delta_count, nullptr) == GRID_EDIT_TAIL_READY);
	REQUIRE(delta_count == 1);
	wire.rp = wire.wp = 0;
	REQUIRE(gridEditDeltaWrite(&wire, &identity, &delta));
	grid_edit_delta_t decoded_delta = {};
	REQUIRE(gridEditDeltaRead(&wire, &decoded_identity, &decoded_delta));
	CHECK(decoded_delta.author_id == kCreator);
	CHECK(gridEditCommandMatches(&command, &decoded_delta.command));

	const NativeDoc committed = snapshot(session.get(), 2);
	grid_edit_snapshot_identity_t snapshot_identity = {};
	snapshot_identity.room = identity;
	snapshot_identity.revision = 2;
	std::strcpy(snapshot_identity.arena_id, "example:tri_arena");
	sha256Hash(snapshot_identity.arena_id, std::strlen(snapshot_identity.arena_id), snapshot_identity.arena_sha256);
	u32 size = 0;
	u8 digest[SHA256_DIGEST_SIZE] = {};
	const MemberBytes packed(gridEditSnapshotEncode(&snapshot_identity, committed.get(), &size, digest, err, sizeof(err)), std::free);
	REQUIRE(packed);
	const NativeDoc reopened(gridEditSnapshotDecode(packed.get(), size, &snapshot_identity, digest, nullptr, err, sizeof(err)),
		forgeLevelDocFree);
	REQUIRE(reopened);
	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocGetDecorationPose(reopened.get(), id, &pad));
	CHECK(pad.position[0] == command.data.move_decoration.x);
	CHECK(pad.position[1] == position[1]);
	CHECK(pad.position[2] == command.data.move_decoration.z);
	CHECK(std::memcmp(pad.look, facing, sizeof(facing)) == 0);
	REQUIRE(forgeLevelDocGetDecorationPose(opened.get(), id, &pad));
	CHECK(pad.position[0] == position[0]);
	CHECK(pad.position[2] == position[2]);
}

TEST_CASE("Grid enforces host grants and independent cross-role visibility", "[grid-edit-session]")
{
	const NativeDoc opened = readExampleDocument();
	const EditSession session = startSession(opened.get());
	CHECK(gridEditSessionCapabilities(session.get(), kHost).see_match == 0);
	CHECK(gridEditSessionSetMember(session.get(), kOutsider, kOutsider, GRID_EDIT_ROLE_CREATOR) == 0);
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kCreator, GRID_EDIT_ROLE_CREATOR));
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kPlayer, GRID_EDIT_ROLE_PLAYER));
	CHECK(gridEditSessionCapabilities(session.get(), kPlayer).see_creators == 0);
	CHECK(gridEditSessionSetMember(session.get(), kPlayer, kPlayer, GRID_EDIT_ROLE_CREATOR) == 0);
	CHECK(gridEditSessionSetVisibility(session.get(), kCreator, 1, 1) == 0);
	CHECK(gridEditSessionRemoveMember(session.get(), kCreator, kPlayer) == 0);
	CHECK(gridEditSessionRemoveMember(session.get(), kHost, kHost) == 0);

	const auto edit = moveSpawn(1, 1, 100.0f);
	for (s32 players_see_creators : {0, 1}) {
		for (s32 creators_see_match : {0, 1}) {
			CAPTURE(players_see_creators, creators_see_match);
			REQUIRE(gridEditSessionSetVisibility(session.get(), kHost, players_see_creators, creators_see_match));

			const auto player = gridEditSessionCapabilities(session.get(), kPlayer);
			CHECK(player.role == GRID_EDIT_ROLE_PLAYER);
			CHECK(player.can_edit == 0);
			CHECK(player.see_creators == players_see_creators);
			CHECK(player.see_match == 1);
			const auto creator = gridEditSessionCapabilities(session.get(), kCreator);
			CHECK(creator.role == GRID_EDIT_ROLE_CREATOR);
			CHECK(creator.can_edit == 1);
			CHECK(creator.see_creators == 1);
			CHECK(creator.see_match == creators_see_match);
			const auto outsider = gridEditSessionCapabilities(session.get(), kOutsider);
			CHECK(outsider.role == GRID_EDIT_ROLE_NONE);
			CHECK(outsider.can_edit == 0);
			CHECK(outsider.see_creators == 0);
			CHECK(outsider.see_match == 0);

			grid_edit_receipt_t receipt = {99};
			CHECK(gridEditSessionApply(session.get(), kPlayer, &edit, &receipt, nullptr, 0) == GRID_EDIT_DENIED);
			CHECK(receipt.revision == 0);
			CHECK(gridEditSessionApply(session.get(), kOutsider, &edit, &receipt, nullptr, 0) == GRID_EDIT_DENIED);
			CHECK(receipt.revision == 0);
			u64 revision = 99;
			NativeDoc denied(gridEditSessionCloneDocument(session.get(), kPlayer, &revision), forgeLevelDocFree);
			CHECK_FALSE(denied);
			CHECK(revision == 0);
		}
	}
	const NativeDoc unedited = snapshot(session.get(), 1);
	CHECK(memberText(unedited.get(), FORGE_LEVEL_MEMBER_PADS) == memberText(opened.get(), FORGE_LEVEL_MEMBER_PADS));
}

TEST_CASE("Grid bounds membership and preserves the sequence across role changes", "[grid-edit-session]")
{
	const NativeDoc opened = readExampleDocument();
	const EditSession session = startSession(opened.get());
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kCreator, GRID_EDIT_ROLE_CREATOR));

	const auto first = moveSpawn(1, 1, 100.0f);
	REQUIRE(gridEditSessionApply(session.get(), kCreator, &first, nullptr, nullptr, 0) == GRID_EDIT_APPLIED);
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kCreator, GRID_EDIT_ROLE_PLAYER));
	CHECK(gridEditSessionApply(session.get(), kCreator, &first, nullptr, nullptr, 0) == GRID_EDIT_DENIED);
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kCreator, GRID_EDIT_ROLE_CREATOR));
	grid_edit_receipt_t receipt = {};
	CHECK(gridEditSessionApply(session.get(), kCreator, &first, &receipt, nullptr, 0) == GRID_EDIT_DUPLICATE);
	CHECK(receipt.revision == 2);

	for (u64 id = 3; id <= GRID_EDIT_MAX_MEMBERS; id++) {
		REQUIRE(gridEditSessionSetMember(session.get(), kHost, id, GRID_EDIT_ROLE_PLAYER));
	}
	constexpr u64 new_id = GRID_EDIT_MAX_MEMBERS + 1;
	CHECK(gridEditSessionSetMember(session.get(), kHost, new_id, GRID_EDIT_ROLE_CREATOR) == 0);
	REQUIRE(gridEditSessionRemoveMember(session.get(), kHost, kCreator));
	CHECK(gridEditSessionCapabilities(session.get(), kCreator).role == GRID_EDIT_ROLE_NONE);
	const auto second = moveSpawn(2, 2, 200.0f);
	CHECK(gridEditSessionApply(session.get(), kCreator, &second, nullptr, nullptr, 0) == GRID_EDIT_DENIED);
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, new_id, GRID_EDIT_ROLE_CREATOR));
	CHECK(gridEditSessionApply(session.get(), new_id, &second, nullptr, nullptr, 0) == GRID_EDIT_OUT_OF_ORDER);
	const auto fresh = moveSpawn(1, 2, 200.0f);
	CHECK(gridEditSessionApply(session.get(), new_id, &fresh, &receipt, nullptr, 0) == GRID_EDIT_APPLIED);
	CHECK(receipt.revision == 3);
}

TEST_CASE("Grid returns exact ordered commits only to authorized creators", "[grid-edit-session]")
{
	const NativeDoc opened = readExampleDocument();
	const EditSession session = startSession(opened.get());
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kCreator, GRID_EDIT_ROLE_CREATOR));
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kPlayer, GRID_EDIT_ROLE_PLAYER));
	REQUIRE(gridEditSessionSetVisibility(session.get(), kHost, 1, 1));

	size_t count = 99;
	u64 latest = 99;
	CHECK(gridEditSessionReadDeltas(session.get(), kCreator, 0, nullptr, 0, &count, &latest)
		== GRID_EDIT_TAIL_SNAPSHOT_REQUIRED);
	CHECK(count == 0);
	CHECK(latest == 1);

	auto move = moveSpawn(1, 1, 100.0f);
	move.data.move_spawn.look[2] = -4.0f;
	REQUIRE(gridEditSessionApply(session.get(), kHost, &move, nullptr, nullptr, 0) == GRID_EDIT_APPLIED);
	CHECK(gridEditSessionApply(session.get(), kHost, &move, nullptr, nullptr, 0) == GRID_EDIT_DUPLICATE);
	move.data.move_spawn.position[0] = 200.0f;

	auto ambient = setAmbientLight(1, 1, 143);
	CHECK(gridEditSessionApply(session.get(), kCreator, &ambient, nullptr, nullptr, 0) == GRID_EDIT_STALE);
	ambient.expected_revision = 2;
	REQUIRE(gridEditSessionApply(session.get(), kCreator, &ambient, nullptr, nullptr, 0) == GRID_EDIT_APPLIED);

	std::array<grid_edit_delta_t, 2> deltas = {};
	deltas[0].revision = 99;
	deltas[1].revision = 99;
	CHECK(gridEditSessionReadDeltas(session.get(), kPlayer, 1, deltas.data(), deltas.size(), &count, &latest)
		== GRID_EDIT_TAIL_DENIED);
	CHECK(count == 0);
	CHECK(latest == 0);
	CHECK(deltas[0].revision == 99);
	CHECK(deltas[1].revision == 99);

	CHECK(gridEditSessionReadDeltas(session.get(), kCreator, 1, deltas.data(), 1, &count, &latest)
		== GRID_EDIT_TAIL_BUFFER_TOO_SMALL);
	CHECK(count == 2);
	CHECK(latest == 3);
	CHECK(deltas[0].revision == 99);
	CHECK(deltas[1].revision == 99);
	CHECK(gridEditSessionReadDeltas(session.get(), kCreator, 1, nullptr, deltas.size(), &count, &latest)
		== GRID_EDIT_TAIL_BUFFER_TOO_SMALL);
	CHECK(count == 2);

	REQUIRE(gridEditSessionReadDeltas(session.get(), kCreator, 1, deltas.data(), deltas.size(), &count, &latest)
		== GRID_EDIT_TAIL_READY);
	REQUIRE(count == 2);
	CHECK(latest == 3);
	CHECK(deltas[0].revision == 2);
	CHECK(deltas[0].author_id == kHost);
	CHECK(deltas[0].command.sequence == 1);
	CHECK(deltas[0].command.expected_revision == 1);
	REQUIRE(deltas[0].command.operation == GRID_EDIT_MOVE_SPAWN);
	CHECK(std::string(deltas[0].command.data.move_spawn.spawn_id) == "spawn_0000");
	CHECK(deltas[0].command.data.move_spawn.position[0] == 100.0f);
	CHECK(deltas[0].command.data.move_spawn.look[2] == -4.0f);
	CHECK(deltas[1].revision == 3);
	CHECK(deltas[1].author_id == kCreator);
	CHECK(deltas[1].command.sequence == 1);
	CHECK(deltas[1].command.expected_revision == 2);
	REQUIRE(deltas[1].command.operation == GRID_EDIT_SET_AMBIENT_LIGHT);
	CHECK(deltas[1].command.data.ambient_light.enabled == 1);
	CHECK(deltas[1].command.data.ambient_light.brightness == 143);

	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kCreator, GRID_EDIT_ROLE_PLAYER));
	CHECK(gridEditSessionReadDeltas(session.get(), kCreator, 1, nullptr, 0, &count, &latest)
		== GRID_EDIT_TAIL_DENIED);
}

TEST_CASE("Grid requires a fresh snapshot beyond its retained commit history", "[grid-edit-session]")
{
	const NativeDoc opened = readExampleDocument();
	const EditSession session = startSession(opened.get());
	constexpr u64 commits = GRID_EDIT_DELTA_CAPACITY + 3;
	for (u64 sequence = 1; sequence <= commits; sequence++) {
		const auto command = setAmbientLight(sequence, sequence, static_cast<s32>(sequence));
		REQUIRE(gridEditSessionApply(session.get(), kHost, &command, nullptr, nullptr, 0) == GRID_EDIT_APPLIED);
	}

	constexpr u64 retained_after = commits + 1 - GRID_EDIT_DELTA_CAPACITY;
	size_t count = 99;
	u64 latest = 0;
	CHECK(gridEditSessionReadDeltas(session.get(), kHost, retained_after - 1, nullptr, 0, &count, &latest)
		== GRID_EDIT_TAIL_SNAPSHOT_REQUIRED);
	CHECK(count == 0);
	CHECK(latest == commits + 1);
	const NativeDoc recovered = snapshot(session.get(), latest);
	scenario_stage_atmosphere_t lighting = {};
	REQUIRE(forgeLevelDocGetAtmosphere(recovered.get(), &lighting));
	CHECK(lighting.has_ambient == 1);
	CHECK(lighting.ambient_light == static_cast<s32>(commits));

	std::array<grid_edit_delta_t, GRID_EDIT_DELTA_CAPACITY> deltas = {};
	REQUIRE(gridEditSessionReadDeltas(session.get(), kHost, retained_after, deltas.data(), deltas.size(), &count,
		&latest) == GRID_EDIT_TAIL_READY);
	REQUIRE(count == deltas.size());
	for (size_t i = 0; i < count; i++) {
		CAPTURE(i);
		CHECK(deltas[i].revision == retained_after + i + 1);
		CHECK(deltas[i].command.expected_revision == retained_after + i);
		CHECK(deltas[i].command.sequence == retained_after + i);
		CHECK(deltas[i].command.data.ambient_light.brightness == static_cast<s32>(retained_after + i));
	}

	CHECK(gridEditSessionReadDeltas(session.get(), kHost, latest, nullptr, 0, &count, nullptr)
		== GRID_EDIT_TAIL_READY);
	CHECK(count == 0);
	CHECK(gridEditSessionReadDeltas(session.get(), kHost, latest + 1, nullptr, 0, &count, nullptr)
		== GRID_EDIT_TAIL_INVALID_REVISION);
	CHECK(count == 0);
}

TEST_CASE("Grid leases isolate competing writes until host-clock expiry or release", "[grid-edit-session]")
{
	const NativeDoc opened = readExampleDocument();
	const EditSession session = startSession(opened.get());
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kCreator, GRID_EDIT_ROLE_CREATOR));
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kPlayer, GRID_EDIT_ROLE_PLAYER));
	const auto spawn = spawnResource("spawn_0000");
	const auto missing = spawnResource("spawn_9999");
	const grid_edit_resource_t ambient = {GRID_EDIT_RESOURCE_AMBIENT_LIGHT, {}};

	constexpr u64 start = 100;
	CHECK(gridEditSessionAcquireLease(session.get(), kCreator, &spawn) == GRID_EDIT_LEASE_CLOCK_UNAVAILABLE);
	CHECK(gridEditSessionAdvanceClock(session.get(), kCreator, start) == 0);
	REQUIRE(gridEditSessionAdvanceClock(session.get(), kHost, start));
	CHECK(gridEditSessionAcquireLease(session.get(), kPlayer, &spawn) == GRID_EDIT_LEASE_DENIED);
	CHECK(gridEditSessionAcquireLease(session.get(), kCreator, &missing) == GRID_EDIT_LEASE_INVALID_RESOURCE);
	REQUIRE(gridEditSessionAcquireLease(session.get(), kCreator, &spawn) == GRID_EDIT_LEASE_GRANTED);
	REQUIRE(gridEditSessionAcquireLease(session.get(), kHost, &ambient) == GRID_EDIT_LEASE_GRANTED);
	CHECK(gridEditSessionAcquireLease(session.get(), kHost, &spawn) == GRID_EDIT_LEASE_HELD);
	CHECK(gridEditSessionReleaseLease(session.get(), kHost, &spawn) == 0);

	const auto first = moveSpawn(1, 1, 100.0f);
	REQUIRE(gridEditSessionApply(session.get(), kCreator, &first, nullptr, nullptr, 0) == GRID_EDIT_APPLIED);
	auto competing = moveSpawn(1, 2, 200.0f);
	const auto lighting = setAmbientLight(2, 2, 143);
	grid_edit_receipt_t receipt = {99};
	CHECK(gridEditSessionApply(session.get(), kHost, &competing, &receipt, nullptr, 0) == GRID_EDIT_RESOURCE_LEASED);
	CHECK(receipt.revision == 0);
	CHECK(gridEditSessionApply(session.get(), kCreator, &lighting, nullptr, nullptr, 0) == GRID_EDIT_RESOURCE_LEASED);

	constexpr u64 renewal = start + GRID_EDIT_LEASE_MS - 1;
	REQUIRE(gridEditSessionAdvanceClock(session.get(), kHost, renewal));
	REQUIRE(gridEditSessionAcquireLease(session.get(), kCreator, &spawn) == GRID_EDIT_LEASE_GRANTED);
	CHECK(gridEditSessionAdvanceClock(session.get(), kHost, renewal - 1) == 0);
	REQUIRE(gridEditSessionReleaseLease(session.get(), kHost, &ambient));
	REQUIRE(gridEditSessionApply(session.get(), kCreator, &lighting, &receipt, nullptr, 0) == GRID_EDIT_APPLIED);
	CHECK(receipt.revision == 3);

	competing.expected_revision = 3;
	REQUIRE(gridEditSessionAdvanceClock(session.get(), kHost, renewal + GRID_EDIT_LEASE_MS - 1));
	CHECK(gridEditSessionApply(session.get(), kHost, &competing, nullptr, nullptr, 0) == GRID_EDIT_RESOURCE_LEASED);
	REQUIRE(gridEditSessionAdvanceClock(session.get(), kHost, renewal + GRID_EDIT_LEASE_MS));
	CHECK(gridEditSessionReleaseLease(session.get(), kCreator, &spawn) == 0);
	REQUIRE(gridEditSessionApply(session.get(), kHost, &competing, &receipt, nullptr, 0) == GRID_EDIT_APPLIED);
	CHECK(receipt.revision == 4);
}

TEST_CASE("Grid releases held resources on creator downgrade and removal", "[grid-edit-session]")
{
	const NativeDoc opened = readExampleDocument();
	const EditSession session = startSession(opened.get());
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kCreator, GRID_EDIT_ROLE_CREATOR));
	REQUIRE(gridEditSessionAdvanceClock(session.get(), kHost, 1));
	const auto spawn = spawnResource("spawn_0000");
	const grid_edit_resource_t ambient = {GRID_EDIT_RESOURCE_AMBIENT_LIGHT, {}};

	REQUIRE(gridEditSessionAcquireLease(session.get(), kCreator, &spawn) == GRID_EDIT_LEASE_GRANTED);
	REQUIRE(gridEditSessionAcquireLease(session.get(), kCreator, &ambient) == GRID_EDIT_LEASE_GRANTED);
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kCreator, GRID_EDIT_ROLE_PLAYER));

	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kCreator, GRID_EDIT_ROLE_CREATOR));
	REQUIRE(gridEditSessionAcquireLease(session.get(), kCreator, &ambient) == GRID_EDIT_LEASE_GRANTED);
	const auto move = moveSpawn(1, 1, 100.0f);
	REQUIRE(gridEditSessionApply(session.get(), kHost, &move, nullptr, nullptr, 0) == GRID_EDIT_APPLIED);

	const auto lighting = setAmbientLight(2, 2, 143);
	CHECK(gridEditSessionApply(session.get(), kHost, &lighting, nullptr, nullptr, 0) == GRID_EDIT_RESOURCE_LEASED);
	REQUIRE(gridEditSessionRemoveMember(session.get(), kHost, kCreator));
	REQUIRE(gridEditSessionApply(session.get(), kHost, &lighting, nullptr, nullptr, 0) == GRID_EDIT_APPLIED);
}
