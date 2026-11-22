/*
 * Static pins for c3842: public typed-archive source files are the native
 * client source, while generated products are cache only.
 */

#include "catch.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

/* c3849 Wave 4 Slice B: the ONE shared catalog-id -> filename slug
 * (header-only static inline), unit-tested behaviorally below. */
#include "assetcatalog_slug.h"

namespace {

std::string readTextFile(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	std::string text = ss.str();
	/* Checkout line endings are not a source contract. Preserve every other
	 * byte, including lone carriage returns, for the existing content pins. */
	std::size_t write = 0;
	for (std::size_t read = 0; read < text.size(); ++read) {
		if (text[read] == '\r' && read + 1 < text.size() && text[read + 1] == '\n') continue;
		text[write++] = text[read];
	}
	text.resize(write);
	return text;
}

TEST_CASE("scenario room domain preserves portal-only rooms and fails closed",
	"[modding][pdxxx][scenario][room-domain][t-tests-002][static]")
{
	const std::string catalog_header = readTextFile("port/include/assetcatalog.h");
	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	const std::string runtime = readTextFile("port/src/scenario_source_runtime.c");
	const std::string bg = readTextFile("src/game/bg.c");
	const std::string conformance = readTextFile("tools/asset_archive_conformance.py");

	REQUIRE(catalog_header.find("s32 source_room_count") != std::string::npos);
	REQUIRE(scanner.find("e->ext.scenario.source_room_count = iniGetInt") !=
		std::string::npos);
	REQUIRE(runtime.find("scenarioSourceResolveRoomCountForStage") !=
		std::string::npos);
	REQUIRE(runtime.find("scenarioRoomDomainResolve(") != std::string::npos);
	REQUIRE(runtime.find("scenarioRoomDomainContains(room_count, rows[i].room1)") !=
		std::string::npos);
	REQUIRE(bg.find("scenarioSourceResolveRoomCountForStage(&stage") !=
		std::string::npos);
	REQUIRE(bg.find("disabled invalid portal boundary") !=
		std::string::npos);
	REQUIRE(conformance.find("exceeds source_room_count") !=
		std::string::npos);
}

TEST_CASE("typed scenario archives carry the canonical room domain",
	"[modding][pdxxx][scenario][room-domain][t-tests-002][static]")
{
	const std::string loader =
		readTextFile("port/src/loader_walker_scenario.c");
	const std::string examples =
		readTextFile("tools/build_typed_pdxxx_examples.py");
	const std::string conformance =
		readTextFile("tools/asset_archive_conformance.py");

	REQUIRE(loader.find(
		"loaderWalkerEnvelopeInt(manifest, manifest_len, \"room_count\"") !=
		std::string::npos);
	REQUIRE(loader.find("room_count < 2 || room_count > 32768") !=
		std::string::npos);
	REQUIRE(loader.find("return -1;") != std::string::npos);
	REQUIRE(loader.find(
		"e->ext.scenario.source_room_count = (s32)room_count;") !=
		std::string::npos);
	REQUIRE(loader.find(
		".always_invoke = 1") != std::string::npos);
	REQUIRE(loader.find(".subdir = \"scenarios\"") != std::string::npos);
	REQUIRE(loader.find(".extension = \".pdscenario\"") != std::string::npos);
	REQUIRE(conformance.find(
		"scenario.ini must declare source_room_count") !=
		std::string::npos);
	REQUIRE(conformance.find(
		"_meta/manifest.json room_count must be between") !=
		std::string::npos);
	REQUIRE(conformance.find(
		"scenario.ini source_room_count must match") !=
		std::string::npos);
	REQUIRE(examples.find("source_room_count = 2") != std::string::npos);
	REQUIRE(examples.find("\\\"room_count\\\": 2") !=
		std::string::npos);
}

std::string functionBlock(const std::string &text, const std::string &name)
{
	const size_t start = text.find(name);
	if (start == std::string::npos) {
		return "";
	}
	const size_t brace = text.find('{', start);
	if (brace == std::string::npos) {
		return "";
	}
	int depth = 0;
	for (size_t i = brace; i < text.size(); i++) {
		if (text[i] == '{') {
			depth++;
		} else if (text[i] == '}') {
			depth--;
			if (depth == 0) {
				return text.substr(start, i - start + 1);
			}
		}
	}
	return "";
}

void requireTokenOrder(const std::string &text, const char *first,
	const char *second)
{
	const size_t first_pos = text.find(first);
	const size_t second_pos = text.find(second);
	INFO("Expected '" << first << "' before '" << second << "'");
	REQUIRE(first_pos != std::string::npos);
	REQUIRE(second_pos != std::string::npos);
	REQUIRE(first_pos < second_pos);
}

size_t countOccurrences(const std::string &text, const char *needle)
{
	size_t count = 0;
	size_t pos = 0;

	while ((pos = text.find(needle, pos)) != std::string::npos) {
		count++;
		pos += strlen(needle);
	}

	return count;
}

}

TEST_CASE("Codex hook preserves the c3842 native-source preflight",
          "[modding][pdxxx][c3842][hooks][static]") {
	const std::string agents = readTextFile("AGENTS.md");
	REQUIRE(agents.find("Asset Pipeline c3842 Codex Hook") !=
	        std::string::npos);
	REQUIRE(agents.find("Public asset source is the game-facing source") !=
	        std::string::npos);
	REQUIRE(agents.find("game client consumes natively through catalog/provider loading") !=
	        std::string::npos);
	REQUIRE(agents.find("runtime ROM/RomProvider fallback after extraction as an asset-chain failure") !=
	        std::string::npos);
	REQUIRE(agents.find("tools/asset_native_source_guard.py") !=
	        std::string::npos);

	const std::string skill =
		readTextFile(".agents/skills/pd2-large-change-sweep/SKILL.md");
	REQUIRE(skill.find("Asset Pipeline c3842 Gate") != std::string::npos);
	REQUIRE(skill.find("public editable source") != std::string::npos);
	REQUIRE(skill.find("runtime-cache-only") != std::string::npos);
	REQUIRE(skill.find("asset-chain failure") != std::string::npos);
	REQUIRE(skill.find("[modding][pdxxx][c3842]") != std::string::npos);
}

TEST_CASE("pre-commit hook runs the asset native-source guard",
          "[modding][pdxxx][c3842][hooks][static]") {
	const std::string hook = readTextFile(".githooks/pre-commit");
	const std::string hook_py = readTextFile(".githooks/pre-commit.py");
	const std::string installer = readTextFile("tools/install-githooks.ps1");
	const std::string dev_window =
		readTextFile("devtools/dev-window-v2/dev-window-v2.ps1");

	REQUIRE(hook.find("pre-commit.py") != std::string::npos);
	REQUIRE(hook_py.find("asset_native_source_guard.py") !=
	        std::string::npos);
	REQUIRE(hook_py.find("--staged") != std::string::npos);
	REQUIRE(hook_py.find("repo_relative_arg(root, guard)") !=
	        std::string::npos);
	REQUIRE(hook_py.find("[sys.executable, str(guard), \"--staged\"]") ==
	        std::string::npos);
	REQUIRE(installer.find("preCommitSh") != std::string::npos);
	REQUIRE(installer.find("pre-commit.py") != std::string::npos);
	REQUIRE(installer.find("asset_native_source_guard.py") !=
	        std::string::npos);
	REQUIRE(dev_window.find("commit --no-verify") == std::string::npos);
	REQUIRE(dev_window.find("outage-safe sync policy") == std::string::npos);
}

TEST_CASE("asset native-source guard is tracked by tests and source docs",
          "[modding][pdxxx][c3842][static]") {
	const std::string cmake = readTextFile("CMakeLists.txt");
	REQUIRE(cmake.find("tests/test_asset_native_source_contract.cpp") !=
	        std::string::npos);

	const std::string guard = readTextFile("tools/asset_native_source_guard.py");
	REQUIRE(guard.find("FORBIDDEN_ARCHIVE_ENTRY_NAMES") !=
	        std::string::npos);
	REQUIRE(guard.find("PDSCENARIO_FORBIDDEN_PUBLIC_ENTRY_NAMES") !=
	        std::string::npos);
	REQUIRE(guard.find("\"objects.tsv\"") != std::string::npos);
	REQUIRE(guard.find("\"objects.json\"") != std::string::npos);
	const size_t guard_suffixes_start =
		guard.find("PUBLIC_TEXT_ENTRY_SUFFIXES = (");
	REQUIRE(guard_suffixes_start != std::string::npos);
	const size_t guard_suffixes_end = guard.find(")", guard_suffixes_start);
	REQUIRE(guard_suffixes_end != std::string::npos);
	const std::string guard_suffixes = guard.substr(
		guard_suffixes_start, guard_suffixes_end - guard_suffixes_start);
	REQUIRE(guard_suffixes.find("\".tsv\"") == std::string::npos);
	REQUIRE(guard.find("FORBIDDEN_NUMERIC_ASSET_REF_KEYS") !=
	        std::string::npos);
	REQUIRE(guard.find("numeric/legacy asset reference") !=
	        std::string::npos);
	REQUIRE(guard.find("Public asset source is the game-facing source") !=
	        std::string::npos);
	REQUIRE(guard.find("Runtime ROM fallback is an asset-chain failure") !=
	        std::string::npos);
	REQUIRE(guard.find("c3844") != std::string::npos);
	REQUIRE(guard.find("source-hashed rebuildable cache") !=
	        std::string::npos);
	REQUIRE(guard.find("examples/modding/typed-pdxxx-basic") !=
	        std::string::npos);
	REQUIRE(guard.find("asset_archive_conformance.py") !=
	        std::string::npos);
	REQUIRE(guard.find("\"Tools/Workbench/data/roadmap.json\"") !=
	        std::string::npos);
	REQUIRE(guard.find("\".pdbotprofile\": \"botprofile.ini\"") !=
	        std::string::npos);
	REQUIRE(guard.find("\".pdtheme\": \"theme.ini\"") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_RUNTIME_COUNT_RULES") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_runtime_count_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_normal_play_fallback_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario normal-play source failures must not fall back to legacy ROM payloads") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario runtime row counts must stay behind source-checked helper boundaries") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_PADFILE_GLOBAL_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_padfile_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario padfile globals must stay inside source-gated runtime count helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_STAGE_SETUP_GLOBAL_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_stage_setup_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario g_StageSetup access must stay inside source-gated runtime table helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_SETUP_TAG_GLOBAL_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_setup_tag_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario setup tag globals must stay inside source-gated runtime setup tag helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_setup_behavior_link_source_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario setup behavior links must validate source record targets before runtime registration") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_SOURCE_WIDE_PAD_CACHE_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_source_wide_pad_cache_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario source wide pad offset cache must stay inside source padfile helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_player_state_access_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario player state access must stay behind runtime player proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_current_player_number_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario current-player-number reads must stay behind runtime player-slot proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("s_aiGraphResolveRuntimeCharacterRefOrSelector") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_ACTIVE_CHARACTER_POINTER_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_active_character_pointer_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario active-character pointer reads must stay behind source-aware proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_VEHICLE_MOTION_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scenarioSourceAiGraphExecuteHovercopterFireRocket") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_vehicle_motion_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario vehicle motion access must stay behind runtime vehicle proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_MISC_BRANCH_VEHICLE_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_misc_branch_vehicle_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario misc-branch vehicle access must stay behind runtime vehicle proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_PROP_TARGET_VEHICLE_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scenarioSourceAiGraphExecuteSetTarget") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_prop_target_vehicle_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario prop-target vehicle access must stay behind runtime vehicle proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_TIMER_VEHICLE_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_timer_vehicle_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario timer vehicle access must stay behind runtime vehicle proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_HOVERCAR_BRANCH_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_hovercar_branch_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario hovercar branch access must stay behind runtime vehicle proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_PROP_INDEX_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_prop_index_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario prop-index derivation must stay behind runtime character proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_PROP_PRESET_INDEX_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_prop_preset_index_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario prop-preset index access must stay behind object-source and character proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_ROOM_PROP_SCAN_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_room_prop_scan_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario room-prop scans must stay behind spatial object-source and character proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_SCENE_ROOM_TABLE_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_scene_room_table_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario room table mutations must stay behind source-built scene room proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_SCENE_ROOM_GLOBAL_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_scene_room_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario room table globals must stay behind source-built scene room proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("g_Rooms\\b(?!\\s*\\[)|\\bg_Vars\\s*\\.\\s*roomcount") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_PORTAL_TABLE_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_portal_table_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario portal table mutations must stay behind source-built portals.json proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_PORTAL_GLOBAL_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_portal_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario portal globals must stay behind source-built portals.json proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_MISSION_MUSIC_MODE_GLOBAL_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_mission_music_mode_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario mission/music mode globals must stay behind graph source proof") !=
	        std::string::npos);
	REQUIRE(guard.find("g_Vars\\s*\\.\\s*(?:normmplayerisrunning|numaibuddies)") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_PLAYER_INVINCIBILITY_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_player_invincibility_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario player invincibility global access must stay behind player-slot proof") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_ENVIRONMENT_GLOBAL_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_environment_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario environment globals must stay behind graph source proof") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_CUTSCENE_FRAME_OVERRUN_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_cutscene_frame_overrun_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario cutscene frame-overrun timing must stay behind animation source proof") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_TELEPORT_SOUND_PRIORITY_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_TELEPORT_SOUND_PRIORITY_CALLER_PROOF_TOKENS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_teleport_sound_priority_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario teleport sound priority must stay behind catalog audio proof") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_LIFT_NUMBER_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_LIFT_NUMBER_CALLER_PROOF_TOKENS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_lift_number_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario lift-number table access must stay behind public pad-source proof") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_AUTOCUT_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_autocut_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario autocut globals must stay behind graph source proof") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_FRAME_STATE_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_frame_state_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario frame-state globals must stay behind graph source proof") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_KILL_COUNT_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_kill_count_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario kill-count global reads must stay behind mission/global source proof") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_STAGE_NUM_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_stage_number_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario stage-number globals must stay behind source-proven special-case handlers") !=
	        std::string::npos);
	REQUIRE(guard.find("g_Vars\\s*\\.\\s*stagenum") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_PLAYER_AUTOWALK_STATE_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_player_autowalk_state_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario player autowalk state reads must stay behind player-slot proof") !=
	        std::string::npos);
	REQUIRE(guard.find("g_Vars\\s*\\.\\s*tickmode") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_ANTI_PLAYER_GLOBAL_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_anti_player_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario Anti-player global reads must stay behind chr_set_team source proof") !=
	        std::string::npos);
	REQUIRE(guard.find("g_Vars\\s*\\.\\s*antiplayernum") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_PLAYER_CONTROL_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_player_control_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario player-control table writes must stay behind runtime player-slot proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_AUDIO_ALIAS_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_AUDIO_CATALOG_RESOLVER_PROOF_TOKENS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_audio_alias_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario audio alias mappings and counts must stay behind catalog audio resolution proof") !=
	        std::string::npos);
	REQUIRE(guard.find("g_NumAudioRussMappings") != std::string::npos);
	REQUIRE(guard.find("SCENARIO_SPECIAL_DEATH_ANIM_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_SPECIAL_DEATH_ANIM_ASSIGNMENT_PROOF_TOKENS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_special_death_animation_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario special-death animation table reads must stay behind animation catalog proof") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_QUIP_ASSET_TABLE_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_quip_asset_table_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario quip asset tables and bank pointers must stay behind character and catalog proof") !=
	        std::string::npos);
	REQUIRE(guard.find("g_(?:Guard|Special|Skedar|Maian)QuipBank\\b") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_CHR_FIND_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_chr_find_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario chrFindById access must stay inside source-aware character-reference helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_LITERAL_CHR_FIND_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_literal_chr_find_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario chrFindByLiteralId access must stay behind runtime character proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_PROP_TARGET_INDEX_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_prop_target_index_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario prop-target index derivation must stay behind runtime character proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_LIST_CONTROL_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_list_control_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario list-control access must stay behind runtime actor proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_QUIP_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_quip_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario quip access must stay behind runtime character proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_CHARACTER_STATE_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_character_state_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario character-state access must stay behind runtime character proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_SETUP_EQUIPMENT_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scenarioSourceAiGraphExecuteTryEquipWeapon") !=
	        std::string::npos);
	REQUIRE(guard.find("scenarioSourceAiGraphExecuteTryEquipHat") !=
	        std::string::npos);
	REQUIRE(guard.find("scenarioSourceAiGraphExecuteObjectDoAnimation") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_setup_equipment_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario setup/equipment access must stay behind runtime character proof helpers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_CUTSCENE_VISIBILITY_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find(
		        "SCENARIO_CUTSCENE_VISIBILITY_SLOT_SCAN_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scenarioSourceAiGraphExecuteShowCutsceneChrs") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_cutscene_visibility_slot_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario cutscene visibility writes must use source-proven character slot pointers") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario cutscene visibility slot scans must preflight source-proven character slots") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_MISSION_GLOBAL_PLAYER_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scenarioSourceAiGraphExecuteKillBond") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_mission_global_player_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario mission-global player writes must use source-proven local player pointers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_PLAYER_GLOBAL_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find(
		        "SCENARIO_PLAYER_GLOBAL_POINTER_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find(
		        "SCENARIO_PLAYER_IDENTITY_GLOBAL_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("scenarioSourceAiGraphExecuteIfChrActivatedObject") !=
	        std::string::npos);
	REQUIRE(guard.find("scenarioSourceAiGraphExecuteToggleP1P2") !=
	        std::string::npos);
	REQUIRE(guard.find("scenarioSourceAiGraphExecuteClearInventory") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_player_identity_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("g_Vars\\s*\\.\\s*(?:bondplayernum|coopplayernum)") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario Bond/Co-op player-number globals must stay behind source/player proof") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_player_global_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("g_Vars\\.(?:bond|coop)\\b(?!\\s*->)") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario player-global access must use source-proven local player pointers") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_AI_INTERPRETER_GLOBALS") !=
	        std::string::npos);
	REQUIRE(guard.find("\"g_Vars.aioffset\"") != std::string::npos);
	REQUIRE(guard.find("\"g_Vars.ailist\"") != std::string::npos);
	REQUIRE(guard.find("SCENARIO_RUNTIME_GLOBAL_MAX_COUNTS") !=
	        std::string::npos);
	REQUIRE(guard.find("\"g_Vars.currentplayernum\": 27") !=
	        std::string::npos);
	REQUIRE(guard.find("\"g_Vars.chrdata\": 14") != std::string::npos);
	REQUIRE(guard.find("scan_scenario_runtime_global_inventory") !=
	        std::string::npos);
	REQUIRE(guard.find("errors.extend(scan_scenario_runtime_global_inventory(root))") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario runtime globals must be inventoried by source-boundary guards or interpreter exceptions") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_RUNTIME_EXTERNAL_GLOBAL_MAX_COUNTS") !=
	        std::string::npos);
	REQUIRE(guard.find("\"g_Rooms\": 10") != std::string::npos);
	REQUIRE(guard.find("\"g_NumAudioRussMappings\": 2") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_runtime_external_global_inventory") !=
	        std::string::npos);
	REQUIRE(guard.find("errors.extend(scan_scenario_runtime_external_global_inventory(root))") !=
	        std::string::npos);
	REQUIRE(guard.find("scenario runtime external globals must be inventoried by source-boundary guards") !=
	        std::string::npos);
	REQUIRE(guard.find("\"ailistFindById\"") != std::string::npos);
	REQUIRE(guard.find("scenario runtime setup/path/AI-list lookups must stay behind source-checked helper boundaries") !=
	        std::string::npos);

	const std::string conformance =
		readTextFile("tools/asset_archive_conformance.py");
	const std::string scene_texture_checker =
		readTextFile("tools/verify_scene_glb_texture_contract.py");
	const std::string pdanim_checker =
		readTextFile("tools/verify_pdanim_sources.py");
	const std::string audio_checker =
		readTextFile("tools/verify_audio_sources.py");
	const std::string pdmesh_checker =
		readTextFile("tools/verify_pdmesh_sources.py");
	const std::string pdmesh_render_stream =
		readTextFile("tools/pdmesh_render_stream.py");
	const std::string workflow_checker =
		readTextFile("tools/verify_pdxxx_modder_workflow.py");
	REQUIRE(conformance.find("Strict conformance checks for PD2 typed asset archives") !=
	        std::string::npos);
	REQUIRE(conformance.find("require_all_families") != std::string::npos);
	REQUIRE(conformance.find("OPTIONAL_PUBLIC_SLOT_CONTRACT") !=
	        std::string::npos);
	REQUIRE(conformance.find("META_SLOT_CONTRACT") != std::string::npos);
	REQUIRE(conformance.find("validate_schema_definitions") !=
	        std::string::npos);
	REQUIRE(conformance.find("source entry hash sidecar") !=
	        std::string::npos);
	REQUIRE(conformance.find("behavior/primary.graph.json") !=
	        std::string::npos);
	REQUIRE(conformance.find("dependencies/assets/scenarios/*.pdscenario") !=
	        std::string::npos);
	REQUIRE(conformance.find("navigation/waypoints.json") !=
	        std::string::npos);
	REQUIRE(conformance.find("decoded waypoint graph source") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"*.tsv\"") != std::string::npos);
	const size_t conformance_suffixes_start =
		conformance.find("PUBLIC_TEXT_ENTRY_SUFFIXES = (");
	REQUIRE(conformance_suffixes_start != std::string::npos);
	const size_t conformance_suffixes_end =
		conformance.find(")", conformance_suffixes_start);
	REQUIRE(conformance_suffixes_end != std::string::npos);
	const std::string conformance_suffixes = conformance.substr(
		conformance_suffixes_start,
		conformance_suffixes_end - conformance_suffixes_start);
	REQUIRE(conformance_suffixes.find("\".tsv\"") == std::string::npos);
	REQUIRE(conformance.find("\"layout.tsv\"") == std::string::npos);
	REQUIRE(conformance.find("\"layout.json\"") != std::string::npos);
	REQUIRE(conformance.find("is_random_selector") != std::string::npos);
	REQUIRE(conformance.find("rooms.obj") != std::string::npos);
	REQUIRE(conformance.find("CATALOG_ID_RE") != std::string::npos);
	REQUIRE(conformance.find("unknown catalog ID reference") !=
	        std::string::npos);
	REQUIRE(conformance.find("collect_archive_catalog_ids") !=
	        std::string::npos);
	REQUIRE(conformance.find("validate_manifest_dependency_refs") !=
	        std::string::npos);
	REQUIRE(conformance.find("scan_delimited_asset_refs") !=
	        std::string::npos);
	REQUIRE(conformance.find("known_catalog_ids") != std::string::npos);
	REQUIRE(conformance.find("PDANIM_CHR_GENERATOR") != std::string::npos);
	REQUIRE(conformance.find("validate_pdanim_source_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("uses stale pdanim generator") !=
	        std::string::npos);
	REQUIRE(conformance.find("sampler {sampler_index} {key} accessor has no values") !=
	        std::string::npos);
	REQUIRE(conformance.find("must be LINEAR or STEP") !=
	        std::string::npos);
	REQUIRE(conformance.find("must be float SCALAR time values") !=
	        std::string::npos);
	REQUIRE(conformance.find("must be float {expected_type} values for {path}") !=
	        std::string::npos);
	REQUIRE(conformance.find("commands.json must contain non-empty commands") !=
	        std::string::npos);
	REQUIRE(conformance.find("command_count {declared_count!r} does not match") !=
	        std::string::npos);
	REQUIRE(conformance.find("--max-errors") != std::string::npos);
	REQUIRE(conformance.find("total.errors[:max_errors]") !=
	        std::string::npos);
	REQUIRE(conformance.find("more error(s)") != std::string::npos);
	REQUIRE(conformance.find("validate_audio_source_contract") !=
	        std::string::npos);
	// Intrinsic format/rate/frame counts come from the selected public bytes.
	// Real archive positives and corruptions live in test_audio_source_conformance.py.
	REQUIRE(conformance.find("parse_selected_audio_source(label, source, zf.read(source), errors)") !=
	        std::string::npos);
	REQUIRE(conformance.find("validate_audio_playback_controls(label, descriptor, descriptor_values, metadata, errors)") !=
	        std::string::npos);
	REQUIRE(conformance.find("validate_audio_native_provenance(label, descriptor, descriptor_values, manifest_values, errors)") !=
	        std::string::npos);
	for (const char *parser : {"parse_wav_source_metadata", "parse_mp3_source_metadata",
			"parse_vorbis_source_metadata"})
		REQUIRE(conformance.find(parser) != std::string::npos);
	REQUIRE(conformance.find("SONG_SEQUENCE_NATIVE_FIELDS") !=
	        std::string::npos);
	REQUIRE(conformance.find("validate_song_source_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("sequence.json must contain non-empty events") !=
	        std::string::npos);
	REQUIRE(conformance.find("FONT_BITMAP_NATIVE_FIELDS") !=
	        std::string::npos);
	REQUIRE(conformance.find("validate_font_source_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("font.metrics.json must contain non-empty glyphs") !=
	        std::string::npos);
	REQUIRE(scene_texture_checker.find("SceneMaterialStats") !=
	        std::string::npos);
	REQUIRE(scene_texture_checker.find("--require-secondary") !=
	        std::string::npos);
	REQUIRE(scene_texture_checker.find("CURRENT_SCENARIO_GLB_STAMP") !=
	        std::string::npos);
	REQUIRE(scene_texture_checker.find("pd2_materials={stats.materials_with_pd2}") !=
	        std::string::npos);
	REQUIRE(scene_texture_checker.find(
		        "complete_pd2_materials={stats.complete_pd2_materials}") !=
	        std::string::npos);
	REQUIRE(scene_texture_checker.find("secondary_textures={stats.secondary_textures}") !=
	        std::string::npos);
	REQUIRE(pdanim_checker.find("pdanim source contract ok") !=
	        std::string::npos);
	REQUIRE(pdanim_checker.find("--require-both-categories") !=
	        std::string::npos);
	REQUIRE(pdanim_checker.find("pd_zero_frame_placeholder") !=
	        std::string::npos);
	REQUIRE(pdanim_checker.find("PDANIM_CHR_GENERATOR") !=
	        std::string::npos);
	REQUIRE(pdanim_checker.find("stale_generators: Counter[str]") !=
	        std::string::npos);
	REQUIRE(pdanim_checker.find("record_stale_generator") !=
	        std::string::npos);
	REQUIRE(pdanim_checker.find("if len(examples) < 5") !=
	        std::string::npos);
	REQUIRE(pdanim_checker.find("--max-errors") != std::string::npos);
	REQUIRE(pdanim_checker.find("errors = summary_errors + errors") !=
	        std::string::npos);
	REQUIRE(pdanim_checker.find("more error(s)") != std::string::npos);
	REQUIRE(pdanim_checker.find("archive(s) use stale pdanim generator") !=
	        std::string::npos);
	REQUIRE(pdanim_checker.find("... {count - len(examples)} more") !=
	        std::string::npos);
	REQUIRE(pdanim_checker.find("must be LINEAR or STEP") !=
	        std::string::npos);
	REQUIRE(pdanim_checker.find("must be float SCALAR time values") !=
	        std::string::npos);
	REQUIRE(pdanim_checker.find("must be float {expected_type} values for {path}") !=
	        std::string::npos);
	REQUIRE(audio_checker.find("audio source contract ok") !=
	        std::string::npos);
	REQUIRE(audio_checker.find("--require-all-categories") !=
	        std::string::npos);
	REQUIRE(audio_checker.find("parse_mp3_info") != std::string::npos);
	REQUIRE(audio_checker.find("parse_ogg_info") != std::string::npos);
	REQUIRE(audio_checker.find("loop sample bounds") != std::string::npos);
	REQUIRE(audio_checker.find("source_filenum provenance") !=
	        std::string::npos);
	REQUIRE(audio_checker.find("effective_pitch_buckets") !=
	        std::string::npos);
	REQUIRE(audio_checker.find("sequence_events") != std::string::npos);
	REQUIRE(audio_checker.find("more error(s)") != std::string::npos);
	REQUIRE(pdmesh_checker.find("pdmesh source contract ok") !=
	        std::string::npos);
	REQUIRE(pdmesh_checker.find("--require-generated-hierarchy") !=
	        std::string::npos);
	REQUIRE(pdmesh_checker.find("parse_obj_info") != std::string::npos);
	REQUIRE(pdmesh_checker.find("parse_gltf_info") != std::string::npos);
	REQUIRE(pdmesh_checker.find("integer_native_boundary") !=
	        std::string::npos);
	REQUIRE(pdmesh_checker.find("quantized_triangle_collapse") !=
	        std::string::npos);
	REQUIRE(pdmesh_checker.find("model.nodes.json") !=
	        std::string::npos);
	REQUIRE(pdmesh_checker.find("model.faces.json") !=
	        std::string::npos);
	REQUIRE(pdmesh_checker.find("render_root, len(faces), render_group_modes") !=
	        std::string::npos);
	REQUIRE(pdmesh_render_stream.find("model.render.json must reference every model.faces.json face") !=
	        std::string::npos);
	REQUIRE(pdmesh_render_stream.find("SCHEMA_CURRENT = 4") !=
	        std::string::npos);
	REQUIRE(pdmesh_render_stream.find("vertex_load") != std::string::npos);
	REQUIRE(pdmesh_render_stream.find("vertex_scope") != std::string::npos);
	REQUIRE(pdmesh_render_stream.find("vertex_cache_reset") !=
	        std::string::npos);
	REQUIRE(pdmesh_render_stream.find("vertex_cache_slots") !=
	        std::string::npos);
	REQUIRE(pdmesh_render_stream.find("MAX_MATRIX_INDEX = 32766") !=
	        std::string::npos);
	REQUIRE(pdmesh_render_stream.find("unrelocatable G_VTX geometry state") !=
	        std::string::npos);
	REQUIRE(workflow_checker.find("Verify the all-family typed .pdxxx modder workflow") !=
	        std::string::npos);
	REQUIRE(workflow_checker.find("DEFAULT_SOURCE_ROOT") !=
	        std::string::npos);
	REQUIRE(workflow_checker.find("examples\" / \"modding\" / \"typed-pdxxx-basic\"") !=
	        std::string::npos);
	REQUIRE(workflow_checker.find("package_pdmod(source_root, pdmod)") !=
	        std::string::npos);
	REQUIRE(workflow_checker.find("extract_pdmod(pdmod, unpacked)") !=
	        std::string::npos);
	REQUIRE(workflow_checker.find("roundtrip changed archive bytes") !=
	        std::string::npos);
	REQUIRE(workflow_checker.find("missing typed family examples") !=
	        std::string::npos);
	REQUIRE(workflow_checker.find("forbidden public source entry") !=
	        std::string::npos);
	REQUIRE(workflow_checker.find("pdxxx modder workflow ok") !=
	        std::string::npos);
}

TEST_CASE("pdanim stale generated installs have an offline v3 to v4 repair path",
          "[modding][pdxxx][c3844][source][static]")
{
	const std::string upgrader =
		readTextFile("tools/upgrade_pdanim_v3_archives.py");

	REQUIRE(upgrader.find("OLD_GENERATOR = \"Perfect Dark 2 pdanim_chr semantic extractor v3\"") !=
	        std::string::npos);
	REQUIRE(upgrader.find("NEW_GENERATOR = \"Perfect Dark 2 pdanim_chr semantic extractor v4\"") !=
	        std::string::npos);
	REQUIRE(upgrader.find("pd_zero_frame_placeholder") != std::string::npos);
	REQUIRE(upgrader.find("frame_count == 0 or bytes_per_frame == 0") !=
	        std::string::npos);
	REQUIRE(upgrader.find("manifest[\"pd_schema_version\"] = 4") !=
	        std::string::npos);
	REQUIRE(upgrader.find("_meta/animation.gltf.sha256") !=
	        std::string::npos);
	REQUIRE(upgrader.find("_meta/inventory.json") != std::string::npos);
	REQUIRE(upgrader.find("_meta/hashes.json") != std::string::npos);
	REQUIRE(upgrader.find("skip:not-v3") != std::string::npos);
}

TEST_CASE("stale generated metadata and weapon manifests have an offline source repair path",
          "[modding][pdxxx][c3844][source][static]")
{
	const std::string upgrader =
		readTextFile("tools/upgrade_generated_manifest_archives.py");

	REQUIRE(upgrader.find("ARCHIVE_DESCRIPTORS") != std::string::npos);
	REQUIRE(upgrader.find("\".pdbotprofile\": \"botprofile.ini\"") !=
	        std::string::npos);
	REQUIRE(upgrader.find("\".pdgamemode\": \"gamemode.ini\"") !=
	        std::string::npos);
	REQUIRE(upgrader.find("\".pdmission\": \"mission.ini\"") !=
	        std::string::npos);
	REQUIRE(upgrader.find("\".pdprojectile\": \"projectile.ini\"") !=
	        std::string::npos);
	REQUIRE(upgrader.find("\".pdentity\": \"entity.ini\"") !=
	        std::string::npos);
	REQUIRE(upgrader.find("\".pdweapon\": \"weapon.ini\"") !=
	        std::string::npos);
	REQUIRE(upgrader.find("\"shared_context\": \"shared_context_file\"") !=
	        std::string::npos);
	REQUIRE(upgrader.find("\"settings\": \"settings_file\"") !=
	        std::string::npos);
	REQUIRE(upgrader.find("\"variables\": \"variables_file\"") !=
	        std::string::npos);
	REQUIRE(upgrader.find("WEAPON_REQUIRED_MEMBERS") != std::string::npos);
	REQUIRE(upgrader.find("WEAPON_RETIRED_MEMBERS") != std::string::npos);
	REQUIRE(upgrader.find("material_slots_file") != std::string::npos);
	REQUIRE(upgrader.find("grip_sockets_file") != std::string::npos);
	REQUIRE(upgrader.find("if name in retired_names") != std::string::npos);
	REQUIRE(upgrader.find("\"objectives_file\"") != std::string::npos);
	REQUIRE(upgrader.find("\"briefing_file\"") == std::string::npos);
	REQUIRE(upgrader.find("\"type_key\"") != std::string::npos);
	REQUIRE(upgrader.find("\"difficulty_key\"") != std::string::npos);
	REQUIRE(upgrader.find("\"min_players\"") != std::string::npos);
	REQUIRE(upgrader.find("\"team_based\"") != std::string::npos);
	REQUIRE(upgrader.find("remove_ini_key(descriptor_text, \"model_file\")") !=
	        std::string::npos);
	REQUIRE(upgrader.find("upgrade_zip_bytes(") != std::string::npos);
	REQUIRE(upgrader.find("\".pdprojectile\", \".pdentity\"") !=
	        std::string::npos);
	REQUIRE(upgrader.find("_meta/inventory.json") != std::string::npos);
	REQUIRE(upgrader.find("_meta/hashes.json") != std::string::npos);
	REQUIRE(upgrader.find("This is an offline currentness repair") !=
	        std::string::npos);
}

TEST_CASE("c3842 source-of-truth docs stay aligned",
          "[modding][pdxxx][c3842][static]") {
	const std::string constraints = readTextFile("context/constraints.md");
	REQUIRE(constraints.find("Public asset source is the game-facing source") !=
	        std::string::npos);
	REQUIRE(constraints.find("Every asset family must expose user-editable") !=
	        std::string::npos);
	REQUIRE(constraints.find("source-hashed rebuildable cache") !=
	        std::string::npos);
	REQUIRE(constraints.find("Runtime ROM fallback is an asset-chain failure") !=
	        std::string::npos);

	const std::string formats =
		readTextFile("context/designs/modding/asset-archive-clean-formats.md");
	REQUIRE(formats.find("The public authoring files are also the game-facing source of truth") !=
	        std::string::npos);
	REQUIRE(formats.find("Runtime ROM fallback is an asset-chain failure") !=
	        std::string::npos);
	REQUIRE(formats.find("one editable file and a separate opaque runtime file") !=
	        std::string::npos);

	const std::string tasks = readTextFile("context/tasks.md");
	REQUIRE(tasks.find("Public typed archives are the game-facing source") !=
	        std::string::npos);
	REQUIRE(tasks.find("source-hashed cache only") != std::string::npos);
	REQUIRE(tasks.find("c3844") != std::string::npos);

	const std::string workbench =
		readTextFile("tools/Workbench/data/roadmap.json");
	REQUIRE(workbench.find("\"id\": \"T-ASSETS-001\"") !=
	        std::string::npos);
	REQUIRE(workbench.find("\"id\": \"A-ASSETS-001\"") !=
	        std::string::npos);
	REQUIRE(workbench.find("\"id\": \"V-001\"") != std::string::npos);
	REQUIRE(workbench.find("Public asset source guard passes") !=
	        std::string::npos);
	REQUIRE(workbench.find("source-hashed") != std::string::npos);
}

TEST_CASE("runtime ROM fallback is tracked as an asset-chain failure",
          "[modding][pdxxx][c3844][static]") {
	const std::string constraints = readTextFile("context/constraints.md");
	const std::string tasks = readTextFile("context/tasks.md");
	const std::string modding = readTextFile("context/pillars/modding.md");
	const std::string catalog = readTextFile("context/pillars/catalog.md");
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");
	const std::string workbench =
		readTextFile("tools/Workbench/data/roadmap.json");

	REQUIRE(constraints.find("Runtime ROM fallback is an asset-chain failure") !=
	        std::string::npos);
	REQUIRE(constraints.find("system/ecosystem failure") != std::string::npos);
	REQUIRE(tasks.find("Runtime ROM/RomProvider fallback after extraction is an asset-chain failure") !=
	        std::string::npos);
	REQUIRE(modding.find("Runtime ROM fallback is an asset-chain failure") !=
	        std::string::npos);
	REQUIRE(catalog.find("runtime ROM/RomProvider fallback after extraction is an asset-chain failure") !=
	        std::string::npos);
	REQUIRE(guard.find("Runtime ROM fallback is an asset-chain failure") !=
	        std::string::npos);
	REQUIRE(workbench.find("\"id\": \"T-ASSETS-001\"") !=
	        std::string::npos);
	REQUIRE(workbench.find("Audit every asset family from extraction through production use") !=
	        std::string::npos);
	REQUIRE(workbench.find("\"id\": \"V-003\"") != std::string::npos);
}

TEST_CASE("scenario object-backed room object weapon predicates validate source before runtime tags",
          "[modding][pdxxx][c3844][static]") {
	const std::string scenario_runtime =
		readTextFile("port/src/scenario_source_runtime.c");
	const char *functions[] = {
		"scenarioSourceAiGraphExecuteIfChrHasObject",
		"scenarioSourceAiGraphExecuteIfWeaponThrownOnObject",
		"scenarioSourceAiGraphExecuteIfGunUnclaimed",
		"scenarioSourceAiGraphExecuteIfObjectHealthy",
	};

	REQUIRE(scenario_runtime.find("s_aiGraphPrepareRoomObjectWeaponBranch") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphLogRoomObjectWeaponBranch") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphLogRoomObjectWeaponBranchWeapon") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition %s weapon_id=%s label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition %s tag=%d object_rows=%d weapon_id=%s") !=
	        std::string::npos);

	for (const char *function : functions) {
		const std::string block = functionBlock(scenario_runtime, function);
		INFO(function);
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphPrepareRoomObjectWeaponBranch",
			"s_aiGraphRequireRuntimeObjectTag");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeObjectTag",
			"objFindByTagId");
	}

	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfWeaponThrown");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphResolveWeaponCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphLogRoomObjectWeaponBranchWeapon") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphPrepareRoomObjectWeaponBranch",
			"s_aiGraphResolveWeaponCatalogId");
		requireTokenOrder(block,
			"s_aiGraphResolveWeaponCatalogId",
			"weaponFindLanded");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfWeaponThrownOnObject");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphResolveWeaponCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphLogRoomObjectWeaponBranchWeaponObject") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeObjectTag",
			"s_aiGraphResolveWeaponCatalogId");
		requireTokenOrder(block,
			"s_aiGraphResolveWeaponCatalogId",
			"objFindByTagId");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrHasWeaponEquipped");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphResolveWeaponCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("player_checked=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphPrepareRoomObjectWeaponBranch",
			"s_aiGraphResolveWeaponCatalogId");
		requireTokenOrder(block,
			"s_aiGraphResolveWeaponCatalogId",
			"s_aiGraphResolveRuntimeCharacterRefOrSelector");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeCharacterRefOrSelector",
			"s_aiGraphRequireRuntimePlayerSlot");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot",
			"bgunGetWeaponNum");
	}
}

TEST_CASE("scenario door graph actions validate source object type before runtime door access",
          "[modding][pdxxx][c3844][static]") {
	const std::string scenario_runtime =
		readTextFile("port/src/scenario_source_runtime.c");
	const char *door_functions[] = {
		"scenarioSourceAiGraphExecuteOpenDoor",
		"scenarioSourceAiGraphExecuteCloseDoor",
		"scenarioSourceAiGraphExecuteIfDoorState",
		"scenarioSourceAiGraphExecuteLockDoor",
		"scenarioSourceAiGraphExecuteUnlockDoor",
		"scenarioSourceAiGraphExecuteIfDoorLocked",
		"scenarioSourceAiGraphExecuteSetDoorOpen",
	};

	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeObjectTagType") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("expected source type %u") !=
	        std::string::npos);

	for (const char *function : door_functions) {
		const std::string block = functionBlock(scenario_runtime, function);
		INFO(function);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeObjectTagType") !=
		        std::string::npos);
		REQUIRE(block.find("OBJTYPE_DOOR") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeObjectTagType",
			"objFindByTagId");
	}

	const std::string predicate =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfObjectIsDoor");
	REQUIRE(!predicate.empty());
	REQUIRE(predicate.find("s_aiGraphRequireRuntimeObjectTagType") ==
	        std::string::npos);
	REQUIRE(predicate.find("obj->type == OBJTYPE_DOOR") !=
	        std::string::npos);
}

TEST_CASE("scenario class-owned object graph actions validate source object types",
          "[modding][pdxxx][c3844][static]") {
	const std::string scenario_runtime =
		readTextFile("port/src/scenario_source_runtime.c");
	const char *lift_functions[] = {
		"scenarioSourceAiGraphExecuteIfLiftStationary",
		"scenarioSourceAiGraphExecuteLiftGoToStop",
		"scenarioSourceAiGraphExecuteIfLiftAtStop",
		"scenarioSourceAiGraphExecuteActivateLift",
	};

	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeObjectTagAnyType") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("expected one of source types %s") !=
	        std::string::npos);

	for (const char *function : lift_functions) {
		const std::string block = functionBlock(scenario_runtime, function);
		INFO(function);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeObjectTagType") !=
		        std::string::npos);
		REQUIRE(block.find("OBJTYPE_LIFT") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeObjectTagType",
			"objFindByTagId");
	}

	const std::string image =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetObjImage");
	REQUIRE(!image.empty());
	REQUIRE(image.find("s_aiGraphRequireRuntimeObjectTagAnyType") !=
	        std::string::npos);
	REQUIRE(image.find("OBJTYPE_SINGLEMONITOR") != std::string::npos);
	REQUIRE(image.find("OBJTYPE_MULTIMONITOR") != std::string::npos);
	requireTokenOrder(image,
		"s_aiGraphRequireRuntimeObjectTagAnyType",
		"objFindByTagId");

	const std::string autogun =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetAutogunTargetTeam");
	REQUIRE(!autogun.empty());
	REQUIRE(autogun.find("s_aiGraphRequireRuntimeObjectTagType") !=
	        std::string::npos);
	REQUIRE(autogun.find("OBJTYPE_AUTOGUN") != std::string::npos);
	requireTokenOrder(autogun,
		"s_aiGraphRequireRuntimeObjectTagType",
		"objFindByTagId");
}

TEST_CASE("scenario setup tag actions validate source setup tag table",
          "[modding][pdxxx][c3844][static]") {
	const std::string scenario_runtime =
		readTextFile("port/src/scenario_source_runtime.c");
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeSetupTag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing source-derived runtime setup tag table") !=
	        std::string::npos);
	REQUIRE(countOccurrences(scenario_runtime,
		        "if (!s_ActiveScenarioGraphs.setup_fields_path[0]) {\n"
		        "\t\ts_aiGraphRuntimeFailure(action, \"missing setup.fields.json source\");\n"
		        "\t\treturn 0;\n"
		        "\t}\n\n"
		        "\ttag_count = s_countRuntimeSetupTags();") >= 1);
	{
		const std::string count_tags =
			functionBlock(scenario_runtime, "s_countRuntimeSetupTags");
		REQUIRE(!count_tags.empty());
		REQUIRE(count_tags.find("struct tag *tag = g_TagsLinkedList;") !=
		        std::string::npos);
		REQUIRE(count_tags.find("tag = tag->next;") !=
		        std::string::npos);
		REQUIRE(count_tags.find("return count;") != std::string::npos);
	}
	const std::string investigation =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteShuffleInvestigationTerminals");

	REQUIRE(!investigation.empty());
	REQUIRE(investigation.find("s_aiGraphRequireRuntimeSetupTag") !=
	        std::string::npos);
	REQUIRE(investigation.find("OBJTYPE_TAG") == std::string::npos);
	REQUIRE(investigation.find("setup_tags=%d") != std::string::npos);
	REQUIRE(investigation.find("s_aiGraphCopyTagPlacementChecked") !=
	        std::string::npos);
	requireTokenOrder(investigation,
		"s_aiGraphRequireRuntimeSetupTag",
		"s_aiGraphCopyTagPlacementChecked");
	REQUIRE(scenario_runtime.find("static s32 s_aiGraphCopyTagPlacement(") ==
	        std::string::npos);

	const std::string checked_helper =
		functionBlock(scenario_runtime, "s_aiGraphCopyTagPlacementChecked");
	REQUIRE(!checked_helper.empty());
	REQUIRE(checked_helper.find("s_aiGraphRequireRuntimeSetupTag") !=
	        std::string::npos);
	REQUIRE(checked_helper.find("OBJTYPE_TAG") == std::string::npos);
	requireTokenOrder(checked_helper,
		"s_aiGraphRequireRuntimeSetupTag",
		"tagFindById");

	for (const char *function : {
		     "scenarioSourceAiGraphExecuteShuffleRuinsPillars",
		     "scenarioSourceAiGraphExecuteShufflePelagicSwitches",
	     }) {
		const std::string block = functionBlock(scenario_runtime, function);
		INFO(function);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphCopyTagPlacementChecked") !=
		        std::string::npos);
		REQUIRE(block.find("setup_tags=%d") != std::string::npos);
	}

	const std::string warp =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteWarpJoToTag");
	REQUIRE(!warp.empty());
	REQUIRE(warp.find("s_aiGraphRequireRuntimeSetupTag") !=
	        std::string::npos);
	REQUIRE(warp.find("OBJTYPE_TAG") == std::string::npos);
	REQUIRE(warp.find("setup_tags=%d") != std::string::npos);
	requireTokenOrder(warp,
		"s_aiGraphRequireRuntimeSetupTag",
		"tagFindById");
}

TEST_CASE("scenario graph return lists preserve background interpreter state",
		"[campaign][scenario][graph][cutscene][static]") {
	const std::string runtime =
		readTextFile("port/src/scenario_source_runtime.c");
	const std::string native = readTextFile("src/game/chraicommands.c");
	const std::string defection = readTextFile("src/setups/setupame.c");
	const std::string interpreter = readTextFile("src/game/chrai.c");
	const std::string chicago = readTextFile("src/setups/setuppete.c");
	const std::string graph_set_return = functionBlock(runtime,
		"scenarioSourceAiGraphExecuteSetReturnList");
	const std::string native_set_return = functionBlock(native,
		"aiSetReturnList");
	const std::string execute = functionBlock(interpreter, "chraiExecute");
	const std::string chicago_intro_from_gameplay = functionBlock(chicago,
		"func040a_intro_from_gameplay");
	const std::string chicago_intro = functionBlock(chicago, "func040a_intro");

	REQUIRE(!graph_set_return.empty());
	REQUIRE(!native_set_return.empty());
	REQUIRE(!execute.empty());
	REQUIRE(!chicago_intro_from_gameplay.empty());
	REQUIRE(!chicago_intro.empty());
	REQUIRE(defection.find(
		"set_returnlist(CHR_SELF, AILIST_INTRO_041E)") !=
		std::string::npos);
	REQUIRE(chicago_intro_from_gameplay.find(
		"set_ailist(CHR_SELF, 0x040a)") != std::string::npos);
	requireTokenOrder(chicago_intro,
		"set_returnlist(CHR_SELF, 0x0406)",
		"set_ailist(CHR_SELF, 0x0401)");
	REQUIRE(graph_set_return.find("active_chr->prop") ==
		std::string::npos);
	REQUIRE(graph_set_return.find("chr && chr->prop") ==
		std::string::npos);
	REQUIRE(graph_set_return.find(
		"active_chr->aireturnlist = list_id;") !=
		std::string::npos);
	REQUIRE(graph_set_return.find("chr->aireturnlist = list_id;") !=
		std::string::npos);
	REQUIRE(graph_set_return.find("list=%u applied=%d") !=
		std::string::npos);
	requireTokenOrder(graph_set_return,
		"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
		"active_chr->aireturnlist = list_id;");
	requireTokenOrder(native_set_return,
		"if (cmd[2] == CHR_SELF)",
		"g_Vars.chrdata->aireturnlist = ailistid;");
	requireTokenOrder(execute,
		"u8 *prevailist = g_Vars.ailist;",
		"g_CommandPointers[type]()");
	REQUIRE(execute.find(
		"if (g_Vars.ailist == prevailist && g_Vars.aioffset == prevoffset)") !=
		std::string::npos);
	REQUIRE(execute.find("if (g_Vars.aioffset == prevoffset)") ==
		std::string::npos);
}

TEST_CASE("base weapon archives use the catalog IDs requested at runtime",
          "[modding][pdxxx][c3844][weapon][static]") {
	const std::string authored =
		readTextFile("port/src/weapondata_authored.c");
	const std::string base =
		readTextFile("port/src/assetcatalog_base_extended.c");
	const std::string extractor =
		readTextFile("port/src/romextract_pdweapon.c");
	const std::string scanner =
		readTextFile("port/src/assetcatalog_scanner.c");
	const std::string walker =
		readTextFile("port/src/loader_walker_weapon.c");
	const std::string slots =
		readTextFile("port/src/assetcatalog_weapon_slots.c");
	const std::string enum_reverse =
		readTextFile("port/src/loader_enum_reverse.c");
	const std::string gun_lang =
		readTextFile("src/assets/ntsc-final/lang/gun.json");
	const std::string match_smoke =
		readTextFile("tools/smoke-verify/tests/weapon_match_source_gate_smoke.json");

	REQUIRE(base.find("\"nothing\"") != std::string::npos);
	REQUIRE(base.find("\"falcon2_silencer\"") != std::string::npos);
	REQUIRE(base.find("\"falcon2_scope\"") != std::string::npos);
	REQUIRE(base.find("\"magsec\"") != std::string::npos);
	REQUIRE(base.find("\"dy357\"") != std::string::npos);
	REQUIRE(base.find("\"hammer_slot83\"") != std::string::npos);
	REQUIRE(base.find("\"hammer_slot84\"") != std::string::npos);
	REQUIRE(base.find("\"none\"") == std::string::npos);
	REQUIRE(base.find("\"magsec4\"") == std::string::npos);
	REQUIRE(base.find("\"dy357magnum\"") == std::string::npos);
	REQUIRE(base.find("\"shield\"") == std::string::npos);
	REQUIRE(base.find("\"disabled\"") == std::string::npos);
	REQUIRE(authored.find("\"base:falcon2_silencer\"") !=
	        std::string::npos);
	REQUIRE(authored.find("\"base:falcon2_scope\"") != std::string::npos);
	REQUIRE(authored.find("\"base:nothing\"") != std::string::npos);
	REQUIRE(authored.find("\"base:magsec\"") != std::string::npos);
	REQUIRE(authored.find("\"base:dy357\"") != std::string::npos);
	REQUIRE(authored.find("\"base:hammer_slot83\"") != std::string::npos);
	REQUIRE(authored.find("\"base:hammer_slot84\"") != std::string::npos);
	REQUIRE(authored.find("\"base:falcon2silencer\"") ==
	        std::string::npos);
	REQUIRE(authored.find("\"base:falcon2scope\"") == std::string::npos);

	REQUIRE(extractor.find("PDWEAPON_FAST_CACHE_KIND \"pdweapon_embedded_v14_clean_public_b943fields_projref_fuseprogram_contactrules_devicerules_sentryrules_armrules_heldprograms_heldtriggers_heldshots_heldreloads_heldcharges\"") !=
		std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_DEPENDENCY_CLOSURE_MARKER \"embedded.v21\"") !=
		std::string::npos);
	REQUIRE(extractor.find("if (val <= 0)") != std::string::npos);
	REQUIRE(extractor.find("jw_field_sfx_or_int(w, \"shootsound\"") !=
		std::string::npos);
	REQUIRE(extractor.find("base_falcon2silencer.pdweapon") !=
	        std::string::npos);
	REQUIRE(extractor.find("category = base") != std::string::npos);
	REQUIRE(extractor.find("bundled = 1") != std::string::npos);
	REQUIRE(extractor.find("s_normalizeSfxCatalogIndex") !=
	        std::string::npos);
	REQUIRE(extractor.find("g_AudioRussMappings[ref.confignum].soundnum") !=
	        std::string::npos);
	REQUIRE(extractor.find("\\\"sound_ref\\\"") == std::string::npos);
	REQUIRE(extractor.find("\\\"sample_catalog_id\\\"") !=
	        std::string::npos);

	REQUIRE(scanner.find("preserved_weapon_id") != std::string::npos);
	REQUIRE(scanner.find("assetCatalogResolve(idbuf)") !=
	        std::string::npos);
	REQUIRE(scanner.find("iniGetInt(ini, \"weapon_id\"") ==
	        std::string::npos);
	REQUIRE(scanner.find("preserved_runtime_index") != std::string::npos);
	REQUIRE(scanner.find("e->ext.weapon.primary_graph") !=
	        std::string::npos);
	REQUIRE(scanner.find("e->ext.weapon.secondary_graph") !=
	        std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.weapon.primary_graph)") !=
	        std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.weapon.model_file)") ==
	        std::string::npos);

	REQUIRE(walker.find("(void)file_path") == std::string::npos);
	REQUIRE(walker.find("s_selectWeaponSlots") != std::string::npos);
	REQUIRE(walker.find("assetCatalogResolveWeaponPrivateSlots") !=
	        std::string::npos);
	REQUIRE(slots.find("s_mpWeaponIdForRuntimeWeapon") !=
	        std::string::npos);
	REQUIRE(slots.find("catalogGetMpWeaponNum(mp_weapon_id)") !=
	        std::string::npos);
	REQUIRE(walker.find("e->runtime_index = runtime_weapon_id") !=
	        std::string::npos);
	REQUIRE(walker.find("e->mp_index = (s16)mp_weapon_id") !=
	        std::string::npos);
	REQUIRE(walker.find("e->ext.weapon.weapon_id = mp_weapon_id") !=
	        std::string::npos);
	REQUIRE(walker.find("catalogSetPrimaryFile(e, file_path)") !=
	        std::string::npos);
	REQUIRE(walker.find("s_bindWeaponArchiveSourceMembers") !=
	        std::string::npos);
	REQUIRE(walker.find("e->ext.weapon.model_file") !=
	        std::string::npos);
	REQUIRE(walker.find("e->ext.weapon.primary_graph") !=
	        std::string::npos);
	REQUIRE(walker.find("e->ext.weapon.secondary_graph") !=
	        std::string::npos);
	REQUIRE(walker.find("e->ref_count = ASSET_REF_BUNDLED") !=
	        std::string::npos);
	{
		const std::string catalog_load =
			readTextFile("port/src/assetcatalog_load.c");
		REQUIRE(catalog_load.find("entry->ext.weapon.primary_graph[0]") !=
		        std::string::npos);
		REQUIRE(catalog_load.find("entry->ext.weapon.secondary_graph[0]") !=
		        std::string::npos);
		REQUIRE(catalog_load.find("entry->ext.weapon.shared_context[0]") !=
		        std::string::npos);
		REQUIRE(catalog_load.find("entry->ext.weapon.settings_file[0]") !=
		        std::string::npos);
		REQUIRE(catalog_load.find("entry->ext.weapon.variables_file[0]") !=
		        std::string::npos);
		REQUIRE(catalog_load.find("entry->ext.weapon.behavior_graph[0]") ==
		        std::string::npos);
		REQUIRE(catalog_load.find("weaponGraphRuntimeRegisterWeaponGraphJson") ==
		        std::string::npos);
		REQUIRE(catalog_load.find("weaponGraphRuntimeRegisterWeaponSourceJson") !=
		        std::string::npos);
		/* c3849 Wave 5f Unit 2: the loose path feeds the tunables trio
		 * through the EXTENDED RegisterWeaponSourceJson signature. */
		REQUIRE(catalog_load.find("settings, settings_size, variables, variables_size") !=
		        std::string::npos);
		REQUIRE(catalog_load.find("presentation, presentation_size") !=
		        std::string::npos);
	}
	REQUIRE(match_smoke.find("MANIFEST-SP: load 'base:dy357'") !=
	        std::string::npos);
	REQUIRE(match_smoke.find("tools/smoke-verify/fixtures/no_mods_enabled.json") !=
	        std::string::npos);
	REQUIRE(match_smoke.find("\"dst\": \"mods-enabled.json\"") !=
	        std::string::npos);
	REQUIRE(match_smoke.find("SPAWN: player 0 spawned with weapon 8 \\\\(DY357\\\\)") !=
	        std::string::npos);
	REQUIRE(match_smoke.find("shortname=19540 name=19468 primary_name=19541 secondary_name=19550") !=
	        std::string::npos);
	REQUIRE(enum_reverse.find("parse_l_gun_name") != std::string::npos);
	REQUIRE(enum_reverse.find("#define LOADER_ENUM_L_GUN_BASE  0x4c00") !=
	        std::string::npos);
	REQUIRE(enum_reverse.find("#define LOADER_ENUM_L_GUN_COUNT 244") !=
	        std::string::npos);
	REQUIRE(enum_reverse.find("name_for_l_gun_value") != std::string::npos);
	REQUIRE(enum_reverse.find("static __thread char namebuf[16]") !=
	        std::string::npos);
	REQUIRE(enum_reverse.find("parse_l_gun_name(name, &value)") !=
	        std::string::npos);
	REQUIRE(gun_lang.find("\"id\": \"L_GUN_050\"") != std::string::npos);
	REQUIRE(gun_lang.find("\"id\": \"L_GUN_057\"") != std::string::npos);
	REQUIRE(gun_lang.find("\"id\": \"L_GUN_058\"") != std::string::npos);
	const size_t smoke_required = match_smoke.find("\"required_lines\"");
	const size_t smoke_forbidden = match_smoke.find("\"forbidden_patterns\"");
	REQUIRE(smoke_required != std::string::npos);
	REQUIRE(smoke_forbidden != std::string::npos);
	REQUIRE(smoke_required < smoke_forbidden);
	const std::string smoke_required_lines =
		match_smoke.substr(smoke_required, smoke_forbidden - smoke_required);
	REQUIRE(smoke_required_lines.find("base:dy357magnum") == std::string::npos);
	REQUIRE(smoke_required_lines.find("base:magsec4") == std::string::npos);
	REQUIRE(match_smoke.find("base:dy357magnum", smoke_forbidden) != std::string::npos);
	REQUIRE(match_smoke.find("base:magsec4", smoke_forbidden) != std::string::npos);
	REQUIRE(match_smoke.find("ASSET\\\\.SOURCE_ONLY", smoke_forbidden) !=
	        std::string::npos);

	const std::string constraints = readTextFile("context/constraints.md");
	const std::string catalog = readTextFile("context/pillars/catalog.md");
	const std::string preview = readTextFile("port/include/pdgui_charpreview.h");
	REQUIRE(constraints.find("base:weapon_falcon2") == std::string::npos);
	REQUIRE(catalog.find("base:weapon_falcon2") == std::string::npos);
	REQUIRE(preview.find("base:weapon_falcon2") == std::string::npos);
	REQUIRE(constraints.find("base:falcon2") != std::string::npos);
	REQUIRE(catalog.find("base:falcon2") != std::string::npos);
	REQUIRE(preview.find("base:falcon2") != std::string::npos);
}

TEST_CASE("shared smoke install refreshes explicit source binaries",
          "[modding][pdxxx][c3844][smoke][static]") {
	const std::string harness =
		readTextFile("tools/smoke-verify/lib/Install-Harness.ps1");
	const std::string managed =
		readTextFile("tools/smoke-verify/lib/Storage-Harness.ps1");
	const std::string storage = readTextFile("tools/smoke-verify/storage.py");
	const std::string runner = readTextFile("tools/smoke-verify/run.ps1");
	REQUIRE(harness.find("if ($ExplicitPath) {") != std::string::npos);
	REQUIRE(harness.find("return $null") != std::string::npos);
	REQUIRE(harness.find("New-SmokeManagedInstall -ProjectRoot $ProjectRoot -TestName $TestName") !=
	        std::string::npos);
	REQUIRE(harness.find("-SourceRom $SourceRom -Target $Target -Shared") !=
	        std::string::npos);
	REQUIRE(managed.find("Find-SourceBinary -ProjectRoot $ProjectRoot -ExplicitPath $SourceBinary") !=
	        std::string::npos);
	REQUIRE(managed.find("$sources.Add(@($bin, $exeName))") != std::string::npos);
	REQUIRE(managed.find("Invoke-SmokeStorage -Action seed") != std::string::npos);
	REQUIRE(storage.find("files[str(relative(destination))] = self.put(plain(source))") !=
	        std::string::npos);
	REQUIRE(storage.find("digest = hashlib.file_digest(f, \"sha256\").hexdigest()") !=
	        std::string::npos);
	REQUIRE(harness.find("$srcInfo.LastWriteTimeUtc -le $dstInfo.LastWriteTimeUtc") ==
	        std::string::npos);
	REQUIRE(harness.find("New-NetFirewallRule -DisplayName $displayName `") !=
	        std::string::npos);
	REQUIRE(harness.find("-ErrorAction Stop | Out-Null") !=
	        std::string::npos);
	REQUIRE(runner.find("$all = @(Get-SmokeTests -Dir $TestsDir)") !=
	        std::string::npos);
	REQUIRE(runner.find("$crashArgs = @()") != std::string::npos);
	REQUIRE(runner.find("$env:PD_SMOKE_DISABLE_CRASH_HANDLER -eq \"1\"") !=
	        std::string::npos);
	REQUIRE(runner.find("$env:PD_SMOKE_KEEP_CRASH_HANDLER") == std::string::npos);
	REQUIRE(runner.find("$allArgs = @(\"--smoke\", $Test.Path) + $crashArgs") !=
	        std::string::npos);
	REQUIRE(runner.find("Keep the game crash handler enabled by default") !=
	        std::string::npos);
}

TEST_CASE("Scenario source matrix runner keeps stage smokes source-only and sequential",
          "[modding][pdxxx][c3844][scenario][smoke][static]") {
	const std::string matrix =
		readTextFile("tools/smoke-verify/run-scenario-source-matrix.ps1");
	const std::string main = readTextFile("port/src/main.c");
	const std::string playerreset = readTextFile("src/game/playerreset.c");

	REQUIRE(matrix.find("AssetSourceOnlyType=30") != std::string::npos);
	REQUIRE(matrix.find("intentionally sequential") != std::string::npos);
	REQUIRE(matrix.find("Build\\data\\ntsc-final\\scenarios") != std::string::npos);
	REQUIRE(matrix.find("_meta/manifest.json") != std::string::npos);
	REQUIRE(matrix.find("ScenarioCatalogId = $id") != std::string::npos);
	REQUIRE(matrix.find("ArenaCatalogId = ($id -replace '^base:scenario_', 'base:arena_')") !=
	        std::string::npos);
	REQUIRE(matrix.find("[string] $Install = \"\"") != std::string::npos);
	REQUIRE(matrix.find("$runnerArgs += @(\"-Install\", $Install)") !=
	        std::string::npos);
	REQUIRE(matrix.find("IsMultiplayer = ($kind -eq \"mp\")") != std::string::npos);
	REQUIRE(matrix.find("AiCommandRows = $aiCommandRows") != std::string::npos);
	REQUIRE(matrix.find("StageNumHex = (\"0x{0:x}\" -f $stagenum)") !=
	        std::string::npos);
	REQUIRE(matrix.find("LegacyBootStage") == std::string::npos);
	REQUIRE(matrix.find("$Entry.ScenarioCatalogId") != std::string::npos);
	REQUIRE(matrix.find("Scenario manifest lacks catalog id") !=
	        std::string::npos);
	REQUIRE(matrix.find("LOAD: lv\\.c entering stage load sequence\",") !=
	        std::string::npos);
	REQUIRE(matrix.find("$stageHexRegex = (\"0x0*{0:x}\"") ==
	        std::string::npos);
	REQUIRE(matrix.find("--boot-stage") != std::string::npos);
	REQUIRE(matrix.find("--launch-mp-room") != std::string::npos);
	REQUIRE(matrix.find("base:combat") != std::string::npos);
	REQUIRE(matrix.find("--debug-auto-start-match") != std::string::npos);
	REQUIRE(matrix.find("direct match start should bind MP arena") != std::string::npos);
	REQUIRE(matrix.find("BOOT: --launch-mp-room direct match start") !=
	        std::string::npos);
	REQUIRE(matrix.find("MATCHSTART\\.DIAG: entry stage_id='{0}'") !=
	        std::string::npos);
	REQUIRE(matrix.find("MATCHSETUP: stage '{0}'.*stagenum={1}") !=
	        std::string::npos);
	REQUIRE(matrix.find("if ([int]$Entry.AiCommandRows -eq 0)") !=
	        std::string::npos);
	REQUIRE(matrix.find("^SCENARIO\\\\\\.GRAPH: AI (?!list source)") !=
	        std::string::npos);
	REQUIRE(matrix.find("$Entry.LegacyBootStageHex") == std::string::npos);
	REQUIRE(matrix.find("tools/smoke-verify/run.ps1") != std::string::npos);
	REQUIRE(matrix.find("-TestsDir") != std::string::npos);
	REQUIRE(matrix.find("System.Text.UTF8Encoding($false)") !=
	        std::string::npos);
	REQUIRE(matrix.find("[System.IO.File]::WriteAllText") !=
	        std::string::npos);
	REQUIRE(matrix.find("base:scenario_chicago") != std::string::npos);
	REQUIRE(matrix.find("base:scenario_citraining") != std::string::npos);
	REQUIRE(matrix.find("base:scenario_airbase") != std::string::npos);
	REQUIRE(matrix.find("base:scenario_test_ash") != std::string::npos);
	REQUIRE(matrix.find("base:scenario_extra25") != std::string::npos);
	REQUIRE(matrix.find("base:scenario_extra26") != std::string::npos);
	REQUIRE(main.find("sysArgGetString(\"--boot-stage\")") != std::string::npos);
	REQUIRE(main.find("sysArgGetInt(\"--boot-stage\", STAGE_TITLE)") ==
	        std::string::npos);
	REQUIRE(main.find("bootResolveStageIdToNum(bootStageArg)") !=
	        std::string::npos);
	REQUIRE(main.find("entry->type == ASSET_SCENARIO") != std::string::npos);
	REQUIRE(main.find("entry->ext.scenario.stagenum") != std::string::npos);
	REQUIRE(main.find("g_StageNum > STAGE_EXTRA26") != std::string::npos);
	REQUIRE(main.find("g_StageNum > 0x5d") == std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: added scene colmesh") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: compiled portals\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: built native portal tables") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: activated level graph") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: table refs") != std::string::npos);
	REQUIRE(matrix.find("portals=.*{1}::portals\\.json") != std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: global settings source") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.global\\.settings\\+level\\.graph\\.nodes") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: level tick from graph source") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.global\\.settings\\+level\\.tick") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: volume source") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.trigger\\.volumes\\+volumes\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: pad source") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.pads\\+pads\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("source_counts=pads:\\d+,volumes:\\d+,waypoints:\\d+,waygroups:\\d+,covers:\\d+,paths:\\d+") !=
	        std::string::npos);
	REQUIRE(matrix.find("source_hashes=sha256") !=
	        std::string::npos);
	REQUIRE(matrix.find("capabilities=walk,jump,drop,wall,ceiling") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.navigation\\.generate\\+navigation\\.ini\\+generated-navmesh\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: trigger volume nodes") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.trigger\\.volumes\\+level\\.graph\\.nodes\\+volumes\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI list source") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.lists\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI basic/lifecycle actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("stop=1 kneel=1 surrender=1 fade_out=1 remove_chr=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.character_lifecycle\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI combat actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("sidestep=1 jump_out=1 run_sideways=1 attack_walk=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("consider_grenade_throw=1 drop_item=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.combat\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI target movement actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("run_from_target=1 jog_to_target_prop=1 walk_to_target_prop=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.target_movement\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI perception/alarm actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("hear_alarm=1 patrolling=1 alarm_active=1 gas_active=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.condition\\.perception_alarm\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI spatial perception conditions") !=
	        std::string::npos);
	REQUIRE(matrix.find("los_chr=1 never_screen=1 on_screen=1 chr_room_screen=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.condition\\.spatial_perception\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI distance perception conditions") !=
	        std::string::npos);
	REQUIRE(matrix.find("chr_pad_lt=1 chr_pad_gt=1 dist_chr_lt=1 dist_chr_gt=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.condition\\.distance_perception\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI room/object/weapon conditions") !=
	        std::string::npos);
	REQUIRE(matrix.find("chr_room=1 target_room=1 chr_object=1 weapon_thrown=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.condition\\.room_object_weapon\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI object interaction actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("activated=1 interact=1 destroy=1 drop_object=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.object_interaction\\+ai/ailists\\.json\\+objects\\.json\\+pads\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI animation actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("chr_do_animation=1 surprise_one_hand=1 surprise_look_around=1 surprise_surrender=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.animation\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI random control") !=
	        std::string::npos);
	REQUIRE(matrix.find("random=1 less_than=1 greater_than=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.control\\.random\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI debug/no-op actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("print=1 noop=1") != std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.debug_noop\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI list-control actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("set_list=1 set_return_list=1 set_shot_list=1 return_list=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.list_control\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI pad actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("walk_to_pad=1 run_to_pad=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI pad movement actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("jog_to_pad=1 go_to_pad_preset=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.pad\\+pads\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI pad-preset actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("set_pad_preset=1 chr_set_pad_preset=1 chr_copy_pad_preset=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.pad_preset\\+pads\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI chr-preset actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("set_chr_preset=1 set_chr_target=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.chr_preset\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI morale/alertness actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("set_morale=1 add_morale=1 chr_add_morale=1 subtract_morale=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.state\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI character conditions") !=
	        std::string::npos);
	REQUIRE(matrix.find("if_num_arghs_less_than=1 if_num_arghs_greater_than=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.condition\\.character_state\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI lifecycle/perception conditions") !=
	        std::string::npos);
	REQUIRE(matrix.find("if_idle=1 if_stopped=1 if_chr_dead=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.condition\\.lifecycle\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI tuning actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("set_hear_distance=1 set_view_distance=1 set_grenade_probability=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.tuning\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI action/order actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("set_action=1 set_team_orders=1 retreat=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.orders\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI intent/status conditions") !=
	        std::string::npos);
	REQUIRE(matrix.find("not_talking=1 orders=1 has_orders=1 squadron_action=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.condition\\.intent_status\\+ai/ailists\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: path source") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.navigation\\.paths\\+navigation/paths\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI mission/global actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("if_objective_complete=1 if_objective_failed=1 if_all_objectives_complete=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("kill_bond=1 backend=graph\\.ai\\.action\\.mission_global\\+ai/ailists\\.json\\+mission\\.graph\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI path actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("set_path=1 start_patrol=1") != std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.path\\+navigation/paths\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("AI action set_path chr_rows=\\d+ source_chr=-?\\d+ path=\\d+ found=1 path_rows=\\d+") !=
	        std::string::npos);
	REQUIRE(matrix.find("AI action start_patrol chr_rows=\\d+ source_chr=-?\\d+ path=\\d+ found=1 path_rows=\\d+") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI cover actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("find_cover=1 find_cover_within_dist=1 find_cover_outside_dist=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.cover\\+navigation/covers\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI player navigation actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("player_auto_walk=1 if_player_auto_walk_finished=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.player_navigation\\+ai/ailists\\.json\\+pads\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI quadrant pad-preset actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("waypoint_quadrant=1 target_quadrant=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.quadrant_preset\\+ai/ailists\\.json\\+pads\\.json\\+navigation\\.generate") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: AI vehicle motion actions") !=
	        std::string::npos);
	REQUIRE(matrix.find("hovercar_begin_path=1 set_vehicle_speed=1 set_rotor_speed=1") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=graph\\.ai\\.action\\.vehicle\\+navigation/paths\\.json") !=
	        std::string::npos);
	for (const char *label : {
		     "SCENARIO\\.GRAPH: AI vehicle/investigation actions",
		     "SCENARIO\\.GRAPH: AI safety/detection conditions",
		     "SCENARIO\\.GRAPH: AI misc branch conditions",
		     "SCENARIO\\.GRAPH: AI misc effect actions",
		     "SCENARIO\\.GRAPH: AI quip/setup shuffle actions",
		     "SCENARIO\\.GRAPH: AI team maintenance actions",
		     "SCENARIO\\.GRAPH: AI alarm actions",
		     "SCENARIO\\.GRAPH: AI flag actions",
		     "SCENARIO\\.GRAPH: AI chr/object flag actions",
		     "SCENARIO\\.GRAPH: AI door actions",
		     "SCENARIO\\.GRAPH: AI lift actions",
		     "SCENARIO\\.GRAPH: AI weather actions",
		     "SCENARIO\\.GRAPH: AI sky actions",
		     "SCENARIO\\.GRAPH: AI lighting actions",
		     "SCENARIO\\.GRAPH: AI room-flag actions",
		     "SCENARIO\\.GRAPH: AI cutscene visibility actions",
		     "SCENARIO\\.GRAPH: AI environment actions",
		     "SCENARIO\\.GRAPH: AI target-distance conditions",
		     "SCENARIO\\.GRAPH: AI audio actions",
		     "SCENARIO\\.GRAPH: AI music track actions",
		     "SCENARIO\\.GRAPH: AI player weapon-state actions",
		     "SCENARIO\\.GRAPH: AI player cutscene/warp actions",
		     "SCENARIO\\.GRAPH: AI setup/spawn/equipment actions",
		     "SCENARIO\\.GRAPH: AI entity lifecycle/motion actions",
		     "SCENARIO\\.GRAPH: AI gun interaction actions",
		     "SCENARIO\\.GRAPH: AI character property actions",
		     "SCENARIO\\.GRAPH: AI object-room conditions",
		     "SCENARIO\\.GRAPH: AI perception conditions",
		     "SCENARIO\\.GRAPH: AI character/inventory actions",
		     "SCENARIO\\.GRAPH: AI player-state actions",
		     "SCENARIO\\.GRAPH: AI state/device conditions",
		     "SCENARIO\\.GRAPH: AI teleport/cutscene weapon actions",
		     "SCENARIO\\.GRAPH: AI cutscene presentation actions",
		     "SCENARIO\\.GRAPH: AI music/mode conditions",
		     "SCENARIO\\.GRAPH: AI pad/reference actions",
		     "SCENARIO\\.GRAPH: AI model-part actions",
		     "SCENARIO\\.GRAPH: AI object-health actions",
		     "SCENARIO\\.GRAPH: AI special-death actions",
		     "SCENARIO\\.GRAPH: AI room-search actions",
		     "SCENARIO\\.GRAPH: AI savefile flag actions",
		     "SCENARIO\\.GRAPH: AI timer/countdown actions",
		     "SCENARIO\\.GRAPH: AI HUD message actions",
	     }) {
		REQUIRE(matrix.find(label) != std::string::npos);
	}
	for (const char *proof : {
		     "danger_object=1 heli_armed=1 hoverbot_next_step=1",
		     "safety2=1 player_cmp_ar34=1 detect_same_floor=1",
		     "squadron_dead=1 if_true=1 squadron_count=1",
		     "chr_explosions=1 tinted_glass=1 rocket=1",
		     "say_quip=1 ci_staff_quip=1 ruins_pillars=1",
		     "set_chr_preset_to_unalerted_teammate=1 rebuild_teams=1 rebuild_squadrons=1",
		     "try_start_alarm=1 activate_alarm=1 deactivate_alarm=1",
		     "set_flag=1 unset_flag=1 if_has_flag=1",
		     "set_chrflag=1 unset_chrflag=1 if_has_chrflag=1",
		     "open_door=1 close_door=1 if_door_state=1",
		     "if_lift_stationary=1 lift_go_to_stop=1 if_lift_at_stop=1",
		     "configure_rain=1 configure_snow=1",
		     "switch_to_alt_sky=1 set_wind_speed=1",
		     "set_lights=1",
		     "set_room_flag=1",
		     "show_cutscene_chrs=1",
		     "configure_environment=1",
		     "if_distance_to_target2_less_than=1 if_distance_to_target2_greater_than=1",
		     "speak=1 play_sound=1 assign_sound=1",
		     "play_x_track=1 stop_x_track=1 play_track_isolated=1",
		     "chr_draw_weapon=1 chr_draw_weapon_in_cutscene=1 set_player_force_speed=1",
		     "end_level=1 end_cutscene=1 warp_jo_to_pad=1",
		     "spawn_chr_at_pad=1 spawn_chr_at_chr=1 try_equip_weapon=1",
		     "duplicate_chr=1 enable_chr=1 disable_chr=1",
		     "do_gun_command=1 if_distance_to_gun_less_than=1 recover_gun=1",
		     "chr_copy_properties=1",
		     "if_obj_in_room=1",
		     "if_player_looking_at_object=1 if_target_is_player=1",
		     "chr_kill=1 remove_weapon_from_inventory=1 clear_inventory=1",
		     "toggle_p1p2=1 chr_set_p1p2=1 chr_set_cloaked=1",
		     "if_pouncebits_eq=1 if_training_pc_holographed=1 if_player_using_device=1",
		     "chr_begin_or_end_teleport=1 if_chr_teleport_full_white=1 chr_set_cutscene_weapon=1",
		     "fade_screen=1 if_fade_complete=1 set_chr_hudpiece_visible=1",
		     "if_music_event_queue_is_empty=1 if_coop_mode=1",
		     "if_chr_same_floor_distance_to_pad_less_than=1 remove_references_to_chr=1",
		     "chr_toggle_model_part=1 obj_set_model_part_visible=1",
		     "if_obj_health_less_than=1 set_obj_health=1",
		     "set_chr_special_death_animation=1",
		     "set_room_to_search=1",
		     "set_savefile_flag=1 unset_savefile_flag=1 if_savefile_flag_set=1",
		     "restart_timer=1 reset_timer=1 pause_timer=1",
		     "show_hudmsg=1 show_hudmsg_middle=1 show_hudmsg_top_middle=1",
	     }) {
		REQUIRE(matrix.find(proof) != std::string::npos);
	}
	for (const char *backend : {
		     "backend=graph\\.ai\\.action\\.vehicle_investigation\\+ai/ailists\\.json\\+objects\\.json\\+pads\\.json",
		     "backend=graph\\.ai\\.condition\\.safety_detection\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.condition\\.misc_branch\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.misc_effect\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.quip_shuffle\\+ai/ailists\\.json\\+objects\\.json",
		     "backend=graph\\.ai\\.action\\.team\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.alarm\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.flags\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.chr_object_flags\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.door\\+objects\\.json",
		     "backend=graph\\.ai\\.action\\.lift\\+objects\\.json\\+pads\\.json",
		     "backend=graph\\.ai\\.action\\.weather\\+ai/ailists\\.json\\+scenario\\.ini",
		     "backend=graph\\.ai\\.action\\.sky\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.lighting\\+ai/ailists\\.json\\+pads\\.json",
		     "backend=graph\\.ai\\.action\\.room_flags\\+ai/ailists\\.json\\+scene\\.glb",
		     "backend=graph\\.ai\\.action\\.cutscene_visibility\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.environment\\+ai/ailists\\.json\\+scenario\\.ini\\+scene\\.glb",
		     "backend=graph\\.ai\\.condition\\.target_distance\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.audio\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.music_track\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.player_weapon_state\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.player_cutscene\\+ai/ailists\\.json\\+pads\\.json\\+objects\\.json",
		     "backend=graph\\.ai\\.action\\.setup_spawn\\+ai/ailists\\.json\\+pads\\.json\\+objects\\.json",
		     "backend=graph\\.ai\\.action\\.entity_lifecycle\\+ai/ailists\\.json\\+pads\\.json\\+objects\\.json",
		     "backend=graph\\.ai\\.action\\.gun_interaction\\+ai/ailists\\.json\\+objects\\.json\\+scene\\.glb",
		     "backend=graph\\.ai\\.action\\.character_property\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.condition\\.object_room\\+ai/ailists\\.json\\+objects\\.json\\+pads\\.json\\+scene\\.glb",
		     "backend=graph\\.ai\\.condition\\.perception\\+ai/ailists\\.json\\+objects\\.json\\+scene\\.glb",
		     "backend=graph\\.ai\\.action\\.character_inventory\\+ai/ailists\\.json\\+objects\\.json",
		     "backend=graph\\.ai\\.action\\.player_state\\+ai/ailists\\.json\\+objects\\.json",
		     "backend=graph\\.ai\\.condition\\.state_device\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.teleport_cutscene_weapon\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.cutscene_presentation\\+ai/ailists\\.json\\+scene\\.glb",
		     "backend=graph\\.ai\\.condition\\.music_mode\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.pad_reference\\+ai/ailists\\.json\\+pads\\.json",
		     "backend=graph\\.ai\\.action\\.model_part\\+ai/ailists\\.json\\+objects\\.json",
		     "backend=graph\\.ai\\.action\\.object_health\\+ai/ailists\\.json\\+objects\\.json",
		     "backend=graph\\.ai\\.action\\.special_death\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.room_search\\+ai/ailists\\.json\\+scene\\.glb",
		     "backend=graph\\.ai\\.action\\.savefile_flags\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.timer\\+ai/ailists\\.json",
		     "backend=graph\\.ai\\.action\\.hud\\+ai/ailists\\.json",
	     }) {
		REQUIRE(matrix.find(backend) != std::string::npos);
	}
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: compiled setup\\.fields\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("path_flags=circular:\\d+,flying:\\d+") !=
	        std::string::npos);
	REQUIRE(matrix.find("Count-JsonRows") != std::string::npos);
	REQUIRE(matrix.find("RequiresGeneratedNavigation") != std::string::npos);
	REQUIRE(matrix.find("NavigationPathRows") != std::string::npos);
	REQUIRE(matrix.find("$MissionDir = Join-Path $ProjectRoot \"Build\\data\\ntsc-final\\missions\"") !=
	        std::string::npos);
	REQUIRE(matrix.find("HasMissionGraph") != std::string::npos);
	REQUIRE(matrix.find("MissionArchiveName") != std::string::npos);
	REQUIRE(matrix.find("Get-MissionObjectiveSummary") != std::string::npos);
	REQUIRE(matrix.find("MissionObjectiveRows") != std::string::npos);
	REQUIRE(matrix.find("MissionObjectiveCriteriaRows") !=
	        std::string::npos);
	REQUIRE(matrix.find("MissionHasMissionFlagCriteria") !=
	        std::string::npos);
	REQUIRE(matrix.find("MissionHasObjectStateCriteria") !=
	        std::string::npos);
	REQUIRE(matrix.find("Get-ScenarioSetupSummary") != std::string::npos);
	REQUIRE(matrix.find("SetupBehaviorLinkRows") != std::string::npos);
	REQUIRE(matrix.find("SetupBehaviorLinkKinds") != std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: setup behavior link source") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: setup behavior link registered from graph source") !=
	        std::string::npos);
	REQUIRE(matrix.find("[regex]::Escape($kind)") != std::string::npos);
	REQUIRE(matrix.find("kind={1}") != std::string::npos);
	REQUIRE(matrix.find("kind=[a-z_]+") == std::string::npos);
	REQUIRE(matrix.find("$Entry.Id -eq \"base:scenario_citraining\"") !=
	        std::string::npos);
	REQUIRE(matrix.find("$Entry.Id -eq \"base:scenario_rescue\"") !=
	        std::string::npos);
	REQUIRE(matrix.find("$Entry.Id -eq \"base:scenario_extra16\"") !=
	        std::string::npos);
	REQUIRE(matrix.find("$Entry.Id -eq \"base:scenario_duel\"") !=
	        std::string::npos);
	for (const char *extra16_live_proof : {
		     "AI action object_move_to_pad tag=\\d+ object_rows=\\d+ pad=\\d+ found=1 pad_rows=\\d+",
		     "AI action activate_lift liftnum=\\d+ pad=\\d+ found=1 pad_rows=\\d+ tag=\\d+ object_rows=\\d+",
		     "AI action set_lights pad=\\d+ found=1 pad_rows=\\d+ chr_rows=\\d+ source_chr=-?\\d+",
		     "AI condition if_chr_distance_to_pad_less_than value=\\d+ pad=\\d+ found=1 pad_rows=\\d+",
		     "AI condition if_chr_in_room chr=\\d+ room_type=\\d+ pad=\\d+ found=1 pad_rows=\\d+",
		     "AI action try_jog_to_target_prop chr=\\d+ chr_rows=\\d+ source_chr=-?\\d+ target_chr=-?\\d+",
		     "AI action try_run_to_target_prop chr=\\d+ chr_rows=\\d+ source_chr=-?\\d+ target_chr=-?\\d+",
	}) {
		REQUIRE(matrix.find(extra16_live_proof) != std::string::npos);
	}
	for (const char *duel_live_proof : {
		     "AI action player_auto_walk chr=\\d+ chr_rows=\\d+ target_chr=-?\\d+ player_checked=\\d+ pad=\\d+ found=1 pad_rows=\\d+ applied=\\d+",
		     "AI condition if_player_auto_walk_finished chr=\\d+ chr_rows=\\d+ target_chr=-?\\d+ player_checked=\\d+ label=\\d+ walking=\\d+",
	     }) {
		REQUIRE(matrix.find(duel_live_proof) != std::string::npos);
	}
	for (const char *rescue_live_proof : {
		     "AI action run_to_pad chr_rows=\\d+ source_chr=-?\\d+ pad=\\d+ found=1 pad_rows=\\d+",
		     "AI action give_object_to_chr tag=\\d+ chr=\\d+ chr_rows=\\d+ target_chr=-?\\d+ player_checked=\\d+ object_rows=\\d+ applied=\\d+",
		     "AI action object_do_animation anim_id=[^ ]+ anim_source=.*\\.pdanim::animation\\.gltf clip_bytes=[1-9]\\d* tag=\\d+ resolved_tag=\\d+ object_rows=\\d+ chr_rows=\\d+",
		     "AI action lock_door tag=\\d+ object_rows=\\d+ bits=0x[0-9a-fA-F]+ applied=\\d+",
		     "AI action open_door tag=\\d+ object_rows=\\d+ applied=\\d+",
		     "AI action set_lights pad=\\d+ found=1 pad_rows=\\d+ chr_rows=\\d+ source_chr=-?\\d+",
		     "AI action configure_environment room=\\d+ room_rows=\\d+ command=0x[0-9a-fA-F]+ value=\\d+",
		     "AI action set_max_damage chr=\\d+ chr_rows=\\d+ target_chr=-?\\d+ vehicle_rows=\\d+ source_vehicle_type=-?\\d+",
		     "AI action set_shot_list chr_rows=\\d+ source_chr=-?\\d+ list=\\d+ applied=\\d+",
		     "AI action speak chr=\\d+ chr_rows=\\d+ target_chr=-?\\d+ target_selector=\\d+ player_checked=\\d+ text_id=[^ ]+ audio_id=[^ ]+",
		     "AI action assign_sound channel=\\d+ audio_id=[^ ]+",
		     "AI action set_object_sound_playing tag=\\d+ object_rows=\\d+ channel=\\d+ timer=\\d+ applied=\\d+",
	     }) {
		REQUIRE(matrix.find(rescue_live_proof) != std::string::npos);
	}
	REQUIRE(matrix.find("if_stage_id_(less_than|greater_than) stagenum=") !=
	        std::string::npos);
	REQUIRE(matrix.find("AI action chr_set_cutscene_weapon chr=\\d+ weapon=") !=
	        std::string::npos);
	for (const char *forbidden_audio_proof : {
		     "AI action speak chr=\\d+ text=\\d+ audio=",
		     "AI action play_sound channel=\\d+ audio=",
		     "AI action assign_sound channel=\\d+ audio=",
		     "AI action play_sound_from_prop .* audio=",
		     "AI action play_repeating_sound_from_pad .* sound=",
	     }) {
		REQUIRE(matrix.find(forbidden_audio_proof) != std::string::npos);
	}
	for (const char *forbidden_music_proof : {
		     "AI action play_temporary_primary_track track=",
		     "AI action play_x_track reason=\\d+ track=",
		     "AI action play_track_isolated track=",
		     "AI action play_cutscene_track track=",
		     "AI action play_temporary_track track=",
	     }) {
		REQUIRE(matrix.find(forbidden_music_proof) != std::string::npos);
	}
	REQUIRE(matrix.find("AI action play_cutscene_track track_id=[^ ]+") !=
	        std::string::npos);
	for (const char *forbidden_animation_proof : {
		     "AI action set_camera_animation anim=",
		     "AI action chr_do_animation chr=\\d+ anim=",
		     "AI action object_do_animation anim=",
		     "AI action do_preset_animation preset=\\d+ applied=",
		     "AI condition if_natural_anim anim=",
	     }) {
		REQUIRE(matrix.find(forbidden_animation_proof) !=
		        std::string::npos);
	}
	REQUIRE(matrix.find("AI action set_camera_animation anim_id=[^ ]+ anim_source=.*\\.pdanim::animation\\.gltf clip_bytes=[1-9]\\d*") !=
	        std::string::npos);
	REQUIRE(matrix.find("AI action chr_do_animation chr=\\d+ chr_rows=\\d+ target_chr=-?\\d+ player_checked=\\d+ anim_id=[^ ]+ anim_source=.*\\.pdanim::animation\\.gltf clip_bytes=[1-9]\\d*") !=
	        std::string::npos);
	REQUIRE(matrix.find("does not deterministically execute every savefile/text/preset branch") !=
	        std::string::npos);
	for (const char *forbidden_asset_proof : {
		     "AI action drop_item chr=\\d+ model=",
		     "AI condition if_player_using_cmp_or_ar34 weapon=",
		     "AI action chr_draw_weapon chr=\\d+ weapon=",
		     "AI action chr_draw_weapon_in_cutscene chr=\\d+ weapon=",
		     "AI action chr_delete_weapon chr=\\d+ weapon=",
		     "AI action spawn_chr_at_pad body=",
		     "AI action spawn_chr_at_chr body=",
		     "AI action try_equip_weapon model=",
		     "AI action try_equip_hat model=",
		     "AI action set_obj_image .* image=",
		     "AI action remove_weapon_from_inventory weapon=",
	     }) {
		REQUIRE(matrix.find(forbidden_asset_proof) !=
		        std::string::npos);
	}
	for (const char *runtime_proof : {
		     "SCENARIO\\.GRAPH: AI action set_list",
		     "SCENARIO\\.GRAPH: AI action chr_move_to_pad",
		     "SCENARIO\\.GRAPH: AI action set_obj_flag",
		     "SCENARIO\\.GRAPH: AI condition if_in_cutscene",
		     "SCENARIO\\.GRAPH: AI action end_cutscene",
		     "SCENARIO\\.GRAPH: AI condition if_chr_distance_to_pad_less_than",
		     "SCENARIO\\.GRAPH: AI condition if_chr_activated_object",
		     "SCENARIO\\.GRAPH: AI condition if_can_see_target",
		     "SCENARIO\\.GRAPH: AI action random",
		     "SCENARIO\\.GRAPH: AI condition if_random_greater_than",
		     "SCENARIO\\.GRAPH: AI condition if_obj_in_room",
		     "SCENARIO\\.GRAPH: AI condition if_los_to_target",
		     "SCENARIO\\.GRAPH: AI action if_obj_has_flag",
		     "SCENARIO\\.GRAPH: AI action set_autogun_target_team",
		     "SCENARIO\\.GRAPH: AI action configure_environment",
		     "SCENARIO\\.GRAPH: AI action stop",
		     "SCENARIO\\.GRAPH: AI action face_entity",
		     "SCENARIO\\.GRAPH: AI action return_list",
		     "SCENARIO\\.GRAPH: AI action play_sound",
		     "SCENARIO\\.GRAPH: AI action toggle_p1p2",
		     "SCENARIO\\.GRAPH: AI action set_passive_mode",
		     "SCENARIO\\.GRAPH: AI action chr_set_chrflag",
		     "SCENARIO\\.GRAPH: AI action set_camera_animation",
		     "SCENARIO\\.GRAPH: AI action play_cutscene_track",
		     "SCENARIO\\.GRAPH: AI action chr_do_animation",
		     "SCENARIO\\.GRAPH: AI action restart_timer",
		     "SCENARIO\\.GRAPH: AI action fade_screen",
		     "SCENARIO\\.GRAPH: AI action if_savefile_flag_unset",
		     "SCENARIO\\.GRAPH: AI action disable_obj",
		     "SCENARIO\\.GRAPH: AI action set_obj_image",
		     "SCENARIO\\.GRAPH: AI action unset_obj_flag",
		     "SCENARIO\\.GRAPH: AI action activate_lift",
		     "SCENARIO\\.GRAPH: AI action set_lights",
		     "SCENARIO\\.GRAPH: AI action set_morale",
		     "SCENARIO\\.GRAPH: AI action if_timer_greater_than",
		     "SCENARIO\\.GRAPH: AI action chr_set_team",
		     "SCENARIO\\.GRAPH: AI action if_stage_flag_eq",
		     "SCENARIO\\.GRAPH: AI action deactivate_alarm",
		     "SCENARIO\\.GRAPH: AI action if_savefile_flag_set",
		     "SCENARIO\\.GRAPH: AI action if_chr_has_hidden_flag",
		     "SCENARIO\\.GRAPH: AI action reorient_for_cutscene_stop",
		     "SCENARIO\\.GRAPH: AI action chr_unset_chrflag",
		     "SCENARIO\\.GRAPH: AI action chr_set_hidden_flag",
		     "SCENARIO\\.GRAPH: AI action print",
		     "SCENARIO\\.GRAPH: AI action set_chrflag",
		     "SCENARIO\\.GRAPH: AI action set_return_list",
		     "SCENARIO\\.GRAPH: AI action if_has_flag",
	}) {
		REQUIRE(matrix.find(runtime_proof) != std::string::npos);
	}
	REQUIRE(matrix.find("resolved_pad=\\d+ mode=\\d+ pass=\\d+ pad_rows=\\d+") !=
	        std::string::npos);
	REQUIRE(matrix.find("go_to_pad_preset chr_rows=\\d+ source_chr=-?\\d+ pad=\\d+ found=1 pad_rows=\\d+") !=
	        std::string::npos);
	REQUIRE(matrix.find("spawn_chr_at_pad body_id=[^ ]+ head_id=[^ ]+ pad=\\d+ found=1 pad_rows=\\d+") !=
	        std::string::npos);
	REQUIRE(matrix.find("set_obj_image tag=\\d+ object_rows=\\d+ slot=\\d+ image_index=\\d+") !=
	        std::string::npos);
	REQUIRE(matrix.find("activate_lift liftnum=\\d+ pad=\\d+ found=1 pad_rows=\\d+ tag=\\d+ object_rows=\\d+") !=
	        std::string::npos);
	REQUIRE(matrix.find("set_lights pad=\\d+ found=1 pad_rows=\\d+ chr_rows=\\d+ source_chr=-?\\d+ room=-?\\d+") !=
	        std::string::npos);
	REQUIRE(matrix.find("if_chr_distance_to_pad_less_than value=\\d+ pad=\\d+ found=1 pad_rows=\\d+") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: compiled scene tiles") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: validated background scene source") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: background renderer using native scene source") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: skipped legacy dynamic-light precompute") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: built native background room tables") !=
	        std::string::npos);
	REQUIRE(matrix.find("navigation/paths\\.json '.*{0}::navigation/paths\\.json'") !=
	        std::string::npos);
	REQUIRE(matrix.find("\\d+ paths, \\d+ AI lists") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: compiled navigation tables") !=
	        std::string::npos);
	REQUIRE(matrix.find("Count-JsonListRefs") !=
	        std::string::npos);
	REQUIRE(matrix.find("Count-GeneratedPathWaypointNeighbourRefs") !=
	        std::string::npos);
	REQUIRE(matrix.find("NavigationWaypointNeighbourRefs") !=
	        std::string::npos);
	REQUIRE(matrix.find("NavigationWaygroupNeighbourRefs") !=
	        std::string::npos);
	REQUIRE(matrix.find("NavigationRuntimeWaypointNeighbourRefs") !=
	        std::string::npos);
	REQUIRE(matrix.find("NavigationRuntimeWaygroupNeighbourRefs") !=
	        std::string::npos);
	REQUIRE(matrix.find("runtimeWaypointNeighbourRefs = Count-GeneratedPathWaypointNeighbourRefs") !=
	        std::string::npos);
	REQUIRE(matrix.find("waypoint_neighbour_refs={1}") !=
	        std::string::npos);
	REQUIRE(matrix.find("waygroup_neighbour_refs={2}") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: generated deterministic navigation tables from public pads\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=source\\.navigation\\.generated\\+pads\\.json\\+navigation/waypoints\\.json\\+navigation/waygroups\\.json\\+navigation/covers\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=source\\.navigation\\.generated\\+pads\\.json\\+navigation/paths\\.json\\+navigation/waypoints\\.json\\+navigation/waygroups\\.json\\+navigation/covers\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("MISSION\\.GRAPH: activated mission graph") !=
	        std::string::npos);
	REQUIRE(matrix.find("MISSION\\.GRAPH: objective runtime source") !=
	        std::string::npos);
	REQUIRE(matrix.find("MISSION\\.GRAPH: objective insert matched graph source") !=
	        std::string::npos);
	REQUIRE(matrix.find("MISSION\\.GRAPH: objective criteria evaluated from graph source") !=
	        std::string::npos);
	REQUIRE(matrix.find("MISSION\\.GRAPH: objective check routed through graph source") !=
	        std::string::npos);
	REQUIRE(matrix.find("MISSION\\.GRAPH: mission flags updated in graph runtime") !=
	        std::string::npos);
	REQUIRE(matrix.find("MISSION\\.GRAPH: objective object state updated from graph source") !=
	        std::string::npos);
	REQUIRE(matrix.find("MISSION\\.GRAPH: phase source") !=
	        std::string::npos);
	REQUIRE(matrix.find("phase=load reason=mission\\.graph\\.activate") !=
	        std::string::npos);
	REQUIRE(matrix.find("phase=active reason=lvTick\\.start") !=
	        std::string::npos);
	REQUIRE(matrix.find("navigation/waypoints\\.json', waygroups") !=
	        std::string::npos);
	REQUIRE(matrix.find("backend=source\\.navigation\\.tables\\+navigation/waypoints\\.json\\+navigation/waygroups\\.json\\+navigation/covers\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: compiled pads\\.json") !=
	        std::string::npos);
	REQUIRE(matrix.find("ASSET\\.SOURCE_ONLY") != std::string::npos);
	REQUIRE(matrix.find("RomProvider:filenum") != std::string::npos);
	REQUIRE(matrix.find("refusing fallback") != std::string::npos);
	REQUIRE(matrix.find("SPAWN\\.INIT: cdFindGroundInfoAtCyl sentinel") !=
	        std::string::npos);
	REQUIRE(matrix.find("GROUNDSNAP: ignoring invalid chr->ground") !=
	        std::string::npos);
	REQUIRE(playerreset.find("playerResetFindPublicPadFallback") !=
	        std::string::npos);
	REQUIRE(playerreset.find("SPAWN: no intro spawn rows; using grounded public pad") !=
	        std::string::npos);
	REQUIRE(playerreset.find("SPAWN.INIT: source-authored spawn Y used because no floor hit") !=
	        std::string::npos);
}

TEST_CASE("Scenario source accepts explicit empty public pads and portals",
          "[modding][pdxxx][c3844][scenario][source_gate][static]") {
	const std::string runtime =
		readTextFile("port/src/scenario_source_runtime.c");
	const std::string bg = readTextFile("src/game/bg.c");

	REQUIRE(runtime.find("s_parsePadsJson(text, &row_count)") !=
	        std::string::npos);
	REQUIRE(runtime.find("scenarioPortalSourceRead(text, text_size, &rows, &row_count") !=
	        std::string::npos);
	REQUIRE(runtime.find("!rows || row_count <= 0") == std::string::npos);
	REQUIRE(runtime.find("SCENARIO.SOURCE: compiled pads.json") !=
	        std::string::npos);
	REQUIRE(runtime.find("SCENARIO.SOURCE: compiled portals.json") !=
	        std::string::npos);
	REQUIRE(bg.find("if (!portals)") != std::string::npos);
	REQUIRE(bg.find("g_BgPortals = portals;") != std::string::npos);
	REQUIRE(bg.find("portals ? portals :") == std::string::npos);
	REQUIRE(bg.find("portalcount > 0 ? portalcount : 0") !=
	        std::string::npos);
	REQUIRE(bg.find("SCENARIO.SOURCE: built native portal tables") !=
	        std::string::npos);
}

TEST_CASE("Original Grid match prepare probes exact GLB bytes with the native CPU scene builder",
	"[modding][pdxxx][scenario][grid][static]")
{
	const std::string renderer = readTextFile("port/fast3d/scenario_scene_renderer.cpp");
	const std::string runtime = readTextFile("port/src/grid_match_runtime.c");
	const auto probe = functionBlock(renderer, "int scenarioSceneRendererProbeGlbBytes(");
	const auto builder = functionBlock(renderer, "static bool buildCpuSceneBytes(");
	const auto native = functionBlock(renderer, "static bool buildCpuScene(");
	REQUIRE(probe.find("buildCpuSceneBytes(scenario_id, scene_path,") != std::string::npos);
	REQUIRE(probe.find("size, probe, SceneBuild::GeometryProbe)") != std::string::npos);
	REQUIRE(probe.find("fillProbeResult(probe, &summary)") != std::string::npos);
	REQUIRE(probe.find("g_scene") == std::string::npos);
	REQUIRE(probe.find("fsFileLoad") == std::string::npos);
	REQUIRE(native.find("buildCpuSceneBytes(scenario_id, path,") != std::string::npos);
	REQUIRE(native.find("size, out,\n\t\tSceneBuild::Runtime)") != std::string::npos);
	REQUIRE(builder.find("modAssetGltfGlbChunks(bytes, size, &chunks)") != std::string::npos);
	REQUIRE(builder.find("modAssetGltfBufferDocument") != std::string::npos);
	REQUIRE(builder.find("modAssetGltfScenePlanBuild") != std::string::npos);
	REQUIRE(builder.find("bin_size, instance, out, vertex_limit)") != std::string::npos);
	REQUIRE(builder.find("} else if (!detached) {") != std::string::npos);
	REQUIRE(builder.find("if (!geometry_probe && !parseImages(") != std::string::npos);

	const auto source = functionBlock(runtime, "static void *runtimeReadScenarioMember(");
	const auto leaf = functionBlock(runtime, "static const char *runtimeScenarioMemberLeaf(");
	REQUIRE(leaf.find("runtimeHasBoundArchive(source->match, root, source->arena, source->arena_size)") != std::string::npos);
	REQUIRE(leaf.find("strcmp(root, source->arena_root) != 0") != std::string::npos);
	REQUIRE(source.find("runtimeScenarioMemberLeaf(source, path)") != std::string::npos);
	REQUIRE(source.find("runtimeReadMemberChain(source->scenario, source->scenario_size, leaf,") != std::string::npos);
	REQUIRE(source.find("fsFileLoad") == std::string::npos);
	const auto attached = functionBlock(runtime, "static s32 runtimePreflightOriginalScenario(");
	REQUIRE(attached.find("GRID_ARCHIVE_ORIGINAL") != std::string::npos);
	REQUIRE(attached.find("runtimeProbeOriginalScenario(") != std::string::npos);
	const auto preflight = functionBlock(runtime, "static s32 runtimeProbeOriginalScenario(");
	REQUIRE(preflight.find("scenario->ext.scenario.scene_file") != std::string::npos);
	requireTokenOrder(preflight, "scenarioSourcePreflightPortals(", "scenarioSceneRendererProbeGlbBytes(");
	const auto prepare = functionBlock(runtime, "s32 gridMatchRuntimePrepare(");
	requireTokenOrder(prepare, "runtimePreflightOriginalScenario(", "s_PendingMatch = match;");
}

TEST_CASE("Original Grid collision preflight compiles exact native triangles and combines room extents",
	"[modding][pdxxx][scenario][grid][static]")
{
	const std::string compiler = readTextFile("port/src/modasset_compiler.c");
	const std::string runtime = readTextFile("port/src/grid_match_runtime.c");
	const std::string scenario = readTextFile("port/src/scenario_source_runtime.c");
	const auto detached = functionBlock(compiler, "s32 modAssetCompilerBuildColmeshFromBytes(");
	const auto ordinary = functionBlock(compiler, "s32 modAssetCompilerBuildColmesh(");
	const auto shared = functionBlock(compiler, "static s32 buildColmeshFromSourceBytes(");
	REQUIRE(detached.find("buildColmeshFromSourceBytes(") != std::string::npos);
	REQUIRE(ordinary.find("buildColmeshFromSourceBytes(") != std::string::npos);
	REQUIRE(detached.find("fsFileLoad") == std::string::npos);
	REQUIRE(shared.find("parseObjSourceBounded(") != std::string::npos);
	REQUIRE(shared.find("parseGlbMeshBounded(") != std::string::npos);
	REQUIRE(shared.find("meshAddTriangle(") != std::string::npos);
	REQUIRE(shared.find("mesh_budget ? obj_mesh.triangle_count : -1") != std::string::npos);
	REQUIRE(shared.find("mesh_budget && flags_text && !have_colflags") != std::string::npos);
	REQUIRE(shared.find("added->flags = colflags.rows[i].flags;") != std::string::npos);
	REQUIRE(compiler.find("current_room >= SCENARIO_ROOM_DOMAIN_MAX_COUNT") != std::string::npos);
	REQUIRE(compiler.find("colflagsReadStrictUint") != std::string::npos);

	const auto collision = functionBlock(runtime, "static s32 runtimePreflightCollision(");
	REQUIRE(collision.find("scenario->ext.scenario.collision_file[0]") != std::string::npos);
	REQUIRE(collision.find("runtimeReadScenarioMember(") != std::string::npos);
	REQUIRE(collision.find("(flags || flags_absent)") != std::string::npos);
	REQUIRE(collision.find("modAssetCompilerBuildColmeshFromBytes(") != std::string::npos);
	REQUIRE(collision.find("collision->tris[i].roomnum") != std::string::npos);
	REQUIRE(collision.find("meshWorld") == std::string::npos);
	const auto preflight = functionBlock(runtime, "static s32 runtimeProbeOriginalScenario(");
	requireTokenOrder(preflight, "runtimePreflightCollision(", "scenarioSourcePreflightPortals(");
	REQUIRE(preflight.find("probe->collision_max_room, &room_count, err, err_cap") != std::string::npos);
	const auto portals = functionBlock(scenario, "s32 scenarioSourcePreflightPortals(");
	REQUIRE(portals.find("mesh_max_room, s_portalRowsMaxRoom(rows, count)") != std::string::npos);
}

TEST_CASE("Grid closure visits only typed operands resolved by the native setup parser",
	"[modding][pdxxx][scenario][grid][setup][static]")
{
	const auto runtime = readTextFile("port/src/scenario_source_runtime.c");
	const auto visit = functionBlock(runtime, "s32 scenarioSourceVisitSetupAssets(");
	REQUIRE(visit.find(".visit_asset = visit, .visit_userdata = ctx") != std::string::npos);
	requireTokenOrder(visit, "s_setupParseFieldsJson(fields_text, &table)", "s_setupApplyObjectsJson(objects_text, &table)");
	requireTokenOrder(visit, "s_setupApplyObjectsJson(objects_text, &table)", "s_setupFreeTable(&table)");
	for (const char *resident : {"s_activeGraphSetSourceSetupChrRefs", "s_ActiveScenarioGraphs",
			"s_clearGridDecorationCommands", "mempAlloc", "fsFileLoad"}) {
		REQUIRE(visit.find(resident) == std::string::npos);
	}
	const auto live = functionBlock(runtime, "u8 *scenarioSourceLoadSetupForStage(");
	requireTokenOrder(live, "s_setupParseFieldsJson(setup_text, &table)", "s_setupApplyObjectsJson(objects_text, &table)");
	const auto resolve = functionBlock(runtime, "static s32 s_setupResolveAssetOperand(");
	requireTokenOrder(resolve, "resolve(id, out)", "table->visit_asset(id, type, table->visit_userdata)");

	const auto fields = functionBlock(runtime, "static s32 s_setupApplyKnownField(");
	const auto dual = fields.find("pickup.dual_weapon");
	const auto rename = fields.find("rename_object.weapon");
	REQUIRE(dual != std::string::npos);
	REQUIRE(rename != std::string::npos);
	requireTokenOrder(fields.substr(dual, rename - dual), "pickup.dual_weapon",
		"s_setupResolveAssetOperand(table, catalog_id, ASSET_WEAPON");
	requireTokenOrder(fields.substr(rename), "rename_object.weapon",
		"s_setupResolveAssetOperand(table, catalog_id, ASSET_WEAPON");
	const auto character = functionBlock(runtime, "static s32 s_setupApplyCharacterField(");
	requireTokenOrder(character, "character.body", "s_setupResolveAssetOperand(table, catalog_id, ASSET_BODY");
	requireTokenOrder(character, "head_selector", "s_setupResolveAssetOperand(table, catalog_id, ASSET_HEAD");
	REQUIRE(runtime.find("s_setupResolveAssetOperand(table, secondary_weapon_id, ASSET_WEAPON,") != std::string::npos);
	REQUIRE(runtime.find("s_setupResolveAssetOperand(table, body_id, ASSET_BODY,") != std::string::npos);
	REQUIRE(runtime.find("s_setupResolveAssetOperand(table, head_id, ASSET_HEAD,") != std::string::npos);
	const auto hum = functionBlock(runtime, "static s32 s_setupResolveHumSound(");
	requireTokenOrder(hum, "s_setupPackHumSound(entry->ext.audio.sound_id, entry->ext.audio.category, out)",
		"table->visit_asset(id, ASSET_AUDIO, table->visit_userdata)");
	requireTokenOrder(hum, "table->visit_asset(id, ASSET_AUDIO, table->visit_userdata)", "catalogResolveAudio(id, &audio)");
	const auto pack = functionBlock(runtime, "static s32 s_setupPackHumSound(");
	REQUIRE(pack.find("category != AUDIO_CAT_SFX && !(category == AUDIO_CAT_VOICE && ref.hasconfig)") != std::string::npos);
}

TEST_CASE("Grid setup asset review follows the native graph's selected setup and AI tables",
	"[modding][pdxxx][scenario][grid][setup][static]")
{
	const auto runtime = readTextFile("port/src/scenario_source_runtime.c");
	const auto matcher = functionBlock(runtime, "s32 scenarioSourceSetupAssetMembersMatch(");
	for (const char *key : {"{ \"setup_fields\", setup_member }", "{ \"objects\", objects_member }",
			"{ \"ai_lists\", ai_member }"}) {
		REQUIRE(matcher.find(key) != std::string::npos);
	}
	requireTokenOrder(matcher, "s_readLevelGraphTableMember(text, expected[i].key, member)",
		"strcmp(member, expected[i].member) == 0");
	REQUIRE(matcher.find("fsFileLoad") == std::string::npos);
	const auto binder = functionBlock(runtime, "static s32 s_bindLevelGraphTablePath(");
	REQUIRE(binder.find("s_readLevelGraphTableMember(graph_text, key, member)") != std::string::npos);
}

TEST_CASE("Exact Grid preparation validates the native room bounds before disposing collision",
	"[modding][pdxxx][scenario][grid][static]")
{
	const auto runtime = readTextFile("port/src/grid_match_runtime.c");
	const auto scenario = readTextFile("port/src/scenario_source_runtime.c");
	const auto probe = functionBlock(runtime, "static s32 runtimeProbeOriginalScenario(");
	requireTokenOrder(probe, "scenarioSourcePreflightPortals(", "runtimePreflightRoomTable(");
	requireTokenOrder(probe, "runtimePreflightRoomTable(", "scenarioSceneRendererProbeGlbBytes(");
	requireTokenOrder(probe, "scenarioSceneRendererProbeGlbBytes(", "scenarioSourcePreflightNavigation(");
	const auto release = probe.rfind("meshFree(&collision)");
	REQUIRE(release != std::string::npos);
	REQUIRE(probe.find("scenarioSourcePreflightNavigation(") < release);

	const auto bind = functionBlock(runtime, "static s32 runtimePreflightRoomTable(");
	requireTokenOrder(bind, "scenarioSourceRoomTablePath(", "runtimeReadScenarioMember(source, path, &size, &absent)");
	requireTokenOrder(bind, "text || absent", "scenarioSourcePreflightRoomTable(text, size, room_count, collision,");
	REQUIRE(bind.find("fsFileLoad") == std::string::npos);

	const auto path = functionBlock(scenario, "s32 scenarioSourceRoomTablePath(");
	REQUIRE(path.find("s_scenarioMemberPath(scenario, \"rooms.json\", out, out_cap)") != std::string::npos);
	const auto live_bounds_start = scenario.find("s32 scenarioSourceApplyRoomTableForStage(");
	const auto live_origins_start = scenario.find("static struct coord *s_loadRoomOrigins(");
	const auto live_origins_end = scenario.find("static s32 s_jsonReadNestedIntArrayInRange(", live_origins_start);
	REQUIRE(live_bounds_start != std::string::npos);
	REQUIRE(live_origins_start != std::string::npos);
	REQUIRE(live_origins_end != std::string::npos);
	const auto live_bounds = scenario.substr(live_bounds_start);
	const auto live_origins = scenario.substr(live_origins_start, live_origins_end - live_origins_start);
	for (const auto *reader : {&live_bounds, &live_origins}) {
		REQUIRE(reader->find("scenarioSourceRoomTablePath(scenario, bounds_path, sizeof(bounds_path))") != std::string::npos);
		REQUIRE(reader->find("s_roomJsonArrayEnd(rows_open, text_end)") != std::string::npos);
		REQUIRE(reader->find("s_jsonReadFloatArray3Value(") != std::string::npos);
	}
	const auto candidate = functionBlock(scenario, "s32 scenarioSourcePreflightRoomTable(");
	requireTokenOrder(candidate, "s_roomCandidateCollision(", "if (text)");
	requireTokenOrder(candidate, "modAssetJsonValidateObject(text, size)", "s_roomCandidateRows(");
	requireTokenOrder(candidate, "boxes[roomnum].seen || (has_bounds && !scenarioRoomBoundsValid(&bounds, 1))",
		"boxes[roomnum].seen = 1;");
	requireTokenOrder(candidate, "boxes[roomnum].seen = 1;", "if (has_bounds)");
	requireTokenOrder(candidate, "if (has_bounds)", "scenarioRoomBoundsContain(&bounds, &boxes[roomnum].collision)");
	requireTokenOrder(candidate, "scenarioRoomBoundsContain(&bounds, &boxes[roomnum].collision)", "authored++;");
	REQUIRE(candidate.find("s_StageRoomRules") == std::string::npos);
	REQUIRE(candidate.find("mempAlloc") == std::string::npos);
	REQUIRE(scenario.find("scenarioRoomBoundsValid(&boxes[roomnum].collision, 0)") != std::string::npos);
	const auto rows = functionBlock(scenario, "static char *s_roomCandidateRows(");
	requireTokenOrder(rows, "strstr(document, \"pd2.scenario.rooms.v1\")", "if (!schema) return rows;");
	requireTokenOrder(rows, "if (!schema) return rows;", "modAssetJsonStringEquals(");
	const auto row_start = scenario.find("static s32 s_roomCandidateReadRow(");
	const auto row_end = scenario.find("s32 scenarioSourcePreflightRoomTable(", row_start);
	REQUIRE(row_start != std::string::npos);
	REQUIRE(row_end != std::string::npos);
	const auto row = scenario.substr(row_start, row_end - row_start);
	requireTokenOrder(row, "(low != NULL) != (high != NULL)", "modAssetJsonParseS32Token(");
	requireTokenOrder(row, "s_jsonReadFloatArray3At(origin_value, end, origin)", "!scenarioRoomOriginValid(origin)");
	requireTokenOrder(row, "!scenarioRoomOriginValid(origin)", "return !*has_bounds ||");
}

TEST_CASE("Staged Grid variants preflight their pinned native package before Mod enable",
	"[modding][pdxxx][scenario][grid][static]")
{
	const auto scanner = readTextFile("port/src/assetcatalog_scanner.c");
	const auto runtime = readTextFile("port/src/grid_match_runtime.c");
	const auto editor = readTextFile("port/src/net/grid_edit_room_net.c");
	const auto detached = functionBlock(scanner, "s32 assetCatalogBindGridScenarioDescriptor(");
	const auto attached = functionBlock(scanner, "case ASSET_SCENARIO: {");
	const auto fields = functionBlock(scanner, "static const char *s_bindScenarioSourceFields(");
	REQUIRE(attached.find("s_bindScenarioSourceFields(e, ini)") != std::string::npos);
	REQUIRE(attached.find("s_mintCustomStagenum(") != std::string::npos);
	REQUIRE(attached.find("catalogSetPrimaryFile(") != std::string::npos);
	requireTokenOrder(detached, "gridLeaseParseDescriptor(", "qualifyTypedArchiveSourcePaths(ini, scenario_ref)");
	requireTokenOrder(detached, "qualifyTypedArchiveSourcePaths(ini, scenario_ref)", "s_bindScenarioSourceFields(out, ini)");
	for (const char *call : {"assetCatalogRegister(", "assetCatalogGetMutable(", "catalogSetPrimaryFile(",
			"s_mintCustomStagenum(", "fileProviderHandle("}) {
		REQUIRE(detached.find(call) == std::string::npos);
		REQUIRE(fields.find(call) == std::string::npos);
	}

	const auto member = functionBlock(runtime, "static void *runtimeReadMemberChain(");
	requireTokenOrder(member, "bound.matches != 1 || !bound.size || bound.size > max_member_size",
		"modArchiveExtractMemAlloc(");
	const auto preflight = functionBlock(runtime, "static s32 runtimePreflightSavedArena(");
	requireTokenOrder(preflight, "sha256Hash(bytes, size, digest)", "strcmp(sha, expected_sha)");
	requireTokenOrder(preflight, "strcmp(sha, expected_sha)", "runtimeReadMemberChain(bytes, size, \"arena.ini\"");
	requireTokenOrder(preflight, "runtimeReadMemberChain(bytes, size, \"arena.ini\"", "gridArchiveOrigin(");
	requireTokenOrder(preflight, "gridMatchPackageLocation(", "strcmp(root, locator)");
	requireTokenOrder(preflight, "strcmp(chain, arena_source.scenario_member)", "runtimeReadMemberChain(bytes, size, chain");
	requireTokenOrder(preflight, "runtimeReadMemberChain(native_scenario, scenario_size, \"scenario.ini\"",
		"assetCatalogBindGridScenarioDescriptor(");
	requireTokenOrder(preflight, "assetCatalogBindGridScenarioDescriptor(", "runtimeProbeOriginalScenario(");
	for (const char *call : {"assetCatalogResolve(", "modAuthorRuntimeEnable(", "fsFileLoad(", "s_PendingMatch ="}) {
		REQUIRE(preflight.find(call) == std::string::npos);
	}

	const auto check_file = functionBlock(editor, "static s32 editPreflightVariantFile(");
	requireTokenOrder(check_file, "gridMatchPackageLoad(path, &size)",
		"gridMatchRuntimePreflightRoomExtension(path, bytes, size, arena_id, expected_sha,");
	requireTokenOrder(check_file, "gridMatchRuntimePreflightRoomExtension(", "free(bytes)");
	const auto stage = functionBlock(editor, "static s32 editStageVariant(");
	requireTokenOrder(stage, "editPreflightVariantFile(candidate.path, candidate.arena_id, NULL, candidate.digest)",
		"forgeRevisionReadParent(");
	requireTokenOrder(stage, "forgeRevisionReadParent(", "forgeRevisionPublishLocal(");
	requireTokenOrder(stage, "forgeRevisionPublishLocal(", "s_EditStagedVariant = staged;");
	const auto launch = functionBlock(editor, "static s32 editEnableStagedVariant(");
	requireTokenOrder(launch, "if (!editPreflightVariantFile(s_EditStagedVariant.archive_path, arena_id, sha, NULL)) return 0;",
		"modAuthorRuntimeEnable(");
	requireTokenOrder(launch, "modAuthorRuntimeEnable(", "gridMatchPackageLocation(");
	requireTokenOrder(launch, "gridMatchPackageLocation(", "editStagedVariantMatchesPin(root, sha)");
}

TEST_CASE("Offline Level File publishes a detached checked room fork and keeps the draft open",
	"[modding][pdxxx][scenario][grid][static]")
{
	const auto io = readTextFile("port/src/forge_level_io.c");
	const auto editor = readTextFile("port/fast3d/pdgui_forge_editor.cpp");
	const auto ui = functionBlock(editor, "static void forgeDrawLevelFileSection(void)");
	requireTokenOrder(ui, "forgeLevelSessionCanSaveExtendedVariant()", "ImGui::Button(\"Save Extended Variant\")");
	requireTokenOrder(ui, "ImGui::Button(\"Save Extended Variant\")", "forgeLevelSessionSaveExtendedVariant(");
	REQUIRE(ui.find("enable it in Mods, then open it from Levels") != std::string::npos);

	const auto can_save = functionBlock(io, "s32 forgeLevelSessionCanSaveExtendedVariant(");
	REQUIRE(can_save.find("g_NetMode == NETMODE_NONE") != std::string::npos);
	REQUIRE(can_save.find("s_Draft.phase == FORGE_DRAFT_NONE") != std::string::npos);
	const auto save = functionBlock(io, "s32 forgeLevelSessionSaveExtendedVariant(");
	requireTokenOrder(save, "levelSourceStillMatches(source, err, err_cap)",
		"forgeRevisionRead(directory, source->arena_id, source->archive_sha256,");
	requireTokenOrder(save, "forgeRevisionRead(directory, source->arena_id, source->archive_sha256,",
		"forgeLevelValidateOriginalDraft(s_Session.doc, source, NULL");
	requireTokenOrder(save, "forgeLevelValidateOriginalDraft(s_Session.doc, source, NULL",
		"levelForkRevisionWithChecks(&revision, name, s_Session.doc, room, extra_length, 1,");
	const auto fork = functionBlock(io, "static s32 levelForkRevisionWithChecks(");
	requireTokenOrder(fork, "modAuthorPlan(&request", "levelAllOverrides(doc, &source");
	requireTokenOrder(fork, "levelAllOverrides(doc, &source", "levelBuildRoomExtension(&source");
	requireTokenOrder(fork, "levelBuildRoomExtension(&source", "levelSaveCheckedRoomFork(&request");

	const auto preflight = functionBlock(io, "static s32 levelPreflightRoomFork(");
	requireTokenOrder(preflight, "gridMatchPackageLoad(fork->archive_path", "sha256Hash(bytes, size, digest)");
	requireTokenOrder(preflight, "sha256Hash(bytes, size, digest)", "gridMatchRuntimePreflightRoomExtension(");
	const auto publish = functionBlock(io, "static s32 levelSaveCheckedRoomFork(");
	requireTokenOrder(publish, "_mkdir(staging)", "modAuthorSave(&draft, candidate");
	requireTokenOrder(publish, "modAuthorSave(&draft, candidate", "levelPreflightRoomFork(candidate");
	requireTokenOrder(publish, "levelPreflightRoomFork(candidate", "MoveFileExA(candidate->mod_dir, fork->mod_dir, MOVEFILE_WRITE_THROUGH)");
	for (const char *mutation : {"forgeLevelSessionSaveAs(", "forgeLevelSessionClose(", "forgeLevelSessionEditBegin(",
			"modAuthorRuntimeEnable(", "modmgrRescanDirectory(", "mainChangeStage(", "MOVEFILE_REPLACE_EXISTING"}) {
		REQUIRE(save.find(mutation) == std::string::npos);
		REQUIRE(publish.find(mutation) == std::string::npos);
	}
}

TEST_CASE("Ordinary original Grid save validates the same-ID draft before publication",
	"[modding][pdxxx][scenario][grid][static]")
{
	const auto io = readTextFile("port/src/forge_level_io.c");
	const auto runtime = readTextFile("port/src/grid_match_runtime.c");
	const auto header = readTextFile("port/include/grid_match_runtime.h");
	const auto save = functionBlock(io, "static s32 levelSaveAs(");
	requireTokenOrder(save, "strcmp(descriptor_values[0].value, GRID_ARENA_PROVENANCE_ORIGINAL) == 0",
		"code = levelSaveExisting(doc, source, target->display_name, original, target, require_direct");
	requireTokenOrder(save, "original && !forgeLevelValidateOriginalDraft(doc, source, NULL",
		": modAuthorRuntimeSave(&request, target, &restarts, err, err_cap)");
	const auto originalSave = functionBlock(io, "static s32 levelSaveExisting(");
	const char *const validate = "levelBuildValidatedOriginalDraft(doc, source, &size, NULL,";
	requireTokenOrder(originalSave, "levelInPlaceOwner(target, source, name, scenario_id, 1, require_resident)", validate);
	requireTokenOrder(originalSave, validate, "code = levelSaveInPlaceArchive(");
	const auto session = functionBlock(io, "static s32 levelSessionSaveAs(");
	requireTokenOrder(session, "s32 status = levelSaveAs(", "if (status == MOD_AUTHOR_OK)");
	requireTokenOrder(session, "if (status == MOD_AUTHOR_OK)", "s_Session.saved_revision = s_Session.revision;");

	const auto draft = functionBlock(io, "static void *levelBuildValidatedOriginalDraft(");
	requireTokenOrder(draft, "levelBuildPrivateDraftArchive(doc, source,", "gridMatchRuntimePreflightLocalArena(path, archive, archive_size, source->arena_id, sha,");
	requireTokenOrder(draft, "gridMatchRuntimePreflightLocalArena(", "gridValidateDraftSources(scenario, scenario_size, source->scenario_member,");
	const auto validateDraft = functionBlock(io, "s32 forgeLevelValidateOriginalDraft(");
	REQUIRE(validateDraft.find("levelBuildValidatedOriginalDraft(doc, source,") != std::string::npos);
	REQUIRE(header.find("s32 gridMatchRuntimePreflightLocalArena(") != std::string::npos);
	const auto local = functionBlock(runtime, "s32 gridMatchRuntimePreflightLocalArena(");
	requireTokenOrder(local, "runtimePreflightSavedArena(locator, bytes, size, arena_id, expected_sha, 0,",
		"128u * 1024u * 1024u");
	const auto network = functionBlock(runtime, "s32 gridMatchRuntimePreflightSavedArena(");
	REQUIRE(network.find("expected_sha, 0, NET_DISTRIB_MAX_COMP") != std::string::npos);
	const auto extension = functionBlock(runtime, "s32 gridMatchRuntimePreflightRoomExtension(");
	REQUIRE(extension.find("expected_sha, 1, NET_DISTRIB_MAX_COMP") != std::string::npos);
	const auto attached = functionBlock(runtime, "static s32 runtimePreflightOriginalScenario(");
	REQUIRE(attached.find(".max_bytes = NET_DISTRIB_MAX_COMP") != std::string::npos);
	const auto detached = functionBlock(runtime, "static s32 runtimePreflightSavedArena(");
	REQUIRE(detached.find("size > max_bytes") != std::string::npos);
	REQUIRE(detached.find("runtimeReadMemberChain(bytes, size, chain, max_bytes,") != std::string::npos);
	REQUIRE(detached.find(".max_bytes = max_bytes") != std::string::npos);
	const auto sources = functionBlock(runtime, "static void *runtimeReadScenarioMember(");
	REQUIRE(sources.find("source->max_bytes, out_size, out_absent") != std::string::npos);
	const auto collision = functionBlock(runtime, "static s32 runtimePreflightCollision(");
	REQUIRE(collision.find("flags, flags_size, source->max_bytes, collision") != std::string::npos);
}

TEST_CASE("Original in-place Save As retains the exact predecessor and can fork it",
	"[modding][pdxxx][scenario][grid][static]")
{
	const auto io = readTextFile("port/src/forge_level_io.c");
	const auto editor = readTextFile("port/fast3d/pdgui_forge_editor.cpp");
	const auto save = functionBlock(io, "static s32 levelSaveAs(");
	requireTokenOrder(save, "modAuthorPlan(&request, target", "const s32 repeat = !own && target->updated && last_saved");
	requireTokenOrder(save, "modsRootsSameDir(last_saved->path, target->archive_path)",
		"if (!own && !repeat && (target->updated || assetCatalogResolveAny(target->catalog_id)");
	requireTokenOrder(save, "request.require_new_folder = 1;",
		"repeat ? levelSaveLastDestination(&request, source, last_saved, target");
	const auto repeat = functionBlock(io, "static s32 levelSaveLastDestination(");
	requireTokenOrder(repeat, "modsRootsSameDir(saved->path, bound.path)", "levelForkSource(&bound, &destination");
	requireTokenOrder(repeat, "levelInPlaceOwner(target, &destination, target->display_name, scenario_id, 1, 0)",
		"draft.mods_root = staging");
	requireTokenOrder(repeat, "modAuthorSave(&draft, candidate", "levelSaveInPlaceArchive(target, &destination");
	REQUIRE(repeat.find("modAuthorRuntimeSave(") == std::string::npos);
	REQUIRE(repeat.find("modAuthorRuntimeEnable(") == std::string::npos);
	const auto session = functionBlock(io, "static s32 levelSessionSaveAs(");
	requireTokenOrder(session, "levelSaveAs(s_Session.doc, &s_Session.source, display_name, require_direct,",
		"&s_Session.saved_archive, NULL, NULL, saved, err, err_cap)");
	const auto retain = functionBlock(io, "static s32 levelRememberPreviousSave(");
	REQUIRE(retain.find("forgeRevisionRememberPrevious(directory, source->arena_id, target->archive_path,") != std::string::npos);
	REQUIRE(retain.find("source->archive_sha256, &previous, err, err_cap)") != std::string::npos);
	REQUIRE(retain.find("forgeRevisionPublishLocal(") == std::string::npos);

	const auto fork = functionBlock(io, "s32 forgeLevelSessionForkPreviousRevision(");
	REQUIRE(fork.find("g_NetMode != NETMODE_NONE") != std::string::npos);
	requireTokenOrder(fork, "forgeRevisionReadPrevious(directory, s_Session.source.arena_id, &previous",
		"levelForkRevision(&previous, name, NULL, 0, 0, fork");
	const auto controls = functionBlock(editor, "static void forgeDrawLevelFileSection(void)");
	requireTokenOrder(controls, "ImGui::Button(\"Fork Previous Save\")", "forkRevision = &forgeLevelSessionForkPreviousRevision");
	requireTokenOrder(controls, "forkRevision = &forgeLevelSessionForkPreviousRevision",
		"forkRevision(s_LevelFileName, fork, err, sizeof(err))");
}

TEST_CASE("Canonical Grid originals replace only the validated loose archive",
	"[modding][pdxxx][scenario][grid][static]")
{
	const auto io = readTextFile("port/src/forge_level_io.c");
	const auto owner = functionBlock(io, "static s32 levelInPlaceOwner(");
	REQUIRE(owner.find("modmgrFindMod(target->mod_id)") != std::string::npos);
	REQUIRE(owner.find("modsRootsSameDir(mod->dirpath, target->mod_dir)") != std::string::npos);
	REQUIRE(owner.find("->category") == std::string::npos);
	REQUIRE(owner.find("strcmp(mod->name, name)") != std::string::npos);
	REQUIRE(owner.find("strcmp(arena->ext.arena.display_name, name)") != std::string::npos);
	REQUIRE(owner.find("modsRootsSameDir(opened, target->archive_path)") != std::string::npos);
	REQUIRE(owner.find("strcmp(expected, scenario->descriptor_path)") != std::string::npos);
	REQUIRE(owner.find("fileProviderPath(catalogEffectiveHandle(arena))") != std::string::npos);
	REQUIRE(owner.find("fileProviderPath(catalogEffectiveHandle(scenario))") != std::string::npos);
	const auto save = functionBlock(io, "static s32 levelSaveExisting(");
	requireTokenOrder(save, "target->ids.archive_count == 2 && target->ids.id_count == 2", "code = levelSaveInPlaceArchive(");
	REQUIRE(save.find("archive, size, catalog_generation, stage_index, require_resident, err, err_cap)") != std::string::npos);
	REQUIRE(save.find("modAuthorRuntimeSave(") == std::string::npos);
	const auto publish = functionBlock(io, "static s32 levelSaveInPlaceArchive(");
	requireTokenOrder(publish, "levelCheckArchiveDigest(prior_digest, source->archive_sha256", "memcmp(before, after, sizeof(before)) == 0");
	requireTokenOrder(publish, "memcmp(before, after, sizeof(before)) == 0", "saveAtomicBegin(&write, target->archive_path)");
	requireTokenOrder(publish, "fwrite(archive, 1, size, saveAtomicStream(&write))", "assetCatalogGetGeneration() != catalog_generation");
	requireTokenOrder(publish, "memcmp(manifest_digest, mod->sha256", "levelRememberPreviousSave(target, source");
	requireTokenOrder(publish, "levelRememberPreviousSave(target, source", "levelSourceStillMatches(source, err, err_cap)");
	requireTokenOrder(publish, "levelSourceStillMatches(source, err, err_cap)", "saveAtomicCommit(&write)");
	REQUIRE(publish.find("saveAtomicAbort(&write)") != std::string::npos);
	REQUIRE(publish.find("modAuthorRuntimeSave(") == std::string::npos);
	REQUIRE(publish.find("modmgrApplyChanges") == std::string::npos);
}

TEST_CASE("Grid Edit Placement routes its input and applies one native document edit",
	"[modding][pdxxx][scenario][grid][static]")
{
	const auto editor = readTextFile("port/fast3d/pdgui_forge_editor.cpp");
	const auto sidebar = functionBlock(editor, "static void forgeSidebarActivate(");
	requireTokenOrder(sidebar, "payload == kForgeFileEditPlacement", "s_LevelFilePoseRequested = true;");
	const auto controls = functionBlock(editor, "static void forgeDrawLevelFileSection(void)");
	requireTokenOrder(controls, "s_LevelFilePoseRequested && netGetMode() == 0", "forgeDrawLevelFilePose(doc, editPlacement)");
	const auto modal = functionBlock(editor, "static void forgeDrawLevelFilePose(");
	requireTokenOrder(modal, "menupoolAcquire(MENU_TYPE_GRID_PLACEMENT", "pose.generation != forgeLevelSessionGeneration()");
	requireTokenOrder(modal, "forgeLevelLivePositionInRoom(pose.position)", "forgeLevelSessionEditBegin()");
	requireTokenOrder(modal, "memcpy(look, pose.before.look", "if (yawChanged)");
	requireTokenOrder(modal, "forgeLevelSessionEditBegin()", "forgeLevelDocMoveObject(doc, pose.id");
	requireTokenOrder(modal, "forgeLevelDocMoveSpawn(doc, pose.id", "forgeLevelSessionEditEnd(changed)");
	requireTokenOrder(modal, "if (close) ImGui::CloseCurrentPopup();", "if (close) menupoolRelease(MENU_TYPE_GRID_PLACEMENT);");
	const auto controller = functionBlock(editor, "static void forgeSidebarHandleInput(");
	REQUIRE(controller.find("if (menupoolIsActive(MENU_TYPE_GRID_PLACEMENT)) return;") != std::string::npos);
	const auto pool = functionBlock(readTextFile("port/src/menupool.c"), "static s32 menupoolLegacyStackOptional(");
	requireTokenOrder(pool, "case MENU_TYPE_GRID_PLACEMENT:", "return 1;");
}

TEST_CASE("Save Opened Level cannot publish to another arena or fall back to Mod Author",
	"[modding][pdxxx][scenario][grid][static]")
{
	const auto io = readTextFile("port/src/forge_level_io.c");
	const auto editor = readTextFile("port/fast3d/pdgui_forge_editor.cpp");
	const auto target = functionBlock(io, "const char *forgeLevelSessionSaveTarget(");
	requireTokenOrder(target, "strcmp(saved->arena_id, source->arena_id)", "modAuthorNames(&request");
	requireTokenOrder(target, "strcmp(saved->digest, source->archive_sha256)", "modAuthorNames(&request");
	requireTokenOrder(target, "!modsRootsSameDir(saved->path, source->archive_path)", "modAuthorNames(&request");
	requireTokenOrder(target, "strcmp(target.catalog_id, source->arena_id)", "levelInPlaceOwner(&target, source, name, NULL, 0, 1)");

	const auto entry = functionBlock(io, "s32 forgeLevelSessionSaveOpened(");
	requireTokenOrder(entry, "assetPathCopyChecked(name", "levelSessionSaveAs(name, NULL, 1");
	const auto common = functionBlock(io, "static s32 levelSaveAs(");
	requireTokenOrder(common, "require_direct && (!original || !own || packed)",
		"levelSaveExisting(doc, source, target->display_name, original, target, require_direct");
	const auto original = functionBlock(io, "static s32 levelSaveExisting(");
	requireTokenOrder(original, "!levelInPlaceOwner(target, source, name, scenario_id, 1, require_resident)",
		"levelBuildValidatedOriginalDraft(");
	REQUIRE(original.find("modAuthorRuntimeEnable(") == std::string::npos);

	const auto sidebar = functionBlock(editor, "static void forgeSidebarActivate(");
	requireTokenOrder(sidebar, "payload == kForgeFileSaveOpened", "s_LevelFileSaveRequested = true;");
	const auto controls = functionBlock(editor, "static void forgeDrawLevelFileSection(void)");
	requireTokenOrder(controls, "s_LevelFileSaveRequested && netGetMode() == 0", "forgeLevelSessionSaveTarget()");
	requireTokenOrder(controls, "if (clickSaveOpened || saveOpened)", "forgeLevelSessionSaveOpened(err, sizeof(err))");
}

TEST_CASE("Grid Level File tools own controller input for trigger edits and native routes",
	"[modding][pdxxx][scenario][grid][static]")
{
	const auto editor = readTextFile("port/fast3d/pdgui_forge_editor.cpp");
	const auto sidebar = functionBlock(editor, "static void forgeSidebarActivate(");
	requireTokenOrder(sidebar, "payload == kForgeFileEditTriggers || payload == kForgeFileEditNavigation", "s_LevelFileToolRequested = payload;");
	const auto controls = functionBlock(editor, "static void forgeDrawLevelFileSection(void)");
	requireTokenOrder(controls, "const int toolRequest = netGetMode() == 0", "forgeDrawLevelFileTools(doc");
	const auto modal = functionBlock(editor, "static void forgeDrawLevelFileTools(");
	requireTokenOrder(modal, "menupoolAcquire(MENU_TYPE_GRID_LEVEL_TOOLS", "forgeDrawLevelFileTriggers(doc, start,");
	requireTokenOrder(modal, "menupoolAcquire(MENU_TYPE_GRID_LEVEL_TOOLS", "forgeDrawLevelFileWaypoints(doc, start)");
	const auto input = functionBlock(editor, "static void forgeSidebarHandleInput(");
	REQUIRE(input.find("if (menupoolIsActive(MENU_TYPE_GRID_LEVEL_TOOLS)) return;") != std::string::npos);
	const auto pool = functionBlock(readTextFile("port/src/menupool.c"), "static s32 menupoolLegacyStackOptional(");
	requireTokenOrder(pool, "case MENU_TYPE_GRID_LEVEL_TOOLS:", "return 1;");

	const auto trigger = functionBlock(editor, "static void forgeDrawLevelFileTriggers(");
	requireTokenOrder(trigger, "forgeLevelSessionEditBegin()", "forgeLevelDocUpdateTrigger(doc, selected.volume_id");
	requireTokenOrder(trigger, "forgeLevelDocUpdateTrigger(doc, selected.volume_id, pad.pad_ref",
		"forgeLevelSessionEditEnd(changed)");
	requireTokenOrder(trigger, "forgeLevelLivePositionInRoom(pad.position)",
		"forgeDrawLevelFileTriggerPreview(pad, halfSize)");
	requireTokenOrder(trigger, "if (trigger.editable)", "ImGui::SmallButton(\"Edit Trigger\")");
	const auto route = functionBlock(editor, "static void forgeDrawLevelFileWaypoints(");
	requireTokenOrder(route, "forgeLevelFilePickPad(doc, \"Waypoint pad\"", "forgeDrawLevelFileCoverMove(doc,");
	const auto cover = functionBlock(editor, "static void forgeDrawLevelFileCoverMove(");
	requireTokenOrder(cover, "forgeLevelDocGetCover(doc", "ImGui::Button(\"Move Cover to Pad\")");
	requireTokenOrder(cover, "ImGui::Button(\"Move Cover to Pad\")", "forgeLevelSessionMoveCoverToPad(s_LevelFileCover, pad->pad_ref");
	const auto cover_move = functionBlock(readTextFile("port/src/forge_level_io.c"), "s32 forgeLevelSessionMoveCoverToPad(");
	requireTokenOrder(cover_move, "forgeLevelSessionCanEditCover()", "levelRouteFloorRoom(&position, pad.room_ref)");
	requireTokenOrder(cover_move, "levelRouteFloorRoom(&position, pad.room_ref)", "forgeLevelSessionEditBegin()");
	requireTokenOrder(cover_move, "forgeLevelSessionEditBegin()", "forgeLevelDocMoveCoverToPad(s_Session.doc, cover_ref, pad_ref");
	requireTokenOrder(cover_move, "forgeLevelDocMoveCoverToPad(s_Session.doc, cover_ref, pad_ref", "forgeLevelSessionEditEnd(changed)");
	requireTokenOrder(route, "forgeLevelDocCanDisconnectPads(doc", "forgeLevelDocDisconnectPads(doc");
	const size_t disconnect = route.find("forgeLevelDocDisconnectPads(doc");
	REQUIRE(route.find("forgeLevelSessionEditEnd(changed)", disconnect) != std::string::npos);
}

TEST_CASE("Grid Room Brightness reaches the native room table for draft and saved play",
	"[modding][pdxxx][scenario][grid][static]")
{
	const auto editor = readTextFile("port/fast3d/pdgui_forge_editor.cpp");
	const auto sidebar = functionBlock(editor, "static void forgeSidebarActivate(");
	requireTokenOrder(sidebar, "payload == kForgeFileEditRoomBrightness", "s_LevelFileToolRequested = payload;");
	const auto tools = functionBlock(editor, "static void forgeDrawLevelFileTools(");
	requireTokenOrder(tools, "menupoolAcquire(MENU_TYPE_GRID_LEVEL_TOOLS", "forgeDrawLevelFileRoomBrightness(doc)");
	const auto bright = functionBlock(editor, "static void forgeDrawLevelFileRoomBrightness(");
	requireTokenOrder(bright, "forgeLevelSessionCanEditRoomBrightness()", "forgeLevelSessionEditBegin()");
	requireTokenOrder(bright, "forgeLevelSessionEditBegin()", "forgeLevelDocSetRoomBrightness(doc, room.roomnum");
	requireTokenOrder(bright, "forgeLevelDocSetRoomBrightness(doc, room.roomnum", "forgeLevelSessionEditEnd(changed)");

	const auto io = readTextFile("port/src/forge_level_io.c");
	const auto can_edit = functionBlock(io, "s32 forgeLevelSessionCanEditRoomBrightness(");
	REQUIRE(can_edit.find("levelScenarioUsesRoomTable(scenario, arena->ext.arena.scenario_archive)") != std::string::npos);
	const auto room_path = functionBlock(io, "static s32 levelScenarioUsesRoomTable(");
	requireTokenOrder(room_path, "scenarioSourceRoomTablePath(", "strcmp(actual, expected) == 0");
	const auto draft = functionBlock(io, "static void *levelBuildPrivateDraftArchive(");
	requireTokenOrder(draft, "levelAllOverrides(doc, source, input, overrides", "assetArchiveRebuildFile(input, path");
	const auto fork = functionBlock(io, "static forge_room_extension_t *levelBuildRoomExtension(");
	requireTokenOrder(fork, "strcmp(sha, source->archive_sha256) == 0", "forgeRoomCreateExtensionWithRoomBrightness(");
	const auto scenario_runtime = readTextFile("port/src/scenario_source_runtime.c");
	const auto apply_start = scenario_runtime.find("s32 scenarioSourceApplyRoomTableForStage(");
	REQUIRE(apply_start != std::string::npos);
	const auto apply = scenario_runtime.substr(apply_start);
	requireTokenOrder(apply, "scenarioSourceRoomTablePath(scenario,", "br_light_min = (u8)brightness[0]");
	requireTokenOrder(apply, "br_light_min = (u8)brightness[0]", "br_light_max = (u8)brightness[1]");
	const auto consume = functionBlock(readTextFile("src/game/dlights.c"), "void roomInitLights(");
	REQUIRE(consume.find("bgGetRoomBrightnessRange(roomnum, &room->br_light_min, &room->br_light_max)") != std::string::npos);
	const auto bg = readTextFile("src/game/bg.c");
	const char *const scene = "static void bgSetScenarioSceneRoomLights(void)";
	const auto scene_start = bg.rfind(scene);
	REQUIRE(scene_start != std::string::npos);
	const auto render = functionBlock(bg.substr(scene_start), scene);
	requireTokenOrder(render, "scenarioSourceRoomHasBrightnessRange(i)", "scenarioSceneRendererSetRoomLights(lights, count)");
	REQUIRE(render.find("g_Rooms[i].br_base") != std::string::npos);
}

TEST_CASE("Exact original Grid prepare checks and packs native navigation before pending publication",
	"[modding][pdxxx][scenario][grid][navigation][static]")
{
	const auto runtime = readTextFile("port/src/grid_match_runtime.c");
	const auto scenario = readTextFile("port/src/scenario_source_runtime.c");
	const auto reader = functionBlock(runtime, "static char *runtimeReadNavigationMember(");
	REQUIRE(reader.find("runtimeReadScenarioMember(ctx, path, &length, NULL)") != std::string::npos);
	REQUIRE(reader.find("fsFileLoad") == std::string::npos);
	const auto original = functionBlock(runtime, "static s32 runtimeProbeOriginalScenario(");
	requireTokenOrder(original, "scenarioSourcePreflightPortals(", "scenarioSourcePreflightNavigation(");
	REQUIRE(original.find("room_count, source->max_bytes, room_extension ? &collision : NULL") != std::string::npos);
	REQUIRE(original.find("scenarioSourcePreflightNavigation(") < original.rfind("meshFree(&collision)"));
	const auto exact_preflight = functionBlock(runtime, "static s32 runtimePreflightOriginalScenario(");
	requireTokenOrder(exact_preflight, "netGridEditPrivateLaunchMatches(match->identity.offer.room_id, match->identity.arena_id)", "runtimeReadMemberChain(arena, size, scenario_chain,");
	REQUIRE(exact_preflight.find("valid = runtimeProbeOriginalScenario(") != std::string::npos);
	requireTokenOrder(exact_preflight, "(void)scenarioSourcePreflightNavigation(scenario, runtimeReadNavigationMember, &source,", "&match->scenario_probe.navigation_probe, NULL, 0)");
	const auto prepare = functionBlock(runtime, "s32 gridMatchRuntimePrepare(");
	requireTokenOrder(prepare, "runtimePreflightOriginalScenario(", "s_PendingMatch = match;");

	const auto detached = functionBlock(scenario, "s32 scenarioSourcePreflightNavigation(");
	REQUIRE(detached.find("s_bindNavigationGenerateSource(") != std::string::npos);
	REQUIRE(detached.find("s_parsePadsJson(") != std::string::npos);
	REQUIRE(detached.find("s_loadNavigationTablesWithSources(") != std::string::npos);
	REQUIRE(detached.find("s_checkCandidateNavigation(") != std::string::npos);
	requireTokenOrder(detached, "s_readNavigationRows(spawns_path,", "s_loadSpawnSourceRows(text, &spawns)");
	requireTokenOrder(detached, "padnum >= summary.pads", "summary.spawns = spawns.count;");
	requireTokenOrder(detached, "if (room_extension_collision)", "s_checkRoomExtensionWalk(");
	requireTokenOrder(detached, "s_checkRoomExtensionWalk(", "s_buildPadfileForOwner(");
	const auto walk = functionBlock(scenario, "static s32 s_checkRoomExtensionWalk(");
	requireTokenOrder(walk, "s_roomExtensionPortal(portal, from, to)", "s_checkRoomExtensionLink(mesh, from");
	REQUIRE(walk.find("s_roomExtensionVisitsAll(nav, SCENARIO_SEGMENT_FLAG_INWARD)") != std::string::npos);
	REQUIRE(walk.find("s_roomExtensionVisitsAll(nav, SCENARIO_SEGMENT_FLAG_OUTWARD)") != std::string::npos);
	REQUIRE(walk.find("if (!waypoint->neighbours.count)") != std::string::npos);
	REQUIRE(walk.find("s_roomExtensionStartConnects(mesh, pads, nav, spawns->rows[i].padnum)") != std::string::npos);
	requireTokenOrder(detached, "s_buildPadfileForOwner(", "sha256Hash(packed,");
	REQUIRE(detached.find("s_ActiveScenarioGraphs") == std::string::npos);
	REQUIRE(detached.find("mempAlloc") == std::string::npos);
	const auto packed = functionBlock(scenario, "static u8 *s_buildPadfileForOwner(");
	REQUIRE(packed.find("native_budget ? malloc(total_size) : mempAlloc(") != std::string::npos);
	REQUIRE(packed.find("g_SourceWidePad") == std::string::npos);
	REQUIRE(detached.find("native_budget, &summary.bytes, NULL)") != std::string::npos);
	const auto refs = functionBlock(scenario, "static s32 s_checkCandidateNavigation(");
	REQUIRE(refs.find("pad->room < -1 || pad->room > 511 || pad->room >= room_count") != std::string::npos);
	REQUIRE(refs.find("row->padnum >= pad_count") != std::string::npos);
	REQUIRE(refs.find("row->groupnum >= nav->waygroup_count") != std::string::npos);
	REQUIRE(refs.find("const u32 waypoint = row->waypoints.values[j];") != std::string::npos);
	requireTokenOrder(refs, "waypoint >= (u32)nav->waypoint_count", "grouped[waypoint]");
	REQUIRE(refs.find("nav->waypoints[waypoint].groupnum != i") != std::string::npos);
	REQUIRE(refs.find("s_checkNavigationSegments(&row->neighbours, nav->waypoint_count)") != std::string::npos);
	REQUIRE(refs.find("s_checkNavigationSegments(&row->neighbours, nav->waygroup_count)") != std::string::npos);
	const auto tables = functionBlock(scenario, "static s32 s_loadNavigationTablesWithSources(");
	REQUIRE(tables.find("row->pads[j] < pad_count") != std::string::npos);
	REQUIRE(tables.find("s_generateNavigationTablesFromPads(") != std::string::npos);
	requireTokenOrder(tables, "row->pads[j] < pad_count", "s_protectCandidateNavigationPad(probe, row->pads[j])");
	requireTokenOrder(detached, "s_checkCandidateNavigation(", "s_protectCandidateNavigationPad(&summary, row->padnum)");
	REQUIRE(detached.find("if (!nav.generated_waypoints)") != std::string::npos);
	REQUIRE(detached.find("if (row->neighbours.count) s_protectCandidateNavigationPad(&summary, row->padnum)") != std::string::npos);
	REQUIRE(detached.find("s_protectCandidateNavigationPad(&summary, nav.waypoints[target].padnum)") != std::string::npos);
	requireTokenOrder(detached, "padnum >= summary.pads", "s_protectCandidateNavigationPad(&summary, padnum)");
	const auto eligible = functionBlock(runtime, "s32 gridMatchRuntimeDecorationPadIndependent(");
	requireTokenOrder(eligible, "pad < nav->pads", "nav->protected_pads[pad / 8]");
	const auto composition = functionBlock(runtime, "static s32 runtimeGridLayoutDecorationsFit(");
	REQUIRE(composition.find("runtimeGridLayoutDecoration(match, proof->pose.record_id)") != std::string::npos);
	const auto layout_decoration = functionBlock(runtime, "static struct defaultobj *runtimeGridLayoutDecoration(");
	REQUIRE(layout_decoration.find("gridMatchRuntimeDecorationPadIndependent(obj->pad)") != std::string::npos);
	const auto commit = functionBlock(runtime, "s32 gridMatchRuntimeCommitPrivateLayout(");
	requireTokenOrder(commit, "runtimeGridLayoutDecorationsFit(layout, match,", "meshWorldSwapPrepared(");
	const auto grid_room = readTextFile("port/src/net/grid_edit_room_net.c");
	const auto live_decoration = functionBlock(grid_room, "static struct defaultobj *editPlaytestDecoration(");
	REQUIRE(live_decoration.find("gridMatchRuntimeDecorationPadIndependent(obj->pad)") != std::string::npos);
}

TEST_CASE("Grid layout Apply accepts the current private ambient state and preserves other stage rules",
	"[modding][pdxxx][scenario][grid][layout][static]")
{
	const auto grid_room = readTextFile("port/src/net/grid_edit_room_net.c");
	const auto state = functionBlock(grid_room, "s32 netGridEditPrivatePlaytestAmbientState(");
	requireTokenOrder(state, "netGridEditPrivatePlaytestRoom(g_NetMatchRoomId)", "*enabled = s_EditPlaytest.ambient_enabled");
	requireTokenOrder(state, "s_EditPlaytest.phase != EDIT_PLAYTEST_ACTIVE || !s_EditPlaytest.displayed_revision",
		"*enabled = s_EditPlaytest.ambient_enabled");
	requireTokenOrder(state, "s_EditPlaytest.stage_epoch != g_NetStageEpoch", "*brightness = s_EditPlaytest.brightness");
	const auto enabled = functionBlock(grid_room, "s32 netGridEditPrivatePlaytestAmbient(");
	requireTokenOrder(enabled, "netGridEditPrivatePlaytestAmbientState(&enabled, &brightness) || !enabled", "*out = brightness");

	const auto runtime = readTextFile("port/src/grid_match_runtime.c");
	const auto rules = functionBlock(runtime, "static s32 runtimeGridLayoutStageRulesMatch(");
	REQUIRE(rules.find("SCENARIO_STAGE_RULES_SCHEMA") != std::string::npos);
	REQUIRE(rules.find("scenarioStageRulesParse(old_absent ? defaults : old_text") != std::string::npos);
	REQUIRE(rules.find("scenarioStageRulesParse(new_absent ? defaults : new_text") != std::string::npos);
	requireTokenOrder(rules, "netGridEditPrivatePlaytestAmbientState(&live_enabled, &live_ambient)",
		"new_rules.atmosphere.has_ambient == live_enabled");
	REQUIRE(rules.find("!live_enabled || new_rules.atmosphere.ambient_light == live_ambient") != std::string::npos);
	requireTokenOrder(rules, "runtimeGridLayoutStripAmbient(&old_rules.atmosphere)", "memcmp(&old_rules, &new_rules, sizeof(old_rules))");
	requireTokenOrder(rules, "runtimeGridLayoutStripAmbient(&new_rules.atmosphere)", "memcmp(&old_rules, &new_rules, sizeof(old_rules))");
	const auto strip = functionBlock(runtime, "static void runtimeGridLayoutStripAmbient(");
	REQUIRE(strip.find("atmosphere->has_sky || atmosphere->has_fog || atmosphere->primary_music[0]") != std::string::npos);

	const auto bg = readTextFile("src/game/bg.c");
	const auto lights = functionBlock(bg, "static void bgSetScenarioSceneRoomLights(void)\n{");
	requireTokenOrder(lights, "netGridEditPrivatePlaytestAmbientState(&ambient_enabled, &ambient)",
		"clear_ambient = live_ambient && !ambient_enabled");
	REQUIRE(lights.find("has_default = source_only && pinned_default && !clear_ambient") != std::string::npos);
	REQUIRE(lights.find("restore_base = clear_ambient && pinned_default") != std::string::npos);
	REQUIRE(lights.find("else if (restore_base) selected = roomGetRangeBaseBrightness(i)") != std::string::npos);
	REQUIRE(lights.find("has_ambient ? ambient : roomGetRangeBaseBrightness(i)") != std::string::npos);
	REQUIRE(lights.find("bgApplyPrivateRoomAmbient(i, light->brightness, selected)") != std::string::npos);
	REQUIRE(lights.find("g_Rooms[i].br_base =") == std::string::npos);
	const auto native = readTextFile("src/game/dlights.c");
	const auto range = functionBlock(native, "s32 roomGetRangeBaseBrightness(");
	REQUIRE(range.find("const struct room *room = &g_Rooms[roomnum]") != std::string::npos);
	REQUIRE(range.find("room->br_light_min + (room->br_light_max - room->br_light_min) * 4 / 5") != std::string::npos);
}

TEST_CASE("Authored covers can opt into native routes while retaining their source rows",
	"[modding][pdxxx][scenario][navigation][static]")
{
	const std::string runtime = readTextFile("port/src/scenario_source_runtime.c");
	const std::string header = readTextFile("port/include/scenario_source_runtime.h");
	const std::string conformance = readTextFile("tools/asset_archive_conformance.py");
	const auto parse_start = runtime.find("static s32 s_readNavigationRootBool(");
	const auto parse_end = runtime.find("static void s_copyMemberPath(", parse_start);
	REQUIRE(parse_start != std::string::npos);
	REQUIRE(parse_end != std::string::npos);
	const auto parser = runtime.substr(parse_start, parse_end - parse_start);
	REQUIRE(parser.find("depth != 1") != std::string::npos);
	REQUIRE(parser.find("modAssetJsonStringEquals(key, p + 1, option)") != std::string::npos);
	REQUIRE(parser.find("s_readNavigationRootBool(text, end, \"generate_routes\", &generated)") != std::string::npos);
	REQUIRE(parser.find("modAssetJsonParseBoolToken(value, end, enabled, &token_end)") != std::string::npos);
	REQUIRE(parser.find("modAssetJsonValidateObject(text, text_size)") != std::string::npos);
	REQUIRE(conformance.find("generate_routes_fields > 1") != std::string::npos);
	REQUIRE(conformance.find("not isinstance(parsed[\"generate_routes\"], bool)") != std::string::npos);

	const auto loader = functionBlock(runtime, "static s32 s_loadNavigationTablesWithSources(");
	REQUIRE(loader.find("generate_routes && nav->cover_count > 0") != std::string::npos);
	REQUIRE(loader.find("nav->waypoint_count == 0 && nav->waygroup_count == 0") != std::string::npos);
	REQUIRE(loader.find("(nav->cover_count == 0 || keep_authored_covers)") != std::string::npos);
	REQUIRE(loader.find("path_table_loaded ? &path_table : NULL, !keep_authored_covers, &generated") != std::string::npos);
	REQUIRE(loader.find("generated.covers = nav->covers;") != std::string::npos);
	REQUIRE(loader.find("generated.cover_count = nav->cover_count;") != std::string::npos);
	requireTokenOrder(loader, "nav->covers = NULL;", "*nav = generated;");
	REQUIRE(loader.find("generated navigation routes with authored covers") != std::string::npos);

	const auto cover_source = functionBlock(runtime, "s32 scenarioSourceUsesGridCoverMember(");
	REQUIRE(header.find("s32 scenarioSourceUsesGridCoverMember(") != std::string::npos);
	REQUIRE(cover_source.find("s_ActiveScenarioGraphs.level_graph_active") != std::string::npos);
	REQUIRE(cover_source.find("s_ActiveScenarioGraphs.covers_path[0]") != std::string::npos);
	REQUIRE(cover_source.find("archive_root, \"::\", \"navigation/covers.json\"") != std::string::npos);
	REQUIRE(cover_source.find("strcmp(s_ActiveScenarioGraphs.covers_path, expected) == 0") != std::string::npos);
}

TEST_CASE("Generated waypoints may opt into traversable path endpoints",
	"[modding][pdxxx][scenario][navigation][static]")
{
	const auto runtime = readTextFile("port/src/scenario_source_runtime.c");
	const auto parse_start = runtime.find("static s32 s_parseWaypointsJson(");
	const auto parse_end = runtime.find("static s32 s_parseWaygroupsJson(", parse_start);
	REQUIRE(parse_start != std::string::npos);
	REQUIRE(parse_end != std::string::npos);
	const auto parse = runtime.substr(parse_start, parse_end - parse_start);
	REQUIRE(parse.find("s_readNavigationRootBool(text, end, \"only_linked_pads\", &linked)") != std::string::npos);
	REQUIRE(parse.find("linked >= 0 && !modAssetJsonValidateObject(text, text_size)") != std::string::npos);
	const auto loader = functionBlock(runtime, "static s32 s_loadNavigationTablesWithSources(");
	requireTokenOrder(loader, "if (generate)", "s_generateNavigationTablesFromPads(pads, pad_count, only_linked_pads,");
	REQUIRE(loader.find("only_linked_pads && path_table.count > 0 && !generated.waypoint_count") != std::string::npos);

	const auto build = functionBlock(runtime, "static s32 s_generateNavigationTablesFromPads(");
	requireTokenOrder(build, "path->pads[path->pad_count - 1]", "s_filterGeneratedWaypointLinks(nav, pad_to_waypoint)");
	requireTokenOrder(build, "nav->cover_count = generate_covers ? pad_count : 0",
		"s_filterGeneratedWaypointLinks(nav, pad_to_waypoint)");
	requireTokenOrder(build, "s_filterGeneratedWaypointLinks(nav, pad_to_waypoint)",
		"s_generateNavigationWaygroupsFromEdges(nav)");

	const auto filter = functionBlock(runtime, "static void s_filterGeneratedWaypointLinks(");
	REQUIRE(filter.find("SCENARIO_SEGMENT_FLAG_OUTWARD | SCENARIO_SEGMENT_FLAG_INWARD") != std::string::npos);
	requireTokenOrder(filter, "(edge & disabled) != disabled", "new_index[i] = count ? kept++ : -1");
	requireTokenOrder(filter, "new_index[i] = count ? kept++ : -1", "new_index[edge & SCENARIO_SEGMENT_ID_MASK]");
	REQUIRE(filter.find("(edge & ~SCENARIO_SEGMENT_ID_MASK) | (u32)neighbour") != std::string::npos);
}

TEST_CASE("Scenario source derives navigation runtime rows from empty public nav tables",
          "[modding][pdxxx][c3844][scenario][navigation][static]") {
	const std::string runtime =
		readTextFile("port/src/scenario_source_runtime.c");
	const std::string examples =
		readTextFile("tools/build_typed_pdxxx_examples.py");
	const std::string fixture_builder =
		readTextFile("tools/smoke-verify/build_generated_nav_fixture.py");
	const std::string fixture_runner =
		readTextFile("tools/smoke-verify/run-scenario-generated-nav-fixture.ps1");

	REQUIRE(runtime.find("s_generateNavigationTablesFromPads") !=
	        std::string::npos);
	REQUIRE(runtime.find("nav->waypoint_count == 0 && nav->waygroup_count == 0") !=
	        std::string::npos);
	REQUIRE(runtime.find("nav->cover_count == 0") != std::string::npos);
	REQUIRE(runtime.find("pad_count < 0 || (!pads && pad_count > 0)") !=
	        std::string::npos);
	REQUIRE(runtime.find("if (pad_count == 0)") != std::string::npos);
	REQUIRE(runtime.find("pad_count <= 0 || !nav") == std::string::npos);
	REQUIRE(runtime.find("const scenario_source_path_table_t *path_table") !=
	        std::string::npos);
	REQUIRE(runtime.find("path_table && path_table->count > 0") !=
	        std::string::npos);
	REQUIRE(runtime.find("pad_order[ordered_count++] = padnum") !=
	        std::string::npos);
	REQUIRE(runtime.find("pad_to_waypoint[padnum] = i") !=
	        std::string::npos);
	REQUIRE(runtime.find("s_generatedNavigationAddEdge(nav, a, b,") !=
	        std::string::npos);
	REQUIRE(runtime.find("path->pads[j - 1]") !=
	        std::string::npos);
	REQUIRE(runtime.find("path->flags & PATHFLAG_CIRCULAR") !=
	        std::string::npos);
	REQUIRE(runtime.find("path->pads[path->pad_count - 1]") !=
	        std::string::npos);
	REQUIRE(runtime.find("s_countNavigationBranchWaypoints(nav)") !=
	        std::string::npos);
	REQUIRE(runtime.find("s_generateNavigationWaygroupsFromEdges(nav)") !=
	        std::string::npos);
	REQUIRE(runtime.find("group_ids[neighbour] = group_count") !=
	        std::string::npos);
	REQUIRE(runtime.find("s_loadPathSourceRows(text, &path_table)") !=
	        std::string::npos);
	REQUIRE(runtime.find("s_countPathRowsWithFlag(&path_table, PATHFLAG_CIRCULAR)") !=
	        std::string::npos);
	REQUIRE(runtime.find("s_countPathRowsWithFlag(&path_table, PATHFLAG_FLYING)") !=
	        std::string::npos);
	REQUIRE(runtime.find("path_flags=circular:%d,flying:%d") !=
	        std::string::npos);
	REQUIRE(runtime.find("nav->waypoints[i].padnum = padnum") !=
	        std::string::npos);
	REQUIRE(runtime.find("nav->waypoints[i].groupnum = group") !=
	        std::string::npos);
	REQUIRE(runtime.find("nav->waygroups[group].waypoints.values[cursor] = (u32)i") !=
	        std::string::npos);
	REQUIRE(runtime.find("nav->covers[i].look[0] = pads[padnum].look[0]") !=
	        std::string::npos);
	REQUIRE(runtime.find("SCENARIO.SOURCE: generated deterministic navigation tables from public pads.json") !=
	        std::string::npos);
	REQUIRE(runtime.find("backend=source.navigation.generated+pads.json+navigation/waypoints.json+navigation/waygroups.json+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(runtime.find("backend=source.navigation.generated+pads.json+navigation/paths.json+navigation/waypoints.json+navigation/waygroups.json+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(runtime.find("s_buildPadfile(rows, row_count, &nav") >
	        runtime.find("s_loadNavigationTables(scenario, rows, row_count, pads_path, &nav)"));

	REQUIRE(examples.find("empty_rows_json(\"pd2.scenario.waypoints.v1\")") !=
	        std::string::npos);
	REQUIRE(examples.find("empty_rows_json(\"pd2.scenario.waygroups.v1\")") !=
	        std::string::npos);
	REQUIRE(examples.find("empty_rows_json(\"pd2.scenario.covers.v1\")") !=
	        std::string::npos);
	REQUIRE(examples.find("paths_json = json.dumps({") != std::string::npos);
	REQUIRE(examples.find("\"schema\": \"pd2.scenario.paths.v1\"") !=
	        std::string::npos);
	REQUIRE(examples.find("\"path_ref\": \"path_0000\"") !=
	        std::string::npos);
	REQUIRE(examples.find("\"pads\": [\"pad_0000\"]") !=
	        std::string::npos);
	REQUIRE(examples.find("\"pads.json\": scenario_pads_json(rel)") !=
	        std::string::npos);
	REQUIRE(examples.find("count_json_rows(kept['pads.json'])") !=
	        std::string::npos);

	REQUIRE(fixture_builder.find("\"path_ref\": \"path_0000\"") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("\"flags\": \"0x01\"") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("\"pads\": [\"pad_0000\", \"pad_0001\", \"pad_0002\"]") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("\"path_ref\": \"path_0001\"") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("\"pads\": [\"pad_0001|outward\", \"pad_0003\"]") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("\"path_ref\": \"path_0002\"") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("\"pad_ref\": \"pad_0000\"") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("\"schema\": \"pd2.scenario.objects.v1\"") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("\"record_id\": \"setup_0001\"") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("\"kind\": \"hover_car\"") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("\"model_catalog_id\": \"base:model_hovcop_eu\"") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("\"ailist_ref\": \"ailist_1026\"") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("\"schema\": \"pd2.scenario.ai.lists.v1\"") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("\"operands\": operands") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("(\"ailist_1025\", \"0x0401\", 0, 0, \"0x0076\", \"set_pad_preset_to_target_quadrant\", [\"0x01\", \"0x01\"])") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("(\"ailist_1025\", \"0x0401\", 4, 14, \"0x0042\", \"set_pad_preset_to_pad_on_route_to_target\", [\"0x03\"])") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("(\"ailist_1025\", \"0x0401\", 6, 20, \"0x0121\", \"find_cover\", [\"0x80\", \"0x85\", \"0x04\"])") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("(\"ailist_1025\", \"0x0401\", 12, 52, \"0x0124\", \"go_to_cover\", [\"0x11\"])") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("(\"ailist_1025\", \"0x0401\", 18, 69, \"0x012f\", \"release_cover\", [])") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("(\"ailist_1026\", \"0x0402\", 0, 0, \"0x00d5\", \"hovercar_begin_path\", [\"0x00\"])") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("(\"ailist_1026\", \"0x0402\", 1, 3, \"0x00d6\", \"set_vehicle_speed\", [\"0x0f\", \"0x00\", \"0x00\", \"0x3c\"])") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("manifest[\"ai_list_count\"] = 2") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("manifest[\"ai_command_count\"] = 23") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("manifest[\"object_count\"] = 2") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("\"source_counts\": {{ \"pads\": {counts[\"pads\"]}") !=
	        std::string::npos);
	REQUIRE(fixture_builder.find("\"source_hashes\": {{ \"scene.glb\":") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("base_scenario_test_ash.pdscenario") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("scenario_generated_nav_fixture.pdmod") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("packed_fixtures") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("@(\"scenario_generated_nav_fixture\")") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("source_counts=pads:5,volumes:0,waypoints:0,waygroups:0,covers:0,paths:3") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("capabilities=walk,jump,drop,wall,ceiling") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("as 5 waypoints, 2 waygroups, 5 covers, 4 path edges, 1 branch waypoints") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("path_flags=circular:1,flying:1") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("generated directional path segments") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("2 flagged neighbour refs") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("waypoint_neighbour_refs=8") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("waygroup_neighbour_refs=0") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("as 2 setup records, \\d+ spawns, 3 paths, 2 AI lists") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("AI action set_pad_preset_to_target_quadrant quadrant=1") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("AI action set_pad_preset_to_pad_on_route_to_target") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("AI action hovercar_begin_path path=0 found=1 vehicle_rows=[1-9]") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("AI action set_vehicle_speed vehicle_rows=[1-9]") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("AI action find_cover chr_rows=") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("AI action find_cover_within_dist chr_rows=") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("AI action find_cover_outside_dist chr_rows=") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("AI action go_to_cover chr_rows=") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("AI action check_cover_out_of_sight chr_rows=") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("AI action face_cover chr_rows=") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("AI action danger_cover chr_rows=") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("AI action release_cover chr_rows=") !=
	        std::string::npos);
	REQUIRE(fixture_runner.find("backend=source\\.navigation\\.generated\\+pads\\.json\\+navigation/paths\\.json") !=
	        std::string::npos);
}

TEST_CASE("Scenario source binds objects.json vehicle AI list refs into live vehicle setup",
          "[modding][pdxxx][c3844][scenario][setup][vehicle][static]") {
	const std::string runtime =
		readTextFile("port/src/scenario_source_runtime.c");
	const std::string setup = readTextFile("src/game/setup.c");

	REQUIRE(setup.find("SETUP.DEBUG") == std::string::npos);
	REQUIRE(setup.find("car->ailist = ailistFindById((uintptr_t)car->ailist)") !=
	        std::string::npos);

	const std::string write_vehicle_ailist =
		functionBlock(runtime, "s_setupWriteVehicleAilistRef");
	REQUIRE(!write_vehicle_ailist.empty());
	REQUIRE(write_vehicle_ailist.find("uintptr_t value = (uintptr_t)(u32)list_id") !=
	        std::string::npos);
	REQUIRE(write_vehicle_ailist.find("case OBJTYPE_TRUCK:") !=
	        std::string::npos);
	REQUIRE(write_vehicle_ailist.find("offsetof(struct truckobj, ailist)") !=
	        std::string::npos);
	REQUIRE(write_vehicle_ailist.find("case OBJTYPE_HELI:") !=
	        std::string::npos);
	REQUIRE(write_vehicle_ailist.find("offsetof(struct heliobj, ailist)") !=
	        std::string::npos);
	REQUIRE(write_vehicle_ailist.find("case OBJTYPE_HOVERCAR:") !=
	        std::string::npos);
	REQUIRE(write_vehicle_ailist.find("offsetof(struct hovercarobj, ailist)") !=
	        std::string::npos);
	REQUIRE(write_vehicle_ailist.find("case OBJTYPE_CHOPPER:") !=
	        std::string::npos);
	REQUIRE(write_vehicle_ailist.find("offsetof(struct chopperobj, ailist)") !=
	        std::string::npos);
	REQUIRE(write_vehicle_ailist.find("s_setupWriteRaw(record, offset, &value, sizeof(value))") !=
	        std::string::npos);

	const size_t apply_start = runtime.find("s_setupApplyObjectsJson");
	const size_t apply_end = runtime.find("s_setupCompareRecords", apply_start);
	REQUIRE(apply_start != std::string::npos);
	REQUIRE(apply_end != std::string::npos);
	const std::string apply_objects =
		runtime.substr(apply_start, apply_end - apply_start);
	REQUIRE(!apply_objects.empty());
	REQUIRE(apply_objects.find("pd2.scenario.objects.v1") !=
	        std::string::npos);
	REQUIRE(apply_objects.find("record->type == OBJTYPE_CHR") !=
	        std::string::npos);
	REQUIRE(apply_objects.find("offsetof(struct packedchr, ailistnum)") !=
	        std::string::npos);
	REQUIRE(apply_objects.find("record->type == OBJTYPE_TRUCK ||") !=
	        std::string::npos);
	REQUIRE(apply_objects.find("record->type == OBJTYPE_HELI ||") !=
	        std::string::npos);
	REQUIRE(apply_objects.find("record->type == OBJTYPE_HOVERCAR ||") !=
	        std::string::npos);
	REQUIRE(apply_objects.find("record->type == OBJTYPE_CHOPPER") !=
	        std::string::npos);
	REQUIRE(apply_objects.find("s_setupWriteVehicleAilistRef(record, ivalue)") !=
	        std::string::npos);
	requireTokenOrder(apply_objects,
		"offsetof(struct packedchr, ailistnum)",
		"s_setupWriteVehicleAilistRef(record, ivalue)");
}
