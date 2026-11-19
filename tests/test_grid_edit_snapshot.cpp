#include "catch.hpp"

#include "modarchive.h"
#include "net/grid_edit_snapshot.h"
#include "net/grid_edit_snapshot_wire.h"
#include "scenario_stage_rules.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

/* The C netbuf header includes the project's C bool macro. */
struct netbuf {
	u8 *data;
	u32 size;
	u32 rp;
	u32 wp;
	u32 error;
};

namespace {

using Doc = std::unique_ptr<forge_level_doc_t, decltype(&forgeLevelDocFree)>;
using Bytes = std::unique_ptr<void, decltype(&std::free)>;
using Digest = std::array<u8, SHA256_DIGEST_SIZE>;

constexpr size_t kIdLengthOffset = 1 + 1 + 8 + 8;
constexpr size_t kMemberHeaderSize = 1 + 4;
constexpr u32 kPacketPrefix = 3;
constexpr u32 kWireRoom = kPacketPrefix + 1;
constexpr u32 kWireGeneration = kWireRoom + 1;
constexpr u32 kWireRevision = kWireGeneration + 8;
constexpr u32 kWireOfferSize = kWireRevision + 16;
constexpr u32 kWireDigest = kWireOfferSize + 4;
constexpr u32 kWireChunkOffset = kWireGeneration + 8;
constexpr u32 kWireChunkSize = kWireChunkOffset + 4;
constexpr grid_edit_wire_identity_t kWireIdentity = {3, 0x1122334455667788ull};

struct SnapshotFixture {
	const std::string path;
	const char *scenario_member;
	grid_edit_snapshot_identity_t identity = {};

	SnapshotFixture(const char *arena_path = "examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena",
		const char *scenario_entry = "dependencies/assets/scenarios/tri_scenario.pdscenario",
		const char *arena_id = "example:tri_arena")
		: path((std::filesystem::path(PD_SOURCE_DIR) / arena_path).string()), scenario_member(scenario_entry)
	{
		identity.room = {3, 0x1122334455667788ull};
		identity.revision = 7;
		std::strcpy(identity.arena_id, arena_id);
		REQUIRE(sha256HashFile(path.c_str(), identity.arena_sha256) == 0);
	}

	Doc nativePlacements(s32 inherited_member = -1) const
	{
		std::unique_ptr<mod_archive_t, decltype(&modArchiveClose)> arena(
			modArchiveOpen(path.c_str()), modArchiveClose);
		REQUIRE(arena != nullptr);
		const s32 entry = modArchiveFindEntry(arena.get(), scenario_member);
		REQUIRE(entry >= 0);
		u32 scenario_size = 0;
		const Bytes scenario(modArchiveExtractAlloc(arena.get(), entry, &scenario_size), std::free);
		REQUIRE(scenario != nullptr);

		forge_level_texts_t texts = {};
		std::array<std::string, FORGE_LEVEL_MEMBER_COUNT> members;
		for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; i++) {
			if (forgeLevelDocMemberOptional(i) && i != inherited_member) continue;
			const char *name = forgeLevelDocMemberName(i);
			CAPTURE(name);
			u32 size = 0;
			const Bytes text(modArchiveExtractMemAlloc(scenario.get(), scenario_size, name, &size), std::free);
			REQUIRE(text != nullptr);
			members[i].assign(static_cast<const char *>(text.get()), size);
			texts.text[i] = members[i].data();
			texts.size[i] = members[i].size();
		}
		char err[256] = {};
		Doc doc(forgeLevelDocRead(&texts, err, sizeof(err)), forgeLevelDocFree);
		INFO(err);
		REQUIRE(doc != nullptr);
		return doc;
	}
};

std::vector<u8> encode(const grid_edit_snapshot_identity_t &identity, forge_level_doc_t *doc, Digest &digest)
{
	char err[256] = {};
	u32 size = 0;
	const Bytes bytes(gridEditSnapshotEncode(&identity, doc, &size, digest.data(), err, sizeof(err)), std::free);
	INFO(err);
	REQUIRE(bytes != nullptr);
	const auto *start = static_cast<const u8 *>(bytes.get());
	return {start, start + size};
}

void writeU32(std::vector<u8> &bytes, size_t offset, u32 value)
{
	for (size_t i = 0; i < 4; i++) bytes.at(offset + i) = static_cast<u8>(value >> (8 * i));
}

u32 readU32(const std::vector<u8> &bytes, size_t offset)
{
	u32 value = 0;
	for (size_t i = 0; i < 4; i++) value |= static_cast<u32>(bytes.at(offset + i)) << (8 * i);
	return value;
}

size_t memberOffset(const std::vector<u8> &bytes, s32 member)
{
	size_t offset = kIdLengthOffset + 1 + bytes.at(kIdLengthOffset) + SHA256_DIGEST_SIZE + 1;
	for (s32 i = 0; i < member; i++) offset += kMemberHeaderSize + readU32(bytes, offset + 1);
	return offset;
}

Digest seal(std::vector<u8> &bytes)
{
	Digest digest = {};
	const size_t prefix = bytes.size() - SHA256_DIGEST_SIZE;
	sha256Hash(bytes.data(), prefix, digest.data());
	std::copy(digest.begin(), digest.end(), bytes.begin() + prefix);
	return digest;
}

void checkRefusal(const void *bytes, u32 size, const grid_edit_snapshot_identity_t &expected, const Digest &digest)
{
	grid_edit_snapshot_identity_t previous = {};
	previous.room = {19, 19};
	previous.revision = 99;
	std::strcpy(previous.arena_id, "example:unchanged");
	std::memset(previous.arena_sha256, 0x5a, sizeof(previous.arena_sha256));
	char err[256] = {};
	const Doc doc(gridEditSnapshotDecode(bytes, size, &expected, digest.data(), &previous, err, sizeof(err)),
		forgeLevelDocFree);
	CHECK(doc == nullptr);
	CHECK(err[0] != '\0');
	CHECK(previous.revision == 99);
	CHECK(previous.room.room_id == 19);
	CHECK(previous.room.generation == 19);
	CHECK(std::strcmp(previous.arena_id, "example:unchanged") == 0);
	for (const u8 byte : previous.arena_sha256) CHECK(byte == 0x5a);
}

grid_edit_snapshot_offer_t wireOffer(u32 size)
{
	grid_edit_snapshot_offer_t offer = {kWireIdentity, 1, 0, size, {}};
	std::fill_n(offer.sha256, SHA256_DIGEST_SIZE, 0x29);
	return offer;
}

void checkCursor(const netbuf &wire, const netbuf &before)
{
	CHECK(wire.data == before.data);
	CHECK(wire.size == before.size);
	CHECK(wire.rp == before.rp);
	CHECK(wire.wp == before.wp);
	CHECK(wire.error == before.error);
}

void checkOfferReadRefusal(netbuf wire, grid_edit_snapshot_kind_e kind)
{
	const auto before = wire;
	const auto previous = wireOffer(12345);
	auto output = previous;
	CHECK_FALSE(gridEditSnapshotOfferRead(&wire, kind, &output));
	checkCursor(wire, before);
	CHECK(gridEditSnapshotOfferMatches(&output, &previous));
}

void checkChunkReadRefusal(netbuf wire)
{
	const auto before = wire;
	const u8 bytes[] = {11, 22, 33};
	grid_edit_snapshot_chunk_t output = {{19, 29}, 102, sizeof(bytes), bytes};
	CHECK_FALSE(gridEditSnapshotChunkRead(&wire, &output));
	checkCursor(wire, before);
	CHECK(output.identity.room_id == 19);
	CHECK(output.identity.generation == 29);
	CHECK(output.offset == 102);
	CHECK(output.size == sizeof(bytes));
	CHECK(output.bytes == &bytes[0]);
}

} // namespace

TEST_CASE("Grid native snapshot preserves exact member texts and optional presence", "[grid-edit-snapshot]")
{
	const SnapshotFixture fixture;
	const Doc opened = fixture.nativePlacements();
	bool have_optional_members = false;
	SECTION("omitted optional members remain omitted") {
		CHECK_FALSE(forgeLevelDocHasHill(opened.get(), "pad_0000"));
	}
	SECTION("authored optional members participate in the snapshot") {
		have_optional_members = true;
		char err[256] = {};
		REQUIRE(forgeLevelDocSetHill(opened.get(), "pad_0000", 1, err, sizeof(err)));
		scenario_stage_atmosphere_t lighting = {};
		lighting.has_ambient = 1;
		lighting.ambient_light = 173;
		REQUIRE(forgeLevelDocSetAtmosphere(opened.get(), &lighting, err, sizeof(err)));
	}

	Digest digest = {};
	const auto bytes = encode(fixture.identity, opened.get(), digest);
	CHECK(bytes.front() == GRID_EDIT_SNAPSHOT_WIRE_VERSION);
	CHECK(std::equal(digest.begin(), digest.end(), bytes.end() - SHA256_DIGEST_SIZE));
	for (s32 member : {FORGE_LEVEL_MEMBER_INTRO, FORGE_LEVEL_MEMBER_STAGE}) {
		const size_t offset = memberOffset(bytes, member);
		CHECK(bytes.at(offset) == member);
		CHECK((readU32(bytes, offset + 1) > 0) == have_optional_members);
	}

	auto expected = fixture.identity;
	expected.revision = 0;
	grid_edit_snapshot_identity_t received = {};
	char err[256] = {};
	const Doc decoded(gridEditSnapshotDecode(bytes.data(), static_cast<u32>(bytes.size()), &expected,
		digest.data(), &received, err, sizeof(err)), forgeLevelDocFree);
	INFO(err);
	REQUIRE(decoded != nullptr);
	CHECK(received.room.room_id == fixture.identity.room.room_id);
	CHECK(received.room.generation == fixture.identity.room.generation);
	CHECK(received.revision == fixture.identity.revision);
	CHECK(std::strcmp(received.arena_id, fixture.identity.arena_id) == 0);
	CHECK(std::memcmp(received.arena_sha256, fixture.identity.arena_sha256, SHA256_DIGEST_SIZE) == 0);
	CHECK(forgeLevelDocSpawnCount(decoded.get()) == forgeLevelDocSpawnCount(opened.get()));
	CHECK((forgeLevelDocHasHill(decoded.get(), "pad_0000") != 0) == have_optional_members);
	scenario_stage_atmosphere_t lighting = {};
	REQUIRE(forgeLevelDocGetAtmosphere(decoded.get(), &lighting));
	CHECK((lighting.has_ambient != 0) == have_optional_members);
	CHECK(lighting.ambient_light == (have_optional_members ? 173 : 0));

	Digest decoded_digest = {};
	CHECK(encode(received, decoded.get(), decoded_digest) == bytes);
	CHECK(decoded_digest == digest);
}

TEST_CASE("Grid v1 snapshots inherit exact pinned cover and refuse unsaved cover edits", "[grid-edit-snapshot]")
{
	const SnapshotFixture fixture;
	const Doc bare = fixture.nativePlacements();
	const Doc covered = fixture.nativePlacements(FORGE_LEVEL_MEMBER_COVERS);
	size_t coverLength = 0;
	REQUIRE(forgeLevelDocMemberText(covered.get(), FORGE_LEVEL_MEMBER_COVERS, &coverLength));
	REQUIRE(coverLength > 0);
	Digest bareDigest = {}, coveredDigest = {};
	CHECK(encode(fixture.identity, covered.get(), coveredDigest) == encode(fixture.identity, bare.get(), bareDigest));
	CHECK(coveredDigest == bareDigest);

	forge_level_pad_t pad = {};
	REQUIRE(forgeLevelDocGetPad(covered.get(), 0, &pad));
	char err[256] = {}, id[32] = {};
	REQUIRE(forgeLevelDocAddCoverAtPad(covered.get(), pad.pad_ref, id, sizeof(id), err, sizeof(err)));
	u32 size = 0;
	const Bytes refused(gridEditSnapshotEncode(&fixture.identity, covered.get(), &size,
		coveredDigest.data(), err, sizeof(err)), std::free);
	CHECK(refused == nullptr);
	CHECK(size == 0);
	CHECK(std::string(err).find("pinned arena") != std::string::npos);
}

TEST_CASE("Grid v1 snapshots restore pinned room lighting without sending rooms.json", "[grid-edit-snapshot]")
{
	const SnapshotFixture fixture("dev-mods/grid_two_rooms/grid_two_rooms.pdarena",
		"dependencies/assets/scenarios/scenario_grid_two_rooms.pdscenario", "mod_grid_two_rooms:arena_grid_two_rooms");
	const Doc bare = fixture.nativePlacements();
	const Doc pinned = fixture.nativePlacements(FORGE_LEVEL_MEMBER_ROOMS);
	size_t source_size = 0;
	const char *source_text = forgeLevelDocMemberText(pinned.get(), FORGE_LEVEL_MEMBER_ROOMS, &source_size);
	REQUIRE(source_text);
	REQUIRE(source_size > 0);
	const std::string room_source(source_text, source_size);

	Digest bare_digest = {}, pinned_digest = {};
	const auto bytes = encode(fixture.identity, pinned.get(), pinned_digest);
	CHECK(bytes == encode(fixture.identity, bare.get(), bare_digest));
	CHECK(pinned_digest == bare_digest);

	char err[256] = {};
	const Doc restored(gridEditSnapshotDecodeWithBaseline(bytes.data(), static_cast<u32>(bytes.size()),
		&fixture.identity, pinned_digest.data(), pinned.get(), nullptr, err, sizeof(err)), forgeLevelDocFree);
	INFO(err);
	REQUIRE(restored);
	size_t restored_size = 0;
	const char *restored_text = forgeLevelDocMemberText(restored.get(), FORGE_LEVEL_MEMBER_ROOMS, &restored_size);
	REQUIRE(restored_text);
	CHECK(std::string(restored_text, restored_size) == room_source);
	CHECK_FALSE(forgeLevelDocMemberChanged(restored.get(), FORGE_LEVEL_MEMBER_ROOMS));

	REQUIRE(forgeLevelDocSetRoomBrightness(pinned.get(), 1, 88, 144, err, sizeof(err)));
	CHECK(forgeLevelDocMemberChanged(pinned.get(), FORGE_LEVEL_MEMBER_ROOMS));
	u32 refused_size = 17;
	const Bytes refused(gridEditSnapshotEncode(&fixture.identity, pinned.get(), &refused_size,
		pinned_digest.data(), err, sizeof(err)), std::free);
	CHECK_FALSE(refused);
	CHECK(refused_size == 17);
	CHECK(pinned_digest == bare_digest);
	CHECK(std::string(err).find("save offline room lighting edits into the pinned arena") != std::string::npos);
}

TEST_CASE("Grid native snapshot requires the authenticated identity and digest", "[grid-edit-snapshot]")
{
	const SnapshotFixture fixture;
	const Doc opened = fixture.nativePlacements();
	Digest digest = {};
	const auto bytes = encode(fixture.identity, opened.get(), digest);
	const auto size = static_cast<u32>(bytes.size());

	auto expected = fixture.identity;
	expected.room.room_id++;
	checkRefusal(bytes.data(), size, expected, digest);
	expected = fixture.identity;
	expected.room.generation++;
	checkRefusal(bytes.data(), size, expected, digest);
	expected = fixture.identity;
	expected.revision++;
	checkRefusal(bytes.data(), size, expected, digest);
	expected = fixture.identity;
	std::strcpy(expected.arena_id, "example:other_arena");
	checkRefusal(bytes.data(), size, expected, digest);
	expected = fixture.identity;
	expected.arena_sha256[0] ^= 1;
	checkRefusal(bytes.data(), size, expected, digest);

	auto wrong_digest = digest;
	wrong_digest[0] ^= 1;
	checkRefusal(bytes.data(), size, fixture.identity, wrong_digest);
	auto tampered = bytes;
	tampered.at(memberOffset(tampered, FORGE_LEVEL_MEMBER_PADS) + kMemberHeaderSize) ^= 1;
	checkRefusal(tampered.data(), size, fixture.identity, digest);
	for (u32 truncated : {0u, 1u, size - 1, size - SHA256_DIGEST_SIZE}) {
		CAPTURE(truncated);
		checkRefusal(bytes.data(), truncated, fixture.identity, digest);
	}
	checkRefusal(bytes.data(), FORGE_LEVEL_CHECKPOINT_MAX_BYTES + 1, fixture.identity, digest);
}

TEST_CASE("Grid native snapshot refuses malformed member framing after digest verification", "[grid-edit-snapshot]")
{
	const SnapshotFixture fixture;
	const Doc opened = fixture.nativePlacements();
	Digest digest = {};
	auto bytes = encode(fixture.identity, opened.get(), digest);
	const size_t first = memberOffset(bytes, FORGE_LEVEL_MEMBER_PADS);
	SECTION("wrong version") {
		bytes.at(0)++;
	}
	SECTION("wrong member count") {
		bytes.at(first - 1)--;
	}
	SECTION("duplicate member number") {
		bytes.at(memberOffset(bytes, FORGE_LEVEL_MEMBER_SPAWNS)) = FORGE_LEVEL_MEMBER_PADS;
	}
	SECTION("unavailable required member") {
		writeU32(bytes, first + 1, 0);
	}
	SECTION("member length extends past the snapshot") {
		writeU32(bytes, first + 1, std::numeric_limits<u32>::max());
	}
	SECTION("noncanonical arena ID") {
		bytes.at(kIdLengthOffset + 1) = 'E';
	}
	SECTION("trailing member bytes") {
		bytes.insert(bytes.end() - SHA256_DIGEST_SIZE, 0);
	}
	SECTION("native member is invalid JSON even with valid framing") {
		bytes.at(first + kMemberHeaderSize) = '!';
	}
	digest = seal(bytes);
	checkRefusal(bytes.data(), static_cast<u32>(bytes.size()), fixture.identity, digest);
}

TEST_CASE("Grid native snapshot encoding leaves outputs intact on refusal", "[grid-edit-snapshot]")
{
	const SnapshotFixture fixture;
	const Doc opened = fixture.nativePlacements();
	Digest digest;
	digest.fill(0x5a);
	const Digest previous = digest;
	u32 size = 123;
	auto identity = fixture.identity;
	SECTION("zero revision") { identity.revision = 0; }
	SECTION("lounge sentinel") { identity.room.room_id = 0xff; }
	SECTION("invalid generation") { identity.room.generation = 0; }
	SECTION("unspecified base arena digest") { std::memset(identity.arena_sha256, 0, sizeof(identity.arena_sha256)); }
	SECTION("unterminated arena identifier") { std::memset(identity.arena_id, 'a', sizeof(identity.arena_id)); }
	const Bytes rejected(gridEditSnapshotEncode(&identity, opened.get(), &size, digest.data(), nullptr, 0), std::free);
	CHECK(rejected == nullptr);
	CHECK(size == 123);
	CHECK(digest == previous);
}

TEST_CASE("Grid snapshot offers and acknowledgments carry the exact revision and digest", "[grid-edit-snapshot][wire]")
{
	for (const auto kind : {GRID_EDIT_SNAPSHOT_OFFER, GRID_EDIT_SNAPSHOT_ACK}) {
		for (const u32 size : {1u, 32779u, FORGE_LEVEL_CHECKPOINT_MAX_BYTES}) {
			CAPTURE(kind, size);
			std::array<u8, kPacketPrefix + GRID_EDIT_SNAPSHOT_OFFER_WIRE_SIZE + 1> bytes;
			bytes.fill(0xa5);
			netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), kPacketPrefix, kPacketPrefix, 0};
			const auto sent = wireOffer(size);
			REQUIRE(gridEditSnapshotOfferWrite(&wire, kind, &sent));
			CHECK(wire.wp == kPacketPrefix + GRID_EDIT_SNAPSHOT_OFFER_WIRE_SIZE);
			CHECK(bytes[kPacketPrefix] == kind);
			CHECK(bytes[kWireGeneration] == 0x88);
			CHECK(bytes[kWireGeneration + 7] == 0x11);
			CHECK(bytes[kPacketPrefix - 1] == 0xa5);
			CHECK(bytes[wire.wp] == 0xa5);

			const auto wrong_kind = kind == GRID_EDIT_SNAPSHOT_OFFER ? GRID_EDIT_SNAPSHOT_ACK : GRID_EDIT_SNAPSHOT_OFFER;
			checkOfferReadRefusal(wire, wrong_kind);
			grid_edit_snapshot_offer_t received = {};
			REQUIRE(gridEditSnapshotOfferRead(&wire, kind, &received));
			CHECK(wire.rp == wire.wp);
			CHECK(wire.error == 0);
			CHECK(received.host_sequence == 0);
			CHECK(gridEditSnapshotOfferMatches(&sent, &received));
		}
	}
}

TEST_CASE("Grid snapshot offer framing refuses invalid fields transactionally", "[grid-edit-snapshot][wire]")
{
	const auto kind = GENERATE(GRID_EDIT_SNAPSHOT_OFFER, GRID_EDIT_SNAPSHOT_ACK);
	std::vector<u8> bytes(kPacketPrefix + GRID_EDIT_SNAPSHOT_OFFER_WIRE_SIZE + 1, 0xa5);
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), kPacketPrefix, kPacketPrefix, 0};
	const auto before = bytes;
	const auto sent = wireOffer(15873);
	auto short_write = wire;
	short_write.size = kPacketPrefix + GRID_EDIT_SNAPSHOT_OFFER_WIRE_SIZE - 1;
	const auto short_before = short_write;
	CHECK_FALSE(gridEditSnapshotOfferWrite(&short_write, kind, &sent));
	checkCursor(short_write, short_before);
	CHECK(bytes == before);

	auto bad_offer = sent;
	SECTION("lounge is not a room") {
		bad_offer.identity.room_id = 0xff;
	}
	SECTION("generation is required") {
		bad_offer.identity.generation = 0;
	}
	SECTION("revision is required") {
		bad_offer.revision = 0;
	}
	SECTION("empty snapshots are invalid") {
		bad_offer.size = 0;
	}
	SECTION("snapshot length is capped") {
		bad_offer.size = FORGE_LEVEL_CHECKPOINT_MAX_BYTES + 1;
	}
	SECTION("digest is required") {
		std::fill_n(bad_offer.sha256, SHA256_DIGEST_SIZE, 0);
	}

	const auto write_before = wire;
	CHECK_FALSE(gridEditSnapshotOfferWrite(&wire, kind, &bad_offer));
	checkCursor(wire, write_before);
	CHECK(bytes == before);
	REQUIRE(gridEditSnapshotOfferWrite(&wire, kind, &sent));
	auto truncated = wire;
	truncated.wp--;
	checkOfferReadRefusal(truncated, kind);
	auto trailing = wire;
	trailing.wp++;
	checkOfferReadRefusal(trailing, kind);

	bytes[kWireRoom] = bad_offer.identity.room_id;
	if (!bad_offer.identity.generation) std::fill_n(bytes.begin() + kWireGeneration, 8, 0);
	if (!bad_offer.revision) std::fill_n(bytes.begin() + kWireRevision, 8, 0);
	writeU32(bytes, kWireOfferSize, bad_offer.size);
	std::copy_n(bad_offer.sha256, SHA256_DIGEST_SIZE, bytes.begin() + kWireDigest);
	checkOfferReadRefusal(wire, kind);
}

TEST_CASE("Grid snapshot chunks accept the final bounded byte and reject overrun", "[grid-edit-snapshot][wire]")
{
	const auto size = GENERATE(1u, GRID_EDIT_SNAPSHOT_CHUNK_MAX_BYTES);
	const std::vector<u8> payload(size, 0x79);
	std::vector<u8> bytes(kPacketPrefix + GRID_EDIT_SNAPSHOT_CHUNK_WIRE_MAX + 1, 0xa5);
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), kPacketPrefix, kPacketPrefix, 0};
	const grid_edit_snapshot_chunk_t sent = {kWireIdentity, FORGE_LEVEL_CHECKPOINT_MAX_BYTES - size,
		static_cast<u16>(size), payload.data()};
	const auto write_before = wire;
	const auto buffer_before = bytes;
	auto overrun = sent;
	overrun.offset++;
	CHECK_FALSE(gridEditSnapshotChunkWrite(&wire, &overrun));
	checkCursor(wire, write_before);
	auto oversized = sent;
	oversized.size = GRID_EDIT_SNAPSHOT_CHUNK_MAX_BYTES + 1;
	CHECK_FALSE(gridEditSnapshotChunkWrite(&wire, &oversized));
	checkCursor(wire, write_before);
	CHECK(bytes == buffer_before);

	auto short_write = wire;
	short_write.size = kPacketPrefix + GRID_EDIT_SNAPSHOT_CHUNK_WIRE_HEADER_SIZE + size - 1;
	const auto short_before = short_write;
	CHECK_FALSE(gridEditSnapshotChunkWrite(&short_write, &sent));
	checkCursor(short_write, short_before);
	CHECK(bytes == buffer_before);
	REQUIRE(gridEditSnapshotChunkWrite(&wire, &sent));
	CHECK(wire.wp == kPacketPrefix + GRID_EDIT_SNAPSHOT_CHUNK_WIRE_HEADER_SIZE + size);
	CHECK(bytes[kPacketPrefix - 1] == 0xa5);
	CHECK(bytes[wire.wp] == 0xa5);

	auto truncated = wire;
	truncated.wp--;
	checkChunkReadRefusal(truncated);
	auto trailing = wire;
	trailing.wp++;
	checkChunkReadRefusal(trailing);
	writeU32(bytes, kWireChunkOffset, overrun.offset);
	checkChunkReadRefusal(wire);
	writeU32(bytes, kWireChunkOffset, std::numeric_limits<u32>::max());
	checkChunkReadRefusal(wire);
	writeU32(bytes, kWireChunkOffset, sent.offset);
	bytes[kWireRoom] = 0xff;
	checkChunkReadRefusal(wire);
	bytes[kWireRoom] = sent.identity.room_id;
	bytes[kWireChunkSize] = 0;
	bytes[kWireChunkSize + 1] = 0;
	checkChunkReadRefusal(wire);
	bytes[kWireChunkSize] = static_cast<u8>(size);
	bytes[kWireChunkSize + 1] = static_cast<u8>(size >> 8);

	grid_edit_snapshot_chunk_t received = {};
	REQUIRE(gridEditSnapshotChunkRead(&wire, &received));
	CHECK(wire.rp == wire.wp);
	CHECK(received.identity.room_id == sent.identity.room_id);
	CHECK(received.identity.generation == sent.identity.generation);
	CHECK(received.offset == sent.offset);
	REQUIRE(received.size == sent.size);
	CHECK(received.bytes == bytes.data() + kPacketPrefix + GRID_EDIT_SNAPSHOT_CHUNK_WIRE_HEADER_SIZE);
	CHECK(std::equal(payload.begin(), payload.end(), received.bytes));
}

TEST_CASE("Grid snapshot authorization rejects old offers and edited bytes across generations", "[grid-edit-snapshot][wire]")
{
	const SnapshotFixture fixture;
	const Doc opened = fixture.nativePlacements();
	Digest previous_digest = {};
	const auto previous_bytes = encode(fixture.identity, opened.get(), previous_digest);
	auto previous = wireOffer(static_cast<u32>(previous_bytes.size()));
	previous.identity = fixture.identity.room;
	previous.revision = fixture.identity.revision;
	std::copy(previous_digest.begin(), previous_digest.end(), previous.sha256);

	auto next_identity = fixture.identity;
	next_identity.room.generation++;
	auto claimed = previous;
	claimed.identity = next_identity.room;
	CHECK_FALSE(gridEditSnapshotOfferMatches(&previous, &claimed));
	checkRefusal(previous_bytes.data(), previous.size, next_identity, previous_digest);

	scenario_stage_atmosphere_t lighting = {};
	lighting.has_ambient = 1;
	lighting.ambient_light = 137;
	REQUIRE(forgeLevelDocSetAtmosphere(opened.get(), &lighting, nullptr, 0));
	Digest next_digest = {};
	const auto next_bytes = encode(next_identity, opened.get(), next_digest);
	checkRefusal(next_bytes.data(), static_cast<u32>(next_bytes.size()), next_identity, previous_digest);
	auto current = claimed;
	std::copy(next_digest.begin(), next_digest.end(), current.sha256);
	CHECK_FALSE(gridEditSnapshotOfferMatches(&claimed, &current));
	current.size = static_cast<u32>(next_bytes.size());
	const Doc accepted(gridEditSnapshotDecode(next_bytes.data(), current.size, &next_identity,
		current.sha256, nullptr, nullptr, 0), forgeLevelDocFree);
	CHECK(accepted != nullptr);

	/* This command's leading room byte is also the numeric OFFER subtype. */
	std::array<u8, kPacketPrefix + GRID_EDIT_COMMAND_WIRE_MAX> command_bytes = {};
	netbuf command_wire = {command_bytes.data(), static_cast<u32>(command_bytes.size()), kPacketPrefix, kPacketPrefix, 0};
	const grid_edit_wire_identity_t command_identity = {GRID_EDIT_SNAPSHOT_OFFER, next_identity.room.generation};
	grid_edit_command_t command = {};
	command.sequence = 1;
	command.expected_revision = next_identity.revision;
	command.operation = GRID_EDIT_SET_AMBIENT_LIGHT;
	command.data.ambient_light = {1, 137};
	REQUIRE(gridEditCommandWrite(&command_wire, &command_identity, &command));
	REQUIRE(command_bytes[kPacketPrefix] == GRID_EDIT_SNAPSHOT_OFFER);
	checkOfferReadRefusal(command_wire, GRID_EDIT_SNAPSHOT_OFFER);
}
