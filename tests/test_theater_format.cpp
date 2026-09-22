#include "catch.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

extern "C" {
#include "theater_format.h"
}

namespace fs = std::filesystem;

namespace {

fs::path theaterTemp(const char *name)
{
	fs::path dir = fs::temp_directory_path() / name;
	fs::remove_all(dir);
	fs::create_directories(dir);
	return dir;
}

std::vector<unsigned char> readBytes(const fs::path &path)
{
	std::ifstream input(path, std::ios::binary);
	return std::vector<unsigned char>(std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

void writeBytes(const fs::path &path, const std::vector<unsigned char> &bytes)
{
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write(reinterpret_cast<const char *>(bytes.data()),
		static_cast<std::streamsize>(bytes.size()));
	REQUIRE(output.good());
}

std::string readText(const fs::path &path)
{
	std::ifstream input(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

void copyId(char *destination, size_t capacity, const char *value)
{
	REQUIRE(std::strlen(value) < capacity);
	std::strncpy(destination, value, capacity - 1);
}

theater_format_match_t campaignMatch()
{
	theater_format_match_t match{};
	match.mode = THEATER_FORMAT_MODE_CAMPAIGN;
	match.authority = THEATER_FORMAT_AUTHORITY_OFFLINE;
	match.difficulty = 2;
	match.campaign_variant = THEATER_FORMAT_CAMPAIGN_SOLO;
	match.tick_rate = 60;
	match.checkpoint_interval_ticks = 12;
	match.start_time_unix = 1777777777;
	copyId(match.stage_id, sizeof(match.stage_id), "base:stage_defection");
	copyId(match.mission_id, sizeof(match.mission_id), "base:mission_defection");
	return match;
}

std::array<theater_format_manifest_entry_t, 5> campaignManifest()
{
	std::array<theater_format_manifest_entry_t, 5> entries{};
	entries[0].type = THEATER_FORMAT_MANIFEST_BODY;
	entries[0].slot = 0;
	entries[0].flags = THEATER_FORMAT_MANIFEST_REQUIRED;
	copyId(entries[0].catalog_id, sizeof(entries[0].catalog_id),
		"base:body_joanna");
	copyId(entries[0].category, sizeof(entries[0].category), "base");

	entries[1].type = THEATER_FORMAT_MANIFEST_HEAD;
	entries[1].slot = 0;
	entries[1].flags = THEATER_FORMAT_MANIFEST_REQUIRED;
	copyId(entries[1].catalog_id, sizeof(entries[1].catalog_id),
		"base:head_joanna");
	copyId(entries[1].category, sizeof(entries[1].category), "base");

	entries[2].type = THEATER_FORMAT_MANIFEST_GENERIC_ASSET;
	entries[2].slot = THEATER_FORMAT_CATALOG_TYPE_MISSION;
	entries[2].flags = THEATER_FORMAT_MANIFEST_REQUIRED;
	copyId(entries[2].catalog_id, sizeof(entries[2].catalog_id),
		"base:mission_defection");
	copyId(entries[2].category, sizeof(entries[2].category), "base");

	entries[3].type = THEATER_FORMAT_MANIFEST_MODEL;
	entries[3].slot = THEATER_FORMAT_MANIFEST_SLOT_MATCH;
	entries[3].flags = THEATER_FORMAT_MANIFEST_REQUIRED;
	copyId(entries[3].catalog_id, sizeof(entries[3].catalog_id),
		"base:model_terminal");
	copyId(entries[3].category, sizeof(entries[3].category), "base");

	entries[4].type = THEATER_FORMAT_MANIFEST_STAGE;
	entries[4].slot = THEATER_FORMAT_MANIFEST_SLOT_MATCH;
	entries[4].flags = THEATER_FORMAT_MANIFEST_REQUIRED
		| THEATER_FORMAT_MANIFEST_HAS_DIGEST
		| THEATER_FORMAT_MANIFEST_HAS_VERSION;
	entries[4].content_digest[0] = 0x51;
	entries[4].content_digest[31] = 0xa7;
	copyId(entries[4].catalog_id, sizeof(entries[4].catalog_id),
		"base:stage_defection");
	copyId(entries[4].category, sizeof(entries[4].category), "base");
	copyId(entries[4].version_id, sizeof(entries[4].version_id), "base-v1");
	return entries;
}

std::array<theater_format_manifest_entry_t, 7> twoAgentCampaignManifest()
{
	const auto single = campaignManifest();
	std::array<theater_format_manifest_entry_t, 7> entries{};
	entries[0] = single[0];
	entries[1] = single[0];
	entries[1].slot = 1;
	entries[2] = single[1];
	entries[3] = single[1];
	entries[3].slot = 1;
	entries[4] = single[2];
	entries[5] = single[3];
	entries[6] = single[4];
	return entries;
}

theater_format_roster_entry_t campaignRoster()
{
	theater_format_roster_entry_t entry{};
	entry.slot = 0;
	entry.kind = THEATER_FORMAT_PARTICIPANT_LOCAL;
	entry.role = THEATER_FORMAT_ROLE_CAMPAIGN_PRIMARY;
	copyId(entry.name, sizeof(entry.name), "Agent 1");
	copyId(entry.body_id, sizeof(entry.body_id), "base:body_joanna");
	copyId(entry.head_id, sizeof(entry.head_id), "base:head_joanna");
	return entry;
}

theater_format_match_t combatMatch(u8 authority)
{
	auto match = campaignMatch();
	match.mode = THEATER_FORMAT_MODE_COMBAT_SIMULATOR;
	match.authority = authority;
	match.campaign_variant = THEATER_FORMAT_CAMPAIGN_NONE;
	match.mission_id[0] = '\0';
	copyId(match.mode_id, sizeof(match.mode_id), "base:mode_combat");
	copyId(match.weapon_ids[0], sizeof(match.weapon_ids[0]), "base:falcon2");
	copyId(match.spawn_weapon_id, sizeof(match.spawn_weapon_id), "base:falcon2");
	match.spawn_weapon_mode = THEATER_FORMAT_SPAWN_WEAPON_SPECIFIC;
	return match;
}

std::array<theater_format_manifest_entry_t, 6> combatManifest()
{
	const auto campaign = campaignManifest();
	std::array<theater_format_manifest_entry_t, 6> entries{};
	entries[0] = campaign[0];
	entries[1].type = THEATER_FORMAT_MANIFEST_WEAPON;
	entries[1].slot = THEATER_FORMAT_MANIFEST_SLOT_MATCH;
	entries[1].flags = THEATER_FORMAT_MANIFEST_REQUIRED;
	copyId(entries[1].catalog_id, sizeof(entries[1].catalog_id), "base:falcon2");
	copyId(entries[1].category, sizeof(entries[1].category), "base");
	entries[2] = campaign[1];
	entries[3].type = THEATER_FORMAT_MANIFEST_GENERIC_ASSET;
	entries[3].slot = THEATER_FORMAT_CATALOG_TYPE_GAMEMODE;
	entries[3].flags = THEATER_FORMAT_MANIFEST_REQUIRED;
	copyId(entries[3].catalog_id, sizeof(entries[3].catalog_id),
		"base:mode_combat");
	copyId(entries[3].category, sizeof(entries[3].category), "base");
	entries[4] = campaign[3];
	entries[5] = campaign[4];
	return entries;
}

theater_format_roster_entry_t combatRoster()
{
	auto entry = campaignRoster();
	entry.kind = THEATER_FORMAT_PARTICIPANT_LOCAL;
	entry.role = THEATER_FORMAT_ROLE_NONE;
	copyId(entry.name, sizeof(entry.name), "Local Agent");
	return entry;
}

std::array<theater_format_entity_t, 2> checkpointEntities(float x = 10.0f)
{
	std::array<theater_format_entity_t, 2> entities{};
	entities[0].stable_id = 1;
	entities[0].kind = THEATER_FORMAT_ENTITY_PLAYER;
	entities[0].health = 1.0f;
	entities[0].score = 7;
	entities[0].deaths = 2;
	entities[0].position[0] = x;
	entities[0].orientation[0] = 1.0f;
	entities[0].orientation[4] = 1.0f;
	entities[0].orientation[8] = 1.0f;
	copyId(entities[0].asset_id, sizeof(entities[0].asset_id),
		"base:body_joanna");
	copyId(entities[0].secondary_asset_id,
		sizeof(entities[0].secondary_asset_id), "base:head_joanna");

	entities[1].stable_id = 2;
	entities[1].parent_id = 1;
	entities[1].kind = THEATER_FORMAT_ENTITY_OBJECT;
	entities[1].health = 100.0f;
	entities[1].position[0] = x + 2.0f;
	entities[1].orientation[0] = 1.0f;
	entities[1].orientation[4] = 1.0f;
	entities[1].orientation[8] = 1.0f;
	copyId(entities[1].asset_id, sizeof(entities[1].asset_id),
		"base:model_terminal");
	return entities;
}

theater_format_checkpoint_t checkpointAt(u64 tick,
	const std::array<theater_format_entity_t, 2> &entities,
	const theater_format_view_t &view)
{
	theater_format_checkpoint_t checkpoint{};
	checkpoint.tick = tick;
	checkpoint.elapsed_ms = tick * 1000 / 60;
	checkpoint.stage_flags = 0x100;
	checkpoint.objective_flags = 1;
	checkpoint.match_elapsed_ticks = static_cast<u32>(tick);
	checkpoint.entity_count = static_cast<u32>(entities.size());
	checkpoint.entities = entities.data();
	checkpoint.view_count = 1;
	checkpoint.views = &view;
	return checkpoint;
}

theater_format_view_t participantView(float x = 10.0f)
{
	theater_format_view_t view{};
	view.slot = 0;
	view.flags = THEATER_FORMAT_VIEW_ACTIVE | THEATER_FORMAT_VIEW_LOCAL;
	view.entity_id = 1;
	view.camera_mode = THEATER_FORMAT_CAMERA_FIRST_PERSON;
	view.position[0] = x;
	view.position[1] = 159.0f;
	view.forward[2] = -1.0f;
	view.up[1] = 1.0f;
	view.fov_y_degrees = 60.0f;
	view.aspect = 16.0f / 9.0f;
	view.aim_yaw_degrees = 45.0f;
	view.aim_pitch_degrees = -4.0f;
	return view;
}

theater_format_writer_t *beginCampaign(const fs::path &path)
{
	auto match = campaignMatch();
	auto manifest = campaignManifest();
	auto roster = campaignRoster();
	theater_format_result_t result = THEATER_FORMAT_CORRUPT;
	auto *writer = theaterFormatBegin(path.string().c_str(), &match,
		manifest.data(), static_cast<u32>(manifest.size()), &roster, 1, &result);
	REQUIRE(result == THEATER_FORMAT_OK);
	REQUIRE(writer != nullptr);
	return writer;
}

void writeCompleteRecording(const fs::path &path)
{
	auto *writer = beginCampaign(path);
	auto first = checkpointEntities(10.0f);
	auto first_view = participantView(10.0f);
	auto first_checkpoint = checkpointAt(10, first, first_view);
	REQUIRE(theaterFormatAppendCheckpoint(writer, &first_checkpoint)
		== THEATER_FORMAT_OK);

	theater_format_event_t event{};
	event.tick = 15;
	event.kind = THEATER_FORMAT_EVENT_OBJECTIVE;
	event.value[0] = 1;
	event.value[1] = 2;
	copyId(event.catalog_id, sizeof(event.catalog_id),
		"builtin:objective_status");
	REQUIRE(theaterFormatAppendEvent(writer, &event) == THEATER_FORMAT_OK);

	auto second = checkpointEntities(20.0f);
	auto second_view = participantView(20.0f);
	auto second_checkpoint = checkpointAt(22, second, second_view);
	REQUIRE(theaterFormatAppendCheckpoint(writer, &second_checkpoint)
		== THEATER_FORMAT_OK);
	REQUIRE(theaterFormatFinish(writer) == THEATER_FORMAT_OK);
}

u32 crc32(const unsigned char *bytes, size_t size)
{
	u32 crc = 0xffffffffu;
	for (size_t i = 0; i < size; i++) {
		crc ^= bytes[i];
		for (u32 bit = 0; bit < 8; bit++) {
			const u32 mask = 0u - (crc & 1u);
			crc = (crc >> 1) ^ (0xedb88320u & mask);
		}
	}
	return ~crc;
}

u32 getU32(const std::vector<unsigned char> &bytes, size_t offset)
{
	return static_cast<u32>(bytes[offset])
		| (static_cast<u32>(bytes[offset + 1]) << 8)
		| (static_cast<u32>(bytes[offset + 2]) << 16)
		| (static_cast<u32>(bytes[offset + 3]) << 24);
}

u16 getU16(const std::vector<unsigned char> &bytes, size_t offset)
{
	return static_cast<u16>(bytes[offset])
		| static_cast<u16>(static_cast<u16>(bytes[offset + 1]) << 8);
}

size_t recordOffset(const std::vector<unsigned char> &bytes, u32 sequence)
{
	size_t offset = THEATER_FORMAT_HEADER_SIZE;
	for (u32 current = 0; current < sequence; current++) {
		REQUIRE(offset + THEATER_FORMAT_RECORD_HEADER_SIZE <= bytes.size());
		offset += THEATER_FORMAT_RECORD_HEADER_SIZE + getU32(bytes, offset + 8);
	}
	REQUIRE(offset + THEATER_FORMAT_RECORD_HEADER_SIZE <= bytes.size());
	return offset;
}

void putU32(std::vector<unsigned char> &bytes, size_t offset, u32 value)
{
	for (u32 i = 0; i < 4; i++) bytes[offset + i] = (u8)(value >> (i * 8));
}

void putU64(std::vector<unsigned char> &bytes, size_t offset, u64 value)
{
	for (u32 i = 0; i < 8; i++) bytes[offset + i] = (u8)(value >> (i * 8));
}

void makeHeaderIncomplete(std::vector<unsigned char> &bytes)
{
	REQUIRE(bytes.size() >= THEATER_FORMAT_HEADER_SIZE);
	putU32(bytes, 8, 0);
	putU64(bytes, 24, 0);
	putU32(bytes, 32, 0);
	putU32(bytes, 36, 0);
	putU64(bytes, 40, 0);
	putU32(bytes, 48, 0);
	putU32(bytes, 56, crc32(bytes.data(), 56));
}

} // namespace

TEST_CASE("Theater v2 writes deterministic bounded records and seek index",
	"[theater][format][index][t-theater-001]")
{
	const fs::path dir = theaterTemp("pd2-theater-format-index");
	const fs::path first_path = dir / "first.pdth";
	const fs::path second_path = dir / "second.pdth";
	writeCompleteRecording(first_path);
	writeCompleteRecording(second_path);

	REQUIRE(readBytes(first_path) == readBytes(second_path));
	REQUIRE_FALSE(fs::exists(first_path.string() + ".part"));

	theater_format_info_t info{};
	REQUIRE(theaterFormatValidate(first_path.string().c_str(), &info)
		== THEATER_FORMAT_OK);
	REQUIRE(info.version == THEATER_FORMAT_VERSION);
	REQUIRE(info.flags == THEATER_FORMAT_FLAG_COMPLETE);
	REQUIRE(info.checkpoint_count == 2);
	REQUIRE(info.record_count == 8);
	REQUIRE(info.roster_count == 1);
	REQUIRE(info.manifest_count == 5);
	REQUIRE(std::string(info.match.stage_id) == "base:stage_defection");
	REQUIRE(std::string(info.match.mission_id) == "base:mission_defection");
	REQUIRE(info.match.campaign_variant == THEATER_FORMAT_CAMPAIGN_SOLO);

	std::array<theater_format_index_entry_t, 2> index{};
	u32 count = 0;
	REQUIRE(theaterFormatReadIndex(first_path.string().c_str(), index.data(),
		static_cast<u32>(index.size()), &count) == THEATER_FORMAT_OK);
	REQUIRE(count == 2);
	REQUIRE(index[0].tick == 10);
	REQUIRE(index[1].tick == 22);
	REQUIRE(index[0].sequence == 3);
	REQUIRE(index[1].sequence == 5);
	REQUIRE(index[0].file_offset < index[1].file_offset);
	REQUIRE(index[0].record_crc != 0);

	std::array<theater_format_manifest_entry_t, 5> manifest{};
	u32 manifest_count = 0;
	REQUIRE(theaterFormatReadManifest(first_path.string().c_str(),
		manifest.data(), static_cast<u32>(manifest.size()), &manifest_count)
		== THEATER_FORMAT_OK);
	REQUIRE(manifest_count == 5);
	REQUIRE(std::string(manifest[2].catalog_id) == "base:mission_defection");
	REQUIRE(manifest[2].type == THEATER_FORMAT_MANIFEST_GENERIC_ASSET);
	REQUIRE(manifest[2].slot == THEATER_FORMAT_CATALOG_TYPE_MISSION);
	REQUIRE(std::string(manifest[4].catalog_id) == "base:stage_defection");
	REQUIRE(std::string(manifest[4].category) == "base");
	REQUIRE(std::string(manifest[4].version_id) == "base-v1");
	REQUIRE(manifest[4].content_digest[0] == 0x51);
	REQUIRE(manifest[4].content_digest[31] == 0xa7);

	std::array<theater_format_roster_entry_t, 1> roster{};
	u32 roster_count = 0;
	REQUIRE(theaterFormatReadRoster(first_path.string().c_str(), roster.data(),
		static_cast<u32>(roster.size()), &roster_count) == THEATER_FORMAT_OK);
	REQUIRE(roster_count == 1);
	REQUIRE(roster[0].kind == THEATER_FORMAT_PARTICIPANT_LOCAL);
	REQUIRE(roster[0].role == THEATER_FORMAT_ROLE_CAMPAIGN_PRIMARY);

	theater_format_checkpoint_t loaded{};
	std::array<theater_format_entity_t, 2> loaded_entities{};
	std::array<theater_format_view_t, 1> loaded_views{};
	REQUIRE(theaterFormatReadCheckpoint(first_path.string().c_str(), 1,
		&loaded, loaded_entities.data(), static_cast<u32>(loaded_entities.size()),
		loaded_views.data(), static_cast<u32>(loaded_views.size()))
		== THEATER_FORMAT_OK);
	REQUIRE(loaded.tick == 22);
	REQUIRE(loaded.entity_count == 2);
	REQUIRE(loaded.view_count == 1);
	REQUIRE(loaded.entities == loaded_entities.data());
	REQUIRE(loaded.views == loaded_views.data());
	REQUIRE(loaded_entities[0].orientation[0] == Approx(1.0f));
	REQUIRE(loaded_entities[0].orientation[8] == Approx(1.0f));
	REQUIRE(loaded_entities[0].score == 7);
	REQUIRE(loaded_entities[0].deaths == 2);
	REQUIRE(loaded_views[0].position[0] == Approx(20.0f));
	REQUIRE(loaded_views[0].forward[2] == Approx(-1.0f));
	REQUIRE(loaded_views[0].fov_y_degrees == Approx(60.0f));
	REQUIRE(loaded_views[0].aim_pitch_degrees == Approx(-4.0f));

	fs::remove_all(dir);
}

TEST_CASE("Theater v2 rejects invalid identity and non-finite world state",
	"[theater][format][identity][t-theater-001]")
{
	const fs::path dir = theaterTemp("pd2-theater-format-identity");
	auto match = campaignMatch();
	auto manifest = campaignManifest();
	auto roster = campaignRoster();
	theater_format_result_t result = THEATER_FORMAT_OK;

	match.manifest_digest[0] = 0xff;
	REQUIRE(theaterFormatBegin((dir / "wrong-digest.pdth").string().c_str(),
		&match, manifest.data(), static_cast<u32>(manifest.size()),
		&roster, 1, &result) == nullptr);
	REQUIRE(result == THEATER_FORMAT_INVALID_IDENTITY);

	match = campaignMatch();
	roster.body_id[0] = '\0';
	REQUIRE(theaterFormatBegin((dir / "no-body.pdth").string().c_str(),
		&match, manifest.data(), static_cast<u32>(manifest.size()),
		&roster, 1, &result) == nullptr);
	REQUIRE(result == THEATER_FORMAT_INVALID_IDENTITY);

	match = campaignMatch();
	manifest = campaignManifest();
	roster = campaignRoster();
	manifest[2].type = THEATER_FORMAT_MANIFEST_MODEL;
	REQUIRE(theaterFormatBegin((dir / "wrong-mission-type.pdth").string().c_str(),
		&match, manifest.data(), static_cast<u32>(manifest.size()),
		&roster, 1, &result) == nullptr);
	REQUIRE(result == THEATER_FORMAT_INVALID_IDENTITY);

	auto *writer = beginCampaign(dir / "world.pdth");
	auto entities = checkpointEntities();
	auto view = participantView();
	std::swap(entities[0], entities[1]);
	auto checkpoint = checkpointAt(10, entities, view);
	REQUIRE(theaterFormatAppendCheckpoint(writer, &checkpoint)
		== THEATER_FORMAT_INVALID_IDENTITY);
	std::swap(entities[0], entities[1]);
	entities[1].parent_id = 99;
	checkpoint = checkpointAt(10, entities, view);
	REQUIRE(theaterFormatAppendCheckpoint(writer, &checkpoint)
		== THEATER_FORMAT_INVALID_IDENTITY);
	entities[1].parent_id = 1;
	entities[0].position[0] = std::numeric_limits<float>::quiet_NaN();
	checkpoint = checkpointAt(10, entities, view);
	REQUIRE(theaterFormatAppendCheckpoint(writer, &checkpoint)
		== THEATER_FORMAT_INVALID_IDENTITY);
	entities[0].position[0] = 10.0f;
	copyId(entities[1].asset_id, sizeof(entities[1].asset_id),
		"base:mission_defection");
	checkpoint = checkpointAt(10, entities, view);
	REQUIRE(theaterFormatAppendCheckpoint(writer, &checkpoint)
		== THEATER_FORMAT_INVALID_IDENTITY);
	theaterFormatAbort(writer);
	REQUIRE_FALSE(fs::exists(dir / "world.pdth.part"));

	fs::remove_all(dir);
}

TEST_CASE("Theater v2 round-trips cooperative and counter-operative roles",
	"[theater][format][campaign][roster][t-theater-001]")
{
	const fs::path dir = theaterTemp("pd2-theater-format-campaign-roles");
	for (const u8 variant : {static_cast<u8>(THEATER_FORMAT_CAMPAIGN_COOPERATIVE),
			static_cast<u8>(THEATER_FORMAT_CAMPAIGN_COUNTER_OPERATIVE)}) {
		auto match = campaignMatch();
		match.campaign_variant = variant;
		auto manifest = twoAgentCampaignManifest();
		std::array<theater_format_roster_entry_t, 2> roster{};
		roster[0] = campaignRoster();
		roster[1] = campaignRoster();
		roster[1].slot = 1;
		roster[1].kind = THEATER_FORMAT_PARTICIPANT_REMOTE;
		roster[1].role = variant == THEATER_FORMAT_CAMPAIGN_COOPERATIVE
			? THEATER_FORMAT_ROLE_CAMPAIGN_COOPERATIVE
			: THEATER_FORMAT_ROLE_CAMPAIGN_COUNTER_OPERATIVE;
		copyId(roster[1].name, sizeof(roster[1].name),
			variant == THEATER_FORMAT_CAMPAIGN_COOPERATIVE
				? "Cooperative Agent" : "Counter-Operative");
		const fs::path path = dir / (variant
			== THEATER_FORMAT_CAMPAIGN_COOPERATIVE ? "coop.pdth" : "anti.pdth");
		theater_format_result_t result = THEATER_FORMAT_CORRUPT;
		auto *writer = theaterFormatBegin(path.string().c_str(), &match,
			manifest.data(), static_cast<u32>(manifest.size()), roster.data(),
			static_cast<u32>(roster.size()), &result);
		REQUIRE(result == THEATER_FORMAT_OK);
		REQUIRE(writer != nullptr);
		const auto base_entities = checkpointEntities();
		std::array<theater_format_entity_t, 3> entities{};
		entities[0] = base_entities[0];
		entities[1] = base_entities[1];
		entities[2] = base_entities[0];
		entities[2].stable_id = 3;
		entities[2].position[0] = 30.0f;
		std::array<theater_format_view_t, 2> views{};
		views[0] = participantView();
		views[1] = participantView(30.0f);
		views[1].slot = 1;
		views[1].flags = THEATER_FORMAT_VIEW_ACTIVE;
		views[1].entity_id = 3;
		theater_format_checkpoint_t checkpoint{};
		checkpoint.tick = 10;
		checkpoint.elapsed_ms = 10000 / 60;
		checkpoint.match_elapsed_ticks = 10;
		checkpoint.entity_count = static_cast<u32>(entities.size());
		checkpoint.entities = entities.data();
		checkpoint.view_count = 1;
		checkpoint.views = views.data();
		REQUIRE(theaterFormatAppendCheckpoint(writer, &checkpoint)
			== THEATER_FORMAT_INVALID_IDENTITY);
		checkpoint.view_count = static_cast<u32>(views.size());
		REQUIRE(theaterFormatAppendCheckpoint(writer, &checkpoint)
			== THEATER_FORMAT_OK);
		REQUIRE(theaterFormatFinish(writer) == THEATER_FORMAT_OK);

		theater_format_info_t info{};
		REQUIRE(theaterFormatValidate(path.string().c_str(), &info)
			== THEATER_FORMAT_OK);
		REQUIRE(info.match.campaign_variant == variant);
		std::array<theater_format_roster_entry_t, 2> loaded{};
		u32 count = 0;
		REQUIRE(theaterFormatReadRoster(path.string().c_str(), loaded.data(),
			static_cast<u32>(loaded.size()), &count) == THEATER_FORMAT_OK);
		REQUIRE(count == 2);
		REQUIRE(loaded[0].role == THEATER_FORMAT_ROLE_CAMPAIGN_PRIMARY);
		REQUIRE(loaded[1].kind == THEATER_FORMAT_PARTICIPANT_REMOTE);
		REQUIRE(loaded[1].role == roster[1].role);
		theater_format_checkpoint_t loaded_checkpoint{};
		std::array<theater_format_entity_t, 3> loaded_entities{};
		std::array<theater_format_view_t, 2> loaded_views{};
		REQUIRE(theaterFormatReadCheckpoint(path.string().c_str(), 0,
			&loaded_checkpoint, loaded_entities.data(),
			static_cast<u32>(loaded_entities.size()), loaded_views.data(),
			static_cast<u32>(loaded_views.size())) == THEATER_FORMAT_OK);
		REQUIRE(loaded_checkpoint.view_count == 2);
		REQUIRE(loaded_views[1].slot == 1);
		REQUIRE(loaded_views[1].entity_id == 3);
		REQUIRE((loaded_views[1].flags & THEATER_FORMAT_VIEW_LOCAL) == 0);
	}

	auto invalid_match = campaignMatch();
	invalid_match.campaign_variant = THEATER_FORMAT_CAMPAIGN_COOPERATIVE;
	auto manifest = campaignManifest();
	auto invalid_roster = campaignRoster();
	theater_format_result_t invalid_result = THEATER_FORMAT_OK;
	REQUIRE(theaterFormatBegin((dir / "missing-coop.pdth").string().c_str(),
		&invalid_match, manifest.data(), static_cast<u32>(manifest.size()),
		&invalid_roster, 1, &invalid_result) == nullptr);
	REQUIRE(invalid_result == THEATER_FORMAT_INVALID_IDENTITY);

	fs::remove_all(dir);
}

TEST_CASE("Theater v2 persists local and listen-authority Combat Simulator identity",
	"[theater][format][combat-sim][authority][t-theater-001]")
{
	const fs::path dir = theaterTemp("pd2-theater-format-combat-authority");
	for (const u8 authority : {static_cast<u8>(THEATER_FORMAT_AUTHORITY_OFFLINE),
			static_cast<u8>(THEATER_FORMAT_AUTHORITY_LISTEN)}) {
		auto match = combatMatch(authority);
		auto manifest = combatManifest();
		auto roster = combatRoster();
		const fs::path path = dir / (authority
			== THEATER_FORMAT_AUTHORITY_OFFLINE ? "local.pdth" : "listen.pdth");
		theater_format_result_t result = THEATER_FORMAT_CORRUPT;
		auto *writer = theaterFormatBegin(path.string().c_str(), &match,
			manifest.data(), static_cast<u32>(manifest.size()), &roster, 1,
			&result);
		REQUIRE(result == THEATER_FORMAT_OK);
		REQUIRE(writer != nullptr);
		auto entities = checkpointEntities();
		auto view = participantView();
		auto checkpoint = checkpointAt(10, entities, view);
		REQUIRE(theaterFormatAppendCheckpoint(writer, &checkpoint)
			== THEATER_FORMAT_OK);
		REQUIRE(theaterFormatFinish(writer) == THEATER_FORMAT_OK);

		theater_format_info_t info{};
		REQUIRE(theaterFormatValidate(path.string().c_str(), &info)
			== THEATER_FORMAT_OK);
		REQUIRE(info.match.mode == THEATER_FORMAT_MODE_COMBAT_SIMULATOR);
		REQUIRE(info.match.authority == authority);
		REQUIRE(info.match.campaign_variant == THEATER_FORMAT_CAMPAIGN_NONE);
		REQUIRE(std::string(info.match.mode_id) == "base:mode_combat");
		REQUIRE(info.match.mission_id[0] == '\0');
		REQUIRE(std::string(info.match.weapon_ids[0]) == "base:falcon2");
		REQUIRE(std::string(info.match.spawn_weapon_id) == "base:falcon2");
		REQUIRE(info.match.spawn_weapon_mode
			== THEATER_FORMAT_SPAWN_WEAPON_SPECIFIC);
	}

	fs::remove_all(dir);
}

TEST_CASE("Theater v2 validation rejects damage without partial publication",
	"[theater][format][fail-closed][t-theater-001]")
{
	const fs::path dir = theaterTemp("pd2-theater-format-reject");
	const fs::path corrupt_path = dir / "corrupt.pdth";
	writeCompleteRecording(corrupt_path);
	std::array<theater_format_index_entry_t, 2> index{};
	u32 count = 0;
	REQUIRE(theaterFormatReadIndex(corrupt_path.string().c_str(), index.data(),
		2, &count) == THEATER_FORMAT_OK);
	auto bytes = readBytes(corrupt_path);
	bytes[static_cast<size_t>(index[0].file_offset)
		+ THEATER_FORMAT_RECORD_HEADER_SIZE + 7] ^= 0x80;
	writeBytes(corrupt_path, bytes);
	theater_format_info_t sentinel;
	std::memset(&sentinel, 0x5a, sizeof(sentinel));
	const theater_format_info_t before = sentinel;
	REQUIRE(theaterFormatValidate(corrupt_path.string().c_str(), &sentinel)
		== THEATER_FORMAT_CORRUPT);
	REQUIRE(std::memcmp(&sentinel, &before, sizeof(sentinel)) == 0);

	const fs::path manifest_tamper_path = dir / "manifest-tamper.pdth";
	writeCompleteRecording(manifest_tamper_path);
	bytes = readBytes(manifest_tamper_path);
	const size_t manifest_record = recordOffset(bytes, 1);
	const u32 manifest_length = getU32(bytes, manifest_record + 8);
	const size_t manifest_payload = manifest_record
		+ THEATER_FORMAT_RECORD_HEADER_SIZE;
	const size_t first_id_length = manifest_payload + 4 + 40;
	const size_t first_category_length = first_id_length + 2
		+ getU16(bytes, first_id_length);
	const size_t first_category = first_category_length + 2;
	REQUIRE(getU16(bytes, first_category_length) == 4);
	bytes[first_category] = 'c'; /* valid inventory; match digest is now stale */
	putU32(bytes, manifest_record + 24,
		crc32(bytes.data() + manifest_payload, manifest_length));
	putU32(bytes, manifest_record + 28,
		crc32(bytes.data() + manifest_record, 28));
	writeBytes(manifest_tamper_path, bytes);
	REQUIRE(theaterFormatValidate(manifest_tamper_path.string().c_str(),
		&sentinel) == THEATER_FORMAT_CORRUPT);
	REQUIRE(std::memcmp(&sentinel, &before, sizeof(sentinel)) == 0);

	const fs::path version_path = dir / "version.pdth";
	writeCompleteRecording(version_path);
	bytes = readBytes(version_path);
	bytes[4] = 99;
	bytes[5] = 0;
	writeBytes(version_path, bytes);
	REQUIRE(theaterFormatValidate(version_path.string().c_str(), &sentinel)
		== THEATER_FORMAT_UNSUPPORTED_VERSION);
	REQUIRE(std::memcmp(&sentinel, &before, sizeof(sentinel)) == 0);

	const fs::path truncated_path = dir / "truncated.pdth";
	writeCompleteRecording(truncated_path);
	fs::resize_file(truncated_path, fs::file_size(truncated_path) - 1);
	REQUIRE(theaterFormatValidate(truncated_path.string().c_str(), &sentinel)
		== THEATER_FORMAT_CORRUPT);
	REQUIRE(std::memcmp(&sentinel, &before, sizeof(sentinel)) == 0);

	std::array<theater_format_index_entry_t, 1> too_small{};
	too_small[0].tick = 0xfeed;
	u32 untouched_count = 0xbeef;
	const fs::path valid_path = dir / "valid.pdth";
	writeCompleteRecording(valid_path);
	REQUIRE(theaterFormatReadIndex(valid_path.string().c_str(), too_small.data(),
		1, &untouched_count) == THEATER_FORMAT_LIMIT_EXCEEDED);
	REQUIRE(too_small[0].tick == 0xfeed);
	REQUIRE(untouched_count == 0xbeef);

	const fs::path oversized_path = dir / "oversized.pdth";
	std::ofstream(oversized_path, std::ios::binary).put('\0');
	fs::resize_file(oversized_path,
		static_cast<uintmax_t>(THEATER_FORMAT_MAX_FILE_SIZE) + 1u);
	REQUIRE(theaterFormatValidate(oversized_path.string().c_str(), &sentinel)
		== THEATER_FORMAT_OVERSIZED);

	fs::remove_all(dir);
}

TEST_CASE("Theater v2 rejects a CRC-invalid terminal record during recovery",
	"[theater][format][interruption][corrupt][t-theater-001]")
{
	const fs::path dir = theaterTemp("pd2-theater-format-interrupted");
	const fs::path final_path = dir / "recover.pdth";
	auto *writer = beginCampaign(final_path);
	auto first = checkpointEntities(1.0f);
	auto first_view = participantView(1.0f);
	auto first_checkpoint = checkpointAt(10, first, first_view);
	REQUIRE(theaterFormatAppendCheckpoint(writer, &first_checkpoint)
		== THEATER_FORMAT_OK);
	theater_format_event_t event{};
	event.tick = 15;
	event.kind = THEATER_FORMAT_EVENT_SCORE;
	REQUIRE(theaterFormatAppendEvent(writer, &event) == THEATER_FORMAT_OK);
	auto second = checkpointEntities(2.0f);
	auto second_view = participantView(2.0f);
	auto second_checkpoint = checkpointAt(22, second, second_view);
	REQUIRE(theaterFormatAppendCheckpoint(writer, &second_checkpoint)
		== THEATER_FORMAT_OK);
	theaterFormatAbandon(writer);
	const fs::path part_path = final_path.string() + ".part";
	REQUIRE(fs::exists(part_path));
	auto corrupted = readBytes(part_path);
	corrupted.back() ^= 0x80; /* full payload with a mismatched payload CRC */
	writeBytes(part_path, corrupted);

	REQUIRE(theaterFormatRecoverInterrupted(final_path.string().c_str())
		== THEATER_FORMAT_CORRUPT);
	REQUIRE_FALSE(fs::exists(final_path));
	REQUIRE(fs::exists(part_path));

	const fs::path header_final_path = dir / "header.pdth";
	writer = beginCampaign(header_final_path);
	REQUIRE(theaterFormatAppendCheckpoint(writer, &first_checkpoint)
		== THEATER_FORMAT_OK);
	REQUIRE(theaterFormatAppendCheckpoint(writer, &second_checkpoint)
		== THEATER_FORMAT_OK);
	theaterFormatAbandon(writer);
	const fs::path header_part_path = header_final_path.string() + ".part";
	corrupted = readBytes(header_part_path);
	const size_t terminal_record = recordOffset(corrupted, 4);
	corrupted[terminal_record + 28] ^= 0x80; /* full header, bad header CRC */
	writeBytes(header_part_path, corrupted);
	REQUIRE(theaterFormatRecoverInterrupted(header_final_path.string().c_str())
		== THEATER_FORMAT_CORRUPT);
	REQUIRE_FALSE(fs::exists(header_final_path));
	REQUIRE(fs::exists(header_part_path));

	fs::remove_all(dir);
}

TEST_CASE("Theater v2 recovers only a physically torn terminal tail",
	"[theater][format][interruption][torn][t-theater-001]")
{
	const fs::path dir = theaterTemp("pd2-theater-format-physical-torn-tail");
	const fs::path final_path = dir / "recover.pdth";
	auto *writer = beginCampaign(final_path);
	auto first = checkpointEntities(1.0f);
	auto first_view = participantView(1.0f);
	auto first_checkpoint = checkpointAt(10, first, first_view);
	REQUIRE(theaterFormatAppendCheckpoint(writer, &first_checkpoint)
		== THEATER_FORMAT_OK);
	theater_format_event_t event{};
	event.tick = 15;
	event.kind = THEATER_FORMAT_EVENT_SCORE;
	REQUIRE(theaterFormatAppendEvent(writer, &event) == THEATER_FORMAT_OK);
	auto second = checkpointEntities(2.0f);
	auto second_view = participantView(2.0f);
	auto second_checkpoint = checkpointAt(22, second, second_view);
	REQUIRE(theaterFormatAppendCheckpoint(writer, &second_checkpoint)
		== THEATER_FORMAT_OK);
	theaterFormatAbandon(writer);
	const fs::path part_path = final_path.string() + ".part";
	REQUIRE(fs::file_size(part_path) > 7);
	fs::resize_file(part_path, fs::file_size(part_path) - 7);

	REQUIRE(theaterFormatRecoverInterrupted(final_path.string().c_str())
		== THEATER_FORMAT_OK);
	REQUIRE(fs::exists(final_path));
	REQUIRE_FALSE(fs::exists(part_path));
	theater_format_info_t info{};
	REQUIRE(theaterFormatValidate(final_path.string().c_str(), &info)
		== THEATER_FORMAT_OK);
	REQUIRE(info.flags == (THEATER_FORMAT_FLAG_COMPLETE
		| THEATER_FORMAT_FLAG_RECOVERED));
	REQUIRE(info.checkpoint_count == 1);
	REQUIRE(info.record_count == 7);

	fs::remove_all(dir);
}

TEST_CASE("Theater v2 rejects non-terminal corruption during recovery",
	"[theater][format][interruption][corrupt][t-theater-001]")
{
	const fs::path dir = theaterTemp("pd2-theater-format-midstream-corrupt");
	const fs::path final_path = dir / "midstream.pdth";
	auto *writer = beginCampaign(final_path);
	auto first = checkpointEntities(1.0f);
	auto first_view = participantView(1.0f);
	auto first_checkpoint = checkpointAt(10, first, first_view);
	REQUIRE(theaterFormatAppendCheckpoint(writer, &first_checkpoint)
		== THEATER_FORMAT_OK);
	auto second = checkpointEntities(2.0f);
	auto second_view = participantView(2.0f);
	auto second_checkpoint = checkpointAt(22, second, second_view);
	REQUIRE(theaterFormatAppendCheckpoint(writer, &second_checkpoint)
		== THEATER_FORMAT_OK);
	theaterFormatAbandon(writer);
	const fs::path part_path = final_path.string() + ".part";
	auto bytes = readBytes(part_path);
	const size_t first_checkpoint_record = recordOffset(bytes, 3);
	bytes[first_checkpoint_record + THEATER_FORMAT_RECORD_HEADER_SIZE + 9]
		^= 0x40;
	writeBytes(part_path, bytes);

	REQUIRE(theaterFormatRecoverInterrupted(final_path.string().c_str())
		== THEATER_FORMAT_CORRUPT);
	REQUIRE_FALSE(fs::exists(final_path));
	REQUIRE(fs::exists(part_path));

	fs::remove_all(dir);
}

TEST_CASE("Theater v2 marks a reconstructed interrupted prefix as recovered",
	"[theater][format][interruption][index][t-theater-001]")
{
	const fs::path dir = theaterTemp("pd2-theater-format-finalize");
	const fs::path final_path = dir / "finalize.pdth";
	writeCompleteRecording(final_path);
	theater_format_info_t original{};
	REQUIRE(theaterFormatValidate(final_path.string().c_str(), &original)
		== THEATER_FORMAT_OK);
	auto bytes = readBytes(final_path);
	makeHeaderIncomplete(bytes);
	REQUIRE(original.file_size >= THEATER_FORMAT_RECORD_HEADER_SIZE);
	bytes.resize(bytes.size() - THEATER_FORMAT_RECORD_HEADER_SIZE);
	fs::remove(final_path);
	const fs::path part_path = final_path.string() + ".part";
	writeBytes(part_path, bytes);

	REQUIRE(theaterFormatRecoverInterrupted(final_path.string().c_str())
		== THEATER_FORMAT_OK);
	theater_format_info_t recovered{};
	REQUIRE(theaterFormatValidate(final_path.string().c_str(), &recovered)
		== THEATER_FORMAT_OK);
	REQUIRE(recovered.checkpoint_count == original.checkpoint_count);
	REQUIRE(recovered.record_count == original.record_count);
	REQUIRE(recovered.flags == (THEATER_FORMAT_FLAG_COMPLETE
		| THEATER_FORMAT_FLAG_RECOVERED));

	fs::remove_all(dir);
}

TEST_CASE("Theater v2 repairs a durable END with clean completion state",
	"[theater][format][interruption][header][t-theater-001]")
{
	const fs::path dir = theaterTemp("pd2-theater-format-header-repair");
	const fs::path final_path = dir / "header-repair.pdth";
	writeCompleteRecording(final_path);
	theater_format_info_t original{};
	REQUIRE(theaterFormatValidate(final_path.string().c_str(), &original)
		== THEATER_FORMAT_OK);
	auto bytes = readBytes(final_path);
	makeHeaderIncomplete(bytes);
	fs::remove(final_path);
	const fs::path part_path = final_path.string() + ".part";
	writeBytes(part_path, bytes);

	REQUIRE(theaterFormatRecoverInterrupted(final_path.string().c_str())
		== THEATER_FORMAT_OK);
	theater_format_info_t repaired{};
	REQUIRE(theaterFormatValidate(final_path.string().c_str(), &repaired)
		== THEATER_FORMAT_OK);
	REQUIRE(repaired.flags == THEATER_FORMAT_FLAG_COMPLETE);
	REQUIRE(repaired.file_size == original.file_size);
	REQUIRE(repaired.record_count == original.record_count);
	REQUIRE(repaired.checkpoint_count == original.checkpoint_count);

	fs::remove_all(dir);
}

TEST_CASE("Theater v2 rejects recovered without complete",
	"[theater][format][flags][corrupt][t-theater-001]")
{
	const fs::path dir = theaterTemp("pd2-theater-format-recovered-invariant");
	const fs::path final_path = dir / "invalid-recovered.pdth";
	writeCompleteRecording(final_path);
	auto bytes = readBytes(final_path);
	putU32(bytes, 8, THEATER_FORMAT_FLAG_RECOVERED);
	putU32(bytes, 56, crc32(bytes.data(), 56));
	writeBytes(final_path, bytes);

	theater_format_info_t untouched{};
	untouched.flags = 0xfeedu;
	REQUIRE(theaterFormatValidate(final_path.string().c_str(), &untouched)
		== THEATER_FORMAT_CORRUPT);
	REQUIRE(untouched.flags == 0xfeedu);

	fs::remove_all(dir);
}

TEST_CASE("Theater v2 rollback finalizes the last accepted checkpoint",
	"[theater][format][lifecycle][rollback][t-theater-001]")
{
	const fs::path dir = theaterTemp("pd2-theater-format-rollback");
	const fs::path final_path = dir / "teardown.pdth";
	auto *writer = beginCampaign(final_path);
	auto entities = checkpointEntities(10.0f);
	auto view = participantView(10.0f);
	auto checkpoint = checkpointAt(10, entities, view);
	REQUIRE(theaterFormatAppendCheckpoint(writer, &checkpoint)
		== THEATER_FORMAT_OK);
	theater_format_mark_t accepted{};
	REQUIRE(theaterFormatMark(writer, &accepted) == THEATER_FORMAT_OK);
	theater_format_event_t foreign{};
	foreign.tick = 20;
	foreign.kind = THEATER_FORMAT_EVENT_STAGE_BEGIN;
	copyId(foreign.catalog_id, sizeof(foreign.catalog_id),
		"base:stage_investigation");
	REQUIRE(theaterFormatAppendEvent(writer, &foreign) == THEATER_FORMAT_OK);
	REQUIRE(theaterFormatRollback(writer, &accepted) == THEATER_FORMAT_OK);
	REQUIRE(theaterFormatFinish(writer) == THEATER_FORMAT_OK);

	theater_format_info_t info{};
	REQUIRE(theaterFormatValidate(final_path.string().c_str(), &info)
		== THEATER_FORMAT_OK);
	REQUIRE(info.checkpoint_count == 1);
	REQUIRE(info.record_count == 6);
	theater_format_checkpoint_t loaded{};
	std::array<theater_format_entity_t, 2> loaded_entities{};
	std::array<theater_format_view_t, 1> loaded_views{};
	REQUIRE(theaterFormatReadCheckpoint(final_path.string().c_str(), 0,
		&loaded, loaded_entities.data(), 2, loaded_views.data(), 1)
		== THEATER_FORMAT_OK);
	REQUIRE(loaded.tick == 10);
	REQUIRE(loaded_entities[0].position[0] == Approx(10.0f));

	fs::remove_all(dir);
}

TEST_CASE("Theater v2 refuses overwrite and keeps corrupt candidates isolated",
	"[theater][format][atomic][t-theater-001]")
{
	const fs::path dir = theaterTemp("pd2-theater-format-atomic");
	const fs::path final_path = dir / "existing.pdth";
	std::ofstream(final_path, std::ios::binary) << "existing";
	auto match = campaignMatch();
	auto manifest = campaignManifest();
	auto roster = campaignRoster();
	theater_format_result_t result = THEATER_FORMAT_OK;
	REQUIRE(theaterFormatBegin(final_path.string().c_str(), &match,
		manifest.data(), static_cast<u32>(manifest.size()), &roster, 1,
		&result) == nullptr);
	REQUIRE(result == THEATER_FORMAT_IO_ERROR);
	REQUIRE(readText(final_path) == "existing");

	const fs::path corrupt_final = dir / "candidate.pdth";
	const fs::path corrupt_part = corrupt_final.string() + ".part";
	std::ofstream(corrupt_part, std::ios::binary) << "not-a-theater-file";
	REQUIRE(theaterFormatRecoverInterrupted(corrupt_final.string().c_str())
		== THEATER_FORMAT_TRUNCATED);
	REQUIRE_FALSE(fs::exists(corrupt_final));
	REQUIRE(fs::exists(corrupt_part));

	fs::remove_all(dir);
}

TEST_CASE("Theater production adapter consumes exact prop generations",
	"[theater][static][authority][t-theater-001]")
{
	const std::string adapter = readText("port/src/theater.c");
	const std::string codec = readText("port/src/theater_format.c");
	REQUIRE(adapter.find("THEATER_FORMAT_AUTHORITY_LISTEN") != std::string::npos);
	REQUIRE(adapter.find("THEATER_FORMAT_MODE_CAMPAIGN") != std::string::npos);
	REQUIRE(adapter.find("THEATER_FORMAT_MODE_COMBAT_SIMULATOR")
		!= std::string::npos);
	REQUIRE(adapter.find("spectatorGet") == std::string::npos);
	REQUIRE(adapter.find("spectatorBeginTheater") == std::string::npos);
	REQUIRE(adapter.find("catalogModelIdByModelnum") != std::string::npos);
	REQUIRE(adapter.find("catalogWeaponIdByRuntimeWeaponNum")
		!= std::string::npos);
	REQUIRE(adapter.find("scenarioSourceActiveMissionId") != std::string::npos);
	REQUIRE(adapter.find("copyCatalogId(match->mission_id, stage_id")
		== std::string::npos);
	REQUIRE(adapter.find("THEATER_FORMAT_ROLE_CAMPAIGN_COOPERATIVE")
		!= std::string::npos);
	REQUIRE(adapter.find("THEATER_FORMAT_ROLE_CAMPAIGN_COUNTER_OPERATIVE")
		!= std::string::npos);
	REQUIRE(adapter.find("theaterEntityLifecycleIdentityAvailable")
		!= std::string::npos);
	const size_t identity_availability = adapter.find(
		"static s32 theaterEntityLifecycleIdentityAvailable");
	const size_t identity_accessor = adapter.find(
		"static u64 theaterPropLifecycleGeneration", identity_availability);
	REQUIRE(identity_availability != std::string::npos);
	REQUIRE(identity_accessor != std::string::npos);
	REQUIRE(adapter.substr(identity_availability,
		identity_accessor - identity_availability).find("return 1;")
		!= std::string::npos);
	REQUIRE(adapter.find("#include \"game/prop.h\"") != std::string::npos);
	REQUIRE(adapter.find("return propGetLifecycleGeneration(prop);")
		!= std::string::npos);
	REQUIRE(adapter.find("recordable && generation == 0")
		!= std::string::npos);
	REQUIRE(adapter.find("slot->backing") == std::string::npos);
	REQUIRE(adapter.find("entry->recovered = (info.flags & THEATER_FORMAT_FLAG_RECOVERED)")
		!= std::string::npos);
	REQUIRE(adapter.find("live_state_mutated=0") != std::string::npos);
	REQUIRE(adapter.find("stage_changed_without_recapture") != std::string::npos);
	REQUIRE(adapter.find("shutdown_without_recapture") != std::string::npos);
	REQUIRE(adapter.find("finalizing last accepted prefix") != std::string::npos);
	REQUIRE(adapter.find("propRecordable") != std::string::npos);
	REQUIRE(adapter.find("effects_explosions_smoke_not_captured=1")
		!= std::string::npos);
	REQUIRE(adapter.find("entity->state[") == std::string::npos);
	REQUIRE(adapter.find("prop->obj->flags") == std::string::npos);
	REQUIRE(codec.find("THEATER_FORMAT_ENTITY_EFFECT") == std::string::npos);
	REQUIRE(codec.find("fwrite(&") == std::string::npos);
	REQUIRE(codec.find("THEATER_FORMAT_MAX_RECORD_PAYLOAD") != std::string::npos);

	const size_t stage_gate = adapter.find(
		"strcmp(stage_id, s_Recorder.match.stage_id) != 0");
	REQUIRE(stage_gate != std::string::npos);
	const size_t stage_return = adapter.find("return;", stage_gate);
	REQUIRE(stage_return != std::string::npos);
	const std::string stage_block = adapter.substr(stage_gate,
		stage_return - stage_gate);
	REQUIRE(stage_block.find("finalizeAcceptedRecording") != std::string::npos);
	REQUIRE(stage_block.find("captureCheckpoint") == std::string::npos);

	const size_t shutdown = adapter.find("void theaterShutdown(void)");
	REQUIRE(shutdown != std::string::npos);
	const std::string shutdown_block = adapter.substr(shutdown, 220);
	REQUIRE(shutdown_block.find("finalizeAcceptedRecording")
		!= std::string::npos);
	REQUIRE(shutdown_block.find("captureCheckpoint") == std::string::npos);
}
