#include "catch.hpp"

#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string readSource(const char *path)
{
	std::ifstream input(path, std::ios::binary);
	REQUIRE(input.good());
	return std::string(std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

size_t occurrences(const std::string &text, const std::string &needle)
{
	size_t count = 0;
	size_t position = 0;
	while ((position = text.find(needle, position)) != std::string::npos) {
		count++;
		position += needle.size();
	}
	return count;
}

std::string functionBody(const std::string &source, const std::string &name,
	const std::string &next)
{
	const auto begin = source.find(name);
	const auto end = source.find(next, begin + name.size());
	REQUIRE(begin != std::string::npos);
	REQUIRE(end != std::string::npos);
	return source.substr(begin, end - begin);
}

} // namespace

TEST_CASE("saved Scenario load is parse plan commit with one publication site",
	"[v006][b1068][scenario][transaction][static]")
{
	const std::string source = readSource("port/src/scenario_save.c");
	const std::string load = functionBody(source, "s32 scenarioLoad(",
		"s32 scenarioDelete(");

	const auto parse = load.find("scenarioDocumentParse(");
	const auto plan = load.find("scenarioBuildPlan(");
	const auto commit = load.find("scenarioCommitPlan(");
	REQUIRE(parse < plan);
	REQUIRE(plan < commit);
	REQUIRE(occurrences(source, "g_MatchConfig =") == 1);
	REQUIRE(source.find("g_MatchConfig = plan->config") != std::string::npos);
	REQUIRE(source.find("mpCommitPreparedWeaponSet(plan->resolved_weapon_set")
		!= std::string::npos);
	REQUIRE(load.find("matchConfigInit(") == std::string::npos);
	REQUIRE(load.find("matchConfigAddBotWithProfile(") == std::string::npos);
}

TEST_CASE("saved Scenario parser and planner reject legacy substring mutation",
	"[v006][b1068][scenario][parse][static]")
{
	const std::string source = readSource("port/src/scenario_save.c");
	const std::string codec = readSource("port/src/scenario_codec.c");

	REQUIRE(source.find("jsonFind") == std::string::npos);
	REQUIRE(source.find("strstr(") == std::string::npos);
	REQUIRE(codec.find("strstr(") == std::string::npos);
}

TEST_CASE("saved Scenario arena planning cannot cross stage index domains",
	"[v006][b1068][scenario][index-domain][static]")
{
	const std::string source = readSource("port/src/scenario_save.c");
	const std::string arena = functionBody(source,
		"static s32 scenarioResolveArena(",
		"static s32 scenarioResolveGameMode(");
	const std::string harness = readSource("port/src/v006_scenario_harness.c");

	REQUIRE(source.find("assetCatalogResolveStageIndex(") == std::string::npos);
	REQUIRE(source.find("catalogStageIdByStageTableIndex(") == std::string::npos);
	REQUIRE(source.find("catalogStageIdBySoloStageIndex(") == std::string::npos);
	REQUIRE(source.find("g_StageIndex") == std::string::npos);
	REQUIRE(source.find("g_SoloStages") == std::string::npos);
	REQUIRE(source.find("catalogResolveStageForType(document->arena_id")
		!= std::string::npos);
	REQUIRE(arena.find("catalog_stage_result_t stage = {0};")
		!= std::string::npos);
	REQUIRE(occurrences(arena, "catalogResolveStageForType(") == 2);
	const auto legacy_find = arena.find(
		"scenarioFindLegacyUnique(ASSET_ARENA");
	const auto legacy_stage = arena.find(
		"catalogResolveStageForType(entry->id, ASSET_ARENA, &stage)",
		legacy_find);
	const auto scenario_source = arena.find(
		"scenarioSourceFindEntryForStage(&stage, 1)", legacy_stage);
	REQUIRE(legacy_find < legacy_stage);
	REQUIRE(legacy_stage < scenario_source);
	REQUIRE(arena.find("stage.entry != entry", legacy_stage)
		!= std::string::npos);
	REQUIRE(arena.find("stage.stagenum != document->legacy_arena", legacy_stage)
		!= std::string::npos);
	REQUIRE(arena.find("stage.stagenum != entry->ext.arena.stagenum",
		legacy_stage) != std::string::npos);
	REQUIRE(arena.find("stage.net_hash != entry->net_hash", legacy_stage)
		!= std::string::npos);
	REQUIRE(arena.find("stage.stagenum = entry->ext.arena.stagenum")
		== std::string::npos);
	REQUIRE(harness.find("v1_numeric_migration") != std::string::npos);
	REQUIRE(source.find("scenarioSourceFindEntryForStage(&stage, 1)")
		!= std::string::npos);
	REQUIRE(source.find("assetRuntimeFindByTypeAndId(ASSET_SCENARIO, scenario->id)")
		!= std::string::npos);
	REQUIRE(source.find("scenario->ext.scenario.stagenum != stage.stagenum")
		!= std::string::npos);
	REQUIRE(source.find("activated public ASSET_SCENARIO source is not runtime-ready")
		!= std::string::npos);
	REQUIRE(source.find("entry->ext.arena.stagenum") != std::string::npos);
	REQUIRE(source.find("entry->ext.gamemode.mode_id") != std::string::npos);
	REQUIRE(source.find("assetRuntimeFindByTypeAndId(ASSET_GAMEMODE, entry->id)")
		!= std::string::npos);
	REQUIRE(source.find("runtime->source_hydrated") != std::string::npos);
	REQUIRE(source.find("assetRuntimePrimaryFileAccessible(runtime)")
		!= std::string::npos);
	REQUIRE(source.find("ASSET_GAMEMODE public source is not runtime-ready")
		!= std::string::npos);
	REQUIRE(source.find("ASSET_MAP") == std::string::npos);
	REQUIRE(source.find("RomProvider") == std::string::npos);
	REQUIRE(source.find("romProvider") == std::string::npos);
}

TEST_CASE("saved Scenario lazy source activation restores exact owner state",
	"[v006][b1068][scenario][source][lifecycle][rollback][static]")
{
	const std::string source = readSource("port/src/scenario_save.c");
	const std::string arena = functionBody(source,
		"static s32 scenarioResolveArena(",
		"static s32 scenarioResolveGameMode(");
	const std::string cleanup = functionBody(source,
		"static s32 scenarioReleasePlanSource(",
		"static const asset_entry_t *scenarioFindLegacyUnique(");
	const std::string plan = functionBody(source,
		"static s32 scenarioBuildPlan(",
		"static void scenarioCommitPlan(");

	REQUIRE(source.find("#include \"assetcatalog_load.h\"")
		!= std::string::npos);
	REQUIRE(source.find("#include \"catalog_activation_ledger.h\"")
		!= std::string::npos);
	REQUIRE(source.find("#include \"catalog_stage_ownership.h\"")
		!= std::string::npos);
	REQUIRE(arena.find("scenarioCaptureSourceOwnership(")
		!= std::string::npos);
	REQUIRE(arena.find(
		"catalogCanDeactivateTypedAsset(ASSET_SCENARIO, scenario->id)")
		!= std::string::npos);
	REQUIRE(arena.find("catalogLoadStageAsset(ASSET_SCENARIO, scenario->id)")
		!= std::string::npos);
	REQUIRE(arena.find("plan->source_stage_ref_acquired = 1")
		!= std::string::npos);
	REQUIRE(arena.find("public ASSET_SCENARIO source activation failed")
		!= std::string::npos);
	REQUIRE(cleanup.find("if (plan->source_stage_ref_acquired)")
		!= std::string::npos);
	REQUIRE(cleanup.find("catalogReleaseStageAsset(ASSET_SCENARIO")
		!= std::string::npos);
	REQUIRE(cleanup.find("scenarioSourceOwnershipMatches(")
		!= std::string::npos);
	REQUIRE(plan.find("scenarioBuildPlanCandidate(") <
		plan.find("scenarioReleasePlanSource("));
	REQUIRE(plan.find("return candidate_ok && ownership_ok;")
		!= std::string::npos);
	REQUIRE(occurrences(source, "catalogReleaseStageAsset(ASSET_SCENARIO") == 1);
	REQUIRE(source.find("catalogLoadTypedAsset(ASSET_SCENARIO")
		== std::string::npos);
	const std::string harness = readSource("port/src/v006_scenario_harness.c");
	REQUIRE(harness.find("lazy_activation_success_restores_ownership")
		!= std::string::npos);
	REQUIRE(harness.find("lazy_activation_failure_rollback")
		!= std::string::npos);
	REQUIRE(harness.find("already_owned_source_preserved")
		!= std::string::npos);
	REQUIRE(harness.find("v006ScenarioSourceOwnerMatches(&before)")
		!= std::string::npos);
}

TEST_CASE("saved Scenario typed fields are authoritative before bounded migration",
	"[v006][b1068][scenario][migration][static]")
{
	const std::string source = readSource("port/src/scenario_save.c");
	const std::string codec = readSource("port/src/scenario_codec.c");

	REQUIRE(source.find("if ((document->present & SCENARIO_DOC_ARENA_ID) != 0)")
		!= std::string::npos);
	REQUIRE(source.find("scenarioFindLegacyUnique(ASSET_ARENA")
		!= std::string::npos);
	REQUIRE(source.find("if (typed_count == NUM_MPWEAPONSLOTS)")
		!= std::string::npos);
	REQUIRE(source.find("else if (legacy_count != 0)") != std::string::npos);
	REQUIRE(source.find("v1 weapon set cannot be migrated unambiguously")
		!= std::string::npos);
	REQUIRE(codec.find("v3 legacy hybrid companion set is partial")
		!= std::string::npos);
	REQUIRE(codec.find("v1 saved Scenario contains fields outside the known writer shape")
		!= std::string::npos);
	REQUIRE(codec.find("v2 saved Scenario has a partial or unknown weapon shape")
		!= std::string::npos);
	REQUIRE(codec.find("v2 bot %d has a partial or unknown identity shape")
		!= std::string::npos);
	REQUIRE(source.find("scenarioValidateTypedCompanions(")
		!= std::string::npos);
	REQUIRE(source.find("numeric arena companion contradicts typed ASSET_ARENA")
		!= std::string::npos);
	REQUIRE(source.find("numeric scenario companion contradicts typed ASSET_GAMEMODE")
		!= std::string::npos);
	REQUIRE(source.find("numeric weapon companion contradicts typed ASSET_WEAPON")
		!= std::string::npos);
}

TEST_CASE("saved Scenario planner prepares every bot identity before publication",
	"[v006][b1068][scenario][identity][rollback][static]")
{
	const std::string source = readSource("port/src/scenario_save.c");
	const std::string plan = functionBody(source,
		"static s32 scenarioBuildPlanCandidate(",
		"static void scenarioCommitPlan(");
	const std::string harness = readSource("port/src/v006_scenario_harness.c");

	REQUIRE(source.find("#include \"player_identity.h\"") != std::string::npos);
	REQUIRE(plan.find("playerIdentityPrepare(body_id, head_id, &identity)")
		!= std::string::npos);
	REQUIRE(plan.find("identity_status != PLAYER_IDENTITY_OK")
		!= std::string::npos);
	REQUIRE(plan.find("slot->body_id, identity.body_id") != std::string::npos);
	REQUIRE(plan.find("slot->head_id, identity.head_id") != std::string::npos);
	REQUIRE(plan.find("slot->bodynum = identity.mp_body_index")
		!= std::string::npos);
	REQUIRE(plan.find("slot->headnum = identity.mp_head_index")
		!= std::string::npos);
	REQUIRE(harness.find("incompatible_rig_preserves_state")
		!= std::string::npos);
	REQUIRE(harness.find("v2_invalid_typed_identity_preserves_state")
		!= std::string::npos);
}

TEST_CASE("active mission identity accessor is read-only committed graph state",
	"[v006][scenario-source][mission][authority][static]")
{
	const std::string header = readSource("port/include/scenario_source_runtime.h");
	const std::string source = readSource("port/src/scenario_source_runtime.c");
	const std::string accessor = functionBody(source,
		"const char *scenarioSourceActiveMissionId(void)",
		"void scenarioSourceFatalRuntimeFallbackForStage(");

	REQUIRE(header.find("const char *scenarioSourceActiveMissionId(void);")
		!= std::string::npos);
	REQUIRE(accessor.find("s_ActiveScenarioGraphs.mission_graph_active")
		!= std::string::npos);
	REQUIRE(accessor.find("s_ActiveScenarioGraphs.mission_id")
		!= std::string::npos);
	REQUIRE(accessor.find("assetCatalogResolve") == std::string::npos);
	REQUIRE(accessor.find("s_findMissionForScenario") == std::string::npos);
}

TEST_CASE("saved Scenario planner requires exact preserved human count",
	"[v006][b1068][scenario][participants][rollback][static]")
{
	const std::string source = readSource("port/src/scenario_save.c");
	REQUIRE(source.find("if (actual_humans != human_count)")
		!= std::string::npos);
	REQUIRE(source.find("live human slots do not match declared human count")
		!= std::string::npos);
	const std::string harness = readSource("port/src/v006_scenario_harness.c");
	REQUIRE(harness.find("human_count_mismatch_preserves_state")
		!= std::string::npos);
	REQUIRE(harness.find("snapshot->players") != std::string::npos);
	REQUIRE(harness.find("snapshot->bots") != std::string::npos);
}

TEST_CASE("saved Scenario read and write boundaries are exact and atomic",
	"[v006][b1068][scenario][save][static]")
{
	const std::string source = readSource("port/src/scenario_save.c");
	const std::string save = functionBody(source, "s32 scenarioSave(",
		"s32 scenarioLoad(");
	const std::string load = functionBody(source, "s32 scenarioLoad(",
		"s32 scenarioDelete(");

	REQUIRE(load.find("nread != (size_t)filesize || ferror(fp)")
		!= std::string::npos);
	REQUIRE(load.find("filesize > (long)SCENARIO_CODEC_MAX_BYTES")
		!= std::string::npos);
	REQUIRE(save.find("scenarioDocumentFromLive(") != std::string::npos);
	REQUIRE(save.find("saveAtomicBegin(") != std::string::npos);
	REQUIRE(save.find("saveAtomicCommit(") != std::string::npos);
	REQUIRE(save.find("saveAtomicAbort(") != std::string::npos);
	REQUIRE(save.find("\\\"arena\\\"") == std::string::npos);
	REQUIRE(save.find("\\\"scenario\\\"") == std::string::npos);
	REQUIRE(save.find("\\\"weapon%d\\\"") == std::string::npos);
}

TEST_CASE("all saved Scenario UI callers publish only after success",
	"[v006][b1068][scenario][callers][static]")
{
	const std::string room = readSource("port/fast3d/pdgui_menu_room.cpp");
	REQUIRE(occurrences(room, "scenarioLoad(fullPath, humanCount)") == 2);
	REQUIRE(occurrences(room,
		"if (scenarioLoad(fullPath, humanCount) == 0) {") == 2);
	REQUIRE(occurrences(room, "syncArenaFromConfig();") >= 2);
	REQUIRE(occurrences(room, "syncSpawnWeaponFromConfig();") >= 2);
}

TEST_CASE("saved Scenario save and production start callers check verdicts",
	"[v006][b1068][scenario][callers][static]")
{
	const std::string room = readSource("port/fast3d/pdgui_menu_room.cpp");
	const std::string boot = readSource("port/src/main.c");
	const std::string scenarios = readSource("port/src/testscenarios.c");

	REQUIRE(occurrences(room, "scenarioSave(s_SaveNameBuf)") == 1);
	REQUIRE(room.find("if (scenarioSave(s_SaveNameBuf) == 0) {")
		!= std::string::npos);
	REQUIRE(room.find("if (matchStart() != 0) {") != std::string::npos);
	REQUIRE(boot.find("if (matchStart() != 0) {") != std::string::npos);
	REQUIRE(scenarios.find("if (matchStart() != 0) {") != std::string::npos);
}

TEST_CASE("Room Load Preset and Quick Start publish through the Room's own paths",
	"[scenario][room][preset][quick-start][static]")
{
	const std::string source = readSource("port/src/scenario_save.c");
	const std::string room = readSource("port/fast3d/pdgui_menu_room.cpp");
	const std::string mplayer = readSource("src/game/mplayer/mplayer.c");

	/* A preset is the original's table entry as a Room document, through the
	 * same plan and commit as a saved setup: no second publication site. */
	const std::string preset = functionBody(source, "s32 scenarioLoadPreset(",
		"s32 scenarioListFiles(");
	const auto config = preset.find("challengeLoadConfig(confignum, buffer, sizeof(buffer))");
	const auto plan = preset.find("scenarioBuildPlan(&document, humanCount, &plan, detail,");
	const auto commit = preset.find("scenarioCommitPlan(&plan);");
	REQUIRE(config != std::string::npos);
	REQUIRE(plan != std::string::npos);
	REQUIRE(commit != std::string::npos);
	REQUIRE(config < plan);
	REQUIRE(plan < commit);
	REQUIRE(preset.find("g_MatchConfig =") == std::string::npos);
	REQUIRE(preset.find("document.options = setup->options | MPOPTION_BOTJUMP;")
		!= std::string::npos);
	REQUIRE(preset.find("challengeIsFeatureUnlocked(MPFEATURE_WEAPON_SHIELD)")
		!= std::string::npos);
	REQUIRE(preset.find("sim->difficulties[diffindex]") != std::string::npos);
	REQUIRE(mplayer.find("s32 mpGetPresetConfigNumBySlot(s32 slot)") != std::string::npos);

	/* The Room: Load Preset at the top of the Combat Simulator tab, host only,
	 * publishing only after success; Quick Start replaces the simulants by the
	 * original's Quick Team rules and starts through the Room's Start Match. */
	const std::string load = functionBody(room, "static void roomLoadPreset(",
		"#define ROOM_QS_PLAYERS_ONLY");
	REQUIRE(load.find("if (scenarioLoadPreset(slot, humanCount) == 0) {") != std::string::npos);
	REQUIRE(load.find("syncArenaFromConfig();") != std::string::npos);
	REQUIRE(load.find("s_RoomSettingsDirty = true;") != std::string::npos);
	REQUIRE(room.find("roomCsSectionPrepareFocus(ROOM_CS_PRESET);") != std::string::npos);
	REQUIRE(room.find("roomCsSectionPrepareFocus(ROOM_CS_PRESET);")
		< room.find("roomCsSectionPrepareFocus(ROOM_CS_ARENA);"));
	const std::string quick = functionBody(room, "static int roomQuickStartApply(",
		"static int roomQuickStartMaxSims(");
	REQUIRE(quick.find("matchConfigRemoveSlot(i);") != std::string::npos);
	REQUIRE(quick.find("matchConfigSetUserOption(MPOPTION_TEAMSENABLED, teams ? 1 : 0);")
		!= std::string::npos);
	REQUIRE(quick.find("matchConfigApplyScenarioRules(\"quick-start\");") != std::string::npos);
	REQUIRE(room.find("if (autoStart || quickStartFire || ImGui::Button(\"Start Match\"")
		!= std::string::npos);
	REQUIRE(occurrences(room, "menuGraphFireSceneOp(MENU_TYPE_ROOM, \"start_match\"") == 1);
	/* Challenges stay without a Room entry (Mike 2026-11-21). */
	REQUIRE(room.find("g_MpChallengesMenuDialog") == std::string::npos);
}

TEST_CASE("Room preset names drop the language file's line terminator (B-1295)",
	"[scenario][room][preset][static]")
{
	const std::string source = readSource("port/src/scenario_save.c");
	const std::string room = readSource("port/fast3d/pdgui_menu_room.cpp");
	const std::string mplayer = readSource("src/game/mplayer/mplayer.c");

	/* The language file ends each menu item with a line feed ("No Shield\n"):
	 * the Room's list, its logs and the preset's Room document take the name
	 * up to it. */
	const std::string copy = functionBody(mplayer, "void mpCopyPresetNameBySlot(",
		"#if VERSION >= VERSION_NTSC_1_0");
	REQUIRE(copy.find("name[len] != '\\n'") != std::string::npos);
	REQUIRE(copy.find("out[len] = '\\0';") != std::string::npos);
	const std::string preset = functionBody(source, "s32 scenarioLoadPreset(",
		"s32 scenarioListFiles(");
	REQUIRE(preset.find("mpCopyPresetNameBySlot(slot, document.name, (s32)sizeof(document.name));")
		!= std::string::npos);
	REQUIRE(preset.find("mpGetPresetNameBySlot(") == std::string::npos);
	REQUIRE(room.find("mpGetPresetNameBySlot(") == std::string::npos);
	REQUIRE(occurrences(room, "mpCopyPresetNameBySlot(") >= 4);
}

TEST_CASE("Room smoke hooks press Load Preset and Quick Start through the Room's own calls",
	"[scenario][room][preset][quick-start][static]")
{
	const std::string room = readSource("port/fast3d/pdgui_menu_room.cpp");

	/* The hooks call what the preset list and the Quick Start popup call. */
	const std::string run = functionBody(room, "static void roomDebugSetupRun(",
		"/* =====");
	REQUIRE(run.find("roomLoadPreset(s_DebugPresetSlot);") != std::string::npos);
	REQUIRE(run.find("roomQuickStartBegin(s_DebugQuickMode, sims, s_DebugQuickDiff);")
		!= std::string::npos);
	REQUIRE(room.find("roomQuickStartBegin(s_QuickStartMode, s_QuickStartSims, s_QuickStartDiff);")
		!= std::string::npos);
	REQUIRE(occurrences(room, "s_QuickStartFireFrames = 2;") == 1);
	/* The debug Start Match waits until the hooks' setup has gone in, and the
	 * hooks run after the frame's Start Match, so a start comes a frame later. */
	REQUIRE(room.find("const bool bootAutoStart = !debugTabPending && !debugSetupPending")
		!= std::string::npos);
	const auto button = room.find("if (autoStart || quickStartFire || ImGui::Button(\"Start Match\"");
	const auto hooks = room.find("roomDebugSetupRun();");
	REQUIRE(button != std::string::npos);
	REQUIRE(hooks != std::string::npos);
	REQUIRE(button < hooks);
}
