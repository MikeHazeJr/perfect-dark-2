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
	return ss.str();
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
	REQUIRE(conformance.find("AUDIO_WAV_NATIVE_FIELDS") !=
	        std::string::npos);
	REQUIRE(conformance.find("validate_audio_source_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("missing WAV playback metadata fields") !=
	        std::string::npos);
	REQUIRE(conformance.find("file_path = sample.wav") !=
	        std::string::npos);
	REQUIRE(conformance.find("AUDIO_WAV_MANIFEST_FIELDS") !=
	        std::string::npos);
	REQUIRE(conformance.find("parse_wav_source_metadata") !=
	        std::string::npos);
	REQUIRE(conformance.find("sample.wav must be a RIFF/WAVE file") !=
	        std::string::npos);
	REQUIRE(conformance.find("must be mono for native SFX/voice parity") !=
	        std::string::npos);
	REQUIRE(conformance.find("not match sample.wav rate") !=
	        std::string::npos);
	REQUIRE(conformance.find("does not match sample.wav frame count") !=
	        std::string::npos);
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
	REQUIRE(pdmesh_checker.find("model.render.json must reference every model.faces.json face exactly once") !=
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
	REQUIRE(upgrader.find("\"objectives_file\"") != std::string::npos);
	REQUIRE(upgrader.find("\"briefing_file\"") != std::string::npos);
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

TEST_CASE("scenario stage payloads reject ROM fallback in source-only mode",
          "[modding][pdxxx][c3844][scenario][source_gate][static]") {
	const std::string header = readTextFile("port/include/asset_source_debug.h");
	const std::string debug = readTextFile("port/src/asset_source_debug.c");
	const std::string setup = readTextFile("src/game/setup.c");
	const std::string propobj_runtime = readTextFile("src/game/propobj.c");
	const std::string early_objectives_runtime =
		readTextFile("src/game/objectives.c");
	const std::string constants = readTextFile("src/include/constants.h");
	const std::string bg_runtime = readTextFile("src/game/bg.c");
	const std::string dlights_runtime = readTextFile("src/game/dlights.c");
	const std::string scene_renderer =
		readTextFile("port/fast3d/scenario_scene_renderer.cpp");
	const std::string scene_renderer_h =
		readTextFile("port/include/scenario_scene_renderer.h");
	const std::string gfx_pc = readTextFile("port/fast3d/gfx_pc.cpp");
	const std::string prop_runtime = readTextFile("src/game/prop.c");
	const std::string tiles = readTextFile("src/game/tilesreset.c");
	const std::string lv = readTextFile("src/game/lv.c");
	const std::string pad_runtime = readTextFile("src/game/pad.c");
	const std::string pad_runtime_h = readTextFile("src/include/game/pad.h");
	const std::string setup_pads = readTextFile("src/game/setuppads.c");
	const std::string scenario_runtime =
		readTextFile("port/src/scenario_source_runtime.c");
	const std::string scenario_runtime_h =
		readTextFile("port/include/scenario_source_runtime.h");
	const std::string chraicommands =
		readTextFile("src/game/chraicommands.c");
	const std::string scenario_walker =
		readTextFile("port/src/loader_walker_scenario.c");
	const std::string scenario_extractor =
		readTextFile("port/src/romextract_pdarena.c");
	const std::string mesh_extractor =
		readTextFile("port/src/romextract_pdmesh.c");
	const std::string modasset_compiler =
		readTextFile("port/src/modasset_compiler.c");
	const std::string catalog_api =
		readTextFile("port/src/assetcatalog_api.c");
	const std::string meta_extractor =
		readTextFile("port/src/romextract_pdmeta.c");
	const std::string conformance =
		readTextFile("tools/asset_archive_conformance.py");

	REQUIRE(header.find("assetSourceDebugHandleUsesPublicFileSource") !=
	        std::string::npos);
	REQUIRE(header.find("assetSourceDebugHandleRequiresPublicFileSource") !=
	        std::string::npos);
	REQUIRE(header.find("assetSourceDebugFatalHandleFallback") !=
	        std::string::npos);
	REQUIRE(debug.find("assetSourceDebugIsEnabledFor(type)") !=
	        std::string::npos);
	REQUIRE(debug.find("assetSourceDebugHandleUsesPublicFileSource(handle)") !=
	        std::string::npos);
	REQUIRE(debug.find("s_pathIsRawExtractedRomPayload") !=
	        std::string::npos);
	REQUIRE(debug.find("s_pathHasSegment(path, \"files\")") !=
	        std::string::npos);
	REQUIRE(debug.find("s_pathHasSegment(path, \"segments\")") !=
	        std::string::npos);
	REQUIRE(debug.find("s_endsWithNoCase(path, \".bin\")") !=
	        std::string::npos);
	REQUIRE(debug.find("clean public typed-archive source") !=
	        std::string::npos);
	REQUIRE(debug.find("refusing ROM/static fallback") !=
	        std::string::npos);

	REQUIRE(setup.find("assetLoadRomToAddr(setupfilenum") ==
	        std::string::npos);
	REQUIRE(setup.find("setupRequireScenarioSourceHandle(\"briefing setup\"") !=
	        std::string::npos);
	REQUIRE(setup.find("assetLoadToAddr(setup_handle") !=
	        std::string::npos);
	REQUIRE(setup.find("assetLoadToNew(setup_handle") ==
	        std::string::npos);
	REQUIRE(setup.find("assetLoadToNew(stage.pads_handle") ==
	        std::string::npos);
	REQUIRE(setup.find("setup source -> ROM handle") ==
	        std::string::npos);
	REQUIRE(setup.find("pads source -> ROM handle") ==
	        std::string::npos);
	REQUIRE(setup.find("\"mp setup\"") != std::string::npos);
	REQUIRE(setup.find("\"setup\"") != std::string::npos);
	REQUIRE(setup.find("scenarioSourceLoadSetupForStage(&stage") !=
	        std::string::npos);
	REQUIRE(setup.find("scenarioSourceActivateGraphsForStage(&stage") !=
	        std::string::npos);
	REQUIRE(setup.find("scenarioSourceFatalRuntimeFallbackForStage(&stage") !=
	        std::string::npos);
	REQUIRE(setup.find("level graph activation failed") !=
	        std::string::npos);
	REQUIRE(setup.find("setup source compile failed") !=
	        std::string::npos);
	REQUIRE(setup.find("pads source compile failed") !=
	        std::string::npos);
	REQUIRE(setup.find("setupIntroCommandsAreValid") !=
	        std::string::npos);
	REQUIRE(setup.find("if (!g_GeCreditsData)") != std::string::npos);
	REQUIRE(setup.find("setupRequireScenarioSourceHandle(\"pads\"") !=
	        std::string::npos);
	REQUIRE(setup.find("scenarioSourceLoadPadsForStage(&stage") !=
	        std::string::npos);
	REQUIRE(setup.find("prop = obj->prop;\n\n\t\t\t\t\t\tif (prop == NULL)") !=
	        std::string::npos);
	REQUIRE(constants.find("#define MODELNODETYPE_TYPE19       0x19") !=
	        std::string::npos);
	REQUIRE(propobj_runtime.find("modelGetType19PartRodata") !=
	        std::string::npos);
	REQUIRE(propobj_runtime.find("(node->type & 0xff) == MODELNODETYPE_TYPE19") !=
	        std::string::npos);
	REQUIRE(propobj_runtime.find("modelGetType19PartRodata(modeldef, MODELPART_BASIC_0065)") !=
	        std::string::npos);
	REQUIRE(propobj_runtime.find("modelGetType19PartRodata(modeldef, MODELPART_BASIC_0066)") !=
	        std::string::npos);
	REQUIRE(propobj_runtime.find("union modelrodata *floorPart = modelGetPartRodata(modeldef, MODELPART_BASIC_0065)") ==
	        std::string::npos);
	REQUIRE(early_objectives_runtime.find("g_Vars.currentplayer && g_Vars.currentplayer->prop &&") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("#include \"scenario_scene_renderer.h\"") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("#include \"scenario_source_runtime.h\"") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("scenarioSourceValidateBackgroundGeometryForStage(&stage") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("bgTryActivateScenarioSourceBackground(&stage") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("bgBuildScenarioSourceTables(stagenum)") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("scenarioSceneRendererIsActive()") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("g_BgUsingScenarioSource") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("assetSourceDebugFatalHandleFallback(ASSET_SCENARIO, \"background geometry\"") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("bg source -> ROM bg cache") ==
	        std::string::npos);
	REQUIRE(bg_runtime.find("scenarioSourceFatalRuntimeFallbackForStage(&stage") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("background source renderer activation failed") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("stage.bg_handle") != std::string::npos);
	REQUIRE(scene_renderer_h.find("scenarioSceneRendererActivate") !=
	        std::string::npos);
	REQUIRE(scene_renderer_h.find("scenario_scene_renderer_probe_t") !=
	        std::string::npos);
	REQUIRE(scene_renderer_h.find("scenarioSceneRendererProbeSource") !=
	        std::string::npos);
	REQUIRE(scene_renderer_h.find("scenarioSceneRendererSetCameraFrame") !=
	        std::string::npos);
	REQUIRE(scene_renderer_h.find("scenarioSceneRendererRender") !=
	        std::string::npos);
	const std::string cmake = readTextFile("CMakeLists.txt");
	const std::string probe_main =
		readTextFile("tools/scenario_scene_probe_main.cpp");
	const std::string probe_stubs =
		readTextFile("tools/scenario_scene_probe_stubs.cpp");
	REQUIRE(cmake.find("add_executable(scenario-scene-probe") !=
	        std::string::npos);
	REQUIRE(cmake.find("CXX_SCAN_FOR_MODULES OFF") != std::string::npos);
	REQUIRE(cmake.find("port/fast3d/scenario_scene_renderer.cpp") !=
	        std::string::npos);
	REQUIRE(probe_main.find("scenarioSceneRendererProbeSource") !=
	        std::string::npos);
	REQUIRE(probe_main.find("--debug-scenario-render-probe") !=
	        std::string::npos);
	REQUIRE(probe_main.find("SCENARIO.RENDER.CPU_PROBE") !=
	        std::string::npos);
	REQUIRE(probe_stubs.find("extern \"C\" void *fsFileLoad") !=
	        std::string::npos);
	REQUIRE(probe_stubs.find("extern \"C\" s32 sysArgCheck") !=
	        std::string::npos);
	REQUIRE(probe_stubs.find("modArchiveExtractMemAlloc") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("scenarioSceneRendererSetCameraFrame(") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("g_Vars.currentplayer->cam_pos.f") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("g_Vars.currentplayer->cam_look.f") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("g_Vars.currentplayer->cam_up.f") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("fovy = viGetFovY()") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("aspect = viGetAspect()") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("fovy = g_Vars.currentplayer->zoominfovy") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("fovy = 60.0f") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("aspect = g_Vars.currentplayer->aspect") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("aspect = 4.0f / 3.0f") !=
	        std::string::npos);
	REQUIRE(gfx_pc.find("scenarioSceneRendererRender(gfx_current_dimensions.width") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("fsFileLoad(path") != std::string::npos);
	REQUIRE(scene_renderer.find("findAttr(\"TEXCOORD_1\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("findAttr(\"TEXCOORD_0\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("findAttr(\"COLOR_0\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("int uv0_i = findAttr(\"TEXCOORD_0\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("int uv1_i = findAttr(\"TEXCOORD_1\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("out.u0 = out.u1") != std::string::npos);
	REQUIRE(scene_renderer.find("out.u1 = out.u0") != std::string::npos);
	REQUIRE(scene_renderer.find("mat.primary_texcoord = base ? intField(*base, \"texCoord\", 0) : 0") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("mat.primary_texcoord = 1") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("vec4 sampled = texture(u_Tex, pdTexcoord(u_PrimaryTexcoord))") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("uniform sampler2D u_Tex2") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("uniform int u_PrimaryTexcoord") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("uniform int u_SecondaryTexcoord") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("glBindAttribLocation(program, 1, \"a_Uv0\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("glBindAttribLocation(program, 2, \"a_Uv1\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("readTextureBinding(textures, samplers") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("mat.has_secondary") != std::string::npos);
	REQUIRE(scene_renderer.find("secondary_texcoord = mat.secondary_texcoord == 0 ? 0 : 1") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("glUniform1i(secondary_texcoord_loc, secondary_texcoord)") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("sampled = mix(sampled, sampled2, 0.5)") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("if (outColor.a < u_AlphaCutoff) discard") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("imageHasNonOpaqueAlpha") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("markMaterialAlpha(out)") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("fillProbeResult") != std::string::npos);
	REQUIRE(scene_renderer.find("probeLoggingEnabled") != std::string::npos);
	REQUIRE(scene_renderer.find("objectField(const crude_json::object &obj") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("intField(const crude_json::object &obj") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("const crude_json::object *mat_obj =") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("m.get_ptr<crude_json::object>()") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("objectField(*mat_obj, \"pbrMetallicRoughness\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("intField(*secondary, \"texCoord\", 1)") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("decode image index=%zu") != std::string::npos);
	const std::string probe_block =
		functionBlock(scene_renderer, "scenarioSceneRendererProbeSource");
	REQUIRE(probe_block.find("Scene probe") != std::string::npos);
	REQUIRE(probe_block.find("buildCpuScene(scenario_id, scene_path, probe)") !=
	        std::string::npos);
	REQUIRE(probe_block.find("fillProbeResult(probe, out_probe)") !=
	        std::string::npos);
	REQUIRE(probe_block.find("freeScene(probe)") != std::string::npos);
	REQUIRE(probe_block.find("g_scene") == std::string::npos);
	REQUIRE(scene_renderer.find("buildViewMatrix(view, position, look, up)") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("buildProjectionMatrix(projection, fovy_degrees, aspect") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("multiplyMatrix(g_scene.view_projection, view, projection)") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("camera_ever_valid") != std::string::npos);
	REQUIRE(scene_renderer.find("if (!g_scene.camera_ever_valid)") !=
	        std::string::npos);
	requireTokenOrder(scene_renderer,
		"if (!g_scene.camera_ever_valid)",
		"SCENARIO.RENDER: skipping source scene");
	REQUIRE(scene_renderer.find("g_scene.view_projection") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("SCENARIO.RENDER: activated source scene") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("SCENARIO.RENDER: accepted camera frame") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("SCENARIO.RENDER: rendered native source scene") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("glGetShaderiv(shader, GL_COMPILE_STATUS") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("SCENARIO.RENDER: shader compile failed") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("glGetProgramiv(program, GL_LINK_STATUS") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("SCENARIO.RENDER: shader link failed") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("if (!ensureShader(scene))") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("glGetIntegerv(GL_VIEWPORT, prev_viewport)") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("glViewport(prev_viewport[0], prev_viewport[1]") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("alpha_textures=%zu alpha_materials=%zu secondary_materials=%zu") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("dualtex=extras") != std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceValidateBackgroundGeometryForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceValidateBackgroundGeometryForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("catalogLoadTypedAsset(ASSET_SCENARIO, scenario->id)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("catalogGetLoadedColmesh(scenario->id)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: validated background scene source") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("SCENARIO.SOURCE: background renderer using native scene source") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("SCENARIO.SOURCE: built native background room tables") !=
	        std::string::npos);
	const std::string scenario_source_smoke =
		readTextFile("tools/smoke-verify/tests/scenario_pads_source_gate_smoke.json");
	REQUIRE(scenario_source_smoke.find("SCENARIO\\\\.RENDER: rendered native source scene") !=
	        std::string::npos);
	REQUIRE(scenario_source_smoke.find("SCENARIO\\\\.RENDER: skipping source scene .* without camera matrices") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("native BG renderer still pending") ==
	        std::string::npos);

	REQUIRE(scenario_walker.find("e->ext.scenario.pads_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("e->ext.scenario.portals_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"portals\", \"portals.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"pads\", \"pads.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("e->ext.scenario.objects_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"objects\", \"objects.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("e->ext.scenario.setup_fields_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"setup_fields\", \"setup.fields.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("navigation_waypoints_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"waypoints\", \"navigation/waypoints.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("navigation_waygroups_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"waygroups\", \"navigation/waygroups.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("navigation_covers_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"covers\", \"navigation/covers.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("navigation_paths_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"paths\", \"navigation/paths.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.lists.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.pads.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("portals.json") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.portals.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_buildBgPortalsJson") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("\\\"portal_count\\\": %u") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.navigation.paths.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_buildPathsJson") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_pathRef((s32)paths[i].id") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_buildAiListsTable") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_buildIntroSpawnsTable") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("INTROCMD_SPAWN") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("\\\"spawn_count\\\": %u") !=
	        std::string::npos);

	REQUIRE(scenario_runtime.find("fsFileLoad(pads_path") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_parsePadsJson") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_parseWaypointsJson") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_generateNavigationTablesFromPads") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("const scenario_source_path_table_t *path_table") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path_table && path_table->count > 0") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("u32 *pad_segments") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_parsePathPadJsonArray(cursor, object_end + 1") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("&row.pad_segments") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("pad_order[ordered_count++] = padnum") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_generatedNavigationAddEdge(nav, a, b,") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_segmentListAppendOrMerge") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_reverseSegmentFlags") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO_SEGMENT_FLAG_OUTWARD") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO_SEGMENT_FLAG_INWARD") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO_SEGMENT_ID_MASK)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path->pads[j - 1]") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path->pad_segments[j - 1]") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path->flags & PATHFLAG_CIRCULAR") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path->pads[path->pad_count - 1]") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path->pad_segments[path->pad_count - 1]") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("branch waypoints backend=source.navigation.generated") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countNavigationDirectionalSegments") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countNavigationWaypointNeighbourRefs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countNavigationWaygroupNeighbourRefs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("generated directional path segments from public navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_generateNavigationWaygroupsFromEdges(nav)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("group_ids[neighbour] = group_count") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("nav->waypoints[i].padnum = padnum") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("nav->covers[i].pos[0] = pads[padnum].pos[0]") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("navigation/waypoints.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing public navigation table 'navigation/waypoints.json'") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing public navigation table 'navigation/waygroups.json'") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing public navigation table 'navigation/covers.json'") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: compiled navigation tables") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("waypoint_neighbour_refs=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("waygroup_neighbour_refs=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=source.navigation.tables+navigation/waypoints.json+navigation/waygroups.json+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: generated deterministic navigation tables from public pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=source.navigation.generated+pads.json+navigation/waypoints.json+navigation/waygroups.json+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_loadPathSourceRows(text, &path_table)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("row.id != table->count") ==
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countPathRowsWithFlag(&path_table, PATHFLAG_CIRCULAR)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countPathRowsWithFlag(&path_table, PATHFLAG_FLYING)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path_flags=circular:%d,flying:%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=source.navigation.generated+pads.json+navigation/paths.json+navigation/waypoints.json+navigation/waygroups.json+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_bindNavigationGenerateSource") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countAndHashSourceJsonRows") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_hashSourceTextHex") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_hashSourceFileHex") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("nav_source_counts") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("nav_source_hashes") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("portals_path") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\\\"portals.json\\\": \\\"%s\\\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("level_navigation_generate_node_count") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("supports_walk = true") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("supports_jump = true") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("supports_drop = true") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("supports_wall = true") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("supports_ceiling = true") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("source_counts=pads:%d,volumes:%d,waypoints:%d,waygroups:%d,covers:%d,paths:%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("source_hashes=sha256") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("capabilities=walk,jump,drop,wall,ceiling") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.navigation.generate+navigation.ini+generated-navmesh.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("navigation.ini missing deterministic nav generation contract") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("generated navmesh metadata missing deterministic cache contract") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("generated navmesh metadata source counts mismatch public navigation tables") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("generated navmesh metadata source hashes mismatch public navigation inputs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_loadPathSourceRows") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("setup->paths = path_count ?") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("struct padsfileheader") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("struct waypoint") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("PADFLAG_HASBBOXDATA") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourcePadsGetWideOffsets") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_clearSourceWidePadOffsets()") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("sizeof(u32)") != std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: pads.json runtime uses 32-bit source pad offsets") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("exceeding u16 pad offsets") ==
	        std::string::npos);
	{
		const std::string clear_wide_offsets =
			functionBlock(scenario_runtime, "s_clearSourceWidePadOffsets");
		const std::string get_wide_offsets =
			functionBlock(scenario_runtime, "scenarioSourcePadsGetWideOffsets");
		const std::string build_padfile =
			functionBlock(scenario_runtime, "s_buildPadfile");
		REQUIRE(!clear_wide_offsets.empty());
		REQUIRE(!get_wide_offsets.empty());
		REQUIRE(!build_padfile.empty());
		REQUIRE(clear_wide_offsets.find("g_SourceWidePadFile = NULL;") !=
		        std::string::npos);
		REQUIRE(clear_wide_offsets.find("g_SourceWidePadOffsets = NULL;") !=
		        std::string::npos);
		REQUIRE(clear_wide_offsets.find("g_SourceWidePadOffsetCount = 0;") !=
		        std::string::npos);
		REQUIRE(get_wide_offsets.find("padfiledata == g_SourceWidePadFile") !=
		        std::string::npos);
		REQUIRE(get_wide_offsets.find(
			        "g_SourceWidePadOffsets && g_SourceWidePadOffsetCount >= 0") !=
		        std::string::npos);
		REQUIRE(get_wide_offsets.find("*out_count = g_SourceWidePadOffsetCount;") !=
		        std::string::npos);
		REQUIRE(get_wide_offsets.find("return g_SourceWidePadOffsets;") !=
		        std::string::npos);
		REQUIRE(build_padfile.find("if (wide_offsets)") !=
		        std::string::npos);
		REQUIRE(build_padfile.find("g_SourceWidePadFile = buf;") !=
		        std::string::npos);
		REQUIRE(build_padfile.find("g_SourceWidePadOffsets = wide_offsets;") !=
		        std::string::npos);
		REQUIRE(build_padfile.find("g_SourceWidePadOffsetCount = row_count;") !=
		        std::string::npos);
	}
	REQUIRE(pad_runtime_h.find("padGetPackedOffset") != std::string::npos);
	REQUIRE(pad_runtime.find("g_PadOffsets32") != std::string::npos);
	REQUIRE(pad_runtime.find("return g_PadOffsets ? g_PadOffsets[padnum] : 0") !=
	        std::string::npos);
	REQUIRE(setup_pads.find("scenarioSourcePadsGetWideOffsets") !=
	        std::string::npos);
	REQUIRE(setup_pads.find("padSetWideOffsets(wide_offsets)") !=
	        std::string::npos);
	REQUIRE(setup_pads.find("offset = padGetPackedOffset(padnum)") !=
	        std::string::npos);
	REQUIRE(setup_pads.find("offset = g_PadOffsets[padnum]") ==
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: compiled pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceLoadPortalsForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_parsePortalsJson") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: compiled portals.json") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("bgBuildScenarioSourcePortalTables") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("scenarioSourceLoadPortalsForStage(stage") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("SCENARIO.SOURCE: built native portal tables") !=
	        std::string::npos);
	{
		const std::string bg_source_tables =
			functionBlock(bg_runtime, "bgBuildScenarioSourceTables");
		REQUIRE(bg_source_tables.find("if (g_BgLightsFileData)") !=
		        std::string::npos);
		REQUIRE(bg_source_tables.find("func0f001c0c();") !=
		        std::string::npos);
		REQUIRE(bg_source_tables.find("skipped legacy dynamic-light precompute") !=
		        std::string::npos);
	}
	{
		const std::string portal_distance =
			functionBlock(dlights_runtime, "func0f000920");
		const std::string light_reset =
			functionBlock(dlights_runtime, "func0f002a98");
		const std::string sound_distance =
			functionBlock(dlights_runtime, "func0f0056f4");
		const std::string portal_range =
			functionBlock(dlights_runtime, "func0f0059fc");
		REQUIRE(portal_distance.find("var80061430 == NULL") !=
		        std::string::npos);
		REQUIRE(portal_distance.find("upper >= g_NumPortals") !=
		        std::string::npos);
		REQUIRE(portal_distance.find("var80061430[upper] == NULL") !=
		        std::string::npos);
		REQUIRE(portal_distance.find("return 0xffffffff") !=
		        std::string::npos);
		REQUIRE(light_reset.find("var80061430 = NULL") !=
		        std::string::npos);
		REQUIRE(light_reset.find("g_NumPortals = 0") !=
		        std::string::npos);
		REQUIRE(sound_distance.find("var80061430 == NULL") !=
		        std::string::npos);
		REQUIRE(sound_distance.find("g_NumPortals <= 0") !=
		        std::string::npos);
		REQUIRE(portal_range.find("var80061430 == NULL") !=
		        std::string::npos);
		REQUIRE(portal_range.find("g_NumPortals <= 0") !=
		        std::string::npos);
		REQUIRE(portal_range.find("coordsGetDistance(pos1, pos2)") !=
		        std::string::npos);
	}
	REQUIRE(scenario_runtime_h.find("scenarioSourceLoadSetupForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceActivateGraphsForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceSetupGraphRecordBehaviorLink") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceLoadSetupForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceActivateGraphsForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("pd2.level.graph.v1") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("pd2.mission.graph.v1") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_loadGraphText") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("fsFileLoad(path") != std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: activated level graph") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: table refs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: volume source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.trigger.volumes+volumes.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceLevelGraphCheckPadRoom") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceLevelGraphCheckPadRoom") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: trigger volume evaluated from graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.trigger.volumes+objective.status") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("level_volume_node_count") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.trigger.volume.source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: trigger volume nodes") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.trigger.volumes+level.graph.nodes+volumes.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: global settings source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.global.settings+level.graph.nodes") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("level_global_settings_node_count") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceLevelGraphRecordTick") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceLevelGraphRecordTick") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: level tick from graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.global.settings+level.tick") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("global settings source scenario mismatch") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_loadAiListSourceJson") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_loadSpawnSourceRows") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiFindOrAddList(table, ailist_ref") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_ActiveScenarioGraphs.spawns_path") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_ActiveScenarioGraphs.ai_lists_path") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI list source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.lists+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI list-control actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.list_control+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI alarm actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.alarm+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI flag actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.flags+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI savefile flag actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.savefile_flags+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI timer/countdown actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.timer+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI HUD message actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.hud+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_return_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_shot_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.return_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.stop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.kneel") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.surrender") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.fade_out") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.remove_chr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_sidestep") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_attack_stand") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_attacking") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_modify_attack") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.face_entity") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.apply_gset_damage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_damage_chr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.consider_grenade_throw") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.drop_item") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_run_from_target") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_jog_to_target_prop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_walk_to_target_prop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_run_to_target_prop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_go_to_cover_prop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_jog_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_walk_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_run_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_do_animation") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.be_surprised_one_hand") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.be_surprised_look_around") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.be_surprised_surrender") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.random") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_random_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_random_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.control.random+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.print") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.noop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePrint") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteNoOp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.debug_noop+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_punch_dodge_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_shooting_at_me_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_dark_room_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_player_dead_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetReturnList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetShotList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteReturnList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteStop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteKneel") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSurrender") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteFadeOut") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRemoveChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.character_lifecycle+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.basic_motion+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action stop chr=%d chr_rows=%d source_chr=%d hovercar=%d vehicle_rows=%d source_vehicle_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action kneel chr=%d chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action surrender chr=%d chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action fade_out chr=%d chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteStop");
		REQUIRE(!block.empty());
		REQUIRE(block.find("objects=%s backend=%s") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"chopperStop(hovercar)");
	}
	{
		const char *actions[][2] = {
			{"scenarioSourceAiGraphExecuteStop", "chrTryStop("},
			{"scenarioSourceAiGraphExecuteKneel", "chrTryKneel("},
			{"scenarioSourceAiGraphExecuteSurrender", "chrTrySurrender("},
			{"scenarioSourceAiGraphExecuteFadeOut", "chrFadeOut("},
		};

		for (const auto &action : actions) {
			const std::string block = functionBlock(scenario_runtime,
				action[0]);
			REQUIRE(!block.empty());
			REQUIRE(block.find("chr_rows=%d source_chr=%d") !=
			        std::string::npos);
			requireTokenOrder(block,
				"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(",
				action[1]);
		}
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.combat+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteFaceEntity") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteApplyGsetDamage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrDamageChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteConsiderGrenadeThrow") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteDropItem") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryRunFromTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryJogToTargetProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryWalkToTargetProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryRunToTargetProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryGoToCoverProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryJogToChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryWalkToChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryRunToChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.target_movement+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfCanHearAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfPatrolling") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfAlarmActive") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfGasActive") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfHearsTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfSawInjury") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfSawDeath") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfLosToTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfLosToAttackTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfTargetNearlyInSight") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfNearlyInTargetsSight") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetPadPresetToPadOnRouteToTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeNavigationTables(\n\t\t\t\"set_pad_preset_to_pad_on_route_to_target\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimePad(\n\t\t\t\t\"set_pad_preset_to_pad_on_route_to_target\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_pad_preset_to_pad_on_route_to_target chr_rows=%d source_chr=%d label=%d pad=%d found=%d pad_rows=%d waypoint_rows=%d waygroup_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfSawTargetRecently") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfHeardTargetRecently") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.perception_alarm+ai/ailists.json") !=
	        std::string::npos);
	for (const char *symbol : {
		     "scenarioSourceAiGraphExecuteIfLosToChr",
		     "scenarioSourceAiGraphExecuteIfNeverBeenOnScreen",
		     "scenarioSourceAiGraphExecuteIfOnScreen",
		     "scenarioSourceAiGraphExecuteIfChrInOnScreenRoom",
		     "scenarioSourceAiGraphExecuteIfRoomIsOnScreen",
		     "scenarioSourceAiGraphExecuteIfTargetAimingAtMe",
		     "scenarioSourceAiGraphExecuteIfNearMiss",
		     "scenarioSourceAiGraphExecuteIfSeesSuspiciousItem",
		     "scenarioSourceAiGraphExecuteIfCheckFovWithTarget",
		     "scenarioSourceAiGraphExecuteIfTargetInFovLeft",
		     "scenarioSourceAiGraphExecuteIfTargetOutOfFovLeft",
		     "scenarioSourceAiGraphExecuteIfTargetInFov",
		     "scenarioSourceAiGraphExecuteIfTargetOutOfFov",
		     "scenarioSourceAiGraphExecuteIfDistanceToTargetLessThan",
		     "scenarioSourceAiGraphExecuteIfDistanceToTargetGreaterThan",
		     "scenarioSourceAiGraphExecuteIfChrDistanceToPadLessThan",
		     "scenarioSourceAiGraphExecuteIfChrDistanceToPadGreaterThan",
		     "scenarioSourceAiGraphExecuteIfDistanceToChrLessThan",
		     "scenarioSourceAiGraphExecuteIfDistanceToChrGreaterThan",
		     "scenarioSourceAiGraphExecuteIfAnyChrNearSelf",
		     "scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadLessThan",
		     "scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadGreaterThan",
		     "scenarioSourceAiGraphExecuteIfChrInRoom",
		     "scenarioSourceAiGraphExecuteIfTargetInRoom",
		     "scenarioSourceAiGraphExecuteIfChrHasObject",
		     "scenarioSourceAiGraphExecuteIfWeaponThrown",
		     "scenarioSourceAiGraphExecuteIfWeaponThrownOnObject",
		     "scenarioSourceAiGraphExecuteIfChrHasWeaponEquipped",
		     "scenarioSourceAiGraphExecuteIfGunUnclaimed",
		     "scenarioSourceAiGraphExecuteIfObjectHealthy",
		     "scenarioSourceAiGraphExecuteIfChrActivatedObject",
		     "scenarioSourceAiGraphExecuteObjInteract",
		     "scenarioSourceAiGraphExecuteDestroyObject",
		     "scenarioSourceAiGraphExecuteDropObjectFromChr",
		     "scenarioSourceAiGraphExecuteChrDropItems",
		     "scenarioSourceAiGraphExecuteChrDropWeapon",
		     "scenarioSourceAiGraphExecuteGiveObjectToChr",
		     "scenarioSourceAiGraphExecuteObjectMoveToPad",
		     "scenarioSourceAiGraphExecuteIfWaypointWithinQuadrant",
		     "scenarioSourceAiGraphExecuteSetPadPresetToTargetQuadrant",
		     "scenarioSourceAiGraphExecuteChrDeleteWeapon",
		     "scenarioSourceAiGraphExecuteIfTriggerShotList",
		     "scenarioSourceAiGraphExecuteEndLevel",
		     "scenarioSourceAiGraphExecuteEndCutscene",
		     "scenarioSourceAiGraphExecuteWarpJoToPad",
		     "scenarioSourceAiGraphExecuteSetCameraAnimation",
		     "scenarioSourceAiGraphExecuteIfInCutscene",
		     "scenarioSourceAiGraphExecuteIfCutsceneButtonPressed",
		     "scenarioSourceAiGraphExecuteReorientForCutsceneStop",
		     "scenarioSourceAiGraphExecuteWarpJoToTag",
		     "scenarioSourceAiGraphExecuteRevokeControl",
		     "scenarioSourceAiGraphExecuteGrantControl",
		     "scenarioSourceAiGraphExecutePlayerFadeIn",
		     "scenarioSourceAiGraphExecutePlayersFadeOut",
		     "scenarioSourceAiGraphExecuteIfColourFadeComplete",
		     "scenarioSourceAiGraphExecutePrepareWarpOrbit",
		     "scenarioSourceAiGraphExecuteBeginWarpLatch",
		     "scenarioSourceAiGraphExecuteIfWarpLatchComplete",
		     "scenarioSourceAiGraphExecuteSpawnChrAtPad",
		     "scenarioSourceAiGraphExecuteSpawnChrAtChr",
		     "scenarioSourceAiGraphExecuteTryEquipWeapon",
		     "scenarioSourceAiGraphExecuteTryEquipHat",
		     "scenarioSourceAiGraphExecuteSetObjImage",
		     "scenarioSourceAiGraphExecuteObjectDoAnimation",
		     "scenarioSourceAiGraphExecuteSetDoorOpen",
		     "scenarioSourceAiGraphExecuteDuplicateChr",
		     "scenarioSourceAiGraphExecuteEnableChr",
		     "scenarioSourceAiGraphExecuteDisableChr",
		     "scenarioSourceAiGraphExecuteEnableObj",
		     "scenarioSourceAiGraphExecuteDisableObj",
		     "scenarioSourceAiGraphExecuteChrMoveToPad",
		     "scenarioSourceAiGraphExecuteChrSetTeam",
		     "scenarioSourceAiGraphExecuteDamageChrByAmount",
		     "scenarioSourceAiGraphExecuteDoPresetAnimation",
		     "scenarioSourceAiGraphExecuteIfPlayerChrPortalDistanceLessThan",
		     "scenarioSourceAiGraphExecuteIfChrRepositionValid",
		     "scenarioSourceAiGraphExecuteReleaseCover",
		     "scenarioSourceAiGraphExecuteIfChrNotTalking",
		     "scenarioSourceAiGraphExecuteIfOrders",
		     "scenarioSourceAiGraphExecuteIfHasOrders",
		     "scenarioSourceAiGraphExecuteIfChrInSquadronDoingAction",
		     "scenarioSourceAiGraphExecuteIfChrListening",
		     "scenarioSourceAiGraphExecuteIfNotListening",
		     "scenarioSourceAiGraphExecuteIfChrInjuredTarget",
		     "scenarioSourceAiGraphExecuteIfAction",
		     "scenarioSourceAiGraphExecuteIfChrAmmoQuantityLessThan",
		     "scenarioSourceAiGraphExecuteIfChrTarget",
		     "scenarioSourceAiGraphExecuteIfCompareChrPresetsTeam",
		     "scenarioSourceAiGraphExecuteIfHuman",
		     "scenarioSourceAiGraphExecuteIfSkedar",
		     "scenarioSourceAiGraphExecuteIfPropPresetIsBlockingSightToTarget",
		     "scenarioSourceAiGraphExecuteRemoveObjectAtPropPreset",
		     "scenarioSourceAiGraphExecuteIfPropPresetHeightLessThan",
		     "scenarioSourceAiGraphExecuteSetTarget",
		     "scenarioSourceAiGraphExecuteIfPresetsTargetIsNotMyTarget",
		     "scenarioSourceAiGraphExecuteSetChrPresetToChrNearSelf",
		     "scenarioSourceAiGraphExecuteSetChrPresetToChrNearPad",
		     "scenarioSourceAiGraphExecuteIfSafety2LessThan",
		     "scenarioSourceAiGraphExecuteIfPlayerUsingCmpOrAr34",
		     "scenarioSourceAiGraphExecuteDetectEnemyOnSameFloor",
		     "scenarioSourceAiGraphExecuteDetectEnemy",
		     "scenarioSourceAiGraphExecuteIfSafetyLessThan",
		     "scenarioSourceAiGraphExecuteIfTargetMovingSlowly",
		     "scenarioSourceAiGraphExecuteIfTargetMovingCloser",
		     "scenarioSourceAiGraphExecuteIfTargetMovingAway",
		     "scenarioSourceAiGraphExecuteIfSquadronIsDead",
		     "scenarioSourceAiGraphExecuteIfTrue",
		     "scenarioSourceAiGraphExecuteIfNumChrsInSquadronGreaterThan",
		     "scenarioSourceAiGraphExecuteIfNaturalAnim",
		     "scenarioSourceAiGraphExecuteIfY",
		     "scenarioSourceAiGraphExecuteIfSoundTimer",
		     "scenarioSourceAiGraphExecuteIfTargetYDifferenceLessThan",
		     "scenarioSourceAiGraphExecuteChrExplosions",
		     "scenarioSourceAiGraphExecuteSetTintedGlassEnabled",
		     "scenarioSourceAiGraphExecuteHovercopterFireRocket",
		     "scenarioSourceAiGraphExecuteChrAdjustMotionBlur",
		     "scenarioSourceAiGraphExecutePunchOrKick",
		     "scenarioSourceAiGraphExecuteSetTargetToEyespyIfInSight",
		     "scenarioSourceAiGraphExecuteMiniSkedarTryPounce",
		     "scenarioSourceAiGraphExecuteIfObjectDistanceToPadLessThan",
		     "scenarioSourceAiGraphExecuteAvoid",
		     "scenarioSourceAiGraphExecuteTitleInitMode",
		     "scenarioSourceAiGraphExecuteTryExitTitle",
		     "scenarioSourceAiGraphExecuteChrEmitSparks",
		     "scenarioSourceAiGraphExecuteSetDrCarollImages",
		     "scenarioSourceAiGraphExecuteSayQuip",
		     "scenarioSourceAiGraphExecuteSayCiStaffQuip",
		     "scenarioSourceAiGraphExecuteShuffleRuinsPillars",
		     "scenarioSourceAiGraphExecuteShufflePelagicSwitches",
		     "scenarioSourceAiGraphExecuteTryAttackAmount",
	     }) {
		REQUIRE(scenario_runtime.find(symbol) != std::string::npos);
	}
	{
		const std::string destroy_object =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteDestroyObject");
		REQUIRE(destroy_object.find("OBJFLAG_01000000") !=
		        std::string::npos);
		REQUIRE(destroy_object.find("OBJHFLAG_DELETING") !=
		        std::string::npos);
		REQUIRE(destroy_object.find("OBJH2FLAG_DESTROYED") !=
		        std::string::npos);
		REQUIRE(destroy_object.find("objectiveRecordObjectState(obj)") !=
		        std::string::npos);
	}
	{
		const std::string set_door_open = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetDoorOpen");
		REQUIRE(!set_door_open.empty());
		requireTokenOrder(set_door_open,
			"s_aiSetupSpawnGraphReady(\"set_door_open\"",
			"s_aiGraphRequireRuntimeObjectTagType(\"set_door_open\"");
		requireTokenOrder(set_door_open,
			"s_aiGraphRequireRuntimeObjectTagType(\"set_door_open\"",
			"obj = objFindByTagId(tag_id)");
		requireTokenOrder(set_door_open,
			"obj = objFindByTagId(tag_id)",
			"door->lastopen60 = g_Vars.lvframe60");
		requireTokenOrder(set_door_open,
			"door->lastopen60 = g_Vars.lvframe60",
			"AI action set_door_open tag=%d object_rows=%d applied=%d");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.spatial_perception+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.distance_perception+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.room_object_weapon+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "backend=graph.ai.condition.spatial_perception+ai/ailists.json+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "backend=graph.ai.condition.room_object_weapon+ai/ailists.json+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI condition if_room_is_on_screen chr_rows=%d source_chr=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI condition if_chr_in_room chr=%d room_type=%d pad=%d found=%d pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI condition if_target_in_room pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfRoomIsOnScreen");
		REQUIRE(!block.empty());
		REQUIRE(block.find(
			        "s_aiGraphRequireRuntimePad(\"if_room_is_on_screen\"") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePad(\"if_room_is_on_screen\"",
			"chrGetPadRoom(chr, pad)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrInRoom");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimePad(\"if_chr_in_room\"") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.players[i]->eyespy") ==
		        std::string::npos);
		REQUIRE(block.find(
			        "AI condition if_chr_in_room chr=%d room_type=%d pad=%d found=%d pad_rows=%d checked_players=%d") !=
		        std::string::npos);
		REQUIRE(block.find("stageGetIndex(g_Vars.stagenum) == STAGEINDEX_G5BUILDING") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePad(\"if_chr_in_room\"",
			"chrGetPadRoom(basechr, padnum)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePad(\"if_chr_in_room\"",
			"stageGetIndex(g_Vars.stagenum) == STAGEINDEX_G5BUILDING");
		requireTokenOrder(block,
			"stageGetIndex(g_Vars.stagenum) == STAGEINDEX_G5BUILDING",
			"s_aiGraphRequireRuntimePlayerSlot(\"if_chr_in_room\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"if_chr_in_room\"",
			"player->eyespy");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"if_chr_in_room\"",
			"chrGetDistanceToPad(");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfTargetInRoom");
		REQUIRE(!block.empty());
		REQUIRE(block.find(
			        "s_aiGraphRequireRuntimePad(\"if_target_in_room\"") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePad(\"if_target_in_room\"",
			"chrGetPadRoom(chr, padnum)");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.object_interaction+ai/ailists.json+objects.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeObjectTag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countRuntimeSetupObjectRows") !=
	        std::string::npos);
	REQUIRE(countOccurrences(scenario_runtime,
		        "if (!s_ActiveScenarioGraphs.objects_path[0]) {\n"
		        "\t\ts_aiGraphRuntimeFailure(action, \"missing objects.json source\");\n"
		        "\t\treturn 0;\n"
		        "\t}\n\n"
		        "\tobject_count = s_countRuntimeSetupObjectRows();") >= 1);
	REQUIRE(scenario_runtime.find("missing source-derived runtime setup object table") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("runtime setup object tag %d does not point at a source row") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimePad(\"object_move_to_pad\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action object_move_to_pad tag=%d object_rows=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.quadrant_preset+ai/ailists.json+pads.json+navigation.generate") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeNavigationTables") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countRuntimeWaypoints") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countRuntimeWaygroups") !=
	        std::string::npos);
	{
		const std::string count_paths =
			functionBlock(scenario_runtime, "s_countRuntimePaths");
		const std::string count_pads =
			functionBlock(scenario_runtime, "s_countRuntimePads");
		const std::string count_covers =
			functionBlock(scenario_runtime, "s_countRuntimeCovers");
		const std::string count_objects =
			functionBlock(scenario_runtime, "s_countRuntimeSetupObjectRows");
		const std::string count_chrs =
			functionBlock(scenario_runtime, "s_countRuntimeSetupChrRows");
		const std::string source_chr_refs =
			functionBlock(scenario_runtime, "s_activeGraphSetSourceSetupChrRefs");
		const std::string contains_source_record =
			functionBlock(scenario_runtime, "s_sourceSetupContainsRecordOrder");
		const std::string contains_object =
			functionBlock(scenario_runtime, "s_runtimeSetupContainsObjectRow");
		const std::string contains_chr =
			functionBlock(scenario_runtime, "s_runtimeSetupContainsChrnum");
		const std::string contains_source_chr =
			functionBlock(scenario_runtime, "s_aiGraphSetupSourceContainsChrnum");
		const std::string count_waypoints =
			functionBlock(scenario_runtime, "s_countRuntimeWaypoints");
		const std::string count_waygroups =
			functionBlock(scenario_runtime, "s_countRuntimeWaygroups");
		REQUIRE(!count_paths.empty());
		REQUIRE(!count_pads.empty());
		REQUIRE(!count_covers.empty());
		REQUIRE(!count_objects.empty());
		REQUIRE(!count_chrs.empty());
		REQUIRE(!source_chr_refs.empty());
		REQUIRE(!contains_source_record.empty());
		REQUIRE(!contains_object.empty());
		REQUIRE(!contains_chr.empty());
		REQUIRE(!contains_source_chr.empty());
		REQUIRE(!count_waypoints.empty());
		REQUIRE(!count_waygroups.empty());
		REQUIRE(count_paths.find("if (!g_StageSetup.paths)") !=
		        std::string::npos);
		REQUIRE(count_paths.find("g_StageSetup.paths[count].pads") !=
		        std::string::npos);
		REQUIRE(count_pads.find("!g_StageSetup.padfiledata") !=
		        std::string::npos);
		REQUIRE(count_pads.find("g_PadsFile->numpads <= 0") !=
		        std::string::npos);
		REQUIRE(count_pads.find("return g_PadsFile->numpads;") !=
		        std::string::npos);
		REQUIRE(count_covers.find("!g_StageSetup.padfiledata") !=
		        std::string::npos);
		REQUIRE(count_covers.find("!g_StageSetup.cover") !=
		        std::string::npos);
		REQUIRE(count_covers.find("g_PadsFile->numcovers <= 0") !=
		        std::string::npos);
		REQUIRE(count_covers.find("return g_PadsFile->numcovers;") !=
		        std::string::npos);
		REQUIRE(count_objects.find("g_StageSetup.props") !=
		        std::string::npos);
		REQUIRE(count_objects.find("obj->type != OBJTYPE_END") !=
		        std::string::npos);
		REQUIRE(count_chrs.find("g_StageSetup.props") !=
		        std::string::npos);
		REQUIRE(count_chrs.find("obj->type == OBJTYPE_CHR") !=
		        std::string::npos);
		REQUIRE(source_chr_refs.find("source_setup_chr_count++") !=
		        std::string::npos);
		REQUIRE(source_chr_refs.find("source_setup_chr_refs") !=
		        std::string::npos);
		REQUIRE(source_chr_refs.find("source_setup_record_count++") !=
		        std::string::npos);
		REQUIRE(source_chr_refs.find("source_setup_record_refs") !=
		        std::string::npos);
		REQUIRE(contains_source_record.find("source_setup_record_refs") !=
		        std::string::npos);
		REQUIRE(contains_object.find("g_StageSetup.props") !=
		        std::string::npos);
		REQUIRE(contains_object.find("if (obj == needle)") !=
		        std::string::npos);
		REQUIRE(contains_chr.find("g_StageSetup.props") !=
		        std::string::npos);
		REQUIRE(contains_chr.find("packed->chrnum == chrnum") !=
		        std::string::npos);
		REQUIRE(contains_source_chr.find("s_sourceSetupContainsChrnum(chrnum)") !=
		        std::string::npos);
		REQUIRE(contains_source_chr.find("s_runtimeSetupContainsChrnum(chrnum)") !=
		        std::string::npos);
		REQUIRE(count_waypoints.find("if (!g_StageSetup.waypoints)") !=
		        std::string::npos);
		REQUIRE(count_waypoints.find(
			        "g_StageSetup.waypoints[count].padnum >= 0") !=
		        std::string::npos);
		REQUIRE(count_waygroups.find("if (!g_StageSetup.waygroups)") !=
		        std::string::npos);
		REQUIRE(count_waygroups.find("g_StageSetup.waygroups[count].waypoints") !=
		        std::string::npos);
		REQUIRE(count_waygroups.find("g_StageSetup.waygroups[count].neighbours") !=
		        std::string::npos);
	}
	REQUIRE(scenario_runtime.find("missing source-derived runtime waypoint table") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing source-derived runtime waygroup table") !=
	        std::string::npos);
	{
		const std::string nav_tables =
			functionBlock(scenario_runtime,
				"s_aiGraphRequireRuntimeNavigationTables");
		REQUIRE(!nav_tables.empty());
		requireTokenOrder(nav_tables,
			"missing pads.json source", "s_countRuntimePads()");
		requireTokenOrder(nav_tables,
			"missing navigation/waypoints.json source",
			"s_countRuntimeWaypoints()");
		requireTokenOrder(nav_tables,
			"missing navigation/waygroups.json source",
			"s_countRuntimeWaygroups()");
	}
	REQUIRE(scenario_runtime.find("pad_rows=%d waypoint_rows=%d waygroup_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_waypoint_within_quadrant quadrant=%d label=%d pad=%d found=%d result=%d pad_rows=%d waypoint_rows=%d waygroup_rows=%d") !=
	        std::string::npos);
	{
		const std::string quadrant_condition =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteIfWaypointWithinQuadrant");
		REQUIRE(!quadrant_condition.empty());
		REQUIRE(quadrant_condition.find("s_aiGraphRequireRuntimePad(\n\t\t\t\t\"if_waypoint_within_quadrant\"") !=
		        std::string::npos);
		requireTokenOrder(quadrant_condition,
			"func0f04a4ec(chr, (u8)quadrant)",
			"s_aiGraphRequireRuntimePad(");
		requireTokenOrder(quadrant_condition,
			"s_aiGraphRequireRuntimePad(",
			"s_aiGraphApplyBranch(branch_taken, label, 4)");
	}
	REQUIRE(scenario_runtime.find("AI action set_pad_preset_to_target_quadrant quadrant=%d label=%d pad=%d found=%d result=%d pad_rows=%d waypoint_rows=%d waygroup_rows=%d") !=
	        std::string::npos);
	{
		const std::string quadrant_block =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteSetPadPresetToTargetQuadrant");
		REQUIRE(!quadrant_block.empty());
		REQUIRE(quadrant_block.find("s_aiGraphRequireRuntimePad(\n\t\t\t\t\"set_pad_preset_to_target_quadrant\"") !=
		        std::string::npos);
		requireTokenOrder(quadrant_block,
			"chrSetPadPresetToWaypointWithinTargetQuadrant",
			"s_aiGraphRequireRuntimePad(");
		requireTokenOrder(quadrant_block,
			"s_aiGraphRequireRuntimePad(",
			"s_aiGraphApplyBranch(branch_taken, label, 4)");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_weapon_state+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_cutscene+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.setup_spawn+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.entity_lifecycle+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.release_cover+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.orders+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.intent_status+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrDoAnimation") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteBeSurprisedOneHand") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteBeSurprisedLookAround") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteBeSurprisedSurrender") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.animation+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_do_animation chr=%d chr_rows=%d target_chr=%d player_checked=%d anim_id=%s anim_source=%s clip_bytes=%u target=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action be_surprised_one_hand chr=%d chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action be_surprised_look_around chr=%d chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action be_surprised_surrender chr=%d chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_list target=%d chr_rows=%d target_chr=%d list=%u") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_return_list target=%d chr_rows=%d source_chr=%d target_chr=%d vehicle_rows=%d source_vehicle_type=%d list=%u") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countSourceBackedMpParticipantChrs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("level_global_settings_kind,\n\t\t\t\t\"mp\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("g_Vars.normmplayerisrunning && g_MpNumChrs > 0") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("g_MpAllChrPtrs[i] == chr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("chr_count = s_ActiveScenarioGraphs.source_setup_chr_count") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("chr_count +=\n\t\ts_countSourceBackedMpParticipantChrs()") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_shot_list chr_rows=%d source_chr=%d list=%u applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action return_list chr_rows=%d source_chr=%d vehicle_rows=%d source_vehicle_type=%d list=%u") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrDoAnimation");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(") == std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		REQUIRE(block.find("g_Vars.players[playernum]") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveAnimationCatalogId(\"chr_do_animation\"",
			"g_CutsceneFrameOverrun240 * speed * 0.25f");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"g_CutsceneFrameOverrun240 * speed * 0.25f");
		requireTokenOrder(block,
			"playerCurrentCutsceneInProgress()",
			"g_CutsceneFrameOverrun240 * speed * 0.25f");
		requireTokenOrder(block,
			"g_CutsceneFrameOverrun240 * speed * 0.25f",
			"AI action chr_do_animation chr=%d chr_rows=%d target_chr=%d player_checked=%d anim_id=%s anim_source=%s clip_bytes=%u");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"playermgrGetPlayerNumByProp",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"chrTryStartAnim(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"player->vv_ground");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"player->vv_manground");
	}
	{
		const char *actions[][2] = {
			{"scenarioSourceAiGraphExecuteBeSurprisedOneHand",
				"chrTrySurprisedOneHand("},
			{"scenarioSourceAiGraphExecuteBeSurprisedLookAround",
				"chrTrySurprisedLookAround("},
			{"scenarioSourceAiGraphExecuteBeSurprisedSurrender",
				"chrTrySurprisedSurrender("},
		};

		for (const auto &action : actions) {
			const std::string block = functionBlock(scenario_runtime,
				action[0]);
			REQUIRE(!block.empty());
			REQUIRE(block.find("chr_rows=%d source_chr=%d") !=
			        std::string::npos);
			requireTokenOrder(block,
				"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
				action[1]);
		}
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetList");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(") == std::string::npos);
		REQUIRE(block.find("ailistFindById(list_id)") !=
		        std::string::npos);
		REQUIRE(block.find("target_preset & 0xff") != std::string::npos);
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireListControlNode(\"set_list\"",
			"ailistFindById(list_id)");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"chr->ailist = ailist");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetReturnList");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(") == std::string::npos);
		REQUIRE(block.find("target_preset == CHR_SELF") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireOptionalRuntimeCharacterPointer(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.chrdata->") == std::string::npos);
		REQUIRE(block.find("g_Vars.truck->") == std::string::npos);
		REQUIRE(block.find("g_Vars.heli->") == std::string::npos);
		REQUIRE(block.find("g_Vars.hovercar->") == std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"active_chr->aireturnlist = list_id");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"if (chr && chr->prop) {\n\t\t\t\tchr->aireturnlist = list_id");
		requireTokenOrder(block,
			"OBJTYPE_TRUCK, -1, &vehicle_count",
			"truck->aireturnlist = list_id");
		requireTokenOrder(block,
			"OBJTYPE_HELI, -1, &vehicle_count",
			"heli->aireturnlist = list_id");
		requireTokenOrder(block,
			"OBJTYPE_HOVERCAR, OBJTYPE_CHOPPER, &vehicle_count",
			"hovercar->aireturnlist = list_id");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteReturnList");
		REQUIRE(!block.empty());
		REQUIRE(block.find("ailistFindById(list_id)") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.chrdata->") == std::string::npos);
		REQUIRE(block.find("g_Vars.truck->") == std::string::npos);
		REQUIRE(block.find("g_Vars.heli->") == std::string::npos);
		REQUIRE(block.find("g_Vars.hovercar->") == std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireListControlNode(\"return_list\"",
			"ailistFindById(list_id)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(\"return_list\"",
			"list_id = chr->aireturnlist");
		requireTokenOrder(block,
			"OBJTYPE_TRUCK,",
			"list_id = (u16)truck->aireturnlist");
		requireTokenOrder(block,
			"OBJTYPE_HELI,",
			"list_id = (u16)heli->aireturnlist");
		requireTokenOrder(block,
			"OBJTYPE_HOVERCAR, OBJTYPE_CHOPPER",
			"list_id = (u16)hovercar->aireturnlist");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetShotList");
		REQUIRE(!block.empty());
		REQUIRE(block.find("g_Vars.chrdata->") == std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(\"set_shot_list\"",
			"chr->aishotlist = list_id");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"static s32 s_aiGraphExecuteSetChrListField");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s16 *field, u16 list_id") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(action, chr",
			"field = (s16 *)((u8 *)chr + field_offset)");
		requireTokenOrder(block,
			"field = (s16 *)((u8 *)chr + field_offset)",
			"*field = (s16)list_id");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteSetPunchDodgeList",
		     "scenarioSourceAiGraphExecuteSetShootingAtMeList",
		     "scenarioSourceAiGraphExecuteSetDarkRoomList",
		     "scenarioSourceAiGraphExecuteSetPlayerDeadList",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("&g_Vars.chrdata->") == std::string::npos);
		REQUIRE(block.find("offsetof(struct chrdata") !=
		        std::string::npos);
	}
	REQUIRE(scenario_runtime.find("AI action chr_set_listening chr=%d chr_rows=%d target_chr=%d value=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_listening chr=%d chr_rows=%d target_chr=%d listening=%d check_convtalk=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_injured_target chr=%d chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_ammo_quantity_less_than chr=%d chr_rows=%d target_chr=%d player_checked=%d ammo_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_target chr=%d chr_rows=%d resolved_chr=%d target_chr=%d target_chr_rows=%d resolved_target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_compare_chr_presets_team chr_rows=%d preset_chr=%d comparison=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_human chr=%d chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrAmmoQuantityLessThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"bgunGetAmmoCount(");
	}
	REQUIRE(scenario_runtime.find("AI condition if_skedar chr=%d chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_prop_preset_blocking_sight_to_target chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action remove_object_at_prop_preset chr_rows=%d source_chr=%d cleared=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_prop_preset_height_less_than chr_rows=%d source_chr=%d value=%.3f height=%.3f") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_target chr_rows=%d source_chr=%d target_chr_rows=%d target_chr=%d resolved_target_chr=%d vehicle_rows=%d source_vehicle_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_presets_target_is_not_my_target chr_rows=%d source_chr=%d preset_target=%d target=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_chr_preset_to_chr_near_self chr_rows=%d source_chr=%d preset=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_chr_preset_to_chr_near_pad chr_rows=%d source_chr=%d preset=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_dangerous_object_nearby chr_rows=%d source_chr=%d flags=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_heli_weapons_armed vehicle_rows=%d source_vehicle_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action %s vehicle_rows=%d source_vehicle_type=%d armed=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action hovercar_begin_path path=%d found=1 vehicle_rows=%d truck_type=%d hovercar_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_vehicle_speed vehicle_rows=%d truck_type=%d hovercar_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_rotor_speed vehicle_rows=%d source_vehicle_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing objects.json source for vehicle weapon state") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("runtime chopper pointer is not source-derived") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("expected source type %u for chopper weapon state") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing objects.json source for vehicle state") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("runtime vehicle pointer is not source-derived") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("expected source vehicle type") !=
	        std::string::npos);
	for (const char *symbol : {
		     "scenarioSourceAiGraphExecuteChrSetListening",
		     "scenarioSourceAiGraphExecuteIfChrListening",
		     "scenarioSourceAiGraphExecuteIfChrInjuredTarget",
		     "scenarioSourceAiGraphExecuteIfChrAmmoQuantityLessThan",
		     "scenarioSourceAiGraphExecuteIfChrTarget",
		     "scenarioSourceAiGraphExecuteIfCompareChrPresetsTeam",
		     "scenarioSourceAiGraphExecuteIfHuman",
		     "scenarioSourceAiGraphExecuteIfSkedar",
	     }) {
		const std::string block = functionBlock(scenario_runtime, symbol);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(") == std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPropPresetIsBlockingSightToTarget");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chrIsPropPresetBlockingSightToTarget(chr)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteRemoveObjectAtPropPreset");
		REQUIRE(!block.empty());
		REQUIRE(block.find("objects=%s backend=graph.ai.action.prop_target+ai/ailists.json+objects.json") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequirePropPresetTargetNode(",
			"struct prop *prop = &g_Vars.props[chr->proppreset1];");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr->proppreset1 >= 0");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"struct prop *prop = &g_Vars.props[chr->proppreset1];");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr->proppreset1 = -1");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPropPresetHeightLessThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("objects=%s backend=graph.ai.condition.prop_target+ai/ailists.json+objects.json") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequirePropPresetTargetNode(",
			"struct prop *prop = &g_Vars.props[chr->proppreset1];");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr->proppreset1 >= 0");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"struct prop *prop = &g_Vars.props[chr->proppreset1];");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"propGetBbox(prop");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetTarget");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(\"set_target\"",
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(\"set_target\"",
			"if (newtarget != chr->target)");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(\"set_target\"",
			"goto log_result;");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(\n\t\t\t\t\"set_target\", &hovercar->base",
			"chopperSetTarget(hovercar, chrnum)");
		REQUIRE(block.find("vehicle_rows=%d source_vehicle_type=%d") !=
		        std::string::npos);
		REQUIRE(block.find("objects=%s backend=%s") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPresetsTargetIsNotMyTarget");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr->chrpreset1 != -1");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"branch_taken = chr && chr->target");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrPresetToChrNearSelf");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chrSetChrPresetToChrNearSelf");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrPresetToChrNearPad");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePad(",
			"s_aiGraphRequireRuntimeCharacterPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chrSetChrPresetToChrNearPad");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfDangerousObjectNearby");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chrDetectDangerousObject(chr");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetListening");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->listening = (u8)listening");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrListening");
		REQUIRE(block.find("if (!check_convtalk)") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->listening == listening");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrInjuredTarget");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->chrflags &= ~CHRCFLAG_INJUREDTARGET");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrAmmoQuantityLessThan");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"playermgrGetPlayerNumByProp");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrTarget");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrGetTargetProp(chr)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfCompareChrPresetsTeam");
		REQUIRE(block.find("chrSetChrPreset(chr, CHR_BOND)") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrCompareTeams(preset_chr, chr, comparison)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfHuman");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"CHRRACE(chr) == RACE_HUMAN");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfSkedar");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"CHRRACE(chr) == RACE_SKEDAR");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetTarget");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"if (!target_chr)");
		requireTokenOrder(block,
			"if (!target_chr)",
			"propGetIndexByChrId(chr, chrnum)");
		REQUIRE(block.find("newtarget = target_chr ? target_chr->target : -1") !=
		        std::string::npos);
		REQUIRE(block.find("chopperSetTarget(hovercar, chrnum)") !=
		        std::string::npos);
	}
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeCharacterPointer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_target_moving_slowly chr=%d chr_rows=%d target_chr=%d delta=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_y chr=%d chr_rows=%d target_chr=%d vehicle_rows=%d source_vehicle_type=%d y=%.3f") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_explosions chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_adjust_motion_blur chr=%d chr_rows=%d target_chr=%d amount=%d mode=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_emit_sparks chr=%d chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_dr_caroll_images chr=%d chr_rows=%d target_chr=%d right=%d left=%d") !=
	        std::string::npos);
	for (const char *symbol : {
		     "scenarioSourceAiGraphExecuteIfTargetMovingSlowly",
		     "scenarioSourceAiGraphExecuteIfY",
		     "scenarioSourceAiGraphExecuteChrExplosions",
		     "scenarioSourceAiGraphExecuteChrAdjustMotionBlur",
		     "scenarioSourceAiGraphExecuteChrEmitSparks",
		     "scenarioSourceAiGraphExecuteSetDrCarollImages",
	     }) {
		const std::string block = functionBlock(scenario_runtime, symbol);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(") == std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetDrCarollImages");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireMiscEffectNode(\"set_dr_caroll_images\"",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"g_Vars.lvframenum % 4");
		requireTokenOrder(block,
			"g_Vars.lvframenum % 4",
			"AI action set_dr_caroll_images chr=%d chr_rows=%d target_chr=%d");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfY");
		REQUIRE(!block.empty());
		REQUIRE(block.find("struct chopperobj *hovercar = g_Vars.hovercar;") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.hovercar->") == std::string::npos);
		REQUIRE(block.find("chopperFromHovercar(g_Vars.hovercar)") ==
		        std::string::npos);
		REQUIRE(block.find("vehicle_rows=%d source_vehicle_type=%d") !=
		        std::string::npos);
		REQUIRE(block.find("objects=%s backend=%s") != std::string::npos);
		requireTokenOrder(block,
			"struct chopperobj *hovercar = g_Vars.hovercar;",
			"s_aiGraphRequireRuntimeVehicleObjectPointer(\"if_y\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(\"if_y\"",
			"chopperFromHovercar(hovercar)");
		requireTokenOrder(block,
			"chopperFromHovercar(hovercar)",
			"chopperGetTargetProp(chopper)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfTargetMovingSlowly");
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterPointer(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrGetDistanceLostToTargetInLastSecond(chr)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfY");
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(\"if_y\"") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(\"if_y\"",
			"chr->prop->pos.y");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrExplosions");
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimePlayerSlot(\"chr_explosions\"") !=
		        std::string::npos);
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"playermgrGetPlayerNumByProp");
		requireTokenOrder(block,
			"playermgrGetPlayerNumByProp",
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_explosions\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_explosions\"",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_explosions\"",
			"playerSurroundWithExplosions");
	}
	{
		const std::string misc_effect_helper = functionBlock(scenario_runtime,
			"s_aiGraphRequireMiscEffectNode");
		REQUIRE(!misc_effect_helper.empty());
		REQUIRE(misc_effect_helper.find("missing ai/ailists.json source") !=
		        std::string::npos);
		const std::string set_tinted_glass = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteSetTintedGlassEnabled");
		REQUIRE(!set_tinted_glass.empty());
		REQUIRE(set_tinted_glass.find(
			        "AI action set_tinted_glass_enabled enabled=%d") !=
		        std::string::npos);
		requireTokenOrder(set_tinted_glass,
			"s_aiGraphRequireMiscEffectNode(\"set_tinted_glass_enabled\"",
			"g_TintedGlassEnabled = enabled");
		requireTokenOrder(set_tinted_glass,
			"g_TintedGlassEnabled = enabled",
			"AI action set_tinted_glass_enabled enabled=%d");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrAdjustMotionBlur");
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->blurdrugamount");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrEmitSparks");
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrDrCarollEmitSparks(chr)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetDrCarollImages");
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"drcaroll->drcarollimage_left");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"drcaroll->drcarollimage_right");
	}
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: pad source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.pads+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.jog_to_pad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.go_to_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.walk_to_pad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.run_to_pad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteWalkToPad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRunToPad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.jog_to_pad+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.go_to_pad_preset+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.walk_to_pad+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.run_to_pad+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action %s chr_rows=%d source_chr=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: path source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.navigation.paths+navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_path") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.start_patrol") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_start_alarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.activate_alarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.deactivate_alarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_copy_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_chr_preset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_chr_target") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_morale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.add_morale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_add_morale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.subtract_morale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.add_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_add_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.subtract_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.increase_squadron_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_hear_distance") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_view_distance") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_grenade_probability") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_chr_num") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_max_damage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.add_health") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_shield") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_reaction_speed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_recovery_speed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_accuracy") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_dodge_rating") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_unarmed_dodge_rating") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.unset_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_has_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_unset_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_chr_has_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_stage_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.unset_stage_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_stage_flag_eq") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.unset_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_has_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_unset_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_chr_has_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_hidden_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_unset_hidden_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_chr_has_hidden_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_obj_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.unset_obj_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_obj_has_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.open_door") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.close_door") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_door_state") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_object_is_door") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.lock_door") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.unlock_door") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_door_locked") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_lift_stationary") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.lift_go_to_stop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_lift_at_stop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.activate_lift") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_using_lift") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.configure_rain") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.configure_snow") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.switch_to_alt_sky") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_wind_speed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_lights") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_room_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.show_cutscene_chrs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.configure_environment") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_distance_to_target2_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_distance_to_target2_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.play_sound_from_prop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.play_temporary_primary_track") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.play_x_track") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.stop_ambient_track") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_draw_weapon") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_draw_weapon_in_cutscene") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_player_force_speed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_invincible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_player_is_invincible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_has_no_gun") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_delete_weapon") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_trigger_shot_list") !=
	        std::string::npos);
	for (const char *node : {
		     "scenario.ai.action.end_level",
		     "scenario.ai.action.end_cutscene",
		     "scenario.ai.action.warp_jo_to_pad",
		     "scenario.ai.action.warp_jo_to_tag",
		     "scenario.ai.action.revoke_control",
		     "scenario.ai.action.grant_control",
		     "scenario.ai.action.player_fade_in",
		     "scenario.ai.action.players_fade_out",
		     "scenario.ai.condition.if_colour_fade_complete",
		     "scenario.ai.action.prepare_warp_orbit",
		     "scenario.ai.action.begin_warp_latch",
		     "scenario.ai.condition.if_warp_latch_complete",
		     "scenario.ai.action.set_camera_animation",
		     "scenario.ai.condition.if_in_cutscene",
		     "scenario.ai.condition.if_cutscene_button_pressed",
		     "scenario.ai.action.reorient_for_cutscene_stop",
		     "scenario.ai.action.duplicate_chr",
		     "scenario.ai.action.enable_chr",
		     "scenario.ai.action.disable_chr",
		     "scenario.ai.action.enable_obj",
		     "scenario.ai.action.disable_obj",
		     "scenario.ai.action.chr_move_to_pad",
		     "scenario.ai.action.chr_set_team",
		     "scenario.ai.action.damage_chr_by_amount",
		     "scenario.ai.action.do_preset_animation",
		     "scenario.ai.condition.if_player_chr_portal_distance_less_than",
		     "scenario.ai.condition.if_chr_reposition_valid",
	}) {
		REQUIRE(scenario_runtime.find(node) != std::string::npos);
	}
	REQUIRE(scenario_runtime.find(
		        "AI action prepare_warp_orbit pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action set_camera_animation anim_id=%s anim_source=%s clip_bytes=%u player_checked=%d chr_rows=%d source_chr=%d yielded=1") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action set_camera_animation anim_id=%s anim_source=%s clip_bytes=%u player_checked=%d chr_rows=%d source_chr=%d yielded=0") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action players_fade_out checked_players=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action player_fade_in chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action revoke_control chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action grant_control chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI condition if_colour_fade_complete chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action enable_obj tag=%d object_rows=%d player_checked=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action clear_inventory checked_players=%d players=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action kill_bond player_checked=1 applied=1") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfCutsceneButtonPressed");
		REQUIRE(!block.empty());
		REQUIRE(block.find("g_Vars.stagenum == STAGE_CITRAINING") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiPlayerCutsceneGraphReady(\"if_cutscene_button_pressed\"",
			"g_Vars.stagenum == STAGE_CITRAINING");
		requireTokenOrder(block,
			"playerAnyCutsceneSkipRequested()",
			"g_Vars.stagenum == STAGE_CITRAINING");
	}
	{
		const std::string end_level = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteEndLevel");
		REQUIRE(!end_level.empty());
		requireTokenOrder(end_level,
			"s_aiPlayerCutsceneGraphReady(\"end_level\"",
			"if (g_Vars.autocutplaying)");
		requireTokenOrder(end_level,
			"if (g_Vars.autocutplaying)",
			"g_Vars.autocutfinished = true");
		requireTokenOrder(end_level,
			"g_Vars.autocutfinished = true",
			"AI action end_level autocut=%d");
	}
	{
		const std::string disable_obj = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteDisableObj");
		REQUIRE(!disable_obj.empty());
		requireTokenOrder(disable_obj,
			"s_aiEntityLifecycleGraphReady(\"disable_obj\"",
			"s_aiGraphRequireRuntimeObjectTag(\"disable_obj\"");
		requireTokenOrder(disable_obj,
			"s_aiGraphRequireRuntimeObjectTag(\"disable_obj\"",
			"obj = objFindByTagId(tag_id)");
		requireTokenOrder(disable_obj,
			"obj = objFindByTagId(tag_id)",
			"if (g_Vars.autocutplaying &&");
		requireTokenOrder(disable_obj,
			"if (g_Vars.autocutplaying &&",
			"AI action disable_obj tag=%d object_rows=%d applied=%d");
	}
	{
		const std::string player_pointer = functionBlock(
			scenario_runtime,
			"s_aiGraphRequireRuntimePlayerPointer");
		REQUIRE(!player_pointer.empty());
		REQUIRE(player_pointer.find("missing %s player state") !=
		        std::string::npos);
		REQUIRE(player_pointer.find("PROPTYPE_PLAYER") !=
		        std::string::npos);
	}
	{
		const std::string set_camera_animation = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteSetCameraAnimation");
		REQUIRE(!set_camera_animation.empty());
		REQUIRE(set_camera_animation.find("g_Vars.currentplayer->haschrbody") ==
		        std::string::npos);
		REQUIRE(set_camera_animation.find(
			        "s_aiGraphRequireRuntimePlayerPointer(\"set_camera_animation\"") !=
		        std::string::npos);
		REQUIRE(set_camera_animation.find("player, \"current\"") !=
		        std::string::npos);
		REQUIRE(set_camera_animation.find(
			        "s_aiGraphRequireRuntimeCharacterStatePointer(\"set_camera_animation\"") !=
		        std::string::npos);
		REQUIRE(set_camera_animation.find("g_Vars.chrdata->sleep") ==
		        std::string::npos);
		REQUIRE(set_camera_animation.find(
			        "struct chrdata *active_chr = g_Vars.chrdata;") !=
		        std::string::npos);
		requireTokenOrder(set_camera_animation,
			"s_aiGraphRequireRuntimePlayerPointer(\"set_camera_animation\"",
			"s_aiGraphRequireRuntimeCharacterStatePointer(\"set_camera_animation\"");
		requireTokenOrder(set_camera_animation,
			"s_aiGraphRequireRuntimeCharacterStatePointer(\"set_camera_animation\"",
			"playerStartCutscene(");
		requireTokenOrder(set_camera_animation,
			"s_aiGraphRequireRuntimeCharacterStatePointer(\"set_camera_animation\"",
			"active_chr->sleep = -1");
	}
	{
		const std::string players_fade_out = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecutePlayersFadeOut");
		REQUIRE(!players_fade_out.empty());
		requireTokenOrder(players_fade_out,
			"s_aiGraphRequireRuntimePlayerSlot(\"players_fade_out\"",
			"playerSetFadeColour(");
		requireTokenOrder(players_fade_out,
			"s_aiGraphRequireRuntimePlayerSlot(\"players_fade_out\"",
			"playerSetFadeFrac(");
	}
	{
		const std::string player_fade_in = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecutePlayerFadeIn");
		REQUIRE(!player_fade_in.empty());
		requireTokenOrder(player_fade_in,
			"s_aiGraphResolvePlayerChr(\"player_fade_in\"",
			"s_aiGraphRequireRuntimePlayerSlot(\"player_fade_in\"");
		requireTokenOrder(player_fade_in,
			"s_aiGraphRequireRuntimePlayerSlot(\"player_fade_in\"",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(player_fade_in,
			"s_aiGraphRequireRuntimePlayerSlot(\"player_fade_in\"",
			"playerSetFadeColour(");
		requireTokenOrder(player_fade_in,
			"s_aiGraphRequireRuntimePlayerSlot(\"player_fade_in\"",
			"playerSetFadeFrac(");
	}
	{
		const std::string revoke_control = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteRevokeControl");
		REQUIRE(!revoke_control.empty());
		REQUIRE(revoke_control.find("g_PlayersWithControl[g_Vars.currentplayernum]") ==
		        std::string::npos);
		requireTokenOrder(revoke_control,
			"s_aiGraphResolvePlayerChr(\"revoke_control\"",
			"s_aiGraphRequireRuntimePlayerSlot(\"revoke_control\"");
		requireTokenOrder(revoke_control,
			"s_aiGraphRequireRuntimePlayerSlot(\"revoke_control\"",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(revoke_control,
			"s_aiGraphRequireRuntimePlayerSlot(\"revoke_control\"",
			"g_PlayersWithControl[playernum] = false");
	}
	{
		const std::string grant_control = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteGrantControl");
		REQUIRE(!grant_control.empty());
		REQUIRE(grant_control.find("g_PlayersWithControl[g_Vars.currentplayernum]") ==
		        std::string::npos);
		requireTokenOrder(grant_control,
			"s_aiGraphResolvePlayerChr(\"grant_control\"",
			"s_aiGraphRequireRuntimePlayerSlot(\"grant_control\"");
		requireTokenOrder(grant_control,
			"s_aiGraphRequireRuntimePlayerSlot(\"grant_control\"",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(grant_control,
			"s_aiGraphRequireRuntimePlayerSlot(\"grant_control\"",
			"g_PlayersWithControl[playernum] = true");
	}
	{
		const std::string if_colour_fade_complete = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteIfColourFadeComplete");
		REQUIRE(!if_colour_fade_complete.empty());
		REQUIRE(if_colour_fade_complete.find(
			        "g_Vars.players[playernum]->colourfadetimemax60") ==
		        std::string::npos);
		requireTokenOrder(if_colour_fade_complete,
			"s_aiGraphResolvePlayerChr(\"if_colour_fade_complete\"",
			"s_aiGraphRequireRuntimePlayerSlot(\"if_colour_fade_complete\"");
		requireTokenOrder(if_colour_fade_complete,
			"s_aiGraphRequireRuntimePlayerSlot(\"if_colour_fade_complete\"",
			"player->colourfadetimemax60");
	}
	{
		const std::string enable_obj = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteEnableObj");
		REQUIRE(!enable_obj.empty());
		REQUIRE(enable_obj.find("g_Vars.currentplayer->eyespy") ==
		        std::string::npos);
		REQUIRE(enable_obj.find(
			        "struct player *current_player = g_Vars.currentplayer;") !=
		        std::string::npos);
		REQUIRE(enable_obj.find("\n\t\t\t\tplayer = g_Vars.currentplayer;") ==
		        std::string::npos);
		requireTokenOrder(enable_obj,
			"s_aiGraphRequireRuntimeObjectTag(\"enable_obj\"",
			"s_aiGraphRequireRuntimePlayerPointer(");
		requireTokenOrder(enable_obj,
			"s_aiGraphRequireRuntimePlayerPointer(",
			"player = current_player");
		requireTokenOrder(enable_obj,
			"player = current_player",
			"propActivate(");
		requireTokenOrder(enable_obj,
			"player = current_player",
			"player->eyespy");
		requireTokenOrder(enable_obj,
			"player->eyespy",
			"playerInitEyespy(");
	}
	{
		const std::string prepare_warp_orbit = functionBlock(
			scenario_runtime, "scenarioSourceAiGraphExecutePrepareWarpOrbit");
		REQUIRE(!prepare_warp_orbit.empty());
		requireTokenOrder(prepare_warp_orbit,
			"s_aiGraphRequireRuntimePad(\"prepare_warp_orbit\"",
			"playerPrepareWarpType3");
	}
	REQUIRE(scenario_runtime.find(
		        "AI action chr_begin_or_end_teleport chr=%d chr_rows=%d target_chr=%d player=%d player_checked=%d pad=%d found=%d pad_rows=%d sound_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "backend=graph.ai.action.teleport_cutscene_weapon+ai/ailists.json+pads.json+audio") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI condition if_chr_teleport_full_white chr=%d chr_rows=%d target_chr=%d player=%d player_checked=%d label=%d branch=%d sound_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "backend=graph.ai.action.teleport_cutscene_weapon+ai/ailists.json+audio") !=
	        std::string::npos);
	{
		const std::string teleport_sound = functionBlock(
			scenario_runtime, "s_aiGraphPlayTeleportSound");
		REQUIRE(!teleport_sound.empty());
		requireTokenOrder(teleport_sound,
			"osGetThreadPri(0)",
			"osGetThreadPri(&g_AudioManager.thread)");
		requireTokenOrder(teleport_sound,
			"osGetThreadPri(&g_AudioManager.thread)",
			"osSetThreadPri(0, audiopri + 1)");
		requireTokenOrder(teleport_sound,
			"osSetThreadPri(0, audiopri + 1)",
			"sndStart(var80095200, soundnum");
		requireTokenOrder(teleport_sound,
			"sndStart(var80095200, soundnum",
			"osSetThreadPri(0, mainpri)");
	}
	{
		const std::string chr_begin_or_end_teleport = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteChrBeginOrEndTeleport");
		REQUIRE(!chr_begin_or_end_teleport.empty());
		REQUIRE(chr_begin_or_end_teleport.find(
			        "chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(chr_begin_or_end_teleport.find(
			        "s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(chr_begin_or_end_teleport.find(
			        "g_Vars.currentplayer->teleport") == std::string::npos);
		REQUIRE(chr_begin_or_end_teleport.find(
			        "pad_count = s_countRuntimePads();") ==
		        std::string::npos);
		requireTokenOrder(chr_begin_or_end_teleport,
			"s_aiGraphRequireRuntimePad(\"chr_begin_or_end_teleport\"",
			"s_aiGraphResolveAudioCatalogId(");
		requireTokenOrder(chr_begin_or_end_teleport,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_begin_or_end_teleport\"");
		requireTokenOrder(chr_begin_or_end_teleport,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_begin_or_end_teleport\"",
			"player->teleportpad = pad_id");
		requireTokenOrder(chr_begin_or_end_teleport,
			"s_aiGraphResolveAudioCatalogId(",
			"s_aiGraphPlayTeleportSound(SFX_RELOAD_FARSIGHT)");
		requireTokenOrder(chr_begin_or_end_teleport,
			"s_aiGraphPlayTeleportSound(SFX_RELOAD_FARSIGHT)",
			"sound_id=%s");
	}
	{
		const std::string if_chr_teleport_full_white = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrTeleportFullWhite");
		REQUIRE(!if_chr_teleport_full_white.empty());
		REQUIRE(if_chr_teleport_full_white.find(
			        "chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(if_chr_teleport_full_white.find(
			        "g_Vars.currentplayer->teleport") == std::string::npos);
		requireTokenOrder(if_chr_teleport_full_white,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(\"if_chr_teleport_full_white\"");
		requireTokenOrder(if_chr_teleport_full_white,
			"s_aiGraphRequireRuntimePlayerSlot(\"if_chr_teleport_full_white\"",
			"player->teleportstate");
		requireTokenOrder(if_chr_teleport_full_white,
			"s_aiGraphResolveAudioCatalogId(",
			"s_aiGraphPlayTeleportSound(SFX_FIRE_SHOTGUN)");
		requireTokenOrder(if_chr_teleport_full_white,
			"s_aiGraphPlayTeleportSound(SFX_FIRE_SHOTGUN)",
			"sound_id=%s");
	}
	REQUIRE(scenario_runtime.find("scenario.ai.action.do_gun_command") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_distance_to_gun_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.recover_gun") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_obj_in_room") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_player_looking_at_object") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_target_is_player") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_kill") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.remove_weapon_from_inventory") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.clear_inventory") !=
	        std::string::npos);
	{
		const std::string clear_inventory = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteClearInventory");
		REQUIRE(!clear_inventory.empty());
		requireTokenOrder(clear_inventory,
			"s_aiGraphRequireRuntimePlayerSlot(\"clear_inventory\"",
			"invClear(");
		requireTokenOrder(clear_inventory,
			"s_aiGraphRequireRuntimePlayerSlot(\"clear_inventory\"",
			"player->devicesactive = 0");
		REQUIRE(clear_inventory.find(
				"g_Vars.currentplayer->devicesactive = 0") ==
			std::string::npos);
		REQUIRE(clear_inventory.find("player == g_Vars.bond") ==
		        std::string::npos);
		REQUIRE(clear_inventory.find("player == g_Vars.coop") ==
		        std::string::npos);
		REQUIRE(clear_inventory.find("playernum == g_Vars.bondplayernum") !=
		        std::string::npos);
		REQUIRE(clear_inventory.find("playernum == g_Vars.coopplayernum") !=
		        std::string::npos);
		requireTokenOrder(clear_inventory,
			"s_aiGraphRequireRuntimePlayerSlot(\"clear_inventory\"",
			"playernum == g_Vars.bondplayernum");
		requireTokenOrder(clear_inventory,
			"setCurrentPlayerNum(playernum)",
			"playernum == g_Vars.bondplayernum");
		requireTokenOrder(clear_inventory,
			"s_aiGraphRequireRuntimePlayerSlot(\"clear_inventory\"",
			"bgunEquipWeapon(WEAPON_UNARMED)");
	}
	REQUIRE(scenario_runtime.find("scenario.ai.action.release_object") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_grab_object") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.toggle_p1p2") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_p1p2") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_cloaked") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_autogun_target_team") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_objective_complete") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_objective_failed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_all_objectives_complete") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_difficulty_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_difficulty_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_stage_timer_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_stage_timer_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_stage_id_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_stage_id_greater_than") !=
	        std::string::npos);
	const std::string stage_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveStageCatalogId");
	REQUIRE(stage_id_helper.find("catalogStageIdByStagenum") !=
	        std::string::npos);
	REQUIRE(stage_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteIfStageIdLessThan",
		     "scenarioSourceAiGraphExecuteIfStageIdGreaterThan",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("s_aiGraphResolveStageCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("stage_id=%s current_stage_id=%s") !=
		        std::string::npos);
		REQUIRE(block.find("stagenum=%d current=%d") ==
		        std::string::npos);
	}
	const std::string weapon_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveWeaponCatalogId");
	REQUIRE(weapon_id_helper.find("catalogWeaponIdByRuntimeWeaponNum") !=
	        std::string::npos);
	REQUIRE(weapon_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	REQUIRE(weapon_id_helper.find("WEAPON_NONE") != std::string::npos);
	const std::string body_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveBodyCatalogId");
	REQUIRE(body_id_helper.find("catalogBodyIdByBodynum") !=
	        std::string::npos);
	REQUIRE(body_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	const std::string head_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveHeadCatalogId");
	REQUIRE(head_id_helper.find("catalogHeadIdByHeadnum") !=
	        std::string::npos);
	REQUIRE(head_id_helper.find("HEAD_RANDOM") != std::string::npos);
	REQUIRE(head_id_helper.find("HEAD_RANDOM_GENDER") !=
	        std::string::npos);
	REQUIRE(head_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	const std::string model_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveModelCatalogId");
	REQUIRE(model_id_helper.find("catalogModelIdByModelnum") !=
	        std::string::npos);
	REQUIRE(model_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	const std::string weapon_modelnum_helper =
		functionBlock(scenario_runtime, "s_aiGraphWeaponModelNumOrNone");
	REQUIRE(weapon_modelnum_helper.find("s_aiGraphWeaponHasNoModelSentinel") !=
	        std::string::npos);
	REQUIRE(weapon_modelnum_helper.find("playermgrGetModelOfWeapon") !=
	        std::string::npos);
	requireTokenOrder(weapon_modelnum_helper,
		"s_aiGraphWeaponHasNoModelSentinel",
		"playermgrGetModelOfWeapon");
	const std::string weapon_model_sentinel_helper =
		functionBlock(scenario_runtime, "s_aiGraphWeaponHasNoModelSentinel");
	REQUIRE(weapon_model_sentinel_helper.find("weaponnum == 0xff") !=
	        std::string::npos);
	REQUIRE(weapon_model_sentinel_helper.find("WEAPON_NONE") !=
	        std::string::npos);
	REQUIRE(weapon_model_sentinel_helper.find("WEAPON_UNARMED") !=
	        std::string::npos);
	const std::string weapon_model_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveWeaponModelCatalogId");
	REQUIRE(weapon_model_id_helper.find("s_aiGraphResolveModelCatalogId") !=
	        std::string::npos);
	REQUIRE(weapon_model_id_helper.find("s_aiGraphWeaponHasNoModelSentinel") !=
	        std::string::npos);
	REQUIRE(weapon_model_id_helper.find("weaponnum == 0xff") ==
	        std::string::npos);
	REQUIRE(weapon_model_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	const std::string cutscene_weapon_block = functionBlock(
		scenario_runtime, "scenarioSourceAiGraphExecuteChrSetCutsceneWeapon");
	REQUIRE(cutscene_weapon_block.find("s_aiGraphResolveWeaponCatalogId") !=
	        std::string::npos);
	REQUIRE(cutscene_weapon_block.find("s_aiGraphResolveWeaponModelCatalogId") !=
	        std::string::npos);
	REQUIRE(cutscene_weapon_block.find("s_aiGraphWeaponModelNumOrNone") !=
	        std::string::npos);
	REQUIRE(cutscene_weapon_block.find("playermgrGetModelOfWeapon") ==
	        std::string::npos);
	REQUIRE(cutscene_weapon_block.find("chrFindById(basechr, chrnum);") ==
	        std::string::npos);
	REQUIRE(cutscene_weapon_block.find(
		        "s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
	        std::string::npos);
	REQUIRE(cutscene_weapon_block.find(
			"chr_rows=%d target_chr=%d weapon_id=%s model_id=%s fallback_weapon_id=%s fallback_model_id=%s") !=
	        std::string::npos);
	REQUIRE(cutscene_weapon_block.find("weapon=%d fallback_weapon=%d") ==
	        std::string::npos);
	requireTokenOrder(cutscene_weapon_block,
		"s_aiGraphRequireRuntimeCharacterRefFromBase(",
		"s_aiGraphResolveWeaponCatalogId");
	requireTokenOrder(cutscene_weapon_block,
		"s_aiGraphResolveWeaponCatalogId",
		"s_aiGraphResolveWeaponModelCatalogId");
	requireTokenOrder(cutscene_weapon_block,
		"s_aiGraphResolveWeaponModelCatalogId",
		"weaponCreateForChr");
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrDrawWeapon",
		     "scenarioSourceAiGraphExecuteChrDrawWeaponInCutscene",
		     "scenarioSourceAiGraphExecuteChrDeleteWeapon",
		     "scenarioSourceAiGraphExecuteRemoveWeaponFromInventory",
		     "scenarioSourceAiGraphExecuteIfPlayerUsingCmpOrAr34",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("s_aiGraphResolveWeaponCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("weapon_id=%s") != std::string::npos);
		REQUIRE(block.find("weapon=%d") == std::string::npos);
	}
	const std::string drop_item_block =
		functionBlock(scenario_runtime, "scenarioSourceAiGraphExecuteDropItem");
	REQUIRE(drop_item_block.find("s_aiGraphResolveModelCatalogId") !=
	        std::string::npos);
	REQUIRE(drop_item_block.find("s_aiGraphResolveWeaponCatalogId") !=
	        std::string::npos);
	REQUIRE(drop_item_block.find("model_id=%s weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(drop_item_block.find("model=%u weapon=%u") ==
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteSpawnChrAtPad",
		     "scenarioSourceAiGraphExecuteSpawnChrAtChr",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("s_aiGraphResolveBodyCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolveHeadCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRegisterSourceSpawnedChr(") !=
		        std::string::npos);
		REQUIRE(block.find("body_id=%s head_id=%s") !=
		        std::string::npos);
		REQUIRE(block.find("body=%d head=%d") == std::string::npos);
	}
	{
		const std::string spawn_at_pad =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteSpawnChrAtPad");
		REQUIRE(!spawn_at_pad.empty());
		REQUIRE(spawn_at_pad.find("ailistFindById(ailistid)") !=
		        std::string::npos);
		requireTokenOrder(spawn_at_pad,
			"s_aiSetupSpawnGraphReady(\"spawn_chr_at_pad\"",
			"ailistFindById(ailistid)");
		requireTokenOrder(spawn_at_pad,
			"ailistFindById(ailistid)",
			"chrSpawnAtPad(");
		requireTokenOrder(spawn_at_pad,
			"chrSpawnAtPad(",
			"s_aiGraphRegisterSourceSpawnedChr(\"spawn_chr_at_pad\"");
		requireTokenOrder(spawn_at_pad,
			"s_aiGraphRegisterSourceSpawnedChr(\"spawn_chr_at_pad\"",
			"g_Vars.aioffset = chraiGoToLabel");
	}
	{
		const std::string spawn_at_chr =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteSpawnChrAtChr");
		REQUIRE(!spawn_at_chr.empty());
		REQUIRE(spawn_at_chr.find("ailistFindById(ailistid)") !=
		        std::string::npos);
		requireTokenOrder(spawn_at_chr,
			"s_aiSetupSpawnGraphReady(\"spawn_chr_at_chr\"",
			"ailistFindById(ailistid)");
		requireTokenOrder(spawn_at_chr,
			"ailistFindById(ailistid)",
			"chrSpawnAtChr(");
		requireTokenOrder(spawn_at_chr,
			"chrSpawnAtChr(",
			"s_aiGraphRegisterSourceSpawnedChr(\"spawn_chr_at_chr\"");
		requireTokenOrder(spawn_at_chr,
			"s_aiGraphRegisterSourceSpawnedChr(\"spawn_chr_at_chr\"",
			"g_Vars.aioffset = chraiGoToLabel");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSpawnChrAtChr");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(\"spawn_chr_at_chr\"") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveHeadCatalogId(\"spawn_chr_at_chr\"",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrSpawnAtChr");
	}
	const std::string try_equip_weapon_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteTryEquipWeapon");
	REQUIRE(try_equip_weapon_block.find("s_aiGraphResolveModelCatalogId") !=
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find("s_aiGraphResolveWeaponCatalogId") !=
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find("model_id=%s weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find("g_Vars.chrdata->") ==
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find(
		        "struct chrdata *active_chr = g_Vars.chrdata;") !=
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find(
		        "s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"try_equip_weapon\"") !=
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find("chrGiveWeapon(active_chr") !=
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find("g_Vars.stagenum") !=
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find("chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	requireTokenOrder(try_equip_weapon_block,
		"s_aiGraphResolveWeaponCatalogId",
		"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"try_equip_weapon\"");
	requireTokenOrder(try_equip_weapon_block,
		"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"try_equip_weapon\"",
		"chrGiveWeapon(active_chr");
	requireTokenOrder(try_equip_weapon_block,
		"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"try_equip_weapon\"",
		"g_Vars.stagenum");
	requireTokenOrder(try_equip_weapon_block,
		"g_Vars.stagenum",
		"chrGiveWeapon(active_chr");
	REQUIRE(try_equip_weapon_block.find("model=%u weapon=%d") ==
	        std::string::npos);
	const std::string try_equip_hat_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteTryEquipHat");
	REQUIRE(try_equip_hat_block.find("s_aiGraphResolveModelCatalogId") !=
	        std::string::npos);
	REQUIRE(try_equip_hat_block.find("model_id=%s") !=
	        std::string::npos);
	REQUIRE(try_equip_hat_block.find("g_Vars.chrdata->") ==
	        std::string::npos);
	REQUIRE(try_equip_hat_block.find(
		        "struct chrdata *active_chr = g_Vars.chrdata;") !=
	        std::string::npos);
	REQUIRE(try_equip_hat_block.find(
		        "s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"try_equip_hat\"") !=
	        std::string::npos);
	REQUIRE(try_equip_hat_block.find("hatCreateForChr(active_chr") !=
	        std::string::npos);
	REQUIRE(try_equip_hat_block.find("chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	requireTokenOrder(try_equip_hat_block,
		"s_aiGraphResolveModelCatalogId",
		"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"try_equip_hat\"");
	requireTokenOrder(try_equip_hat_block,
		"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"try_equip_hat\"",
		"hatCreateForChr(active_chr");
	REQUIRE(try_equip_hat_block.find("model=%u") == std::string::npos);
	const std::string set_obj_image_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetObjImage");
	REQUIRE(set_obj_image_block.find("image_index=%d") !=
	        std::string::npos);
	REQUIRE(set_obj_image_block.find("image=%d") == std::string::npos);
	const std::string object_do_animation_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteObjectDoAnimation");
	REQUIRE(!object_do_animation_block.empty());
	REQUIRE(object_do_animation_block.find("g_Vars.chrdata->") ==
	        std::string::npos);
	REQUIRE(object_do_animation_block.find(
		        "struct chrdata *active_chr = g_Vars.chrdata;") !=
	        std::string::npos);
	REQUIRE(object_do_animation_block.find(
		        "s_aiGraphRequireRuntimeCharacterStatePointer(\n\t\t\t\t\t\"object_do_animation\"") !=
	        std::string::npos);
	REQUIRE(object_do_animation_block.find("resolved_tag_id = active_chr->myspecial;") !=
	        std::string::npos);
	REQUIRE(object_do_animation_block.find("chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	requireTokenOrder(object_do_animation_block,
		"tag_id == 255",
		"s_aiGraphRequireRuntimeCharacterStatePointer(");
	requireTokenOrder(object_do_animation_block,
		"s_aiGraphRequireRuntimeCharacterStatePointer(",
		"resolved_tag_id = active_chr->myspecial");
	requireTokenOrder(object_do_animation_block,
		"\"object_do_animation\", anim_id, \"object\"",
		"g_CutsceneFrameOverrun240 * speed *");
	requireTokenOrder(object_do_animation_block,
		"s_aiGraphRequireRuntimeObjectTag(\"object_do_animation\"",
		"g_CutsceneFrameOverrun240 * speed *");
	requireTokenOrder(object_do_animation_block,
		"playerCurrentCutsceneInProgress()",
		"g_CutsceneFrameOverrun240 * speed *");
	requireTokenOrder(object_do_animation_block,
		"g_CutsceneFrameOverrun240 * speed *",
		"AI action object_do_animation anim_id=%s anim_source=%s clip_bytes=%u tag=%d resolved_tag=%d object_rows=%d chr_rows=%d");
	const std::string audio_id_helper =
		functionBlock(scenario_runtime,
			"static const char *s_aiGraphResolveAudioCatalogId");
	REQUIRE(audio_id_helper.find("s_aiGraphNormalizeAudioRuntimeId") !=
	        std::string::npos);
	REQUIRE(audio_id_helper.find("catalogResolveSound") != std::string::npos);
	REQUIRE(audio_id_helper.find("assetCatalogGetByIndex") !=
	        std::string::npos);
	REQUIRE(audio_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	requireTokenOrder(audio_id_helper,
		"s_aiGraphNormalizeAudioRuntimeId(audio_id)",
		"catalogResolveSound(leaf_id)");
	const std::string audio_normalize_helper =
		functionBlock(scenario_runtime,
			"static s32 s_aiGraphNormalizeAudioRuntimeId");
	REQUIRE(audio_normalize_helper.find("union soundnumhack") !=
	        std::string::npos);
	REQUIRE(audio_normalize_helper.find("g_AudioRussMappings") !=
	        std::string::npos);
	REQUIRE(audio_normalize_helper.find("g_NumAudioRussMappings") !=
	        std::string::npos);
	REQUIRE(audio_normalize_helper.find(
		        "ref.hasconfig && ref.confignum < (u32)g_NumAudioRussMappings") !=
	        std::string::npos);
	requireTokenOrder(audio_normalize_helper,
		"ref.hasconfig && ref.confignum < (u32)g_NumAudioRussMappings",
		"g_AudioRussMappings[ref.confignum].soundnum");
	const std::string mp3_file_helper =
		functionBlock(scenario_runtime,
			"static s32 s_aiGraphResolveMp3FileNum");
	REQUIRE(mp3_file_helper.find("ref.mp3priority != 0") !=
	        std::string::npos);
	REQUIRE(mp3_file_helper.find("mapped.mp3priority != 0") !=
	        std::string::npos);
	REQUIRE(mp3_file_helper.find(
		        "g_AudioRussMappings[ref.confignum].soundnum") !=
	        std::string::npos);
	const std::string speech_audio_helper =
		functionBlock(scenario_runtime,
			"static const char *s_aiGraphResolveSpeechAudioSourceId");
	REQUIRE(speech_audio_helper.find("s_aiGraphResolveMp3FileNum") !=
	        std::string::npos);
	REQUIRE(speech_audio_helper.find("s_aiGraphResolveAudioCatalogId") !=
	        std::string::npos);
	REQUIRE(speech_audio_helper.find("romExtractRelPathForFilenum") !=
	        std::string::npos);
	REQUIRE(speech_audio_helper.find("fsFileSize(rel_path) <= 0") !=
	        std::string::npos);
	REQUIRE(speech_audio_helper.find("romdataFileGetName(file_num)") !=
	        std::string::npos);
	requireTokenOrder(speech_audio_helper,
		"romExtractRelPathForFilenum(file_num",
		"fsFileSize(rel_path) <= 0");
	requireTokenOrder(speech_audio_helper,
		"fsFileSize(rel_path) <= 0",
		"romdataFileGetName(file_num)");
	const std::string audio_source_guard =
		readTextFile("tools/asset_native_source_guard.py");
	REQUIRE(audio_source_guard.find("SCENARIO_AUDIO_SOURCE_METADATA_ERROR") !=
	        std::string::npos);
	REQUIRE(audio_source_guard.find(
		        "scan_scenario_audio_source_metadata_guards") !=
	        std::string::npos);
	REQUIRE(audio_source_guard.find(
		        "romExtractRelPathForFilenum|romdataFileGetName") !=
	        std::string::npos);
	REQUIRE(audio_source_guard.find(
		        "scenario ROM audio metadata helpers must stay behind extracted speech source proof") !=
	        std::string::npos);
	const std::string lang_text_helper =
		functionBlock(scenario_runtime,
			"static const char *s_aiGraphResolveLangTextCatalogId");
	const std::string lang_catalog_helper =
		functionBlock(scenario_runtime,
			"static const asset_entry_t *s_aiGraphFindLangCatalogEntryForBank");
	REQUIRE(lang_catalog_helper.find("ASSET_LANG") != std::string::npos);
	REQUIRE(lang_catalog_helper.find("LANG_MANIFEST_MAX_BANKS") !=
	        std::string::npos);
	REQUIRE(lang_catalog_helper.find("ext.lang.bank_id") != std::string::npos);
	REQUIRE(lang_catalog_helper.find("ext.lang.strings_file") !=
	        std::string::npos);
	REQUIRE(lang_catalog_helper.find("entry->id[0]") != std::string::npos);
	REQUIRE(lang_text_helper.find("s_aiGraphFindLangCatalogEntryForBank") !=
	        std::string::npos);
	REQUIRE(lang_text_helper.find("LANG_MANIFEST_MAX_BANKS") !=
	        std::string::npos);
	REQUIRE(lang_text_helper.find("langManifestLoadBankFromCatalog") !=
	        std::string::npos);
	REQUIRE(lang_text_helper.find("public .pdlang source") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSpeak");
		REQUIRE(block.find("s_aiGraphResolveSpeechAudioSourceId") !=
		        std::string::npos);
		REQUIRE(block.find("audio_id=%s") != std::string::npos);
		REQUIRE(block.find("audio=%d") == std::string::npos);
		REQUIRE(block.find("sound=%d") == std::string::npos);
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecutePlaySound",
		     "scenarioSourceAiGraphExecuteAssignSound",
		     "scenarioSourceAiGraphExecutePlaySoundFromProp",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("s_aiGraphResolveSpeechAudioSourceId") !=
		        std::string::npos);
		REQUIRE(block.find("audio_id=%s") != std::string::npos);
		REQUIRE(block.find("audio=%d") == std::string::npos);
		REQUIRE(block.find("sound=%d") == std::string::npos);
	}
	const std::string repeat_pad_sound_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecutePlayRepeatingSoundFromPad");
	REQUIRE(repeat_pad_sound_block.find("s_aiGraphResolveSpeechAudioSourceId") !=
	        std::string::npos);
	REQUIRE(repeat_pad_sound_block.find(
		        "s_aiGraphRequireRuntimePad(\"play_repeating_sound_from_pad\"") !=
	        std::string::npos);
	REQUIRE(repeat_pad_sound_block.find("audio_id=%s") !=
	        std::string::npos);
	REQUIRE(repeat_pad_sound_block.find("pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(repeat_pad_sound_block.find(
		        "backend=graph.ai.action.audio+ai/ailists.json+pads.json") !=
	        std::string::npos);
	REQUIRE(repeat_pad_sound_block.find("audio=%d") == std::string::npos);
	REQUIRE(repeat_pad_sound_block.find("sound=%d") == std::string::npos);
	requireTokenOrder(repeat_pad_sound_block,
		"s_aiGraphRequireRuntimePad(\"play_repeating_sound_from_pad\"",
		"psCreate(0, NULL, sound, padnum");
	const std::string near_pad_target_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrPresetToChrNearPad");
	REQUIRE(near_pad_target_block.find(
		        "s_aiGraphRequireRuntimePad(\"set_chr_preset_to_chr_near_pad\"") !=
	        std::string::npos);
	REQUIRE(near_pad_target_block.find(
		        "pad=%d found=1 pad_rows=%d") != std::string::npos);
	REQUIRE(near_pad_target_block.find(
		        "backend=graph.ai.action.prop_target+ai/ailists.json+pads.json") !=
	        std::string::npos);
	requireTokenOrder(near_pad_target_block,
		"s_aiGraphRequireRuntimePad(\"set_chr_preset_to_chr_near_pad\"",
		"chrSetChrPresetToChrNearPad(preset, chr, distance, padnum)");
	const std::string speak_block =
		functionBlock(scenario_runtime, "scenarioSourceAiGraphExecuteSpeak");
	REQUIRE(speak_block.find("s_aiGraphResolveSpeechAudioSourceId") !=
	        std::string::npos);
	REQUIRE(speak_block.find("s_aiGraphResolveLangTextCatalogId") !=
	        std::string::npos);
	requireTokenOrder(speak_block,
		"s_aiGraphResolveSpeechAudioSourceId(\"speak\"",
		"s_aiGraphResolveLangTextCatalogId(\"speak\"");
	REQUIRE(speak_block.find(
		        "s_aiGraphResolveRuntimeCharacterRefOrSelector(\"speak\"") !=
	        std::string::npos);
	REQUIRE(speak_block.find("s_aiGraphRequireRuntimePlayerSlot(\"speak\"") !=
	        std::string::npos);
	REQUIRE(speak_block.find("chrFindById(basechr, chrnum)") ==
	        std::string::npos);
	REQUIRE(speak_block.find(
		        "chr_rows=%d target_chr=%d target_selector=%d player_checked=%d text_id=%s audio_id=%s") !=
	        std::string::npos);
	REQUIRE(speak_block.find("player_checked=%d") != std::string::npos);
	REQUIRE(speak_block.find("text_id=%s audio_id=%s") !=
	        std::string::npos);
	REQUIRE(speak_block.find("text=%d audio_id=%s") ==
	        std::string::npos);
	requireTokenOrder(speak_block,
		"s_aiGraphResolveRuntimeCharacterRefOrSelector(\"speak\"",
		"s_aiGraphRequireRuntimePlayerSlot(\"speak\"");
	requireTokenOrder(speak_block,
		"s_aiGraphRequireRuntimePlayerSlot(\"speak\"",
		"setCurrentPlayerNum(playernum)");
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteShowHudmsg",
		     "scenarioSourceAiGraphExecuteShowHudmsgMiddle",
		     "scenarioSourceAiGraphExecuteShowHudmsgTopMiddle",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("s_aiGraphResolveLangTextCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("text_id=%s") != std::string::npos);
		REQUIRE(block.find("text=%u") == std::string::npos);
	}
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphHudPlayerForChr");
		REQUIRE(!helper.empty());
		REQUIRE(helper.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(helper.find("s_aiGraphRequireRuntimeCharacterRefFromBase") !=
		        std::string::npos);
		REQUIRE(helper.find("playermgrGetPlayerNumByProp") !=
		        std::string::npos);
		REQUIRE(helper.find("s32 playernum = g_Vars.currentplayernum") ==
		        std::string::npos);
		REQUIRE(helper.find("playernum = g_Vars.currentplayernum;") !=
		        std::string::npos);
		REQUIRE(helper.find("s_aiGraphRequireRuntimePlayerSlot(action") !=
		        std::string::npos);
		requireTokenOrder(helper,
			"s_aiGraphRequireRuntimeCharacterRefFromBase",
			"playernum = g_Vars.currentplayernum;");
		const size_t fallback_player_pos =
			helper.find("playernum = g_Vars.currentplayernum;");
		REQUIRE(fallback_player_pos != std::string::npos);
		REQUIRE(helper.find("s_aiGraphRequireRuntimePlayerSlot(action",
			fallback_player_pos) != std::string::npos);
		for (const char *fn : {
			     "scenarioSourceAiGraphExecuteShowHudmsg",
			     "scenarioSourceAiGraphExecuteShowHudmsgTopMiddle",
		     }) {
			const std::string block = functionBlock(scenario_runtime, fn);
			REQUIRE(!block.empty());
			REQUIRE(block.find("s32 playernum = g_Vars.currentplayernum") ==
			        std::string::npos);
			REQUIRE(block.find("chr_rows=%d target_chr=%d player_checked=%d") !=
			        std::string::npos);
			REQUIRE(block.find("player_checked=%d") != std::string::npos);
			requireTokenOrder(block,
				"s_aiGraphHudPlayerForChr(",
				"setCurrentPlayerNum(playernum)");
		}
	}
	const std::string say_quip_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSayQuip");
	REQUIRE(say_quip_block.find("s_aiGraphResolveAudioCatalogId") !=
	        std::string::npos);
	REQUIRE(say_quip_block.find("s_aiGraphResolveLangTextCatalogId") !=
	        std::string::npos);
	REQUIRE(say_quip_block.find("g_GuardQuipBank") !=
	        std::string::npos);
	REQUIRE(say_quip_block.find("g_QuipTexts") !=
	        std::string::npos);
	REQUIRE(say_quip_block.find("chr_rows=%d target_chr=%d source_chr=%d checked=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(say_quip_block.find("text=%d") == std::string::npos);
	REQUIRE(say_quip_block.find("g_Vars.players[playernum]->isdead") ==
	        std::string::npos);
	REQUIRE(say_quip_block.find("g_Vars.chrdata->") ==
	        std::string::npos);
	REQUIRE(say_quip_block.find("struct chrdata *active_chr = g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(say_quip_block.find("chrFindById(basechr") ==
	        std::string::npos);
	REQUIRE(say_quip_block.find(
		        "s_aiGraphRequireRuntimeCharacterRefFromBase(\"say_quip\"") !=
	        std::string::npos);
	REQUIRE(say_quip_block.find(
		        "s_aiGraphRequireRuntimeCharacterStatePointer(\"say_quip\"") !=
	        std::string::npos);
	REQUIRE(countOccurrences(say_quip_block,
		"s_aiGraphRequireRuntimeCharacterStatePointer(") >= 2);
	requireTokenOrder(say_quip_block,
		"s_aiGraphRequireRuntimeCharacterRefFromBase(\"say_quip\"",
		"s_aiGraphRequireRuntimeCharacterStatePointer(\"say_quip\"");
	requireTokenOrder(say_quip_block,
		"s_aiGraphRequireRuntimeCharacterStatePointer(\"say_quip\"",
		"CHRRACE(active_chr)");
	requireTokenOrder(say_quip_block,
		"s_aiGraphRequireRuntimeCharacterStatePointer(\"say_quip\"",
		"g_GuardQuipBank");
	requireTokenOrder(say_quip_block,
		"s_aiGraphRequireRuntimeCharacterStatePointer(\"say_quip\"",
		"active_chr->soundtimer");
	requireTokenOrder(say_quip_block,
		"\"say_quip\", loopchr",
		"loopchr && loopchr->model");
	requireTokenOrder(say_quip_block,
		"s_aiGraphRequireRuntimeCharacterRefFromBase(\"say_quip\"",
		"playermgrGetPlayerNumByProp(chr->prop)");
	requireTokenOrder(say_quip_block,
		"playermgrGetPlayerNumByProp(chr->prop)",
		"s_aiGraphRequireRuntimePlayerSlot(\"say_quip\"");
	requireTokenOrder(say_quip_block,
		"s_aiGraphRequireRuntimePlayerSlot(\"say_quip\"",
		"player->isdead");
	requireTokenOrder(say_quip_block,
		"s_aiGraphRequireRuntimePlayerSlot(\"say_quip\"",
		"g_Vars.coopplayernum >= 0");
	requireTokenOrder(say_quip_block,
		"g_Vars.coopplayernum >= 0 && player->isdead",
		"alternate_playernum = playernum == g_Vars.bondplayernum");
	requireTokenOrder(say_quip_block,
		"alternate_playernum = playernum == g_Vars.bondplayernum",
		"playernum = (u32)alternate_playernum");
	const std::string ci_quip_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSayCiStaffQuip");
	REQUIRE(ci_quip_block.find("s_aiGraphResolveAudioCatalogId") !=
	        std::string::npos);
	REQUIRE(ci_quip_block.find("g_CiGreetingQuips") !=
	        std::string::npos);
	REQUIRE(ci_quip_block.find("chr_rows=%d target_chr=%d type=%d channel=%d audio_id=%s") !=
	        std::string::npos);
	REQUIRE(ci_quip_block.find("s_aiGraphRequireRuntimeCharacterPointer(") !=
	        std::string::npos);
	REQUIRE(ci_quip_block.find("quip=%d") == std::string::npos);
	requireTokenOrder(ci_quip_block,
		"s_aiGraphRequireRuntimeCharacterPointer(",
		"g_CiGreetingQuips[chr->morale]");
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrNotTalking");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_chr_not_talking chr=%d chr_rows=%d target_chr=%d") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterPointer(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr->propsoundcount");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrAddMorale",
		     "scenarioSourceAiGraphExecuteChrAddAlertness",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"incrementByte(&chr->");
	}
	const std::string max_damage_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetMaxDamage");
	REQUIRE(!max_damage_block.empty());
	REQUIRE(max_damage_block.find("chrFindById(basechr, chrnum)") ==
	        std::string::npos);
	REQUIRE(max_damage_block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
	        std::string::npos);
	REQUIRE(max_damage_block.find("chr_rows=%d target_chr=%d vehicle_rows=%d source_vehicle_type=%d value=%.3f") !=
	        std::string::npos);
	requireTokenOrder(max_damage_block,
		"s_aiGraphRequireRuntimeCharacterRefFromBase(",
		"chrSetMaxDamage(chr, maxdamage)");
	REQUIRE(scenario_runtime.find("s_aiGraphFindChrById(") ==
	        std::string::npos);
	for (const char *fn : {
		     "s_aiGraphRequireRuntimeCharacterRefFromBase",
		     "s_aiGraphResolveRuntimeCharacterRefOrSelector",
		     "s_aiGraphRuntimeCharacterRefLooksSourceBacked",
		     "s_aiGraphRequireRuntimeCharacterPointer",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRuntimeChrIsSourceSpawned(") !=
		        std::string::npos);
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteSetChrflag",
		     "scenarioSourceAiGraphExecuteUnsetChrflag",
		     "scenarioSourceAiGraphExecuteIfHasChrflag",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterStatePointer(") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrflag");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(",
			"chr->chrflags |= flags");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteUnsetChrflag");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(",
			"chr->chrflags &= ~flags");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfHasChrflag");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(",
			"result = ((chr->chrflags & flags) == flags)");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrSetChrflag",
		     "scenarioSourceAiGraphExecuteChrUnsetChrflag",
		     "scenarioSourceAiGraphExecuteIfChrHasChrflag",
		     "scenarioSourceAiGraphExecuteChrSetHiddenFlag",
		     "scenarioSourceAiGraphExecuteChrUnsetHiddenFlag",
		     "scenarioSourceAiGraphExecuteIfChrHasHiddenFlag",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d selector=%d") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphResolveRuntimeCharacterRefOrSelector");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphChrIdIsRuntimeSelector(chr_id)") !=
		        std::string::npos);
		REQUIRE(block.find("if (selector)") != std::string::npos);
		REQUIRE(block.find("\"missing runtime character id %d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"if (selector)",
			"\"missing runtime character id %d");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphChrIdIsRuntimeSelector(chr_id)") !=
		        std::string::npos);
		REQUIRE(block.find("if (!chr || !chr->prop)") !=
		        std::string::npos);
		REQUIRE(block.find("return 1;") != std::string::npos);
		REQUIRE(block.find("runtime character id %d does not point at a source character row") !=
		        std::string::npos);
		requireTokenOrder(block,
			"if (!chr || !chr->prop)",
			"return 1;");
		requireTokenOrder(block,
			"s_aiGraphCountRuntimeSetupChrRowsFromSource(action",
			"runtime character id %d does not point at a source character row");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetChrflag");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"if (chr) {\n\t\tchr->chrflags |= flags");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrUnsetChrflag");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"if (chr) {\n\t\tchr->chrflags &= ~flags");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrHasChrflag");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"result = chr ? ((chr->chrflags & flags) == flags) : 0");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetHiddenFlag");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"if (chr) {\n\t\tchr->hidden |= flags");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrUnsetHiddenFlag");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"if (chr) {\n\t\tchr->hidden &= ~flags");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrHasHiddenFlag");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"result = chr ? ((chr->hidden & flags) == flags) : 0");
	}
	const std::string music_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveMusicCatalogId");
	const std::string music_match_helper =
		functionBlock(scenario_runtime, "s_aiGraphMatchMusicCatalogId");
	REQUIRE(music_id_helper.find("assetCatalogIterateByType") !=
	        std::string::npos);
	REQUIRE(music_id_helper.find("ASSET_AUDIO") != std::string::npos);
	REQUIRE(music_match_helper.find("AUDIO_CAT_MUSIC") != std::string::npos);
	REQUIRE(music_match_helper.find("ext.audio.sound_id") !=
	        std::string::npos);
	REQUIRE(music_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecutePlayTemporaryPrimaryTrack",
		     "scenarioSourceAiGraphExecutePlayTrackIsolated",
		     "scenarioSourceAiGraphExecutePlayCutsceneTrack",
		     "scenarioSourceAiGraphExecutePlayTemporaryTrack",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("s_aiGraphResolveMusicCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("track_id=%s") != std::string::npos);
		REQUIRE(block.find("track=%d") == std::string::npos);
	}
	const std::string play_x_track_block =
		functionBlock(scenario_runtime, "scenarioSourceAiGraphExecutePlayXTrack");
	REQUIRE(play_x_track_block.find("minsecs=%d maxsecs=%d") !=
	        std::string::npos);
	REQUIRE(play_x_track_block.find("track=%d") == std::string::npos);
	const std::string anim_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveAnimationCatalogId");
	const std::string catalog_register_anim =
		functionBlock(readTextFile("port/src/assetcatalog.c"),
			"assetCatalogRegisterAnimation");
	const std::string walker_anim =
		readTextFile("port/src/loader_walker_anim.c");
	REQUIRE(catalog_register_anim.find("entry->source_animnum = anim_id") !=
	        std::string::npos);
	REQUIRE(walker_anim.find("id, /* anim_id: */ -1") !=
	        std::string::npos);
	REQUIRE(walker_anim.find("e->source_animnum = (s32)source_index") !=
	        std::string::npos);
	REQUIRE(walker_anim.find("fsFileLoad(source_path, &command_size)") !=
	        std::string::npos);
	REQUIRE(walker_anim.find("loaderPoolParseAnimationSourceJson(command_json, command_size") !=
	        std::string::npos);
	REQUIRE(walker_anim.find("loaderPoolParseAnimationJson(manifest, manifest_len)") ==
	        std::string::npos);
	REQUIRE(walker_anim.find("LOADER.POOL.ANIMATION.SOURCE: id=%s source=%s bytes=%u") !=
	        std::string::npos);
	REQUIRE(walker_anim.find("LOADER.POOL.ANIMATION.SOURCE_FAIL") !=
	        std::string::npos);
	REQUIRE(anim_id_helper.find("catalogResolveAnim") != std::string::npos);
	REQUIRE(anim_id_helper.find("assetCatalogGetByIndex") !=
	        std::string::npos);
	REQUIRE(anim_id_helper.find("has no public .pdanim source") !=
	        std::string::npos);
	REQUIRE(anim_id_helper.find("catalogLoadTypedAsset(ASSET_ANIMATION") !=
	        std::string::npos);
	REQUIRE(anim_id_helper.find("catalogGetLoadedAnimationClip") !=
	        std::string::npos);
	REQUIRE(anim_id_helper.find("has no compiled public clip") !=
	        std::string::npos);
	REQUIRE(anim_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	const std::string special_death_anim_helper =
		functionBlock(scenario_runtime,
			"s_aiGraphResolveSpecialDeathAnimationCatalogIds");
	REQUIRE(special_death_anim_helper.find("SPECIALDIE_NONE") !=
	        std::string::npos);
	REQUIRE(special_death_anim_helper.find("SPECIALDIE_ONCHAIR") !=
	        std::string::npos);
	REQUIRE(special_death_anim_helper.find("g_SpecialDieAnims") !=
	        std::string::npos);
	REQUIRE(special_death_anim_helper.find("s_aiGraphResolveAnimationCatalogId") !=
	        std::string::npos);
	REQUIRE(special_death_anim_helper.find("invalid special death animation mode %d") !=
	        std::string::npos);
	requireTokenOrder(special_death_anim_helper,
		"specialdie < SPECIALDIE_FALLBACK || specialdie > SPECIALDIE_ONCHAIR",
		"g_SpecialDieAnims[specialdie - 1].animnum");
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrDoAnimation",
		     "scenarioSourceAiGraphExecuteSetCameraAnimation",
		     "scenarioSourceAiGraphExecuteObjectDoAnimation",
		     "scenarioSourceAiGraphExecuteDoPresetAnimation",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("s_aiGraphResolveAnimationCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("anim_id=%s") != std::string::npos);
		REQUIRE(block.find("anim_source=%s") != std::string::npos);
		REQUIRE(block.find("clip_bytes=%u") != std::string::npos);
		REQUIRE(block.find("anim=%d") == std::string::npos);
		REQUIRE(block.find("anim=%u") == std::string::npos);
	}
	const std::string preset_anim_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteDoPresetAnimation");
	REQUIRE(preset_anim_block.find("selected_anim") != std::string::npos);
	requireTokenOrder(preset_anim_block,
		"s_aiGraphResolveAnimationCatalogId",
		"chrTryStartAnim");
	REQUIRE(preset_anim_block.find(
			"preset=%d anim_id=%s anim_source=%s clip_bytes=%u applied=%d") != std::string::npos);
	REQUIRE(preset_anim_block.find(
			"preset=%d applied=%d source=%s") == std::string::npos);
	const std::string natural_anim_block =
		functionBlock(scenario_runtime, "scenarioSourceAiGraphExecuteIfNaturalAnim");
	REQUIRE(natural_anim_block.find("s_aiGraphResolveAnimationCatalogId") !=
	        std::string::npos);
	REQUIRE(natural_anim_block.find("anim_id=%s anim_source=%s clip_bytes=%u current_anim_id=%s current_anim_source=%s current_clip_bytes=%u") !=
	        std::string::npos);
	REQUIRE(natural_anim_block.find("anim=%d current=%d") ==
	        std::string::npos);
	const std::string special_death_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrSpecialDeathAnimation");
	REQUIRE(special_death_block.find(
			"s_aiGraphResolveSpecialDeathAnimationCatalogIds") !=
	        std::string::npos);
	REQUIRE(special_death_block.find(
			"chr_rows=%d target_chr=%d specialdie=%d anim_id=%s alt_anim_id=%s chair_fallback_anim_id=%s") !=
	        std::string::npos);
	REQUIRE(special_death_block.find("chrFindById(basechr, chrnum);") ==
	        std::string::npos);
	REQUIRE(special_death_block.find("animation=%d") == std::string::npos);
	requireTokenOrder(special_death_block,
		"s_aiGraphRequireRuntimeCharacterRefFromBase(",
		"s_aiGraphResolveSpecialDeathAnimationCatalogIds");
	requireTokenOrder(special_death_block,
		"s_aiGraphResolveSpecialDeathAnimationCatalogIds",
		"chr->specialdie = animation");
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_num_players_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_kill_count_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_num_knocked_out_chrs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.kill_bond") !=
	        std::string::npos);
	{
		const std::string kill_count = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfKillCountGreaterThan");
		REQUIRE(!kill_count.empty());
		REQUIRE(kill_count.find(
			        "AI condition if_kill_count_greater_than kill_count=%d current=%d") !=
		        std::string::npos);
		requireTokenOrder(kill_count,
			"s_aiGraphRequireMissionGlobalConditionNode(",
			"branch_taken = g_Vars.killcount > kill_count");
		requireTokenOrder(kill_count,
			"branch_taken = g_Vars.killcount > kill_count",
			"AI condition if_kill_count_greater_than kill_count=%d current=%d");
	}
	{
		const std::string kill_bond = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteKillBond");
		REQUIRE(!kill_bond.empty());
		REQUIRE(kill_bond.find("g_Vars.bond->isdead = true") ==
		        std::string::npos);
		REQUIRE(kill_bond.find("struct player *bond = g_Vars.bond;") !=
		        std::string::npos);
		requireTokenOrder(kill_bond,
			"s_aiGraphRequireRuntimePlayerPointer(\"kill_bond\"",
			"bond->isdead = true");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteKillBond") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.mission_global+ai/ailists.json+mission.graph.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.mission_global+ai/ailists.json+mission.graph.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_num_arghs_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_num_arghs_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_num_close_arghs_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_num_close_arghs_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_health_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_health_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_shield_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_shield_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_injured") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_shield_damaged") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_morale_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_morale_less_than_random") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_alertness_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_alertness_less_than_random") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.character_state+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_idle") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_stopped") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_dead") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_death_animation_finished") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_knocked_out") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_can_see_target") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.lifecycle+ai/ailists.json") !=
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteIfChrDead",
		     "scenarioSourceAiGraphExecuteIfChrKnockedOut",
		     "scenarioSourceAiGraphExecuteIfChrShieldLessThan",
		     "scenarioSourceAiGraphExecuteIfChrShieldGreaterThan",
		     "scenarioSourceAiGraphExecuteIfInjured",
		     "scenarioSourceAiGraphExecuteIfShieldDamaged",
		     "scenarioSourceAiGraphExecuteIfChrAlertnessLessThan",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrDeathAnimationFinished");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		REQUIRE(block.find("AI condition if_chr_death_animation_finished chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.players[playernum]->isdead") ==
		        std::string::npos);
		requireTokenOrder(block,
			"playermgrGetPlayerNumByProp(chr->prop)",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"player->isdead");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphChrHealthComparison");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("g_Vars.players[playernum]->bondhealth") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"player->bondhealth");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->maxdamage - chr->damage");
	}
	REQUIRE(scenario_runtime.find("AI condition if_chr_dead chr=%d chr_rows=%d target_chr=%d label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_death_animation_finished chr=%d chr_rows=%d target_chr=%d player_checked=%d label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_knocked_out chr=%d chr_rows=%d target_chr=%d label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_health_greater_than chr=%d chr_rows=%d target_chr=%d player_checked=%d value=%.3f current=%.3f label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_health_less_than chr=%d chr_rows=%d target_chr=%d player_checked=%d value=%.3f current=%.3f label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_shield_less_than chr=%d chr_rows=%d target_chr=%d value=%.3f current=%.3f label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_shield_greater_than chr=%d chr_rows=%d target_chr=%d value=%.3f current=%.3f label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_injured chr=%d chr_rows=%d target_chr=%d label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_shield_damaged chr=%d chr_rows=%d target_chr=%d label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_alertness_less_than chr=%d chr_rows=%d target_chr=%d threshold=%d current=%d label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_pouncebits_eq") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_training_pc_holographed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_player_using_device") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.state_device+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI condition if_player_using_device chr=%d chr_rows=%d target_chr=%d player=%d player_checked=%d device=%d label=%d branch=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPlayerUsingDevice");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(\"if_player_using_device\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"if_player_using_device\"",
			"currentPlayerGetDeviceState(devicenum)");
	}
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_begin_or_end_teleport") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_teleport_full_white") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_cutscene_weapon") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.fade_screen") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_fade_complete") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_chr_hudpiece_visible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_passive_mode") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_firing_in_cutscene") !=
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteSetChrHudpieceVisible",
		     "scenarioSourceAiGraphExecuteChrSetFiringInCutscene",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
	}
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_portal_flag") !=
	        std::string::npos);
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphResolveRuntimePortal");
		REQUIRE(!helper.empty());
		REQUIRE(helper.find("missing portals.json source") !=
		        std::string::npos);
		REQUIRE(helper.find("missing source-built portal table") !=
		        std::string::npos);
		REQUIRE(helper.find("if (!g_BgPortals)") !=
		        std::string::npos);
		REQUIRE(helper.find("g_BgNumPortalCameraCacheItems > 0") !=
		        std::string::npos);
		REQUIRE(helper.find("portalnum >= 0") != std::string::npos);
		REQUIRE(helper.find("portalnum < g_BgNumPortalCameraCacheItems") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetPortalFlag");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action set_portal_flag portal=%d found=%d portal_rows=%d") !=
		        std::string::npos);
		REQUIRE(block.find("portals=%s scene=scene.glb backend=graph.ai.action.cutscene_presentation+ai/ailists.json+portals.json+scene.glb") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimePortal(\"set_portal_flag\"",
			"if (portal_available)");
		requireTokenOrder(block,
			"if (portal_available)",
			"g_BgPortals[portalnum].flags |= flags");
	}
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_music_event_queue_is_empty") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_coop_mode") !=
	        std::string::npos);
	{
		const std::string queue_block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfMusicEventQueueIsEmpty");
		REQUIRE(!queue_block.empty());
		REQUIRE(queue_block.find("AI condition if_music_event_queue_is_empty queue=%d waited=%d branch=%d label=%d") !=
		        std::string::npos);
		requireTokenOrder(queue_block,
			"missing ai/ailists.json source",
			"g_MusicEventQueueLength && !waited");
		requireTokenOrder(queue_block,
			"missing ai/ailists.json source",
			"g_MusicEventQueueLength, waited");
	}
	{
		const std::string coop_block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfCoopMode");
		REQUIRE(!coop_block.empty());
		REQUIRE(coop_block.find("AI condition if_coop_mode coop=%d normmp=%d branch=%d label=%d") !=
		        std::string::npos);
		requireTokenOrder(coop_block,
			"missing ai/ailists.json source",
			"g_Vars.normmplayerisrunning");
		requireTokenOrder(coop_block,
			"missing ai/ailists.json source",
			"g_MissionConfig.iscoop");
	}
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_same_floor_distance_to_pad_less_than chr=%d chr_rows=%d target_chr=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.remove_references_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_toggle_model_part") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.obj_set_model_part_visible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_obj_health_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_obj_health") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_chr_special_death_animation") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_room_to_search") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteShowCutsceneChrs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.cutscene_visibility+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlayXTrack") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteStopAmbientTrack") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.music_track+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrDrawWeapon") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetPlayerForceSpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetInvincible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfPlayerIsInvincible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfChrHasNoGun") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteDoGunCommand") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfDistanceToGunLessThan") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRecoverGun") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireCurrentGunProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("current gun prop has invalid runtime weapon %d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("current gun prop is type %d without weapon payload") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_has_no_gun chr=%d chr_rows=%d target_chr=%d gun_model_id=%s weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_draw_weapon chr=%d chr_rows=%d target_chr=%d player_checked=%d weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_draw_weapon_in_cutscene chr=%d chr_rows=%d target_chr=%d player_checked=%d weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_player_force_speed chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_set_invincible chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_player_is_invincible chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action do_gun_command chr_rows=%d source_chr=%d mode=%d gun_model_id=%s weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_distance_to_gun_less_than chr_rows=%d source_chr=%d distance=%.2f gun_model_id=%s weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action recover_gun chr_rows=%d source_chr=%d gun_model_id=%s weapon_id=%s") !=
	        std::string::npos);
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphResolvePlayerChr");
		REQUIRE(!helper.empty());
		REQUIRE(helper.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(helper.find("s_aiGraphResolveRuntimeCharacterRefOrSelector") !=
		        std::string::npos);
		REQUIRE(helper.find("s_aiGraphRequireRuntimeCharacterRefFromBase") ==
		        std::string::npos);
		REQUIRE(helper.find("PROPTYPE_PLAYER") != std::string::npos);
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrDrawWeapon",
		     "scenarioSourceAiGraphExecuteChrDrawWeaponInCutscene",
		     "scenarioSourceAiGraphExecuteSetPlayerForceSpeed",
		     "scenarioSourceAiGraphExecuteChrSetInvincible",
		     "scenarioSourceAiGraphExecuteIfPlayerIsInvincible",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolvePlayerChr(") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrDrawWeapon",
		     "scenarioSourceAiGraphExecuteChrDrawWeaponInCutscene",
		     "scenarioSourceAiGraphExecuteSetPlayerForceSpeed",
		     "scenarioSourceAiGraphExecuteChrSetInvincible",
		     "scenarioSourceAiGraphExecuteIfPlayerIsInvincible",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolvePlayerChr(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"setCurrentPlayerNum(playernum)");
	}
	{
		const std::string chr_set_invincible = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetInvincible");
		REQUIRE(!chr_set_invincible.empty());
		REQUIRE(chr_set_invincible.find(
			        "AI action chr_set_invincible chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
		        std::string::npos);
		requireTokenOrder(chr_set_invincible,
			"missing scenario.ai.action.chr_set_invincible node",
			"g_PlayerInvincible = true");
		requireTokenOrder(chr_set_invincible,
			"s_aiGraphResolvePlayerChr(\"chr_set_invincible\"",
			"g_PlayerInvincible = true");
		requireTokenOrder(chr_set_invincible,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_invincible\"",
			"g_PlayerInvincible = true");
	}
	{
		const std::string if_player_is_invincible = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPlayerIsInvincible");
		REQUIRE(!if_player_is_invincible.empty());
		REQUIRE(if_player_is_invincible.find(
			        "AI condition if_player_is_invincible chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
		        std::string::npos);
		requireTokenOrder(if_player_is_invincible,
			"missing scenario.ai.condition.if_player_is_invincible node",
			"pass = g_PlayerInvincible ? 1 : 0");
		requireTokenOrder(if_player_is_invincible,
			"s_aiGraphResolvePlayerChr(\"if_player_is_invincible\"",
			"pass = g_PlayerInvincible ? 1 : 0");
		requireTokenOrder(if_player_is_invincible,
			"s_aiGraphRequireRuntimePlayerSlot(\"if_player_is_invincible\"",
			"pass = g_PlayerInvincible ? 1 : 0");
	}
	{
		const std::string set_player_force_speed = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteSetPlayerForceSpeed");
		REQUIRE(set_player_force_speed.find(
			        "g_Vars.currentplayer->bondforcespeed") ==
		        std::string::npos);
		requireTokenOrder(set_player_force_speed,
			"s_aiGraphRequireRuntimePlayerSlot(\"set_player_force_speed\"",
			"player->bondforcespeed.x");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteIfChrHasNoGun",
		     "scenarioSourceAiGraphExecuteChrDeleteWeapon",
		     "scenarioSourceAiGraphExecuteIfTriggerShotList",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
	}
	{
		const std::string if_gun_unclaimed =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteIfGunUnclaimed");
		REQUIRE(!if_gun_unclaimed.empty());
		requireTokenOrder(if_gun_unclaimed,
			"s_aiGraphRequireCurrentGunProp(\"if_gun_unclaimed\"",
			"weapon->base.flags |= OBJFLAG_FORCENOBOUNCE");
	}
	{
		const std::string if_chr_has_no_gun =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteIfChrHasNoGun");
		REQUIRE(!if_chr_has_no_gun.empty());
		requireTokenOrder(if_chr_has_no_gun,
			"s_aiGraphRequireCurrentGunProp(\"if_chr_has_no_gun\"",
			"pass = chr && chr->model && chr->gunprop == NULL");
		requireTokenOrder(if_chr_has_no_gun,
			"s_aiGraphRequireCurrentGunProp(\"if_chr_has_no_gun\"",
			"s_ActiveScenarioGraphs.ai_action_if_chr_has_no_gun_logged");
	}
	{
		const std::string do_gun_command =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteDoGunCommand");
		REQUIRE(!do_gun_command.empty());
		requireTokenOrder(do_gun_command,
			"s_aiGraphRequireRuntimeCharacterPointer(\"do_gun_command\"",
			"s_aiGraphRequireCurrentGunProp(\"do_gun_command\"");
		requireTokenOrder(do_gun_command,
			"s_aiGraphRequireRuntimeCharacterPointer(\"do_gun_command\"",
			"chrGoToProp(chr, chr->gunprop, GOPOSFLAG_JOG)");
		requireTokenOrder(do_gun_command,
			"s_aiGraphRequireCurrentGunProp(\"do_gun_command\"",
			"chrGoToProp(chr, chr->gunprop, GOPOSFLAG_JOG)");
	}
	{
		const std::string distance_to_gun =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteIfDistanceToGunLessThan");
		REQUIRE(!distance_to_gun.empty());
		requireTokenOrder(distance_to_gun,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"s_aiGraphRequireCurrentGunProp(\"if_distance_to_gun_less_than\"");
		requireTokenOrder(distance_to_gun,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"xdiff = chr->prop->pos.x - chr->gunprop->pos.x");
		requireTokenOrder(distance_to_gun,
			"s_aiGraphRequireCurrentGunProp(\"if_distance_to_gun_less_than\"",
			"xdiff = chr->prop->pos.x - chr->gunprop->pos.x");
	}
	{
		const std::string recover_gun =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteRecoverGun");
		REQUIRE(!recover_gun.empty());
		requireTokenOrder(recover_gun,
			"s_aiGraphRequireRuntimeCharacterPointer(\"recover_gun\"",
			"prop = chr->gunprop");
		requireTokenOrder(recover_gun,
			"s_aiGraphRequireRuntimeCharacterPointer(\"recover_gun\"",
			"chr->gunprop = NULL");
		requireTokenOrder(recover_gun,
			"s_aiGraphRequireCurrentGunProp(\"recover_gun\"",
			"chr->gunprop = NULL");
		requireTokenOrder(recover_gun,
			"s_aiGraphRequireCurrentGunProp(\"recover_gun\"",
			"chrEquipWeapon(prop->weapon, chr)");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrCopyProperties") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action duplicate_chr chr=%d chr_rows=%d source_chr=%d body_id=%s weapon0_model_id=%s weapon0_id=%s weapon1_model_id=%s weapon1_id=%s hat_model_id=%s ailist=%u pad=%d found=%d pad_rows=%d pass=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_copy_properties src_chr=%d chr_rows=%d source_chr=%d label=%d pad=%d found=%d pad_rows=%d copied=%d") !=
	        std::string::npos);
	const std::string enable_chr_block = functionBlock(scenario_runtime,
		"scenarioSourceAiGraphExecuteEnableChr");
	REQUIRE(!enable_chr_block.empty());
	REQUIRE(enable_chr_block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
	        std::string::npos);
	REQUIRE(enable_chr_block.find("chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	const std::string disable_chr_block = functionBlock(scenario_runtime,
		"scenarioSourceAiGraphExecuteDisableChr");
	REQUIRE(!disable_chr_block.empty());
	REQUIRE(disable_chr_block.find("s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(") !=
	        std::string::npos);
	REQUIRE(disable_chr_block.find("chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	{
		const std::string enable_chr = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteEnableChr");
		REQUIRE(enable_chr.find(
			        "s_aiGraphRequireRuntimeCharacterRefFromBase(\"enable_chr\"") !=
		        std::string::npos);
		requireTokenOrder(enable_chr,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"propActivate(chr->prop)");
	}
	{
		const std::string disable_chr = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteDisableChr");
		REQUIRE(disable_chr.find(
			        "s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(\"disable_chr\"") !=
		        std::string::npos);
		requireTokenOrder(disable_chr,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"propDeregisterRooms(chr->prop)");
	}
	{
		const std::string duplicate_chr =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteDuplicateChr");
		REQUIRE(!duplicate_chr.empty());
		REQUIRE(duplicate_chr.find("ailistFindById(ailistid)") !=
		        std::string::npos);
		REQUIRE(duplicate_chr.find(
			        "s_aiGraphRequireRuntimeCharacterRefFromBase(\"duplicate_chr\"") !=
		        std::string::npos);
		REQUIRE(duplicate_chr.find("chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		REQUIRE(duplicate_chr.find("s_aiGraphRequireRuntimePad(\"duplicate_chr\"") !=
		        std::string::npos);
		REQUIRE(duplicate_chr.find("s_aiGraphRequireRuntimePadTable(\n\t\t\t\t\"duplicate_chr\"") !=
		        std::string::npos);
		REQUIRE(duplicate_chr.find("s_aiGraphResolveBodyCatalogId(\"duplicate_chr\"") !=
		        std::string::npos);
		REQUIRE(duplicate_chr.find("s_aiGraphResolveModelCatalogId(\n\t\t\t\t\t\"duplicate_chr\"") !=
		        std::string::npos);
		REQUIRE(duplicate_chr.find("s_aiGraphResolveWeaponCatalogId(\n\t\t\t\t\t\"duplicate_chr\"") !=
		        std::string::npos);
		REQUIRE(duplicate_chr.find("body_id=%s weapon0_model_id=%s weapon0_id=%s") !=
		        std::string::npos);
		requireTokenOrder(duplicate_chr,
			"s_aiEntityLifecycleGraphReady(\"duplicate_chr\"",
			"ailistFindById(ailistid)");
		requireTokenOrder(duplicate_chr,
			"ailistFindById(ailistid)",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(\"duplicate_chr\"");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"copied_pad = chr->padpreset1");
		requireTokenOrder(duplicate_chr,
			"copied_pad = chr->padpreset1",
			"s_aiGraphRequireRuntimePad(\"duplicate_chr\"");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphRequireRuntimePad(\"duplicate_chr\"",
			"s_aiGraphResolveBodyCatalogId(\"duplicate_chr\"");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphResolveBodyCatalogId(\"duplicate_chr\"",
			"cloneprop = chrSpawnAtChr");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphResolveModelCatalogId(",
			"cloneprop = chrSpawnAtChr");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphResolveWeaponCatalogId(",
			"cloneprop = chrSpawnAtChr");
		requireTokenOrder(duplicate_chr,
			"cloneprop = chrSpawnAtChr",
			"clone->padpreset1 = copied_pad");
		requireTokenOrder(duplicate_chr,
			"missing scenario.ai.action.duplicate_chr node",
			"g_MissionConfig.iscoop");
		requireTokenOrder(duplicate_chr,
			"missing pads.json source",
			"g_MissionConfig.iscoop");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(\"duplicate_chr\"",
			"g_MissionConfig.iscoop");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphResolveBodyCatalogId(\"duplicate_chr\"",
			"g_MissionConfig.iscoop");
		requireTokenOrder(duplicate_chr,
			"missing scenario.ai.action.duplicate_chr node",
			"g_Vars.normmplayerisrunning");
		requireTokenOrder(duplicate_chr,
			"missing pads.json source",
			"g_Vars.normmplayerisrunning");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(\"duplicate_chr\"",
			"g_Vars.normmplayerisrunning");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphResolveBodyCatalogId(\"duplicate_chr\"",
			"g_Vars.normmplayerisrunning");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphResolveBodyCatalogId(\"duplicate_chr\"",
			"g_Vars.numaibuddies");
		REQUIRE(duplicate_chr.find(
			        "AI action duplicate_chr chr=%d chr_rows=%d source_chr=%d") !=
		        std::string::npos);
	}
	{
		const std::string copy_properties =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteChrCopyProperties");
		REQUIRE(!copy_properties.empty());
		REQUIRE(copy_properties.find(
			        "s_aiGraphRequireRuntimeCharacterRefFromBase(\n\t\t\t\"chr_copy_properties\"") !=
		        std::string::npos);
		REQUIRE(copy_properties.find("chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		REQUIRE(copy_properties.find("s_aiGraphRequireRuntimePad(\n\t\t\t\t\t\"chr_copy_properties\"") !=
		        std::string::npos);
		REQUIRE(copy_properties.find("s_aiGraphRequireRuntimePadTable(\n\t\t\t\t\"chr_copy_properties\"") !=
		        std::string::npos);
		requireTokenOrder(copy_properties,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"copied_pad = src->padpreset1");
		requireTokenOrder(copy_properties,
			"copied_pad = src->padpreset1",
			"s_aiGraphRequireRuntimePad(");
		requireTokenOrder(copy_properties,
			"s_aiGraphRequireRuntimePad(",
			"basechr->hearingscale = src->hearingscale");
		requireTokenOrder(copy_properties,
			"basechr->hearingscale = src->hearingscale",
			"basechr->padpreset1 = copied_pad");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlayerAutoWalk") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimePad(\"player_auto_walk\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action player_auto_walk chr=%d chr_rows=%d target_chr=%d player_checked=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecutePlayerAutoWalk");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chr = chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(\"player_auto_walk\"") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimePlayerSlot(\"player_auto_walk\"") !=
		        std::string::npos);
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePad(\"player_auto_walk\"",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(\"player_auto_walk\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(\"player_auto_walk\"",
			"s_aiGraphRequireRuntimePlayerSlot(\"player_auto_walk\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"player_auto_walk\"",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"player_auto_walk\"",
			"playerAutoWalk(");
	}
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimePad(\"chr_move_to_pad\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_move_to_pad chr=%d chr_rows=%d target_chr=%d operand=%d mode88_chr=%d mode88_chr_rows=%d resolved_pad=%d mode=%d pass=%d pad_rows=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrMoveToPad");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("chrFindById(basechr, pad_or_chr") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(\"chr_move_to_pad\"") !=
		        std::string::npos);
		REQUIRE(block.find("s32 pad_count = s_countRuntimePads();") ==
		        std::string::npos);
		REQUIRE(block.find("mode88_target_chr = target_operand") !=
		        std::string::npos);
		REQUIRE(block.find("&mode88_chr_count, &chr2") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(\"chr_move_to_pad\"",
			"chrResolvePadId");
		requireTokenOrder(block,
			"&mode88_chr_count, &chr2",
			"chrGetInverseTheta");
	}
	REQUIRE(scenario_runtime.find("AI action warp_jo_to_pad pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action spawn_chr_at_pad body_id=%s head_id=%s pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteRevokeControl",
		     "scenarioSourceAiGraphExecuteGrantControl",
		     "scenarioSourceAiGraphExecutePlayerFadeIn",
		     "scenarioSourceAiGraphExecuteIfColourFadeComplete",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolvePlayerChr(") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfPlayerAutoWalkFinished") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_player_auto_walk_finished chr=%d chr_rows=%d target_chr=%d player_checked=%d label=%d walking=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPlayerAutoWalkFinished");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chr = chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimePlayerSlot(") !=
		        std::string::npos);
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"missing ai/ailists.json source",
			"g_Vars.tickmode == TICKMODE_AUTOWALK");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"g_Vars.tickmode == TICKMODE_AUTOWALK");
		requireTokenOrder(block,
			"setCurrentPlayerNum(playernum)",
			"g_Vars.tickmode == TICKMODE_AUTOWALK");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteRemoveChr");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") ==
		        std::string::npos);
		REQUIRE(block.find("chrFindById(basechr") == std::string::npos);
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteApplyGsetDamage",
		     "scenarioSourceAiGraphExecuteChrDamageChr",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("chrFindById(basechr") == std::string::npos);
	}
	REQUIRE(scenario_runtime.find("AI action remove_chr chr=%d chr_rows=%d target_chr=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action apply_gset_damage chr=%d chr_rows=%d target_chr=%d hitpart=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_damage_chr attacker=%d attacker_chr_rows=%d source_attacker_chr=%d victim=%d victim_chr_rows=%d source_victim_chr=%d hitpart=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireOptionalRuntimeCharacterPointer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action %s chr=%d chr_rows=%d source_chr=%d label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_attack_locked chr_rows=%d source_chr=%d label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_attacking chr_rows=%d source_chr=%d label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireCharacterConditionActor") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_idle chr_rows=%d source_chr=%d actiontype=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_num_arghs_less_than chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_morale_less_than chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_alertness_less_than_random chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action random chr_rows=%d source_chr=%d value=%u") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_pouncebits_eq chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	{
		const std::string helper = functionBlock(scenario_runtime,
			"static s32 s_aiGraphRequireCharacterConditionActor(const char *condition");
		REQUIRE(!helper.empty());
		requireTokenOrder(helper,
			"s_aiGraphRequireCharacterConditionNode(",
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(");
	}
	struct ActiveStateGuardCase {
		const char *fn;
		const char *live_read;
	};
	const ActiveStateGuardCase active_state_guard_cases[] = {
		{ "scenarioSourceAiGraphExecuteIfIdle", "chr->actiontype == ACT_ANIM" },
		{ "scenarioSourceAiGraphExecuteIfStopped", "chrIsStopped(chr)" },
		{ "scenarioSourceAiGraphExecuteIfCanSeeTarget", "chrCheckCanSeeTarget(chr)" },
		{ "scenarioSourceAiGraphExecuteIfNumArghsLessThan", "chrGetNumArghs(chr)" },
		{ "scenarioSourceAiGraphExecuteIfNumArghsGreaterThan", "chrGetNumArghs(chr)" },
		{ "scenarioSourceAiGraphExecuteIfNumCloseArghsLessThan", "chrGetNumCloseArghs(chr)" },
		{ "scenarioSourceAiGraphExecuteIfNumCloseArghsGreaterThan", "chrGetNumCloseArghs(chr)" },
		{ "scenarioSourceAiGraphExecuteIfMoraleLessThan", "chr && chr->prop ? chr->morale : 0" },
		{ "scenarioSourceAiGraphExecuteIfMoraleLessThanRandom", "chr && chr->prop ? chr->morale : 0" },
		{ "scenarioSourceAiGraphExecuteIfAlertness", "chr && chr->prop ? chr->alertness : 0" },
		{ "scenarioSourceAiGraphExecuteIfAlertnessLessThanRandom", "chr && chr->prop ? chr->alertness : 0" },
		{ "scenarioSourceAiGraphExecuteIfPouncebitsEq", "chr->pouncebits == pouncebits" },
	};
	for (const ActiveStateGuardCase &entry : active_state_guard_cases) {
		const std::string block = functionBlock(scenario_runtime, entry.fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireCharacterConditionActor(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(",
			entry.live_read);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteRandom");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(\"random\"",
			"chr->random = rngRandom() & 0xff");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfRandomLessThan");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(",
			"branch_taken = (chr && chr->random < threshold)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfRandomGreaterThan");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(",
			"branch_taken = (chr && chr->random > threshold)");
	}
	REQUIRE(scenario_runtime.find("AI action try_modify_attack chr=%d chr_rows=%d source_chr=%d hovercar=%d vehicle_rows=%d source_vehicle_type=%d label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action face_entity chr=%d chr_rows=%d source_chr=%d type=%u id=%u label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition consider_grenade_throw chr=%d chr_rows=%d source_chr=%d type=%u id=%u label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action drop_item chr=%d chr_rows=%d source_chr=%d model_id=%s weapon_id=%s label=%d result=%d") !=
	        std::string::npos);
	struct CombatGuardCase {
		const char *fn;
		const char *live_call;
	};
	const CombatGuardCase combat_guard_cases[] = {
		{ "scenarioSourceAiGraphExecuteTrySidestep", "chrTrySidestep(chr)" },
		{ "scenarioSourceAiGraphExecuteTryJumpOut", "chrTryJumpOut(chr)" },
		{ "scenarioSourceAiGraphExecuteTryRunSideways", "chrTryRunSideways(chr)" },
		{ "scenarioSourceAiGraphExecuteTryAttackWalk", "chrTryAttackWalk(chr)" },
		{ "scenarioSourceAiGraphExecuteTryAttackRun", "chrTryAttackRun(chr)" },
		{ "scenarioSourceAiGraphExecuteTryAttackRoll", "chrTryAttackRoll(chr)" },
		{ "scenarioSourceAiGraphExecuteTryAttackStand", "chrTryAttackStand(chr" },
		{ "scenarioSourceAiGraphExecuteTryAttackKneel", "chrTryAttackKneel(chr" },
		{ "scenarioSourceAiGraphExecuteTryAttackLie", "chrTryAttackLie(chr" },
		{ "scenarioSourceAiGraphExecuteTryModifyAttack", "chrTryModifyAttack(chr" },
		{ "scenarioSourceAiGraphExecuteFaceEntity", "chrFaceEntity(chr" },
		{ "scenarioSourceAiGraphExecuteConsiderGrenadeThrow", "chrConsiderGrenadeThrow(chr" },
		{ "scenarioSourceAiGraphExecuteDropItem", "chrDropItem(chr" },
	};
	for (const CombatGuardCase &entry : combat_guard_cases) {
		const std::string block = functionBlock(scenario_runtime, entry.fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireOptionalRuntimeCharacterPointer(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			entry.live_call);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteTryModifyAttack");
		REQUIRE(!block.empty());
		REQUIRE(block.find("objects=%s backend=%s") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"chopperAttack(hovercar)");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteIfAttackLocked",
		     "scenarioSourceAiGraphExecuteIfAttacking",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireOptionalRuntimeCharacterPointer(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"chr->actiontype");
	}
	REQUIRE(scenario_runtime.find("AI action %s chr=%d chr_rows=%d source_chr=%d target_chr=%d target_chr_rows=%d resolved_target_chr=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteTryRunFromTarget");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireTargetMovementNode(\"try_run_from_target\"",
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"chrTryRunFromTarget(chr)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphExecuteTryToTargetProp");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireTargetMovementNode(action",
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(",
			"chrGoToTarget(chr");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteTryGoToCoverProp");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireTargetMovementNode(\"try_go_to_cover_prop\"",
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"chrGoToCoverProp(chr)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphExecuteTryToChr");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireTargetMovementNode(action",
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(action");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(action",
			"chrGoToChr(chr");
	}
	for (const char *helper_name : {
		     "s_aiGraphRequireRuntimeCharacterRefFromBase",
		     "s_aiGraphResolveRuntimeCharacterRefOrSelector",
		     "s_aiGraphRuntimeCharacterRefLooksSourceBacked",
	     }) {
		const std::string helper = functionBlock(scenario_runtime,
			helper_name);
		REQUIRE(!helper.empty());
		REQUIRE(helper.find("chr = chrFindById(basechr, chr_id);") !=
		        std::string::npos);
		REQUIRE(helper.find("s_aiGraphRuntimeChrIsSourceSpawned(chr)") !=
		        std::string::npos);
		REQUIRE(helper.find("s_aiGraphSetupSourceContainsChrnum(chr->chrnum)") !=
		        std::string::npos);
	}
	REQUIRE(scenario_runtime.find("AI %s %s chr_rows=%d source_chr=%d value=%d label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_pad_preset_to_pad_on_route_to_target chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition %s chr_rows=%d source_chr=%d value=%d label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_room_is_on_screen chr_rows=%d source_chr=%d pad=%d") !=
	        std::string::npos);
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphRequirePerceptionAlarmActor");
		REQUIRE(!helper.empty());
		requireTokenOrder(helper,
			"s_aiGraphRequirePerceptionAlarmNode(",
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(");
	}
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphRequireSpatialPerceptionActor");
		REQUIRE(!helper.empty());
		requireTokenOrder(helper,
			"s_aiGraphRequirePerceptionAlarmNode(",
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(");
	}
	struct PerceptionAlarmGuardCase {
		const char *fn;
		const char *live_read;
	};
	const PerceptionAlarmGuardCase perception_alarm_guard_cases[] = {
		{ "scenarioSourceAiGraphExecuteIfCanHearAlarm", "chrCanHearAlarm(chr)" },
		{ "scenarioSourceAiGraphExecuteIfPatrolling", "chr->actiontype" },
		{ "scenarioSourceAiGraphExecuteIfHearsTarget", "chrIsHearingTarget(chr)" },
		{ "scenarioSourceAiGraphExecuteIfSawInjury", "chrSawInjury(chr" },
		{ "scenarioSourceAiGraphExecuteIfSawDeath", "chrSawDeath(chr" },
		{ "scenarioSourceAiGraphExecuteIfLosToTarget", "chrHasLosToTarget(chr)" },
		{ "scenarioSourceAiGraphExecuteIfLosToAttackTarget", "chrHasLosToAttackTarget(chr" },
		{ "scenarioSourceAiGraphExecuteIfTargetNearlyInSight", "chrIsTargetNearlyInSight(chr" },
		{ "scenarioSourceAiGraphExecuteIfNearlyInTargetsSight", "chrIsNearlyInTargetsSight(chr" },
		{ "scenarioSourceAiGraphExecuteIfSawTargetRecently", "chrSawTargetRecently(chr)" },
		{ "scenarioSourceAiGraphExecuteIfHeardTargetRecently", "chrHeardTargetRecently(chr)" },
	};
	for (const PerceptionAlarmGuardCase &entry : perception_alarm_guard_cases) {
		const std::string block = functionBlock(scenario_runtime, entry.fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequirePerceptionAlarmActor(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequirePerceptionAlarmActor(",
			entry.live_read);
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteIfLosToTarget",
		     "scenarioSourceAiGraphExecuteIfLosToAttackTarget",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("vehicle_rows=%d source_vehicle_type=%d") !=
		        std::string::npos);
		REQUIRE(block.find("objects=%s backend=%s") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"chopperCheckTargetInFov(hovercar, 64)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"chopperCheckTargetInSight(hovercar)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetPadPresetToPadOnRouteToTarget");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequirePerceptionAlarmNode(\"action\"",
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"chrGetTargetProp(chr)");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"chrSetPadPresetToPadOnRouteToTarget(chr)");
	}
	struct SpatialPerceptionGuardCase {
		const char *fn;
		const char *live_read;
	};
	const SpatialPerceptionGuardCase spatial_perception_guard_cases[] = {
		{ "scenarioSourceAiGraphExecuteIfLosToChr", "chrHasLosToPos(basechr" },
		{ "scenarioSourceAiGraphExecuteIfNeverBeenOnScreen", "chr->chrflags" },
		{ "scenarioSourceAiGraphExecuteIfOnScreen", "chr->prop->flags" },
		{ "scenarioSourceAiGraphExecuteIfChrInOnScreenRoom", "s_aiGraphChrInOnScreenRoom(chr)" },
		{ "scenarioSourceAiGraphExecuteIfRoomIsOnScreen", "chrGetPadRoom(chr, pad)" },
		{ "scenarioSourceAiGraphExecuteIfTargetAimingAtMe", "chrIsTargetAimingAtMe(chr)" },
		{ "scenarioSourceAiGraphExecuteIfNearMiss", "chrResetNearMiss(chr)" },
		{ "scenarioSourceAiGraphExecuteIfSeesSuspiciousItem", "s_aiGraphChrSeesSuspiciousItem(chr)" },
		{ "scenarioSourceAiGraphExecuteIfCheckFovWithTarget", "chrIsInTargetsFovX(chr" },
		{ "scenarioSourceAiGraphExecuteIfTargetInFovLeft", "chrGetAngleToTarget(chr)" },
		{ "scenarioSourceAiGraphExecuteIfTargetOutOfFovLeft", "chrGetAngleToTarget(chr)" },
		{ "scenarioSourceAiGraphExecuteIfTargetInFov(", "chrIsTargetInFov(chr" },
		{ "scenarioSourceAiGraphExecuteIfTargetOutOfFov(", "chrIsTargetInFov(chr" },
		{ "scenarioSourceAiGraphExecuteIfDistanceToTargetLessThan", "chrGetDistanceToTarget(chr)" },
		{ "scenarioSourceAiGraphExecuteIfDistanceToTargetGreaterThan", "chrGetDistanceToTarget(chr)" },
	};
	for (const SpatialPerceptionGuardCase &entry :
			spatial_perception_guard_cases) {
		const std::string block = functionBlock(scenario_runtime, entry.fn);
		REQUIRE(!block.empty());
		if (std::string(entry.fn) ==
				"scenarioSourceAiGraphExecuteIfRoomIsOnScreen") {
			REQUIRE(block.find(
				        "s_aiGraphRequireOptionalRuntimeCharacterPointer(") !=
			        std::string::npos);
			requireTokenOrder(block,
				"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
				entry.live_read);
		} else {
			REQUIRE(block.find("s_aiGraphRequireSpatialPerceptionActor(") !=
			        std::string::npos);
			requireTokenOrder(block,
				"s_aiGraphRequireSpatialPerceptionActor(",
				entry.live_read);
		}
	}
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphChrSeesSuspiciousItem");
		REQUIRE(!helper.empty());
		REQUIRE(helper.find("roomGetProps(chr->prop->rooms") !=
		        std::string::npos);
		REQUIRE(helper.find("struct prop *prop = &g_Vars.props[*ptr];") !=
		        std::string::npos);
		REQUIRE(helper.find("chrHasLosToProp(chr, prop)") !=
		        std::string::npos);
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfSeesSuspiciousItem");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chr, 0, 1, &chr_count, &source_chr") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireSpatialPerceptionActor(",
			"s_aiGraphChrSeesSuspiciousItem(chr)");
		requireTokenOrder(block,
			"chr, 0, 1, &chr_count, &source_chr",
			"s_aiGraphChrSeesSuspiciousItem(chr)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfLosToChr");
		requireTokenOrder(block,
			"s_aiGraphRequireSpatialPerceptionActor(",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrHasLosToPos(basechr");
	}
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphPreparePadDistancePerceptionBranch");
		REQUIRE(!helper.empty());
		requireTokenOrder(helper,
			"s_aiGraphRequirePadDistancePerceptionSource(",
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(");
		requireTokenOrder(helper,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"chrResolvePadId(chr, padnum)");
	}
	struct DistancePerceptionGuardCase {
		const char *fn;
		const char *guard;
		const char *live_read;
	};
	const DistancePerceptionGuardCase distance_perception_guard_cases[] = {
		{ "scenarioSourceAiGraphExecuteIfChrDistanceToPadLessThan",
			"s_aiGraphRequirePadDistancePerceptionSource(",
			"chrGetDistanceToPad(chr, resolved_pad)" },
		{ "scenarioSourceAiGraphExecuteIfChrDistanceToPadGreaterThan",
			"s_aiGraphRequirePadDistancePerceptionSource(",
			"chrGetDistanceToPad(chr, resolved_pad)" },
		{ "scenarioSourceAiGraphExecuteIfDistanceToChrLessThan",
			"s_aiGraphRequireDistancePerceptionActor(",
			"chrGetDistanceToChr(chr, chrnum)" },
		{ "scenarioSourceAiGraphExecuteIfDistanceToChrGreaterThan",
			"s_aiGraphRequireDistancePerceptionActor(",
			"chrGetDistanceToChr(chr, chrnum)" },
		{ "scenarioSourceAiGraphExecuteIfAnyChrNearSelf",
			"s_aiGraphRequireDistancePerceptionActor(",
			"chrSetChrPresetToAnyChrNearSelf(chr, distance)" },
		{ "scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadLessThan",
			"s_aiGraphPreparePadDistancePerceptionBranch(",
			"chrGetDistanceFromTargetToPad(chr, resolved_pad)" },
		{ "scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadGreaterThan",
			"s_aiGraphPreparePadDistancePerceptionBranch(",
			"chrGetDistanceFromTargetToPad(chr, resolved_pad)" },
	};
	for (const DistancePerceptionGuardCase &entry :
			distance_perception_guard_cases) {
		const std::string block = functionBlock(scenario_runtime, entry.fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find(entry.guard) != std::string::npos);
		requireTokenOrder(block, entry.guard, entry.live_read);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrDistanceToPadLessThan");
		requireTokenOrder(block,
			"s_aiGraphRequirePadDistancePerceptionSource(",
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"s_aiGraphPreparePadDistancePerceptionBranch(");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrDistanceToPadGreaterThan");
		requireTokenOrder(block,
			"s_aiGraphRequirePadDistancePerceptionSource(",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphPreparePadDistancePerceptionBranch(");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteIfDistanceToChrLessThan",
		     "scenarioSourceAiGraphExecuteIfDistanceToChrGreaterThan",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		requireTokenOrder(block,
			"s_aiGraphRequireDistancePerceptionActor(",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrGetDistanceToChr(chr, chrnum)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteRemoveChr");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"chr->hidden |= 0x20");
		REQUIRE(block.find("applied = chr && chr->prop") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteApplyGsetDamage");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrDamageByImpact(");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrDamageChr");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrGetHeldUsableProp(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrDamageByImpact(");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteIfLosToChr",
		     "scenarioSourceAiGraphExecuteIfChrInOnScreenRoom",
		     "scenarioSourceAiGraphExecuteIfChrDistanceToPadGreaterThan",
		     "scenarioSourceAiGraphExecuteIfChrInRoom",
		     "scenarioSourceAiGraphExecuteChrDropItems",
		     "scenarioSourceAiGraphExecuteChrDropWeapon",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("chrFindById(") == std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrHasObject");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphResolveRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeObjectTag(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeObjectTag(") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") ==
		        std::string::npos);
		REQUIRE(block.find("chrFindById(") == std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeObjectTag(",
			"objFindByTagId(tag_id)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrHasWeaponEquipped");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphResolveRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeObjectTag(") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") ==
		        std::string::npos);
		REQUIRE(block.find("chrFindById(") == std::string::npos);
		REQUIRE(block.find("objFindByTagId(tag_id)") == std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteGiveObjectToChr");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphResolveRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") ==
		        std::string::npos);
		REQUIRE(block.find("chrFindById(") == std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrActivatedObject");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRef(") !=
		        std::string::npos);
		REQUIRE(block.find("chrFindById(") == std::string::npos);
		REQUIRE(block.find("chrnum == CHR_ANY") != std::string::npos);
		REQUIRE(block.find("struct player *bond = g_Vars.bond;") !=
		        std::string::npos);
		REQUIRE(block.find("struct player *coop = g_Vars.coop;") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.bond->prop") == std::string::npos);
		REQUIRE(block.find("g_Vars.coop->prop") == std::string::npos);
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRef(",
			"s_aiGraphRequireRuntimePlayerPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRef(",
			"g_Vars.coopplayernum >= 0");
		requireTokenOrder(block,
			"g_Vars.coopplayernum >= 0",
			"\"if_chr_activated_object\", coop");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerPointer(",
			"chr->prop == bond->prop");
		requireTokenOrder(block,
			"\"if_chr_activated_object\", coop",
			"chr->prop == coop->prop");
	}
	REQUIRE(scenario_runtime.find("AI condition if_chr_activated_object chr=%d chr_rows=%d target_chr=%d player_checked=%d tag=%d object_rows=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_has_object chr=%d chr_rows=%d target_chr=%d player_checked=%d tag=%d object_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_has_weapon_equipped chr=%d chr_rows=%d target_chr=%d player_checked=%d weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action drop_object_from_chr tag=%d object_rows=%d chr_rows=%d source_chr=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_drop_items chr=%d chr_rows=%d target_chr=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_drop_weapon chr=%d chr_rows=%d target_chr=%d player_checked=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action give_object_to_chr tag=%d chr=%d chr_rows=%d target_chr=%d player_checked=%d object_rows=%d applied=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrHasObject");
		REQUIRE(!block.empty());
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeObjectTag(",
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(",
			"objFindByTagId(tag_id)");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"invHasProp(");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrHasWeaponEquipped");
		REQUIRE(!block.empty());
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveWeaponCatalogId(",
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"bgunGetWeaponNum(");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteDropObjectFromChr");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeObjectTag(",
			"s_aiGraphRequireRuntimeCharacterPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"objSetDropped(obj->prop, DROPTYPE_SURRENDER)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr->hidden |= CHRHFLAG_DROPPINGITEM");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteGiveObjectToChr");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeObjectTag(",
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(",
			"if (obj && obj->prop && chr && chr->prop)");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrSetTeam",
		     "scenarioSourceAiGraphExecuteDamageChrByAmount",
		     "scenarioSourceAiGraphExecuteChrKill",
		     "scenarioSourceAiGraphExecuteChrGrabObject",
		     "scenarioSourceAiGraphExecuteToggleP1P2",
		     "scenarioSourceAiGraphExecuteChrSetP1P2",
		     "scenarioSourceAiGraphExecuteChrSetCloaked",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
	}
	{
		const std::string chr_source_guard = functionBlock(scenario_runtime,
			"s_aiGraphRequireRuntimeSetupCharacterSource");
		REQUIRE(!chr_source_guard.empty());
		REQUIRE(chr_source_guard.find(
			        "missing setup.fields.json or spawns.json source") !=
		        std::string::npos);

		const std::string chr_count_guard = functionBlock(scenario_runtime,
			"s_aiGraphCountRuntimeSetupChrRowsFromSource");
		REQUIRE(!chr_count_guard.empty());
		requireTokenOrder(chr_count_guard,
			"s_aiGraphRequireRuntimeSetupCharacterSource(action)",
			"s_aiGraphTryCountRuntimeSetupChrRowsFromSource(out_chr_count)");

		const std::string chr_try_count_guard = functionBlock(scenario_runtime,
			"s_aiGraphTryCountRuntimeSetupChrRowsFromSource");
		REQUIRE(!chr_try_count_guard.empty());
		requireTokenOrder(chr_try_count_guard,
			"s_aiGraphRuntimeSetupCharacterSourceIsActive()",
			"s_ActiveScenarioGraphs.source_setup_chr_count");

		const std::string selector_block = functionBlock(scenario_runtime,
			"s_aiGraphChrIdIsRuntimeSelector");
		REQUIRE(!selector_block.empty());
		REQUIRE(selector_block.find("case CHR_SELF:") != std::string::npos);
		REQUIRE(selector_block.find("case CHR_TARGET:") != std::string::npos);
		REQUIRE(selector_block.find("case CHR_P1P2:") != std::string::npos);

		const std::string guard_block = functionBlock(scenario_runtime,
			"s_aiGraphRequireRuntimeCharacterRefFromBase");
		REQUIRE(!guard_block.empty());
		REQUIRE(guard_block.find("s_aiGraphChrIdIsRuntimeSelector(chr_id)") !=
		        std::string::npos);
		requireTokenOrder(guard_block,
			"s_aiGraphChrIdIsRuntimeSelector(chr_id)",
			"return 1;");
		requireTokenOrder(guard_block,
			"s_aiGraphChrIdIsRuntimeSelector(chr_id)",
			"s_aiGraphSetupSourceContainsChrnum(chr->chrnum)");
		requireTokenOrder(guard_block,
			"s_aiGraphCountRuntimeSetupChrRowsFromSource(action",
			"s_aiGraphSetupSourceContainsChrnum(chr->chrnum)");
	}
	REQUIRE(scenario_runtime.find("AI action chr_set_team chr=%d chr_rows=%d target_chr=%d checked_players=%d team=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action damage_chr_by_amount chr=%d chr_rows=%d target_chr=%d amount=%d mode=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_kill chr=%d chr_rows=%d target_chr=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_grab_object chr=%d chr_rows=%d target_chr=%d player_checked=%d tag=%d object_rows=%d grabbed=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action toggle_p1p2 chr=%d chr_rows=%d target_chr=%d player_checked=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_set_p1p2 chr=%d chr_rows=%d source_chr=%d target_chr=%d target_chr_rows=%d resolved_target_chr=%d player_checked=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_set_cloaked chr=%d chr_rows=%d target_chr=%d cloaked=%d timer=%d applied=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteToggleP1P2");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"missing ai/ailists.json source",
			"g_Vars.coopplayernum >= 0");
		requireTokenOrder(block,
			"g_Vars.coopplayernum >= 0",
			"s_aiGraphRequireRuntimePlayerPointer(\"toggle_p1p2\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->p1p2 == g_Vars.bondplayernum");
		requireTokenOrder(block,
			"\"toggle_p1p2\", coop",
			"chr->p1p2 = g_Vars.coopplayernum");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetP1P2");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"missing ai/ailists.json source",
			"g_Vars.coopplayernum >= 0");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_p1p2\"",
			"(s32)playernum == g_Vars.coopplayernum");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_p1p2\"",
			"chr1->p1p2 = g_Vars.bondplayernum");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetTeam");
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("chrnum == CHR_ANTI") != std::string::npos);
		REQUIRE(block.find("g_Vars.antiplayernum >= 0") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.players[playernum]") ==
		        std::string::npos);
		REQUIRE(block.find("checked_players=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiEntityLifecycleGraphReady(\"chr_set_team\"",
			"g_Vars.antiplayernum >= 0");
		requireTokenOrder(block,
			"s_aiGraphCountRuntimeSetupChrRowsFromSource(",
			"g_Vars.antiplayernum >= 0");
		requireTokenOrder(block,
			"g_Vars.antiplayernum >= 0",
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_team\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_team\"",
			"PLAYER_IS_ANTI(player)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_team\"",
			"player->prop->chr");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPlayerChrPortalDistanceLessThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("g_Vars.currentplayer->prop") ==
		        std::string::npos);
		REQUIRE(block.find("g_Vars.currentplayer &&") ==
		        std::string::npos);
		REQUIRE(block.find("AI condition if_player_chr_portal_distance_less_than chr_rows=%d source_chr=%d player_checked=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"s_aiGraphRequireRuntimePlayerPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerPointer(",
			"func0f0056f4(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerPointer(",
			"player->prop->rooms[0]");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteDamageChrByAmount");
		REQUIRE(block.find("chr = chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrDamageByMisc(");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrKill");
		REQUIRE(block.find("chr = chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->actiontype = ACT_DEAD");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrGrabObject");
		REQUIRE(block.find("chr = chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("g_Vars.currentplayer->bondmovemode") ==
		        std::string::npos);
		REQUIRE(block.find("g_Vars.currentplayer->crouchoffset") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeObjectTag(",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_grab_object\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_grab_object\"",
			"player->bondmovemode");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_grab_object\"",
			"player->crouchoffset");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_grab_object\"",
			"bmoveGrabProp(obj->prop)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteToggleP1P2");
		REQUIRE(block.find("chr = chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("struct player *bond = g_Vars.bond;") !=
		        std::string::npos);
		REQUIRE(block.find("struct player *coop = g_Vars.coop;") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.coop->isdead") ==
		        std::string::npos);
		REQUIRE(block.find("g_Vars.bond->isdead") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerPointer(\"toggle_p1p2\"",
			"coop->isdead");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerPointer(\"toggle_p1p2\"",
			"bond->isdead");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->p1p2 =");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerPointer(\"toggle_p1p2\"",
			"chr->p1p2 =");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetP1P2");
		REQUIRE(block.find("chrFindById(basechr") ==
		        std::string::npos);
		REQUIRE(block.find("g_Vars.players[playernum]->isdead") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_p1p2\"",
			"player->isdead");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr1->p1p2 =");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_p1p2\"",
			"chr1->p1p2 =");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetCloaked");
		REQUIRE(block.find("chr = chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrCloak(chr, timer)");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfObjInRoom") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfPlayerLookingAtObject") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPlayerLookingAtObject");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolvePlayerChr(") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d player_checked=%d tag=%d object_rows=%d") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.currentplayer->lookingatprop.prop") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeObjectTag(",
			"s_aiGraphResolvePlayerChr(");
		requireTokenOrder(block,
			"s_aiGraphResolvePlayerChr(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"player->lookingatprop.prop");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrDropWeapon",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"setCurrentPlayerNum(playernum)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteGiveObjectToChr");
		REQUIRE(!block.empty());
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"setCurrentPlayerNum(playernum)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrDropWeapon");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_drop_weapon\"",
			"bgunGetWeaponNum(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_drop_weapon\"",
			"invRemoveItemByNum(");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteGiveObjectToChr");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"give_object_to_chr\"",
			"propPickupByPlayer(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"give_object_to_chr\"",
			"propExecuteTickOperation(");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfTargetIsPlayer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_weapon_state+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.gun_interaction+ai/ailists.json+objects.json+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.character_property+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_navigation+ai/ailists.json+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.object_room+ai/ailists.json+objects.json+pads.json+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI condition if_obj_in_room tag=%d object_rows=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfObjInRoom");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimePad(\"if_obj_in_room\"") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePad(\"if_obj_in_room\"",
			"chrGetPadRoom(basechr, room_id)");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.perception+ai/ailists.json+objects.json+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteConfigureEnvironment") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.environment+ai/ailists.json+scenario.ini+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfDistanceToTarget2") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.target_distance+ai/ailists.json") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfDistanceToTarget2");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition %s chr_rows=%d source_chr=%d actual=%.2f") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(action, chr",
			"chrGetDistanceToTarget2(chr)");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSpeak") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlaySound") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteAssignSound") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteAudioMuteChannel") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfChannelFree") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetObjectSoundVolume") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetObjectSoundVolumeByDistance") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetObjectSoundPlaying") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlayRepeatingSoundFromObject") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlaySoundFromEntity") !=
	        std::string::npos);
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphRequireRuntimeCharacterRefFromBase");
		REQUIRE(!helper.empty());
		REQUIRE(helper.find("chrFindById(basechr, chr_id)") !=
		        std::string::npos);
		REQUIRE(helper.find("chr->prop->type == PROPTYPE_PLAYER") !=
		        std::string::npos);
		REQUIRE(helper.find("s_aiGraphSetupSourceContainsChrnum(chr->chrnum)") !=
		        std::string::npos);
		REQUIRE(scenario_runtime.find(
			        "return s_aiGraphRequireRuntimeCharacterRefFromBase(action,\n\t\tg_Vars.chrdata") !=
		        std::string::npos);
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecutePlaySoundFromEntity");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(g_Vars.chrdata, entity_id)") ==
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRef(\"play_sound_from_entity\"",
			"psModify(channel, -1, -1, prop");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlayRepeatingSoundFromPad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfObjectSoundVolumeLessThan") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlaySoundFromProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlayTemporaryPrimaryTrack") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.audio+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrKill") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRemoveWeaponFromInventory") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteClearInventory") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteReleaseObject") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrGrabObject") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteToggleP1P2") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetP1P2") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetCloaked") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetAutogunTargetTeam") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrBeginOrEndTeleport") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfChrTeleportFullWhite") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetCutsceneWeapon") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteFadeScreen") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfFadeComplete") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetChrHudpieceVisible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetPassiveMode") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetFiringInCutscene") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetPortalFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.character_inventory+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.character_inventory+ai/ailists.json+objects.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_state+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_state+ai/ailists.json+objects.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.teleport_cutscene_weapon+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.cutscene_presentation+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.cutscene_presentation+ai/ailists.json+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfMusicEventQueueIsEmpty") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfCoopMode") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.music_mode+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfChrSameFloorDistanceToPadLessThan") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRemoveReferencesToChr") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrSameFloorDistanceToPadLessThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePad(",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrGetSameFloorDistanceToPad");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteRemoveReferencesToChr");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"prop_index = (s32)(chr->prop - g_Vars.props);");
		requireTokenOrder(block,
			"prop_index = (s32)(chr->prop - g_Vars.props);",
			"chrClearReferences(prop_index)");
	}
	REQUIRE(scenario_runtime.find("s_aiGraphPreparePadDistancePerceptionBranch") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("padnum == PAD_PRESET") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("chrResolvePadId(chr, padnum)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing chr for PAD_PRESET resolution") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphLogPadDistancePerceptionBranch") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition %s value=%d pad=%d found=1 pad_rows=%d chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.pad_reference+ai/ailists.json+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteOpenDoor") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfDoorState") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.door+objects.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action open_door tag=%d object_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action if_door_locked tag=%d object_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteLiftGoToStop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeLiftNumber") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireLiftMotionPads") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action activate_lift liftnum=%d pad=%d found=1 pad_rows=%d tag=%d object_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action lift_go_to_stop tag=%d object_rows=%d stop=%d pad=%d found=%d pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.lift+objects.json+pads.json") !=
	        std::string::npos);
	{
		const std::string lift_number = functionBlock(scenario_runtime,
			"s_aiGraphRequireRuntimeLiftNumber");
		REQUIRE(!lift_number.empty());
		REQUIRE(lift_number.find("ARRAYCOUNT(g_Lifts)") !=
		        std::string::npos);
		requireTokenOrder(lift_number,
			"s_aiGraphRequireRuntimePadTable(action, &pad_count)",
			"ARRAYCOUNT(g_Lifts)");
		requireTokenOrder(lift_number,
			"ARRAYCOUNT(g_Lifts)",
			"padUnpack(i, PADFIELD_LIFT, &pad)");
		requireTokenOrder(lift_number,
			"padUnpack(i, PADFIELD_LIFT, &pad)",
			"pad.liftnum == liftnum");
	}
	{
		const std::string activate_lift = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteActivateLift");
		REQUIRE(!activate_lift.empty());
		requireTokenOrder(activate_lift,
			"s_aiGraphRequireLiftNode(\"activate_lift\"",
			"s_aiGraphRequireRuntimeLiftNumber(\"activate_lift\"");
		requireTokenOrder(activate_lift,
			"s_aiGraphRequireRuntimeLiftNumber(\"activate_lift\"",
			"s_aiGraphRequireRuntimeObjectTagType(\"activate_lift\"");
		requireTokenOrder(activate_lift,
			"s_aiGraphRequireRuntimeObjectTagType(\"activate_lift\"",
			"liftActivate(obj->prop, (u8)liftnum)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfUsingLift");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action if_using_lift pad_rows=%d chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"if_using_lift\"",
			"chr && chr->prop && chrIsUsingLift(chr)");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteConfigureRain") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.weather+ai/ailists.json+scenario.ini") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSwitchToAltSky") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetWindSpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.sky+ai/ailists.json") !=
	        std::string::npos);
	{
		const std::string sky_helper = functionBlock(scenario_runtime,
			"s_aiGraphRequireSkyNode");
		REQUIRE(!sky_helper.empty());
		REQUIRE(sky_helper.find("missing ai/ailists.json source") !=
		        std::string::npos);
		const std::string set_wind_speed = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetWindSpeed");
		REQUIRE(!set_wind_speed.empty());
		REQUIRE(set_wind_speed.find(
			        "AI action set_wind_speed speed=%d value=%.3f") !=
		        std::string::npos);
		requireTokenOrder(set_wind_speed,
			"s_aiGraphRequireSkyNode(\"set_wind_speed\"",
			"g_SkyWindSpeed = 0.1f * speed");
		requireTokenOrder(set_wind_speed,
			"g_SkyWindSpeed = 0.1f * speed",
			"AI action set_wind_speed speed=%d value=%.3f");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetLights") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimePad(\"set_lights\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_lights pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.lighting+ai/ailists.json+pads.json") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetLights");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action set_lights pad=%d found=1 pad_rows=%d chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePad(\"set_lights\"",
			"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"set_lights\"");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"set_lights\"",
			"roomnum = chr && chr->prop ? chrGetPadRoom(chr, padnum) : -1");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer");
		REQUIRE(!block.empty());
		REQUIRE(block.find("*out_chrnum = chr && chr->prop ? chr->chrnum : -1") !=
		        std::string::npos);
		requireTokenOrder(block,
			"if (!chr || !chr->prop)",
			"s_aiGraphRequireRuntimeCharacterPointer(action, chr");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetRoomFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.room_flags+ai/ailists.json+scene.glb") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphRequireRuntimeSceneRooms");
		REQUIRE(!block.empty());
		REQUIRE(block.find("if (!g_Rooms || g_Vars.roomcount <= 1)") !=
		        std::string::npos);
		REQUIRE(block.find("*out_room_count = g_Vars.roomcount;") !=
		        std::string::npos);
		REQUIRE(block.find("missing source-built scene room table") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetRoomFlag");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action set_room_flag room=%d room_rows=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeSceneRoom(\"set_room_flag\"",
			"if (room_available)");
		requireTokenOrder(block,
			"if (room_available)",
			"g_Rooms[roomnum].flags |= flag");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphResolveRuntimeSceneRoom");
		REQUIRE(!block.empty());
		REQUIRE(block.find("if (roomnum > 0 && roomnum < g_Vars.roomcount)") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRuntimeFailure(action") ==
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteConfigureEnvironment");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action configure_environment room=%d room_rows=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeSceneRoom(\"configure_environment\"",
			"if (room_available)");
		requireTokenOrder(block,
			"if (room_available)",
			"g_Rooms[roomnum].flags &= ~ROOMFLAG_PLAYAMBIENTTRACK");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeSceneRoom(\"configure_environment\"",
			"g_Rooms[roomnum].flags &= ~ROOMFLAG_OUTDOORS");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeSceneRoom(\"configure_environment\"",
			"g_Rooms[roomnum].unk4e_04 = value");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeSceneRoom(\"configure_environment\"",
			"g_Rooms[roomnum].unk4d = value");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeSceneRooms(\"configure_environment\"",
			"for (i = 1; i < g_Vars.roomcount; i++)");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeSceneRoom(\"configure_environment\"",
			"roomSetLightsFaulty(roomnum, value)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteShowCutsceneChrs");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action show_cutscene_chrs show=%d chr_rows=%d checked_chrs=%d applied=%d") !=
		        std::string::npos);
		REQUIRE(block.find("g_ChrSlots[i].hidden2 &=") ==
		        std::string::npos);
		REQUIRE(block.find("g_ChrSlots[i].hidden2 |=") ==
		        std::string::npos);
		REQUIRE(block.find("g_ChrSlots[i].chrflags &=") ==
		        std::string::npos);
		REQUIRE(block.find("g_ChrSlots[i].chrflags |=") ==
		        std::string::npos);
		REQUIRE(block.find("struct chrdata *checked_chr_slots[slot_count];") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphCountRuntimeSetupChrRowsFromSource(",
			"slot_count = chrsGetNumSlots();");
		requireTokenOrder(block,
			"slot_count = chrsGetNumSlots();",
			"chr = &g_ChrSlots[i];");
		requireTokenOrder(block,
			"\"show_cutscene_chrs\", chr",
			"checked_chr_slots[checked_chrs++] = chr");
		requireTokenOrder(block,
			"chr = checked_chr_slots[i];",
			"chr->hidden2 &=");
		requireTokenOrder(block,
			"chr = checked_chr_slots[i];",
			"chr->hidden2 |= CHRH2FLAG_HIDDENFORCUTSCENE");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrToggleModelPart") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteObjSetModelPartVisible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.model_part+ai/ailists.json+objects.json") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrToggleModelPart");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrToggleModelPart");
	}
	REQUIRE(scenario_runtime.find("AI action obj_set_model_part_visible tag=%d object_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfObjHealthLessThan") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetObjHealth") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.object_health+ai/ailists.json+objects.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_obj_health tag=%d object_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetChrSpecialDeathAnimation") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.special_death+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetRoomToSearch") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.room_search+ai/ailists.json+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_room_to_search chr_rows=%d chr=%d target_chr=%d target_selector=%d room=%d") !=
	        std::string::npos);
	{
		const std::string set_room_to_search =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteSetRoomToSearch");
		REQUIRE(!set_room_to_search.empty());
		REQUIRE(set_room_to_search.find(
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_room_to_search\"") !=
		        std::string::npos);
		REQUIRE(set_room_to_search.find(
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(\"set_room_to_search\"") !=
		        std::string::npos);
		requireTokenOrder(set_room_to_search,
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_room_to_search\"",
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(\"set_room_to_search\"");
		requireTokenOrder(set_room_to_search,
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(\"set_room_to_search\"",
			"chr->roomtosearch = room");
	}
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_savefile_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.unset_savefile_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_savefile_flag_set") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_savefile_flag_unset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.restart_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.reset_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.pause_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.resume_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_timer_stopped") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_timer_greater_than_random") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_timer_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_timer_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.show_countdown_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.hide_countdown_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_countdown_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.stop_countdown_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.start_countdown_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_countdown_timer_stopped") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_countdown_timer_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_countdown_timer_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRestartTimer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetCountdownTimerValue") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeCharacterStatePointer") !=
	        std::string::npos);
	struct TimerCharacterPointerPin {
		const char *fn;
		const char *guard;
		const char *use;
	};
	const TimerCharacterPointerPin timer_character_pointer_pins[] = {
		     {
			     "scenarioSourceAiGraphExecuteRestartTimer",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(\"restart_timer\"",
			     "chrRestartTimer(chr)",
		     },
		     {
			     "scenarioSourceAiGraphExecuteResetTimer",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(\"reset_timer\"",
			     "chr->timer60 = 0",
		     },
		     {
			     "scenarioSourceAiGraphExecutePauseTimer",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(\"pause_timer\"",
			     "chr->hidden &= ~CHRHFLAG_TIMER_RUNNING",
		     },
		     {
			     "scenarioSourceAiGraphExecuteResumeTimer",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(\"resume_timer\"",
			     "chr->hidden |= CHRHFLAG_TIMER_RUNNING",
		     },
		     {
			     "scenarioSourceAiGraphExecuteIfTimerStopped",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(\"if_timer_stopped\"",
			     "result = (chr->hidden & CHRHFLAG_TIMER_RUNNING) == 0",
		     },
		     {
			     "scenarioSourceAiGraphExecuteIfTimerGreaterThanRandom",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(",
			     "timer = chrGetTimer(chr)",
		     },
		     {
			     "scenarioSourceAiGraphExecuteIfTimerLessThan",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(",
			     "chr_timer = chrGetTimer(chr)",
		     },
		     {
			     "scenarioSourceAiGraphExecuteIfTimerGreaterThan(",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(",
			     "chr_timer = chrGetTimer(chr)",
		     },
	     };
	for (const TimerCharacterPointerPin &pin :
			timer_character_pointer_pins) {
		const std::string block = functionBlock(scenario_runtime, pin.fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find(pin.guard) != std::string::npos);
		requireTokenOrder(block, pin.guard, pin.use);
	}
	struct TimerVehiclePointerPin {
		const char *fn;
		const char *guard_arg;
		const char *use;
	};
	const TimerVehiclePointerPin timer_vehicle_pointer_pins[] = {
		     {
			     "scenarioSourceAiGraphExecuteRestartTimer",
			     "\"restart_timer\", &hovercar->base",
			     "chopperRestartTimer(hovercar)",
		     },
		     {
			     "scenarioSourceAiGraphExecuteIfTimerLessThan",
			     "\"if_timer_less_than\", &hovercar->base",
			     "chopperGetTimer(hovercar)",
		     },
		     {
			     "scenarioSourceAiGraphExecuteIfTimerGreaterThan(",
			     "\"if_timer_greater_than\", &hovercar->base",
			     "chopperGetTimer(hovercar)",
		     },
	     };
	for (const TimerVehiclePointerPin &pin : timer_vehicle_pointer_pins) {
		const std::string block = functionBlock(scenario_runtime, pin.fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find(pin.guard_arg) != std::string::npos);
		REQUIRE(block.find("OBJTYPE_CHOPPER, OBJTYPE_HOVERCAR") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(", pin.use);
	}
	REQUIRE(scenario_runtime.find("AI action restart_timer target=%s chr_rows=%d target_chr=%d vehicle_rows=%d source_vehicle_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action reset_timer chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action pause_timer chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action resume_timer chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action if_timer_stopped chr_rows=%d target_chr=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action if_timer_greater_than_random chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action if_timer_less_than value=%f result=%d target=%s chr_rows=%d target_chr=%d vehicle_rows=%d source_vehicle_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action if_timer_greater_than value=%f result=%d target=%s chr_rows=%d target_chr=%d vehicle_rows=%d source_vehicle_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.show_hudmsg") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.show_hudmsg_middle") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.show_hudmsg_top_middle") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.hovercar_begin_path") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_vehicle_speed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_rotor_speed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_explosions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_object_distance_to_pad_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.misc_effect+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.quip_shuffle+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteHovercarBeginPath") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphFindRuntimePath(\"hovercar_begin_path\"") !=
	        std::string::npos);
	{
		const std::string find_runtime_path =
			functionBlock(scenario_runtime, "s_aiGraphFindRuntimePath");
		REQUIRE(!find_runtime_path.empty());
		requireTokenOrder(find_runtime_path,
			"missing navigation/paths.json source",
			"s_countRuntimePaths()");
		requireTokenOrder(find_runtime_path,
			"missing navigation/paths.json source", "pathFindById");
	}
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimePathPointer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimePathPads") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("runtime path pointer is not source-derived") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("navigation path id %d has no source pad sequence") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing pads.json source for navigation path pads") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action hovercar_begin_path path=%d found=1 vehicle_rows=%d truck_type=%d hovercar_type=%d path_rows=%d source=%s") !=
	        std::string::npos);
	{
		const std::string hovercar_begin_path = functionBlock(
			scenario_runtime, "scenarioSourceAiGraphExecuteHovercarBeginPath");
		REQUIRE(!hovercar_begin_path.empty());
		REQUIRE(hovercar_begin_path.find("g_Vars.truck->") ==
		        std::string::npos);
		REQUIRE(hovercar_begin_path.find("g_Vars.hovercar->") ==
		        std::string::npos);
		requireTokenOrder(hovercar_begin_path,
			"s_aiGraphFindRuntimePath(\"hovercar_begin_path\"",
			"s_aiGraphRequireRuntimePathPads(\"hovercar_begin_path\"");
		requireTokenOrder(hovercar_begin_path,
			"s_aiGraphRequireRuntimePathPads(\"hovercar_begin_path\"",
			"s_aiGraphRequireRuntimeVehicleObjectPointer(");
		requireTokenOrder(hovercar_begin_path,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"truck->path = path");
		requireTokenOrder(hovercar_begin_path,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"chopperFromHovercar(hovercar)");
		requireTokenOrder(hovercar_begin_path,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"hovercar->path = path");
	}
	REQUIRE(scenario_runtime.find("AI condition if_hoverbot_next_step vehicle_rows=%d source_vehicle_type=%d comparison=%d value=%d path=%d path_rows=%d path_pads=%d pad_rows=%d nextstep=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.vehicle_investigation+ai/ailists.json+objects.json+navigation/paths.json+pads.json") !=
	        std::string::npos);
	{
		const std::string hoverbot_next_step = functionBlock(
			scenario_runtime, "scenarioSourceAiGraphExecuteIfHoverbotNextStep");
		REQUIRE(!hoverbot_next_step.empty());
		requireTokenOrder(hoverbot_next_step,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"s_aiGraphRequireRuntimePathPointer(\"if_hoverbot_next_step\"");
		requireTokenOrder(hoverbot_next_step,
			"s_aiGraphRequireRuntimePathPointer(\"if_hoverbot_next_step\"",
			"s_aiGraphRequireRuntimePathPads(");
		requireTokenOrder(hoverbot_next_step,
			"s_aiGraphRequireRuntimePathPads(",
			"hovercar->nextstep < 0");
		requireTokenOrder(hoverbot_next_step,
			"hovercar->nextstep < 0",
			"branch_taken = hovercar &&");
	}
	{
		const std::string path_pointer =
			functionBlock(scenario_runtime,
				"s_aiGraphRequireRuntimePathPointer");
		REQUIRE(!path_pointer.empty());
		requireTokenOrder(path_pointer,
			"missing navigation/paths.json source",
			"s_countRuntimePaths()");
		REQUIRE(path_pointer.find("if (&g_StageSetup.paths[i] == path)") !=
		        std::string::npos);
	}
	{
		const std::string heli_armed = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfHeliWeaponsArmed");
		REQUIRE(!heli_armed.empty());
		requireTokenOrder(heli_armed,
			"s_aiGraphRequireRuntimeChopperPointer(\"if_heli_weapons_armed\"",
			"branch_taken = hovercar && hovercar->weaponsarmed");
	}
	{
		const std::string heli_set_armed = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteHeliSetWeaponsArmed");
		REQUIRE(!heli_set_armed.empty());
		requireTokenOrder(heli_set_armed,
			"s_aiGraphRequireRuntimeChopperPointer(name, hovercar",
			"chopperSetArmed(hovercar, armed)");
	}
	{
		const std::string chopper_pointer = functionBlock(scenario_runtime,
			"s_aiGraphRequireRuntimeChopperPointer");
		REQUIRE(!chopper_pointer.empty());
		requireTokenOrder(chopper_pointer,
			"missing objects.json source for vehicle weapon state",
			"s_countRuntimeSetupObjectRows()");

		const size_t vehicle_pointer_pos = scenario_runtime.rfind(
			"static s32 s_aiGraphRequireRuntimeVehicleObjectPointer");
		REQUIRE(vehicle_pointer_pos != std::string::npos);
		const std::string vehicle_pointer = functionBlock(
			scenario_runtime.substr(vehicle_pointer_pos),
			"s_aiGraphRequireRuntimeVehicleObjectPointer");
		REQUIRE(!vehicle_pointer.empty());
		requireTokenOrder(vehicle_pointer,
			"missing objects.json source for vehicle state",
			"s_countRuntimeSetupObjectRows()");
	}
	REQUIRE(scenario_runtime.find("AI condition if_object_distance_to_pad_less_than tag=%d object_rows=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"set_pad_preset_to_investigation_terminal\",\n\t\t\t\t\t\tselected_pad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_pad_preset_to_investigation_terminal tag=%d object_rows=%d pad=%d found=%d pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetVehicleSpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetRotorSpeed") !=
	        std::string::npos);
	{
		const std::string set_vehicle_speed = functionBlock(
			scenario_runtime, "scenarioSourceAiGraphExecuteSetVehicleSpeed");
		REQUIRE(!set_vehicle_speed.empty());
		REQUIRE(set_vehicle_speed.find("g_Vars.truck->") ==
		        std::string::npos);
		REQUIRE(set_vehicle_speed.find("g_Vars.hovercar->") ==
		        std::string::npos);
		requireTokenOrder(set_vehicle_speed,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"truck->speedaim = speedaim");
		requireTokenOrder(set_vehicle_speed,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"hovercar->speedaim = speedaim");
	}
	{
		const std::string set_rotor_speed = functionBlock(
			scenario_runtime, "scenarioSourceAiGraphExecuteSetRotorSpeed");
		REQUIRE(!set_rotor_speed.empty());
		REQUIRE(set_rotor_speed.find("g_Vars.heli->") ==
		        std::string::npos);
		requireTokenOrder(set_rotor_speed,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"heli->rotoryspeedaim = speedaim");
	}
	REQUIRE(scenario_runtime.find("chopperFromHovercar(hovercar)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("PATHFLAG_INUSE") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("OBJFLAG_CHOPPER_INIT") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteShowHudmsg") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteShowHudmsgMiddle") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteShowHudmsgTopMiddle") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_action") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_team_orders") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.retreat") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.find_cover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.find_cover_within_dist") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.find_cover_outside_dist") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.go_to_cover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.check_cover_out_of_sight") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.orbit_target") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_chr_preset_to_unalerted_teammate") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_squadron") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.face_cover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.danger_cover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.release_cover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.rebuild_teams") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.rebuild_squadrons") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_listening") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_dangerous_object_nearby") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.shuffle_investigation_terminals") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteHeliSetWeaponsArmed") !=
	        std::string::npos);
	for (const char *kind : {
		     "scenario.ai.condition.if_safety2_less_than",
		     "scenario.ai.condition.if_player_using_cmp_or_ar34",
		     "scenario.ai.condition.detect_enemy_on_same_floor",
		     "scenario.ai.condition.detect_enemy",
		     "scenario.ai.condition.if_safety_less_than",
		     "scenario.ai.condition.if_target_moving_slowly",
		     "scenario.ai.condition.if_target_moving_closer",
		     "scenario.ai.condition.if_target_moving_away",
		     "scenario.ai.condition.if_squadron_is_dead",
		     "scenario.ai.condition.if_true",
		     "scenario.ai.condition.if_num_chrs_in_squadron_greater_than",
		     "scenario.ai.condition.if_natural_anim",
		     "scenario.ai.condition.if_y",
		     "scenario.ai.condition.if_sound_timer",
		     "scenario.ai.condition.if_target_y_difference_less_than",
	     }) {
		REQUIRE(scenario_runtime.find(kind) != std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfNaturalAnim");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_natural_anim chr_rows=%d source_chr=%d anim_id=%s anim_source=%s clip_bytes=%u") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\"if_natural_anim\"",
			"chr && chr->naturalanim >= 0");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\"if_natural_anim\"",
			"chr && chr->naturalanim == anim");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfSoundTimer");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_sound_timer chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\"if_sound_timer\"",
			"chr->soundtimer > ticks_value");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfTargetYDifferenceLessThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_target_y_difference_less_than chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\n\t\t\t\"if_target_y_difference_less_than\"",
			"prop = chr ? chrGetTargetProp(chr) : NULL");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\n\t\t\t\"if_target_y_difference_less_than\"",
			"prop->pos.y - chr->prop->pos.y");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteHovercopterFireRocket");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action hovercopter_fire_rocket vehicle_rows=%d source_vehicle_type=%d") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.hovercar->") == std::string::npos);
		REQUIRE(block.find("chopperFireRocket(g_Vars.hovercar, side)") ==
		        std::string::npos);
		REQUIRE(block.find("struct chopperobj *hovercar = g_Vars.hovercar;") !=
		        std::string::npos);
		requireTokenOrder(block,
			"struct chopperobj *hovercar = g_Vars.hovercar;",
			"s_aiGraphRequireRuntimeVehicleObjectPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"chopperFireRocket(hovercar, side)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecutePunchOrKick");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action punch_or_kick chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\"punch_or_kick\"",
			"chrTryPunch(chr, reverse)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetTargetToEyespyIfInSight");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action set_target_to_eyespy_if_in_sight chr_rows=%d source_chr=%d player_checked=%d target_chr_rows=%d source_target_chr=%d") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.players[chr->p1p2]->eyespy") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\n\t\t\t\"set_target_to_eyespy_if_in_sight\"",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"eyespy = player->eyespy");
		requireTokenOrder(block,
			"targetchr, &target_chr_count",
			"propGetIndexByChrId(chr, targetchr->chrnum)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteMiniSkedarTryPounce");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action mini_skedar_try_pounce chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\n\t\t\t\"mini_skedar_try_pounce\"",
			"chrTrySkJump(chr, chr->pouncebits");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteAvoid");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action avoid chr_rows=%d source_chr=%d applied=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\"avoid\"",
			"chrAvoid(chr)");
	}
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphSafety2Score");
		REQUIRE(!helper.empty());
		REQUIRE(helper.find("AI condition if_safety2_less_than chr_rows=%d source_chr=%d checked=%d") ==
		        std::string::npos);
		REQUIRE(countOccurrences(helper,
			"s_aiGraphRequireRuntimeCharacterPointer(") >= 2);
		requireTokenOrder(helper,
			"s_aiGraphRequireRuntimeCharacterPointer(\"if_safety2_less_than\"",
			"chrGetNumArghs(chr)");
		requireTokenOrder(helper,
			"\"if_safety2_less_than\", nearby",
			"nearby && nearby->model");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfSafety2LessThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_safety2_less_than chr_rows=%d source_chr=%d checked=%d") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteDetectEnemyOnSameFloor");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition detect_enemy_on_same_floor chr_rows=%d source_chr=%d checked=%d") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRuntimeCharacterPointerLooksSourceBacked(") !=
		        std::string::npos);
		REQUIRE(block.find("stageGetIndex(g_Vars.stagenum) ==") !=
		        std::string::npos);
		REQUIRE(block.find("STAGEINDEX_MAIANSOS") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRuntimeCharacterPointerLooksSourceBacked(chr",
			"chr->teamscandist");
		requireTokenOrder(block,
			"s_aiGraphRuntimeCharacterPointerLooksSourceBacked(chr",
			"stageGetIndex(g_Vars.stagenum) ==");
		requireTokenOrder(block,
			"s_aiGraphRuntimeCharacterPointerLooksSourceBacked(",
			"candidate_source_backed && candidate->prop");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteDetectEnemy(struct chrdata");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition detect_enemy chr_rows=%d source_chr=%d checked=%d") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRuntimeCharacterPointerLooksSourceBacked(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRuntimeCharacterPointerLooksSourceBacked(chr",
			"chrnums = teamGetChrIds(1)");
		requireTokenOrder(block,
			"s_aiGraphRuntimeCharacterPointerLooksSourceBacked(",
			"candidate_source_backed && candidate->prop");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfSafetyLessThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_safety_less_than chr_rows=%d source_chr=%d checked=%d") !=
		        std::string::npos);
		REQUIRE(countOccurrences(block,
			"s_aiGraphRequireRuntimeCharacterPointer(") >= 2);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(\"if_safety_less_than\"",
			"chrGetNumArghs(chr)");
		requireTokenOrder(block,
			"\"if_safety_less_than\", nearby",
			"nearby && nearby->model");
	}
	{
		const std::string guard =
			readTextFile("tools/asset_native_source_guard.py");
		const char *literal_scan_functions[] = {
			"scenarioSourceAiGraphExecuteSetTeamOrders",
			"scenarioSourceAiGraphExecuteSetChrPresetToUnalertedTeammate",
			"scenarioSourceAiGraphExecuteIfChrNotTalking",
			"scenarioSourceAiGraphExecuteIfChrInSquadronDoingAction",
			"s_aiGraphSafety2Score",
			"scenarioSourceAiGraphExecuteDetectEnemyOnSameFloor",
			"scenarioSourceAiGraphExecuteDetectEnemy",
			"scenarioSourceAiGraphExecuteIfSafetyLessThan",
			"scenarioSourceAiGraphExecuteIfSquadronIsDead",
			"scenarioSourceAiGraphExecuteIfNumChrsInSquadronGreaterThan",
			"scenarioSourceAiGraphExecuteSayQuip",
			"scenarioSourceAiGraphExecuteIncreaseSquadronAlertness",
		};
		for (const char *fn : literal_scan_functions) {
			INFO(fn);
			REQUIRE(guard.find(fn) != std::string::npos);
		}
		REQUIRE(scenario_runtime.find("chrFindByLiteralId(") !=
		        std::string::npos);
	}
	{
		struct LiteralScanGuardCase {
			const char *fn;
			const char *log_token;
		};
		const LiteralScanGuardCase cases[] = {
			{
				"scenarioSourceAiGraphExecuteSetTeamOrders",
				"AI action set_team_orders chr_rows=%d source_chr=%d action=%d chrs=%d checked=%d",
			},
			{
				"scenarioSourceAiGraphExecuteIfChrNotTalking",
				"AI condition if_chr_not_talking chr=%d chr_rows=%d target_chr=%d",
			},
			{
				"s_aiGraphSafety2Score",
				"out_checked_chrs",
			},
			{
				"scenarioSourceAiGraphExecuteIfSquadronIsDead",
				"AI condition if_squadron_is_dead chr_rows=%d checked=%d",
			},
			{
				"scenarioSourceAiGraphExecuteSayQuip",
				"AI action say_quip chr=%d chr_rows=%d target_chr=%d source_chr=%d checked=%d",
			},
			{
				"scenarioSourceAiGraphExecuteIncreaseSquadronAlertness",
				"AI action increase_squadron_alertness chr_rows=%d source_chr=%d checked=%d",
			},
		};
		for (const LiteralScanGuardCase &entry : cases) {
			const std::string block = functionBlock(scenario_runtime,
				entry.fn);
			INFO(entry.fn);
			REQUIRE(!block.empty());
			REQUIRE(block.find(entry.log_token) != std::string::npos);
			REQUIRE(countOccurrences(block, "chrFindByLiteralId(") >= 1);
			REQUIRE(countOccurrences(block,
				        "s_aiGraphRequireRuntimeCharacterPointer(") +
				        countOccurrences(block,
					        "s_aiGraphRequireRuntimeCharacterStatePointer(") >=
			        countOccurrences(block, "chrFindByLiteralId("));
		}
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrNotTalking");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"chr = chrFindByLiteralId(chrnum);",
			"s_aiGraphRequireRuntimeCharacterPointer(\"if_chr_not_talking\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(\"if_chr_not_talking\"",
			"branch_taken = chr && chr->propsoundcount == 0;");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSayQuip");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"loopchr = chrFindByLiteralId(*chrnums);",
			"\"say_quip\", loopchr");
		requireTokenOrder(block,
			"\"say_quip\", loopchr",
			"loopchr && loopchr->model");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIncreaseSquadronAlertness");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"struct chrdata *target = chrFindByLiteralId(*chrnums);",
			"target, &chr_count, NULL");
		requireTokenOrder(block,
			"target, &chr_count, NULL",
			"target &&\n\t\t\t\t\ttarget->model");
	}
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_attack_amount") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetPath") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteStartPatrol") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryStartAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteActivateAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteDeactivateAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetPadPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetPadPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrCopyPadPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetChrPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetChrTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteAddMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrAddMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSubtractMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteAddAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrAddAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSubtractAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIncreaseSquadronAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetHearDistance") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetViewDistance") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetGrenadeProbability") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetChrNum") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetMaxDamage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteAddHealth") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetShield") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetReactionSpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetRecoverySpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetAccuracy") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetDodgeRating") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetUnarmedDodgeRating") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetAction") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetTeamOrders") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRetreat") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteFindCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteFindCoverWithinDist") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteFindCoverOutsideDist") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteGoToCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteCheckCoverOutOfSight") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeCoverTable") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphValidateRuntimeCoverIndex") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing source-derived runtime cover table") !=
	        std::string::npos);
	{
		const std::string cover_table =
			functionBlock(scenario_runtime,
				"s_aiGraphRequireRuntimeCoverTable");
		REQUIRE(!cover_table.empty());
		requireTokenOrder(cover_table,
			"missing navigation/covers.json source",
			"s_countRuntimeCovers()");
	}
	REQUIRE(scenario_runtime.find("missing runtime cover id %d (cover rows=%d)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("retreat chr_rows=%d source_chr=%d speed=%d operation=%d assigned=%d cover_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("chr_rows=%d source_chr=%d criteria=0x%04x refdist=%d assigned=%d cover_rows=%d source=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("go_to_cover chr_rows=%d source_chr=%d speed=%d cover=%d moved_cover=%d cover_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("check_cover_out_of_sight chr_rows=%d source_chr=%d cover=%d out_of_sight=%d cover_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("orbit_target chr_rows=%d source_chr=%d angle=%u alternate=%d speed=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("set_squadron chr_rows=%d source_chr=%d squadron=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteOrbitTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetChrPresetToUnalertedTeammate") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetSquadron") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteFaceCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteDangerCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("face_cover chr_rows=%d source_chr=%d cover=%d faced=%d cover_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("danger_cover chr_rows=%d source_chr=%d assigned=%d moved=%d cover_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("release_cover chr_rows=%d source_chr=%d cover=%d released=%d cover_rows=%d") !=
	        std::string::npos);
	{
		const std::string retreat =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteRetreat");
		REQUIRE(!retreat.empty());
		REQUIRE(retreat.find("assigned_cover = chrAssignCoverByCriteria") !=
		        std::string::npos);
		REQUIRE(retreat.find("s_aiGraphValidateRuntimeCoverIndex(\"retreat\"") !=
		        std::string::npos);
		requireTokenOrder(retreat,
			"s_aiGraphRequireRuntimeCharacterPointer(\"retreat\"",
			"chrRunFromPos(chr, speed");
		requireTokenOrder(retreat,
			"s_aiGraphRequireRuntimeCharacterPointer(\"retreat\"",
			"chrGetTargetProp(chr)");
		requireTokenOrder(retreat,
			"s_aiGraphRequireRuntimeCharacterPointer(\"retreat\"",
			"assigned_cover = chrAssignCoverByCriteria");
		requireTokenOrder(retreat,
			"assigned_cover = chrAssignCoverByCriteria",
			"s_aiGraphValidateRuntimeCoverIndex(\"retreat\"");
		requireTokenOrder(retreat,
			"s_aiGraphValidateRuntimeCoverIndex(\"retreat\"",
			"chrGoToCover(chr, speed)");
	}
	{
		const std::string find_cover =
			functionBlock(scenario_runtime,
				"s_aiGraphExecuteFindCoverByCriteria");
		REQUIRE(!find_cover.empty());
		REQUIRE(find_cover.find("assigned = chrAssignCoverByCriteria") !=
		        std::string::npos);
		REQUIRE(find_cover.find("s_aiGraphValidateRuntimeCoverIndex(action, assigned") !=
		        std::string::npos);
		requireTokenOrder(find_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(action, chr",
			"assigned = chrAssignCoverByCriteria");
		requireTokenOrder(find_cover,
			"assigned = chrAssignCoverByCriteria",
			"s_aiGraphValidateRuntimeCoverIndex(action, assigned");
		requireTokenOrder(find_cover,
			"s_aiGraphValidateRuntimeCoverIndex(action, assigned",
			"if (out_assigned)");
	}
	{
		const std::string go_to_cover =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteGoToCover");
		REQUIRE(!go_to_cover.empty());
		requireTokenOrder(go_to_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"go_to_cover\"",
			"s_aiGraphValidateRuntimeCoverIndex(\"go_to_cover\"");
		requireTokenOrder(go_to_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"go_to_cover\"",
			"moved_cover = chrGoToCover(chr, speed)");
	}
	{
		const std::string check_cover =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteCheckCoverOutOfSight");
		REQUIRE(!check_cover.empty());
		requireTokenOrder(check_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"check_cover_out_of_sight\"",
			"s_aiGraphValidateRuntimeCoverIndex(");
		requireTokenOrder(check_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"check_cover_out_of_sight\"",
			"out_of_sight = chrCheckCoverOutOfSight");
	}
	{
		const std::string orbit =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteOrbitTarget");
		REQUIRE(!orbit.empty());
		requireTokenOrder(orbit,
			"s_aiGraphRequireRuntimeCharacterPointer(\"orbit_target\"",
			"target = chrGetTargetProp(chr)");
		requireTokenOrder(orbit,
			"s_aiGraphRequireRuntimeCharacterPointer(\"orbit_target\"",
			"chr0f04c874(chr, angle");
	}
	{
		const std::string set_squadron =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteSetSquadron");
		REQUIRE(!set_squadron.empty());
		requireTokenOrder(set_squadron,
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_squadron\"",
			"chr->squadron = (u8)squadron");
	}
	{
		const std::string face_cover =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteFaceCover");
		REQUIRE(!face_cover.empty());
		requireTokenOrder(face_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"face_cover\"",
			"s_aiGraphValidateRuntimeCoverIndex(\"face_cover\"");
		requireTokenOrder(face_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"face_cover\"",
			"faced = chrFaceCover(chr)");
	}
	{
		const std::string danger =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteDangerCover");
		REQUIRE(!danger.empty());
		REQUIRE(danger.find("assigned = chrAssignCoverAwayFromDanger") !=
		        std::string::npos);
		REQUIRE(danger.find("s_aiGraphValidateRuntimeCoverIndex(\"danger_cover\"") !=
		        std::string::npos);
		requireTokenOrder(danger,
			"s_aiGraphRequireRuntimeCharacterPointer(\"danger_cover\"",
			"func0f03aca0(chr");
		requireTokenOrder(danger,
			"s_aiGraphRequireRuntimeCharacterPointer(\"danger_cover\"",
			"assigned = chrAssignCoverAwayFromDanger");
		requireTokenOrder(danger,
			"assigned = chrAssignCoverAwayFromDanger",
			"s_aiGraphValidateRuntimeCoverIndex(\"danger_cover\"");
		requireTokenOrder(danger,
			"s_aiGraphValidateRuntimeCoverIndex(\"danger_cover\"",
			"chrGoToCover(chr, GOPOSFLAG_RUN)");
	}
	{
		const std::string release_cover =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteReleaseCover");
		REQUIRE(!release_cover.empty());
		requireTokenOrder(release_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"release_cover\"",
			"s_aiGraphValidateRuntimeCoverIndex(\"release_cover\"");
		requireTokenOrder(release_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"release_cover\"",
			"if (chr->cover >= 0)");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRebuildTeams") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRebuildSquadrons") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetListening") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetSavefileFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteUnsetSavefileFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfSavefileFlagIsSet") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfSavefileFlagIsUnset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetChrflag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetHiddenFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetObjFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_path+navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.start_patrol+navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphResolveOptionalRuntimePath(\"set_path\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphFindRuntimePath(\"start_patrol\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing source-derived runtime path table") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing navigation path id %d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing source-derived runtime pad table") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing runtime pad id %d (pad rows=%d)") !=
	        std::string::npos);
	REQUIRE(countOccurrences(scenario_runtime,
		        "if (!s_ActiveScenarioGraphs.pads_path[0]) {\n"
		        "\t\ts_aiGraphRuntimeFailure(action, \"missing pads.json source\");\n"
		        "\t\treturn 0;\n"
		        "\t}\n\n"
		        "\tpad_count = s_countRuntimePads();") >= 2);
	REQUIRE(scenario_runtime.find("AI action set_path chr_rows=%d source_chr=%d path=%d found=1 path_rows=%d source=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action start_patrol chr_rows=%d source_chr=%d path=%u found=1 path_rows=%d source=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path_pads=%u pad_rows=%d backend=graph.ai.action.set_path+navigation/paths.json+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path_pads=%u pad_rows=%d backend=graph.ai.action.start_patrol+navigation/paths.json+pads.json") !=
	        std::string::npos);
	{
		const std::string set_path = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetPath");
		const std::string start_patrol = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteStartPatrol");
		REQUIRE(!set_path.empty());
		REQUIRE(!start_patrol.empty());
		requireTokenOrder(set_path,
			"s_aiGraphResolveOptionalRuntimePath(\"set_path\"",
			"s_aiGraphRequireRuntimePathPads(\"set_path\"");
		requireTokenOrder(set_path,
			"s_aiGraphRequireRuntimePathPads(\"set_path\"",
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_path\"");
		requireTokenOrder(set_path,
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_path\"",
			"chrSetPath(chr, (u32)path_id)");
		requireTokenOrder(start_patrol,
			"s_aiGraphRequireRuntimeCharacterPointer(\"start_patrol\"",
			"if (chr->path < 0)");
		requireTokenOrder(start_patrol,
			"if (chr->path < 0)",
			"s_aiGraphFindRuntimePath(\"start_patrol\"");
		requireTokenOrder(start_patrol,
			"s_aiGraphFindRuntimePath(\"start_patrol\"",
			"s_aiGraphRequireRuntimePathPads(\"start_patrol\"");
		requireTokenOrder(start_patrol,
			"s_aiGraphRequireRuntimePathPads(\"start_patrol\"",
			"chrTryStartPatrol(chr)");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_pad_preset+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_set_pad_preset+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimePad(\"try_start_alarm\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_pad_preset chr_rows=%d source_chr=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_set_pad_preset chr=%d chr_rows=%d target_chr=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action try_start_alarm chr_rows=%d source_chr=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_copy_pad_preset src_chr=%d dst_chr=%d chr_rows=%d source_chr=%d target_chr=%d pad=%d found=%d pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_copy_pad_preset+chrstate+pads.json") !=
	        std::string::npos);
	{
		const std::string try_alarm = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteTryStartAlarm");
		const std::string go_to_pad = functionBlock(scenario_runtime,
			"s_aiGraphExecuteGoToPad");
		const std::string go_to_pad_preset = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteGoToPadPreset");
		const std::string set_pad_preset = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetPadPreset(struct chrdata *chr");
		const std::string chr_set_pad_preset =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteChrSetPadPreset");
		const std::string copy_preset =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteChrCopyPadPreset");
		REQUIRE(!try_alarm.empty());
		REQUIRE(!go_to_pad.empty());
		REQUIRE(!go_to_pad_preset.empty());
		REQUIRE(!set_pad_preset.empty());
		REQUIRE(!chr_set_pad_preset.empty());
		REQUIRE(!copy_preset.empty());
		requireTokenOrder(try_alarm,
			"s_aiGraphRequireRuntimePad(\"try_start_alarm\"",
			"s_aiGraphRequireRuntimeCharacterPointer(\"try_start_alarm\"");
		requireTokenOrder(try_alarm,
			"s_aiGraphRequireRuntimeCharacterPointer(\"try_start_alarm\"",
			"chrTryStartAlarm(chr, pad_id)");
		requireTokenOrder(go_to_pad,
			"s_aiGraphRequireRuntimePad(action, pad",
			"s_aiGraphRequireRuntimeCharacterPointer(action, chr");
		requireTokenOrder(go_to_pad,
			"s_aiGraphRequireRuntimeCharacterPointer(action, chr",
			"chrGoToPad(chr, pad, goposflags)");
		requireTokenOrder(go_to_pad_preset,
			"s_aiGraphRequireRuntimeCharacterPointer(\"go_to_pad_preset\"",
			"chr->padpreset1");
		requireTokenOrder(set_pad_preset,
			"s_aiGraphRequireRuntimePad(\"set_pad_preset\"",
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_pad_preset\"");
		requireTokenOrder(set_pad_preset,
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_pad_preset\"",
			"chrSetPadPreset(chr, pad)");
		REQUIRE(chr_set_pad_preset.find("chrSetPadPresetByChrnum(") ==
		        std::string::npos);
		requireTokenOrder(chr_set_pad_preset,
			"s_aiGraphRequireRuntimePad(\"chr_set_pad_preset\"",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(chr_set_pad_preset,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->padpreset1 = chrResolvePadId(basechr, pad)");
		REQUIRE(copy_preset.find("missing pads.json source") !=
		        std::string::npos);
		REQUIRE(copy_preset.find("s_aiGraphRequireRuntimeCharacterRefFromBase(\n\t\t\t\"chr_copy_pad_preset\"") !=
		        std::string::npos);
		REQUIRE(copy_preset.find("chr_rows=%d source_chr=%d target_chr=%d") !=
		        std::string::npos);
		REQUIRE(copy_preset.find("s_aiGraphRequireRuntimePad(\"chr_copy_pad_preset\"") !=
		        std::string::npos);
		REQUIRE(copy_preset.find("s_aiGraphRequireRuntimePadTable(\"chr_copy_pad_preset\"") !=
		        std::string::npos);
		requireTokenOrder(copy_preset,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"copied_pad = chrsrc->padpreset1");
		requireTokenOrder(copy_preset,
			"copied_pad = chrsrc->padpreset1",
			"s_aiGraphRequireRuntimePad(\"chr_copy_pad_preset\"");
		requireTokenOrder(copy_preset,
			"s_aiGraphRequireRuntimePad(\"chr_copy_pad_preset\"",
			"chrdst->padpreset1 = copied_pad");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_chr_preset+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_chr_target+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_chr_preset chr_rows=%d target_chr=%d chrpreset=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_chr_target chr=%d chr_rows=%d target_chr=%d chrpreset=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrPreset");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireOptionalRuntimeCharacterPointer(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"chrSetChrPreset(chr, chrpreset)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrTarget");
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("chrSetChrPresetByChrnum(") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrSetChrPreset(chr, chrpreset)");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_morale+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.add_morale+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_add_morale+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.subtract_morale+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_alertness+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.add_alertness+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_add_alertness+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.subtract_alertness+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.increase_squadron_alertness+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_hear_distance+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_view_distance+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_grenade_probability+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_chr_num+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_max_damage+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.add_health+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_shield+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_reaction_speed+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_recovery_speed+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_accuracy+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_dodge_rating+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_unarmed_dodge_rating+ai/ailists.json") !=
	        std::string::npos);
	for (const char *proof : {
		     "AI action set_morale chr_rows=%d source_chr=%d value=%d",
		     "AI action add_morale chr_rows=%d source_chr=%d amount=%d",
		     "AI action subtract_morale chr_rows=%d source_chr=%d amount=%d",
		     "AI action set_alertness chr_rows=%d source_chr=%d value=%d",
		     "AI action add_alertness chr_rows=%d source_chr=%d amount=%d",
		     "AI action subtract_alertness chr_rows=%d source_chr=%d amount=%d",
		     "AI action set_hear_distance chr_rows=%d source_chr=%d value=%.3f",
		     "AI action set_view_distance chr_rows=%d source_chr=%d value=%d applied=%d",
		     "AI action set_grenade_probability chr_rows=%d source_chr=%d value=%d",
		     "AI action set_chr_num chr_rows=%d source_chr=%d value=%d",
		     "AI action set_max_damage chr=%d chr_rows=%d target_chr=%d vehicle_rows=%d source_vehicle_type=%d",
		     "AI action add_health chr_rows=%d source_chr=%d amount=%.3f",
		     "AI action set_shield chr_rows=%d source_chr=%d value=%.3f",
		     "AI action set_reaction_speed chr_rows=%d source_chr=%d value=%d",
		     "AI action set_recovery_speed chr_rows=%d source_chr=%d value=%d",
		     "AI action set_accuracy chr_rows=%d source_chr=%d value=%d",
		     "AI action set_dodge_rating chr_rows=%d source_chr=%d mode=%d value=%d",
		     "AI action set_unarmed_dodge_rating chr_rows=%d source_chr=%d value=%d",
		     "AI action set_flag chr_rows=%d source_chr=%d flags=0x%08x bank=%u",
		     "AI action unset_flag chr_rows=%d source_chr=%d flags=0x%08x bank=%u",
		     "AI action if_has_flag chr_rows=%d source_chr=%d flags=0x%08x bank=%u",
		     "AI action chr_set_flag chr=%d chr_rows=%d target_chr=%d flags=0x%08x bank=%u",
		     "AI action chr_unset_flag chr=%d chr_rows=%d target_chr=%d flags=0x%08x bank=%u",
		     "AI action if_chr_has_flag chr=%d chr_rows=%d target_chr=%d flags=0x%08x bank=%u",
	     }) {
		REQUIRE(scenario_runtime.find(proof) != std::string::npos);
	}
	{
		const char *mutations[][2] = {
			{"scenarioSourceAiGraphExecuteSetMorale",
				"chr->morale = (u8)morale"},
			{"scenarioSourceAiGraphExecuteAddMorale",
				"incrementByte(&chr->morale"},
			{"scenarioSourceAiGraphExecuteSubtractMorale",
				"decrementByte(&chr->morale"},
			{"scenarioSourceAiGraphExecuteSetAlertness",
				"chr->alertness = (u8)alertness"},
			{"scenarioSourceAiGraphExecuteAddAlertness",
				"incrementByte(&chr->alertness"},
			{"scenarioSourceAiGraphExecuteSubtractAlertness",
				"decrementByte(&chr->alertness"},
			{"scenarioSourceAiGraphExecuteSetHearDistance",
				"chr->hearingscale = distance"},
			{"scenarioSourceAiGraphExecuteSetViewDistance",
				"chr->visionrange = (u8)distance"},
			{"scenarioSourceAiGraphExecuteSetGrenadeProbability",
				"chr->grenadeprob = (u8)probability"},
			{"scenarioSourceAiGraphExecuteSetChrNum",
				"chrSetChrnum(chr, chrnum)"},
			{"scenarioSourceAiGraphExecuteAddHealth",
				"chrAddHealth(chr, amount)"},
			{"scenarioSourceAiGraphExecuteSetShield",
				"chrSetShield(chr, amount)"},
			{"scenarioSourceAiGraphExecuteSetReactionSpeed",
				"chr->speedrating = (s8)speed"},
			{"scenarioSourceAiGraphExecuteSetRecoverySpeed",
				"chr->arghrating = (s8)speed"},
			{"scenarioSourceAiGraphExecuteSetAccuracy",
				"chr->accuracyrating = (s8)accuracy"},
			{"scenarioSourceAiGraphExecuteSetDodgeRating",
				"chr->dodgerating = (s8)rating"},
			{"scenarioSourceAiGraphExecuteSetUnarmedDodgeRating",
				"chr->unarmeddodgerating = (s8)rating"},
			{"scenarioSourceAiGraphExecuteSetFlag",
				"chrSetFlags(chr, flags, bank)"},
			{"scenarioSourceAiGraphExecuteUnsetFlag",
				"chrUnsetFlags(chr, flags, bank)"},
			{"scenarioSourceAiGraphExecuteIfHasFlag",
				"chrHasFlag(chr, flags, bank)"},
		};

		for (const auto &mutation : mutations) {
			const std::string block = functionBlock(scenario_runtime,
				mutation[0]);
			REQUIRE(!block.empty());
			requireTokenOrder(block,
				"s_aiGraphRequireActiveCharacterAction(",
				mutation[1]);
		}
	}
	{
		const char *mutations[][3] = {
			{"scenarioSourceAiGraphExecuteChrSetFlag",
				"chrSetFlagsById(", "chrSetFlags(chr, flags, bank)"},
			{"scenarioSourceAiGraphExecuteChrUnsetFlag",
				"chrUnsetFlagsById(", "chrUnsetFlags(chr, flags, bank)"},
			{"scenarioSourceAiGraphExecuteIfChrHasFlag",
				"chrHasFlagById(", "chrHasFlag(chr, flags, bank)"},
		};

		for (const auto &mutation : mutations) {
			const std::string block = functionBlock(scenario_runtime,
				mutation[0]);
			REQUIRE(!block.empty());
			REQUIRE(block.find(mutation[1]) == std::string::npos);
			requireTokenOrder(block,
				"s_aiGraphRequireRuntimeCharacterRefFromBase(",
				mutation[2]);
		}
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetMaxDamage");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"chopperSetMaxDamage(hovercar, maxdamage)");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.unset_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.if_has_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_set_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_unset_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.if_chr_has_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_stage_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.unset_stage_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.if_stage_flag_eq+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_chrflag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_set_hidden_flag+ai/ailists.json") !=
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteSetChrflag",
		     "scenarioSourceAiGraphExecuteUnsetChrflag",
		     "scenarioSourceAiGraphExecuteIfHasChrflag",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterStatePointer(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterPointer(") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(",
			"chr->chrflags");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_obj_flag+objects.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_obj_flag tag=%d object_rows=%d") !=
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteSetObjFlag",
		     "scenarioSourceAiGraphExecuteUnsetObjFlag",
		     "scenarioSourceAiGraphExecuteIfObjHasFlag",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeObjectTag(") !=
		        std::string::npos);
		REQUIRE(block.find("objFindByTagId(tag_id)") == std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeObjectTag(",
			"s_aiGraphObjectFlagBank(obj, bank)");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_object_flags+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_savefile_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.unset_savefile_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.if_savefile_flag_set+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.if_savefile_flag_unset+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_action+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_action chr_rows=%d target_chr=%d action=%d clear_orders=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetAction");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(",
			"chr->myaction = (u8)action");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_team_orders+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_team_orders chr_rows=%d source_chr=%d action=%d chrs=%d checked=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetTeamOrders");
		REQUIRE(!block.empty());
		REQUIRE(countOccurrences(block,
			"s_aiGraphRequireRuntimeCharacterPointer(") >= 3);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_team_orders\", chr,",
			"chrnums = squadronGetChrIds(chr->squadron)");
		requireTokenOrder(block,
			"\"set_team_orders\", target, &chr_count, NULL",
			"target && target->model");
		requireTokenOrder(block,
			"\"set_team_orders\", target, &chr_count, NULL",
			"target->orders = MA_SHOOTING");
		REQUIRE(block.find("chrHasFlagById(") == std::string::npos);
		requireTokenOrder(block,
			"\"set_team_orders\", target, &chr_count, NULL",
			"chrHasFlag(target, CHRFLAG0_CAN_BACKOFF");
	}
	REQUIRE(scenario_runtime.find("AI action try_attack_amount chr_rows=%d source_chr=%d arg0=%d arg1=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteTryAttackAmount");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(\"try_attack_amount\"",
			"chrTryAttackAmount(chr, 512, 0, arg0, arg1)");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.retreat+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.find_cover+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.find_cover_within_dist+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.find_cover_outside_dist+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.go_to_cover+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.check_cover_out_of_sight+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.orbit_target+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_chr_preset_to_unalerted_teammate+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_chr_preset_to_unalerted_teammate chr_rows=%d source_chr=%d checked=%d candidate=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrPresetToUnalertedTeammate");
		REQUIRE(!block.empty());
		REQUIRE(countOccurrences(block,
			"s_aiGraphRequireRuntimeCharacterPointer(") >= 2);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chrnums = teamGetChrIds(chr->team)");
		requireTokenOrder(block,
			"\"set_chr_preset_to_unalerted_teammate\",\n\t\t\t\t\t\ttarget",
			"!target || !target->model");
		requireTokenOrder(block,
			"\"set_chr_preset_to_unalerted_teammate\",\n\t\t\t\t\t\ttarget",
			"chrSetChrPreset(chr, candidate_chrnum)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfOrders");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_orders chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_orders\"",
			"chr && chr->prop && chr->orders == order");
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_orders\"",
			"chr && chr->prop ? chr->orders : -1");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfHasOrders");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_has_orders chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_has_orders\"",
			"chr && chr->prop && chr->orders");
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_has_orders\"",
			"chr && chr->prop ? chr->orders : -1");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfNotListening");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_not_listening chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_not_listening\"",
			"chr && chr->prop && chr->listening == 0");
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_not_listening\"",
			"chr && chr->prop ? chr->listening : -1");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfAction");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_action chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_action\"",
			"chr && chr->prop && chr->myaction == action");
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_action\"",
			"chr && chr->prop ? chr->myaction : -1");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrInSquadronDoingAction");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_chr_in_squadron_doing_action chr_rows=%d source_chr=%d checked=%d") !=
		        std::string::npos);
		REQUIRE(countOccurrences(block,
			"s_aiGraphRequireRuntimeCharacterPointer(") >= 2);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chrnums = squadronGetChrIds(chr->squadron)");
		requireTokenOrder(block,
			"\"if_chr_in_squadron_doing_action\",\n\t\t\t\t\t\t\tother",
			"other && other->model");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfSquadronIsDead");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_squadron_is_dead chr_rows=%d checked=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr && chr->model");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfNumChrsInSquadronGreaterThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_num_chrs_in_squadron_greater_than chr_rows=%d checked=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr && chr->prop");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIncreaseSquadronAlertness");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action increase_squadron_alertness chr_rows=%d source_chr=%d checked=%d") !=
		        std::string::npos);
		REQUIRE(countOccurrences(block,
			"s_aiGraphRequireRuntimeCharacterPointer(") >= 2);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chrnums = teamGetChrIds(chr->team)");
		requireTokenOrder(block,
			"\"increase_squadron_alertness\",\n\t\t\t\t\t\ttarget",
			"target->model");
		requireTokenOrder(block,
			"\"increase_squadron_alertness\",\n\t\t\t\t\t\ttarget",
			"incrementByte(&target->alertness");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_squadron+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.face_cover+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.danger_cover+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.rebuild_teams+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.rebuild_squadrons+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_set_listening+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetReturnList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetShotList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteReturnList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteStop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteKneel") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSurrender") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteFadeOut") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteRemoveChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteBeSurprisedSurrender") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteKillBond") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteFaceEntity") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteApplyGsetDamage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrDamageChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteConsiderGrenadeThrow") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteDropItem") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryRunFromTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryJogToTargetProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryWalkToTargetProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryRunToTargetProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryGoToCoverProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryJogToChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryWalkToChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryRunToChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfCanHearAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfPatrolling") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfAlarmActive") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfGasActive") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfHearsTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfSawInjury") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfSawDeath") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfLosToTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfLosToAttackTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfTargetNearlyInSight") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfNearlyInTargetsSight") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetPadPresetToPadOnRouteToTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfSawTargetRecently") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfHeardTargetRecently") !=
	        std::string::npos);
	for (const char *symbol : {
		     "scenarioSourceAiGraphExecuteIfLosToChr",
		     "scenarioSourceAiGraphExecuteIfNeverBeenOnScreen",
		     "scenarioSourceAiGraphExecuteIfOnScreen",
		     "scenarioSourceAiGraphExecuteIfChrInOnScreenRoom",
		     "scenarioSourceAiGraphExecuteIfRoomIsOnScreen",
		     "scenarioSourceAiGraphExecuteIfTargetAimingAtMe",
		     "scenarioSourceAiGraphExecuteIfNearMiss",
		     "scenarioSourceAiGraphExecuteIfSeesSuspiciousItem",
		     "scenarioSourceAiGraphExecuteIfCheckFovWithTarget",
		     "scenarioSourceAiGraphExecuteIfTargetInFovLeft",
		     "scenarioSourceAiGraphExecuteIfTargetOutOfFovLeft",
		     "scenarioSourceAiGraphExecuteIfTargetInFov",
		     "scenarioSourceAiGraphExecuteIfTargetOutOfFov",
		     "scenarioSourceAiGraphExecuteIfDistanceToTargetLessThan",
		     "scenarioSourceAiGraphExecuteIfDistanceToTargetGreaterThan",
		     "scenarioSourceAiGraphExecuteIfChrDistanceToPadLessThan",
		     "scenarioSourceAiGraphExecuteIfChrDistanceToPadGreaterThan",
		     "scenarioSourceAiGraphExecuteIfDistanceToChrLessThan",
		     "scenarioSourceAiGraphExecuteIfDistanceToChrGreaterThan",
		     "scenarioSourceAiGraphExecuteIfAnyChrNearSelf",
		     "scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadLessThan",
		     "scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadGreaterThan",
		     "scenarioSourceAiGraphExecuteIfChrInRoom",
		     "scenarioSourceAiGraphExecuteIfTargetInRoom",
		     "scenarioSourceAiGraphExecuteIfChrHasObject",
		     "scenarioSourceAiGraphExecuteIfWeaponThrown",
		     "scenarioSourceAiGraphExecuteIfWeaponThrownOnObject",
		     "scenarioSourceAiGraphExecuteIfChrHasWeaponEquipped",
		     "scenarioSourceAiGraphExecuteIfGunUnclaimed",
		     "scenarioSourceAiGraphExecuteIfObjectHealthy",
		     "scenarioSourceAiGraphExecuteIfWaypointWithinQuadrant",
		     "scenarioSourceAiGraphExecuteSetPadPresetToTargetQuadrant",
		     "scenarioSourceAiGraphExecuteChrDeleteWeapon",
		     "scenarioSourceAiGraphExecuteIfTriggerShotList",
		     "scenarioSourceAiGraphExecuteEndLevel",
		     "scenarioSourceAiGraphExecuteEndCutscene",
		     "scenarioSourceAiGraphExecuteWarpJoToPad",
		     "scenarioSourceAiGraphExecuteSetCameraAnimation",
		     "scenarioSourceAiGraphExecuteIfInCutscene",
		     "scenarioSourceAiGraphExecuteIfCutsceneButtonPressed",
		     "scenarioSourceAiGraphExecuteReorientForCutsceneStop",
		     "scenarioSourceAiGraphExecuteWarpJoToTag",
		     "scenarioSourceAiGraphExecuteRevokeControl",
		     "scenarioSourceAiGraphExecuteGrantControl",
		     "scenarioSourceAiGraphExecutePlayerFadeIn",
		     "scenarioSourceAiGraphExecutePlayersFadeOut",
		     "scenarioSourceAiGraphExecuteIfColourFadeComplete",
		     "scenarioSourceAiGraphExecutePrepareWarpOrbit",
		     "scenarioSourceAiGraphExecuteBeginWarpLatch",
		     "scenarioSourceAiGraphExecuteIfWarpLatchComplete",
		     "scenarioSourceAiGraphExecuteDuplicateChr",
		     "scenarioSourceAiGraphExecuteEnableChr",
		     "scenarioSourceAiGraphExecuteDisableChr",
		     "scenarioSourceAiGraphExecuteEnableObj",
		     "scenarioSourceAiGraphExecuteDisableObj",
		     "scenarioSourceAiGraphExecuteChrMoveToPad",
		     "scenarioSourceAiGraphExecuteChrSetTeam",
		     "scenarioSourceAiGraphExecuteDamageChrByAmount",
		     "scenarioSourceAiGraphExecuteDoPresetAnimation",
		     "scenarioSourceAiGraphExecuteIfPlayerChrPortalDistanceLessThan",
		     "scenarioSourceAiGraphExecuteIfChrRepositionValid",
		     "scenarioSourceAiGraphExecuteReleaseCover",
		     "scenarioSourceAiGraphExecuteIfSafety2LessThan",
		     "scenarioSourceAiGraphExecuteIfPlayerUsingCmpOrAr34",
		     "scenarioSourceAiGraphExecuteDetectEnemyOnSameFloor",
		     "scenarioSourceAiGraphExecuteDetectEnemy",
		     "scenarioSourceAiGraphExecuteIfSafetyLessThan",
		     "scenarioSourceAiGraphExecuteIfTargetMovingSlowly",
		     "scenarioSourceAiGraphExecuteIfTargetMovingCloser",
		     "scenarioSourceAiGraphExecuteIfTargetMovingAway",
		     "scenarioSourceAiGraphExecuteIfSquadronIsDead",
		     "scenarioSourceAiGraphExecuteIfTrue",
		     "scenarioSourceAiGraphExecuteIfNumChrsInSquadronGreaterThan",
		     "scenarioSourceAiGraphExecuteIfNaturalAnim",
		     "scenarioSourceAiGraphExecuteIfY",
		     "scenarioSourceAiGraphExecuteIfSoundTimer",
		     "scenarioSourceAiGraphExecuteIfTargetYDifferenceLessThan",
		     "scenarioSourceAiGraphExecuteChrExplosions",
		     "scenarioSourceAiGraphExecuteSetTintedGlassEnabled",
		     "scenarioSourceAiGraphExecuteHovercopterFireRocket",
		     "scenarioSourceAiGraphExecuteChrAdjustMotionBlur",
		     "scenarioSourceAiGraphExecutePunchOrKick",
		     "scenarioSourceAiGraphExecuteSetTargetToEyespyIfInSight",
		     "scenarioSourceAiGraphExecuteMiniSkedarTryPounce",
		     "scenarioSourceAiGraphExecuteIfObjectDistanceToPadLessThan",
		     "scenarioSourceAiGraphExecuteAvoid",
		     "scenarioSourceAiGraphExecuteTitleInitMode",
		     "scenarioSourceAiGraphExecuteTryExitTitle",
		     "scenarioSourceAiGraphExecuteChrEmitSparks",
		     "scenarioSourceAiGraphExecuteSetDrCarollImages",
		     "scenarioSourceAiGraphExecuteSayQuip",
		     "scenarioSourceAiGraphExecuteSayCiStaffQuip",
		     "scenarioSourceAiGraphExecuteShuffleRuinsPillars",
		     "scenarioSourceAiGraphExecuteShufflePelagicSwitches",
		     "scenarioSourceAiGraphExecuteTryAttackAmount",
	     }) {
		REQUIRE(scenario_runtime_h.find(symbol) != std::string::npos);
	}
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetPunchDodgeList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetShootingAtMeList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetDarkRoomList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetPlayerDeadList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetPath") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteStartPatrol") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryStartAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteActivateAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteDeactivateAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetPadPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrSetPadPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrCopyPadPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetChrPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetChrTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteAddMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrAddMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSubtractMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteAddAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrAddAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSubtractAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIncreaseSquadronAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetHearDistance") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetViewDistance") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetGrenadeProbability") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetChrNum") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetMaxDamage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteAddHealth") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetShield") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetReactionSpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetRecoverySpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetAccuracy") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetDodgeRating") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetUnarmedDodgeRating") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteUnsetFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfHasFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrSetFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrUnsetFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfChrHasFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetStageFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteUnsetStageFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfStageFlagEq") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetSavefileFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteUnsetSavefileFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfSavefileFlagIsSet") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfSavefileFlagIsUnset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetAction") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetTeamOrders") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteRetreat") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteFindCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteFindCoverWithinDist") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteFindCoverOutsideDist") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteGoToCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteCheckCoverOutOfSight") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteOrbitTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetChrPresetToUnalertedTeammate") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetSquadron") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteFaceCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteDangerCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteRebuildTeams") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteRebuildSquadrons") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrSetListening") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteWalkToPad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteRunToPad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteJogToPad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteGoToPadPreset") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetList(cmd[2]") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetReturnList(cmd[2]") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetShotList(ailistid)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteReturnList()") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteStop(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteKneel(g_Vars.chrdata)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSurrender(g_Vars.chrdata)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteFadeOut(g_Vars.chrdata)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteRemoveChr(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteBeSurprisedSurrender(g_Vars.chrdata)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteKillBond()") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTrySidestep(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryAttackStand(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfAttacking(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryModifyAttack(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteFaceEntity(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteApplyGsetDamage(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrDamageChr(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteConsiderGrenadeThrow(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteDropItem(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryRunFromTarget(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryJogToTargetProp(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryWalkToTargetProp(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryRunToTargetProp(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryGoToCoverProp(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryJogToChr(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryWalkToChr(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryRunToChr(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfCanHearAlarm(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfPatrolling(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfAlarmActive(cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfGasActive(cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfHearsTarget(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfSawInjury(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfSawDeath(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfLosToTarget(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfLosToAttackTarget(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfTargetNearlyInSight(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfNearlyInTargetsSight(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetPadPresetToPadOnRouteToTarget(") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfSawTargetRecently(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfHeardTargetRecently(g_Vars.chrdata") !=
	        std::string::npos);
	for (const char *call : {
		     "scenarioSourceAiGraphExecuteIfLosToChr(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfNeverBeenOnScreen(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfOnScreen(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfChrInOnScreenRoom(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfRoomIsOnScreen(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetAimingAtMe(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfNearMiss(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfSeesSuspiciousItem(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfCheckFovWithTarget(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetInFovLeft(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetOutOfFovLeft(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetInFov(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetOutOfFov(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfDistanceToTargetLessThan(",
		     "scenarioSourceAiGraphExecuteIfDistanceToTargetGreaterThan(",
		     "scenarioSourceAiGraphExecuteIfChrDistanceToPadLessThan(",
		     "scenarioSourceAiGraphExecuteIfChrDistanceToPadGreaterThan(",
		     "scenarioSourceAiGraphExecuteIfDistanceToChrLessThan(",
		     "scenarioSourceAiGraphExecuteIfDistanceToChrGreaterThan(",
		     "scenarioSourceAiGraphExecuteIfAnyChrNearSelf(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadLessThan(",
		     "scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadGreaterThan(",
		     "scenarioSourceAiGraphExecuteIfChrInRoom(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetInRoom(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfChrHasObject(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfWeaponThrown(cmd[2]",
		     "scenarioSourceAiGraphExecuteIfWeaponThrownOnObject(cmd->b2",
		     "scenarioSourceAiGraphExecuteIfChrHasWeaponEquipped(",
		     "scenarioSourceAiGraphExecuteIfGunUnclaimed(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfObjectHealthy(cmd[2]",
		     "scenarioSourceAiGraphExecuteIfWaypointWithinQuadrant(",
		     "scenarioSourceAiGraphExecuteSetPadPresetToTargetQuadrant(",
		     "scenarioSourceAiGraphExecuteChrDeleteWeapon(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTriggerShotList(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteEndLevel()",
		     "scenarioSourceAiGraphExecuteEndCutscene()",
		     "scenarioSourceAiGraphExecuteWarpJoToPad(pad_id)",
		     "scenarioSourceAiGraphExecuteSetCameraAnimation(anim_id)",
		     "scenarioSourceAiGraphExecuteIfInCutscene(cmd[2])",
		     "scenarioSourceAiGraphExecuteIfCutsceneButtonPressed(cmd[2])",
		     "scenarioSourceAiGraphExecuteReorientForCutsceneStop(cmd[2])",
		     "scenarioSourceAiGraphExecuteWarpJoToTag(cmd[2]",
		     "scenarioSourceAiGraphExecuteRevokeControl(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteGrantControl(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecutePlayerFadeIn(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecutePlayersFadeOut()",
		     "scenarioSourceAiGraphExecuteIfColourFadeComplete(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecutePrepareWarpOrbit(range",
		     "scenarioSourceAiGraphExecuteBeginWarpLatch()",
		     "scenarioSourceAiGraphExecuteIfWarpLatchComplete(cmd[2])",
		     "scenarioSourceAiGraphExecuteDuplicateChr(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteEnableChr(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteDisableChr(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteEnableObj(cmd[2])",
		     "scenarioSourceAiGraphExecuteDisableObj(cmd[2])",
		     "scenarioSourceAiGraphExecuteChrMoveToPad(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteChrSetTeam(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteDamageChrByAmount(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteDoPresetAnimation(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfPlayerChrPortalDistanceLessThan(",
		     "scenarioSourceAiGraphExecuteIfChrRepositionValid(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteReleaseCover(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteTryAttackAmount(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfSafety2LessThan(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfPlayerUsingCmpOrAr34(cmd[2])",
		     "scenarioSourceAiGraphExecuteDetectEnemyOnSameFloor(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteDetectEnemy(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfSafetyLessThan(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetMovingSlowly(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetMovingCloser(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetMovingAway(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfSquadronIsDead(cmd[2], cmd[3])",
		     "scenarioSourceAiGraphExecuteIfTrue(cmd[5])",
		     "scenarioSourceAiGraphExecuteIfNumChrsInSquadronGreaterThan(",
		     "scenarioSourceAiGraphExecuteIfNaturalAnim(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfY(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfSoundTimer(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetYDifferenceLessThan(",
		     "scenarioSourceAiGraphExecuteChrExplosions(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteSetTintedGlassEnabled(cmd[2])",
		     "scenarioSourceAiGraphExecuteHovercopterFireRocket(cmd[2])",
		     "scenarioSourceAiGraphExecuteChrAdjustMotionBlur(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecutePunchOrKick(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteSetTargetToEyespyIfInSight(",
		     "scenarioSourceAiGraphExecuteMiniSkedarTryPounce(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfObjectDistanceToPadLessThan(",
		     "scenarioSourceAiGraphExecuteAvoid(g_Vars.chrdata)",
		     "scenarioSourceAiGraphExecuteTitleInitMode(cmd[2])",
		     "scenarioSourceAiGraphExecuteTryExitTitle(cmd[2])",
		     "scenarioSourceAiGraphExecuteChrEmitSparks(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteSetDrCarollImages(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteSayQuip(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteSayCiStaffQuip(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteShuffleRuinsPillars(cmd)",
		     "scenarioSourceAiGraphExecuteShufflePelagicSwitches()",
	     }) {
		REQUIRE(chraicommands.find(call) != std::string::npos);
	}
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetPunchDodgeList(ailistid)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetShootingAtMeList(ailistid)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetDarkRoomList(ailistid)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetPlayerDeadList(ailistid)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteJogToPad(g_Vars.chrdata, pad)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteGoToPadPreset(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteWalkToPad(g_Vars.chrdata, pad)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteRunToPad(g_Vars.chrdata, pad)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetPath(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteStartPatrol(g_Vars.chrdata)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteHovercarBeginPath(cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetVehicleSpeed(speedaim, speedtime)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetRotorSpeed(speedaim, speedtime)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryStartAlarm(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteActivateAlarm()") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteDeactivateAlarm()") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetPadPreset(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrSetPadPreset(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrCopyPadPreset(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetChrPreset(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetChrTarget(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetMorale(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteAddMorale(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrAddMorale(g_Vars.chrdata, cmd[2]") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSubtractMorale(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetAlertness(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteAddAlertness(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrAddAlertness(g_Vars.chrdata, cmd[2]") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSubtractAlertness(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIncreaseSquadronAlertness(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetHearDistance(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetViewDistance(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetGrenadeProbability(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetChrNum(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetMaxDamage(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteAddHealth(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetShield(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetReactionSpeed(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetRecoverySpeed(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetAccuracy(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetDodgeRating(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetUnarmedDodgeRating(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetFlag(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteUnsetFlag(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfHasFlag(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrSetFlag(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrUnsetFlag(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfChrHasFlag(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetStageFlag(flags)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteUnsetStageFlag(flags)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfStageFlagEq(flags") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetSavefileFlag(cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteUnsetSavefileFlag(cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfSavefileFlagIsSet(cmd[2], cmd[3])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfSavefileFlagIsUnset(cmd[2], cmd[3])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetAction(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetTeamOrders(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteRetreat(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteFindCover(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteFindCoverWithinDist(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteFindCoverOutsideDist(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteGoToCover(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteCheckCoverOutOfSight(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteOrbitTarget(") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetChrPresetToUnalertedTeammate(") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetSquadron(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteFaceCover(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteDangerCover(g_Vars.chrdata)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteRebuildTeams()") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteRebuildSquadrons()") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrSetListening(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.trigger.volume.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.global.settings.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.pads.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.lists.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_return_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_shot_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.return_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.stop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.kneel") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.surrender") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.fade_out") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.remove_chr") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_sidestep") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_attack_stand") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_attacking") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_modify_attack") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.face_entity") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.apply_gset_damage") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_damage_chr") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.consider_grenade_throw") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.drop_item") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_run_from_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_jog_to_target_prop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_walk_to_target_prop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_run_to_target_prop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_go_to_cover_prop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_jog_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_walk_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_run_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_do_animation") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.be_surprised_one_hand") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.be_surprised_look_around") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.be_surprised_surrender") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.random") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_random_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_random_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.print") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.noop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_punch_dodge_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_shooting_at_me_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_dark_room_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_player_dead_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.jog_to_pad") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.go_to_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.walk_to_pad") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.run_to_pad") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_path") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.start_patrol") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_start_alarm") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.activate_alarm") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.deactivate_alarm") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_morale") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.add_morale") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_add_morale") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.subtract_morale") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.add_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_add_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.subtract_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.increase_squadron_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_hear_distance") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_view_distance") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_grenade_probability") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_chr_num") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_max_damage") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.add_health") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_shield") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_reaction_speed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_recovery_speed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_accuracy") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_dodge_rating") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_unarmed_dodge_rating") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.unset_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_has_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_unset_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_chr_has_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_stage_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.unset_stage_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_stage_flag_eq") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.unset_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_has_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_unset_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_chr_has_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_hidden_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_unset_hidden_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_chr_has_hidden_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_obj_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.unset_obj_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_obj_has_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.open_door") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.close_door") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_door_state") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_object_is_door") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.lock_door") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.unlock_door") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_door_locked") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_lift_stationary") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.lift_go_to_stop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_lift_at_stop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.activate_lift") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_using_lift") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.configure_rain") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.configure_snow") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.switch_to_alt_sky") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_wind_speed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_lights") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_room_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.show_cutscene_chrs") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.configure_environment") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_distance_to_target2_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_distance_to_target2_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.speak") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.play_sound") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.assign_sound") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.audio_mute_channel") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_channel_free") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_object_sound_volume") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_object_sound_volume_by_distance") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_object_sound_playing") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.play_repeating_sound_from_object") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.play_sound_from_entity") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.play_repeating_sound_from_pad") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_object_sound_volume_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.play_sound_from_prop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.play_temporary_primary_track") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.play_x_track") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.stop_ambient_track") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_draw_weapon") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_draw_weapon_in_cutscene") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_player_force_speed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_invincible") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_player_is_invincible") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_chr_has_no_gun") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_delete_weapon") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_trigger_shot_list") !=
	        std::string::npos);
	for (const char *node : {
		     "scenario.ai.action.end_level",
		     "scenario.ai.action.end_cutscene",
		     "scenario.ai.action.warp_jo_to_pad",
		     "scenario.ai.action.warp_jo_to_tag",
		     "scenario.ai.action.revoke_control",
		     "scenario.ai.action.grant_control",
		     "scenario.ai.action.player_fade_in",
		     "scenario.ai.action.players_fade_out",
		     "scenario.ai.condition.if_colour_fade_complete",
		     "scenario.ai.action.prepare_warp_orbit",
		     "scenario.ai.action.begin_warp_latch",
		     "scenario.ai.condition.if_warp_latch_complete",
		     "scenario.ai.action.set_camera_animation",
		     "scenario.ai.condition.if_in_cutscene",
		     "scenario.ai.condition.if_cutscene_button_pressed",
		     "scenario.ai.action.reorient_for_cutscene_stop",
	     }) {
		REQUIRE(scenario_extractor.find(node) != std::string::npos);
	}
	REQUIRE(scenario_extractor.find("scenario.ai.action.do_gun_command") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_distance_to_gun_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.recover_gun") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_copy_properties") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.player_auto_walk") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_player_auto_walk_finished") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_obj_in_room") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("\\\"objects\\\": \\\"objects.json\\\", \\\"pads\\\": \\\"pads.json\\\", \\\"scene\\\": \\\"scene.glb\\\", \\\"target\\\": \\\"object.room\\\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("\\\"from\\\": \\\"scenario.pads\\\", \\\"to\\\": \\\"scenario.ai.condition.if_obj_in_room\\\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_player_looking_at_object") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_target_is_player") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_kill") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.remove_weapon_from_inventory") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.clear_inventory") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.release_object") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_grab_object") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.toggle_p1p2") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_p1p2") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_cloaked") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_autogun_target_team") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_objective_complete") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_objective_failed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_all_objectives_complete") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_difficulty_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_difficulty_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_stage_timer_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_stage_timer_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_stage_id_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_stage_id_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_num_players_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_kill_count_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_num_knocked_out_chrs") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.kill_bond") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_num_arghs_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_chr_health_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_chr_shield_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_injured") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_morale_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_idle") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_chr_dead") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_can_see_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_pouncebits_eq") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_training_pc_holographed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_player_using_device") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_begin_or_end_teleport") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_chr_teleport_full_white") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_cutscene_weapon") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.fade_screen") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_fade_complete") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_chr_hudpiece_visible") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_passive_mode") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_firing_in_cutscene") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_portal_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_music_event_queue_is_empty") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_coop_mode") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.remove_references_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_toggle_model_part") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.obj_set_model_part_visible") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_obj_health_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_obj_health") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_chr_special_death_animation") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_room_to_search") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_savefile_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.unset_savefile_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_savefile_flag_set") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_savefile_flag_unset") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.restart_timer") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_timer_greater_than_random") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_countdown_timer") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_countdown_timer_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.show_hudmsg") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.show_hudmsg_middle") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.show_hudmsg_top_middle") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.hovercar_begin_path") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_vehicle_speed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_rotor_speed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_explosions") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_object_distance_to_pad_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.say_quip") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.say_ci_staff_quip") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.shuffle_ruins_pillars") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.shuffle_pelagic_switches") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_action") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_team_orders") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.retreat") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.find_cover") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.find_cover_within_dist") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.find_cover_outside_dist") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.go_to_cover") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.check_cover_out_of_sight") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.orbit_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_chr_preset_to_unalerted_teammate") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_squadron") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.face_cover") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.danger_cover") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.release_cover") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.rebuild_teams") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.rebuild_squadrons") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_listening") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_dangerous_object_nearby") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.shuffle_investigation_terminals") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.heli_arm_weapons") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_attack_amount") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_can_hear_alarm") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_patrolling") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_alarm_active") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_gas_active") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_hears_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_saw_injury") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_saw_death") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_los_to_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_los_to_attack_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_target_nearly_in_sight") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_nearly_in_targets_sight") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_pad_preset_to_pad_on_route_to_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_saw_target_recently") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_heard_target_recently") !=
	        std::string::npos);
	for (const char *kind : {
		     "scenario.ai.condition.if_los_to_chr",
		     "scenario.ai.condition.if_never_been_on_screen",
		     "scenario.ai.condition.if_on_screen",
		     "scenario.ai.condition.if_chr_in_on_screen_room",
		     "scenario.ai.condition.if_room_is_on_screen",
		     "scenario.ai.condition.if_target_aiming_at_me",
		     "scenario.ai.condition.if_near_miss",
		     "scenario.ai.condition.if_sees_suspicious_item",
		     "scenario.ai.condition.if_target_in_fov_left",
		     "scenario.ai.condition.if_check_fov_with_target",
		     "scenario.ai.condition.if_target_out_of_fov_left",
		     "scenario.ai.condition.if_target_in_fov",
		     "scenario.ai.condition.if_target_out_of_fov",
		     "scenario.ai.condition.if_distance_to_target_less_than",
		     "scenario.ai.condition.if_distance_to_target_greater_than",
		     "scenario.ai.condition.if_chr_distance_to_pad_less_than",
		     "scenario.ai.condition.if_chr_distance_to_pad_greater_than",
		     "scenario.ai.condition.if_distance_to_chr_less_than",
		     "scenario.ai.condition.if_distance_to_chr_greater_than",
		     "scenario.ai.condition.if_any_chr_near_self",
		     "scenario.ai.condition.if_distance_from_target_to_pad_less_than",
		     "scenario.ai.condition.if_distance_from_target_to_pad_greater_than",
		     "scenario.ai.condition.if_chr_in_room",
		     "scenario.ai.condition.if_target_in_room",
		     "scenario.ai.condition.if_chr_has_object",
		     "scenario.ai.condition.if_weapon_thrown",
		     "scenario.ai.condition.if_weapon_thrown_on_object",
		     "scenario.ai.condition.if_chr_has_weapon_equipped",
		     "scenario.ai.condition.if_gun_unclaimed",
		     "scenario.ai.condition.if_object_healthy",
		     "scenario.ai.condition.if_chr_activated_object",
		     "scenario.ai.action.obj_interact",
		     "scenario.ai.action.destroy_object",
		     "scenario.ai.action.drop_object_from_chr",
		     "scenario.ai.action.chr_drop_items",
		     "scenario.ai.action.chr_drop_weapon",
		     "scenario.ai.action.give_object_to_chr",
		     "scenario.ai.action.object_move_to_pad",
		     "scenario.ai.condition.if_chr_not_talking",
		     "scenario.ai.condition.if_orders",
		     "scenario.ai.condition.if_has_orders",
		     "scenario.ai.condition.if_chr_in_squadron_doing_action",
		     "scenario.ai.condition.if_chr_listening",
		     "scenario.ai.condition.if_not_listening",
		     "scenario.ai.condition.if_chr_injured_target",
		     "scenario.ai.condition.if_action",
		     "scenario.ai.condition.if_chr_ammo_quantity_less_than",
		     "scenario.ai.condition.if_chr_target",
		     "scenario.ai.condition.if_compare_chr_presets_team",
		     "scenario.ai.condition.if_human",
		     "scenario.ai.condition.if_skedar",
		     "scenario.ai.condition.if_prop_preset_blocking_sight_to_target",
		     "scenario.ai.action.remove_object_at_prop_preset",
		     "scenario.ai.condition.if_prop_preset_height_less_than",
		     "scenario.ai.action.set_target",
		     "scenario.ai.condition.if_presets_target_is_not_my_target",
		     "scenario.ai.action.set_chr_preset_to_chr_near_self",
		     "scenario.ai.action.set_chr_preset_to_chr_near_pad",
		     "scenario.ai.condition.if_waypoint_within_quadrant",
		     "scenario.ai.action.set_pad_preset_to_target_quadrant",
	     }) {
		REQUIRE(scenario_extractor.find(kind) != std::string::npos);
	}
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_copy_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_chr_preset") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_chr_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.global.settings") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("trigger.volume.%04u") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: setup behavior link source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: setup behavior link registered from graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("refusing legacy-only setup behavior") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("setup_link_logged_mask") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupBehaviorLinkLogBit") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupGraphValidateBlockedPathWaypoints") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupFindRecordByOrder") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupValidateBehaviorLinkSourceTargets") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupValidateBehaviorLinkTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("is not a public setup.fields.json row") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("expected source type %u") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"linked_guns.weapon_1\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"linked_guns.weapon_2\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"lift_door_link.door\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"lift_door_link.lift\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"safe_item.safe\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"safe_item.door\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"padlocked_door.door\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"conditional_scenery.trigger\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"conditional_scenery.unexploded\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"conditional_scenery.exploded\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"blocked_path.blocker\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("blocked_path waypoint_1 %d does not point at a source row") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("blocked_path waypoint_2 %d does not point at a source row") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.setup.links+setup.fields.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_bindLevelGraphTablePath") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_loadLevelVolumeSourceRows") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_activeGraphPathForScenario") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing level graph table") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_ActiveScenarioGraphs.volumes_path") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_ActiveScenarioGraphs.setup_fields_path") !=
	        std::string::npos);
	{
		const std::string behavior_link =
			functionBlock(scenario_runtime,
				"scenarioSourceSetupGraphRecordBehaviorLink");
		REQUIRE(!behavior_link.empty());
		REQUIRE(behavior_link.find(
				"s_setupGraphValidateBlockedPathWaypoints") !=
		        std::string::npos);
		requireTokenOrder(behavior_link,
			"s_setupGraphValidateBlockedPathWaypoints",
			"s_setupBehaviorLinkTargetsMatch");
	}
	{
		const std::string collect_links =
			functionBlock(scenario_runtime,
				"s_setupCollectBehaviorLinkSource");
		REQUIRE(!collect_links.empty());
		requireTokenOrder(collect_links,
			"s_setupFillBehaviorLink",
			"s_setupValidateBehaviorLinkSourceTargets");
		requireTokenOrder(collect_links,
			"s_setupValidateBehaviorLinkSourceTargets",
			"s_ActiveScenarioGraphs.setup_link_count = count");
	}
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: activated mission graph") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphRecordCheck") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphRecordInsert") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphRecordCriterionStatus") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphRecordObjectState") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphRecordStageFlags") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphHasStageFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceMissionGraphRecordPhase") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphGetCriterionType") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphRecordEvaluate") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: objective runtime source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: objective insert matched graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: objective criteria evaluated from graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: objective check routed through graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: objective state updated from graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: objective object state updated from graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: mission flags updated in graph runtime") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: phase source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: phase transition from graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.mission.phase+mission.graph.nodes") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.mission.phase") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("mission_phase_node_count") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("mission_stage_flags") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.objective.operands+graph.objective.state+graph.objective.object_state+graph.mission.flags") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.objective.operands+graph.objective.state\"") ==
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.objective.operands+og.objective.state") ==
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing executable mission objective source nodes") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing executable mission objective source rows") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("assetSourceDebugIsEnabledFor(ASSET_MISSION)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("empty nodes") != std::string::npos);
	REQUIRE(scenario_runtime.find("missing mission objective nodes") !=
	        std::string::npos);
	const std::string objectives_runtime = readTextFile("src/game/objectives.c");
	const std::string inv_runtime = readTextFile("src/game/inv.c");
	const std::string objectives_reset_runtime =
		readTextFile("src/game/objectivesreset.c");
	const std::string stage_reset_runtime =
		readTextFile("src/game/game_00b820.c");
	const std::string chraction_runtime = readTextFile("src/game/chraction.c");
	const std::string netmsg_runtime = readTextFile("port/src/net/netmsg.c");
	const std::string pdmain_runtime = readTextFile("port/src/pdmain.c");
	const std::string lv_runtime = readTextFile("src/game/lv.c");
	REQUIRE(objectives_runtime.find("scenario_source_runtime.h") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceObjectiveGraphRecordCheck(index, objstatus)") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("objectiveCheckGraphSource") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceObjectiveGraphGetCriterionType(index, i") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceObjectiveGraphGetCriterionOperand(index, i") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("objectiveEvaluateGraphRequirement(&operand)") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("objectiveGraphRequirementUsesMissionFlags") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("objectiveGraphRequirementUsesObjectState") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceObjectiveGraphHasStageFlag") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceObjectiveGraphRecordCriterionStatus") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceMissionGraphRecordPhase(") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("\"objectives.check\"") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("\"objectiveIsAllComplete\"") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceLevelGraphCheckPadRoom(criteria->pad") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("\"objective.enter_room\"") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("\"objective.throw_in_room\"") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceObjectiveGraphRecordObjectState") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("missing criteria graph runtime status") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("missing graph mission flag state") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("missing graph object state") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("chrHasStageFlag(NULL, operand->stage_flag_mask)") ==
	        std::string::npos);
	const size_t graph_eval_start = objectives_runtime.find(
		"static s32 objectiveEvaluateGraphRequirement");
	const size_t graph_eval_end = objectives_runtime.find(
		"static bool objectiveCheckGraphSource");
	REQUIRE(graph_eval_start != std::string::npos);
	REQUIRE(graph_eval_end != std::string::npos);
	REQUIRE(graph_eval_end > graph_eval_start);
	const std::string graph_eval = objectives_runtime.substr(
		graph_eval_start, graph_eval_end - graph_eval_start);
	REQUIRE(graph_eval.find("objFindByTagId") == std::string::npos);
	REQUIRE(graph_eval.find("invHasProp") == std::string::npos);
	REQUIRE(inv_runtime.find("objectiveRecordPropState(insertedprop)") !=
	        std::string::npos);
	REQUIRE(inv_runtime.find("objectiveRecordPropState(removedprop)") !=
	        std::string::npos);
	REQUIRE(objectives_reset_runtime.find("scenario_source_runtime.h") !=
	        std::string::npos);
	REQUIRE(objectives_reset_runtime.find("scenarioSourceObjectiveGraphRecordInsert(objective)") !=
	        std::string::npos);
	REQUIRE(stage_reset_runtime.find("scenarioSourceObjectiveGraphRecordStageFlags(g_StageFlags)") !=
	        std::string::npos);
	REQUIRE(chraction_runtime.find("scenario_source_runtime.h") !=
	        std::string::npos);
	REQUIRE(chraction_runtime.find("scenarioSourceObjectiveGraphRecordStageFlags(g_StageFlags)") !=
	        std::string::npos);
	REQUIRE(chraction_runtime.find("chrSetStageFlag(NULL, STAGEFLAG_EYESPY_DESTROYED)") !=
	        std::string::npos);
	REQUIRE(chraction_runtime.find("g_StageFlags |= STAGEFLAG_EYESPY_DESTROYED") ==
	        std::string::npos);
	REQUIRE(netmsg_runtime.find("scenario_source_runtime.h") !=
	        std::string::npos);
	REQUIRE(netmsg_runtime.find("scenarioSourceObjectiveGraphRecordStageFlags(g_StageFlags)") !=
	        std::string::npos);
	REQUIRE(pdmain_runtime.find("scenario_source_runtime.h") !=
	        std::string::npos);
	REQUIRE(pdmain_runtime.find("scenarioSourceMissionGraphRecordPhase(\"end\", \"mainEndStage\")") !=
	        std::string::npos);
	REQUIRE(lv_runtime.find("scenarioSourceMissionGraphRecordPhase(\"active\", \"lvTick.start\")") !=
	        std::string::npos);
	REQUIRE(lv_runtime.find("scenarioSourceLevelGraphRecordTick(\"lvTick.start\")") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("PDMETA_FAST_CACHE_KIND \"pdmeta_table_backed_v13_hydrated_metadata_source\"") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("PDMETA_SCENARIO_DEP_CACHE_KIND") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("pdscenario_scene_glb_clean_public_v99_standalone_backfill_collision_obj_collision_flags_json_room_lights_json_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_alphamask_quip_shuffle_graph_portals_json_objects_json_setup_fields_json_ai_lists_json_ai_command_graph_navhashes_objectives_spawns_volumes_pads_paths_json_navtables_json") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("pdscenario_scene_glb_clean_public_v96_") ==
	        std::string::npos);
	REQUIRE(meta_extractor.find("scenario_graph_cache = \" PDMETA_SCENARIO_DEP_CACHE_KIND") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("s_existingArchiveEntryContains") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("mission.objectives.source") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("mission.objective.source") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("mission.objective.criteria.source") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("mission.phase.source") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("mission.phase.active") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("s_loadScenarioObjectiveRows") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("mission.behavior.parity_backend") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("dependencies/assets/scenarios/%s.pdscenario::objectives.json#%s") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("pd2.mission.objectives.v1") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("objectives_file = objectives.json") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("\\\"objectives_file\\\": \\\"objectives.json\\\"") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("\\\"briefing_file\\\": \\\"briefing.json\\\"") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("\\\"min_players\\\": %d") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("\\\"type_key\\\": \\\"%s\\\"") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("objectives_file = objectives.tsv") ==
	        std::string::npos);
	REQUIRE(meta_extractor.find("\"operand_kind\"") != std::string::npos);
	REQUIRE(meta_extractor.find("Original mission objectives") ==
	        std::string::npos);
	REQUIRE(meta_extractor.find("primary\\tOriginal mission objectives\\toriginal_perfect_dark_setup") ==
	        std::string::npos);
	REQUIRE(conformance.find("mission.graph.json must contain executable mission/objective nodes") !=
	        std::string::npos);
	REQUIRE(conformance.find("mission.graph.json must include mission objective graph nodes") !=
	        std::string::npos);
	REQUIRE(conformance.find("mission.graph.json must include mission phase graph nodes") !=
	        std::string::npos);
	REQUIRE(conformance.find("mission.graph.json must bind mission.objectives.source to objectives.json") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2.mission.briefing.v1") !=
	        std::string::npos);
	REQUIRE(conformance.find("_meta/manifest.json must declare {field} = {expected}") !=
	        std::string::npos);
	REQUIRE(conformance.find("(\"briefing_file\", \"briefing.json\")") !=
	        std::string::npos);
	REQUIRE(conformance.find("level.graph.json must include exactly one {description} graph node") !=
	        std::string::npos);
	REQUIRE(conformance.find("scenario.ini must declare ai_lists_file = ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(conformance.find("scenario.ini must declare paths_file = navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(conformance.find("path_ref id must fit native u8") !=
	        std::string::npos);
	REQUIRE(conformance.find("path_ref must preserve path order") ==
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.lists.source\": \"AI list source\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.pads.source\": \"pad source\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.navigation.paths.source\": \"navigation path source\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_list\": \"AI set_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_return_list\": \"AI set_return_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_shot_list\": \"AI set_shot_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.return_list\": \"AI return_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.stop\": \"AI stop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.kneel\": \"AI kneel action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.surrender\": \"AI surrender action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.fade_out\": \"AI fade_out action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.remove_chr\": \"AI remove_chr action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_sidestep\": \"AI try_sidestep action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_attack_stand\": \"AI try_attack_stand action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_attacking\": \"AI if_attacking condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_modify_attack\": \"AI try_modify_attack action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.face_entity\": \"AI face_entity action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.apply_gset_damage\": \"AI apply_gset_damage action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_damage_chr\": \"AI chr_damage_chr action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.consider_grenade_throw\": \"AI consider_grenade_throw condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.drop_item\": \"AI drop_item action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_run_from_target\": \"AI try_run_from_target action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_jog_to_target_prop\": \"AI try_jog_to_target_prop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_walk_to_target_prop\": \"AI try_walk_to_target_prop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_run_to_target_prop\": \"AI try_run_to_target_prop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_go_to_cover_prop\": \"AI try_go_to_cover_prop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_jog_to_chr\": \"AI try_jog_to_chr action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_walk_to_chr\": \"AI try_walk_to_chr action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_run_to_chr\": \"AI try_run_to_chr action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_do_animation\": \"AI chr_do_animation action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.be_surprised_one_hand\": \"AI be_surprised_one_hand action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.be_surprised_look_around\": \"AI be_surprised_look_around action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.be_surprised_surrender\": \"AI be_surprised_surrender action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.random\": \"AI random action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_random_less_than\": \"AI if_random_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_random_greater_than\": \"AI if_random_greater_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.print\": \"AI print action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.noop\": \"AI no-op action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_punch_dodge_list\": \"AI set_punch_dodge_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_shooting_at_me_list\": \"AI set_shooting_at_me_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_dark_room_list\": \"AI set_dark_room_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_player_dead_list\": \"AI set_player_dead_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.jog_to_pad\": \"AI jog_to_pad action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.go_to_pad_preset\": \"AI go_to_pad_preset action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.walk_to_pad\": \"AI walk_to_pad action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.run_to_pad\": \"AI run_to_pad action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_path\": \"AI set_path action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.start_patrol\": \"AI start_patrol action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_start_alarm\": \"AI try_start_alarm action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.activate_alarm\": \"AI activate_alarm action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.deactivate_alarm\": \"AI deactivate_alarm action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_morale\": \"AI set_morale action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.add_morale\": \"AI add_morale action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_add_morale\": \"AI chr_add_morale action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.subtract_morale\": \"AI subtract_morale action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_alertness\": \"AI set_alertness action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.add_alertness\": \"AI add_alertness action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_add_alertness\": \"AI chr_add_alertness action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.subtract_alertness\": \"AI subtract_alertness action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.increase_squadron_alertness\": \"AI increase_squadron_alertness action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_hear_distance\": \"AI set_hear_distance action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_view_distance\": \"AI set_view_distance action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_grenade_probability\": \"AI set_grenade_probability action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_chr_num\": \"AI set_chr_num action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_max_damage\": \"AI set_max_damage action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.add_health\": \"AI add_health action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_shield\": \"AI set_shield action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_reaction_speed\": \"AI set_reaction_speed action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_recovery_speed\": \"AI set_recovery_speed action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_accuracy\": \"AI set_accuracy action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_dodge_rating\": \"AI set_dodge_rating action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_unarmed_dodge_rating\": \"AI set_unarmed_dodge_rating action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_flag\": \"AI set_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.unset_flag\": \"AI unset_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_has_flag\": \"AI if_has_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_flag\": \"AI chr_set_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_unset_flag\": \"AI chr_unset_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_chr_has_flag\": \"AI if_chr_has_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_stage_flag\": \"AI set_stage_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.unset_stage_flag\": \"AI unset_stage_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_stage_flag_eq\": \"AI if_stage_flag_eq action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_chrflag\": \"AI set_chrflag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.unset_chrflag\": \"AI unset_chrflag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_has_chrflag\": \"AI if_has_chrflag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_chrflag\": \"AI chr_set_chrflag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_unset_chrflag\": \"AI chr_unset_chrflag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_chr_has_chrflag\": \"AI if_chr_has_chrflag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_hidden_flag\": \"AI chr_set_hidden_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_unset_hidden_flag\": \"AI chr_unset_hidden_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_chr_has_hidden_flag\": \"AI if_chr_has_hidden_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_obj_flag\": \"AI set_obj_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.unset_obj_flag\": \"AI unset_obj_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_obj_has_flag\": \"AI if_obj_has_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.open_door\": \"AI open_door action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.close_door\": \"AI close_door action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_door_state\": \"AI if_door_state action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_object_is_door\": \"AI if_object_is_door action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.lock_door\": \"AI lock_door action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.unlock_door\": \"AI unlock_door action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_door_locked\": \"AI if_door_locked action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_lift_stationary\": \"AI if_lift_stationary action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.lift_go_to_stop\": \"AI lift_go_to_stop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_lift_at_stop\": \"AI if_lift_at_stop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.activate_lift\": \"AI activate_lift action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_using_lift\": \"AI if_using_lift action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.configure_rain\": \"AI configure_rain action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.configure_snow\": \"AI configure_snow action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.switch_to_alt_sky\": \"AI switch_to_alt_sky action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_wind_speed\": \"AI set_wind_speed action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_lights\": \"AI set_lights action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_room_flag\": \"AI set_room_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.show_cutscene_chrs\": \"AI show_cutscene_chrs action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.configure_environment\": \"AI configure_environment action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_distance_to_target2_less_than\": \"AI if_distance_to_target2_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_distance_to_target2_greater_than\": \"AI if_distance_to_target2_greater_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.speak\": \"AI speak action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.play_sound\": \"AI play_sound action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.assign_sound\": \"AI assign_sound action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.audio_mute_channel\": \"AI audio_mute_channel action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_channel_free\": \"AI if_channel_free condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_object_sound_volume\": \"AI set_object_sound_volume action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_object_sound_volume_by_distance\": \"AI set_object_sound_volume_by_distance action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_object_sound_playing\": \"AI set_object_sound_playing action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.play_repeating_sound_from_object\": \"AI play_repeating_sound_from_object action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.play_sound_from_entity\": \"AI play_sound_from_entity action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.play_repeating_sound_from_pad\": \"AI play_repeating_sound_from_pad action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_object_sound_volume_less_than\": \"AI if_object_sound_volume_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.play_sound_from_prop\": \"AI play_sound_from_prop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.play_temporary_primary_track\": \"AI play_temporary_primary_track action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.play_x_track\": \"AI play_x_track action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.stop_ambient_track\": \"AI stop_ambient_track action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_draw_weapon\": \"AI chr_draw_weapon action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_draw_weapon_in_cutscene\": \"AI chr_draw_weapon_in_cutscene action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_player_force_speed\": \"AI set_player_force_speed action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_invincible\": \"AI chr_set_invincible action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_player_is_invincible\": \"AI if_player_is_invincible condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_chr_has_no_gun\": \"AI if_chr_has_no_gun condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_delete_weapon\": \"AI chr_delete_weapon action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_trigger_shot_list\": \"AI if_trigger_shot_list condition\"") !=
	        std::string::npos);
	for (const char *node : {
		     "\"scenario.ai.action.end_level\": \"AI end_level action\"",
		     "\"scenario.ai.action.end_cutscene\": \"AI end_cutscene action\"",
		     "\"scenario.ai.action.warp_jo_to_pad\": \"AI warp_jo_to_pad action\"",
		     "\"scenario.ai.action.warp_jo_to_tag\": \"AI warp_jo_to_tag action\"",
		     "\"scenario.ai.action.revoke_control\": \"AI revoke_control action\"",
		     "\"scenario.ai.action.grant_control\": \"AI grant_control action\"",
		     "\"scenario.ai.action.player_fade_in\": \"AI player_fade_in action\"",
		     "\"scenario.ai.action.players_fade_out\": \"AI players_fade_out action\"",
		     "\"scenario.ai.condition.if_colour_fade_complete\": \"AI if_colour_fade_complete condition\"",
		     "\"scenario.ai.action.prepare_warp_orbit\": \"AI prepare_warp_orbit action\"",
		     "\"scenario.ai.action.begin_warp_latch\": \"AI begin_warp_latch action\"",
		     "\"scenario.ai.condition.if_warp_latch_complete\": \"AI if_warp_latch_complete condition\"",
		     "\"scenario.ai.action.set_camera_animation\": \"AI set_camera_animation action\"",
		     "\"scenario.ai.condition.if_in_cutscene\": \"AI if_in_cutscene condition\"",
		     "\"scenario.ai.condition.if_cutscene_button_pressed\": \"AI if_cutscene_button_pressed condition\"",
		     "\"scenario.ai.action.reorient_for_cutscene_stop\": \"AI reorient_for_cutscene_stop action\"",
		     "\"scenario.ai.action.duplicate_chr\": \"AI duplicate_chr action\"",
		     "\"scenario.ai.action.enable_chr\": \"AI enable_chr action\"",
		     "\"scenario.ai.action.disable_chr\": \"AI disable_chr action\"",
		     "\"scenario.ai.action.enable_obj\": \"AI enable_obj action\"",
		     "\"scenario.ai.action.disable_obj\": \"AI disable_obj action\"",
		     "\"scenario.ai.action.chr_move_to_pad\": \"AI chr_move_to_pad action\"",
		     "\"scenario.ai.action.chr_set_team\": \"AI chr_set_team action\"",
		     "\"scenario.ai.action.damage_chr_by_amount\": \"AI damage_chr_by_amount action\"",
		     "\"scenario.ai.action.do_preset_animation\": \"AI do_preset_animation action\"",
		     "\"scenario.ai.condition.if_player_chr_portal_distance_less_than\": \"AI if_player_chr_portal_distance_less_than condition\"",
		     "\"scenario.ai.condition.if_chr_reposition_valid\": \"AI if_chr_reposition_valid condition\"",
	     }) {
		REQUIRE(conformance.find(node) != std::string::npos);
	}
	REQUIRE(conformance.find("\"scenario.ai.action.do_gun_command\": \"AI do_gun_command action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_distance_to_gun_less_than\": \"AI if_distance_to_gun_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.recover_gun\": \"AI recover_gun action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_copy_properties\": \"AI chr_copy_properties action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.player_auto_walk\": \"AI player_auto_walk action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_player_auto_walk_finished\": \"AI if_player_auto_walk_finished condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_obj_in_room\": \"AI if_obj_in_room condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_player_looking_at_object\": \"AI if_player_looking_at_object condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_target_is_player\": \"AI if_target_is_player condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_kill\": \"AI chr_kill action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.remove_weapon_from_inventory\": \"AI remove_weapon_from_inventory action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.clear_inventory\": \"AI clear_inventory action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.release_object\": \"AI release_object action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_grab_object\": \"AI chr_grab_object action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.toggle_p1p2\": \"AI toggle_p1p2 action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_p1p2\": \"AI chr_set_p1p2 action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_cloaked\": \"AI chr_set_cloaked action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_autogun_target_team\": \"AI set_autogun_target_team action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_objective_complete\": \"AI if_objective_complete condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_objective_failed\": \"AI if_objective_failed condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_all_objectives_complete\": \"AI if_all_objectives_complete condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_difficulty_less_than\": \"AI if_difficulty_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_difficulty_greater_than\": \"AI if_difficulty_greater_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_stage_timer_less_than\": \"AI if_stage_timer_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_stage_timer_greater_than\": \"AI if_stage_timer_greater_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_stage_id_less_than\": \"AI if_stage_id_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_stage_id_greater_than\": \"AI if_stage_id_greater_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_num_players_less_than\": \"AI if_num_players_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_kill_count_greater_than\": \"AI if_kill_count_greater_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_num_knocked_out_chrs\": \"AI if_num_knocked_out_chrs condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.kill_bond\": \"AI kill_bond action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_num_arghs_less_than\": \"AI if_num_arghs_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_chr_health_greater_than\": \"AI if_chr_health_greater_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_chr_shield_less_than\": \"AI if_chr_shield_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_injured\": \"AI if_injured condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_morale_less_than\": \"AI if_morale_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_alertness\": \"AI if_alertness condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_idle\": \"AI if_idle condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_chr_dead\": \"AI if_chr_dead condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_can_see_target\": \"AI if_can_see_target condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_pouncebits_eq\": \"AI if_pouncebits_eq condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_training_pc_holographed\": \"AI if_training_pc_holographed condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_player_using_device\": \"AI if_player_using_device condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_begin_or_end_teleport\": \"AI chr_begin_or_end_teleport action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_chr_teleport_full_white\": \"AI if_chr_teleport_full_white condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_cutscene_weapon\": \"AI chr_set_cutscene_weapon action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.fade_screen\": \"AI fade_screen action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_fade_complete\": \"AI if_fade_complete condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_chr_hudpiece_visible\": \"AI set_chr_hudpiece_visible action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_passive_mode\": \"AI set_passive_mode action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_firing_in_cutscene\": \"AI chr_set_firing_in_cutscene action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_portal_flag\": \"AI set_portal_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_music_event_queue_is_empty\": \"AI if_music_event_queue_is_empty condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_coop_mode\": \"AI if_coop_mode condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than\": \"AI if_chr_same_floor_distance_to_pad_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.remove_references_to_chr\": \"AI remove_references_to_chr action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_toggle_model_part\": \"AI chr_toggle_model_part action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.obj_set_model_part_visible\": \"AI obj_set_model_part_visible action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_obj_health_less_than\": \"AI if_obj_health_less_than action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_obj_health\": \"AI set_obj_health action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_chr_special_death_animation\": \"AI set_chr_special_death_animation action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_room_to_search\": \"AI set_room_to_search action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_savefile_flag\": \"AI set_savefile_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.unset_savefile_flag\": \"AI unset_savefile_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_savefile_flag_set\": \"AI if_savefile_flag_set action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_savefile_flag_unset\": \"AI if_savefile_flag_unset action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.restart_timer\": \"AI restart_timer action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_timer_greater_than_random\": \"AI if_timer_greater_than_random action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_countdown_timer\": \"AI set_countdown_timer action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_countdown_timer_greater_than\": \"AI if_countdown_timer_greater_than action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.show_hudmsg\": \"AI show_hudmsg action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.show_hudmsg_middle\": \"AI show_hudmsg_middle action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.show_hudmsg_top_middle\": \"AI show_hudmsg_top_middle action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.hovercar_begin_path\": \"AI hovercar_begin_path action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_vehicle_speed\": \"AI set_vehicle_speed action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_rotor_speed\": \"AI set_rotor_speed action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_explosions\": \"AI chr_explosions action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_object_distance_to_pad_less_than\": \"AI if_object_distance_to_pad_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_action\": \"AI set_action action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_team_orders\": \"AI set_team_orders action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.retreat\": \"AI retreat action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.find_cover\": \"AI find_cover action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.find_cover_within_dist\": \"AI find_cover_within_dist action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.find_cover_outside_dist\": \"AI find_cover_outside_dist action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.go_to_cover\": \"AI go_to_cover action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.check_cover_out_of_sight\": \"AI check_cover_out_of_sight action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.orbit_target\": \"AI orbit_target action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_chr_preset_to_unalerted_teammate\": \"AI set_chr_preset_to_unalerted_teammate action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_squadron\": \"AI set_squadron action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.face_cover\": \"AI face_cover action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.danger_cover\": \"AI danger_cover action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.release_cover\": \"AI release_cover action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.rebuild_teams\": \"AI rebuild_teams action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.rebuild_squadrons\": \"AI rebuild_squadrons action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_listening\": \"AI chr_set_listening action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_attack_amount\": \"AI try_attack_amount action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_pad_preset\": \"AI set_pad_preset action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_pad_preset\": \"AI chr_set_pad_preset action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_copy_pad_preset\": \"AI chr_copy_pad_preset action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_can_hear_alarm\": \"AI if_can_hear_alarm condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_patrolling\": \"AI if_patrolling condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_alarm_active\": \"AI if_alarm_active condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_gas_active\": \"AI if_gas_active condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_hears_target\": \"AI if_hears_target condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_saw_injury\": \"AI if_saw_injury condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_saw_death\": \"AI if_saw_death condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_los_to_target\": \"AI if_los_to_target condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_los_to_attack_target\": \"AI if_los_to_attack_target condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_target_nearly_in_sight\": \"AI if_target_nearly_in_sight condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_nearly_in_targets_sight\": \"AI if_nearly_in_targets_sight condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_pad_preset_to_pad_on_route_to_target\": \"AI set_pad_preset_to_pad_on_route_to_target action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_saw_target_recently\": \"AI if_saw_target_recently condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_heard_target_recently\": \"AI if_heard_target_recently condition\"") !=
	        std::string::npos);
	for (const char *entry : {
		     "\"scenario.ai.condition.if_los_to_chr\": \"AI if_los_to_chr condition\"",
		     "\"scenario.ai.condition.if_never_been_on_screen\": \"AI if_never_been_on_screen condition\"",
		     "\"scenario.ai.condition.if_on_screen\": \"AI if_on_screen condition\"",
		     "\"scenario.ai.condition.if_chr_in_on_screen_room\": \"AI if_chr_in_on_screen_room condition\"",
		     "\"scenario.ai.condition.if_room_is_on_screen\": \"AI if_room_is_on_screen condition\"",
		     "\"scenario.ai.condition.if_target_aiming_at_me\": \"AI if_target_aiming_at_me condition\"",
		     "\"scenario.ai.condition.if_near_miss\": \"AI if_near_miss condition\"",
		     "\"scenario.ai.condition.if_sees_suspicious_item\": \"AI if_sees_suspicious_item condition\"",
		     "\"scenario.ai.condition.if_target_in_fov_left\": \"AI if_target_in_fov_left condition\"",
		     "\"scenario.ai.condition.if_check_fov_with_target\": \"AI if_check_fov_with_target condition\"",
		     "\"scenario.ai.condition.if_target_out_of_fov_left\": \"AI if_target_out_of_fov_left condition\"",
		     "\"scenario.ai.condition.if_target_in_fov\": \"AI if_target_in_fov condition\"",
		     "\"scenario.ai.condition.if_target_out_of_fov\": \"AI if_target_out_of_fov condition\"",
		     "\"scenario.ai.condition.if_distance_to_target_less_than\": \"AI if_distance_to_target_less_than condition\"",
		     "\"scenario.ai.condition.if_distance_to_target_greater_than\": \"AI if_distance_to_target_greater_than condition\"",
		     "\"scenario.ai.condition.if_chr_distance_to_pad_less_than\": \"AI if_chr_distance_to_pad_less_than condition\"",
		     "\"scenario.ai.condition.if_chr_distance_to_pad_greater_than\": \"AI if_chr_distance_to_pad_greater_than condition\"",
		     "\"scenario.ai.condition.if_distance_to_chr_less_than\": \"AI if_distance_to_chr_less_than condition\"",
		     "\"scenario.ai.condition.if_distance_to_chr_greater_than\": \"AI if_distance_to_chr_greater_than condition\"",
		     "\"scenario.ai.condition.if_any_chr_near_self\": \"AI if_any_chr_near_self condition\"",
		     "\"scenario.ai.condition.if_distance_from_target_to_pad_less_than\": \"AI if_distance_from_target_to_pad_less_than condition\"",
		     "\"scenario.ai.condition.if_distance_from_target_to_pad_greater_than\": \"AI if_distance_from_target_to_pad_greater_than condition\"",
		     "\"scenario.ai.condition.if_chr_in_room\": \"AI if_chr_in_room condition\"",
		     "\"scenario.ai.condition.if_target_in_room\": \"AI if_target_in_room condition\"",
		     "\"scenario.ai.condition.if_chr_has_object\": \"AI if_chr_has_object condition\"",
		     "\"scenario.ai.condition.if_weapon_thrown\": \"AI if_weapon_thrown condition\"",
		     "\"scenario.ai.condition.if_weapon_thrown_on_object\": \"AI if_weapon_thrown_on_object condition\"",
		     "\"scenario.ai.condition.if_chr_has_weapon_equipped\": \"AI if_chr_has_weapon_equipped condition\"",
		     "\"scenario.ai.condition.if_gun_unclaimed\": \"AI if_gun_unclaimed condition\"",
		     "\"scenario.ai.condition.if_object_healthy\": \"AI if_object_healthy condition\"",
		     "\"scenario.ai.condition.if_chr_activated_object\": \"AI if_chr_activated_object condition\"",
		     "\"scenario.ai.action.obj_interact\": \"AI obj_interact action\"",
		     "\"scenario.ai.action.destroy_object\": \"AI destroy_object action\"",
		     "\"scenario.ai.action.drop_object_from_chr\": \"AI drop_object_from_chr action\"",
		     "\"scenario.ai.action.chr_drop_items\": \"AI chr_drop_items action\"",
		     "\"scenario.ai.action.chr_drop_weapon\": \"AI chr_drop_weapon action\"",
		     "\"scenario.ai.action.give_object_to_chr\": \"AI give_object_to_chr action\"",
		     "\"scenario.ai.action.object_move_to_pad\": \"AI object_move_to_pad action\"",
		     "\"scenario.ai.condition.if_chr_not_talking\": \"AI if_chr_not_talking condition\"",
		     "\"scenario.ai.condition.if_orders\": \"AI if_orders condition\"",
		     "\"scenario.ai.condition.if_has_orders\": \"AI if_has_orders condition\"",
		     "\"scenario.ai.condition.if_chr_in_squadron_doing_action\": \"AI if_chr_in_squadron_doing_action condition\"",
		     "\"scenario.ai.condition.if_chr_listening\": \"AI if_chr_listening condition\"",
		     "\"scenario.ai.condition.if_not_listening\": \"AI if_not_listening condition\"",
		     "\"scenario.ai.condition.if_chr_injured_target\": \"AI if_chr_injured_target condition\"",
		     "\"scenario.ai.condition.if_action\": \"AI if_action condition\"",
		     "\"scenario.ai.condition.if_chr_ammo_quantity_less_than\": \"AI if_chr_ammo_quantity_less_than condition\"",
		     "\"scenario.ai.condition.if_chr_target\": \"AI if_chr_target condition\"",
		     "\"scenario.ai.condition.if_compare_chr_presets_team\": \"AI if_compare_chr_presets_team condition\"",
		     "\"scenario.ai.condition.if_human\": \"AI if_human condition\"",
		     "\"scenario.ai.condition.if_skedar\": \"AI if_skedar condition\"",
		     "\"scenario.ai.condition.if_prop_preset_blocking_sight_to_target\": \"AI if_prop_preset_blocking_sight_to_target condition\"",
		     "\"scenario.ai.action.remove_object_at_prop_preset\": \"AI remove_object_at_prop_preset action\"",
		     "\"scenario.ai.condition.if_prop_preset_height_less_than\": \"AI if_prop_preset_height_less_than condition\"",
		     "\"scenario.ai.action.set_target\": \"AI set_target action\"",
		     "\"scenario.ai.condition.if_presets_target_is_not_my_target\": \"AI if_presets_target_is_not_my_target condition\"",
		     "\"scenario.ai.action.set_chr_preset_to_chr_near_self\": \"AI set_chr_preset_to_chr_near_self action\"",
		     "\"scenario.ai.action.set_chr_preset_to_chr_near_pad\": \"AI set_chr_preset_to_chr_near_pad action\"",
		     "\"scenario.ai.condition.if_dangerous_object_nearby\": \"AI if_dangerous_object_nearby condition\"",
		     "\"scenario.ai.condition.if_heli_weapons_armed\": \"AI if_heli_weapons_armed condition\"",
		     "\"scenario.ai.condition.if_hoverbot_next_step\": \"AI if_hoverbot_next_step condition\"",
		     "\"scenario.ai.action.shuffle_investigation_terminals\": \"AI shuffle_investigation_terminals action\"",
		     "\"scenario.ai.action.set_pad_preset_to_investigation_terminal\": \"AI set_pad_preset_to_investigation_terminal action\"",
		     "\"scenario.ai.action.heli_arm_weapons\": \"AI heli_arm_weapons action\"",
		     "\"scenario.ai.action.heli_unarm_weapons\": \"AI heli_unarm_weapons action\"",
		     "\"scenario.ai.condition.if_squadron_is_dead\": \"AI if_squadron_is_dead condition\"",
		     "\"scenario.ai.condition.if_true\": \"AI if_true condition\"",
		     "\"scenario.ai.condition.if_num_chrs_in_squadron_greater_than\": \"AI if_num_chrs_in_squadron_greater_than condition\"",
		     "\"scenario.ai.condition.if_natural_anim\": \"AI if_natural_anim condition\"",
		     "\"scenario.ai.condition.if_y\": \"AI if_y condition\"",
		     "\"scenario.ai.condition.if_sound_timer\": \"AI if_sound_timer condition\"",
		     "\"scenario.ai.condition.if_target_y_difference_less_than\": \"AI if_target_y_difference_less_than condition\"",
		     "\"scenario.ai.action.set_tinted_glass_enabled\": \"AI set_tinted_glass_enabled action\"",
		     "\"scenario.ai.action.hovercopter_fire_rocket\": \"AI hovercopter_fire_rocket action\"",
		     "\"scenario.ai.action.chr_adjust_motion_blur\": \"AI chr_adjust_motion_blur action\"",
		     "\"scenario.ai.action.punch_or_kick\": \"AI punch_or_kick action\"",
		     "\"scenario.ai.action.set_target_to_eyespy_if_in_sight\": \"AI set_target_to_eyespy_if_in_sight action\"",
		     "\"scenario.ai.action.mini_skedar_try_pounce\": \"AI mini_skedar_try_pounce action\"",
		     "\"scenario.ai.action.avoid\": \"AI avoid action\"",
		     "\"scenario.ai.action.title_init_mode\": \"AI title_init_mode action\"",
		     "\"scenario.ai.action.try_exit_title\": \"AI try_exit_title action\"",
		     "\"scenario.ai.action.chr_emit_sparks\": \"AI chr_emit_sparks action\"",
		     "\"scenario.ai.action.set_dr_caroll_images\": \"AI set_dr_caroll_images action\"",
		     "\"scenario.ai.action.say_quip\": \"AI say_quip action\"",
		     "\"scenario.ai.action.say_ci_staff_quip\": \"AI say_ci_staff_quip action\"",
		     "\"scenario.ai.action.shuffle_ruins_pillars\": \"AI shuffle_ruins_pillars action\"",
		     "\"scenario.ai.action.shuffle_pelagic_switches\": \"AI shuffle_pelagic_switches action\"",
		     "\"scenario.ai.condition.if_waypoint_within_quadrant\": \"AI if_waypoint_within_quadrant condition\"",
		     "\"scenario.ai.action.set_pad_preset_to_target_quadrant\": \"AI set_pad_preset_to_target_quadrant action\"",
	     }) {
		REQUIRE(conformance.find(entry) != std::string::npos);
	}
	REQUIRE(conformance.find("\"scenario.ai.action.set_chr_preset\": \"AI set_chr_preset action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_chr_target\": \"AI set_chr_target action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("level.graph.json must link {pair[0]} to {pair[1]}") !=
	        std::string::npos);
	REQUIRE(conformance.find("level.graph.json must bind paths table to navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2.scenario.ai.lists.v1") !=
	        std::string::npos);
	REQUIRE(conformance.find("must declare schema pd2.scenario.paths.v1") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2.scenario.spawns.v1") !=
	        std::string::npos);
	REQUIRE(conformance.find("mission.objective.criteria.source") !=
	        std::string::npos);
	REQUIRE(conformance.find("objectives.json still points at original_perfect_dark_setup") !=
	        std::string::npos);
	REQUIRE(conformance.find("SCENARIO_OBJECTIVES_HEADER") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"target_record_ref\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("objective_step.argument") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupParseFieldsJson") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("setup.fields.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario_source_setup_link_t") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupCollectBehaviorLinkSource") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("objects.json") != std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: compiled setup.fields.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"autogun.y_zero\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"autogun.y_max_left\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"autogun.max_speed\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("spawns.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("cmd[0] = INTROCMD_SPAWN") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("setup->ailists = (struct ailist *)(uintptr_t)ailists_offset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("setup->paths = path_count ?") !=
	        std::string::npos);
	REQUIRE(prop_runtime.find("count < (s32)ARRAYCOUNT(prop->rooms)") !=
	        std::string::npos);
	REQUIRE(prop_runtime.find("PROP.ROOMS: register room list missing terminator") !=
	        std::string::npos);
	REQUIRE(prop_runtime.find("PROP.ROOMS: deregister invalid room") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("BG.ROOMS: skipping invalid entered room") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("BG.ROOMS: skipping invalid portal") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("for (len = 0; len <= maxlen && rooms[len] != -1; len++);") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("if (len > maxlen)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("unsupported setup source field") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("catalogResolveBody(catalog_id") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("catalogResolveHead(catalog_id") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("head_selector") != std::string::npos);
	REQUIRE(scenario_runtime.find("\"embedded\"") != std::string::npos);
	REQUIRE(scenario_runtime.find("weapon_selector") != std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupRecordRefOffset") != std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupRecordOrderFromId(record_id, default_order)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupRecordOrderFromId(record_id, order)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("offsetof(struct tag, cmdoffset)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("offsetof(struct textoverride, objoffset)") !=
	        std::string::npos);
	const std::string setup_create_props = functionBlock(setup, "setupCreateProps");
	REQUIRE(!setup_create_props.empty());
	REQUIRE(setup_create_props.find("scenarioSourceSetupGraphRecordBehaviorLink(") !=
	        std::string::npos);
	REQUIRE(setup_create_props.find("OBJTYPE_LINKGUNS, index") != std::string::npos);
	REQUIRE(setup_create_props.find("OBJTYPE_LINKLIFTDOOR, index") !=
	        std::string::npos);
	REQUIRE(setup_create_props.find("OBJTYPE_SAFEITEM, index") != std::string::npos);
	REQUIRE(setup_create_props.find("OBJTYPE_PADLOCKEDDOOR, index") !=
	        std::string::npos);
	REQUIRE(setup_create_props.find("OBJTYPE_CONDITIONALSCENERY, index") !=
	        std::string::npos);
	REQUIRE(setup_create_props.find("OBJTYPE_BLOCKEDPATH, index") !=
	        std::string::npos);
	REQUIRE(countOccurrences(setup_create_props,
			"if (!scenarioSourceSetupGraphRecordBehaviorLink(") >= 6);
	requireTokenOrder(setup_create_props, "OBJTYPE_LINKGUNS, index",
		"propweaponSetDual");
	requireTokenOrder(setup_create_props, "OBJTYPE_LINKLIFTDOOR, index",
		"setupCreateLiftDoor(link)");
	requireTokenOrder(setup_create_props, "OBJTYPE_SAFEITEM, index",
		"setupCreateSafeItem(link)");
	requireTokenOrder(setup_create_props, "OBJTYPE_PADLOCKEDDOOR, index",
		"setupCreatePadlockedDoor(link)");
	requireTokenOrder(setup_create_props, "OBJTYPE_CONDITIONALSCENERY, index",
		"setupCreateConditionalScenery(link)");
	requireTokenOrder(setup_create_props, "OBJTYPE_BLOCKEDPATH, index",
		"setupCreateBlockedPath(blockedpath)");
	REQUIRE(catalog_api.find("Extractors run before catalogBuildRuntimeCaches()") !=
	        std::string::npos);
	REQUIRE(catalog_api.find("assetCatalogGetByIndex(i)") !=
	        std::string::npos);
	REQUIRE(mesh_extractor.find("for (s32 i = 0; i < NUM_MODELS; i++)") !=
	        std::string::npos);
	REQUIRE(mesh_extractor.find("catalogModelIdByModelnum(modelnum)") !=
	        std::string::npos);
	REQUIRE(mesh_extractor.find("catalogReadableModelIdForModelnum(modelnum") !=
	        std::string::npos);
	const std::string mesh_walker =
		readTextFile("port/src/loader_walker_mesh.c");
	REQUIRE(mesh_walker.find("\"mesh\", \"meshes\", \".pdmesh\", /* always_invoke: */ 1") !=
	        std::string::npos);
	REQUIRE(mesh_walker.find("preserved_runtime_index") !=
	        std::string::npos);
	REQUIRE(mesh_walker.find("e->runtime_index = preserved_runtime_index") !=
	        std::string::npos);
	REQUIRE(mesh_walker.find("catalogSetPrimaryFile(e, source_path)") !=
	        std::string::npos);
	REQUIRE(mesh_extractor.find("pdmesh_model_obj_mtx_v23_materials_hierarchy_parts_faces_json_relations_raw_mtx_render_commands_json_allmodels_menuhud_zero_tri_models") !=
	        std::string::npos);
	REQUIRE(mesh_extractor.find("catalogReadableModelIdForFile((s32)FILE_GHUDPIECE, \"menu\", \"menu\"") !=
	        std::string::npos);
	REQUIRE(mesh_extractor.find("(u16)FILE_GHUDPIECE, \"menu\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_setupFieldHeadRef") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_setupFieldWeaponRef") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("\"rename_object.weapon\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("g_WeaponDataCatalogIds[weaponnum]") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceLoadTilesForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceLoadTilesForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("catalogGetLoadedColmesh(scenario->id)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: compiled scene tiles") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("assetArchiveWriterAddPublicMem(&asset_writer, \"collision.obj\"") !=
	        std::string::npos);
	// B-943: the per-triangle GEOFLAG/floortype/floorcol sidecar is emitted
	// alongside collision.obj, schema-versioned, and required for an archive to
	// be considered clean (forces re-extraction of pre-B-943 archives).
	REQUIRE(scenario_extractor.find("assetArchiveWriterAddPublicMem(&asset_writer, \"collision.flags.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("pd2.scenario.collision.flags.v1") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_existingArchiveHasEntry(relpath, \"collision.flags.json\")") !=
	        std::string::npos);
	// Runtime must consume the sidecar as the authoritative flag source and
	// carry floortype/floorcol into the tile cache (not hardcode 0).
	REQUIRE(scenario_runtime.find("tile->floortype = src->floortype;") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("tile->floorcol = src->floorcol;") !=
	        std::string::npos);
	// B-943 (gap #1 part-2): the BG per-room LIGHT table is emitted as a
	// schema-versioned room_lights.json sidecar, required for a clean archive
	// (forces re-extraction of pre-lights archives), and rebuilt at runtime into
	// g_BgLightsFileData so the shoot-out-lights / dimming mechanic survives the
	// scenario-source path.
	REQUIRE(scenario_extractor.find("assetArchiveWriterAddPublicMem(&asset_writer, \"room_lights.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("pd2.scenario.room.lights.v1") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_existingArchiveHasEntry(relpath, \"room_lights.json\")") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_buildBgRoomLightsJson") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceLoadRoomLightsForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceLoadRoomLightsForStage") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("scenarioSourceLoadRoomLightsForStage") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("g_BgLightsFileData = lightdata;") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("collision_source = collision.obj") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("\\\"collision_source\\\": \\\"collision.obj\\\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("\\\"collision\\\": \\\"collision.obj\\\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_existingArchiveHasEntry(relpath, \"collision.obj\")") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_collectStandaloneStageScenario") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("assetCatalogIterateByType(ASSET_MAP, s_collectStandaloneStageScenario") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("base:scenario_%s") != std::string::npos);
	REQUIRE(scenario_extractor.find("strcmp(slug, \"citraining\")") ==
	        std::string::npos);
	REQUIRE(scenario_extractor.find(":citraining") != std::string::npos);
	REQUIRE(scenario_extractor.find("return \"firingrange\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("fast-cache blocked by missing/stale standalone stage archive") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceFindEntryForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceFindEntryForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("category_exact") != std::string::npos);
	REQUIRE(scenario_runtime.find("s_findScenarioByDerivedStageId") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("assetCatalogResolve(scenario_id)") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("strcmp(kind, \"firingrange\")") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("_PD_ROOM") != std::string::npos);
	REQUIRE(scenario_extractor.find("roomnum") != std::string::npos);
	REQUIRE(modasset_compiler.find("gltfApplyRoomTags") != std::string::npos);
	REQUIRE(modasset_compiler.find("_PD_ROOM") != std::string::npos);
	REQUIRE(modasset_compiler.find("parseObjRoomName") != std::string::npos);
	REQUIRE(modasset_compiler.find("current_room = parseObjRoomName(p)") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("objMeshAddTriangleWithTexcoords(mesh,") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("indices[0], indices[i - 1], indices[i],") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("tri->roomnum") != std::string::npos);
	REQUIRE(modasset_compiler.find("added->roomnum = (RoomNum)tri->roomnum") !=
	        std::string::npos);

	REQUIRE(tiles.find("asset_source_debug.h") != std::string::npos);
	REQUIRE(tiles.find("scenarioSourceLoadTilesForStage(&stage") !=
	        std::string::npos);
	REQUIRE(tiles.find("TILES: using scenario source scene-derived tile cache") !=
	        std::string::npos);
	REQUIRE(tiles.find("assetSourceDebugFatalHandleFallback(ASSET_SCENARIO, \"tiles\"") !=
	        std::string::npos);
	REQUIRE(tiles.find("assetLoadToNew(stage.tile_handle") ==
	        std::string::npos);
	REQUIRE(tiles.find("tiles source -> ROM handle") ==
	        std::string::npos);
	REQUIRE(tiles.find("scenarioSourceFatalRuntimeFallbackForStage(&stage") !=
	        std::string::npos);
	REQUIRE(tiles.find("tile source compile failed") !=
	        std::string::npos);

	REQUIRE(lv.find("scenario_source_runtime.h") != std::string::npos);
	REQUIRE(lv.find("lvAddScenarioSourceColmesh") != std::string::npos);
	REQUIRE(lv.find("scenarioSourceFindEntryForStage(&stage") !=
	        std::string::npos);
	REQUIRE(lv.find("catalogLoadTypedAsset(ASSET_SCENARIO") !=
	        std::string::npos);
	REQUIRE(lv.find("catalogGetLoadedColmesh(scenario->id)") !=
	        std::string::npos);
	REQUIRE(lv.find("SCENARIO.SOURCE: added scene colmesh") !=
	        std::string::npos);
	REQUIRE(lv.find("MESHCOL: ENABLED -- source=") != std::string::npos);
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

	REQUIRE(extractor.find("PDWEAPON_FAST_CACHE_KIND \"pdweapon_embedded_v14_clean_public_b943fields_projref\"") !=
		std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_DEPENDENCY_CLOSURE_MARKER \"embedded.v15\"") !=
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
	const std::string runner = readTextFile("tools/smoke-verify/run.ps1");
	REQUIRE(harness.find("Explicit -SourceBinary means \"run this exact build\"") !=
	        std::string::npos);
	REQUIRE(harness.find("if ($ExplicitPath) {") != std::string::npos);
	REQUIRE(harness.find("return $null") != std::string::npos);
	REQUIRE(harness.find("if (-not $SourceBinary -and (Test-Path -LiteralPath $destBin))") !=
	        std::string::npos);
	REQUIRE(harness.find("Copy-Item -LiteralPath $bin -Destination $destBin -Force") !=
	        std::string::npos);
	REQUIRE(harness.find("$srcInfo.LastWriteTimeUtc -le $dstInfo.LastWriteTimeUtc") !=
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
	REQUIRE(runtime.find("s_parsePortalsJson(text, &row_count)") !=
	        std::string::npos);
	REQUIRE(runtime.find("!rows || row_count <= 0") == std::string::npos);
	REQUIRE(runtime.find("SCENARIO.SOURCE: compiled pads.json") !=
	        std::string::npos);
	REQUIRE(runtime.find("SCENARIO.SOURCE: compiled portals.json") !=
	        std::string::npos);
	REQUIRE(bg.find("portals ? portals :") != std::string::npos);
	REQUIRE(bg.find("portalcount > 0 ? portalcount : 0") !=
	        std::string::npos);
	REQUIRE(bg.find("SCENARIO.SOURCE: built native portal tables") !=
	        std::string::npos);
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

TEST_CASE("Settings Debug can force one asset family to public file source",
          "[modding][pdxxx][c3842][debug][source_gate][static]") {
	const std::string header = readTextFile("port/include/asset_source_debug.h");
	const std::string debug = readTextFile("port/src/asset_source_debug.c");
	const std::string load = readTextFile("port/src/assetcatalog_load.c");
	const std::string load_h = readTextFile("port/include/assetcatalog_load.h");
	const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
	const std::string romdata = readTextFile("port/src/romdata.c");
	const std::string mod = readTextFile("port/src/mod.c");
	const std::string snd = readTextFile("src/lib/snd.c");
	const std::string anim_c = readTextFile("src/lib/anim.c");
	const std::string propsnd = readTextFile("src/game/propsnd.c");
	const std::string main_c = readTextFile("port/src/main.c");
	const std::string texreset_c = readTextFile("src/game/texreset.c");
	const std::string pdgui_theme = readTextFile("port/fast3d/pdgui_theme.cpp");
	const std::string loader_ui = readTextFile("port/src/loader_walker_ui.c");
	const std::string smoke_run = readTextFile("tools/smoke-verify/run.ps1");
	const std::string all_family_smoke =
		readTextFile("tools/smoke-verify/tests/all_family_source_gate_smoke.json");
	const std::string audio_live_smoke =
		readTextFile("tools/smoke-verify/tests/audio_live_playback_source_smoke.json");
	const std::string all_family_matrix =
		readTextFile("tools/smoke-verify/run-all-family-source-matrix.ps1");

	REQUIRE(header.find("assetSourceDebugOnlyType") != std::string::npos);
	REQUIRE(header.find("assetSourceDebugEntryRequiresPublicFileSource") !=
	        std::string::npos);
	REQUIRE(debug.find("Debug.AssetSourceOnlyType") != std::string::npos);
	REQUIRE(debug.find("configRegisterInt(\"Debug.AssetSourceOnlyType\"") !=
	        std::string::npos);
	REQUIRE(debug.find("assetSourceDebugHandleUsesPublicFileSource(entry->source.primary)") !=
	        std::string::npos);
	REQUIRE(debug.find("assetSourceDebugHandleUsesPublicFileSource(entry->source.override)") !=
	        std::string::npos);

	REQUIRE(load_h.find("source_only_blocked") != std::string::npos);
	REQUIRE(load_h.find("catalogResolveMusicSequence") != std::string::npos);
	REQUIRE(load.find("assetSourceDebugEntryRequiresPublicFileSource(e)") !=
	        std::string::npos);
	REQUIRE(load.find("r->source_only_blocked = 1") != std::string::npos);
	REQUIRE(load.find("assetSourceDebugEntryRequiresPublicFileSource(entry)") !=
	        std::string::npos);
	REQUIRE(load.find("CatalogResolveResult catalogResolveMusicSequence") !=
	        std::string::npos);
	REQUIRE(load.find("modSequenceVirtualTrackId(tracknum)") !=
	        std::string::npos);
	REQUIRE(load.find("strcmp(e->id, virtual_id)") != std::string::npos);
	REQUIRE(load.find("e->ext.audio.category != AUDIO_CAT_MUSIC") !=
	        std::string::npos);
	REQUIRE(load.find("e->ext.audio.sound_id != tracknum") !=
	        std::string::npos);
	REQUIRE(load.find("r.path = entryGetFilePath(entry)") != std::string::npos);
	REQUIRE(load.find("r.is_mod_override = 1") != std::string::npos);
	REQUIRE(load.find("s_catalogApplySourceOnlyDebug(&r, entry)") !=
	        std::string::npos);
	REQUIRE(load.find("refusing fallback") != std::string::npos);

	REQUIRE(romdata.find("r.source_only_blocked") != std::string::npos);
	REQUIRE(romdata.find("ASSET.SOURCE_ONLY: file") != std::string::npos);
	REQUIRE(romdata.find("ROM/static fallback.") != std::string::npos);
	const std::string mod_sequence_load = functionBlock(mod, "void *modSequenceLoad");
	const std::string mplayer = readTextFile("src/game/mplayer/mplayer.c");
	const std::string audio = readTextFile("port/src/audio.c");
	REQUIRE(mod.find("r.source_only_blocked") != std::string::npos);
	REQUIRE(mod.find("catalogResolveMusicSequence((s32)num)") !=
	        std::string::npos);
	{
		const std::string anim_load_data =
			functionBlock(mod, "void *modAnimationLoadData");
		const std::string anim_try_override =
			functionBlock(mod, "void *modAnimationTryCatalogOverride");
		const std::string anim_load_frame =
			functionBlock(anim_c, "u8 animLoadFrame");
		const std::string anim_load_header =
			functionBlock(anim_c, "void animLoadHeader");
		REQUIRE(mod.find("modAnimationFatalPublicSourceFailure") !=
		        std::string::npos);
		REQUIRE(mod.find("ASSET.SOURCE_ONLY: animation %d maps to public animation source") !=
		        std::string::npos);
		REQUIRE(mod.find("refusing ROM/static fallback") !=
		        std::string::npos);
		REQUIRE(anim_load_data.find("catalogResolveAnim((s32)num)") !=
		        std::string::npos);
		REQUIRE(anim_load_data.find("r.source_only_blocked") !=
		        std::string::npos);
		REQUIRE(anim_load_data.find("assetSourceDebugIsEnabledFor(ASSET_ANIMATION)") ==
		        std::string::npos);
		REQUIRE(anim_load_data.find("runtime clip compilation failed") !=
		        std::string::npos);
		REQUIRE(anim_load_data.find("the selected public source is not editable GLTF/GLB animation source") !=
		        std::string::npos);
		REQUIRE(anim_load_data.find("fsFileLoad(r.path") ==
		        std::string::npos);
		REQUIRE(anim_try_override.find("catalogResolveAnim((s32)num)") !=
		        std::string::npos);
		REQUIRE(anim_try_override.find("catalogGetAnimOverride") ==
		        std::string::npos);
		REQUIRE(anim_try_override.find("r.source_only_blocked") !=
		        std::string::npos);
		REQUIRE(anim_try_override.find("assetSourceDebugIsEnabledFor(ASSET_ANIMATION)") ==
		        std::string::npos);
		REQUIRE(anim_try_override.find("runtime clip compilation failed") !=
		        std::string::npos);
		REQUIRE(anim_try_override.find("the selected public source is not editable GLTF/GLB animation source") !=
		        std::string::npos);
		REQUIRE(anim_try_override.find("fsFileLoad(path") ==
		        std::string::npos);
		REQUIRE(anim_load_frame.find("modAnimationTryCatalogOverride(animnum)") <
		        anim_load_frame.find("animDma(&g_AnimFrameByteSlots"));
		REQUIRE(anim_load_header.find("modAnimationTryCatalogOverride(animnum)") <
		        anim_load_header.find("animDma(&g_AnimHeaderByteSlots"));
	}
	REQUIRE(mod.find("#define MOD_SEQUENCE_VIRTUAL_BASE 0x4000") !=
	        std::string::npos);
	REQUIRE(mod.find("s_ModSequenceVirtualIds[MOD_SEQUENCE_VIRTUAL_SLOTS]") !=
	        std::string::npos);
	REQUIRE(mod.find("modSequenceVirtualTrackForCatalogId") !=
	        std::string::npos);
	REQUIRE(mod.find("entry->ext.audio.category != AUDIO_CAT_MUSIC") !=
	        std::string::npos);
	REQUIRE(mod.find("entry->ext.audio.file_path[0]") !=
	        std::string::npos);
	REQUIRE(mod.find("private sequence slot") != std::string::npos);
	REQUIRE(mplayer.find("modSequenceVirtualTrackForCatalogId(modId)") !=
	        std::string::npos);
	REQUIRE(mplayer.find("return virtual_track") != std::string::npos);
	REQUIRE(audio.find("modSequenceVirtualTrackForCatalogId(track_id)") !=
	        std::string::npos);
	REQUIRE(audio.find("musicStartTemporaryPrimary(virtual_track)") !=
	        std::string::npos);
	REQUIRE(audio.find("sequence-backed sync is a start-only handoff") !=
	        std::string::npos);
	REQUIRE(audio.find("sequence track-change") != std::string::npos);
	REQUIRE(mod.find("modSequencePlayAudioSource(u16 num)") !=
	        std::string::npos);
	REQUIRE(main_c.find("modSequenceLoad((u16)audio->sound_id, &compiled_size)") !=
	        std::string::npos);
	REQUIRE(main_c.find("sysMemFree(compiled)") != std::string::npos);
	REQUIRE(main_c.find("modSequenceLoad((u16)audio->sound_id, &compiled_size)") <
	        main_c.find("BOOT: --debug-play-catalog-audio result kind=%s id='%s' result=OK track=%d state=registered"));
	REQUIRE(mod.find("modSequencePathHasAudioExtension(r.path)") !=
	        std::string::npos);
	REQUIRE(mod.find("modMusicPlay(r.path)") != std::string::npos);
	REQUIRE(mod.find("modMusicIsPlaying()") != std::string::npos);
	REQUIRE(mod.find("modSequenceCompilePublicSource") != std::string::npos);
	REQUIRE(mod.find("modSequenceSiblingPath(r->path, \"sequence.mid\"") !=
	        std::string::npos);
	REQUIRE(mod.find("modSequenceSiblingPath(r->path, \"sequence.json\"") !=
	        std::string::npos);
	REQUIRE(mod.find("fsFileSize(mid_path) <= 0") != std::string::npos);
	REQUIRE(mod.find("modSequenceLoadEventsJson") != std::string::npos);
	REQUIRE(mod.find("modSequenceBuildAlcBuffer") != std::string::npos);
	REQUIRE(mod.find("modSequencePutBe32(data + 64, division)") !=
	        std::string::npos);
	REQUIRE(mod.find("AL_CMIDI_LOOPSTART_CODE") != std::string::npos);
	REQUIRE(mod.find("AL_CMIDI_LOOPEND_CODE") != std::string::npos);
	REQUIRE(mod.find("modSequenceCloseOpenLoopsAtTrackEnd") !=
	        std::string::npos);
	REQUIRE(mod.find("modSequenceEmitLoopEndBody(track, 0xff)") !=
	        std::string::npos);
	REQUIRE(mod.find("modSequenceCloseOpenLoopsAtTrackEnd(track)") <
	        mod.find("AL_MIDI_META_EOT"));
	REQUIRE(mod.find("streaming playback failed") != std::string::npos);
	REQUIRE(mod.find("ASSET.SOURCE_ONLY: music sequence") !=
	        std::string::npos);
	REQUIRE(mod.find("but has no public FileProvider source") !=
	        std::string::npos);
	REQUIRE(mod.find("sequencer-native ") != std::string::npos);
	REQUIRE(mod.find("public source compile failed") != std::string::npos);
	REQUIRE(mod.find("assetSourceDebugIsEnabledFor(ASSET_AUDIO)") ==
	        std::string::npos);
	REQUIRE(mod_sequence_load.find("catalogResolveMusicSequence((s32)num)") <
	        mod_sequence_load.find("modSequenceCompilePublicSource(&r, num, outSize)"));
	REQUIRE(mod_sequence_load.find("modSequenceCompilePublicSource(&r, num, outSize)") <
	        mod_sequence_load.find("r.source_only_blocked"));
	REQUIRE(mod_sequence_load.find("modSequenceCompilePublicSource(&r, num, outSize)") <
	        mod_sequence_load.find("fsFileSize(MOD_SEQUENCES_DIR \"/\")"));
	REQUIRE(mod.find("modSequenceSiblingPath(r->path, \"sequence.mid\"") <
	        mod.find("fsFileSize(mid_path) <= 0"));
	REQUIRE(mod.find("fsFileSize(mid_path) <= 0") <
	        mod.find("modSequenceLoadEventsJson(json_path, tracks, &event_count)"));
	REQUIRE(snd.find("modSequencePlayAudioSource(seq->tracknum)") !=
	        std::string::npos);
	REQUIRE(snd.find("modSequencePlayAudioSource(seq->tracknum)") <
	        snd.find("modSequenceLoad(seq->tracknum, &extlen)"));
	REQUIRE(snd.find("modSequenceLoad(seq->tracknum, &extlen)") <
	        snd.find("g_SeqRomAddrs[seq->tracknum] < 0x10000"));
	REQUIRE(snd.find("seq->tracknum >= g_SeqTable->count") !=
	        std::string::npos);
	REQUIRE(snd.find("modSequenceIsVirtualTrack(seq->tracknum)") !=
	        std::string::npos);
	REQUIRE(snd.find("var8005ecf8[seq->tracknum] >= 0") !=
	        std::string::npos);
	REQUIRE(snd.find("r.source_only_blocked") != std::string::npos);
	REQUIRE(snd.find("audioStartFileSound(r.path, volume, pan") !=
	        std::string::npos);
	REQUIRE(snd.find("f32 filebasepitch = 1.0f") !=
	        std::string::npos);
	REQUIRE(snd.find("u8 file_sample_pan = AL_PAN_CENTER") !=
	        std::string::npos);
	REQUIRE(snd.find("u8 file_sample_volume = 127") !=
	        std::string::npos);
	REQUIRE(snd.find("u8 file_key_volume_index = 0") !=
	        std::string::npos);
	REQUIRE(snd.find("file_sample_pan = entry->ext.audio.sample_pan") !=
	        std::string::npos);
	REQUIRE(snd.find("file_sample_volume = entry->ext.audio.sample_volume") !=
	        std::string::npos);
	REQUIRE(snd.find("file_key_volume_index = (u8)(entry->ext.audio.key_min & 0x1f)") !=
	        std::string::npos);
	REQUIRE(snd.find("filebasepitch = alCents2Ratio(cents)") !=
	        std::string::npos);
	REQUIRE(snd.find("pitch,\n\t\t\t\t\tfilebasepitch") !=
	        std::string::npos);
	REQUIRE(snd.find("file_sample_pan,\n\t\t\t\t\tfile_sample_volume") !=
	        std::string::npos);
	REQUIRE(snd.find("file_key_volume_index") != std::string::npos);
	REQUIRE(snd.find("entry ? entry->ext.audio.has_loop : 0") !=
	        std::string::npos);
	REQUIRE(snd.find("entry ? entry->ext.audio.loop_start_samples : 0") !=
	        std::string::npos);
	REQUIRE(snd.find("entry ? entry->ext.audio.loop_end_samples : 0") !=
	        std::string::npos);
	REQUIRE(snd.find("entry ? entry->ext.audio.loop_count : 0") !=
	        std::string::npos);
	REQUIRE(snd.find("entry ? entry->ext.audio.has_envelope : 0") !=
	        std::string::npos);
	REQUIRE(snd.find("entry ? entry->ext.audio.attack_time_us : 0") !=
	        std::string::npos);
	REQUIRE(snd.find("entry ? entry->ext.audio.release_time_us : 0") !=
	        std::string::npos);
	REQUIRE(snd.find("file_fxmix_key_offset = (u8)((entry->ext.audio.key_max & 0x0f) * 8)") !=
	        std::string::npos);
	REQUIRE(snd.find("fxmix,\n\t\t\t\t\tfxbus,\n\t\t\t\t\tfile_fxmix_key_offset") !=
	        std::string::npos);
	REQUIRE(snd.find("ASSET.CHAIN: sound %d maps to public file source") !=
	        std::string::npos);
	REQUIRE(snd.find("but file playback failed; refusing native bank fallback") !=
	        std::string::npos);
	REQUIRE(snd.find("assetSourceDebugIsEnabledFor(ASSET_AUDIO)") ==
	        std::string::npos);
	REQUIRE(snd.find("falling back to ROM") == std::string::npos);
	const std::string snd_start_mp3 = functionBlock(snd, "void sndStartMp3(s16");
	const std::string snd_mp3_resolve =
		functionBlock(snd, "static s32 sndMp3ResolvePublicSource");
	REQUIRE(snd.find("#include \"asset_source_debug.h\"") ==
	        std::string::npos);
	REQUIRE(snd.find("#include \"fs.h\"") != std::string::npos);
	REQUIRE(snd.find("#include \"romextract.h\"") == std::string::npos);
	REQUIRE(snd.find("#include \"assetcatalog_load.h\"") !=
	        std::string::npos);
	REQUIRE(snd.find("static void *g_SndMp3SourceBytes = NULL") !=
	        std::string::npos);
	REQUIRE(snd.find("sndMp3FreeSourceBuffer()") != std::string::npos);
	REQUIRE(snd.find("sndMp3LoadPublicSourceFile") != std::string::npos);
	REQUIRE(snd.find("sndMp3ResolvePublicSource") != std::string::npos);
	REQUIRE(snd.find("catalogResolveFile(filenum)") != std::string::npos);
	REQUIRE(snd.find("fsFileLoad(source.path, &size)") !=
	        std::string::npos);
	REQUIRE(snd.find("romExtractRelPathForFilenum") == std::string::npos);
	REQUIRE(snd.find("g_SndMp3SourceBytes = bytes") != std::string::npos);
	REQUIRE(snd.find("ASSET.CHAIN: MP3 file") !=
	        std::string::npos);
	REQUIRE(snd.find("refusing loose extracted file or ROM playback fallback") !=
	        std::string::npos);
	REQUIRE(snd_start_mp3.find(
		        "g_AudioRussMappings[sp24.confignum].audioconfig_index") !=
	        std::string::npos);
	REQUIRE(snd_start_mp3.find("g_AudioConfigs[sp24.confignum]") ==
	        std::string::npos);
	REQUIRE(snd_start_mp3.find("config->volpercentage") <
	        snd_start_mp3.find("sndMp3ResolvePublicSource((s32)sp20.id"));
	REQUIRE(snd_start_mp3.find(
		        "config && (config->flags & AUDIOCONFIGFLAG_RESPONDHELLO)") !=
	        std::string::npos);
	REQUIRE(snd_start_mp3.find("sndMp3ResolvePublicSource((s32)sp20.id") <
	        snd_start_mp3.find("mp3PlayFile(g_SndCurMp3.romaddr, g_SndCurMp3.romsize)"));
	REQUIRE(snd_mp3_resolve.find(
		"return sndMp3LoadPublicSourceFile(filenum, outaddr, outsize)") !=
	        std::string::npos);
	const std::string snd_mp3_load =
		functionBlock(snd, "static s32 sndMp3LoadPublicSourceFile");
	REQUIRE(snd_mp3_load.find("catalogResolveFile(filenum)") <
	        snd_mp3_load.find("fsFileLoad(source.path, &size)"));
	REQUIRE(propsnd.find("#include \"asset_source_debug.h\"") ==
	        std::string::npos);
	REQUIRE(propsnd.find("#include \"fs.h\"") != std::string::npos);
	REQUIRE(propsnd.find("#include \"romextract.h\"") == std::string::npos);
	REQUIRE(propsnd.find("#include \"assetcatalog_load.h\"") !=
	        std::string::npos);
	REQUIRE(propsnd.find("psMp3DurationGetPublicSourceSize") !=
	        std::string::npos);
	REQUIRE(propsnd.find("catalogResolveFile(filenum)") !=
	        std::string::npos);
	REQUIRE(propsnd.find("fsFileSize(source.path)") !=
	        std::string::npos);
	REQUIRE(propsnd.find("romExtractRelPathForFilenum") ==
	        std::string::npos);
	REQUIRE(propsnd.find("assetSourceDebugIsEnabledFor(ASSET_AUDIO)") ==
	        std::string::npos);
	REQUIRE(propsnd.find("ASSET.CHAIN: MP3 file") !=
	        std::string::npos);
	REQUIRE(propsnd.find("ROM/static file-size fallback.") !=
	        std::string::npos);
	REQUIRE(propsnd.find("psMp3DurationGetPublicSourceSize((s32)soundnum.id)") !=
	        std::string::npos);
	REQUIRE(propsnd.find("catalogResolveFile(filenum)") <
	        propsnd.find("fsFileSize(source.path)"));
	REQUIRE(propsnd.find("fileGetRomSize(") == std::string::npos);
	REQUIRE(main_c.find("strcmp(s, \"voice\") == 0") != std::string::npos);
	REQUIRE(main_c.find("strcmp(s, \"song\") == 0") != std::string::npos);
	REQUIRE(main_c.find("strcmp(s, \"music\") == 0") != std::string::npos);
	REQUIRE(main_c.find("bootEnsureUiArchivesReadyAfterTextureInit") !=
	        std::string::npos);
	REQUIRE(main_c.find("written = romExtractAllPdui(0)") != std::string::npos);
	REQUIRE(main_c.find("texture_result = romExtractAllPdtexture(0)") <
	        main_c.find("written = romExtractAllPdui(0)"));
	{
		const std::string ui_ready = functionBlock(main_c,
			"s32 bootEnsureUiArchivesReadyAfterTextureInit");
		REQUIRE(!ui_ready.empty());
		requireTokenOrder(ui_ready,
			"texture_result = romExtractAllPdtexture(0)",
			"written = romExtractAllPdui(0)");
	}
	REQUIRE(pdgui_theme.find("g_TcGeneralConfigs") != std::string::npos);
	REQUIRE(pdgui_theme.find("cfg_copy = g_TcGeneralConfigs[e->tex_index]") !=
		std::string::npos);
	REQUIRE(pdgui_theme.find("s_decodeUiConfigWithTempPool") !=
		std::string::npos);
	REQUIRE(pdgui_theme.find("texInitPool(&pool") != std::string::npos);
	REQUIRE(pdgui_theme.find("extractor-only") != std::string::npos);
	REQUIRE(pdgui_theme.find("deferred to render-loop trigger") ==
		std::string::npos);
	{
		const std::string check_extract = functionBlock(pdgui_theme,
			"void pdguiThemeCheckExtract");
		REQUIRE(!check_extract.empty());
		REQUIRE(check_extract.find("bootProgressIsActive()") <
		        check_extract.find("s_checked = true;"));
		REQUIRE(check_extract.find("bootProgressIsComplete()") <
		        check_extract.find("s_checked = true;"));
		REQUIRE(check_extract.find("pdguiThemeEmitPduiZips(0)") >
		        check_extract.find("s_checked = true;"));
	}
	REQUIRE(main_c.find("loaderWalkerScanUi(data_root, &kr)") !=
	        std::string::npos);
	REQUIRE(main_c.find("pdguiThemeReloadPduiSourceTextures()") !=
	        std::string::npos);
	REQUIRE(main_c.find("BOOT: UI source archives ready after texReset") !=
	        std::string::npos);
	{
		const std::string tex_reset = functionBlock(texreset_c, "void texReset");
		REQUIRE(tex_reset.find("g_TexWords[i] = NULL;") <
		        tex_reset.find("bootEnsureUiArchivesReadyAfterTextureInit();"));
		REQUIRE(tex_reset.find("bootEnsureUiArchivesReadyAfterTextureInit();") <
		        tex_reset.find("texLoadFromDisplayList(g_TexGdl1"));
	}
	{
		const std::string reload = functionBlock(pdgui_theme,
			"void pdguiThemeReloadPduiSourceTextures");
		REQUIRE(reload.find("s_ThemeLateInitDone = false") <
		        reload.find("pdguiThemeLateInit();"));
	}
	{
		const std::string apply_ui = functionBlock(pdgui_theme,
			"static void s_applyCatalogUiAsset");
		REQUIRE(apply_ui.find("s_loadThemeTextureFromPath(entry->id, path)") !=
		        std::string::npos);
		REQUIRE(apply_ui.find("s_registerModTexture(entry->id, path)") ==
		        std::string::npos);
	}
	REQUIRE(loader_ui.find("assetCatalogGetMutable(id)") != std::string::npos);
	REQUIRE(loader_ui.find("if (!e)") < loader_ui.find("assetCatalogRegister(id, ASSET_UI)"));
	REQUIRE(loader_ui.find("catalogSetPrimaryFile(e, source_path)") !=
	        std::string::npos);
	REQUIRE(loader_ui.find("e->ext.ui.texture_file") != std::string::npos);
	REQUIRE(loader_ui.find("e->ext.ui.layout_file") != std::string::npos);
	REQUIRE(loader_ui.find("e->ext.ui.nineslice_file") != std::string::npos);
	REQUIRE(loader_ui.find("e->ext.ui.nineslice_left") != std::string::npos);
	REQUIRE(loader_ui.find("\"edgeMode\"") != std::string::npos);
	REQUIRE(main_c.find("bootArmDebugLoadCatalogAssets") != std::string::npos);
	REQUIRE(main_c.find("--debug-play-catalog-audio") != std::string::npos);
	REQUIRE(main_c.find("--debug-play-catalog-audio-source-only") !=
	        std::string::npos);
	REQUIRE(main_c.find("catalogResolveAudio(asset_id, &audio)") !=
	        std::string::npos);
	REQUIRE(main_c.find("assetSourceDebugSetOnlyType(ASSET_AUDIO)") !=
	        std::string::npos);
	REQUIRE(main_c.find("extern void sndSetSfxVolume(s32 vol)") ==
	        std::string::npos);
	REQUIRE(main_c.find("catalogLoadTypedAsset(ASSET_AUDIO, asset_id)") !=
	        std::string::npos);
	REQUIRE(main_c.find("sndStart(0, (s16)audio->sound_id") !=
	        std::string::npos);
	REQUIRE(main_c.find("bootDebugPlayCatalogSong(kind, asset_id, &audio)") !=
	        std::string::npos);
	REQUIRE(main_c.find("state=registered") != std::string::npos);
	REQUIRE(main_c.find("state=unassigned") != std::string::npos);
	REQUIRE(main_c.find("static void bootExitAfterCatalogProbesIfRequested(void)") !=
	        std::string::npos);
	REQUIRE(main_c.find("--debug-load-catalog-assets-file") !=
	        std::string::npos);
	REQUIRE(main_c.find("bootApplyDebugLoadCatalogAssetsFile") !=
	        std::string::npos);
	REQUIRE(main_c.find("g_BootDebugLoadCatalogAssetsPending = 1") !=
	        std::string::npos);
	REQUIRE(main_c.find("s32 bootApplyDeferredDebugLoadCatalogAssets(void)") !=
	        std::string::npos);
	{
		const std::string boot_catalog = functionBlock(main_c,
			"static void bootRunCatalogWork");
		REQUIRE(boot_catalog.find("bootArmDebugLoadCatalogAssets();") !=
		        std::string::npos);
		requireTokenOrder(boot_catalog,
			"Asset Catalog: %d entries registered",
			"bootArmDebugLoadCatalogAssets();");
		REQUIRE(boot_catalog.find("bootArmDebugLoadCatalogAssets();") <
		        boot_catalog.find("romExtractAllPdmesh(0)"));
		REQUIRE(boot_catalog.find("romExtractAllPdtexture(0)") <
		        boot_catalog.find("romExtractAllPdui(0)"));
	}
	{
		const std::string cli_fast_paths = functionBlock(main_c,
			"static void bootApplyCliFastPaths");
		REQUIRE(cli_fast_paths.find("bootArmDebugLoadCatalogAssets();") !=
		        std::string::npos);
		REQUIRE(cli_fast_paths.find("bootApplyDeferredDebugLoadCatalogAssets();") !=
		        std::string::npos);
		requireTokenOrder(cli_fast_paths,
			"bootArmDebugLoadCatalogAssets();",
			"bootApplyDeferredDebugLoadCatalogAssets();");
		REQUIRE(cli_fast_paths.find("bootApplyDebugLoadCatalogAssets(") ==
		        std::string::npos);
	}
	{
		const std::string anim_probe = functionBlock(main_c,
			"static void bootApplyDebugProbeAnimationSource");
		REQUIRE(anim_probe.find("catalogLoadTypedAsset(ASSET_ANIMATION, asset_id)") !=
		        std::string::npos);
		REQUIRE(anim_probe.find("result=INVALID_ANIM") != std::string::npos);
		const std::size_t anim_source_only =
			anim_probe.find("assetSourceDebugSetOnlyType(ASSET_ANIMATION)");
		const std::size_t anim_load =
			anim_probe.find("catalogLoadTypedAsset(ASSET_ANIMATION, asset_id)");
		const std::size_t anim_invalid =
			anim_probe.find("result=INVALID_ANIM");
		const std::size_t anim_exit =
			anim_probe.find("bootExitAfterCatalogProbesIfRequested()");
		REQUIRE(anim_source_only != std::string::npos);
		REQUIRE(anim_load != std::string::npos);
		REQUIRE(anim_invalid != std::string::npos);
		REQUIRE(anim_exit != std::string::npos);
		REQUIRE(anim_source_only < anim_load);
		REQUIRE(anim_load < anim_invalid);
	}
	{
		const std::string audio_probe = functionBlock(main_c,
			"static void bootApplyDebugPlayCatalogAudio(const char *arg");
		REQUIRE(audio_probe.find("bootApplyDebugPlayCatalogAudioToken") !=
		        std::string::npos);
		REQUIRE(audio_probe.find("bootExitAfterCatalogProbesIfRequested()") !=
		        std::string::npos);
		REQUIRE(audio_probe.find("bootApplyDebugPlayCatalogAudioToken") <
		        audio_probe.find("bootExitAfterCatalogProbesIfRequested()"));
	}
	{
		std::size_t deferred_impl =
			main_c.find("s32 bootApplyDeferredDebugLoadCatalogAssets(void)\n{");
		if (deferred_impl == std::string::npos) {
			deferred_impl =
				main_c.find("s32 bootApplyDeferredDebugLoadCatalogAssets(void)\r\n{");
		}
		REQUIRE(deferred_impl != std::string::npos);
		const std::size_t defer_gate =
			main_c.find("bootDebugCatalogProbesNeedCompletedBaseEmit()",
				deferred_impl);
		const std::size_t defer_complete =
			main_c.find("!bootProgressIsComplete()", deferred_impl);
		const std::size_t defer_anim =
			main_c.find("bootDebugCatalogProbesNeedAnimationTable()",
				deferred_impl);
		const std::size_t defer_wait =
			main_c.find("return 0;", defer_gate);
		const std::size_t defer_anim_wait =
			main_c.find("return 0;", defer_anim);
		const std::size_t defer_clear =
			main_c.find("g_BootDebugLoadCatalogAssetsPending = 0;",
				deferred_impl);
		REQUIRE(defer_gate != std::string::npos);
		REQUIRE(defer_complete != std::string::npos);
		REQUIRE(defer_anim != std::string::npos);
		REQUIRE(defer_wait != std::string::npos);
		REQUIRE(defer_anim_wait != std::string::npos);
		REQUIRE(defer_clear != std::string::npos);
		REQUIRE(defer_gate < defer_wait);
		REQUIRE(defer_complete < defer_wait);
		REQUIRE(defer_anim < defer_anim_wait);
		REQUIRE(defer_anim_wait < defer_clear);
		REQUIRE(defer_wait < defer_clear);
		REQUIRE(main_c.find("bootExitAfterCatalogProbesIfRequested();",
			        defer_clear) != std::string::npos);
		const std::size_t deferred_start =
			main_c.find("\n\tg_BootDebugLoadCatalogAssetsPending = 0;");
		REQUIRE(deferred_start != std::string::npos);
		const std::size_t ensure_ui =
			main_c.find("bootEnsureUiArchivesReadyForCliSourceLoads();",
				deferred_start);
		const std::size_t load_arg =
			main_c.find("bootApplyDebugLoadCatalogAssets(",
				deferred_start);
		const std::size_t load_file =
			main_c.find("bootApplyDebugLoadCatalogAssetsFile(",
				deferred_start);
		REQUIRE(ensure_ui != std::string::npos);
		REQUIRE(load_arg != std::string::npos);
		REQUIRE(load_file != std::string::npos);
		REQUIRE(ensure_ui < load_arg);
		REQUIRE(load_arg < load_file);
	}
	{
		const std::string theme_check = functionBlock(pdgui_theme,
			"void pdguiThemeCheckExtract");
		REQUIRE(theme_check.find("pdguiThemeEmitPduiZips(0)") <
		        theme_check.find("bootApplyDeferredDebugLoadCatalogAssets()"));
	}
	{
		const std::string loaded_ui = functionBlock(pdgui_theme,
			"static void s_registerLoadedThemeTexture");
		REQUIRE(loaded_ui.find("s_findPduiEntryByCatalogId(catalog_id)") !=
		        std::string::npos);
		REQUIRE(loaded_ui.find("e->load_state = ASSET_STATE_ACTIVE") !=
		        std::string::npos);
		REQUIRE(loaded_ui.find("e->payload_kind = ASSET_PAYLOAD_RUNTIME_ACTIVE") !=
		        std::string::npos);
		REQUIRE(loaded_ui.find("\"%s::texture.tga\"") != std::string::npos);
		REQUIRE(loaded_ui.find("catalogSetPrimaryFile(e, member_path)") !=
		        std::string::npos);
	}
	REQUIRE(all_family_smoke.find("audio=base:sfx_alarm_2") != std::string::npos);
	REQUIRE(all_family_smoke.find("voice=base:voice_cover_me_aiw") !=
	        std::string::npos);
	REQUIRE(all_family_smoke.find("song=base:song_sequence_a") !=
	        std::string::npos);
	REQUIRE(all_family_smoke.find("id='base:voice_cover_me_aiw' result=OK") !=
	        std::string::npos);
	REQUIRE(all_family_smoke.find("id='base:song_sequence_a' result=OK") !=
	        std::string::npos);
	REQUIRE(all_family_smoke.find("\"min\": 24, \"max\": 24") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("--debug-play-catalog-audio-source-only") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("--debug-exit-after-catalog-probes") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("--debug-play-catalog-audio") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("    \"--no-sound\",") ==
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("sfx=base:sfx_alarm_2,voice=base:voice_cover_me_aiw,song=base:song_sequence_a") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("BOOT: --debug-play-catalog-audio request kind=sfx id='base:sfx_alarm_2' source_only=1") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("BOOT: --debug-play-catalog-audio result kind=sfx id='base:sfx_alarm_2' result=OK") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("BOOT: --debug-play-catalog-audio result kind=voice id='base:voice_cover_me_aiw' result=OK") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("CATALOG: music sequence .* -> public sequence source") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("BOOT: --debug-play-catalog-audio result kind=song id='base:song_sequence_a' result=OK") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("\"BOOT: --debug-play-catalog-audio result kind=.* result=OK\", \"min\": 3, \"max\": 3") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("LOUDFAIL\\\\.FALLBACK") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("ASSET\\\\.FALLBACK") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("Runs exhaustive non-Scenario typed-archive source-only catalog smokes") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find(".pdscenario") == std::string::npos);
	REQUIRE(all_family_matrix.find("\".pdsfx\"") != std::string::npos);
	REQUIRE(all_family_matrix.find("\".pdvoice\"") != std::string::npos);
	REQUIRE(all_family_matrix.find("\".pdsong\"") != std::string::npos);
	REQUIRE(all_family_matrix.find("Request = \"sfx\";") != std::string::npos);
	REQUIRE(all_family_matrix.find("Request = \"voice\";") != std::string::npos);
	REQUIRE(all_family_matrix.find("Request = \"song\";") != std::string::npos);
	REQUIRE(all_family_matrix.find("Result = \"audio\";") != std::string::npos);
	REQUIRE(all_family_matrix.find("--debug-load-catalog-assets-source-only") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("--debug-load-catalog-assets-file") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find(".assets.txt") != std::string::npos);
	REQUIRE(all_family_matrix.find("BugId = \"B-509\"") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("all-family-source-matrix-manifest") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("asset_list_file") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("debug_load_argument") ==
	        std::string::npos);
	REQUIRE(all_family_matrix.find("Missing debug-load request/result blocks are B-509") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("BOOT: --debug-load-catalog-assets request type=.* source_only=1") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("BOOT: --debug-load-catalog-assets result type=") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("--debug-load-catalog-assets invalid token") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("--debug-load-catalog-assets token '.*' missing type=id form") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("RomProvider:filenum") != std::string::npos);
	REQUIRE(all_family_matrix.find("refusing fallback") != std::string::npos);
	REQUIRE(smoke_run.find("$resultLabel = if ($r.Passed)") !=
	        std::string::npos);
	REQUIRE(smoke_run.find("$tag = if ($r.Passed)") == std::string::npos);

	REQUIRE(mainmenu.find("Asset Source Gate") != std::string::npos);
	REQUIRE(mainmenu.find("assetSourceDebugOnlyType()") != std::string::npos);
	REQUIRE(mainmenu.find("ImGui::Checkbox(family.label, &checked)") !=
	        std::string::npos);
	REQUIRE(mainmenu.find("assetSourceDebugSetOnlyType(checked ? family.type : ASSET_NONE)") !=
	        std::string::npos);

	const std::string family_marker =
		"static const DebugAssetSourceFamily s_DebugAssetSourceFamilies[]";
	const std::string family_end_marker = "static void renderSettingsDebug";
	const size_t family_start = mainmenu.find(family_marker);
	REQUIRE(family_start != std::string::npos);
	const size_t family_end = mainmenu.find(family_end_marker, family_start);
	REQUIRE(family_end != std::string::npos);
	const std::string families =
		mainmenu.substr(family_start, family_end - family_start);

	size_t listed_families = 0;
	size_t listed_pos = 0;
	while ((listed_pos = families.find("{ ASSET_", listed_pos)) != std::string::npos) {
		listed_families++;
		listed_pos += 8;
	}
	REQUIRE(listed_families == 25);

	auto requireFamily = [&](const char *type, const char *label) {
		REQUIRE(families.find(type) != std::string::npos);
		REQUIRE(families.find(label) != std::string::npos);
	};

	requireFamily("ASSET_WEAPON", "Weapon (.pdweapon)");
	requireFamily("ASSET_PROJECTILE", "Projectile (.pdprojectile)");
	requireFamily("ASSET_ENTITY", "Entity (.pdentity)");
	requireFamily("ASSET_CHARACTER", "Character (.pdcharacter)");
	requireFamily("ASSET_HEAD", "Head (.pdhead)");
	requireFamily("ASSET_BODY", "Body (.pdbody)");
	requireFamily("ASSET_ARENA", "Arena (.pdarena)");
	requireFamily("ASSET_SCENARIO", "Scenario (.pdscenario)");
	requireFamily("ASSET_MISSION", "Mission (.pdmission)");
	requireFamily("ASSET_MODEL", "Mesh (.pdmesh)");
	requireFamily("ASSET_ANIMATION", "Animation (.pdanim)");
	requireFamily("ASSET_TEXTURE", "Texture (.pdtexture)");
	requireFamily("ASSET_MATERIAL", "Material (.pdmaterial)");
	requireFamily("ASSET_SKIN", "Skin (.pdskin)");
	requireFamily("ASSET_EFFECT", "Effect (.pdeffect)");
	requireFamily("ASSET_PROP", "Prop (.pdprop)");
	requireFamily("ASSET_VEHICLE", "Vehicle (.pdvehicle)");
	requireFamily("ASSET_AUDIO", "Audio (.pdsfx/.pdvoice/.pdsong)");
	requireFamily("ASSET_UI", "UI (.pdui)");
	requireFamily("ASSET_FONT", "Font (.pdfont)");
	requireFamily("ASSET_LANG", "Language (.pdlang)");
	requireFamily("ASSET_GAMEMODE", "Gamemode (.pdgamemode)");
	requireFamily("ASSET_BOT_PROFILE", "Bot Profile (.pdbotprofile)");
	requireFamily("ASSET_HUD", "HUD (.pdhud)");
	requireFamily("ASSET_THEME", "Theme (.pdtheme)");

	REQUIRE(families.find("ASSET_TOOL") == std::string::npos);
	REQUIRE(families.find("ASSET_MAP") == std::string::npos);
	REQUIRE(families.find("ASSET_TEXTURES") == std::string::npos);
	REQUIRE(families.find("ASSET_SFX") == std::string::npos);
	REQUIRE(families.find("ASSET_MUSIC") == std::string::npos);
	REQUIRE(families.find("ASSET_BOT_VARIANT") == std::string::npos);
}

TEST_CASE("public source families have explicit runtime load surfaces",
          "[modding][pdxxx][runtime][c3838][adapters][static]") {
	const std::string load = readTextFile("port/src/assetcatalog_load.c");
	const std::string runtime = readTextFile("port/src/asset_runtime.c");
	const std::string supports =
		functionBlock(runtime, "assetRuntimeSupportsType");
	const std::string activate =
		functionBlock(runtime, "assetRuntimeActivateCatalogEntry");

	const char *adapter_families[] = {
		"ASSET_WEAPON",
		"ASSET_PROJECTILE",
		"ASSET_ENTITY",
		"ASSET_CHARACTER",
		"ASSET_HEAD",
		"ASSET_BODY",
		"ASSET_ARENA",
		"ASSET_SCENARIO",
		"ASSET_MISSION",
		"ASSET_MATERIAL",
		"ASSET_SKIN",
		"ASSET_EFFECT",
		"ASSET_PROP",
		"ASSET_VEHICLE",
		"ASSET_UI",
		"ASSET_FONT",
		"ASSET_LANG",
		"ASSET_GAMEMODE",
		"ASSET_BOT_PROFILE",
		"ASSET_HUD",
		"ASSET_THEME",
	};

	REQUIRE(!supports.empty());
	REQUIRE(!activate.empty());
	for (const char *family : adapter_families) {
		const std::string case_label =
			std::string("case ") + family + ":";
		REQUIRE(supports.find(case_label) != std::string::npos);
		REQUIRE(activate.find(case_label) != std::string::npos);
	}

	REQUIRE(load.find("s_catalogTypeUsesModelPayload(entry->type)") !=
	        std::string::npos);
	REQUIRE(load.find("s_catalogLoadEntryAnimationPayload(entry)") !=
	        std::string::npos);
	REQUIRE(load.find("s_catalogTypeUsesAudioRuntimePayload(entry->type)") !=
	        std::string::npos);
	REQUIRE(load.find("entry->type == ASSET_TEXTURE") != std::string::npos);
	REQUIRE(load.find("s_catalogLoadEntryTexturePayload(entry, handle)") !=
	        std::string::npos);
	{
		const std::string texture_source = readTextFile("port/src/mod_texture_source.c");
		const std::string mod = readTextFile("port/src/mod.c");
		REQUIRE(texture_source.find("assetSourceDebugIsEnabledFor(ASSET_TEXTURE)") ==
		        std::string::npos);
		REQUIRE(texture_source.find("the selected public source is not an editable image source") !=
		        std::string::npos);
		REQUIRE(texture_source.find("modTextureFindDirectPublicSource") !=
		        std::string::npos);
		REQUIRE(texture_source.find("assetCatalogGetCount()") !=
		        std::string::npos);
		REQUIRE(texture_source.find("entry->source_texnum != (s32)num") !=
		        std::string::npos);
		REQUIRE(texture_source.find("entry->source.primary.provider == fileProvider()") !=
		        std::string::npos);
		REQUIRE(texture_source.find("fileProviderPath(entry->source.primary)") !=
		        std::string::npos);
		REQUIRE(texture_source.find("modTextureFindDirectPublicSource(num, &source_path") !=
		        std::string::npos);
		REQUIRE(mod.find("assetSourceDebugIsEnabledFor(ASSET_TEXTURE)") ==
		        std::string::npos);
		REQUIRE(mod.find("refusing legacy compressed texture fallback") !=
		        std::string::npos);
	}
}

TEST_CASE("universal extracted archive walkers bind public source members",
          "[modding][pdxxx][c3844][source][static]") {
	const std::string common_h = readTextFile("port/include/loader_walker_common.h");
	const std::string common = readTextFile("port/src/loader_walker_common.c");
	const std::string fs = readTextFile("port/src/fs.c");
	const std::string load = readTextFile("port/src/assetcatalog_load.c");
	const std::string meta_walker = readTextFile("port/src/loader_walker_meta.c");
	const std::string provider = readTextFile("port/src/assetprovider_file.c");
	const std::string player = readTextFile("src/game/player.c");
	const std::string menu = readTextFile("src/game/menu.c");
	const std::string mainmenu = readTextFile("src/game/mainmenu.c");
	const std::string mplayer_setup = readTextFile("src/game/mplayer/setup.c");
	const std::string title = readTextFile("src/game/title.c");
	const std::string modelcatalog = readTextFile("port/src/modelcatalog.c");
	const std::string forge_runtime = readTextFile("port/src/forge/forge_runtime.c");
	const std::string setuputils = readTextFile("src/game/setuputils.c");
	const std::string body_runtime = readTextFile("src/game/body.c");
	const std::string chraction = readTextFile("src/game/chraction.c");
	const std::string modeldef = readTextFile("src/game/modeldef.c");
	const std::string setup = readTextFile("src/game/setup.c");
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");
	const std::string conformance = readTextFile("tools/asset_archive_conformance.py");
	const std::string body_mgr = readTextFile("port/src/catalog_mgr_bodies.c");
	const std::string head_mgr = readTextFile("port/src/catalog_mgr_heads.c");
	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	const std::string netdistrib = readTextFile("port/src/net/netdistrib.c");
	const std::string runtime = readTextFile("port/src/asset_runtime.c");

	REQUIRE(common_h.find("loaderWalkerArchiveMemberPath") != std::string::npos);
	REQUIRE(common_h.find("loaderWalkerMarkBaseArchiveEntry") != std::string::npos);
	REQUIRE(common.find("snprintf(out, out_n, \"%s::%s\", archive_path, member)") !=
	        std::string::npos);
	REQUIRE(common.find("register_mutex") != std::string::npos);
	REQUIRE(common.find("SDL_LockMutex(ctx->register_mutex)") != std::string::npos);
	REQUIRE(common.find("entry->ref_count = ASSET_REF_BUNDLED") !=
	        std::string::npos);
	REQUIRE(provider.find("s_PathMutex") != std::string::npos);
	REQUIRE(provider.find("SDL_LockMutex(s_PathMutex)") != std::string::npos);
	REQUIRE(fs.find("fsExtractNestedArchiveChain") != std::string::npos);
	REQUIRE(fs.find("sep + 2") != std::string::npos);
	REQUIRE(load.find("s_catalogTypePreloadsBundledMetadataPayload(e->type)") !=
	        std::string::npos);
	REQUIRE(load.find("type == ASSET_ANIMATION || type == ASSET_SCENARIO") !=
	        std::string::npos);
	REQUIRE(load.find("static s32 s_catalogPreloadBundledMetadataPayload(asset_entry_t *entry)") !=
	        std::string::npos);
	REQUIRE(load.find("s_catalogPreloadBundledMetadataPayload(mutable_entry)") !=
	        std::string::npos);
	REQUIRE(load.find("bundled metadata preload") <
	        load.find("return s_catalogLoadEntryMetadataPayload(entry)"));
	REQUIRE(load.find("s_catalogLoadEntryModelPayload(entry, handle)") !=
	        std::string::npos);
	const std::string modelPayloadBlock =
		functionBlock(load, "static s32 s_catalogTypeUsesModelPayload");
	const std::string metadataPayloadBlock =
		functionBlock(load, "static s32 s_catalogTypeUsesMetadataRuntimePayload");
	REQUIRE(modelPayloadBlock.find("case ASSET_PROP:") == std::string::npos);
	REQUIRE(load.find("|| type == ASSET_PROP") != std::string::npos);
	REQUIRE(load.find("s_catalogLoadEntryAudioPayload(entry, handle)") <
	        load.find("CATALOG: retain bundled"));
	REQUIRE(load.find("s_catalogLoadEntryTexturePayload(entry, handle)") <
	        load.find("CATALOG: retain bundled"));
	REQUIRE(load.find("&& e->source.primary.provider == fileProvider()") !=
	        std::string::npos);
	const std::string compiler = readTextFile("port/src/modasset_compiler.c");
	REQUIRE(compiler.find("skipped_degenerate") != std::string::npos);
	REQUIRE(compiler.find("len2 <= 0.000001f") != std::string::npos);
	REQUIRE(compiler.find("continue;") != std::string::npos);

	const char *walkers[] = {
		"port/src/loader_walker_anim.c",
		"port/src/loader_walker_arena.c",
		"port/src/loader_walker_body.c",
		"port/src/loader_walker_font.c",
		"port/src/loader_walker_head.c",
		"port/src/loader_walker_lang.c",
		"port/src/loader_walker_mesh.c",
		"port/src/loader_walker_scenario.c",
		"port/src/loader_walker_sfx.c",
		"port/src/loader_walker_song.c",
		"port/src/loader_walker_ui.c",
		"port/src/loader_walker_voice.c",
	};
	for (const char *path : walkers) {
		const std::string walker = readTextFile(path);
		INFO(path);
		REQUIRE(walker.find("(void)file_path") == std::string::npos);
		REQUIRE(walker.find("loaderWalkerArchiveMemberPath") != std::string::npos);
		REQUIRE(walker.find("catalogSetPrimaryFile") != std::string::npos);
		REQUIRE(walker.find("loaderWalkerMarkBaseArchiveEntry") !=
		        std::string::npos);
	}

	const char *metadataExts[] = {
		".pdarena", ".pdcharacter", ".pdskin", ".pdprop", ".pdvehicle", ".pdmission",
		".pdgamemode", ".pdbotprofile", ".pdhud", ".pdeffect", ".pdmaterial",
		".pdtheme", ".pdbody", ".pdhead",
	};
	for (const char *ext : metadataExts) {
		INFO(ext);
		REQUIRE(meta_walker.find(ext) != std::string::npos);
	}
	REQUIRE(meta_walker.find("desc.always_invoke = 1") != std::string::npos);
	REQUIRE(meta_walker.find("assetCatalogGetMutable(id)") != std::string::npos);
	REQUIRE(meta_walker.find("entry->type != type") != std::string::npos);
	REQUIRE(meta_walker.find("loaderWalkerArchiveMemberPath") != std::string::npos);
	REQUIRE(meta_walker.find("catalogSetPrimaryFile(entry, source_path)") !=
	        std::string::npos);
	REQUIRE(meta_walker.find("loaderWalkerMarkBaseArchiveEntry(entry)") !=
	        std::string::npos);
	REQUIRE(meta_walker.find("s_copyManifestMemberPath") != std::string::npos);
	REQUIRE(meta_walker.find("\"body_archive\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"head_archive\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"scenario_archive\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"skin_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"swatches_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"material_archive\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"texture_archive\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"prop_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"physics_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"behavior_graph\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"timeline_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"mission_graph_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("{ ASSET_PROP,        \"prop\",       \"props\",       \".pdprop\",       \"prop.ini\",      { \"model_file\", \"prop_file\", \"behavior_graph\", NULL } }") !=
	        std::string::npos);
	REQUIRE(meta_walker.find("{ ASSET_PROP,        \"prop\",       \"props\",       \".pdprop\",       \"prop.ini\",      { \"model_file\", \"behavior_graph\", \"prop_file\", NULL } }") ==
	        std::string::npos);
	REQUIRE(meta_walker.find("{ ASSET_VEHICLE,     \"vehicle\",    \"vehicles\",    \".pdvehicle\",    \"vehicle.ini\",   { \"model_file\", \"behavior_graph\", \"physics_file\", NULL } }") !=
	        std::string::npos);
	REQUIRE(meta_walker.find("{ ASSET_VEHICLE,     \"vehicle\",    \"vehicles\",    \".pdvehicle\",    \"vehicle.ini\",   { \"physics_file\", \"behavior_graph\", \"model_file\", NULL } }") ==
	        std::string::npos);
	REQUIRE(meta_walker.find("\"theme_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.prop.model_file") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.prop.behavior_graph") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.vehicle.model_file") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.vehicle.behavior_graph") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.effect.timeline_file") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.arena.scenario_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.mission.scenario_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.hud.texture_file") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.material.texture_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.theme.ui_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.theme.audio_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.theme.music_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.theme.effect_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.gamemode.mode_id") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.gamemode.max_players") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.gamemode.requirefeature") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.bot_profile.target_body") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.bot_profile.difficulty") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.bot_profile.requirefeature") != std::string::npos);
	REQUIRE(meta_walker.find("s_manifestBotType") != std::string::npos);
	REQUIRE(meta_walker.find("s_manifestBotDifficulty") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.body.mesh_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.body.hand_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.head.mesh_archive") != std::string::npos);
	REQUIRE(readTextFile("port/src/loader_walker_body.c").find("s64 bodynum = -1;") !=
	        std::string::npos);
	REQUIRE(readTextFile("port/src/loader_walker_body.c").find("s64 bodynum = 0;") ==
	        std::string::npos);
	REQUIRE(readTextFile("port/src/loader_walker_head.c").find("s64 headnum = -1;") !=
	        std::string::npos);
	REQUIRE(readTextFile("port/src/loader_walker_head.c").find("s64 headnum = 0;") ==
	        std::string::npos);
	REQUIRE(conformance.find("\".pdmaterial\": schema(") != std::string::npos);
	REQUIRE(conformance.find("required=[\"material.ini\", \"material.json\"]") !=
	        std::string::npos);
	REQUIRE(scanner.find("\"mesh_archive\"") != std::string::npos);
	REQUIRE(scanner.find("const char *pf = iniGet(ini, \"material_file\",") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"texture_archive\",\n\t\t\t\tiniGet(ini, \"texture_file\"") ==
	        std::string::npos);
	REQUIRE(scanner.find("e->ext.arena.scenario_archive") != std::string::npos);
	REQUIRE(scanner.find("e->ext.body.mesh_archive") != std::string::npos);
	REQUIRE(scanner.find("e->ext.body.hand_archive") != std::string::npos);
	REQUIRE(scanner.find("e->ext.head.mesh_archive") != std::string::npos);
	{
		const size_t arena_start = scanner.find("case ASSET_ARENA:");
		const size_t body_start = scanner.find("case ASSET_BODY:", arena_start);
		REQUIRE(arena_start != std::string::npos);
		REQUIRE(body_start != std::string::npos);
		const std::string arena_case =
			scanner.substr(arena_start, body_start - arena_start);
		REQUIRE(arena_case.find("catalogSetPrimaryFile(e, e->ext.arena.scenario_archive)") !=
		        std::string::npos);
		REQUIRE(arena_case.find("geometry_file") == std::string::npos);
		REQUIRE(arena_case.find("iniGet(ini, \"geometry\"") == std::string::npos);
	}
	{
		const size_t body_start = scanner.find("case ASSET_BODY:");
		const size_t head_start = scanner.find("case ASSET_HEAD:", body_start);
		const size_t model_start = scanner.find("case ASSET_MODEL:", head_start);
		REQUIRE(body_start != std::string::npos);
		REQUIRE(head_start != std::string::npos);
		REQUIRE(model_start != std::string::npos);
		const std::string body_case =
			scanner.substr(body_start, head_start - body_start);
		const std::string head_case =
			scanner.substr(head_start, model_start - head_start);
		REQUIRE(body_case.find("resolveBodyRuntimeSlotForScan(") !=
		        std::string::npos);
		REQUIRE(body_case.find("e->runtime_index = e->ext.body.bodynum") !=
		        std::string::npos);
		REQUIRE(body_case.find("sourceModelPathFromMeshArchive(e->ext.body.mesh_archive") !=
		        std::string::npos);
		REQUIRE(body_case.find("parseBodyManifestForPrivateSlot(e->id") !=
		        std::string::npos);
		REQUIRE(body_case.find("model_file") == std::string::npos);
		REQUIRE(head_case.find("resolveHeadRuntimeSlotForScan(") !=
		        std::string::npos);
		REQUIRE(head_case.find("e->runtime_index = e->ext.head.headnum") !=
		        std::string::npos);
		REQUIRE(head_case.find("sourceModelPathFromMeshArchive(e->ext.head.mesh_archive") !=
		        std::string::npos);
		REQUIRE(head_case.find("parseHeadManifestForPrivateSlot(e->id") !=
		        std::string::npos);
		REQUIRE(head_case.find("model_file") == std::string::npos);
	}
	REQUIRE(scanner.find("e->ext.gamemode.max_players") != std::string::npos);
	REQUIRE(scanner.find("e->ext.bot_profile.target_body") != std::string::npos);
	REQUIRE(scanner.find("const char *pf = e->ext.theme.theme_file") != std::string::npos);
	REQUIRE(scanner.find("e->ext.theme.ui_archive[0] ?") == std::string::npos);
	REQUIRE(netdistrib.find("e->ext.arena.scenario_archive") != std::string::npos);
	REQUIRE(netdistrib.find("e->ext.body.mesh_archive") != std::string::npos);
	REQUIRE(netdistrib.find("e->ext.body.hand_archive") != std::string::npos);
	REQUIRE(netdistrib.find("e->ext.head.mesh_archive") != std::string::npos);
	{
		const size_t arena_start = netdistrib.find("case ASSET_ARENA:");
		const size_t body_start = netdistrib.find("case ASSET_BODY:", arena_start);
		REQUIRE(arena_start != std::string::npos);
		REQUIRE(body_start != std::string::npos);
		const std::string arena_case =
			netdistrib.substr(arena_start, body_start - arena_start);
		REQUIRE(arena_case.find("distribSetPrimaryFromFile(e, dirpath,\n                                      e->ext.arena.scenario_archive)") !=
		        std::string::npos);
		REQUIRE(arena_case.find("geometry_file") == std::string::npos);
		REQUIRE(arena_case.find("iniGet(ini, \"geometry\"") == std::string::npos);
	}
	{
		const size_t body_start = netdistrib.find("case ASSET_BODY:");
		const size_t head_start = netdistrib.find("case ASSET_HEAD:", body_start);
		const size_t model_start = netdistrib.find("case ASSET_MODEL:", head_start);
		REQUIRE(body_start != std::string::npos);
		REQUIRE(head_start != std::string::npos);
		REQUIRE(model_start != std::string::npos);
		const std::string body_case =
			netdistrib.substr(body_start, head_start - body_start);
		const std::string head_case =
			netdistrib.substr(head_start, model_start - head_start);
		REQUIRE(body_case.find("distribResolveBodyRuntimeSlot(") !=
		        std::string::npos);
		REQUIRE(body_case.find("e->runtime_index = e->ext.body.bodynum") !=
		        std::string::npos);
		REQUIRE(body_case.find("distribSourceModelPathFromMeshArchive(mesh_full") !=
		        std::string::npos);
		REQUIRE(body_case.find("distribParseBodyManifestForPrivateSlot(e->id") !=
		        std::string::npos);
		REQUIRE(body_case.find("model_file") == std::string::npos);
		REQUIRE(head_case.find("distribResolveHeadRuntimeSlot(") !=
		        std::string::npos);
		REQUIRE(head_case.find("e->runtime_index = e->ext.head.headnum") !=
		        std::string::npos);
		REQUIRE(head_case.find("distribSourceModelPathFromMeshArchive(mesh_full") !=
		        std::string::npos);
		REQUIRE(head_case.find("distribParseHeadManifestForPrivateSlot(e->id") !=
		        std::string::npos);
		REQUIRE(head_case.find("model_file") == std::string::npos);
	}
	REQUIRE(netdistrib.find("e->ext.gamemode.max_players") != std::string::npos);
	REQUIRE(netdistrib.find("e->ext.bot_profile.target_body") != std::string::npos);
	REQUIRE(netdistrib.find("const char *pf = iniGet(ini, \"material_file\",") != std::string::npos);
	REQUIRE(netdistrib.find("iniGet(ini, \"texture_archive\",\n                iniGet(ini, \"texture_file\"") ==
	        std::string::npos);
	REQUIRE(netdistrib.find("const char *pf = e->ext.theme.theme_file") != std::string::npos);
	REQUIRE(netdistrib.find("e->ext.theme.ui_archive[0] ?") == std::string::npos);
	REQUIRE(runtime.find("case ASSET_ARENA:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.arena.scenario_archive") != std::string::npos);
	REQUIRE(runtime.find("binding->name_langid = entry->ext.arena.name_langid") != std::string::npos);
	REQUIRE(runtime.find("binding->requirefeature = entry->ext.arena.requirefeature") != std::string::npos);
	const size_t runtime_binding_switch = runtime.find("switch (entry->type)");
	REQUIRE(runtime_binding_switch != std::string::npos);
	{
		const size_t arena_start =
			runtime.find("case ASSET_ARENA:", runtime_binding_switch);
		const size_t body_start = runtime.find("case ASSET_BODY:", arena_start);
		REQUIRE(arena_start != std::string::npos);
		REQUIRE(body_start != std::string::npos);
		const std::string arena_case =
			runtime.substr(arena_start, body_start - arena_start);
		REQUIRE(arena_case.find("s_hasAnyFile(binding->authored_file, NULL, NULL, NULL)") !=
		        std::string::npos);
		REQUIRE(arena_case.find("binding->primary_path") == std::string::npos);
	}
	REQUIRE(runtime.find("case ASSET_BODY:") != std::string::npos);
	REQUIRE(runtime.find("case ASSET_HEAD:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.body.mesh_archive") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.body.hand_archive") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.body.display_name") != std::string::npos);
	REQUIRE(runtime.find("binding->name_langid = entry->ext.body.name_langid") != std::string::npos);
	REQUIRE(runtime.find("binding->requirefeature = entry->ext.body.requirefeature") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.head.mesh_archive") != std::string::npos);
	REQUIRE(runtime.find("binding->requirefeature = entry->ext.head.requirefeature") != std::string::npos);
	{
		const size_t character_start =
			runtime.find("case ASSET_CHARACTER:", runtime_binding_switch);
		const size_t skin_start = runtime.find("case ASSET_SKIN:",
			character_start);
		REQUIRE(character_start != std::string::npos);
		REQUIRE(skin_start != std::string::npos);
		const std::string character_case =
			runtime.substr(character_start, skin_start - character_start);
		REQUIRE(character_case.find("entry->ext.character.bodyfile") !=
		        std::string::npos);
		REQUIRE(character_case.find("entry->ext.character.portrait_file") !=
		        std::string::npos);
		REQUIRE(character_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(character_case.find("s_hasAnyFile(binding->authored_file, NULL, NULL, NULL)") !=
		        std::string::npos);
	}
	{
		const size_t body_start =
			runtime.find("case ASSET_BODY:", runtime_binding_switch);
		const size_t head_start = runtime.find("case ASSET_HEAD:",
			body_start);
		REQUIRE(body_start != std::string::npos);
		REQUIRE(head_start != std::string::npos);
		const std::string body_case =
			runtime.substr(body_start, head_start - body_start);
		REQUIRE(body_case.find("entry->ext.body.mesh_archive") !=
		        std::string::npos);
		REQUIRE(body_case.find("entry->ext.body.hand_archive") !=
		        std::string::npos);
		REQUIRE(body_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(body_case.find("s_hasAnyFile(binding->authored_file, NULL, NULL, NULL)") !=
		        std::string::npos);
	}
	{
		const size_t head_start =
			runtime.find("case ASSET_HEAD:", runtime_binding_switch);
		const size_t character_start = runtime.find("case ASSET_CHARACTER:",
			head_start);
		REQUIRE(head_start != std::string::npos);
		REQUIRE(character_start != std::string::npos);
		const std::string head_case =
			runtime.substr(head_start, character_start - head_start);
		REQUIRE(head_case.find("entry->ext.head.mesh_archive") !=
		        std::string::npos);
		REQUIRE(head_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(head_case.find("s_hasAnyFile(binding->authored_file, NULL, NULL, NULL)") !=
		        std::string::npos);
	}
	REQUIRE(runtime.find("case ASSET_GAMEMODE:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.gamemode.rules_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.gamemode.name") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.gamemode.description") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.gamemode.min_players") != std::string::npos);
	REQUIRE(runtime.find("binding->gamemode_max_players = entry->ext.gamemode.max_players") != std::string::npos);
	REQUIRE(runtime.find("binding->gamemode_requirefeature = entry->ext.gamemode.requirefeature") != std::string::npos);
	REQUIRE(runtime.find("case ASSET_BOT_PROFILE:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.bot_profile.profile_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.bot_profile.target_body") != std::string::npos);
	REQUIRE(runtime.find("binding->bot_profile_name_langid = entry->ext.bot_profile.name_langid") != std::string::npos);
	REQUIRE(runtime.find("binding->bot_profile_requirefeature = entry->ext.bot_profile.requirefeature") != std::string::npos);
	{
		const size_t gamemode_start =
			runtime.find("case ASSET_GAMEMODE:", runtime_binding_switch);
		const size_t bot_start = runtime.find("case ASSET_BOT_PROFILE:",
			gamemode_start);
		REQUIRE(gamemode_start != std::string::npos);
		REQUIRE(bot_start != std::string::npos);
		const std::string gamemode_case =
			runtime.substr(gamemode_start, bot_start - gamemode_start);
		REQUIRE(gamemode_case.find("entry->ext.gamemode.rules_file") !=
		        std::string::npos);
		REQUIRE(gamemode_case.find("binding->primary_path") ==
		        std::string::npos);
	}
	{
		const size_t bot_start =
			runtime.find("case ASSET_BOT_PROFILE:", runtime_binding_switch);
		const size_t hud_start = runtime.find("case ASSET_HUD:", bot_start);
		REQUIRE(bot_start != std::string::npos);
		REQUIRE(hud_start != std::string::npos);
		const std::string bot_case =
			runtime.substr(bot_start, hud_start - bot_start);
		REQUIRE(bot_case.find("entry->ext.bot_profile.profile_file") !=
		        std::string::npos);
		REQUIRE(bot_case.find("binding->primary_path") ==
		        std::string::npos);
	}
	REQUIRE(runtime.find("entry->ext.effect.effect_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.effect.timeline_file") != std::string::npos);
	REQUIRE(runtime.find("binding->dependency_a, binding->shader_id") ==
	        std::string::npos);
	{
		const size_t effect_start =
			runtime.find("case ASSET_EFFECT:", runtime_binding_switch);
		const size_t vehicle_start = runtime.find("case ASSET_VEHICLE:",
			effect_start);
		REQUIRE(effect_start != std::string::npos);
		REQUIRE(vehicle_start != std::string::npos);
		const std::string effect_case =
			runtime.substr(effect_start, vehicle_start - effect_start);
		REQUIRE(effect_case.find("entry->ext.effect.effect_file") !=
		        std::string::npos);
		REQUIRE(effect_case.find("entry->ext.effect.timeline_file") !=
		        std::string::npos);
		REQUIRE(effect_case.find("binding->primary_path") ==
		        std::string::npos);
	}
	{
		const size_t vehicle_start =
			runtime.find("case ASSET_VEHICLE:", runtime_binding_switch);
		const size_t prop_start = runtime.find("case ASSET_PROP:",
			vehicle_start);
		REQUIRE(vehicle_start != std::string::npos);
		REQUIRE(prop_start != std::string::npos);
		const std::string vehicle_case =
			runtime.substr(vehicle_start, prop_start - vehicle_start);
		REQUIRE(vehicle_case.find("entry->ext.vehicle.model_file") !=
		        std::string::npos);
		REQUIRE(vehicle_case.find("entry->ext.vehicle.physics_file") !=
		        std::string::npos);
		REQUIRE(vehicle_case.find("entry->ext.vehicle.behavior_graph") !=
		        std::string::npos);
		REQUIRE(vehicle_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(vehicle_case.find("s_hasText(binding->dependency_a)") !=
		        std::string::npos);
	}
	REQUIRE(runtime.find("case ASSET_SKIN:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.skin_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.material_archive") != std::string::npos);
	REQUIRE(runtime.find("s_hasAnyFile(binding->dependency_c, binding->dependency_d") ==
	        std::string::npos);
	REQUIRE(runtime.find("case ASSET_MATERIAL:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.material.material_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.material.texture_archive") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.material.effect_archive") != std::string::npos);
	REQUIRE(runtime.find("case ASSET_THEME:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.theme.theme_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.theme.ui_archive") != std::string::npos);
	REQUIRE(runtime.find("s_hasAnyFile(binding->authored_file, NULL, NULL, NULL)") !=
	        std::string::npos);
	REQUIRE(runtime.find("case ASSET_WEAPON:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.model_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.primary_graph") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.secondary_graph") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.shared_context") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.settings_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.variables_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.weapon_id") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.requirefeature") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.dual_wieldable") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.name") != std::string::npos);
	{
		const size_t weapon_start =
			runtime.find("case ASSET_WEAPON:", runtime_binding_switch);
		const size_t projectile_start = runtime.find("case ASSET_PROJECTILE:",
			weapon_start);
		REQUIRE(weapon_start != std::string::npos);
		REQUIRE(projectile_start != std::string::npos);
		const std::string weapon_case =
			runtime.substr(weapon_start, projectile_start - weapon_start);
		REQUIRE(weapon_case.find("entry->ext.weapon.primary_graph") !=
		        std::string::npos);
		REQUIRE(weapon_case.find("entry->ext.weapon.secondary_graph") !=
		        std::string::npos);
		REQUIRE(weapon_case.find("entry->ext.weapon.shared_context") !=
		        std::string::npos);
		REQUIRE(weapon_case.find("entry->ext.weapon.settings_file") !=
		        std::string::npos);
		REQUIRE(weapon_case.find("entry->ext.weapon.variables_file") !=
		        std::string::npos);
		REQUIRE(weapon_case.find("entry->ext.weapon.behavior_graph") ==
		        std::string::npos);
		REQUIRE(weapon_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(weapon_case.find("s_hasText(binding->authored_file)") !=
		        std::string::npos);
		REQUIRE(weapon_case.find("s_hasText(binding->dependency_d)") !=
		        std::string::npos);
	}
	REQUIRE(runtime.find("case ASSET_PROJECTILE:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.projectile.behavior_graph") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.projectile.model_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.projectile.entity_ref") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.projectile.name") != std::string::npos);
	{
		const size_t projectile_start =
			runtime.find("case ASSET_PROJECTILE:", runtime_binding_switch);
		const size_t entity_start = runtime.find("case ASSET_ENTITY:",
			projectile_start);
		REQUIRE(projectile_start != std::string::npos);
		REQUIRE(entity_start != std::string::npos);
		const std::string projectile_case =
			runtime.substr(projectile_start, entity_start - projectile_start);
		REQUIRE(projectile_case.find(": entry->ext.projectile.model_file") ==
		        std::string::npos);
		REQUIRE(projectile_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(projectile_case.find("s_hasAnyFile(binding->authored_file, NULL, NULL, NULL)") !=
		        std::string::npos);
	}
	REQUIRE(runtime.find("case ASSET_ENTITY:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.entity.behavior_graph") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.entity.model_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.entity.archetype") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.entity.name") != std::string::npos);
	{
		const size_t entity_start =
			runtime.find("case ASSET_ENTITY:", runtime_binding_switch);
		const size_t mission_start = runtime.find("case ASSET_MISSION:",
			entity_start);
		REQUIRE(entity_start != std::string::npos);
		REQUIRE(mission_start != std::string::npos);
		const std::string entity_case =
			runtime.substr(entity_start, mission_start - entity_start);
		REQUIRE(entity_case.find(": entry->ext.entity.model_file") ==
		        std::string::npos);
		REQUIRE(entity_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(entity_case.find("s_hasAnyFile(binding->authored_file, NULL, NULL, NULL)") !=
		        std::string::npos);
	}
	REQUIRE(runtime.find("entry->ext.prop.name") != std::string::npos);
	{
		const size_t prop_start =
			runtime.find("case ASSET_PROP:", runtime_binding_switch);
		const size_t weapon_start = runtime.find("case ASSET_WEAPON:",
			prop_start);
		REQUIRE(prop_start != std::string::npos);
		REQUIRE(weapon_start != std::string::npos);
		const std::string prop_case =
			runtime.substr(prop_start, weapon_start - prop_start);
		REQUIRE(prop_case.find("entry->ext.prop.model_file") !=
		        std::string::npos);
		REQUIRE(prop_case.find("entry->ext.prop.prop_file") !=
		        std::string::npos);
		REQUIRE(prop_case.find("entry->ext.prop.behavior_graph") !=
		        std::string::npos);
		REQUIRE(prop_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(prop_case.find("s_hasAnyFile(binding->authored_file, NULL, NULL, NULL)") !=
		        std::string::npos);
	}
	REQUIRE(runtime.find("case ASSET_LANG:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.lang.strings_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.lang.locale") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.lang.lang_category") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.lang.string_count") != std::string::npos);

	const std::string scenario = readTextFile("port/src/loader_walker_scenario.c");
	REQUIRE(scenario.find("\"runtime_source\"") != std::string::npos);
	REQUIRE(scenario.find("\"scene\"") != std::string::npos);
	REQUIRE(scenario.find("e->ext.scenario.scene_file") != std::string::npos);
	REQUIRE(scenario.find("e->ext.scenario.level_graph_file") !=
	        std::string::npos);

	const std::string mesh = readTextFile("port/src/loader_walker_mesh.c");
	REQUIRE(mesh.find("\"geometry\"") != std::string::npos);
	REQUIRE(mesh.find("\"model.obj\"") != std::string::npos);
	REQUIRE(mesh.find("\"source_filenum_symbol\"") != std::string::npos);
	REQUIRE(mesh.find("loaderEnumResolveFileEnum(source_symbol, -1)") !=
	        std::string::npos);
	REQUIRE(mesh.find("e->source_filenum = source_filenum") !=
	        std::string::npos);
	const std::string pdmesh_extract = readTextFile("port/src/romextract_pdmesh.c");
	REQUIRE(pdmesh_extract.find("skeleton_symbol") !=
	        std::string::npos);
	REQUIRE(pdmesh_extract.find("\\\"source_filenum_symbol\\\"") !=
	        std::string::npos);
	REQUIRE(pdmesh_extract.find("source_filenum_symbol = %s") ==
	        std::string::npos);
	REQUIRE(pdmesh_extract.find("modAssetCompilerSkeletonSymbolForPointer") !=
	        std::string::npos);
	REQUIRE(pdmesh_extract.find("g_CartFileNums") != std::string::npos);
	REQUIRE(pdmesh_extract.find("base:model_cartridge_rifle") !=
	        std::string::npos);
	REQUIRE(pdmesh_extract.find("FILE_GHUDPIECE") !=
	        std::string::npos);

	const std::string body = readTextFile("port/src/loader_walker_body.c");
	const std::string head = readTextFile("port/src/loader_walker_head.c");
	REQUIRE(body.find("\"mesh_archive\"") != std::string::npos);
	REQUIRE(body.find("\"hand_archive\"") != std::string::npos);
	REQUIRE(body.find("\"mesh.pdmesh\"") != std::string::npos);
	REQUIRE(body.find("mesh_archive_path") != std::string::npos);
	REQUIRE(body.find("\"%s::model.obj\"") != std::string::npos);
	REQUIRE(body.find("s_bindBodyHandModelSource") != std::string::npos);
	REQUIRE(body.find("loaderEnumResolveFileEnum(symbol, -1)") !=
	        std::string::npos);
	REQUIRE(body.find("catalogReadableModelIdForFile(hand_filenum, \"hand\", \"hand\"") !=
	        std::string::npos);
	REQUIRE(body.find("catalogSetPrimaryFile(hand_entry, hand_source_path)") !=
	        std::string::npos);
	REQUIRE(head.find("\"mesh_archive\"") != std::string::npos);
	REQUIRE(head.find("\"mesh.pdmesh\"") != std::string::npos);
	REQUIRE(head.find("mesh_archive_path") != std::string::npos);
	REQUIRE(head.find("\"%s::model.obj\"") != std::string::npos);

	REQUIRE(body_mgr.find("catalogLoadTypedAsset(ASSET_BODY, id)") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("s_Bodies[bodynum].modeldef = catalogGetLoadedModeldef(id)") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("catalogLoadTypedAsset(ASSET_HEAD, id)") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("s_Heads[headnum].modeldef = catalogGetLoadedModeldef(id)") !=
	        std::string::npos);
	REQUIRE(load.find("s_catalogModelPayloadSourcePath") != std::string::npos);
	REQUIRE(load.find("\"model.obj\"") != std::string::npos);
	REQUIRE(load.find("\"model.gltf\"") != std::string::npos);
	REQUIRE(load.find("\"model.glb\"") != std::string::npos);
	REQUIRE(load.find("fsFileSize(candidate) > 0") != std::string::npos);
	REQUIRE(load.find("s_catalogModelPayloadSourcePath(entry, source_path") <
	        load.find("modeldefLoadToNewFromHandle(handle"));
	const std::string modeldef_loader = readTextFile("src/game/modeldef.c");
	REQUIRE(modeldef_loader.find("modeldefResolveExternalSourcePath") != std::string::npos);
	REQUIRE(modeldef_loader.find("\"model.obj\"") != std::string::npos);
	REQUIRE(modeldef_loader.find("\"model.gltf\"") != std::string::npos);
	REQUIRE(modeldef_loader.find("\"model.glb\"") != std::string::npos);
	REQUIRE(modeldef_loader.find("entry->ext.weapon.model_file") != std::string::npos);
	REQUIRE(modeldef_loader.find("entry->ext.prop.model_file") != std::string::npos);
	REQUIRE(modeldef_loader.find("entry->ext.vehicle.model_file") != std::string::npos);
	REQUIRE(modeldef_loader.find("modAssetCompilerBuildModeldef(entry, resolved_source_path") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("catalogManagerBodyModeldefPassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("body manager modeldef fallback") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("assetSourceDebugFatalHandleFallback(ASSET_BODY") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("assetSourceDebugHandleRequiresPublicFileSource(ASSET_BODY, handle)") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("catalogManagerBodyModeldefPassesSourceOnlyCheck(bodynum, id") <
	        body_mgr.find("modeldefLoadToNewFromHandle(handle"));
	REQUIRE(head_mgr.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("catalogManagerHeadModeldefPassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("head manager modeldef fallback") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("assetSourceDebugFatalHandleFallback(ASSET_HEAD") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("assetSourceDebugHandleRequiresPublicFileSource(ASSET_HEAD, handle)") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("catalogManagerHeadModeldefPassesSourceOnlyCheck(headnum, id") <
	        head_mgr.find("modeldefLoadToNewFromHandle(handle"));
	REQUIRE(player.find("bodyresult.handle.provider == fileProvider()") !=
	        std::string::npos);
	REQUIRE(player.find("headresult.handle.provider == fileProvider()") !=
	        std::string::npos);
	REQUIRE(player.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(player.find("assetSourceDebugFatalHandleFallback(ASSET_BODY") !=
	        std::string::npos);
	REQUIRE(player.find("assetSourceDebugFatalHandleFallback(ASSET_HEAD") !=
	        std::string::npos);
	REQUIRE(player.find("playerFatalSourceOnlyCharacterAssetFailure(ASSET_BODY") !=
	        std::string::npos);
	REQUIRE(player.find("playerFatalSourceOnlyCharacterAssetFailure(ASSET_HEAD") !=
	        std::string::npos);
	REQUIRE(player.find("refusing player fallback/substitution") !=
	        std::string::npos);
	REQUIRE(player.find("catalogGetBodyModeldef(bodynum)") !=
	        std::string::npos);
	REQUIRE(player.find("catalogGetHeadModeldef(headnum)") !=
	        std::string::npos);
	REQUIRE(player.find("public_source_generated_body") !=
	        std::string::npos);
	REQUIRE(player.find("catalogGetLoadedModeldef(multi_body_id) == bodymodeldef") !=
	        std::string::npos);
	REQUIRE(player.find("bodymodeldef->numparts == 0") ==
	        std::string::npos);
	REQUIRE(player.find("weapon_model_file_source_1p") != std::string::npos);
	REQUIRE(player.find("player chrbody weapon modeldef") !=
	        std::string::npos);
	REQUIRE(player.find("assetSourceDebugFatalHandleFallback(ASSET_MODEL") !=
	        std::string::npos);
	REQUIRE(player.find("assetSourceDebugHandleRequiresPublicFileSource(ASSET_MODEL, weapon_handle)") !=
	        std::string::npos);
	REQUIRE(player.find("catalogLoadTypedAsset(ASSET_MODEL, weapon_model_id_1p)") !=
	        std::string::npos);
	REQUIRE(menu.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(menu.find("menuModelHandlePassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(menu.find("menu body preview modeldef") !=
	        std::string::npos);
	REQUIRE(menu.find("menu head preview modeldef") !=
	        std::string::npos);
	REQUIRE(menu.find("menu raw model preview") !=
	        std::string::npos);
	REQUIRE(menu.find("menuModelHandlePassesSourceOnlyCheck(ASSET_BODY") !=
	        std::string::npos);
	REQUIRE(menu.find("menuModelHandlePassesSourceOnlyCheck(ASSET_HEAD") !=
	        std::string::npos);
	REQUIRE(menu.find("menuModelHandlePassesSourceOnlyCheck(ASSET_MODEL") !=
	        std::string::npos);
	REQUIRE(mainmenu.find("MENUMODELPARAMS_SET_FILENUM(weaponGetFileNum(weaponnum))") ==
	        std::string::npos);
	REQUIRE(mainmenu.find("weapon menu preview weaponnum=%d has no provider-backed catalog entry") !=
	        std::string::npos);
	REQUIRE(mplayer_setup.find("MENUMODELPARAMS_SET_FILENUM(catalogGetHeadFilenumByIndex(headnum))") ==
	        std::string::npos);
	REQUIRE(mplayer_setup.find("MP head preview headnum=%d has no provider-backed catalog entry") !=
	        std::string::npos);
	REQUIRE(title.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(title.find("title model size") !=
	        std::string::npos);
	REQUIRE(title.find("assetSourceDebugFatalHandleFallback(ASSET_MODEL") !=
	        std::string::npos);
	REQUIRE(title.find("assetSourceDebugHandleRequiresPublicFileSource(ASSET_MODEL, modelresult.handle)") !=
	        std::string::npos);
	REQUIRE(modelcatalog.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(modelcatalog.find("catalogValidatePassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(modelcatalog.find("modelcatalog body validation modeldef") !=
	        std::string::npos);
	REQUIRE(modelcatalog.find("modelcatalog head validation modeldef") !=
	        std::string::npos);
	REQUIRE(modelcatalog.find("assetSourceDebugFatalHandleFallback(type, context, id, handle)") !=
	        std::string::npos);
	REQUIRE(modelcatalog.find("catalogValidateSourceMissing(handle, filenum)") >
	        modelcatalog.find("catalogValidatePassesSourceOnlyCheck(index, ce->category, filenum, handle)"));
	REQUIRE(forge_runtime.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(forge_runtime.find("s_forgeModelHandlePassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(forge_runtime.find("forge door modeldef") !=
	        std::string::npos);
	REQUIRE(forge_runtime.find("forge weapon pad modeldef") !=
	        std::string::npos);
	REQUIRE(forge_runtime.find("forge prop modeldef") !=
	        std::string::npos);
	REQUIRE(forge_runtime.find("assetSourceDebugFatalHandleFallback(type, context, catalog_id, handle)") !=
	        std::string::npos);
	REQUIRE(forge_runtime.find("|| pr.filenum <= 0") == std::string::npos);
	REQUIRE(forge_runtime.find("|| wr.filenum <= 0") == std::string::npos);
	REQUIRE(forge_runtime.find("assetHandleIsNull(pr.handle)") != std::string::npos);
	REQUIRE(forge_runtime.find("assetHandleIsNull(wr.handle)") != std::string::npos);
	REQUIRE(setuputils.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(setuputils.find("setupModelHandlePassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(setuputils.find("setup modeldef") !=
	        std::string::npos);
	REQUIRE(setuputils.find("assetSourceDebugFatalHandleFallback(ASSET_MODEL") !=
	        std::string::npos);
	REQUIRE(setuputils.find("assetSourceDebugHandleRequiresPublicFileSource(ASSET_MODEL, handle)") !=
	        std::string::npos);
	REQUIRE(setuputils.find("setupModelHandlePassesSourceOnlyCheck(modelnum, model_id, model_handle)") <
	        setuputils.find("modeldefLoadToNewFromHandle(model_handle, source_filenum)"));
	const std::string bondgun = readTextFile("src/game/bondgun.c");
	REQUIRE(bondgun.find("#include \"assetcatalog.h\"") !=
	        std::string::npos);
	REQUIRE(bondgun.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(bondgun.find("#include \"modasset_compiler.h\"") !=
	        std::string::npos);
	REQUIRE(bondgun.find("fileProviderPath(player->gunctrl.loadhandle)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("catalogHandleByModelSourceFilenum(ASSET_NONE, filenum)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("fileProviderHandle") == std::string::npos);
	REQUIRE(bondgun.find("bgunQueuedLoadPassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(bondgun.find("assetSourceDebugFatalHandleFallback(ASSET_MODEL") !=
	        std::string::npos);
	REQUIRE(bondgun.find("assetSourceDebugHandleRequiresPublicFileSource(ASSET_MODEL") !=
	        std::string::npos);
	REQUIRE(bondgun.find("weapon model inflated-size") !=
	        std::string::npos);
	REQUIRE(bondgun.find("weapon model loaded-size") !=
	        std::string::npos);
	REQUIRE(bondgun.find("weapon model load-to-addr") !=
	        std::string::npos);
	REQUIRE(bondgun.find("bgunResolveCatalogModelSourcePath(") !=
	        std::string::npos);
	REQUIRE(bondgun.find("fsFileSize(candidate) > 0") !=
	        std::string::npos);
	REQUIRE(bondgun.find("catalogIdBySourceHandle(ASSET_MODEL, player->gunctrl.loadhandle)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("catalogLoadTypedAsset(ASSET_MODEL, model_id)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("catalogGetLoadedModeldef(model_id)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("BONDGUN.SOURCE: loaded catalog model source") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_bondgun_model_source_only_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("BONDGUN_MODEL_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_model_handle_source_only_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("MODEL_HANDLE_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
	REQUIRE(guard.find("menu raw model preview") !=
	        std::string::npos);
	REQUIRE(guard.find("title model size") !=
	        std::string::npos);
	REQUIRE(guard.find("player chrbody weapon modeldef") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_modelcatalog_source_only_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("MODELCATALOG_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
	REQUIRE(guard.find("modelcatalog body validation modeldef") !=
	        std::string::npos);
	REQUIRE(guard.find("source-only check must precede catalogValidateSourceMissing") !=
	        std::string::npos);
	REQUIRE(guard.find("CATALOG_METADATA_PRELOAD_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_catalog_metadata_preload_source_only_guard(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("catalog bundled metadata preloads must enforce source-only public sources") !=
	        std::string::npos);
	REQUIRE(guard.find("FORGE_RUNTIME_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_forge_runtime_source_only_guards(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("Forge runtime model spawns must enforce source-only public family handles") !=
	        std::string::npos);
	REQUIRE(guard.find("SETUP_MODELDEF_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_setup_modeldef_source_only_guard(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("setup modeldef loads must enforce source-only public model handles") !=
	        std::string::npos);
	REQUIRE(guard.find("CHARACTER_MANAGER_MODELDEF_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_character_manager_modeldef_source_only_guards(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("body/head manager modeldef fallbacks must enforce source-only public") !=
	        std::string::npos);
	REQUIRE(guard.find("weapon model load-to-addr") !=
	        std::string::npos);
	REQUIRE(guard.find("assetSourceDebugFatalHandleFallback(ASSET_MODEL") !=
	        std::string::npos);
	REQUIRE(modeldef.find("#include \"assetcatalog.h\"") !=
	        std::string::npos);
	REQUIRE(modeldef.find("#include \"modasset_compiler.h\"") !=
	        std::string::npos);
	REQUIRE(modeldef.find("modeldefLoadExternalCatalogSource") !=
	        std::string::npos);
	REQUIRE(modeldef.find("modeldefExternalCatalogSourceHandle") !=
	        std::string::npos);
	REQUIRE(modeldef.find("catalogHandleByModelSourceFilenum(ASSET_NONE, source_filenum)") !=
	        std::string::npos);
	REQUIRE(modeldef.find("handle = modeldefCatalogModelSourceHandle((s32)fileid)") !=
	        std::string::npos);
	REQUIRE(modeldef.find("handle = external_handle") !=
	        std::string::npos);
	REQUIRE(modeldef.find("fileProviderHandle") == std::string::npos);
	REQUIRE(modeldef.find("modeldefRefuseRomSource") !=
	        std::string::npos);
	REQUIRE(modeldef.find("ROM fallback refused") !=
	        std::string::npos);
	REQUIRE(modeldef.find("assetLoadRomToNew") == std::string::npos);
	REQUIRE(modeldef.find("assetLoadRomToAddr") == std::string::npos);
	REQUIRE(modeldef.find("modAssetCompilerIsExternalSource(modeldefCatalogSourcePath(handle))") !=
	        std::string::npos);
	REQUIRE(modeldef.find("catalogIdBySourceHandle(model_payload_types[i], handle)") !=
	        std::string::npos);
	REQUIRE(modeldef.find("modeldefTypeUsesLoadedModelPayload(model_type)") !=
	        std::string::npos);
	REQUIRE(modeldef.find("catalogLoadTypedAsset(model_type, entry->id)") !=
	        std::string::npos);
	REQUIRE(modeldef.find("modAssetCompilerBuildModeldef(entry, resolved_source_path") !=
	        std::string::npos);
	REQUIRE(modeldef.find("MODELDEF.SOURCE: loaded catalog model source") !=
	        std::string::npos);
	REQUIRE(modeldef.find("i < ARRAYCOUNT(g_Skeletons)") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("public_source_static_modeldef") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("public_source_generated_modeldef") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("bodyFatalSourceOnlyCharacterAssetFailure(ASSET_BODY") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("bodyFatalSourceOnlyCharacterAssetFailure(ASSET_HEAD") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("refusing body fallback/substitution") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("catalogGetLoadedModeldef(body_source_id) == bodymodeldef") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("modelmgrInstantiateModelWithoutAnim(bodymodeldef)") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("modelmgrInstantiateModelWithAnim(bodymodeldef)") !=
	        std::string::npos);
	const std::string modasset_compiler = readTextFile("port/src/modasset_compiler.c");
	const std::string modasset_compiler_h = readTextFile("port/include/modasset_compiler.h");
	REQUIRE(modasset_compiler_h.find("#define MODASSET_COMPILER_VERSION 7") !=
	        std::string::npos);
	REQUIRE(modasset_compiler_h.find("#define MODASSET_COMPILER_MODELDEF_VERSION 9") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("modAssetCompilerSkeletonForSymbol") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("modAssetCompilerSkeletonSymbolForPointer") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("{ \"SKEL_HEAD\", NULL, SKEL_HEAD }") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("token < 0x10000") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("modAssetCompilerSkeletonSymbolForId((s16)token)") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefSkeletonFromMetadata") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefReadHierarchy") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("buildGeneratedModeldefFromMeshHierarchy") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("\"model.nodes.json\"") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("\"model.parts.json\"") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("\"model.faces.json\"") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("\"model.nodes.tsv\"") == std::string::npos);
	REQUIRE(modasset_compiler.find("\"model.parts.tsv\"") == std::string::npos);
	REQUIRE(modasset_compiler.find("\"model.faces.tsv\"") == std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefReadParts") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefReadFaces") !=
	        std::string::npos);
	REQUIRE(pdmesh_extract.find("distance_near") != std::string::npos);
	REQUIRE(pdmesh_extract.find("reorder_target_a") != std::string::npos);
	REQUIRE(modasset_compiler.find("MODELNODETYPE_DISTANCE") != std::string::npos);
	REQUIRE(modasset_compiler.find("rodata->distance.near = row->distance_near") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->dynamic_rodatas[i].reorder.unk18") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->dynamic_rodatas[i].reorder.unk1c") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("if (parts_rc <= 0)") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("tri->matrix_index") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("tri->matrix_index >= 0 ? tri->matrix_index") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("\"_meta/manifest.json\"") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("\"mesh.ini\"") != std::string::npos);
	REQUIRE(pdmesh_extract.find("model_scale = %.9g") != std::string::npos);
	REQUIRE(pdmesh_extract.find("\\\"model_scale\\\": %.9g") != std::string::npos);
	REQUIRE(pdmesh_extract.find("faces_file = model.faces.json") != std::string::npos);
	REQUIRE(pdmesh_extract.find("\\\"faces\\\": \\\"model.faces.json\\\"") != std::string::npos);
	REQUIRE(pdmesh_extract.find("faces_file = model.faces.tsv") == std::string::npos);
	REQUIRE(pdmesh_extract.find("\\\"faces\\\": \\\"model.faces.tsv\\\"") == std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefScaleFromMetadata") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.scale = generatedModeldefScaleFromMetadata(source_path)") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("strstr(sep + 2, \"::\")") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->root_rodata.position.part = skeleton ? 0 : 0xffff") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.skel = skeleton") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefNeedsChrRoot") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("entry->type == ASSET_BODY") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("skeleton = chr_root ? &g_SkelChr") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("if (chr_root && i == 0)") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("node_type = MODELNODETYPE_CHRINFO") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("strcmp(group_name, \"-\") == 0") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefConfigureSourceBounds") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->root_node.type == MODELNODETYPE_CHRINFO") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->bbox_node.type = MODELNODETYPE_BBOX") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefMeshBounds(mesh, &owner->bbox_rodata.bbox)") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->root_node.child = &owner->bbox_node") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefConfigureCctvParts") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.skel != &g_SkelCctv") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_CCTV_CASING") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_CCTV_LENS") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_CCTV_0002") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_CCTV_0003") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.parts = owner->cctv_part_table.nodes") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.nummatrices = 2") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefConfigureAutogunParts") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.skel != &g_SkelAutogun") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_AUTOGUN_0001") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_AUTOGUN_0002") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->toggle_node.type = MODELNODETYPE_POSITION") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("autogun_part_table") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("struct modelnode *nodes[2]") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("s16 partnums[2]") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.parts = owner->autogun_part_table.nodes") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.nummatrices = 3") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefConfigureWindowedDoorParts") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.skel != &g_SkelWindowedDoor") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_WINDOWEDDOOR_0000") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_WINDOWEDDOOR_0001") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_WINDOWEDDOOR_0002") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->toggle_rodata.toggle.target = NULL") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.parts = owner->windowed_door_part_table.nodes") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.numparts = 3") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefConfigureLogoParts") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.skel != &g_SkelLogo") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.skel != &g_SkelPdLogo") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_LOGO_FRONTSIDE") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_LOGO_0003") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("struct modelnode *nodes[4]") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.parts = owner->logo_part_table.nodes") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.numparts = 4") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("vertex_colour_bytes") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("ALIGN8(vertex_bytes)") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->colours = (Col *)((u8 *)owner->vertices + ALIGN8(vertex_bytes))") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("free(owner->colours)") ==
	        std::string::npos);
	REQUIRE(chraction.find("chr->model == NULL || chr->model->anim == NULL") !=
	        std::string::npos);
	REQUIRE(chraction.find("static model has no animation controller") !=
	        std::string::npos);
	const std::string chr_runtime = readTextFile("src/game/chr.c");
	REQUIRE(chr_runtime.find("bool model_has_anim") != std::string::npos);
	REQUIRE(chr_runtime.find("if (!model_has_anim)") != std::string::npos);
	REQUIRE(chr_runtime.find("!model_has_anim || chr->actiontype != ACT_STAND") !=
	        std::string::npos);
	REQUIRE(setup.find("setupResolvePointerInLoadedSetup") !=
	        std::string::npos);
	REQUIRE(setup.find("invalid path pads pointer") != std::string::npos);

	const std::string sfx = readTextFile("port/src/loader_walker_sfx.c");
	const std::string voice = readTextFile("port/src/loader_walker_voice.c");
	const std::string song = readTextFile("port/src/loader_walker_song.c");
	const std::string font = readTextFile("port/src/loader_walker_font.c");
	REQUIRE(sfx.find("\"sample.wav\"") != std::string::npos);
	REQUIRE(sfx.find("always_invoke = 1") != std::string::npos);
	REQUIRE(sfx.find("e->source_soundnum") != std::string::npos);
	REQUIRE(sfx.find("loaderWalkerEnvelopeInt(manifest, manifest_len, \"sample_rate_hz\"") !=
	        std::string::npos);
	REQUIRE(sfx.find("loaderWalkerEnvelopeInt(manifest, manifest_len, \"decoded_sample_count\"") !=
	        std::string::npos);
	REQUIRE(sfx.find("(decoded_sample_count * 1000 + sample_rate_hz / 2) / sample_rate_hz") !=
	        std::string::npos);
	REQUIRE(voice.find("\"sample.wav\"") != std::string::npos);
	REQUIRE(voice.find("always_invoke = 1") != std::string::npos);
	REQUIRE(voice.find("e->source_soundnum") != std::string::npos);
	REQUIRE(voice.find("loaderWalkerEnvelopeInt(manifest, manifest_len, \"sample_rate_hz\"") !=
	        std::string::npos);
	REQUIRE(voice.find("loaderWalkerEnvelopeInt(manifest, manifest_len, \"decoded_sample_count\"") !=
	        std::string::npos);
	REQUIRE(voice.find("(decoded_sample_count * 1000 + sample_rate_hz / 2) / sample_rate_hz") !=
	        std::string::npos);
	REQUIRE(song.find("\"sequence.mid\"") != std::string::npos);
	REQUIRE(song.find("always_invoke = 1") != std::string::npos);
	REQUIRE(song.find("assetCatalogResolve(id)") != std::string::npos);
	REQUIRE(song.find("duration_ms = existing->ext.audio.duration_ms") !=
	        std::string::npos);
	REQUIRE(font.find("e->ext.font.font_file") != std::string::npos);
	REQUIRE(font.find("e->ext.font.metrics_file") != std::string::npos);

	const std::string lang = readTextFile("port/src/loader_walker_lang.c");
	REQUIRE(lang.find("\"source_bank\"") != std::string::npos);
	REQUIRE(lang.find("\"string_count\"") != std::string::npos);
	REQUIRE(lang.find("\"locale\"") != std::string::npos);
	REQUIRE(lang.find("\"category\"") != std::string::npos);
	REQUIRE(lang.find("e->ext.lang.bank_id") != std::string::npos);
	REQUIRE(lang.find("e->ext.lang.locale") != std::string::npos);
	REQUIRE(lang.find("e->ext.lang.lang_category") != std::string::npos);
	REQUIRE(lang.find("e->ext.lang.string_count") != std::string::npos);
	REQUIRE(lang.find("e->ext.lang.strings_file") != std::string::npos);
}

TEST_CASE("source-generated windowed doors guard missing legacy toggle nodes",
          "[modding][pdxxx][c3844][source][static][b389]") {
	const std::string propobj = readTextFile("src/game/propobj.c");

	REQUIRE(propobj.find("#include \"model_rodata_guard.h\"") !=
	        std::string::npos);
	REQUIRE(propobj.find("doorGetWindowedDoorToggleRwData") !=
	        std::string::npos);
	REQUIRE(propobj.find("modelGetPart(model->definition, MODELPART_WINDOWEDDOOR_0001)") !=
	        std::string::npos);
	REQUIRE(propobj.find("modelRodataIsReadable(node->rodata") !=
	        std::string::npos);
	REQUIRE(propobj.find("WindowedDoor.portal-toggle") !=
	        std::string::npos);
	REQUIRE(propobj.find("WindowedDoor.destroy-toggle") !=
	        std::string::npos);
	REQUIRE(propobj.find("if (!rwdata || !rwdata->toggle.visible)") !=
	        std::string::npos);
}

TEST_CASE("custom body/head assembly chain stays source-backed through the private slot bridge",
          "[modding][pdxxx][c3844][source][static][bodyhead]") {
	const std::string body_walker = readTextFile("port/src/loader_walker_body.c");
	const std::string head_walker = readTextFile("port/src/loader_walker_head.c");
	const std::string api = readTextFile("port/src/assetcatalog_api.c");
	const std::string catalog = readTextFile("port/src/assetcatalog.c");
	const std::string body_mgr = readTextFile("port/src/catalog_mgr_bodies.c");
	const std::string head_mgr = readTextFile("port/src/catalog_mgr_heads.c");
	const std::string body_runtime = readTextFile("src/game/body.c");
	const std::string modmgr = readTextFile("port/src/modmgr.c");
	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	const std::string netdistrib = readTextFile("port/src/net/netdistrib.c");
	const std::string main_c = readTextFile("port/src/main.c");
	const std::string types = readTextFile("src/include/types.h");

	/* Custom archives with no legacy bodynum/headnum must allocate a private
	 * catalog-owned slot before catalog registration, then parse public source
	 * into that exact slot. Otherwise body0f02ce8c can only see an unbound
	 * integer and either fails or silently renders the wrong base body. */
	REQUIRE(body_walker.find("assetCatalogResolveBodyPrivateSlot(id)") !=
	        std::string::npos);
	REQUIRE(head_walker.find("assetCatalogResolveHeadPrivateSlot(id)") !=
	        std::string::npos);
	requireTokenOrder(body_walker,
		"assetCatalogResolveBodyPrivateSlot(id)",
		"assetCatalogRegisterBody(");
	requireTokenOrder(head_walker,
		"assetCatalogResolveHeadPrivateSlot(id)",
		"assetCatalogRegisterHead(");
	REQUIRE(body_walker.find("e->runtime_index = (s32)bodynum") !=
	        std::string::npos);
	REQUIRE(head_walker.find("e->runtime_index = (s32)headnum") !=
	        std::string::npos);
	REQUIRE(body_walker.find("catalogSetPrimaryFile(e, source_path)") !=
	        std::string::npos);
	REQUIRE(head_walker.find("catalogSetPrimaryFile(e, source_path)") !=
	        std::string::npos);
	REQUIRE(body_walker.find("loaderPoolParseBodyJsonForSlot(manifest, manifest_len, (s32)bodynum)") !=
	        std::string::npos);
	REQUIRE(head_walker.find("loaderPoolParseHeadJsonForSlot(manifest, manifest_len, (s32)headnum)") !=
	        std::string::npos);
	requireTokenOrder(body_walker,
		"e->runtime_index = (s32)bodynum",
		"loaderPoolParseBodyJsonForSlot(manifest, manifest_len, (s32)bodynum)");
	requireTokenOrder(head_walker,
		"e->runtime_index = (s32)headnum",
		"loaderPoolParseHeadJsonForSlot(manifest, manifest_len, (s32)headnum)");

	/* Direct typed registration is also a custom-body path: a later
	 * assetCatalogRegisterBody/Head call must not wipe runtime_index back to
	 * -1 or keep an authored -1 in the selector row. */
	REQUIRE(catalog.find("s_resolveBodyRegistrationSlot") !=
	        std::string::npos);
	REQUIRE(catalog.find("s_resolveHeadRegistrationSlot") !=
	        std::string::npos);
	REQUIRE(catalog.find("assetCatalogResolveBodyPrivateSlot(id)") !=
	        std::string::npos);
	REQUIRE(catalog.find("assetCatalogResolveHeadPrivateSlot(id)") !=
	        std::string::npos);
	REQUIRE(catalog.find("entry->runtime_index = resolved_bodynum") !=
	        std::string::npos);
	REQUIRE(catalog.find("entry->runtime_index = resolved_headnum") !=
	        std::string::npos);

	/* Staged mods and network-distributed mods do not pass through the base
	 * boot walker. They must receive the same private runtime-slot bridge at
	 * catalog registration time, bind the inner .pdmesh model source, and
	 * activate the loader-pool rows immediately. */
	REQUIRE(scanner.find("#include \"assetcatalog_body_head_slots.h\"") !=
	        std::string::npos);
	REQUIRE(scanner.find("ASSETCATALOG.SCANNER.BODY.CUSTOM_SLOT") !=
	        std::string::npos);
	REQUIRE(scanner.find("ASSETCATALOG.SCANNER.HEAD.CUSTOM_SLOT") !=
	        std::string::npos);
	REQUIRE(scanner.find("\"model.gltf\"") != std::string::npos);
	REQUIRE(scanner.find("\"model.glb\"") != std::string::npos);
	REQUIRE(scanner.find("fsFileSize(out) > 0") != std::string::npos);
	requireTokenOrder(scanner,
		"resolveBodyRuntimeSlotForScan(",
		"parseBodyManifestForPrivateSlot(e->id");
	requireTokenOrder(scanner,
		"resolveHeadRuntimeSlotForScan(",
		"parseHeadManifestForPrivateSlot(e->id");
	REQUIRE(scanner.find("loaderPoolParseBodyJsonForSlot(json, json_size, bodynum)") !=
	        std::string::npos);
	REQUIRE(scanner.find("loaderPoolParseHeadJsonForSlot(json, json_size, headnum)") !=
	        std::string::npos);
	REQUIRE(scanner.find("loaderPoolFinalize()") != std::string::npos);
	REQUIRE(scanner.find("catalogManagerRegisterBody(id, body)") !=
	        std::string::npos);
	REQUIRE(scanner.find("catalogManagerRegisterHead(id, head)") !=
	        std::string::npos);
	requireTokenOrder(scanner,
		"loaderPoolFinalize();",
		"catalogManagerRegisterBody(id, body)");
	requireTokenOrder(scanner,
		"loaderPoolFinalize();",
		"catalogManagerRegisterHead(id, head)");
	requireTokenOrder(main_c,
		"catalogManagerHeadInit();",
		"assetCatalogScanComponents(modsdir);");
	requireTokenOrder(main_c,
		"catalogManagerBodyInit();",
		"assetCatalogScanComponents(modsdir);");
	REQUIRE(main_c.find("assetCatalogScanComponents(modsdir);\n\t\t\tassetCatalogScanBotVariants(modsdir);") !=
	        std::string::npos);

	REQUIRE(netdistrib.find("#include \"assetcatalog_body_head_slots.h\"") !=
	        std::string::npos);
	REQUIRE(netdistrib.find("NETDISTRIB.BODY.CUSTOM_SLOT") !=
	        std::string::npos);
	REQUIRE(netdistrib.find("NETDISTRIB.HEAD.CUSTOM_SLOT") !=
	        std::string::npos);
	REQUIRE(netdistrib.find("\"model.gltf\"") != std::string::npos);
	REQUIRE(netdistrib.find("\"model.glb\"") != std::string::npos);
	REQUIRE(netdistrib.find("fsFileSize(out) > 0") != std::string::npos);
	requireTokenOrder(netdistrib,
		"distribResolveBodyRuntimeSlot(",
		"distribParseBodyManifestForPrivateSlot(e->id");
	requireTokenOrder(netdistrib,
		"distribResolveHeadRuntimeSlot(",
		"distribParseHeadManifestForPrivateSlot(e->id");
	REQUIRE(netdistrib.find("loaderPoolParseBodyJsonForSlot(json, json_size, bodynum)") !=
	        std::string::npos);
	REQUIRE(netdistrib.find("loaderPoolParseHeadJsonForSlot(json, json_size, headnum)") !=
	        std::string::npos);
	REQUIRE(netdistrib.find("loaderPoolFinalize()") != std::string::npos);
	REQUIRE(netdistrib.find("catalogManagerRegisterBody(id, body)") !=
	        std::string::npos);
	REQUIRE(netdistrib.find("catalogManagerRegisterHead(id, head)") !=
	        std::string::npos);

	/* Private custom head slots begin above the signed 8-bit range. chrdata is
	 * the render-time handoff field, so it must not narrow slot 152 to -104. */
	REQUIRE(types.find("/*0x006*/ s16 headnum;") != std::string::npos);
	REQUIRE(types.find("/*0x006*/ s8 headnum;") == std::string::npos);

	/* Legacy mod.json content rows are not allowed to require public numeric
	 * body/head slots or overwrite the private slot with a cache position. */
	REQUIRE(modmgr.find("if (is_bodies)") != std::string::npos);
	REQUIRE(modmgr.find("if (is_bodies && bodynum_field >= 0)") ==
	        std::string::npos);
	REQUIRE(modmgr.find("} else if (is_heads)") != std::string::npos);
	REQUIRE(modmgr.find("} else if (is_heads && headnum_field >= 0)") ==
	        std::string::npos);
	REQUIRE(modmgr.find("e->runtime_index = body_start + body_reg++") ==
	        std::string::npos);
	REQUIRE(modmgr.find("e->runtime_index = head_start + head_reg++") ==
	        std::string::npos);

	/* The render path still receives an integer slot today, so reverse lookup
	 * must scan catalog entries by runtime_index when boot-time caches have not
	 * yet been built or when a custom slot lives outside the base cache range. */
	REQUIRE(api.find("const char *catalogIdByRuntime(asset_type_e type, s32 runtime_index)") !=
	        std::string::npos);
	REQUIRE(api.find("for (i = 0; i < assetCatalogGetPoolSize(); i++)") !=
	        std::string::npos);
	REQUIRE(api.find("if (e->runtime_index != runtime_index) continue;") !=
	        std::string::npos);
	REQUIRE(api.find("match = e->id;") != std::string::npos);

	/* The managers must load the typed body/head archive from public source
	 * before any legacy handle/modeldef fallback. If public source conversion
	 * fails, FileProvider rows fail closed instead of falling through to a ROM
	 * handle or native bytes. */
	requireTokenOrder(body_mgr,
		"catalogLoadTypedAsset(ASSET_BODY, id)",
		"modeldefLoadToNewFromHandle(handle");
	requireTokenOrder(head_mgr,
		"catalogLoadTypedAsset(ASSET_HEAD, id)",
		"modeldefLoadToNewFromHandle(handle");
	REQUIRE(body_mgr.find("e->source.primary.provider == fileProvider()") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("e->source.primary.provider == fileProvider()") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("public source modeldef conversion failed") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("public source modeldef conversion failed") !=
	        std::string::npos);

	/* Character assembly must resolve the public catalog body identity before
	 * reading body fields or loading a modeldef, and generated source-backed
	 * modeldefs must instantiate without the legacy animation/skeleton path. */
	requireTokenOrder(body_runtime,
		"body_source_id = catalogBodyIdByBodynum(bodynum);",
		"catalogGetBodyScaleChecked(bodynum, &scaleRaw)");
	requireTokenOrder(body_runtime,
		"body_source_id = catalogBodyIdByBodynum(bodynum);",
		"bodymodeldef = catalogGetBodyModeldef(bodynum);");
	REQUIRE(body_runtime.find("BODY.IDENTITY: bodynum=%d has no catalog body; refusing slot-0 visual fallback") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("catalogGetLoadedModeldef(body_source_id) == bodymodeldef") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("bodymodeldef->numparts == 0") ==
	        std::string::npos);
	REQUIRE(body_runtime.find("if (public_source_generated_modeldef) {\n\t\theadmodeldef = NULL;") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("modelmgrInstantiateModelWithoutAnim(bodymodeldef)") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("modelmgrInstantiateModelWithAnim(bodymodeldef)") !=
	        std::string::npos);
}

TEST_CASE("source-generated character hit tests use generated display-list pointers",
          "[modding][pdxxx][c3844][source][static][b769]") {
	const std::string propobj = readTextFile("src/game/propobj.c");
	const std::string hit_walker = functionBlock(propobj, "func0f06bea0");

	REQUIRE(!hit_walker.empty());
	REQUIRE(hit_walker.find("modAssetCompilerModeldefIsGenerated(model->definition)") !=
	        std::string::npos);
	REQUIRE(hit_walker.find("s4 = rwdata->gdl;") != std::string::npos);
	REQUIRE(hit_walker.find("s6 = rodata->dl.xlugdl;") !=
	        std::string::npos);
	requireTokenOrder(hit_walker,
		"modAssetCompilerModeldefIsGenerated(model->definition)",
		"s4 = rwdata->gdl;");
	requireTokenOrder(hit_walker,
		"s4 = rwdata->gdl;",
		"UNSEGADDR(rodata->dl.opagdl)");
	requireTokenOrder(hit_walker,
		"modAssetCompilerModeldefIsGenerated(model->definition)",
		"s6 = rodata->dl.xlugdl;");
	requireTokenOrder(hit_walker,
		"s6 = rodata->dl.xlugdl;",
		"UNSEGADDR(rodata->dl.xlugdl)");
}

TEST_CASE("language runtime loads public pdlang strings source",
          "[modding][pdxxx][c3844][lang][static]") {
	const std::string lang_manifest = readTextFile("port/src/langmanifest.c");
	const std::string lang_manifest_h = readTextFile("port/include/langmanifest.h");
	const std::string lang = readTextFile("src/game/lang.c");
	const std::string langreset = readTextFile("src/game/langreset.c");
	const std::string base = readTextFile("port/src/assetcatalog_base_extended.c");

	REQUIRE(lang_manifest_h.find("langManifestLoadBankFromCatalog") !=
	        std::string::npos);
	REQUIRE(lang_manifest.find("langManifestFindBestEntryForBank") !=
	        std::string::npos);
	REQUIRE(lang_manifest.find("entry->ext.lang.bank_id != bank") !=
	        std::string::npos);
	REQUIRE(lang_manifest.find("entry->source.primary.provider == fileProvider()") !=
	        std::string::npos);
	REQUIRE(lang_manifest.find("langManifestLoadExternalJson(entry)") !=
	        std::string::npos);
	REQUIRE(lang_manifest.find("score = entry->bundled ? 1 : 2") !=
	        std::string::npos);
	REQUIRE(lang_manifest.find("JSON bank") !=
	        std::string::npos);

	const std::string lang_extract = readTextFile("port/src/romextract_pdlang.c");
	REQUIRE(lang_extract.find("rzipIs1173") != std::string::npos);
	REQUIRE(lang_extract.find("rzipInflate") != std::string::npos);
	REQUIRE(lang_extract.find("PDLANG_EXTRACT_VERSION") != std::string::npos);
	REQUIRE(lang_extract.find("pdlang_strings_json_rzip_v1") !=
	        std::string::npos);
	REQUIRE(lang_extract.find("extract_version = %s\\n") != std::string::npos);
	REQUIRE(lang_extract.find("s_existingArchiveEntryContains(dst_rel, \"lang.ini\"") !=
	        std::string::npos);

	const std::string conformance = readTextFile("tools/asset_archive_conformance.py");
	REQUIRE(conformance.find("LANG_NATIVE_FIELDS") != std::string::npos);
	REQUIRE(conformance.find("validate_lang_source_contract") != std::string::npos);
	REQUIRE(conformance.find("source_bank must be a positive integer") !=
	        std::string::npos);
	REQUIRE(conformance.find("strings.json must contain a non-empty strings array") !=
	        std::string::npos);

	REQUIRE(lang.find("langManifestLoadBankFromCatalog(bank)") !=
	        std::string::npos);
	REQUIRE(lang.find("ASSET.CHAIN: language bank") != std::string::npos);
	REQUIRE(lang.find("refusing ROM fallback") != std::string::npos);
	REQUIRE(lang.find("assetLoadRomToNew") == std::string::npos);
	REQUIRE(lang.find("assetLoadRomToAddr") == std::string::npos);

	REQUIRE(langreset.find("langLoad(LANGBANK_GUN)") != std::string::npos);
	REQUIRE(langreset.find("langLoad(LANGBANK_MPMENU)") != std::string::npos);
	REQUIRE(langreset.find("assetLoadRomToNew") == std::string::npos);

	REQUIRE(base.find("base:lang_options_en") != std::string::npos);
	REQUIRE(base.find("langManifestLoadBankFromCatalog") !=
	        std::string::npos);
}

TEST_CASE("typed archive guard rejects numeric asset references",
          "[modding][pdxxx][c3842][catalog-id][static]") {
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");
	REQUIRE(guard.find("\"model_id\"") != std::string::npos);
	REQUIRE(guard.find("\"modelnum\"") != std::string::npos);
	REQUIRE(guard.find("\"filenum\"") != std::string::npos);
	REQUIRE(guard.find("\"weapon_id\"") != std::string::npos);
	REQUIRE(guard.find("\"mode_id\"") != std::string::npos);
	REQUIRE(guard.find("\"hud_id\"") != std::string::npos);
	REQUIRE(guard.find("\"prop_type\"") != std::string::npos);
	REQUIRE(guard.find("\"stagenum\"") != std::string::npos);
	REQUIRE(guard.find("NUMERIC_LITERAL_RE") != std::string::npos);
	REQUIRE(guard.find("LEGACY_ASSET_SYMBOL_PREFIXES") !=
	        std::string::npos);
	REQUIRE(guard.find("MODEL_") != std::string::npos);
	REQUIRE(guard.find("use a catalog ID") != std::string::npos);
	REQUIRE(guard.find("\"runtime.graph.json\"") != std::string::npos);

	const std::string arena = readTextFile("port/src/romextract_pdarena.c");
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"setup.tsv\"") ==
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"mpsetup.tsv\"") ==
	        std::string::npos);
	REQUIRE(arena.find("s_buildSetupTsv") == std::string::npos);
	REQUIRE(arena.find("s_buildMpSetupTsv") == std::string::npos);
	REQUIRE(arena.find("visual_segments.tsv") == std::string::npos);
	REQUIRE(arena.find("s_buildWordsTsv") == std::string::npos);
}

TEST_CASE("c3844 live smokes use scoped asset proof logging",
          "[modding][pdxxx][c3844][source][static][b801]") {
	const std::string smoke_harness = readTextFile("port/src/smoke_harness.c");
	const std::string system = readTextFile("port/src/system.c");
	const std::string main_c = readTextFile("port/src/main.c");
	const std::string smoke_runner = readTextFile("tools/smoke-verify/run.ps1");
	const std::string smoke_readme = readTextFile("tools/smoke-verify/README.md");
	const std::string fs_c = readTextFile("port/src/fs.c");
	const std::string modasset_compiler =
		readTextFile("port/src/modasset_compiler.c");
	const std::string scenario_smoke =
		readTextFile("tools/smoke-verify/tests/scenario_pads_source_gate_smoke.json");
	const std::string weapon_smoke =
		readTextFile("tools/smoke-verify/tests/weapon_match_source_gate_smoke.json");
	const std::string all_family_smoke =
		readTextFile("tools/smoke-verify/tests/all_family_source_gate_smoke.json");

	REQUIRE(smoke_harness.find("sysLogChannelNames[i]") != std::string::npos);
	REQUIRE(smoke_harness.find("sysLogChannelBits[i]") != std::string::npos);
	REQUIRE(smoke_harness.find("strtok_r(") == std::string::npos);
	REQUIRE(smoke_harness.find("strtok(tmp, \",|+; \")") != std::string::npos);
	REQUIRE(smoke_harness.find("unknown log_channel_mask token") != std::string::npos);
	REQUIRE(smoke_readme.find("named channels such as") != std::string::npos);
	REQUIRE(smoke_runner.find("Using freshly built smoke binary") != std::string::npos);
	REQUIRE(smoke_runner.find(".claude\\session-builds\\$Session") != std::string::npos);
	REQUIRE(smoke_runner.find("-SourceBinary $BinaryOverride") != std::string::npos);

	REQUIRE(system.find("\"CATALOG.\"") != std::string::npos);
	REQUIRE(system.find("\"ASSET.\"") != std::string::npos);
	REQUIRE(system.find("\"LOADER.\"") != std::string::npos);
	REQUIRE(system.find("\"romextract \"") != std::string::npos);
	REQUIRE(system.find("\"SCENARIO.SOURCE:\"") != std::string::npos);
	REQUIRE(system.find("\"SCENARIO.GRAPH:\"") != std::string::npos);
	REQUIRE(system.find("\"MISSION.GRAPH:\"") != std::string::npos);
	REQUIRE(system.find("\"MODASSET.COMPILER:\"") != std::string::npos);
	REQUIRE(system.find("\"SCENARIO.RENDER:\"") != std::string::npos);
	REQUIRE(system.find("\"MODASSET.RENDER:\"") != std::string::npos);
	REQUIRE(system.find("\"MODELDEF.SOURCE:\"") != std::string::npos);
	REQUIRE(system.find("\"BONDGUN.SOURCE:\"") != std::string::npos);
	REQUIRE(system.find("\"BGUN.ANIM.SOURCE:\"") != std::string::npos);
	REQUIRE(system.find("\"LOG.WPN.DIAG:\"") != std::string::npos);
	REQUIRE(system.find("\"MESHCOL:\"") != std::string::npos);
	REQUIRE(fs_c.find("sysLogPrintf(LOG_VERBOSE, \"FSPATH:") !=
	        std::string::npos);
	REQUIRE(fs_c.find("sysLogPrintf(LOG_NOTE, \"FSPATH:") ==
	        std::string::npos);
	REQUIRE(modasset_compiler.find("ensureCacheFileParentDirs") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("could not open %s cache") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("errno=%d") != std::string::npos);
	REQUIRE(countOccurrences(modasset_compiler,
		        "ensureCacheFileParentDirs(path);\n\tf = fsFileOpenWrite(path);") >=
	        4);
	REQUIRE(modasset_compiler.find("char digest_key[33]") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("memcpy(digest_key, digest_hex") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("source_sha256") != std::string::npos);
	REQUIRE(modasset_compiler.find("MODASSET_COMPILER_VERSION, digest_key") !=
	        std::string::npos);
	REQUIRE(main_c.find("bootDebugCatalogProbesCanRunBeforeBaseEmit") !=
	        std::string::npos);
	REQUIRE(main_c.find("g_BootDebugLoadCatalogAssetsSourceOnly") !=
	        std::string::npos);
	REQUIRE(main_c.find("strstr(arg, \"base:\")") != std::string::npos);
	REQUIRE(main_c.find("g_BootDebugLoadCatalogAssetsFileArg") !=
	        std::string::npos);
	REQUIRE(main_c.find("if (bootDebugCatalogProbesCanRunBeforeBaseEmit())") !=
	        std::string::npos);

	REQUIRE(scenario_smoke.find("\"log_channel_mask\": \"game,catalog,render,system\"") !=
	        std::string::npos);
	REQUIRE(scenario_smoke.find("\"log_channel_mask\": \"all\"") == std::string::npos);
	REQUIRE(weapon_smoke.find("\"log_channel_mask\": \"game,catalog,render,match,combat,network,system\"") !=
	        std::string::npos);
	REQUIRE(weapon_smoke.find("\"log_channel_mask\": \"all\"") == std::string::npos);
	REQUIRE(all_family_smoke.find("\"log_channel_mask\": \"audio,render,game,system\"") !=
	        std::string::npos);
	REQUIRE(all_family_smoke.find("--debug-exit-after-catalog-probes") !=
	        std::string::npos);
	REQUIRE(all_family_smoke.find("\"log_channel_mask\": \"all\"") ==
	        std::string::npos);
	REQUIRE(all_family_smoke.find("\"log_channel_mask\": \"catalog") ==
	        std::string::npos);
}

TEST_CASE("extract-assets-only regenerates archives without gameplay boot",
          "[modding][pdxxx][c3844][source][static][extract]") {
	const std::string main_c = readTextFile("port/src/main.c");
	const std::string config_c = readTextFile("port/src/config.c");
	const std::string boot_progress = readTextFile("port/src/boot_progress.c");
	const std::string conformance = readTextFile("tools/asset_archive_conformance.py");
	const std::string boot_catalog = functionBlock(main_c, "bootRunCatalogWork");
	const std::string main_fn = functionBlock(main_c, "int main");

	REQUIRE(config_c.find("#define CONFIG_MAX_SETTINGS 2048") !=
	        std::string::npos);
	REQUIRE(config_c.find("#define CONFIG_MAX_SETTINGS 512") ==
	        std::string::npos);
	REQUIRE(boot_progress.find("current 512-entry cap") ==
	        std::string::npos);
	REQUIRE(conformance.find("collect_source_catalog_ids") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"port/src/bodydata_authored.c\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"port/src/headdata_authored.c\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"port/src/weapondata_authored.c\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("iter_catalog_context_archives") !=
	        std::string::npos);
	REQUIRE(conformance.find("without validating siblings") !=
	        std::string::npos);

	REQUIRE(main_c.find("--extract-assets-only") != std::string::npos);
	REQUIRE(main_c.find("g_BootExtractAssetsOnly") != std::string::npos);
	REQUIRE(main_c.find("BOOT: --extract-assets-only complete; exiting before scheduler/stage/gameplay init") !=
	        std::string::npos);
	REQUIRE(main_c.find("BOOT: --extract-assets-only shutdown; gameplay/window teardown skipped") !=
	        std::string::npos);
	REQUIRE(main_c.find("BOOT: --extract-assets-only set; window/UI/input startup skipped") !=
	        std::string::npos);
	REQUIRE(main_c.find("BOOT: --extract-assets-only set; audioInit() skipped") !=
	        std::string::npos);
	REQUIRE(main_c.find("BOOT: --extract-assets-only set; netInit() skipped") !=
	        std::string::npos);
	REQUIRE(main_c.find("if (!g_BootExtractAssetsOnly && !sysArgCheck(\"--no-update-check\"))") !=
	        std::string::npos);
	REQUIRE(main_c.find("if (!g_BootExtractAssetsOnly) {\n\t\tvideoInit();") !=
	        std::string::npos);
	REQUIRE(main_c.find("if (!g_BootExtractAssetsOnly) {\n\t\tpdguiBootOverlayInit();") !=
	        std::string::npos);
	REQUIRE(main_c.find("if (!g_BootExtractAssetsOnly) {\n\t\t\tpdguiBootOverlayPump();") !=
	        std::string::npos);
	REQUIRE(main_c.find("SDL_Delay(1);") != std::string::npos);

	REQUIRE(!boot_catalog.empty());
	REQUIRE(boot_catalog.find("!g_BootNoNet && !g_BootExtractAssetsOnly") !=
	        std::string::npos);
	REQUIRE(boot_catalog.find("romExtractAllPdarena(0)") !=
	        std::string::npos);
	requireTokenOrder(boot_catalog,
		"romExtractAllPdarena(0)",
		"loaderWalkerLoadAll(&walker_result)");
	requireTokenOrder(boot_catalog,
		"loaderWalkerLoadAll(&walker_result)",
		"catalogBuildRuntimeCaches()");
	requireTokenOrder(boot_catalog,
		"catalogBuildRuntimeCaches()",
		"bootProgressMarkComplete()");

	REQUIRE(!main_fn.empty());
	REQUIRE(main_fn.find("g_BootExtractAssetsOnly = sysArgCheck(\"--extract-assets-only\") ? true : false;") !=
	        std::string::npos);
	REQUIRE(main_fn.find("audioInit();\n\t} else {\n\t\tsysLogPrintf(LOG_NOTE,") !=
	        std::string::npos);
	requireTokenOrder(main_fn,
		"bootPoolWaitIdle();",
		"BOOT: --extract-assets-only complete; exiting before scheduler/stage/gameplay init");
	requireTokenOrder(main_fn,
		"BOOT: --extract-assets-only complete; exiting before scheduler/stage/gameplay init",
		"bootCreateSched();");
	requireTokenOrder(main_fn,
		"BOOT: --extract-assets-only complete; exiting before scheduler/stage/gameplay init",
		"mainProc();");
}

TEST_CASE("c3841 scenario archives are source-first and runtime-native",
          "[modding][pdxxx][c3841][scenario][static]") {
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");
	REQUIRE(guard.find("PDSCENARIO_REQUIRED_PUBLIC_ENTRY_NAMES") !=
	        std::string::npos);
	REQUIRE(guard.find("navigation/waypoints.json") !=
	        std::string::npos);
	REQUIRE(guard.find("navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(guard.find("setup.fields.json") !=
	        std::string::npos);
	REQUIRE(guard.find("\"scene.glb\"") != std::string::npos);
	REQUIRE(guard.find("\"level.graph.json\"") != std::string::npos);
	const std::string conformance = readTextFile("tools/asset_archive_conformance.py");
	const std::string scene_texture_checker =
		readTextFile("tools/verify_scene_glb_texture_contract.py");
	REQUIRE(conformance.find("runtime_source_file = scene.glb") !=
	        std::string::npos);
	REQUIRE(conformance.find("setup_fields_file = setup.fields.json") !=
	        std::string::npos);
	const std::string arena = readTextFile("port/src/romextract_pdarena.c");
	REQUIRE(arena.find("\"vehicle.ai_list\"") == std::string::npos);
	REQUIRE(arena.find("\"vehicle.ai_offset\"") != std::string::npos);
	REQUIRE(arena.find("const struct heliobj *heli") != std::string::npos);
	REQUIRE(arena.find("\"vehicle.speed\", heli->speed") !=
	        std::string::npos);

	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("scene_file = scene.glb") != std::string::npos);
	REQUIRE(scanner.find("portals_file = portals.json") != std::string::npos);
	REQUIRE(scanner.find("setup_fields_file = setup.fields.json") !=
	        std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"portals_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"waypoints_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"waygroups_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"covers_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"paths_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"scene_file\"") != std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, sf)") != std::string::npos);

	const std::string distrib = readTextFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("iniGet(ini, \"waypoints_file\"") !=
	        std::string::npos);
	REQUIRE(distrib.find("iniGet(ini, \"paths_file\"") !=
	        std::string::npos);

	const std::string load = readTextFile("port/src/assetcatalog_load.c");
	REQUIRE(load.find("modAssetCompilerBuildColmesh(colmesh_source_path, mesh)") !=
	        std::string::npos);
	REQUIRE(load.find("entry->ext.scenario.collision_file") !=
	        std::string::npos);

	const std::string runtime = readTextFile("port/src/asset_runtime.c");
	REQUIRE(runtime.find("entry->ext.scenario.scene_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.scenario.portals_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("scenario_portals_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.scenario.navigation_waypoints_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("scenario_navigation_waypoints_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.scenario.navigation_paths_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("scenario_navigation_paths_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.scenario.level_graph_file") !=
	        std::string::npos);
	{
		const size_t scenario_anchor =
			runtime.find("entry->ext.scenario.scene_file");
		const size_t scenario_start =
			runtime.rfind("case ASSET_SCENARIO:", scenario_anchor);
		const size_t theme_start = runtime.find("case ASSET_THEME:",
			scenario_anchor);
		REQUIRE(scenario_anchor != std::string::npos);
		REQUIRE(scenario_start != std::string::npos);
		REQUIRE(theme_start != std::string::npos);
		const std::string scenario_case =
			runtime.substr(scenario_start, theme_start - scenario_start);
		REQUIRE(scenario_case.find("entry->ext.scenario.scene_file[0]") ==
		        std::string::npos);
		REQUIRE(scenario_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->authored_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->dependency_b) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_portals_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_pads_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_spawns_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_volumes_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_objects_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_setup_fields_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_ai_lists_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_objectives_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_navigation_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_navigation_waypoints_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_navigation_waygroups_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_navigation_covers_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_navigation_paths_file)") !=
		        std::string::npos);
	}
	REQUIRE(runtime.find("entry->ext.skin.texture_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.swatches_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.material_archive") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.texture_archive") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.hud.layout_file") !=
	        std::string::npos);
	{
		const size_t hud_anchor =
			runtime.find("entry->ext.hud.layout_file");
		const size_t hud_start =
			runtime.rfind("case ASSET_HUD:", hud_anchor);
		const size_t ui_start = runtime.find("case ASSET_UI:", hud_anchor);
		const size_t material_start = runtime.find("case ASSET_MATERIAL:", ui_start);
		REQUIRE(hud_anchor != std::string::npos);
		REQUIRE(hud_start != std::string::npos);
		REQUIRE(ui_start != std::string::npos);
		REQUIRE(material_start != std::string::npos);
		const std::string hud_case =
			runtime.substr(hud_start, ui_start - hud_start);
		REQUIRE(hud_case.find("entry->ext.hud.layout_file") !=
		        std::string::npos);
		REQUIRE(hud_case.find("entry->ext.hud.texture_file[0]") ==
		        std::string::npos);
		REQUIRE(hud_case.find("binding->primary_path") ==
		        std::string::npos);
		const std::string ui_case =
			runtime.substr(ui_start, material_start - ui_start);
		REQUIRE(ui_case.find("entry->ext.ui.texture_file") !=
		        std::string::npos);
		REQUIRE(ui_case.find("entry->ext.ui.layout_file") !=
		        std::string::npos);
		REQUIRE(ui_case.find("entry->ext.ui.nineslice_file") !=
		        std::string::npos);
		REQUIRE(ui_case.find(": entry->ext.ui.layout_file") ==
		        std::string::npos);
		REQUIRE(ui_case.find("binding->primary_path") ==
		        std::string::npos);
	}
	REQUIRE(runtime.find("entry->ext.mission.objectives_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.mission.briefing_file") !=
	        std::string::npos);
	{
		const size_t mission_anchor =
			runtime.find("entry->ext.mission.mission_graph_file");
		const size_t mission_start =
			runtime.rfind("case ASSET_MISSION:", mission_anchor);
		const size_t gamemode_start = runtime.find("case ASSET_GAMEMODE:",
			mission_anchor);
		REQUIRE(mission_anchor != std::string::npos);
		REQUIRE(mission_start != std::string::npos);
		REQUIRE(gamemode_start != std::string::npos);
		const std::string mission_case =
			runtime.substr(mission_start, gamemode_start - mission_start);
		REQUIRE(mission_case.find("entry->ext.mission.mission_graph_file") !=
		        std::string::npos);
		REQUIRE(mission_case.find("entry->ext.mission.scenario_archive") !=
		        std::string::npos);
		REQUIRE(mission_case.find("entry->ext.mission.objectives_file") !=
		        std::string::npos);
		REQUIRE(mission_case.find("entry->ext.mission.briefing_file") !=
		        std::string::npos);
		REQUIRE(mission_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(mission_case.find("s_hasText(binding->authored_file) &&") !=
		        std::string::npos);
		REQUIRE(mission_case.find("s_hasText(binding->dependency_a) &&") !=
		        std::string::npos);
		REQUIRE(mission_case.find("s_hasText(binding->dependency_b) &&") !=
		        std::string::npos);
		REQUIRE(mission_case.find("s_hasText(binding->dependency_c)") !=
		        std::string::npos);
	}
	REQUIRE(runtime.find("entry->ext.effect.timeline_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.character.portrait_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("dependency_c") != std::string::npos);
	REQUIRE(runtime.find("dependency_d") != std::string::npos);
	REQUIRE(runtime.find("dependency_e") != std::string::npos);
}

TEST_CASE("c3843 remaining base asset families emit clean native archives",
          "[modding][pdxxx][c3843][static]") {
	const std::string base =
		readTextFile("port/src/assetcatalog_base_extended.c");
	const std::string meta = readTextFile("port/src/romextract_pdmeta.c");
	const std::string texture_extractor =
		readTextFile("port/src/romextract_pdtexture.c");
	const std::string arena = readTextFile("port/src/romextract_pdarena.c");
	const std::string conformance =
		readTextFile("tools/asset_archive_conformance.py");
	const std::string guard =
		readTextFile("tools/asset_native_source_guard.py");
	const std::string scene_texture_checker =
		readTextFile("tools/verify_scene_glb_texture_contract.py");
	const std::string header = readTextFile("port/include/romextract_pd.h");
	const std::string theme =
		readTextFile("port/fast3d/pdgui_theme_loader.cpp");
	const std::string runtime = readTextFile("port/src/asset_runtime.c");
	const std::string file_provider =
		readTextFile("port/src/assetprovider_file.c");
	const std::string scanner =
		readTextFile("port/src/assetcatalog_scanner.c");
	const std::string distrib =
		readTextFile("port/src/net/netdistrib.c");
	const std::string examples =
		readTextFile("tools/build_typed_pdxxx_examples.py");

	const char *families[] = {
		".pdtexture",
		".pdmaterial",
		".pdskin",
		".pdeffect",
		".pdprop",
		".pdvehicle",
		".pdgamemode",
		".pdbotprofile",
		".pdhud",
		".pdmission",
		".pdtheme",
	};
	for (const char *family : families) {
		INFO(family);
		REQUIRE((base.find(family) != std::string::npos ||
		         meta.find(family) != std::string::npos ||
		         texture_extractor.find(family) != std::string::npos ||
		         header.find(family) != std::string::npos));
	}

	REQUIRE(base.find("ASSET_MATERIAL") != std::string::npos);
	REQUIRE(base.find("ASSET_SKIN") != std::string::npos);
	REQUIRE(base.find("ASSET_EFFECT") != std::string::npos);
	REQUIRE(base.find("ASSET_VEHICLE") != std::string::npos);
	REQUIRE(base.find("textures\", idbuf, \".pdtexture\"") !=
	        std::string::npos);
	REQUIRE(base.find("materials\", idbuf, \".pdmaterial\"") !=
	        std::string::npos);
	REQUIRE(base.find("skins\", skin_id, \".pdskin\"") !=
	        std::string::npos);
	REQUIRE(base.find("effects\", idbuf, \".pdeffect\"") !=
	        std::string::npos);
	REQUIRE(base.find("props\", idbuf, \".pdprop\"") !=
	        std::string::npos);
	REQUIRE(base.find("vehicles\", idbuf, \".pdvehicle\"") !=
	        std::string::npos);
	REQUIRE(base.find("dependencies/assets/models") !=
	        std::string::npos);
	REQUIRE(base.find("fsDataPathFor(rel, out, out_n)") !=
	        std::string::npos);
	{
		const size_t vehicle_start = base.find("/* ---- vehicles ---- */");
		const size_t hand_start = base.find("/* ---- first-person hand models", vehicle_start);
		REQUIRE(vehicle_start != std::string::npos);
		REQUIRE(hand_start != std::string::npos);
		const std::string vehicle_block =
			base.substr(vehicle_start, hand_start - vehicle_start);
		REQUIRE(vehicle_block.find("const char *primary_file = e->ext.vehicle.model_file[0]") !=
		        std::string::npos);
		REQUIRE(vehicle_block.find("catalogSetPrimaryFile(e, primary_file)") !=
		        std::string::npos);
		REQUIRE(vehicle_block.find("catalogSetPrimaryFile(e, e->ext.vehicle.physics_file)") ==
		        std::string::npos);
	}

	/* c3849 Wave 4: the texture emitter lives in romextract_pdtexture.c and
	 * gates on the family fast-cache stamp pdmeta wrote but never read. */
	REQUIRE(texture_extractor.find("s_emitTexture") != std::string::npos);
	REQUIRE(meta.find("s_emitTexture") == std::string::npos);
	/* Full-parity Phase 1a (2026-07-07): token gained _fmtmeta_palv3_json when
	 * the schema-v2 manifest + palette.json emission landed (commit 8180fa04). */
	REQUIRE(texture_extractor.find(
	                "ROMEXTRACT_PDTEXTURE_FAST_CACHE_KIND \\\n"
	                "\t\"pdtexture_png_v1_decoded_rom_rgba_manifest_texture_file_cipalfix_b943_iafix_b945_fmtmeta_palv3_json\"") !=
	        std::string::npos);
	REQUIRE(texture_extractor.find(
	                "romExtractPdFastCacheCanSkip(ROMEXTRACT_PDTEXTURE_FAST_CACHE_KIND") !=
	        std::string::npos);
	REQUIRE(texture_extractor.find(
	                "romExtractPdFastCacheWrite(ROMEXTRACT_PDTEXTURE_FAST_CACHE_KIND") !=
	        std::string::npos);
	/* c3849 Slice B: boot bind verification (loud TEXTURE.BIND pass) is
	 * part of the extractor; details pinned in the [c3849][bind] case. */
	REQUIRE(texture_extractor.find("TEXTURE.BIND") != std::string::npos);
	REQUIRE(meta.find("s_emitMaterial") != std::string::npos);
	REQUIRE(meta.find("s_emitSkin") != std::string::npos);
	REQUIRE(meta.find("s_emitEffect") != std::string::npos);
	REQUIRE(meta.find("s_emitProp") != std::string::npos);
	REQUIRE(meta.find("s_emitVehicle") != std::string::npos);
	REQUIRE(meta.find("PDMETA_FAST_CACHE_KIND \"pdmeta_table_backed_v13_hydrated_metadata_source\"") !=
	        std::string::npos);
	REQUIRE(texture_extractor.find("texture.png") != std::string::npos);
	REQUIRE(meta.find("material.json") != std::string::npos);
	REQUIRE(meta.find("skin.json") != std::string::npos);
	REQUIRE(meta.find("swatches.json") != std::string::npos);
	REQUIRE(meta.find("\\\"swatches_file\\\": \\\"swatches.json\\\"") !=
	        std::string::npos);
	REQUIRE(meta.find("\\\"min_players\\\": %d") !=
	        std::string::npos);
	REQUIRE(meta.find("\\\"type_key\\\": \\\"%s\\\"") !=
	        std::string::npos);
	REQUIRE(meta.find("\\\"objectives_file\\\": \\\"objectives.json\\\"") !=
	        std::string::npos);
	REQUIRE(meta.find("\\\"briefing_file\\\": \\\"briefing.json\\\"") !=
	        std::string::npos);
	REQUIRE(meta.find("prop.json") != std::string::npos);
	REQUIRE(meta.find("physics.json") != std::string::npos);
	REQUIRE(meta.find("behavior.graph.json") != std::string::npos);
	REQUIRE(meta.find("assetArchiveWriterAddPublicDisk(&writer, model_member") !=
	        std::string::npos);

	REQUIRE(arena.find("romExtractDecodeTextureImages") !=
	        std::string::npos);
	REQUIRE(arena.find("romExtractTextureSlotIsEmpty") !=
	        std::string::npos);
	REQUIRE(header.find("romExtractDecodeTextureImages") !=
	        std::string::npos);
	REQUIRE(header.find("romExtractTextureSlotIsEmpty") !=
	        std::string::npos);
	REQUIRE(texture_extractor.find("k_Transparent1x1Png") !=
	        std::string::npos);
	REQUIRE(texture_extractor.find("empty_rom_slot") != std::string::npos);
	REQUIRE(meta.find("\"mode_id = %d") == std::string::npos);
	REQUIRE(meta.find("\"type = %d") == std::string::npos);
	REQUIRE(meta.find("\"difficulty = %d") == std::string::npos);
	REQUIRE(meta.find("\"hud_id = %d") == std::string::npos);
	REQUIRE(meta.find("\"element_type = %d") == std::string::npos);
	REQUIRE(meta.find("\"effect_type = %d") == std::string::npos);
	REQUIRE(meta.find("\"prop_type = %d") == std::string::npos);
	REQUIRE(arena.find("ROMEXTRACT_PDARENA_FAST_CACHE_KIND \"pdarena_clean_public_v10_pdscenario_v99_iafix_b945\"") !=
	        std::string::npos);
	REQUIRE(arena.find("ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND \"pdscenario_scene_glb_clean_public_v99_standalone_backfill_collision_obj_collision_flags_json_room_lights_json_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_alphamask_alphablend_quip_shuffle_graph_portals_json_objects_json_setup_fields_json_ai_lists_json_ai_command_graph_navhashes_objectives_spawns_volumes_pads_paths_json_navtables_json_cipalfix_b943_iafix_b945\"") !=
	        std::string::npos);
	REQUIRE(arena.find("s_aiOpcodeSemanticKind") != std::string::npos);
	REQUIRE(arena.find("ai_command_nodes_json") != std::string::npos);
	REQUIRE(arena.find("ai_command_links_json") != std::string::npos);
	REQUIRE(arena.find("\\\"kind\\\": \\\"scenario.ai.command\\\"") != std::string::npos);
	REQUIRE(arena.find("\\\"semantic_kind\\\"") != std::string::npos);
	REQUIRE(arena.find("\\\"ai_commands\\\": %u") != std::string::npos);
	REQUIRE(conformance.find("counts.ai_commands must match ai/ailists.json rows") !=
	        std::string::npos);
	REQUIRE(conformance.find("must include one scenario.ai.command node per ai/ailists.json row") !=
	        std::string::npos);
	REQUIRE(conformance.find("must be linked from scenario.ai.lists") !=
	        std::string::npos);
	REQUIRE(arena.find("supports_drop = true") != std::string::npos);
	REQUIRE(arena.find("\\\"source_counts\\\": { \\\"pads\\\": %u, \\\"volumes\\\": %u, \\\"waypoints\\\": %u, \\\"waygroups\\\": %u, \\\"covers\\\": %u, \\\"paths\\\": %u }") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"source_hashes\\\": { \\\"scene.glb\\\": \\\"%s\\\", \\\"collision.obj\\\": \\\"%s\\\", \\\"navigation.ini\\\": \\\"%s\\\", \\\"portals.json\\\": \\\"%s\\\", \\\"pads.json\\\": \\\"%s\\\", \\\"spawns.json\\\": \\\"%s\\\", \\\"volumes.json\\\": \\\"%s\\\"") !=
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"portals.json\"") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntry(relpath, \"portals.tsv\")") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingPdscenarioGeneratedNavmeshIsCurrent(relpath)") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingPdscenarioNavmeshMetadataMatches") !=
	        std::string::npos);
	REQUIRE(arena.find("\"_meta/generated-navmesh.json\"") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"source_counts\\\": {") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"source_hashes\\\": {") !=
	        std::string::npos);
	REQUIRE(arena.find("\"navigation/paths.json\", \"paths\"") !=
	        std::string::npos);
	REQUIRE(arena.find("\"scenario.portals.source\"") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"portals\\\": \\\"portals.json\\\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("validate_portals_json_schema") !=
	        std::string::npos);
	REQUIRE(conformance.find("scenario.ini must declare portals_file = portals.json") !=
	        std::string::npos);
	REQUIRE(conformance.find("level.graph.json must bind portals table to portals.json") !=
	        std::string::npos);
	REQUIRE(arena.find("PDSCENARIO_BG_VISUAL_EXPORT_VERSION \"bg_visual_scene_glb_v12_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_alphamask_materialextras_dualtex_alphablend_cipalfix_b943_iafix_b945\"") !=
	        std::string::npos);
	REQUIRE(arena.find("s_rgbaHasNonOpaqueAlpha") !=
	        std::string::npos);
	REQUIRE(arena.find("tex->has_alpha") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"alphaMode\\\":\\\"MASK\\\",\\\"alphaCutoff\\\":0.01") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveEntryContains(relpath, \"scene.glb\",") !=
	        std::string::npos);
	REQUIRE(arena.find("PDSCENARIO_BG_VISUAL_EXPORT_VERSION)") !=
	        std::string::npos);
	REQUIRE(arena.find("scenario_graph_cache = %s") != std::string::npos);
	REQUIRE(arena.find("scenario_graph_cache = \" ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND") !=
	        std::string::npos);
	REQUIRE(conformance.find("SCENARIO_GRAPH_CACHE_KIND") !=
	        std::string::npos);
	REQUIRE(conformance.find("_meta/generated-navmesh.json must declare source_counts") !=
	        std::string::npos);
	REQUIRE(conformance.find("source_counts.{count_key} must match") !=
	        std::string::npos);
	REQUIRE(conformance.find("_meta/generated-navmesh.json must declare source_hashes") !=
	        std::string::npos);
	REQUIRE(conformance.find("source_hashes.{source_name} must match public source bytes") !=
	        std::string::npos);
	REQUIRE(conformance.find("navigation.ini must declare {description} capability support") !=
	        std::string::npos);
	REQUIRE(conformance.find("_meta/generated-navmesh.json must declare movement capabilities walk,jump,drop,wall,ceiling") !=
	        std::string::npos);
	REQUIRE(conformance.find("arena.ini must declare scenario_graph_cache") !=
	        std::string::npos);
	REQUIRE(conformance.find("mission.ini must declare scenario_graph_cache") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"texCoord\\\":0") != std::string::npos);
	REQUIRE(arena.find("s_bgMaterialUvScaleForGlb") != std::string::npos);
	REQUIRE(arena.find("s_bgMaterialGlbAuthorUv") != std::string::npos);
	REQUIRE(arena.find("\\\"TEXCOORD_1\\\":%u") != std::string::npos);
	REQUIRE(arena.find("\\\"COLOR_0\\\":%u") != std::string::npos);
	REQUIRE(arena.find("\\\"baseColorTexture\\\":{\\\"index\\\":%d,\\\"texCoord\\\":0}") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"extras\\\":{\\\"pd2_material\\\":") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"texture_command\\\":\\\"%s\\\"") !=
	        std::string::npos);
	REQUIRE(arena.find("s_bgMaterialTextureCommandName") != std::string::npos);
	REQUIRE(arena.find("s_bgGltfWrapName") != std::string::npos);
	REQUIRE(arena.find("\\\"secondaryTexture\\\":{\\\"index\\\":%d,\\\"texCoord\\\":1}") !=
	        std::string::npos);
	REQUIRE(arena.find("s_bgGltfWrapMode") != std::string::npos);
	REQUIRE(arena.find("texture_scale_s") != std::string::npos);
	REQUIRE(arena.find("op == (u8)G_TEXTURE") != std::string::npos);
	REQUIRE(arena.find("\\\"wrapS\\\":%u,\\\"wrapT\\\":%u") != std::string::npos);
	REQUIRE(arena.find("m->shifts <= 10") != std::string::npos);
	REQUIRE(arena.find("m->shiftt <= 10") != std::string::npos);
	REQUIRE(arena.find("s_shift_scale / (f32)tex->width") != std::string::npos);
	REQUIRE(arena.find("t_shift_scale / (f32)tex->height") != std::string::npos);
	REQUIRE(arena.find("s_bgMaterialGlbUv(m->vertices[v].u, m->vertices[v].v") !=
	        std::string::npos);
	REQUIRE(conformance.find("validate_scene_glb_texture_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("TEXCOORD_0 range") != std::string::npos);
	REQUIRE(conformance.find("bg_visual_scene_glb_v12_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_alphamask_materialextras_dualtex_alphablend") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2_material extras for source renderer material parity") !=
	        std::string::npos);
	REQUIRE(conformance.find("must be repeat, clamp, or mirror") !=
	        std::string::npos);
	REQUIRE(conformance.find("must be an integer native material field") !=
	        std::string::npos);
	REQUIRE(conformance.find("must reference a texture index") !=
	        std::string::npos);
	REQUIRE(conformance.find("renderer-parity runtime UVs") !=
	        std::string::npos);
	REQUIRE(conformance.find("declares secondary_image") != std::string::npos);
	REQUIRE(conformance.find("TEXCOORD_1 runtime UVs") != std::string::npos);
	REQUIRE(conformance.find("COLOR_0 vertex colors") != std::string::npos);
	REQUIRE(conformance.find("DCC-authoring UV range") != std::string::npos);
	REQUIRE(conformance.find("visible textures to TEXCOORD_0") !=
	        std::string::npos);
	REQUIRE(scene_texture_checker.find("validate_scene_glb_texture_contract") !=
	        std::string::npos);
	REQUIRE(scene_texture_checker.find(".pdscenario") != std::string::npos);
	REQUIRE(scene_texture_checker.find(".pdarena") != std::string::npos);
	REQUIRE(scene_texture_checker.find("path.is_dir()") != std::string::npos);
	REQUIRE(scene_texture_checker.find("path.rglob(\"*\")") !=
	        std::string::npos);
	REQUIRE(scene_texture_checker.find("scene.glb texture contract failed") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_generated_scenario_texture_contracts") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_ai_command_graph_coverage") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_runtime_count_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_runtime_lookup_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_player_state_access_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_RUNTIME_LOOKUP_RULES") !=
	        std::string::npos);
	REQUIRE(guard.find(
		        "SCENARIO_CURRENT_PLAYER_POINTER_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("\"objFindByTagId\"") !=
	        std::string::npos);
	REQUIRE(guard.find("\"tagFindById\"") !=
	        std::string::npos);
	REQUIRE(guard.find("\"pathFindById\"") !=
	        std::string::npos);
	REQUIRE(guard.find("g_Vars\\.currentplayer\\s*->") !=
	        std::string::npos);
	REQUIRE(guard.find("g_Vars\\.currentplayer\\b(?!\\s*->)") !=
	        std::string::npos);
	REQUIRE(guard.find("s_aiGraphRequireRuntimePlayerSlot") !=
	        std::string::npos);
	REQUIRE(guard.find("\"s_countRuntimePads\"") !=
	        std::string::npos);
	REQUIRE(guard.find("s_aiGraphTryCountRuntimeSetupChrRowsFromSource") !=
	        std::string::npos);
	REQUIRE(guard.find("s_aiGraphRuntimeSetupCharacterSourceIsActive()") !=
	        std::string::npos);
	REQUIRE(guard.find("s_setupGraphValidateBlockedPathWaypoints") !=
	        std::string::npos);
	REQUIRE(guard.find("AI_INTERPRETER_ONLY_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("AI_GRAPH_PENDING_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("AI_GRAPH_PENDING_FUNCTIONS = set()") !=
	        std::string::npos);
	REQUIRE(guard.find("\"aiStop\"") == std::string::npos);
	REQUIRE(guard.find("\"aiKneel\"") == std::string::npos);
	REQUIRE(guard.find("\"aiSurrender\"") == std::string::npos);
	REQUIRE(guard.find("\"aiFadeOut\"") == std::string::npos);
	REQUIRE(guard.find("\"aiRemoveChr\"") == std::string::npos);
	REQUIRE(guard.find("\"aiChrDoAnimation\"") == std::string::npos);
	REQUIRE(guard.find("\"aiBeSurprisedOneHand\"") == std::string::npos);
	REQUIRE(guard.find("\"aiBeSurprisedLookAround\"") == std::string::npos);
	REQUIRE(guard.find("\"aiBeSurprisedSurrender\"") == std::string::npos);
	REQUIRE(guard.find("\"aiKillBond\"") == std::string::npos);
	REQUIRE(guard.find("\"aiFaceEntity\"") == std::string::npos);
	REQUIRE(guard.find("\"ai0019\"") == std::string::npos);
	REQUIRE(guard.find("\"aiChrDamageChr\"") == std::string::npos);
	REQUIRE(guard.find("\"aiConsiderGrenadeThrow\"") == std::string::npos);
	REQUIRE(guard.find("\"aiDropItem\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryRunFromTarget\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryJogToTargetProp\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryWalkToTargetProp\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryRunToTargetProp\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryGoToCoverProp\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryJogToChr\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryWalkToChr\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryRunToChr\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfCanHearAlarm\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfPatrolling\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfAlarmActive\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfGasActive\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfHearsTarget\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfSawInjury\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfSawDeath\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfLosToTarget\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfLosToAttackTarget\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfTargetNearlyInSight\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfNearlyInTargetsSight\"") == std::string::npos);
	REQUIRE(guard.find("\"aiSetPadPresetToPadOnRouteToTarget\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfSawTargetRecently\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfHeardTargetRecently\"") == std::string::npos);
	REQUIRE(guard.find("\"aiSayQuip\"") == std::string::npos);
	REQUIRE(guard.find("\"aiSayCiStaffQuip\"") == std::string::npos);
	REQUIRE(guard.find("\"aiShuffleRuinsPillars\"") == std::string::npos);
	REQUIRE(guard.find("\"aiShufflePelagicSwitches\"") == std::string::npos);
	for (const char *name : {
		     "\"aiIfLosToChr\"",
		     "\"aiIfNeverBeenOnScreen\"",
		     "\"aiIfOnScreen\"",
		     "\"aiIfChrInOnScreenRoom\"",
		     "\"aiIfRoomIsOnScreen\"",
		     "\"aiIfTargetAimingAtMe\"",
		     "\"aiIfNearMiss\"",
		     "\"aiIfSeesSuspiciousItem\"",
		     "\"aiIfCheckFovWithTarget\"",
		     "\"aiIfTargetInFovLeft\"",
		     "\"aiIfTargetOutOfFovLeft\"",
		     "\"aiIfTargetInFov\"",
		     "\"aiIfTargetOutOfFov\"",
		     "\"aiIfDistanceToTargetLessThan\"",
		     "\"aiIfDistanceToTargetGreaterThan\"",
		     "\"aiIfChrDistanceToPadLessThan\"",
		     "\"aiIfChrDistanceToPadGreaterThan\"",
		     "\"aiIfDistanceToChrLessThan\"",
		     "\"aiIfDistanceToChrGreaterThan\"",
		     "\"ai0058\"",
		     "\"aiIfDistanceFromTargetToPadLessThan\"",
		     "\"aiIfDistanceFromTargetToPadGreaterThan\"",
		     "\"aiIfChrInRoom\"",
		     "\"aiIfTargetInRoom\"",
		     "\"aiIfChrHasObject\"",
		     "\"aiIfWeaponThrown\"",
		     "\"aiIfWeaponThrownOnObject\"",
		     "\"aiIfChrHasWeaponEquipped\"",
		     "\"aiIfGunUnclaimed\"",
		     "\"aiIfObjectHealthy\"",
		     "\"ai0075\"",
		     "\"aiSetPadPresetToTargetQuadrant\"",
		     "\"ai00e9\"",
		     "\"ai00fd\"",
		     "\"ai012f\"",
		     "\"ai0184\"",
		     "\"aiEndLevel\"",
		     "\"ai00dd\"",
		     "\"aiWarpJoToPad\"",
		     "\"aiSetCameraAnimation\"",
		     "\"aiIfInCutscene\"",
		     "\"aiIfCutsceneButtonPressed\"",
		     "\"ai0175\"",
		     "\"ai00df\"",
		     "\"aiRevokeControl\"",
		     "\"aiGrantControl\"",
		     "\"ai00e3\"",
		     "\"ai00e4\"",
		     "\"aiIfColourFadeComplete\"",
		     "\"ai00f4\"",
		     "\"ai00f5\"",
		     "\"ai00f6\"",
		     "\"aiSpawnChrAtPad\"",
		     "\"aiSpawnChrAtChr\"",
		     "\"aiTryEquipWeapon\"",
		     "\"aiTryEquipHat\"",
		     "\"aiSetObjImage\"",
		     "\"aiObjectDoAnimation\"",
		     "\"aiSetDoorOpen\"",
		     "\"aiDuplicateChr\"",
		     "\"aiEnableChr\"",
		     "\"aiDisableChr\"",
		     "\"aiEnableObj\"",
		     "\"aiDisableObj\"",
		     "\"aiChrMoveToPad\"",
		     "\"aiChrSetTeam\"",
		     "\"aiDamageChrByAmount\"",
		     "\"aiDoPresetAnimation\"",
		     "\"ai01aa\"",
		     "\"ai01b4\"",
		     "\"aiIfChrNotTalking\"",
		     "\"aiIfOrders\"",
		     "\"aiIfHasOrders\"",
		     "\"aiIfChrInSquadronDoingAction\"",
		     "\"aiIfChrListening\"",
		     "\"aiIfNotListening\"",
		     "\"aiIfChrInjured\"",
		     "\"aiIfAction\"",
		     "\"aiIfChrAmmoQuantityLessThan\"",
		     "\"aiIfChrTarget\"",
		     "\"aiIfCompareChrPresetsTeam\"",
		     "\"aiIfHuman\"",
		     "\"aiIfSkedar\"",
		     "\"aiIfPropPresetIsBlockingSightToTarget\"",
		     "\"aiRemoveObjectAtPropPreset\"",
		     "\"aiIfPropPresetHeightLessThan\"",
		     "\"aiSetTarget\"",
		     "\"aiIfPresetsTargetIsNotMyTarget\"",
		     "\"aiSetChrPresetToChrNearSelf\"",
		     "\"aiSetChrPresetToChrNearPad\"",
		     "\"aiIfDangerousObjectNearby\"",
		     "\"aiIfHeliWeaponsArmed\"",
		     "\"aiIfHoverbotNextStep\"",
		     "\"aiShuffleInvestigationTerminals\"",
		     "\"aiSetPadPresetToInvestigationTerminal\"",
		     "\"aiHeliArmWeapons\"",
		     "\"aiHeliUnarmWeapons\"",
		     "\"aiIfSafety2LessThan\"",
		     "\"aiIfPlayerUsingCmpOrAr34\"",
		     "\"aiDetectEnemyOnSameFloor\"",
		     "\"aiDetectEnemy\"",
		     "\"aiIfSafetyLessThan\"",
		     "\"aiIfTargetMovingSlowly\"",
		     "\"aiIfTargetMovingCloser\"",
		     "\"aiIfTargetMovingAway\"",
		     "\"aiIfSquadronIsDead\"",
		     "\"aiIfTrue\"",
		     "\"aiIfNumChrsInSquadronGreaterThan\"",
		     "\"aiIfNaturalAnim\"",
		     "\"aiIfY\"",
		     "\"aiIfSoundTimer\"",
		     "\"aiIfTargetYDifferenceLessThan\"",
	     }) {
		REQUIRE(guard.find(name) == std::string::npos);
	}
	REQUIRE(guard.find("untracked debt") != std::string::npos);
	REQUIRE(guard.find("Build/data/ntsc-final/scenarios") !=
	        std::string::npos);
	REQUIRE(guard.find("Build/data/ntsc-final/arenas") != std::string::npos);
	REQUIRE(guard.find(".pdarena") != std::string::npos);
	REQUIRE(guard.find("validate_scene_glb_texture_contract") !=
	        std::string::npos);
	REQUIRE(arena.find("\"stagenum = %d\\n\"") == std::string::npos);
	REQUIRE(scanner.find("parseModeKeyValue") != std::string::npos);
	REQUIRE(scanner.find("parseBotTypeKeyValue") != std::string::npos);
	REQUIRE(scanner.find("parseHudElementKeyValue") != std::string::npos);
	REQUIRE(scanner.find("parsePropKeyValue") != std::string::npos);
	REQUIRE(scanner.find("parseEffectTypeKeyValue") != std::string::npos);
	REQUIRE(distrib.find("distribParseModeKeyValue") != std::string::npos);
	REQUIRE(distrib.find("distribParseHudElementKeyValue") !=
	        std::string::npos);
	REQUIRE(distrib.find("distribParsePropKeyValue") != std::string::npos);
	REQUIRE(distrib.find("distribParseEffectTypeKeyValue") !=
	        std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"music_file\", iniGet(ini, \"midi_file\", \"\"))") !=
	        std::string::npos);
	REQUIRE(distrib.find("iniGet(ini, \"music_file\", iniGet(ini, \"midi_file\", \"\"))") !=
	        std::string::npos);
	REQUIRE(scanner.find("strncpy(e->ext.audio.file_path, audio_file") !=
	        std::string::npos);
	REQUIRE(distrib.find("strncpy(e->ext.audio.file_path, audio_file") !=
	        std::string::npos);
	REQUIRE(scanner.find("strncpy(e->ext.font.font_file, pf") !=
	        std::string::npos);
	REQUIRE(scanner.find("strncpy(e->ext.font.metrics_file, mf") !=
	        std::string::npos);
	REQUIRE(distrib.find("strncpy(e->ext.font.font_file, pf") !=
	        std::string::npos);
	REQUIRE(distrib.find("strncpy(e->ext.font.metrics_file, mf") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.font.font_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.font.metrics_file") != std::string::npos);
	REQUIRE(runtime.find("s_fontSourceComplete") != std::string::npos);
	/* Slice anchors below start at the binding switch ("switch
	 * (entry->type)", the established pattern earlier in this file).
	 * Anchoring at the file's FIRST "case ASSET_FONT:" broke when the
	 * early type-support switch ("switch (type)") gained an adjacent
	 * "case ASSET_LANG:" label, shrinking the slice to two lines. */
	const size_t runtime_binding_switch = runtime.find("switch (entry->type)");
	REQUIRE(runtime_binding_switch != std::string::npos);
	{
		const size_t font_start = runtime.find("case ASSET_FONT:",
			runtime_binding_switch);
		const size_t lang_start = runtime.find("case ASSET_LANG:", font_start);
		REQUIRE(font_start != std::string::npos);
		REQUIRE(lang_start != std::string::npos);
		const std::string font_case =
			runtime.substr(font_start, lang_start - font_start);
		REQUIRE(font_case.find("entry->ext.font.font_file") !=
		        std::string::npos);
		REQUIRE(font_case.find("entry->ext.font.metrics_file") !=
		        std::string::npos);
		REQUIRE(font_case.find("s_fontSourceComplete(binding)") !=
		        std::string::npos);
		REQUIRE(font_case.find("binding->primary_path") ==
		        std::string::npos);
	}
	{
		const size_t lang_start = runtime.find("case ASSET_LANG:",
			runtime_binding_switch);
		const size_t scenario_start = runtime.find("case ASSET_SCENARIO:",
			lang_start);
		REQUIRE(lang_start != std::string::npos);
		REQUIRE(scenario_start != std::string::npos);
		const std::string lang_case =
			runtime.substr(lang_start, scenario_start - lang_start);
		REQUIRE(lang_case.find("entry->ext.lang.strings_file") !=
		        std::string::npos);
		REQUIRE(lang_case.find("entry->ext.lang.bank_id") !=
		        std::string::npos);
		REQUIRE(lang_case.find("entry->ext.lang.bank_id >= 0") !=
		        std::string::npos);
		REQUIRE(lang_case.find("binding->primary_path") ==
		        std::string::npos);
	}
	REQUIRE(examples.find("mode_key = combat") != std::string::npos);
	REQUIRE(examples.find("manifest_with(\"gamemode\"") != std::string::npos);
	REQUIRE(examples.find("\"rules_file\": \"rules.json\"") != std::string::npos);
	REQUIRE(examples.find("\"max_players\": 8") != std::string::npos);
	REQUIRE(examples.find("manifest_with(\"botprofile\"") != std::string::npos);
	REQUIRE(examples.find("\"target_body\": \"example:tri_body\"") !=
	        std::string::npos);
	REQUIRE(examples.find("\"profile_file\": \"profile.json\"") !=
	        std::string::npos);
	REQUIRE(examples.find("hud_key = ammo") != std::string::npos);
	REQUIRE(examples.find("prop_key = object") != std::string::npos);
	REQUIRE(examples.find("audio_wav_descriptor") != std::string::npos);
	REQUIRE(examples.find("audio_wav_manifest") != std::string::npos);
	REQUIRE(examples.find("pcm16_mono_wav") != std::string::npos);
	REQUIRE(examples.find("b\"RIFF\"") != std::string::npos);
	REQUIRE(examples.find("sfx_sample = pcm16_mono_wav()") != std::string::npos);
	REQUIRE(examples.find("update_audio_examples()") != std::string::npos);
	REQUIRE(examples.find("song_sequence_descriptor") != std::string::npos);
	REQUIRE(examples.find("song_sequence_manifest") != std::string::npos);
	REQUIRE(examples.find("update_song_examples()") != std::string::npos);
	REQUIRE(examples.find("\"events_file = sequence.json") != std::string::npos);
	REQUIRE(examples.find("\\\"event_count\\\": 1") != std::string::npos);
	REQUIRE(examples.find("font_bitmap_descriptor") != std::string::npos);
	REQUIRE(examples.find("font_bitmap_manifest") != std::string::npos);
	REQUIRE(examples.find("update_font_examples()") != std::string::npos);
	REQUIRE(examples.find("\"metrics_file = font.metrics.json") !=
	        std::string::npos);
	REQUIRE(examples.find("key_base = 60") != std::string::npos);
	REQUIRE(examples.find("\\\"attack_time_us\\\": 0") != std::string::npos);
	REQUIRE(examples.find("velocity_max = 127") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.stop") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.kneel") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.surrender") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.fade_out") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.remove_chr") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_sidestep") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_attack_stand") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_attacking") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_modify_attack") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.face_entity") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.apply_gset_damage") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.chr_damage_chr") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.consider_grenade_throw") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.drop_item") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_run_from_target") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_jog_to_target_prop") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_walk_to_target_prop") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_run_to_target_prop") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_go_to_cover_prop") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_jog_to_chr") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_walk_to_chr") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_run_to_chr") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_can_hear_alarm") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_patrolling") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_alarm_active") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_gas_active") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_hears_target") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_saw_injury") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_saw_death") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_los_to_target") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_los_to_attack_target") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_target_nearly_in_sight") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_nearly_in_targets_sight") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.set_pad_preset_to_pad_on_route_to_target") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_saw_target_recently") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_heard_target_recently") != std::string::npos);
	for (const char *kind : {
		     "scenario.ai.condition.if_los_to_chr",
		     "scenario.ai.condition.if_never_been_on_screen",
		     "scenario.ai.condition.if_on_screen",
		     "scenario.ai.condition.if_chr_in_on_screen_room",
		     "scenario.ai.condition.if_room_is_on_screen",
		     "scenario.ai.condition.if_target_aiming_at_me",
		     "scenario.ai.condition.if_near_miss",
		     "scenario.ai.condition.if_sees_suspicious_item",
		     "scenario.ai.condition.if_target_in_fov_left",
		     "scenario.ai.condition.if_check_fov_with_target",
		     "scenario.ai.condition.if_target_out_of_fov_left",
		     "scenario.ai.condition.if_target_in_fov",
		     "scenario.ai.condition.if_target_out_of_fov",
		     "scenario.ai.condition.if_distance_to_target_less_than",
		     "scenario.ai.condition.if_distance_to_target_greater_than",
		     "scenario.ai.condition.if_chr_distance_to_pad_less_than",
		     "scenario.ai.condition.if_chr_distance_to_pad_greater_than",
		     "scenario.ai.condition.if_distance_to_chr_less_than",
		     "scenario.ai.condition.if_distance_to_chr_greater_than",
		     "scenario.ai.condition.if_any_chr_near_self",
		     "scenario.ai.condition.if_distance_from_target_to_pad_less_than",
		     "scenario.ai.condition.if_distance_from_target_to_pad_greater_than",
		     "scenario.ai.condition.if_chr_in_room",
		     "scenario.ai.condition.if_target_in_room",
		     "scenario.ai.condition.if_chr_has_object",
		     "scenario.ai.condition.if_weapon_thrown",
		     "scenario.ai.condition.if_weapon_thrown_on_object",
		     "scenario.ai.condition.if_chr_has_weapon_equipped",
		     "scenario.ai.condition.if_gun_unclaimed",
		     "scenario.ai.condition.if_object_healthy",
		     "scenario.ai.condition.if_chr_activated_object",
		     "scenario.ai.action.obj_interact",
		     "scenario.ai.action.destroy_object",
		     "scenario.ai.action.drop_object_from_chr",
		     "scenario.ai.action.chr_drop_items",
		     "scenario.ai.action.chr_drop_weapon",
		     "scenario.ai.action.give_object_to_chr",
		     "scenario.ai.action.object_move_to_pad",
		     "scenario.ai.action.say_quip",
		     "scenario.ai.action.say_ci_staff_quip",
		     "scenario.ai.action.shuffle_ruins_pillars",
		     "scenario.ai.action.shuffle_pelagic_switches",
		     "scenario.ai.condition.if_waypoint_within_quadrant",
		     "scenario.ai.action.set_pad_preset_to_target_quadrant",
	     }) {
		REQUIRE(examples.find(kind) != std::string::npos);
	}
	REQUIRE(examples.find("scenario.ai.action.chr_do_animation") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.be_surprised_one_hand") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.be_surprised_look_around") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.be_surprised_surrender") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.random") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.kill_bond") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_random_less_than") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_random_greater_than") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.print") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.noop") != std::string::npos);
	REQUIRE(texture_extractor.find("\"empty_rom_slot = %s\\n\"") ==
	        std::string::npos);
	REQUIRE(texture_extractor.find("\\\"source_state\\\": \\\"%s\\\"") !=
	        std::string::npos);
	REQUIRE(theme.find("fsDataPathFor(rel, out, out_n)") !=
	        std::string::npos);
	REQUIRE(theme.find("(void)romExtractAllPdtheme(0)") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.skin_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.swatches_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.material_archive") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.texture_archive") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.prop.prop_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.prop.behavior_graph") !=
	        std::string::npos);
	REQUIRE(file_provider.find("FILE_PROVIDER_POOL_BYTES   (1024 * 1024)") !=
	        std::string::npos);
	REQUIRE(file_provider.find("FILE_PROVIDER_MAX_PATHS    16384") !=
	        std::string::npos);
}

TEST_CASE("typed asset extractors do not leave zip inspection artifacts",
          "[modding][pdxxx][c3842][extract][static]") {
	const std::string writer = readTextFile("port/src/asset_archive_writer.c");
	const std::string writer_h =
		readTextFile("port/include/asset_archive_writer.h");
	REQUIRE(writer.find("assetArchiveWriterAddBundledMem") !=
	        std::string::npos);
	REQUIRE(writer_h.find("assetArchiveWriterAddBundledMem") !=
	        std::string::npos);

	const std::string arena = readTextFile("port/src/romextract_pdarena.c");
	REQUIRE(arena.find("s_cleanupLegacyPdarenaZipSiblings") !=
	        std::string::npos);
	REQUIRE(arena.find("ROMEXTRACT_PDARENA_FAST_CACHE_KIND") !=
	        std::string::npos);
	REQUIRE(arena.find("ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntryPrefix") !=
	        std::string::npos);
	REQUIRE(arena.find("s_addScenarioArchiveDependency") !=
	        std::string::npos);
	REQUIRE(arena.find("\"scenario/\"") != std::string::npos);
	REQUIRE(arena.find("\"_meta/scenario/\"") != std::string::npos);
	REQUIRE(arena.find("\"_meta/_meta/\"") != std::string::npos);
	REQUIRE(arena.find("s_pdarenaOutputsCleanForFastCache") !=
	        std::string::npos);
	REQUIRE(arena.find("fast-cache blocked by stale archive") !=
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicDisk(writer, dst_name") !=
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddBundledMem(&asset_writer, \"_meta/generated-collision.json\"") !=
	        std::string::npos);
	REQUIRE(arena.find("\"scenario/_meta/generated-collision.json\"") ==
	        std::string::npos);

	const std::string mesh = readTextFile("port/src/romextract_pdmesh.c");
	REQUIRE(mesh.find("s_removeLegacyZipForMesh") != std::string::npos);
	REQUIRE(mesh.find("romextract pdmesh: removed stale typed-archive zip") !=
	        std::string::npos);
}

/* ========================================================================
 * c3849 Wave 4 Slice B -- texture boot bind verification.
 *
 * texLoad's public-source path sysFatalErrors at FIRST USE of a texture
 * whose bound .pdtexture archive is missing (mod_texture_source.c), with
 * zero boot-time detection. romExtractAllPdtexture now ends with a
 * Gate-5-style verification pass (mirrors B-908): one LOG_WARNING per
 * miss (catalog id + path) plus a "TEXTURE.BIND: n missing of m" summary.
 * These pins lock the shape: actual bound handle, "::" split, stat-only
 * cost, and execution on BOTH the emit and fast-cache skip paths.
 * ======================================================================== */

TEST_CASE("c3849 Slice B: texture bind verification is loud, cheap, and runs on both paths",
          "[modding][pdxxx][c3849][texture][bind][static]") {
	const std::string texture_extractor =
		readTextFile("port/src/romextract_pdtexture.c");

	/* The pass exists and self-identifies in the log. */
	REQUIRE(texture_extractor.find("TEXTURE.BIND") != std::string::npos);

	const std::string verify_block =
		functionBlock(texture_extractor, "static void s_verifyTextureBinds(void)");
	REQUIRE(!verify_block.empty());

	/* It checks the ACTUAL bound primary handle, not a re-derived path --
	 * a registration bug that binds the wrong path must be caught too. */
	REQUIRE(verify_block.find("fileProviderPath(e->source.primary)") !=
	        std::string::npos);
	/* Split the bound path at "::" and stat only the archive half. */
	REQUIRE(verify_block.find("strstr(archive, \"::\")") !=
	        std::string::npos);
	REQUIRE(verify_block.find("fsFileSize(archive)") != std::string::npos);
	/* Cheap by contract: no ZIP opens inside the verification pass. */
	REQUIRE(verify_block.find("modArchiveOpen") == std::string::npos);
	/* One LOG_WARNING per miss with catalog id + path, plus the summary. */
	REQUIRE(verify_block.find("LOG_WARNING") != std::string::npos);
	REQUIRE(verify_block.find("has no FileProvider primary bound") !=
	        std::string::npos);
	REQUIRE(verify_block.find("missing archive") != std::string::npos);
	REQUIRE(verify_block.find("%d missing of %d") != std::string::npos);
	REQUIRE(verify_block.find("0 missing of %d") != std::string::npos);
	/* Same bundled ASSET_TEXTURE predicate as the emit loop. */
	REQUIRE(verify_block.find("e->type != ASSET_TEXTURE") !=
	        std::string::npos);

	/* Runs on BOTH paths: the fast-cache skip early-out AND the full emit
	 * path. The skip path is the one a warm boot takes every time, so
	 * skipping verification there would defeat the point of the pass. */
	const std::string extract_all =
		functionBlock(texture_extractor, "s32 romExtractAllPdtexture");
	REQUIRE(!extract_all.empty());
	REQUIRE(countOccurrences(extract_all, "s_verifyTextureBinds();") == 2);
	requireTokenOrder(extract_all, "fast-cache)",
		"s_verifyTextureBinds();");
	requireTokenOrder(extract_all, "romExtractPdFastCacheWrite",
		"\n\ts_verifyTextureBinds();");
}

/* ========================================================================
 * c3849 Wave 4 Slice B -- unified catalog filename slug.
 *
 * The emitter names the archive on disk and registration builds the bound
 * FileProvider primary path; both sides must produce the IDENTICAL slug
 * or boot completes fine and the first texLoad of the divergent texture
 * is fatal. Slice B unified the texture pair onto the shared
 * catalogIdToFilenameSlug (port/include/assetcatalog_slug.h). The
 * remaining private copy in romextract_pdmeta.c (used by the non-texture
 * meta families that assetcatalog_base_extended.c also binds) is pinned
 * byte-identical to the shared body below.
 * ======================================================================== */

TEST_CASE("c3849 Slice B: shared catalog filename slug produces stable slugs",
          "[modding][pdxxx][c3849][slug]") {
	char out[64];

	/* Separators (':' '/' '\\') map to '_'; everything else verbatim. */
	catalogIdToFilenameSlug("base:texture_0000", out, sizeof(out));
	REQUIRE(std::string(out) == "base_texture_0000");

	catalogIdToFilenameSlug("base:weapon/falcon\\mk2", out, sizeof(out));
	REQUIRE(std::string(out) == "base_weapon_falcon_mk2");

	/* Case and dots are PRESERVED -- the slug is not lowercased, and any
	 * future "improvement" is a re-emit migration for existing installs. */
	catalogIdToFilenameSlug("Base:TeX/SubDir\\Name.01", out, sizeof(out));
	REQUIRE(std::string(out) == "Base_TeX_SubDir_Name.01");

	/* All-separator input. */
	catalogIdToFilenameSlug(":/\\", out, sizeof(out));
	REQUIRE(std::string(out) == "___");

	/* Empty and NULL ids produce the empty slug, never garbage. */
	memset(out, 'x', sizeof(out));
	catalogIdToFilenameSlug("", out, sizeof(out));
	REQUIRE(out[0] == '\0');
	memset(out, 'x', sizeof(out));
	catalogIdToFilenameSlug(NULL, out, sizeof(out));
	REQUIRE(out[0] == '\0');

	/* Long ids truncate to out_n - 1 plus NUL (catalog ids are capped at
	 * CATALOG_ID_LEN 64, same as this buffer). */
	const std::string long_id =
		"base:" + std::string(90, 'a') + ":tail";
	catalogIdToFilenameSlug(long_id.c_str(), out, sizeof(out));
	REQUIRE(std::strlen(out) == sizeof(out) - 1);
	REQUIRE(std::string(out) == "base_" + std::string(58, 'a'));

	/* Degenerate buffer sizes stay safe. */
	char tiny[1];
	tiny[0] = 'x';
	catalogIdToFilenameSlug("base:texture_0000", tiny, sizeof(tiny));
	REQUIRE(tiny[0] == '\0');
	catalogIdToFilenameSlug("base:texture_0000", NULL, 0);
}

TEST_CASE("c3849 Slice B: filename slug is unified for the texture pair and pinned elsewhere",
          "[modding][pdxxx][c3849][slug][static]") {
	const std::string slug_header =
		readTextFile("port/include/assetcatalog_slug.h");
	const std::string texture_extractor =
		readTextFile("port/src/romextract_pdtexture.c");
	const std::string base =
		readTextFile("port/src/assetcatalog_base_extended.c");
	const std::string meta = readTextFile("port/src/romextract_pdmeta.c");

	/* The texture pair (emitter archive name + bound primary path) calls
	 * the ONE shared helper; the private copies are gone. */
	REQUIRE(texture_extractor.find("#include \"assetcatalog_slug.h\"") !=
	        std::string::npos);
	REQUIRE(base.find("#include \"assetcatalog_slug.h\"") !=
	        std::string::npos);
	REQUIRE(texture_extractor.find("s_idToFilenameSlug") ==
	        std::string::npos);
	REQUIRE(base.find("s_catalogIdToFilenameSlug") == std::string::npos);
	{
		const std::string rel_path_block =
			functionBlock(texture_extractor, "static void s_archiveRelPath");
		REQUIRE(rel_path_block.find(
		                "catalogIdToFilenameSlug(id, slug, sizeof(slug))") !=
		        std::string::npos);
		const std::string member_path_block =
			functionBlock(base, "static void s_buildArchiveMemberPath");
		REQUIRE(member_path_block.find(
		                "catalogIdToFilenameSlug(id, slug, sizeof(slug))") !=
		        std::string::npos);
	}

	/* romextract_pdmeta.c keeps a private s_idToFilenameSlug for the
	 * non-texture meta families (out of Slice B's allowed surface); those
	 * archives are ALSO bound by s_buildArchiveMemberPath, so its body
	 * must stay byte-identical to the shared helper. Drift here is the
	 * same boot-fine-then-fatal failure the unification removed for
	 * textures. Fix drift by re-pointing pdmeta at assetcatalog_slug.h,
	 * not by editing either body in place. */
	const auto bracedBody = [](const std::string &text, const char *name) {
		const std::string block = functionBlock(text, name);
		const size_t brace = block.find('{');
		if (brace == std::string::npos) {
			return std::string();
		}
		return block.substr(brace);
	};
	const std::string shared_body = bracedBody(slug_header,
		"static inline void catalogIdToFilenameSlug");
	const std::string meta_body = bracedBody(meta,
		"static void s_idToFilenameSlug");
	REQUIRE(!shared_body.empty());
	REQUIRE(!meta_body.empty());
	REQUIRE(shared_body == meta_body);
}

/* ------------------------------------------------------------------------
 * c3849 Wave 6a (Unit 10) -- meta-family runtime consumers.
 *
 * Native systems now require active public-source bindings for bot profiles,
 * game modes, and catalog themes. Native mirrors and provider-path fallbacks
 * are not acceptable after extraction.
 * ---------------------------------------------------------------------- */

TEST_CASE("c3849 Wave 6a: meta-family runtime consumers feed native systems",
          "[modding][pdxxx][runtime][c3849][meta][static]") {
	const std::string mplayer = readTextFile("src/game/mplayer/mplayer.c");
	const std::string setup = readTextFile("src/game/mplayer/setup.c");
	const std::string scenarios = readTextFile("src/game/mplayer/scenarios.c");
	const std::string theme_loader =
		readTextFile("port/fast3d/pdgui_theme_loader.cpp");
	const std::string mplayer_h =
		readTextFile("src/include/game/mplayer/mplayer.h");

	/* Shared helper is declared in the header both consumers include. */
	REQUIRE(mplayer_h.find(
		"const struct asset_runtime_binding *mpBotProfileRuntimeBinding(s32 profilenum)") !=
		std::string::npos);

	/* Bot-profile lookup by permanent catalog ID requires a readable binding
	 * and fails closed instead of using g_BotProfiles as authored truth. */
	const std::string helper = functionBlock(mplayer,
		"const struct asset_runtime_binding *mpBotProfileRuntimeBindingById");
	REQUIRE(!helper.empty());
	REQUIRE(helper.find("assetCatalogResolve(profile_id)") != std::string::npos);
	REQUIRE(helper.find("assetRuntimeFindByTypeAndId(ASSET_BOT_PROFILE") !=
	        std::string::npos);
	REQUIRE(helper.find("assetRuntimePrimaryFileAccessible") !=
	        std::string::npos);
	REQUIRE(helper.find("ASSET.CHAIN: bot profile") != std::string::npos);
	REQUIRE(helper.find("CATALOG.BOTPROFILE.RUNTIME_MISS") ==
	        std::string::npos);

	/* mpCreateBotFromProfileId consumes only binding values and retains ID. */
	const std::string create =
		functionBlock(mplayer, "s32 mpCreateBotFromProfileId");
	REQUIRE(!create.empty());
	REQUIRE(create.find("mpBotProfileRuntimeBindingById(profile_id)") !=
	        std::string::npos);
	REQUIRE(create.find("bot_profile_type") != std::string::npos);
	REQUIRE(create.find("bot_profile_difficulty") != std::string::npos);
	REQUIRE(create.find("profile->target_id") != std::string::npos);
	REQUIRE(create.find("g_BotConfigsArray[botnum].profile_id") !=
	        std::string::npos);
	REQUIRE(create.find("g_BotProfiles[profilenum]") == std::string::npos);

	/* Simulant menu apply site consumes the same helper without fallback. */
	REQUIRE(setup.find("mpBotProfileRuntimeBindingById(ctx.result_id)") !=
	        std::string::npos);
	REQUIRE(setup.find("profile->bot_profile_type") != std::string::npos);
	REQUIRE(setup.find("g_BotProfiles[profnum]") == std::string::npos);

	/* B-953: gamemode selection is public-source-owned. The helper requires
	 * an active, readable rules binding; the picker consumes team and
	 * participant bounds; public name/description feed real menu text. */
	const std::string binding_helper =
		functionBlock(scenarios, "static const asset_runtime_binding_t *scenarioBindingForEntry");
	REQUIRE(!binding_helper.empty());
	REQUIRE(binding_helper.find("assetRuntimeFindByTypeAndId(ASSET_GAMEMODE") !=
	        std::string::npos);
	REQUIRE(binding_helper.find("assetRuntimePrimaryFileAccessible") !=
	        std::string::npos);
	REQUIRE(binding_helper.find("ASSET.CHAIN: gamemode") !=
	        std::string::npos);
	REQUIRE(binding_helper.find("ext.gamemode") == std::string::npos);

	const std::string accepts =
		functionBlock(scenarios, "static bool scenarioCtxAccepts");
	REQUIRE(!accepts.empty());
	REQUIRE(accepts.find("scenarioBindingForEntry") != std::string::npos);
	REQUIRE(accepts.find("gamemode_team_based") != std::string::npos);
	REQUIRE(accepts.find("mpGetActiveParticipantCount") != std::string::npos);
	REQUIRE(accepts.find("gamemode_min_players") != std::string::npos);
	REQUIRE(accepts.find("gamemode_max_players") != std::string::npos);
	REQUIRE(accepts.find("ext.gamemode") == std::string::npos);
	REQUIRE(scenarios.find("binding->gamemode_name") != std::string::npos);
	REQUIRE(scenarios.find("binding->gamemode_description") !=
	        std::string::npos);
	REQUIRE(scenarios.find(
		"(uintptr_t)&mpMenuTextScenarioDescription") != std::string::npos);
	REQUIRE(scenarios.find("CATALOG.GAMEMODE.RUNTIME_MISS") ==
	        std::string::npos);

	/* theme: enabled catalog-only rows require a binding and never resolve a
	 * parallel FileProvider path directly. */
	const std::string theme_reg =
		functionBlock(theme_loader, "register_catalog_theme_entry");
	REQUIRE(!theme_reg.empty());
	REQUIRE(theme_reg.find("assetRuntimeFindByTypeAndId(ASSET_THEME") !=
	        std::string::npos);
	REQUIRE(theme_reg.find("binding->primary_path") != std::string::npos);
	REQUIRE(theme_reg.find("binding->authored_file") != std::string::npos);
	REQUIRE(theme_reg.find("ASSET.CHAIN: enabled theme") !=
	        std::string::npos);
	REQUIRE(theme_reg.find("CATALOG.THEME.RUNTIME_MISS") ==
	        std::string::npos);
	REQUIRE(theme_reg.find("fileProviderPath(entry->source.primary)") ==
	        std::string::npos);
}

TEST_CASE("B-959 HUD material skin and vehicle public source owns production behavior",
          "[modding][pdxxx][b959][static]") {
	const std::string loader = readTextFile("port/src/assetcatalog_load.c");
	const std::string runtime = readTextFile("port/src/asset_runtime.c");
	const std::string conformance =
		readTextFile("tools/asset_archive_conformance.py");
	const std::string bondgun = readTextFile("src/game/bondgun.c");
	const std::string player = readTextFile("src/game/player.c");
	const std::string radar = readTextFile("src/game/radar.c");
	const std::string hud = readTextFile("port/fast3d/pdgui_hud.cpp");
	const std::string propobj = readTextFile("src/game/propobj.c");
	const std::string bondbike = readTextFile("src/game/bondbike.c");
	const std::string chr = readTextFile("src/game/chr.c");
	const std::string forge = readTextFile("port/src/forge/forge_runtime.c");
	const std::string scenarios = readTextFile("src/game/mplayer/scenarios.c");
	const std::string mplayer = readTextFile("src/game/mplayer/mplayer.c");

	REQUIRE(loader.find("assetRuntimeHydrateCatalogEntry(entry)") !=
	        std::string::npos);
	REQUIRE(runtime.find("binding->active = 0") != std::string::npos);
	REQUIRE(runtime.find("if (!binding || !action) return 0;") !=
	        std::string::npos);
	REQUIRE(runtime.find("case ASSET_PROP:     ok = s_hydrateProp(binding);") !=
	        std::string::npos);
	REQUIRE(runtime.find("case ASSET_GAMEMODE: ok = s_hydrateGamemode(binding);") !=
	        std::string::npos);
	REQUIRE(runtime.find(
		"case ASSET_BOT_PROFILE: ok = s_hydrateBotProfile(binding);") !=
	        std::string::npos);

	REQUIRE(bondgun.find("assetRuntimeHudElementEnabled(HUD_ELEM_AMMO)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("assetRuntimeHudElementEnabled(HUD_ELEM_CROSSHAIR)") !=
	        std::string::npos);
	REQUIRE(player.find("assetRuntimeHudElementEnabled(HUD_ELEM_HEALTH)") !=
	        std::string::npos);
	REQUIRE(radar.find("assetRuntimeHudElementEnabled(HUD_ELEM_RADAR)") !=
	        std::string::npos);
	REQUIRE(hud.find("assetRuntimeHudElement(HUD_ELEM_SCORE)") !=
	        std::string::npos);
	REQUIRE(hud.find("assetRuntimeHudElement(HUD_ELEM_TIMER)") !=
	        std::string::npos);

	REQUIRE(propobj.find("assetRuntimeVehicleForModelnum") !=
	        std::string::npos);
	REQUIRE(propobj.find("assetRuntimeVehicleHover") != std::string::npos);
	REQUIRE(propobj.find("assetRuntimeVehicleAllows(obj->modelnum, \"mount\")") !=
	        std::string::npos);
	REQUIRE(bondbike.find(
		"assetRuntimeVehicleAllows(vehicle->modelnum, \"dismount\")") !=
	        std::string::npos);
	REQUIRE(chr.find("assetRuntimeSkinAppearance") != std::string::npos);
	REQUIRE(forge.find("source->prop_health * 10.0f") != std::string::npos);
	REQUIRE(forge.find("source->prop_flags") != std::string::npos);
	REQUIRE(scenarios.find("!binding || !binding->source_hydrated") !=
	        std::string::npos);
	REQUIRE(mplayer.find("!binding || !binding->source_hydrated") !=
	        std::string::npos);

	REQUIRE(conformance.find("pd2.prop.v2") != std::string::npos);
	REQUIRE(conformance.find("pd2.gamemode.rules.v2") != std::string::npos);
	REQUIRE(conformance.find("pd2.botprofile.v2") != std::string::npos);
	REQUIRE(conformance.find("validate_vehicle_source_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2.vehicle.physics.v2") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2.vehicle.behavior.v2") !=
	        std::string::npos);
	REQUIRE(conformance.find("validate_hud_source_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2.hud.layout.v1") != std::string::npos);
	REQUIRE(conformance.find("validate_material_source_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2.material.v1") != std::string::npos);
	REQUIRE(conformance.find("validate_skin_source_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2.skin.v1") != std::string::npos);
	REQUIRE(conformance.find("pd2.skin.swatches.v1") !=
	        std::string::npos);
}
