/*
 * Static pins for c3842: public typed-archive source files are the native
 * client source, while generated products are cache only.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readTextFile(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
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
	REQUIRE(guard.find("c3842-s7") != std::string::npos);
	REQUIRE(guard.find("c3842-s8") != std::string::npos);
	REQUIRE(guard.find("c3842-s9") != std::string::npos);

	const std::string conformance =
		readTextFile("tools/asset_archive_conformance.py");
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
	REQUIRE(conformance.find("navigation/waypoints.tsv") !=
	        std::string::npos);
	REQUIRE(conformance.find("decoded waypoint graph source") !=
	        std::string::npos);
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
	REQUIRE(tasks.find("Asset Pipeline native-source correction") !=
	        std::string::npos);
	REQUIRE(tasks.find("c3842") != std::string::npos);
	REQUIRE(tasks.find("c3844") != std::string::npos);

	const std::string kanban = readTextFile("tools/kanban/state.json");
	REQUIRE(kanban.find("\"id\": \"c3842\"") != std::string::npos);
	REQUIRE(kanban.find("\"priority\": 1") != std::string::npos);
	REQUIRE(kanban.find("\"id\": \"c3842-s7\"") != std::string::npos);
	REQUIRE(kanban.find("\"id\": \"c3842-s8\"") != std::string::npos);
	REQUIRE(kanban.find("\"id\": \"c3842-s9\"") != std::string::npos);
	REQUIRE(kanban.find("definitive optional-slot") != std::string::npos);
}

TEST_CASE("runtime ROM fallback is tracked as an asset-chain failure",
          "[modding][pdxxx][c3844][static]") {
	const std::string constraints = readTextFile("context/constraints.md");
	const std::string tasks = readTextFile("context/tasks.md");
	const std::string modding = readTextFile("context/pillars/modding.md");
	const std::string catalog = readTextFile("context/pillars/catalog.md");
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");
	const std::string kanban = readTextFile("tools/kanban/state.json");

	REQUIRE(constraints.find("Runtime ROM fallback is an asset-chain failure") !=
	        std::string::npos);
	REQUIRE(constraints.find("system/ecosystem failure") != std::string::npos);
	REQUIRE(tasks.find("Runtime ROM fallback is an asset-chain failure") !=
	        std::string::npos);
	REQUIRE(modding.find("Runtime ROM fallback is an asset-chain failure") !=
	        std::string::npos);
	REQUIRE(catalog.find("runtime ROM/RomProvider fallback after extraction is an asset-chain failure") !=
	        std::string::npos);
	REQUIRE(guard.find("c3844 must explicitly track ROM fallback") !=
	        std::string::npos);
	REQUIRE(kanban.find("\"id\": \"c3844\"") != std::string::npos);
	REQUIRE(kanban.find("Asset Pipeline: make runtime ROM fallback a hard failure") !=
	        std::string::npos);
	REQUIRE(kanban.find("\"id\": \"c3844-s1\"") != std::string::npos);
}

TEST_CASE("scenario stage payloads reject ROM fallback in source-only mode",
          "[modding][pdxxx][c3844][scenario][source_gate][static]") {
	const std::string header = readTextFile("port/include/asset_source_debug.h");
	const std::string debug = readTextFile("port/src/asset_source_debug.c");
	const std::string setup = readTextFile("src/game/setup.c");
	const std::string tiles = readTextFile("src/game/tilesreset.c");
	const std::string lv = readTextFile("src/game/lv.c");
	const std::string scenario_runtime =
		readTextFile("port/src/scenario_source_runtime.c");
	const std::string scenario_runtime_h =
		readTextFile("port/include/scenario_source_runtime.h");
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
	REQUIRE(setup.find("\"mp setup\"") != std::string::npos);
	REQUIRE(setup.find("\"setup\"") != std::string::npos);
	REQUIRE(setup.find("scenarioSourceLoadSetupForStage(&stage") !=
	        std::string::npos);
	REQUIRE(setup.find("scenarioSourceActivateGraphsForStage(&stage") !=
	        std::string::npos);
	REQUIRE(setup.find("if (!g_GeCreditsData)") != std::string::npos);
	REQUIRE(setup.find("setupRequireScenarioSourceHandle(\"pads\"") !=
	        std::string::npos);
	REQUIRE(setup.find("scenarioSourceLoadPadsForStage(&stage") !=
	        std::string::npos);

	REQUIRE(scenario_walker.find("e->ext.scenario.pads_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"pads\", \"pads.tsv\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("e->ext.scenario.objects_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"objects\", \"objects.tsv\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("e->ext.scenario.setup_fields_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"setup_fields\", \"setup.fields.tsv\"") !=
	        std::string::npos);

	REQUIRE(scenario_runtime.find("fsFileLoad(pads_path") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_parsePadsTsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_parseWaypointsTsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("navigation/waypoints.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("struct padsfileheader") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("struct waypoint") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("PADFLAG_HASBBOXDATA") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: compiled pads.tsv") !=
	        std::string::npos);
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
	REQUIRE(scenario_runtime.find("backend=graph.trigger.volumes+volumes.tsv") !=
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
	REQUIRE(scenario_runtime.find("backend=graph.trigger.volumes+level.graph.nodes+volumes.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: global settings source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.global.settings+level.graph.nodes") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("level_global_settings_node_count") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.trigger.volume.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.global.settings.source") !=
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
	REQUIRE(scenario_runtime.find("backend=graph.setup.links+setup.fields.tsv") !=
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
	REQUIRE(meta_extractor.find("PDMETA_FAST_CACHE_KIND \"pdmeta_table_backed_v6\"") !=
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
	REQUIRE(meta_extractor.find("dependencies/assets/scenarios/%s.pdscenario::objectives.tsv#%s") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("operand_kind\\ttarget_ref\\ttarget_record_ref\\tpad_ref\\tstate_ref\\tmatch_value\\tinitial_status") !=
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
	REQUIRE(conformance.find("level.graph.json must include global settings graph nodes") !=
	        std::string::npos);
	REQUIRE(conformance.find("mission.objective.criteria.source") !=
	        std::string::npos);
	REQUIRE(conformance.find("objectives.tsv still points at original_perfect_dark_setup") !=
	        std::string::npos);
	REQUIRE(conformance.find("definitive objective source columns") !=
	        std::string::npos);
	REQUIRE(conformance.find("objective_step.argument") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupParseFieldsTsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("setup.fields.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario_source_setup_link_t") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupCollectBehaviorLinkSource") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("objects.tsv") != std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: compiled setup.fields.tsv") !=
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
	REQUIRE(scenario_runtime.find("offsetof(struct tag, cmdoffset)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("offsetof(struct textoverride, objoffset)") !=
	        std::string::npos);
	REQUIRE(setup.find("scenarioSourceSetupGraphRecordBehaviorLink(") !=
	        std::string::npos);
	REQUIRE(setup.find("OBJTYPE_LINKGUNS, index") != std::string::npos);
	REQUIRE(setup.find("OBJTYPE_LINKLIFTDOOR, index") !=
	        std::string::npos);
	REQUIRE(setup.find("OBJTYPE_SAFEITEM, index") != std::string::npos);
	REQUIRE(setup.find("OBJTYPE_PADLOCKEDDOOR, index") !=
	        std::string::npos);
	REQUIRE(setup.find("OBJTYPE_CONDITIONALSCENERY, index") !=
	        std::string::npos);
	REQUIRE(setup.find("OBJTYPE_BLOCKEDPATH, index") !=
	        std::string::npos);
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
	REQUIRE(mesh_extractor.find("pdmesh_model_obj_mtx_v10_skeleton_allmodels") !=
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
	REQUIRE(scenario_runtime_h.find("scenarioSourceFindEntryForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceFindEntryForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("category_exact") != std::string::npos);
	REQUIRE(scenario_runtime.find("s_findScenarioByDerivedStageId") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("assetCatalogResolve(scenario_id)") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("_PD_ROOM") != std::string::npos);
	REQUIRE(scenario_extractor.find("roomnum") != std::string::npos);
	REQUIRE(modasset_compiler.find("gltfApplyRoomTags") != std::string::npos);
	REQUIRE(modasset_compiler.find("_PD_ROOM") != std::string::npos);
	REQUIRE(modasset_compiler.find("tri->roomnum") != std::string::npos);

	REQUIRE(tiles.find("asset_source_debug.h") != std::string::npos);
	REQUIRE(tiles.find("scenarioSourceLoadTilesForStage(&stage") !=
	        std::string::npos);
	REQUIRE(tiles.find("TILES: using scenario source scene-derived tile cache") !=
	        std::string::npos);
	REQUIRE(tiles.find("assetSourceDebugFatalHandleFallback(ASSET_SCENARIO, \"tiles\"") !=
	        std::string::npos);
	REQUIRE(tiles.find("assetLoadToNew(stage.tile_handle") !=
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

	REQUIRE(extractor.find("PDWEAPON_FAST_CACHE_KIND \"pdweapon_embedded_v12_clean_public\"") !=
	        std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_DEPENDENCY_CLOSURE_MARKER \"embedded.v12\"") !=
	        std::string::npos);
	REQUIRE(extractor.find("base_falcon2silencer.pdweapon") !=
	        std::string::npos);
	REQUIRE(extractor.find("category = base") != std::string::npos);
	REQUIRE(extractor.find("bundled = 1") != std::string::npos);
	REQUIRE(extractor.find("s_normalizeSfxCatalogIndex") !=
	        std::string::npos);
	REQUIRE(extractor.find("g_AudioRussMappings[ref.confignum].soundnum") !=
	        std::string::npos);
	REQUIRE(extractor.find("sfx_index\\tarchive_entry\\tcatalog_id") !=
	        std::string::npos);

	REQUIRE(scanner.find("preserved_weapon_id") != std::string::npos);
	REQUIRE(scanner.find("assetCatalogResolve(idbuf)") !=
	        std::string::npos);
	REQUIRE(scanner.find("iniGetInt(ini, \"weapon_id\", preserved_weapon_id)") !=
	        std::string::npos);
	REQUIRE(scanner.find("preserved_runtime_index") != std::string::npos);

	REQUIRE(walker.find("(void)file_path") == std::string::npos);
	REQUIRE(walker.find("s_mpWeaponIdForRuntimeWeapon") !=
	        std::string::npos);
	REQUIRE(walker.find("catalogGetMpWeaponNum(mp_weapon_id)") !=
	        std::string::npos);
	REQUIRE(walker.find("e->runtime_index = runtime_weapon_id") !=
	        std::string::npos);
	REQUIRE(walker.find("e->mp_index = (s16)mp_weapon_id") !=
	        std::string::npos);
	REQUIRE(walker.find("e->ext.weapon.weapon_id = mp_weapon_id") !=
	        std::string::npos);
	REQUIRE(walker.find("catalogSetPrimaryFile(e, file_path)") !=
	        std::string::npos);
	REQUIRE(walker.find("e->ref_count = ASSET_REF_BUNDLED") !=
	        std::string::npos);
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
	REQUIRE(load.find("assetSourceDebugEntryRequiresPublicFileSource(e)") !=
	        std::string::npos);
	REQUIRE(load.find("r->source_only_blocked = 1") != std::string::npos);
	REQUIRE(load.find("assetSourceDebugEntryRequiresPublicFileSource(entry)") !=
	        std::string::npos);
	REQUIRE(load.find("refusing fallback") != std::string::npos);

	REQUIRE(romdata.find("r.source_only_blocked") != std::string::npos);
	REQUIRE(romdata.find("ASSET.SOURCE_ONLY: file") != std::string::npos);
	REQUIRE(romdata.find("ROM/static fallback.") != std::string::npos);
	REQUIRE(mod.find("r.source_only_blocked") != std::string::npos);
	REQUIRE(snd.find("r.source_only_blocked") != std::string::npos);

	REQUIRE(mainmenu.find("Asset Source Gate") != std::string::npos);
	REQUIRE(mainmenu.find("assetSourceDebugOnlyType()") != std::string::npos);
	REQUIRE(mainmenu.find("ImGui::Checkbox(family.label, &checked)") !=
	        std::string::npos);
	REQUIRE(mainmenu.find("assetSourceDebugSetOnlyType(checked ? family.type : ASSET_NONE)") !=
	        std::string::npos);
	REQUIRE(mainmenu.find("ASSET_WEAPON") != std::string::npos);
	REQUIRE(mainmenu.find("ASSET_SCENARIO") != std::string::npos);
	REQUIRE(mainmenu.find("ASSET_AUDIO") != std::string::npos);
}

TEST_CASE("universal extracted archive walkers bind public source members",
          "[modding][pdxxx][c3844][source][static]") {
	const std::string common_h = readTextFile("port/include/loader_walker_common.h");
	const std::string common = readTextFile("port/src/loader_walker_common.c");
	const std::string fs = readTextFile("port/src/fs.c");
	const std::string load = readTextFile("port/src/assetcatalog_load.c");
	const std::string provider = readTextFile("port/src/assetprovider_file.c");
	const std::string player = readTextFile("src/game/player.c");
	const std::string body_runtime = readTextFile("src/game/body.c");
	const std::string chraction = readTextFile("src/game/chraction.c");
	const std::string modeldef = readTextFile("src/game/modeldef.c");
	const std::string setup = readTextFile("src/game/setup.c");
	const std::string body_mgr = readTextFile("port/src/catalog_mgr_bodies.c");
	const std::string head_mgr = readTextFile("port/src/catalog_mgr_heads.c");

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
	REQUIRE(load.find("s_catalogLoadEntryModelPayload(entry, handle)") !=
	        std::string::npos);
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
	REQUIRE(pdmesh_extract.find("modAssetCompilerSkeletonSymbolForPointer") !=
	        std::string::npos);
	REQUIRE(pdmesh_extract.find("g_CartFileNums") != std::string::npos);
	REQUIRE(pdmesh_extract.find("base:model_cartridge_rifle") !=
	        std::string::npos);

	const std::string body = readTextFile("port/src/loader_walker_body.c");
	const std::string head = readTextFile("port/src/loader_walker_head.c");
	REQUIRE(body.find("\"mesh_archive\"") != std::string::npos);
	REQUIRE(body.find("\"mesh.pdmesh\"") != std::string::npos);
	REQUIRE(body.find("mesh_archive_path") != std::string::npos);
	REQUIRE(body.find("\"%s::model.obj\"") != std::string::npos);
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
	REQUIRE(player.find("bodyresult.handle.provider == fileProvider()") !=
	        std::string::npos);
	REQUIRE(player.find("headresult.handle.provider == fileProvider()") !=
	        std::string::npos);
	REQUIRE(player.find("catalogGetBodyModeldef(bodynum)") !=
	        std::string::npos);
	REQUIRE(player.find("catalogGetHeadModeldef(headnum)") !=
	        std::string::npos);
	REQUIRE(player.find("public_source_generated_body") !=
	        std::string::npos);
	REQUIRE(player.find("catalogGetLoadedModeldef(multi_body_id) == bodymodeldef") !=
	        std::string::npos);
	REQUIRE(player.find("weapon_model_file_source_1p") != std::string::npos);
	REQUIRE(player.find("catalogLoadTypedAsset(ASSET_MODEL, weapon_model_id_1p)") !=
	        std::string::npos);
	const std::string bondgun = readTextFile("src/game/bondgun.c");
	REQUIRE(bondgun.find("#include \"assetcatalog.h\"") !=
	        std::string::npos);
	REQUIRE(bondgun.find("#include \"modasset_compiler.h\"") !=
	        std::string::npos);
	REQUIRE(bondgun.find("fileProviderPath(player->gunctrl.loadhandle)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("catalogHandleByModelSourceFilenum(ASSET_NONE, filenum)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("fileProviderHandle") == std::string::npos);
	REQUIRE(bondgun.find("modAssetCompilerIsExternalSource(bgunQueuedModelSourcePath(player))") !=
	        std::string::npos);
	REQUIRE(bondgun.find("catalogIdBySourceHandle(ASSET_MODEL, player->gunctrl.loadhandle)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("catalogLoadTypedAsset(ASSET_MODEL, model_id)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("catalogGetLoadedModeldef(model_id)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("BONDGUN.SOURCE: loaded catalog model source") !=
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
	REQUIRE(modeldef.find("catalogLoadTypedAsset(model_type, model_id)") !=
	        std::string::npos);
	REQUIRE(modeldef.find("MODELDEF.SOURCE: loaded catalog model source") !=
	        std::string::npos);
	REQUIRE(modeldef.find("i < ARRAYCOUNT(g_Skeletons)") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("public_source_static_modeldef") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("public_source_generated_modeldef") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("catalogGetLoadedModeldef(body_source_id) == bodymodeldef") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("modelmgrInstantiateModelWithoutAnim(bodymodeldef)") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("modelmgrInstantiateModelWithAnim(bodymodeldef)") !=
	        std::string::npos);
	const std::string modasset_compiler = readTextFile("port/src/modasset_compiler.c");
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
	REQUIRE(modasset_compiler.find("\"_meta/manifest.json\"") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("\"mesh.ini\"") != std::string::npos);
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
	REQUIRE(modasset_compiler.find("owner->def.nummatrices = 2") !=
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
	REQUIRE(sfx.find("\"sample.wav\"") != std::string::npos);
	REQUIRE(sfx.find("e->source_soundnum") != std::string::npos);
	REQUIRE(voice.find("\"sample.wav\"") != std::string::npos);
	REQUIRE(voice.find("e->source_soundnum") != std::string::npos);
	REQUIRE(song.find("\"sequence.mid\"") != std::string::npos);

	const std::string lang = readTextFile("port/src/loader_walker_lang.c");
	REQUIRE(lang.find("\"source_bank\"") != std::string::npos);
	REQUIRE(lang.find("e->ext.lang.bank_id") != std::string::npos);
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
	REQUIRE(lang_manifest.find("langManifestLoadExternalTsv(entry)") !=
	        std::string::npos);
	REQUIRE(lang_manifest.find("score = entry->bundled ? 1 : 2") !=
	        std::string::npos);
	REQUIRE(lang_manifest.find("valid empty TSV values") !=
	        std::string::npos);

	const std::string lang_extract = readTextFile("port/src/romextract_pdlang.c");
	REQUIRE(lang_extract.find("rzipIs1173") != std::string::npos);
	REQUIRE(lang_extract.find("rzipInflate") != std::string::npos);
	REQUIRE(lang_extract.find("PDLANG_EXTRACT_VERSION") != std::string::npos);
	REQUIRE(lang_extract.find("pdlang_strings_tsv_rzip_v2") !=
	        std::string::npos);
	REQUIRE(lang_extract.find("extract_version = %s\\n") != std::string::npos);
	REQUIRE(lang_extract.find("s_existingArchiveEntryContains(dst_rel, \"lang.ini\"") !=
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
	REQUIRE(arena.find("setup.tsv") == std::string::npos);
	REQUIRE(arena.find("mpsetup.tsv") == std::string::npos);
	REQUIRE(arena.find("visual_segments.tsv") == std::string::npos);
	REQUIRE(arena.find("s_buildWordsTsv") == std::string::npos);
}

TEST_CASE("c3841 scenario archives are source-first and runtime-native",
          "[modding][pdxxx][c3841][scenario][static]") {
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");
	REQUIRE(guard.find("PDSCENARIO_REQUIRED_PUBLIC_ENTRY_NAMES") !=
	        std::string::npos);
	REQUIRE(guard.find("navigation/waypoints.tsv") !=
	        std::string::npos);
	REQUIRE(guard.find("setup.fields.tsv") !=
	        std::string::npos);
	REQUIRE(guard.find("\"scene.glb\"") != std::string::npos);
	REQUIRE(guard.find("\"level.graph.json\"") != std::string::npos);
	const std::string conformance = readTextFile("tools/asset_archive_conformance.py");
	REQUIRE(conformance.find("runtime_source_file = scene.glb") !=
	        std::string::npos);
	REQUIRE(conformance.find("setup_fields_file = setup.fields.tsv") !=
	        std::string::npos);
	const std::string arena = readTextFile("port/src/romextract_pdarena.c");
	REQUIRE(arena.find("\"vehicle.ai_list\"") == std::string::npos);
	REQUIRE(arena.find("\"vehicle.ai_offset\"") != std::string::npos);
	REQUIRE(arena.find("const struct heliobj *heli") != std::string::npos);
	REQUIRE(arena.find("\"vehicle.speed\", heli->speed") !=
	        std::string::npos);

	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("scene_file = scene.glb") != std::string::npos);
	REQUIRE(scanner.find("setup_fields_file = setup.fields.tsv") !=
	        std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"scene_file\"") != std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, sf)") != std::string::npos);

	const std::string load = readTextFile("port/src/assetcatalog_load.c");
	REQUIRE(load.find("modAssetCompilerBuildColmesh(colmesh_source_path, mesh)") !=
	        std::string::npos);
	REQUIRE(load.find("entry->ext.scenario.collision_file") !=
	        std::string::npos);

	const std::string runtime = readTextFile("port/src/asset_runtime.c");
	REQUIRE(runtime.find("entry->ext.scenario.scene_file[0]") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.scenario.level_graph_file") !=
	        std::string::npos);
}

TEST_CASE("c3843 remaining base asset families emit clean native archives",
          "[modding][pdxxx][c3843][static]") {
	const std::string base =
		readTextFile("port/src/assetcatalog_base_extended.c");
	const std::string meta = readTextFile("port/src/romextract_pdmeta.c");
	const std::string arena = readTextFile("port/src/romextract_pdarena.c");
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

	REQUIRE(meta.find("s_emitTexture") != std::string::npos);
	REQUIRE(meta.find("s_emitMaterial") != std::string::npos);
	REQUIRE(meta.find("s_emitSkin") != std::string::npos);
	REQUIRE(meta.find("s_emitEffect") != std::string::npos);
	REQUIRE(meta.find("s_emitProp") != std::string::npos);
	REQUIRE(meta.find("s_emitVehicle") != std::string::npos);
	REQUIRE(meta.find("PDMETA_FAST_CACHE_KIND \"pdmeta_table_backed_v6\"") !=
	        std::string::npos);
	REQUIRE(meta.find("texture.png") != std::string::npos);
	REQUIRE(meta.find("material.json") != std::string::npos);
	REQUIRE(meta.find("skin.json") != std::string::npos);
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
	REQUIRE(meta.find("k_Transparent1x1Png") != std::string::npos);
	REQUIRE(meta.find("empty_rom_slot") != std::string::npos);
	REQUIRE(meta.find("\"mode_id = %d") == std::string::npos);
	REQUIRE(meta.find("\"type = %d") == std::string::npos);
	REQUIRE(meta.find("\"difficulty = %d") == std::string::npos);
	REQUIRE(meta.find("\"hud_id = %d") == std::string::npos);
	REQUIRE(meta.find("\"element_type = %d") == std::string::npos);
	REQUIRE(meta.find("\"effect_type = %d") == std::string::npos);
	REQUIRE(meta.find("\"prop_type = %d") == std::string::npos);
	REQUIRE(arena.find("ROMEXTRACT_PDARENA_FAST_CACHE_KIND \"pdarena_clean_public_v4\"") !=
	        std::string::npos);
	REQUIRE(arena.find("ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND \"pdscenario_scene_glb_clean_public_v9\"") !=
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
	REQUIRE(examples.find("mode_key = custom") != std::string::npos);
	REQUIRE(examples.find("hud_key = ammo") != std::string::npos);
	REQUIRE(examples.find("prop_key = object") != std::string::npos);
	REQUIRE(meta.find("\"empty_rom_slot = %s\\n\"\n"
	                  "\t\t\"texture_file = texture.png\\n\",") !=
	        std::string::npos);
	REQUIRE(meta.find("\"empty_rom_slot = %s\\n\",\n"
	                  "\t\t\"texture_file = texture.png\\n\"") ==
	        std::string::npos);
	REQUIRE(theme.find("fsDataPathFor(rel, out, out_n)") !=
	        std::string::npos);
	REQUIRE(theme.find("(void)romExtractAllPdtheme(0)") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.skin_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.prop.prop_file") !=
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
