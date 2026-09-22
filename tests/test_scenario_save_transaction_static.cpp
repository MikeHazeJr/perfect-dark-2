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
