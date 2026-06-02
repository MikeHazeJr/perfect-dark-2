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
	const std::string scene_texture_checker =
		readTextFile("tools/verify_scene_glb_texture_contract.py");
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
	const std::string bg_runtime = readTextFile("src/game/bg.c");
	const std::string scene_renderer =
		readTextFile("port/fast3d/scenario_scene_renderer.cpp");
	const std::string scene_renderer_h =
		readTextFile("port/include/scenario_scene_renderer.h");
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
	REQUIRE(setup.find("\"mp setup\"") != std::string::npos);
	REQUIRE(setup.find("\"setup\"") != std::string::npos);
	REQUIRE(setup.find("scenarioSourceLoadSetupForStage(&stage") !=
	        std::string::npos);
	REQUIRE(setup.find("scenarioSourceActivateGraphsForStage(&stage") !=
	        std::string::npos);
	REQUIRE(setup.find("setupIntroCommandsAreValid") !=
	        std::string::npos);
	REQUIRE(setup.find("if (!g_GeCreditsData)") != std::string::npos);
	REQUIRE(setup.find("setupRequireScenarioSourceHandle(\"pads\"") !=
	        std::string::npos);
	REQUIRE(setup.find("scenarioSourceLoadPadsForStage(&stage") !=
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
	REQUIRE(bg_runtime.find("stage.bg_handle") != std::string::npos);
	REQUIRE(scene_renderer_h.find("scenarioSceneRendererActivate") !=
	        std::string::npos);
	REQUIRE(scene_renderer_h.find("scenarioSceneRendererRender") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("fsFileLoad(path") != std::string::npos);
	REQUIRE(scene_renderer.find("findAttr(\"TEXCOORD_1\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("findAttr(\"TEXCOORD_0\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("SCENARIO.RENDER: activated source scene") !=
	        std::string::npos);
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
	REQUIRE(scenario_runtime.find("native BG renderer still pending") ==
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
	REQUIRE(scenario_extractor.find("ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.lists.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.pads.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("portals.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.portals.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_buildBgPortalsTsv") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("\\\"portal_count\\\": %u") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("navigation/paths.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.navigation.paths.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_buildPathTsv") !=
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
	REQUIRE(scenario_runtime.find("s_parsePadsTsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_parseWaypointsTsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("navigation/waypoints.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("navigation/paths.tsv") !=
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
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: pads.tsv runtime uses 32-bit source pad offsets") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("exceeding u16 pad offsets") ==
	        std::string::npos);
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
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: compiled pads.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceLoadPortalsForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_parsePortalsTsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: compiled portals.tsv") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("bgBuildScenarioSourcePortalTables") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("scenarioSourceLoadPortalsForStage(stage") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("SCENARIO.SOURCE: built native portal tables") !=
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
	REQUIRE(scenario_runtime.find("s_loadAiListSourceRows") !=
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
	REQUIRE(scenario_runtime.find("backend=graph.ai.lists+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI list-control actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.list_control+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI alarm actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.alarm+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI flag actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.flags+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI savefile flag actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.savefile_flags+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI timer/countdown actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.timer+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI HUD message actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.hud+ai/ailists.tsv") !=
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
	REQUIRE(scenario_runtime.find("backend=graph.ai.control.random+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.print") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.noop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePrint") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteNoOp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.debug_noop+ai/ailists.tsv") !=
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
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.character_lifecycle+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.basic_motion+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.combat+ai/ailists.tsv") !=
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
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.target_movement+ai/ailists.tsv") !=
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
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfSawTargetRecently") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfHeardTargetRecently") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.perception_alarm+ai/ailists.tsv") !=
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
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.spatial_perception+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.distance_perception+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.room_object_weapon+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.object_interaction+ai/ailists.tsv+objects.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.quadrant_preset+ai/ailists.tsv+pads.tsv+navigation.generate") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_weapon_state+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_cutscene+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.setup_spawn+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.entity_lifecycle+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.release_cover+navigation/covers.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.orders+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.intent_status+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrDoAnimation") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteBeSurprisedOneHand") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteBeSurprisedLookAround") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteBeSurprisedSurrender") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.animation+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: pad source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.pads+pads.tsv") !=
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
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.jog_to_pad+pads.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.go_to_pad_preset+pads.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.walk_to_pad+pads.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.run_to_pad+pads.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: path source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.navigation.paths+navigation/paths.tsv") !=
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
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_num_players_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_kill_count_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_num_knocked_out_chrs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.kill_bond") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteKillBond") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.mission_global+ai/ailists.tsv+mission.graph.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.mission_global+ai/ailists.tsv+mission.graph.json") !=
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
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.character_state+ai/ailists.tsv") !=
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
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.lifecycle+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_pouncebits_eq") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_training_pc_holographed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_player_using_device") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.state_device+ai/ailists.tsv") !=
	        std::string::npos);
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
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_portal_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_music_event_queue_is_empty") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_coop_mode") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than") !=
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
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.cutscene_visibility+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlayXTrack") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteStopAmbientTrack") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.music_track+ai/ailists.tsv") !=
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
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrCopyProperties") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlayerAutoWalk") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfPlayerAutoWalkFinished") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfObjInRoom") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfPlayerLookingAtObject") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfTargetIsPlayer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_weapon_state+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.gun_interaction+ai/ailists.tsv+objects.tsv+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.character_property+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_navigation+ai/ailists.tsv+pads.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.object_room+ai/ailists.tsv+objects.tsv+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.perception+ai/ailists.tsv+objects.tsv+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteConfigureEnvironment") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.environment+ai/ailists.tsv+scenario.ini+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfDistanceToTarget2") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.target_distance+ai/ailists.tsv") !=
	        std::string::npos);
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
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlayRepeatingSoundFromPad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfObjectSoundVolumeLessThan") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlaySoundFromProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlayTemporaryPrimaryTrack") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.audio+ai/ailists.tsv") !=
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
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.character_inventory+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.character_inventory+ai/ailists.tsv+objects.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_state+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_state+ai/ailists.tsv+objects.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.teleport_cutscene_weapon+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.cutscene_presentation+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.cutscene_presentation+ai/ailists.tsv+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfMusicEventQueueIsEmpty") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfCoopMode") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.music_mode+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfChrSameFloorDistanceToPadLessThan") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRemoveReferencesToChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.pad_reference+ai/ailists.tsv+pads.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteOpenDoor") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfDoorState") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.door+objects.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteLiftGoToStop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.lift+objects.tsv+pads.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteConfigureRain") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.weather+ai/ailists.tsv+scenario.ini") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSwitchToAltSky") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetWindSpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.sky+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetLights") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.lighting+ai/ailists.tsv+pads.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetRoomFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.room_flags+ai/ailists.tsv+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrToggleModelPart") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteObjSetModelPartVisible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.model_part+ai/ailists.tsv+objects.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfObjHealthLessThan") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetObjHealth") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.object_health+ai/ailists.tsv+objects.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetChrSpecialDeathAnimation") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.special_death+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetRoomToSearch") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.room_search+ai/ailists.tsv+scene.glb") !=
	        std::string::npos);
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
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.misc_effect+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.quip_shuffle+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteHovercarBeginPath") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetVehicleSpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetRotorSpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("chopperFromHovercar(g_Vars.hovercar)") !=
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
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_path+navigation/paths.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.start_patrol+navigation/paths.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_pad_preset+pads.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_set_pad_preset+pads.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_copy_pad_preset+chrstate") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_chr_preset+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_chr_target+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_morale+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.add_morale+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_add_morale+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.subtract_morale+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_alertness+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.add_alertness+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_add_alertness+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.subtract_alertness+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.increase_squadron_alertness+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_hear_distance+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_view_distance+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_grenade_probability+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_chr_num+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_max_damage+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.add_health+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_shield+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_reaction_speed+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_recovery_speed+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_accuracy+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_dodge_rating+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_unarmed_dodge_rating+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_flag+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.unset_flag+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.if_has_flag+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_set_flag+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_unset_flag+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.if_chr_has_flag+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_stage_flag+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.unset_stage_flag+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.if_stage_flag_eq+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_chrflag+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_set_hidden_flag+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_obj_flag+objects.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_object_flags+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_savefile_flag+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.unset_savefile_flag+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.if_savefile_flag_set+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.if_savefile_flag_unset+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_action+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_team_orders+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.retreat+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.find_cover+navigation/covers.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.find_cover_within_dist+navigation/covers.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.find_cover_outside_dist+navigation/covers.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.go_to_cover+navigation/covers.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.check_cover_out_of_sight+navigation/covers.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.orbit_target+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_chr_preset_to_unalerted_teammate+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_squadron+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.face_cover+navigation/covers.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.danger_cover+navigation/covers.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.rebuild_teams+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.rebuild_squadrons+ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_set_listening+ai/ailists.tsv") !=
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
	REQUIRE(meta_extractor.find("PDMETA_FAST_CACHE_KIND \"pdmeta_table_backed_v8_pdscenario_v77\"") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("PDMETA_SCENARIO_DEP_CACHE_KIND") !=
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
	REQUIRE(conformance.find("level.graph.json must include exactly one {description} graph node") !=
	        std::string::npos);
	REQUIRE(conformance.find("scenario.ini must declare ai_lists_file = ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(conformance.find("scenario.ini must declare paths_file = navigation/paths.tsv") !=
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
	REQUIRE(conformance.find("level.graph.json must bind paths table to navigation/paths.tsv") !=
	        std::string::npos);
	REQUIRE(conformance.find("ai/ailists.tsv must use the definitive AI list source header") !=
	        std::string::npos);
	REQUIRE(conformance.find("navigation/paths.tsv must use the definitive path source header") !=
	        std::string::npos);
	REQUIRE(conformance.find("spawns.tsv must use the definitive spawn source header") !=
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
	REQUIRE(scenario_runtime.find("\"autogun.y_zero\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"autogun.y_max_left\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"autogun.max_speed\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("spawns.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("ai/ailists.tsv") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("navigation/paths.tsv") !=
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
	REQUIRE(mesh_extractor.find("pdmesh_model_obj_mtx_v11_skeleton_allmodels_menuhud") !=
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
	REQUIRE(modasset_compiler.find("objMeshAddTriangle(mesh, indices[0], indices[i - 1], indices[i],") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("tri->roomnum") != std::string::npos);
	REQUIRE(modasset_compiler.find("out_mesh->tris[out_mesh->numtris - 1].roomnum") !=
	        std::string::npos);

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

	REQUIRE(extractor.find("PDWEAPON_FAST_CACHE_KIND \"pdweapon_embedded_v13_clean_public\"") !=
		std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_DEPENDENCY_CLOSURE_MARKER \"embedded.v13\"") !=
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
	REQUIRE(runner.find("$all = @(Get-SmokeTests -Dir $TestsDir)") !=
	        std::string::npos);
}

TEST_CASE("Scenario source matrix runner keeps stage smokes source-only and sequential",
          "[modding][pdxxx][c3844][scenario][smoke][static]") {
	const std::string matrix =
		readTextFile("tools/smoke-verify/run-scenario-source-matrix.ps1");

	REQUIRE(matrix.find("AssetSourceOnlyType=30") != std::string::npos);
	REQUIRE(matrix.find("intentionally sequential") != std::string::npos);
	REQUIRE(matrix.find("Build\\data\\ntsc-final\\scenarios") != std::string::npos);
	REQUIRE(matrix.find("_meta/manifest.json") != std::string::npos);
	REQUIRE(matrix.find("stagenum") != std::string::npos);
	REQUIRE(matrix.find("$stageHexRegex = (\"0x0*{0:x}\"") !=
	        std::string::npos);
	REQUIRE(matrix.find("--boot-stage") != std::string::npos);
	REQUIRE(matrix.find("tools/smoke-verify/run.ps1") != std::string::npos);
	REQUIRE(matrix.find("-TestsDir") != std::string::npos);
	REQUIRE(matrix.find("base:scenario_chicago") != std::string::npos);
	REQUIRE(matrix.find("base:scenario_citraining") != std::string::npos);
	REQUIRE(matrix.find("base:scenario_airbase") != std::string::npos);
	REQUIRE(matrix.find("base:scenario_test_ash") != std::string::npos);
	REQUIRE(matrix.find("base:scenario_extra25") != std::string::npos);
	REQUIRE(matrix.find("base:scenario_extra26") != std::string::npos);
	REQUIRE(readTextFile("port/src/main.c").find("g_StageNum > STAGE_EXTRA26") !=
	        std::string::npos);
	REQUIRE(readTextFile("port/src/main.c").find("g_StageNum > 0x5d") ==
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: added scene colmesh") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: compiled portals\\.tsv") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: built native portal tables") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: activated level graph") !=
	        std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.GRAPH: table refs") != std::string::npos);
	REQUIRE(matrix.find("SCENARIO\\.SOURCE: compiled pads\\.tsv") !=
	        std::string::npos);
	REQUIRE(matrix.find("ASSET\\.SOURCE_ONLY") != std::string::npos);
	REQUIRE(matrix.find("RomProvider:filenum") != std::string::npos);
	REQUIRE(matrix.find("refusing fallback") != std::string::npos);
	REQUIRE(matrix.find("GROUNDSNAP: ignoring invalid chr->ground") !=
	        std::string::npos);
}

TEST_CASE("Scenario source accepts explicit empty public pads and portals",
          "[modding][pdxxx][c3844][scenario][source_gate][static]") {
	const std::string runtime =
		readTextFile("port/src/scenario_source_runtime.c");
	const std::string bg = readTextFile("src/game/bg.c");

	REQUIRE(runtime.find("s_parsePadsTsv(text, &row_count)") !=
	        std::string::npos);
	REQUIRE(runtime.find("s_parsePortalsTsv(text, &row_count)") !=
	        std::string::npos);
	REQUIRE(runtime.find("!rows || row_count <= 0") == std::string::npos);
	REQUIRE(runtime.find("SCENARIO.SOURCE: compiled pads.tsv") !=
	        std::string::npos);
	REQUIRE(runtime.find("SCENARIO.SOURCE: compiled portals.tsv") !=
	        std::string::npos);
	REQUIRE(bg.find("portals ? portals :") != std::string::npos);
	REQUIRE(bg.find("portalcount > 0 ? portalcount : 0") !=
	        std::string::npos);
	REQUIRE(bg.find("SCENARIO.SOURCE: built native portal tables") !=
	        std::string::npos);
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
	const std::string main_c = readTextFile("port/src/main.c");
	const std::string pdgui_theme = readTextFile("port/fast3d/pdgui_theme.cpp");
	const std::string loader_ui = readTextFile("port/src/loader_walker_ui.c");
	const std::string all_family_smoke =
		readTextFile("tools/smoke-verify/tests/all_family_source_gate_smoke.json");

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
	REQUIRE(main_c.find("strcmp(s, \"voice\") == 0") != std::string::npos);
	REQUIRE(main_c.find("strcmp(s, \"song\") == 0") != std::string::npos);
	REQUIRE(main_c.find("strcmp(s, \"music\") == 0") != std::string::npos);
	REQUIRE(main_c.find("bootEnsureUiArchivesReadyForCliSourceLoads") !=
	        std::string::npos);
	REQUIRE(main_c.find("(void)romExtractAllPdui(0)") != std::string::npos);
	REQUIRE(main_c.find("loaderWalkerScanUi(data_root, &kr)") !=
	        std::string::npos);
	REQUIRE(loader_ui.find("assetCatalogGetMutable(id)") != std::string::npos);
	REQUIRE(loader_ui.find("if (!e)") < loader_ui.find("assetCatalogRegister(id, ASSET_UI)"));
	REQUIRE(loader_ui.find("catalogSetPrimaryFile(e, source_path)") !=
	        std::string::npos);
	REQUIRE(main_c.find("bootArmDebugLoadCatalogAssets") != std::string::npos);
	REQUIRE(main_c.find("g_BootDebugLoadCatalogAssetsPending = 1") !=
	        std::string::npos);
	REQUIRE(main_c.find("s32 bootApplyDeferredDebugLoadCatalogAssets(void)") !=
	        std::string::npos);
	{
		const std::string cli_fast_paths = functionBlock(main_c,
			"static void bootApplyCliFastPaths");
		REQUIRE(cli_fast_paths.find("bootArmDebugLoadCatalogAssets();") !=
		        std::string::npos);
		REQUIRE(cli_fast_paths.find("bootApplyDebugLoadCatalogAssets(") ==
		        std::string::npos);
	}
	{
		const std::string deferred = functionBlock(main_c,
			"s32 bootApplyDeferredDebugLoadCatalogAssets");
		REQUIRE(deferred.find("bootEnsureUiArchivesReadyForCliSourceLoads();") <
		        deferred.find("bootApplyDebugLoadCatalogAssets("));
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

TEST_CASE("universal extracted archive walkers bind public source members",
          "[modding][pdxxx][c3844][source][static]") {
	const std::string common_h = readTextFile("port/include/loader_walker_common.h");
	const std::string common = readTextFile("port/src/loader_walker_common.c");
	const std::string fs = readTextFile("port/src/fs.c");
	const std::string load = readTextFile("port/src/assetcatalog_load.c");
	const std::string meta_walker = readTextFile("port/src/loader_walker_meta.c");
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
		".pdcharacter", ".pdskin", ".pdprop", ".pdvehicle", ".pdmission",
		".pdgamemode", ".pdbotprofile", ".pdhud", ".pdeffect", ".pdmaterial",
		".pdtheme",
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
	REQUIRE(meta_walker.find("\"body_archive\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"head_archive\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"skin_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"prop_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"physics_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"behavior_graph\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"mission_graph_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"theme_file\"") != std::string::npos);

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
	REQUIRE(pdmesh_extract.find("FILE_GHUDPIECE") !=
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
	REQUIRE(sfx.find("\"sample.wav\"") != std::string::npos);
	REQUIRE(sfx.find("always_invoke = 1") != std::string::npos);
	REQUIRE(sfx.find("e->source_soundnum") != std::string::npos);
	REQUIRE(voice.find("\"sample.wav\"") != std::string::npos);
	REQUIRE(voice.find("always_invoke = 1") != std::string::npos);
	REQUIRE(voice.find("e->source_soundnum") != std::string::npos);
	REQUIRE(song.find("\"sequence.mid\"") != std::string::npos);
	REQUIRE(song.find("always_invoke = 1") != std::string::npos);

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
	REQUIRE(guard.find("navigation/paths.tsv") !=
	        std::string::npos);
	REQUIRE(guard.find("setup.fields.tsv") !=
	        std::string::npos);
	REQUIRE(guard.find("\"scene.glb\"") != std::string::npos);
	REQUIRE(guard.find("\"level.graph.json\"") != std::string::npos);
	const std::string conformance = readTextFile("tools/asset_archive_conformance.py");
	const std::string scene_texture_checker =
		readTextFile("tools/verify_scene_glb_texture_contract.py");
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
	REQUIRE(meta.find("PDMETA_FAST_CACHE_KIND \"pdmeta_table_backed_v8_pdscenario_v77\"") !=
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
	REQUIRE(arena.find("ROMEXTRACT_PDARENA_FAST_CACHE_KIND \"pdarena_clean_public_v6_pdscenario_v77\"") !=
	        std::string::npos);
	REQUIRE(arena.find("ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND \"pdscenario_scene_glb_clean_public_v77_standalone_backfill_collision_obj_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_quip_shuffle_graph_portals\"") !=
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"portals.tsv\"") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntry(relpath, \"portals.tsv\")") !=
	        std::string::npos);
	REQUIRE(arena.find("\"scenario.portals.source\"") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"portals\\\": \\\"portals.tsv\\\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("SCENARIO_PORTALS_HEADER") !=
	        std::string::npos);
	REQUIRE(conformance.find("scenario.ini must declare portals_file = portals.tsv") !=
	        std::string::npos);
	REQUIRE(conformance.find("level.graph.json must bind portals table to portals.tsv") !=
	        std::string::npos);
	REQUIRE(arena.find("PDSCENARIO_BG_VISUAL_EXPORT_VERSION \"bg_visual_scene_glb_v7_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound\"") !=
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
	REQUIRE(conformance.find("arena.ini must declare scenario_graph_cache") !=
	        std::string::npos);
	REQUIRE(conformance.find("mission.ini must declare scenario_graph_cache") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"texCoord\\\":0") != std::string::npos);
	REQUIRE(arena.find("s_bgMaterialUvScaleForGlb") != std::string::npos);
	REQUIRE(arena.find("s_bgMaterialGlbAuthorUv") != std::string::npos);
	REQUIRE(arena.find("\\\"TEXCOORD_1\\\":%u") != std::string::npos);
	REQUIRE(arena.find("\\\"baseColorTexture\\\":{\\\"index\\\":%d,\\\"texCoord\\\":0}") !=
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
	REQUIRE(conformance.find("bg_visual_scene_glb_v7_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound") !=
	        std::string::npos);
	REQUIRE(conformance.find("TEXCOORD_1 runtime UVs") != std::string::npos);
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
	REQUIRE(examples.find("mode_key = custom") != std::string::npos);
	REQUIRE(examples.find("hud_key = ammo") != std::string::npos);
	REQUIRE(examples.find("prop_key = object") != std::string::npos);
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
