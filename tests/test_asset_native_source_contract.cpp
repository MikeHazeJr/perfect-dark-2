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
	REQUIRE(agents.find("tools/asset_native_source_guard.py") !=
	        std::string::npos);

	const std::string skill =
		readTextFile(".agents/skills/pd2-large-change-sweep/SKILL.md");
	REQUIRE(skill.find("Asset Pipeline c3842 Gate") != std::string::npos);
	REQUIRE(skill.find("public editable source") != std::string::npos);
	REQUIRE(skill.find("runtime-cache-only") != std::string::npos);
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
	REQUIRE(guard.find("source-hashed rebuildable cache") !=
	        std::string::npos);
	REQUIRE(guard.find("examples/modding/typed-pdxxx-basic") !=
	        std::string::npos);
	REQUIRE(guard.find("c3842-s1") != std::string::npos);
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

	const std::string formats =
		readTextFile("context/designs/modding/asset-archive-clean-formats.md");
	REQUIRE(formats.find("The public authoring files are also the game-facing source of truth") !=
	        std::string::npos);
	REQUIRE(formats.find("one editable file and a separate opaque runtime file") !=
	        std::string::npos);

	const std::string tasks = readTextFile("context/tasks.md");
	REQUIRE(tasks.find("Asset Pipeline native-source correction") !=
	        std::string::npos);
	REQUIRE(tasks.find("c3842") != std::string::npos);

	const std::string kanban = readTextFile("tools/kanban/state.json");
	REQUIRE(kanban.find("\"id\": \"c3842\"") != std::string::npos);
	REQUIRE(kanban.find("\"column\": \"done\"") != std::string::npos);
	REQUIRE(kanban.find("\"priority\": 1") != std::string::npos);
	REQUIRE(kanban.find("\"id\": \"c3842-s1\"") != std::string::npos);
	REQUIRE(kanban.find("\"status\": \"done\"") != std::string::npos);
}

TEST_CASE("typed archive guard rejects numeric asset references",
          "[modding][pdxxx][c3842][catalog-id][static]") {
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");
	REQUIRE(guard.find("\"model_id\"") != std::string::npos);
	REQUIRE(guard.find("\"modelnum\"") != std::string::npos);
	REQUIRE(guard.find("\"filenum\"") != std::string::npos);
	REQUIRE(guard.find("\"weapon_id\"") != std::string::npos);
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
	REQUIRE(guard.find("\"scene.glb\"") != std::string::npos);
	REQUIRE(guard.find("\"level.graph.json\"") != std::string::npos);
	REQUIRE(guard.find("runtime_source_file = scene.glb") !=
	        std::string::npos);

	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("scene_file = scene.glb") != std::string::npos);
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
