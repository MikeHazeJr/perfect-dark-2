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
	REQUIRE(conformance.find("is_random_selector") != std::string::npos);
	REQUIRE(conformance.find("rooms.obj") != std::string::npos);
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
	REQUIRE(debug.find("entry->source.primary.provider == fileProvider()") !=
	        std::string::npos);
	REQUIRE(debug.find("entry->source.override.provider == fileProvider()") !=
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
