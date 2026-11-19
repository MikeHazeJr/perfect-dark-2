#include "catch.hpp"

#include "modarchive.h"
#include "scenario_portal_source.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

using PortalRows = std::unique_ptr<scenario_source_portal_row_t, decltype(&std::free)>;

const char *const kPortal = R"({"portal_ref":"portal_0000","room_a":"room_1","room_b":"room_2",)"
	R"("flags":"0x03","light_flood_blocked":true,"open_to_light":false,"vertices":[[0,0,0],[0,0,5],[0,6,5],[0,6,0]]})";

std::string table(const std::string &rows)
{
	return "{\"schema\":\"pd2.scenario.portals.v1\",\"rows\":[" + rows + "]}";
}

} // namespace

TEST_CASE("Native portal source consumes every row and preserves supported flag forms", "[scenario][portal]")
{
	char err[160] = {};
	const auto valid = table(kPortal);
	scenario_source_portal_row_t *raw = nullptr;
	s32 count = -1;
	REQUIRE(scenarioPortalSourceRead(valid.data(), valid.size(), &raw, &count, err, sizeof(err)));
	const PortalRows rows(raw, std::free);
	REQUIRE(count == 1);
	CHECK(rows->room1 == 1);
	CHECK(rows->room2 == 2);
	CHECK(rows->flags == 3);
	CHECK(rows->light_rules == SCENARIO_PORTAL_RULE_FLOOD_BLOCKED);
	CHECK(rows->vertices[2][1] == 6.0f);
	CHECK(scenarioPortalSourceCheckCandidate(rows.get(), count, 3, err, sizeof(err)));

	const auto invalid_tail = table(std::string(kPortal) + ",42");
	CHECK_FALSE(scenarioPortalSourceRead(invalid_tail.data(), invalid_tail.size(), &raw, &count, err, sizeof(err)));
	CHECK(std::string(err).find("row 2") != std::string::npos);
	CHECK(raw == rows.get());
	CHECK(count == 1);
	CHECK_FALSE(scenarioPortalSourceRead(valid.data(), valid.size() - 1, &raw, &count, err, sizeof(err)));
	CHECK(raw == rows.get());
	CHECK(count == 1);

	std::string overflow = valid;
	overflow.replace(overflow.find("[0,6,5]"), 7, "[0,1e40,5]");
	CHECK_FALSE(scenarioPortalSourceRead(overflow.data(), overflow.size(), &raw, &count, err, sizeof(err)));
	std::string signed_flags = valid;
	signed_flags.replace(signed_flags.find("\"0x03\""), 6, "-1");
	REQUIRE(scenarioPortalSourceRead(signed_flags.data(), signed_flags.size(), &raw, &count, err, sizeof(err)));
	const PortalRows signed_rows(raw, std::free);
	CHECK(signed_rows->flags == 0xffffffffu);

	const auto empty = table("");
	REQUIRE(scenarioPortalSourceRead(empty.data(), empty.size(), &raw, &count, err, sizeof(err)));
	const PortalRows empty_rows(raw, std::free);
	CHECK(count == 0);
	CHECK(scenarioPortalSourceCheckCandidate(empty_rows.get(), 0, 0, err, sizeof(err)));
}

TEST_CASE("Detached Grid portal checks use the native arena and refuse unrepresentable output", "[scenario][portal][grid]")
{
	using Archive = std::unique_ptr<mod_archive_t, decltype(&modArchiveClose)>;
	using Bytes = std::unique_ptr<void, decltype(&std::free)>;
	const auto path = (std::filesystem::path(PD_SOURCE_DIR) / "dev-mods/grid_two_rooms/grid_two_rooms.pdarena").string();
	const Archive arena(modArchiveOpen(path.c_str()), modArchiveClose);
	REQUIRE(arena != nullptr);
	const s32 index = modArchiveFindEntry(arena.get(), "dependencies/assets/scenarios/scenario_grid_two_rooms.pdscenario");
	REQUIRE(index >= 0);
	u32 scenario_size = 0;
	const Bytes scenario(modArchiveExtractAlloc(arena.get(), index, &scenario_size), std::free);
	REQUIRE(scenario != nullptr);
	u32 text_size = 0;
	const Bytes text(modArchiveExtractMemAlloc(scenario.get(), scenario_size, "portals.json", &text_size), std::free);
	REQUIRE(text != nullptr);

	char err[160] = {};
	scenario_source_portal_row_t *raw = nullptr;
	s32 count = 0;
	REQUIRE(scenarioPortalSourceRead(static_cast<const char *>(text.get()), text_size, &raw, &count, err, sizeof(err)));
	const PortalRows rows(raw, std::free);
	REQUIRE(count == 1);
	CHECK(rows->flags == 0);
	CHECK(scenarioPortalSourceCheckCandidate(rows.get(), count, 3, err, sizeof(err)));
	CHECK_FALSE(scenarioPortalSourceCheckCandidate(rows.get(), count, 2, err, sizeof(err)));
	CHECK(std::string(err).find("room domain") != std::string::npos);

	auto collapsed = *rows;
	for (s32 i = 0; i < collapsed.vertex_count; ++i) collapsed.vertices[i][1] = 0.0f;
	CHECK_FALSE(scenarioPortalSourceCheckCandidate(&collapsed, 1, 3, err, sizeof(err)));
	CHECK(std::string(err).find("nonzero portal plane") != std::string::npos);

	std::vector<scenario_source_portal_row_t> edges(128, *rows);
	for (s32 i = 0; i < static_cast<s32>(edges.size()); ++i) edges[i].room2 = i + 2;
	CHECK(scenarioPortalSourceCheckCandidate(edges.data(), 127, 130, err, sizeof(err)));
	CHECK_FALSE(scenarioPortalSourceCheckCandidate(edges.data(), 128, 130, err, sizeof(err)));
	CHECK(std::string(err).find("127 portal links") != std::string::npos);
}
