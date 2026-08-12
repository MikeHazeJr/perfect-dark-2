/*
 * tests/test_catalog_provider_static.cpp -- Catalog/provider migration guards.
 *
 * These checks pin the provider boundary while Phase 4 keeps a small
 * allowlist for approved catalog/provider bridge files.
 */

#include "catch.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string readTextFile(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

static std::string functionBlock(const std::string &text, const std::string &name)
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

static std::string normalizePath(const fs::path &path)
{
	std::string s = path.generic_string();
	if (s.rfind("./", 0) == 0) {
		s.erase(0, 2);
	}
	return s;
}

static bool isAllowedPath(const std::string &path, const std::vector<std::string> &allowed)
{
	for (const std::string &candidate : allowed) {
		if (path == candidate) {
			return true;
		}
	}
	return false;
}

static bool isScannedSource(const fs::path &path)
{
	const std::string ext = path.extension().generic_string();
	return ext == ".c" || ext == ".cpp" || ext == ".h" || ext == ".hpp";
}

static std::vector<std::string> repoSourceFiles()
{
	std::vector<std::string> files;
	const std::array<const char *, 2> roots = { "src", "port" };

	for (const char *root : roots) {
		if (!fs::exists(root)) {
			continue;
		}

		for (const fs::directory_entry &entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
			if (!entry.is_regular_file()) {
				continue;
			}
			if (!isScannedSource(entry.path())) {
				continue;
			}
			files.push_back(normalizePath(entry.path()));
		}
	}

	return files;
}

static std::string stripComments(const std::string &text, bool strip_strings)
{
	std::string out;
	out.reserve(text.size());

	for (size_t i = 0; i < text.size(); i++) {
		const char c = text[i];
		const char next = i + 1 < text.size() ? text[i + 1] : '\0';

		if (c == '/' && next == '/') {
			out.push_back(' ');
			out.push_back(' ');
			i += 2;
			while (i < text.size() && text[i] != '\n') {
				out.push_back(' ');
				i++;
			}
			if (i < text.size()) {
				out.push_back('\n');
			}
			continue;
		}

		if (c == '/' && next == '*') {
			out.push_back(' ');
			out.push_back(' ');
			i += 2;
			while (i < text.size()) {
				if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '/') {
					out.push_back(' ');
					out.push_back(' ');
					i++;
					break;
				}
				out.push_back(text[i] == '\n' ? '\n' : ' ');
				i++;
			}
			continue;
		}

		if (strip_strings && (c == '"' || c == '\'')) {
			const char quote = c;
			out.push_back(' ');
			i++;
			while (i < text.size()) {
				const char sc = text[i];
				out.push_back(sc == '\n' ? '\n' : ' ');
				if (sc == '\\' && i + 1 < text.size()) {
					i++;
					out.push_back(text[i] == '\n' ? '\n' : ' ');
				} else if (sc == quote) {
					break;
				}
				i++;
			}
			continue;
		}

		out.push_back(c);
	}

	return out;
}

static std::string joinLines(const std::vector<std::string> &lines)
{
	std::ostringstream ss;
	for (const std::string &line : lines) {
		ss << line << '\n';
	}
	return ss.str();
}

static size_t countOccurrences(const std::string &text, const std::string &needle)
{
	size_t count = 0;
	size_t pos = 0;

	while ((pos = text.find(needle, pos)) != std::string::npos) {
		count++;
		pos += needle.size();
	}

	return count;
}

TEST_CASE("catalog lifecycle call sites use typed wrappers", "[catalog][provider][static]")
{
	const std::array<const char *, 3> files = {
		"src/game/lv.c",
		"port/src/screenmfst.c",
		"port/src/net/netmanifest.c",
	};

	for (const char *path : files) {
		const std::string stripped = stripComments(readTextFile(path), true);
		INFO(path);
		REQUIRE(stripped.find("catalogLoadAsset(") == std::string::npos);
		REQUIRE(stripped.find("catalogUnloadAsset(") == std::string::npos);
		REQUIRE(stripped.find("catalogReleaseAsset(") == std::string::npos);
		REQUIRE(stripped.find("catalogRetainAsset(") == std::string::npos);
		if (std::string(path) == "src/game/lv.c") {
			REQUIRE(stripped.find("catalogLoadStageAsset(") != std::string::npos);
			REQUIRE(stripped.find("catalogReleaseStageAsset(") != std::string::npos);
		} else {
			REQUIRE(stripped.find("catalogLoadTypedAsset(") != std::string::npos);
			REQUIRE(stripped.find("catalogReleaseTypedAsset(") != std::string::npos);
		}
	}
}

TEST_CASE("untyped catalog lifecycle calls stay inside catalog implementation", "[catalog][provider][static]")
{
	const std::vector<std::string> allowed = {};
	const std::array<const char *, 4> banned = {
		"catalogLoadAsset(",
		"catalogUnloadAsset(",
		"catalogRetainAsset(",
		"catalogReleaseAsset(",
	};
	std::vector<std::string> violations;

	for (const std::string &path : repoSourceFiles()) {
		const std::string stripped = stripComments(readTextFile(path.c_str()), true);

		for (const char *pattern : banned) {
			if (stripped.find(pattern) != std::string::npos
					&& !isAllowedPath(path, allowed)) {
				violations.push_back(path + ": " + pattern);
			}
		}
	}

	INFO("untyped lifecycle calls outside catalog implementation:\n" << joinLines(violations));
	REQUIRE(violations.empty());

	const std::string api = stripComments(readTextFile("port/include/assetcatalog_load.h"), true);
	const std::string catalogLoad = stripComments(readTextFile("port/src/assetcatalog_load.c"), true);
	const std::string stubs = stripComments(readTextFile("port/src/server_stubs.c"), true);
	for (const char *pattern : banned) {
		REQUIRE(api.find(pattern) == std::string::npos);
		REQUIRE(catalogLoad.find(pattern) == std::string::npos);
		REQUIRE(stubs.find(pattern) == std::string::npos);
	}
}

TEST_CASE("typed catalog release and retain use entry-level internals", "[catalog][provider][static]")
{
	const std::string catalogLoad = stripComments(readTextFile("port/src/assetcatalog_load.c"), true);

	REQUIRE(catalogLoad.find("s_catalogUnloadEntry") != std::string::npos);
	REQUIRE(catalogLoad.find("s_catalogRetainEntry") != std::string::npos);
	REQUIRE(catalogLoad.find("catalogUnloadAsset(dep_id)") == std::string::npos);
	REQUIRE(catalogLoad.find("catalogUnloadAsset(assetId)") == std::string::npos);
	REQUIRE(catalogLoad.find("catalogRetainAsset(assetId)") == std::string::npos);
}

TEST_CASE("typed catalog lifecycle does not use generic path fallback", "[catalog][provider][static]")
{
	const std::string catalogLoad = readTextFile("port/src/assetcatalog_load.c");

	REQUIRE(catalogLoad.find("CATALOG.LIFECYCLE.LOAD: typed '%s' %s payload has no provider handle") != std::string::npos);
	REQUIRE(catalogLoad.find("if (expected_type != ASSET_NONE)") != std::string::npos);
	REQUIRE(catalogLoad.find("return s_catalogLoadEntryFromProvider(entry, expected_type);") != std::string::npos);
	REQUIRE(catalogLoad.find("s_catalogLoadEntryFromPath") == std::string::npos);
	REQUIRE(catalogLoad.find("FALLBACK: catalogLoadAsset") == std::string::npos);
	REQUIRE(catalogLoad.find("catalogLoadAsset(") == std::string::npos);
}

TEST_CASE("catalog identity fallbacks use typed helpers at migrated boundaries", "[catalog][provider][static]")
{
	const std::string scenarioSave = readTextFile("port/src/scenario_save.c");
	const std::string savefile = readTextFile("port/src/savefile.c");
	const std::string net = readTextFile("port/src/net/net.c");
	const std::string matchsetup = readTextFile("port/src/net/matchsetup.c");
	const std::string netmsg = readTextFile("port/src/net/netmsg.c");
	const std::string room = readTextFile("port/fast3d/pdgui_menu_room.cpp");

	REQUIRE(scenarioSave.find("catalogIdByRuntime(ASSET_GAMEMODE") == std::string::npos);
	REQUIRE(savefile.find("catalogIdByRuntime(ASSET_GAMEMODE") == std::string::npos);
	REQUIRE(net.find("catalogIdByRuntime(ASSET_GAMEMODE") == std::string::npos);
	REQUIRE(matchsetup.find("catalogIdByRuntime(ASSET_GAMEMODE") == std::string::npos);
	REQUIRE(netmsg.find("catalogIdByRuntime(ASSET_GAMEMODE") == std::string::npos);
	REQUIRE(room.find("catalogIdByRuntime(ASSET_GAMEMODE") == std::string::npos);
	REQUIRE(room.find("catalogIdByRuntime(ASSET_MAP") == std::string::npos);
}

TEST_CASE("generic catalog runtime lookup is confined to typed helper implementations", "[catalog][identity][static]")
{
	const std::regex generic_typed_lookup(
		R"(catalogIdByRuntime\s*\(\s*(ASSET_MAP|ASSET_MODEL|ASSET_BODY|ASSET_HEAD|ASSET_WEAPON|ASSET_GAMEMODE)\b)");
	const std::vector<std::string> allowed = {
		"port/src/assetcatalog_api.c",
	};
	std::vector<std::string> violations;

	for (const std::string &path : repoSourceFiles()) {
		if (isAllowedPath(path, allowed)) {
			continue;
		}

		const std::string stripped = stripComments(readTextFile(path.c_str()), true);
		if (std::regex_search(stripped, generic_typed_lookup)) {
			violations.push_back(path);
		}
	}

	INFO("generic catalogIdByRuntime uses for typed domains:\n" << joinLines(violations));
	REQUIRE(violations.empty());
}

TEST_CASE("modeldef handle loader is not RomProvider-only", "[catalog][provider][static]")
{
	const std::string modeldef = readTextFile("src/game/modeldef.c");

	REQUIRE(modeldef.find("handle.provider != romProvider()") == std::string::npos);
	REQUIRE(modeldef.find("only RomProvider handles can safely use this loader") == std::string::npos);
	REQUIRE(modeldef.find("only supports RomProvider") == std::string::npos);
	REQUIRE(modeldef.find("RomProvider-only") == std::string::npos);
	REQUIRE(modeldef.find("modeldefPromoteDisplayListsWithSizes") != std::string::npos);
	REQUIRE(modeldef.find("assetLoadGetLoadedSize(handle)") != std::string::npos);
}

TEST_CASE("first-person gun async loader keeps provider payload sizes local", "[catalog][provider][static]")
{
	const std::string bondgun = readTextFile("src/game/bondgun.c");
	const std::string modeldef = readTextFile("src/include/game/modeldef.h");

	REQUIRE(bondgun.find("g_FileInfo[") == std::string::npos);
	REQUIRE(bondgun.find("handle.provider == romProvider()") == std::string::npos);
	REQUIRE(bondgun.find("handle.provider != romProvider()") == std::string::npos);
	REQUIRE(bondgun.find("temporary ROM fallback until model promotion") == std::string::npos);
	REQUIRE(modeldef.find("modeldefPromoteDisplayListsUsingSizes") != std::string::npos);
}

TEST_CASE("source-filenum provider handle reverse lookups stay catalog-owned", "[catalog][provider][static]")
{
	const std::array<const char *, 3> files = {
		"src/game/bondgun.c",
		"src/game/menu.c",
		"port/src/modelcatalog.c",
	};
	const std::string header = readTextFile("port/include/assetcatalog.h");
	const std::string api = stripComments(readTextFile("port/src/assetcatalog_api.c"), true);
	const std::string meshWalker = readTextFile("port/src/loader_walker_mesh.c");
	const size_t sourceHelperStart = api.find("catalogHandleBySourceFilenum");
	REQUIRE(sourceHelperStart != std::string::npos);
	const size_t sourceHelperEnd = api.find("catalogHandleByModelSourceFilenum", sourceHelperStart);
	REQUIRE(sourceHelperEnd != std::string::npos);
	const std::string sourceHelperBlock = api.substr(
		sourceHelperStart, sourceHelperEnd - sourceHelperStart);
	const size_t helperStart = api.find("catalogHandleByModelSourceFilenum");
	REQUIRE(helperStart != std::string::npos);
	const size_t helperEnd = api.find("static s32 s_catalogHandleEquals", helperStart);
	REQUIRE(helperEnd != std::string::npos);
	const std::string helperBlock = api.substr(helperStart, helperEnd - helperStart);

	REQUIRE(header.find("catalogHandleByModelSourceFilenum") != std::string::npos);
	REQUIRE(sourceHelperBlock.find("fallback_handle") != std::string::npos);
	REQUIRE(sourceHelperBlock.find("handle.provider == fileProvider()") != std::string::npos);
	REQUIRE(sourceHelperBlock.find("catalogReadableModelIdForFile(source_filenum") != std::string::npos);
	REQUIRE(meshWalker.find("\"mesh\", \"meshes\", \".pdmesh\", /* always_invoke: */ 1") != std::string::npos);
	REQUIRE(meshWalker.find("const asset_entry_t *existing = assetCatalogResolve(id)") != std::string::npos);
	REQUIRE(meshWalker.find("e->runtime_index = preserved_runtime_index") != std::string::npos);
	REQUIRE(meshWalker.find("catalogSetPrimaryFile(e, source_path)") != std::string::npos);
	REQUIRE(helperBlock.find("catalogHandleBySourceFilenum(") != std::string::npos);
	REQUIRE(helperBlock.find("ASSET_MODEL") != std::string::npos);
	REQUIRE(helperBlock.find("ASSET_BODY") != std::string::npos);
	REQUIRE(helperBlock.find("ASSET_HEAD") != std::string::npos);
	REQUIRE(helperBlock.find("ASSET_WEAPON") != std::string::npos);
	REQUIRE(helperBlock.find("ASSET_PROP") != std::string::npos);
	REQUIRE(helperBlock.find("ASSET_VEHICLE") != std::string::npos);

	for (const char *path : files) {
		const std::string stripped = stripComments(readTextFile(path), true);
		INFO(path);
		REQUIRE(stripped.find("catalogHandleByModelSourceFilenum(") != std::string::npos);
		REQUIRE(stripped.find("catalogHandleBySourceFilenum(") == std::string::npos);
		REQUIRE(stripped.find("catalogIdBySourceFilenum(") == std::string::npos);
		REQUIRE(stripped.find("catalogEffectiveHandle(") == std::string::npos);
	}
}

TEST_CASE("modelnum load sites use typed model catalog APIs", "[catalog][provider][static]")
{
	const std::array<const char *, 3> files = {
		"src/game/title.c",
		"src/game/player.c",
		"src/game/setuputils.c",
	};
	const std::string api = readTextFile("port/include/assetcatalog.h");

	REQUIRE(api.find("catalog_model_result_t") != std::string::npos);
	REQUIRE(api.find("catalogResolveModelByModelnum") != std::string::npos);
	REQUIRE(api.find("catalogGetModelHandle") != std::string::npos);

	for (const char *path : files) {
		const std::string stripped = stripComments(readTextFile(path), true);
		INFO(path);
		REQUIRE(stripped.find("catalogResolveModelByModelnum(") != std::string::npos);
		REQUIRE(stripped.find("catalogGetPropHandle(") == std::string::npos);
		REQUIRE(stripped.find("catalogGetPropFilenumByIndex(") == std::string::npos);
	}
}

TEST_CASE("prop-named model compatibility wrappers stay inside catalog API", "[catalog][provider][static]")
{
	const std::vector<std::string> allowed = {
		"port/include/assetcatalog.h",
		"port/src/assetcatalog_api.c",
	};
	const std::array<const char *, 2> banned = {
		"catalogGetPropHandle(",
		"catalogGetPropFilenumByIndex(",
	};
	std::vector<std::string> violations;

	for (const std::string &path : repoSourceFiles()) {
		const std::string stripped = stripComments(readTextFile(path.c_str()), true);

		for (const char *pattern : banned) {
			if (stripped.find(pattern) != std::string::npos
					&& !isAllowedPath(path, allowed)) {
				violations.push_back(path + ": " + pattern);
			}
		}
	}

	INFO("prop-named model wrapper usage outside catalog API:\n" << joinLines(violations));
	REQUIRE(violations.empty());
}

TEST_CASE("temporary ROM model fallbacks stay explicit and allowlisted", "[catalog][provider][static]")
{
	const std::vector<std::string> allowed = {};
	std::vector<std::string> violations;

	for (const std::string &path : repoSourceFiles()) {
		const std::string stripped = stripComments(readTextFile(path.c_str()), false);

		if (stripped.find("temporary ROM fallback") != std::string::npos
				&& !isAllowedPath(path, allowed)) {
			violations.push_back(path);
		}
	}

	const std::string bondgun = readTextFile("src/game/bondgun.c");
	const std::string player = readTextFile("src/game/player.c");
	const std::string menu = readTextFile("src/game/menu.c");
	const std::string mainmenu = readTextFile("src/game/mainmenu.c");
	const std::string mplayerSetup = readTextFile("src/game/mplayer/setup.c");
	const std::string title = readTextFile("src/game/title.c");
	const std::string modelcatalog = readTextFile("port/src/modelcatalog.c");
	const std::string setuputils = readTextFile("src/game/setuputils.c");

	REQUIRE(bondgun.find("temporary ROM fallback") == std::string::npos);
	REQUIRE(bondgun.find("assetLoadRomToAddr(player->gunctrl.loadfilenum") == std::string::npos);
	REQUIRE(bondgun.find("fileGetInflatedSize(player->gunctrl.loadfilenum") == std::string::npos);
	REQUIRE(player.find("temporary ROM fallback") == std::string::npos);
	REQUIRE(player.find("modeldefLoad((u16)wfn") == std::string::npos);
	REQUIRE(player.find("fileGetLoadedSize(wfn)") == std::string::npos);
	REQUIRE(player.find("player chrbody weapon modeldef") != std::string::npos);
	REQUIRE(player.find("assetSourceDebugFatalHandleFallback(ASSET_MODEL") != std::string::npos);
	REQUIRE(menu.find("temporary ROM fallback") == std::string::npos);
	REQUIRE(menu.find("modeldefLoad((u16)source_filenum") == std::string::npos);
	REQUIRE(menu.find("fileGetInflatedSize(source_filenum, LOADTYPE_MODEL)") == std::string::npos);
	REQUIRE(menu.find("menuModelHandlePassesSourceOnlyCheck(ASSET_BODY") != std::string::npos);
	REQUIRE(menu.find("menuModelHandlePassesSourceOnlyCheck(ASSET_HEAD") != std::string::npos);
	REQUIRE(menu.find("menuModelHandlePassesSourceOnlyCheck(ASSET_MODEL") != std::string::npos);
	REQUIRE(mainmenu.find("MENUMODELPARAMS_SET_FILENUM(weaponGetFileNum(weaponnum))") ==
	        std::string::npos);
	REQUIRE(mainmenu.find("weapon menu preview weaponnum=%d has no provider-backed catalog entry") !=
	        std::string::npos);
	REQUIRE(mplayerSetup.find("MENUMODELPARAMS_SET_FILENUM(catalogGetHeadFilenumByIndex(headnum))") ==
	        std::string::npos);
	REQUIRE(mplayerSetup.find("MP head preview headnum=%d has no provider-backed catalog entry") !=
	        std::string::npos);
	REQUIRE(title.find("temporary ROM fallback") == std::string::npos);
	REQUIRE(title.find("return modeldefLoad((u16)modelresult.filenum") == std::string::npos);
	REQUIRE(title.find("title model size") != std::string::npos);
	REQUIRE(title.find("assetSourceDebugFatalHandleFallback(ASSET_MODEL") != std::string::npos);
	REQUIRE(modelcatalog.find("catalogValidatePassesSourceOnlyCheck") != std::string::npos);
	REQUIRE(modelcatalog.find("modelcatalog body validation modeldef") != std::string::npos);
	REQUIRE(modelcatalog.find("modelcatalog head validation modeldef") != std::string::npos);
	REQUIRE(modelcatalog.find("catalogValidateSourceMissing(handle, filenum)") >
	        modelcatalog.find("catalogValidatePassesSourceOnlyCheck(index, ce->category, filenum, handle)"));
	REQUIRE(modelcatalog.find("temporary ROM fallback") == std::string::npos);
	REQUIRE(modelcatalog.find("modeldefLoadToNew(filenum)") == std::string::npos);
	REQUIRE(setuputils.find("setupModelHandlePassesSourceOnlyCheck(modelnum, model_id, model_handle)") <
	        setuputils.find("modeldefLoadToNewFromHandle(model_handle, source_filenum)"));
	REQUIRE(setuputils.find("setup modeldef") != std::string::npos);
	REQUIRE(setuputils.find("assetSourceDebugFatalHandleFallback(ASSET_MODEL") !=
	        std::string::npos);

	INFO("temporary ROM fallback wording outside allowlist:\n" << joinLines(violations));
	REQUIRE(violations.empty());
}

TEST_CASE("typed catalog model lifecycle activates model payloads", "[catalog][provider][static]")
{
	const std::string catalogLoad = readTextFile("port/src/assetcatalog_load.c");
	const std::string catalog = readTextFile("port/include/assetcatalog.h");
	const std::string api = readTextFile("port/include/assetcatalog_load.h");

	REQUIRE(catalog.find("ASSET_PAYLOAD_STAGE_MODELDEF") != std::string::npos);
	REQUIRE(catalogLoad.find("s_catalogLoadEntryModelPayload") != std::string::npos);
	REQUIRE(catalogLoad.find("s_catalogModelPayloadSourcePath") != std::string::npos);
	REQUIRE(catalogLoad.find("\"model.obj\"") != std::string::npos);
	REQUIRE(catalogLoad.find("\"model.gltf\"") != std::string::npos);
	REQUIRE(catalogLoad.find("\"model.glb\"") != std::string::npos);
	REQUIRE(catalogLoad.find("fsFileSize(candidate) > 0") != std::string::npos);
	REQUIRE(catalogLoad.find("modeldefLoadToNewFromHandle") != std::string::npos);
	REQUIRE(catalogLoad.find("model payload has no provider handle") != std::string::npos);
	REQUIRE(catalogLoad.find("s_catalogModelPayloadSourcePath(entry, source_path") <
	        catalogLoad.find("modeldefLoadToNewFromHandle(handle"));
	REQUIRE(catalogLoad.find("modAssetCompilerCompileReadable(entry") <
	        catalogLoad.find("modeldefLoadToNewFromHandle(handle"));
	const std::string modelPayloadBlock =
		functionBlock(catalogLoad, "static s32 s_catalogTypeUsesModelPayload");
	const std::string metadataPayloadBlock =
		functionBlock(catalogLoad, "static s32 s_catalogTypeUsesMetadataRuntimePayload");
	REQUIRE(!modelPayloadBlock.empty());
	REQUIRE(!metadataPayloadBlock.empty());
	REQUIRE(modelPayloadBlock.find("case ASSET_WEAPON:") == std::string::npos);
	REQUIRE(modelPayloadBlock.find("case ASSET_PROP:") == std::string::npos);
	REQUIRE(catalogLoad.find("|| type == ASSET_PROP") != std::string::npos);
	REQUIRE(catalogLoad.find("|| type == ASSET_WEAPON") != std::string::npos);
	REQUIRE(catalogLoad.find("s_catalogLoadEntryAudioPayload(entry, handle)") <
	        catalogLoad.find("CATALOG: retain bundled"));
	REQUIRE(catalogLoad.find("s_catalogLoadEntryTexturePayload(entry, handle)") <
	        catalogLoad.find("CATALOG: retain bundled"));
	REQUIRE(catalogLoad.find("catalogGetLoadedModeldef") != std::string::npos);
	REQUIRE(api.find("catalogGetLoadedModeldef") != std::string::npos);
}

TEST_CASE("Modding Hub model scale tool reads mesh source scale", "[catalog][provider][static]")
{
	const std::string hub = readTextFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	const std::string scaleSelectBlock = functionBlock(hub, "static void scaleSelectEntry");
	const std::string bodyCollectBlock = functionBlock(hub, "static void scaleCollectBodyCallback");

	REQUIRE(hub.find("scaleReadSourceScale") != std::string::npos);
	REQUIRE(hub.find("iniParseBuffer") != std::string::npos);
	REQUIRE(hub.find("\"model_scale\"") != std::string::npos);
	REQUIRE(hub.find("scaleBuildMeshIniFromBodyArchive") != std::string::npos);
	REQUIRE(hub.find("scaleBuildMeshIniSource(e->ext.body.mesh_archive") !=
	        std::string::npos);
	REQUIRE(hub.find("catalogResolveFile(e->source_filenum)") == std::string::npos);
	REQUIRE(hub.find("readModelScale") == std::string::npos);
	REQUIRE(hub.find("writeModelScale") == std::string::npos);
	REQUIRE(hub.find("fseek(f, 0x10") == std::string::npos);
	REQUIRE(hub.find("Bake Scale to File") == std::string::npos);
	REQUIRE(hub.find("model binary") == std::string::npos);
	REQUIRE(!scaleSelectBlock.empty());
	REQUIRE(scaleSelectBlock.find("scaleReadSourceScale(se.scale_source") !=
	        std::string::npos);
	REQUIRE(!bodyCollectBlock.empty());
	REQUIRE(bodyCollectBlock.find("catalogResolveFile") == std::string::npos);
	REQUIRE(bodyCollectBlock.find("source_filenum") == std::string::npos);
}

TEST_CASE("model previews accept source-backed handles without legacy file numbers",
          "[catalog][provider][static]")
{
	const std::string charpreview = readTextFile("port/fast3d/pdgui_charpreview.c");
	const std::string preview_header = readTextFile("port/include/pdgui_charpreview.h");
	const std::string menu_header = readTextFile("src/include/game/menu.h");
	const std::string menu = readTextFile("src/game/menu.c");
	const std::string requestBlock = functionBlock(charpreview,
		"void pdguiCharPreviewRequestEx");
	const std::string handleBlock = functionBlock(charpreview,
		"void pdguiCharPreviewRequestHandle");
	const std::string menuSetHandleBlock = functionBlock(menu,
		"void menuSetModelFileHandle");

	REQUIRE(menu_header.find("MENUMODEL_HANDLE_SENTINEL_FILENUM 0xfffe") !=
	        std::string::npos);
	REQUIRE(charpreview.find("CHARPREVIEW_HANDLE_SENTINEL_FILENUM") ==
	        std::string::npos);
	REQUIRE(charpreview.find("MENUMODEL_HANDLE_SENTINEL_FILENUM") !=
	        std::string::npos);
	REQUIRE(charpreview.find("catalogEffectiveHandle(e)") != std::string::npos);
	REQUIRE(charpreview.find("pdguiCharPreviewRequestHandle(type, handle, filenum)") !=
	        std::string::npos);
	REQUIRE(charpreview.find("menuSetModelFileHandle(&g_Menus[playernum].menumodel") !=
	        std::string::npos);
	REQUIRE(preview_header.find("pdguiCharPreviewRequestHandle") !=
	        std::string::npos);
	REQUIRE(preview_header.find("direct catalog provider handle") !=
	        std::string::npos);
	REQUIRE(!requestBlock.empty());
	REQUIRE(requestBlock.find("e->source_filenum > 0") <
	        requestBlock.find("catalogEffectiveHandle(e)"));
	REQUIRE(!handleBlock.empty());
	REQUIRE(handleBlock.find("menuSetModelFileHandle") <
	        handleBlock.find("charPreviewSubmitParams"));
	REQUIRE(!menuSetHandleBlock.empty());
	REQUIRE(menuSetHandleBlock.find("MENUMODELPARAMS_GET_FILENUM(source_filenum) == 0xffff") !=
	        std::string::npos);
	REQUIRE(menuSetHandleBlock.find("request_filenum = MENUMODEL_HANDLE_SENTINEL_FILENUM") !=
	        std::string::npos);
	REQUIRE(menuSetHandleBlock.find("MENUMODELPARAMS_SET_FILENUM(request_filenum)") !=
	        std::string::npos);
	REQUIRE(menuSetHandleBlock.find("newhandle_filenum = request_filenum") !=
	        std::string::npos);
}

TEST_CASE("Training and Hangar previews use catalog/provider source before raw filenum",
          "[catalog][provider][static]")
{
	const std::string training = readTextFile("port/fast3d/pdgui_menu_training.cpp");
	const std::string bridge = readTextFile("port/fast3d/pdgui_bridge.c");
	const std::string requestBlock = functionBlock(training,
		"static void requestSourcePreview");
	const std::string drawBlock = functionBlock(training,
		"static void drawSourcePreview");
	const std::string detailsBlock = functionBlock(training,
		"static s32 renderTrainingDetailsImpl");
	const std::string dtIdBlock = functionBlock(bridge,
		"const char *pdguiTrDtCurrentWeaponCatalogId");
	const std::string htIdBlock = functionBlock(bridge,
		"const char *pdguiTrHtCurrentWeaponCatalogId");
	const std::string hangarIdBlock = functionBlock(bridge,
		"const char *pdguiTrHangarCurrentVehicleCatalogId");

	REQUIRE(training.find("#include \"assetcatalog.h\"") != std::string::npos);
	REQUIRE(training.find("drawFilenumPreview") == std::string::npos);
	REQUIRE(!requestBlock.empty());
	REQUIRE(requestBlock.find("assetCatalogResolve(catalogId)") != std::string::npos);
	REQUIRE(requestBlock.find("catalogHandleByModelSourceFilenum") != std::string::npos);
	REQUIRE(requestBlock.find("pdguiCharPreviewRequestHandle") <
	        requestBlock.find("pdguiCharPreviewRequestFilenum"));
	REQUIRE(requestBlock.find("pdguiCharPreviewRequestEx") != std::string::npos);
	REQUIRE(!drawBlock.empty());
	REQUIRE(drawBlock.find("requestSourcePreview(kind, preferredType, catalogId, filenum)") !=
	        std::string::npos);
	REQUIRE(!detailsBlock.empty());
	REQUIRE(detailsBlock.find("const char *weaponCatalogId") != std::string::npos);
	REQUIRE(detailsBlock.find("drawSourcePreview(PDGUI_PREVIEW_WEAPON, ASSET_WEAPON") !=
	        std::string::npos);
	REQUIRE(training.find("pdguiTrDtCurrentWeaponCatalogId()") != std::string::npos);
	REQUIRE(training.find("pdguiTrHtCurrentWeaponCatalogId()") != std::string::npos);
	REQUIRE(training.find("pdguiTrHangarCurrentVehicleCatalogId()") != std::string::npos);
	REQUIRE(training.find("drawSourcePreview(PDGUI_PREVIEW_VEHICLE, ASSET_VEHICLE") !=
	        std::string::npos);
	REQUIRE(!dtIdBlock.empty());
	REQUIRE(dtIdBlock.find("catalogWeaponIdByRuntimeWeaponNum") != std::string::npos);
	REQUIRE(!htIdBlock.empty());
	REQUIRE(htIdBlock.find("catalogWeaponIdByRuntimeWeaponNum") != std::string::npos);
	REQUIRE(!hangarIdBlock.empty());
	REQUIRE(hangarIdBlock.find("catalogIdBySourceFilenum(ASSET_VEHICLE") !=
	        std::string::npos);
	REQUIRE(hangarIdBlock.find("catalogIdBySourceFilenum(ASSET_MODEL") !=
	        std::string::npos);
}

TEST_CASE("weapon graph and prop model files populate provider handles", "[catalog][provider][static]")
{
	const std::string api = readTextFile("port/src/assetcatalog_api.c");
	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	const std::string distrib = readTextFile("port/src/net/netdistrib.c");

	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.weapon.primary_graph)") != std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.weapon.model_file)") == std::string::npos);
	REQUIRE(api.find("static asset_data_handle_t s_weaponModelHandle") != std::string::npos);
	REQUIRE(api.find("catalogHandleForSourceFile(e->ext.weapon.model_file)") != std::string::npos);
	REQUIRE(api.find("catalogHandleByModelSourceFilenum(ASSET_MODEL, e->source_filenum)") != std::string::npos);
	REQUIRE(api.find("out->handle     = s_weaponModelHandle(e)") != std::string::npos);
	REQUIRE(scanner.find("e->ext.weapon.primary_graph") != std::string::npos);
	REQUIRE(scanner.find("e->ext.weapon.secondary_graph") != std::string::npos);
	REQUIRE(scanner.find("e->ext.weapon.shared_context") != std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.prop.model_file)") != std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.prop.behavior_graph)") == std::string::npos);
	REQUIRE(scanner.find("const char *pf = e->ext.vehicle.model_file[0]") != std::string::npos);
	REQUIRE(scanner.find("e->ext.vehicle.behavior_graph") != std::string::npos);
	REQUIRE(scanner.find("e->ext.vehicle.behavior_graph : e->ext.vehicle.physics_file") == std::string::npos);
	REQUIRE(distrib.find("#include \"assetprovider.h\"") == std::string::npos);
	REQUIRE(distrib.find("\"prop.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"mesh.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"model.ini\"") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.character.bodyfile)") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.weapon.primary_graph)") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.weapon.model_file)") == std::string::npos);
	REQUIRE(distrib.find("e->ext.weapon.primary_graph") != std::string::npos);
	REQUIRE(distrib.find("e->ext.weapon.secondary_graph") != std::string::npos);
	REQUIRE(distrib.find("e->ext.weapon.shared_context") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.prop.model_file)") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.prop.behavior_graph)") == std::string::npos);
	REQUIRE(distrib.find("const char *pf = e->ext.vehicle.model_file[0]") != std::string::npos);
	REQUIRE(distrib.find("e->ext.vehicle.behavior_graph") != std::string::npos);
	REQUIRE(distrib.find("e->ext.vehicle.behavior_graph : e->ext.vehicle.physics_file") == std::string::npos);
	REQUIRE(distrib.find("case ASSET_MODEL:") != std::string::npos);
	REQUIRE(distrib.find("assetPathJoinChecked(fullpath, sizeof(fullpath), dirpath, \"/\",") != std::string::npos);
	REQUIRE(distrib.find("catalogSetPrimaryFile(e, path)") != std::string::npos);
	REQUIRE(distrib.find("preserved_weapon_id = preserved_entry->ext.weapon.weapon_id") != std::string::npos);
	REQUIRE(distrib.find("preserved_runtime_index = preserved_entry->runtime_index") != std::string::npos);
	REQUIRE(distrib.find("preserved_mp_index = preserved_entry->mp_index") != std::string::npos);
	REQUIRE(distrib.find("preserved_weapon_requirefeature = preserved_entry->ext.weapon.requirefeature") != std::string::npos);
	REQUIRE(distrib.find("weapon_name[0] ? weapon_name : preserved_weapon_name") != std::string::npos);
	REQUIRE(distrib.find("e->ext.weapon.requirefeature = preserved_weapon_requirefeature") != std::string::npos);
}

TEST_CASE("MP body and head selector identity covers catalog-backed custom rows", "[catalog][provider][static]")
{
	const std::string api = readTextFile("port/src/assetcatalog_api.c");
	const std::string modmgr = readTextFile("port/src/modmgr.c");
	const std::string bodyIdBlock = functionBlock(api, "const char *catalogMpBodyId");
	const std::string headIdBlock = functionBlock(api, "const char *catalogMpHeadId");
	const std::string defaultHeadBlock = functionBlock(api, "s32 catalogGetBodyDefaultMpHeadIdx");
	const std::string displayNameBlock = functionBlock(api, "const char *catalogGetBodyDisplayName");

	REQUIRE(api.find("#define CATALOG_MP_SELECT_CACHE_COUNT 256") != std::string::npos);
	REQUIRE(api.find("s_MpBodyIdCache[CATALOG_MP_SELECT_CACHE_COUNT]") != std::string::npos);
	REQUIRE(api.find("s_MpHeadIdCache[CATALOG_MP_SELECT_CACHE_COUNT]") != std::string::npos);
	REQUIRE(api.find("assetCatalogIterateByType(ASSET_BODY, catalogAssignBodyMpIndex") !=
	        std::string::npos);
	REQUIRE(api.find("assetCatalogIterateByType(ASSET_HEAD, catalogAssignHeadMpIndex") !=
	        std::string::npos);

	REQUIRE(!bodyIdBlock.empty());
	REQUIRE(!headIdBlock.empty());
	REQUIRE(bodyIdBlock.find("CATALOG_MP_SELECT_CACHE_COUNT") != std::string::npos);
	REQUIRE(headIdBlock.find("CATALOG_MP_SELECT_CACHE_COUNT") != std::string::npos);
	REQUIRE(bodyIdBlock.find("catalogFindMpIndexedAssetId(ASSET_BODY, mp_idx)") !=
	        std::string::npos);
	REQUIRE(headIdBlock.find("catalogFindMpIndexedAssetId(ASSET_HEAD, mp_idx)") !=
	        std::string::npos);
	REQUIRE(bodyIdBlock.find("MP_BODY_BASE_COUNT") == std::string::npos);
	REQUIRE(headIdBlock.find("MP_HEAD_BASE_COUNT") == std::string::npos);

	REQUIRE(!defaultHeadBlock.empty());
	REQUIRE(!displayNameBlock.empty());
	REQUIRE(defaultHeadBlock.find("catalogMpBodyId(mpbodynum)") != std::string::npos);
	REQUIRE(defaultHeadBlock.find("MP_BODY_BASE_COUNT") == std::string::npos);
	REQUIRE(displayNameBlock.find("catalogMpBodyId(mpbodynum)") != std::string::npos);
	REQUIRE(displayNameBlock.find("MP_BODY_BASE_COUNT") == std::string::npos);
	REQUIRE(countOccurrences(modmgr, "((asset_entry_t *)entry)->mp_index = (s16)idx;") >= 2);
}

TEST_CASE("Forge runtime model spawns enforce source-only family handles", "[catalog][provider][static]")
{
	const std::string forge = readTextFile("port/src/forge/forge_runtime.c");
	const std::string modeldef = readTextFile("src/game/modeldef.c");
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");

	REQUIRE(forge.find("#include \"asset_source_debug.h\"") != std::string::npos);
	REQUIRE(forge.find("s_forgeModelHandlePassesSourceOnlyCheck") != std::string::npos);
	REQUIRE(forge.find("assetSourceDebugFatalHandleFallback(type, context, catalog_id, handle)") !=
	        std::string::npos);
	REQUIRE(forge.find("assetSourceDebugHandleRequiresPublicFileSource(type, handle)") !=
	        std::string::npos);
	REQUIRE(forge.find("|| pr.filenum <= 0") == std::string::npos);
	REQUIRE(forge.find("|| wr.filenum <= 0") == std::string::npos);
	REQUIRE(countOccurrences(forge, "assetHandleIsNull(pr.handle)") >= 2);
	REQUIRE(forge.find("assetHandleIsNull(wr.handle)") != std::string::npos);
	REQUIRE(forge.find("s_forgeModelHandlePassesSourceOnlyCheck(ASSET_PROP, o->catalog_id") <
	        forge.find("modeldefLoadToNewFromHandle(pr.handle, pr.filenum)"));
	REQUIRE(forge.find("s_forgeModelHandlePassesSourceOnlyCheck(ASSET_WEAPON") <
	        forge.find("modeldefLoadToNewFromHandle(wr.handle, wr.filenum)"));
	REQUIRE(forge.find("forge door modeldef") != std::string::npos);
	REQUIRE(forge.find("forge weapon pad modeldef") != std::string::npos);
	REQUIRE(forge.find("forge prop modeldef") != std::string::npos);
	REQUIRE(modeldef.find("modeldefResolveExternalSourcePath") != std::string::npos);
	REQUIRE(modeldef.find("\"model.obj\"") != std::string::npos);
	REQUIRE(modeldef.find("\"model.gltf\"") != std::string::npos);
	REQUIRE(modeldef.find("\"model.glb\"") != std::string::npos);
	REQUIRE(modeldef.find("entry->ext.weapon.model_file") != std::string::npos);
	REQUIRE(modeldef.find("entry->ext.prop.model_file") != std::string::npos);
	REQUIRE(modeldef.find("entry->ext.vehicle.model_file") != std::string::npos);
	REQUIRE(modeldef.find("modAssetCompilerBuildModeldef(entry, resolved_source_path") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_forge_runtime_source_only_guards(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("FORGE_RUNTIME_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
}

TEST_CASE("setup modeldef loads enforce source-only model handles", "[catalog][provider][static]")
{
	const std::string setuputils = readTextFile("src/game/setuputils.c");
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");

	REQUIRE(setuputils.find("#include \"asset_source_debug.h\"") != std::string::npos);
	REQUIRE(setuputils.find("setupModelHandlePassesSourceOnlyCheck") != std::string::npos);
	REQUIRE(setuputils.find("assetSourceDebugFatalHandleFallback(ASSET_MODEL") !=
	        std::string::npos);
	REQUIRE(setuputils.find("assetSourceDebugHandleRequiresPublicFileSource(ASSET_MODEL, handle)") !=
	        std::string::npos);
	REQUIRE(setuputils.find("setup modeldef") != std::string::npos);
	REQUIRE(setuputils.find("setupModelHandlePassesSourceOnlyCheck(modelnum, model_id, model_handle)") <
	        setuputils.find("modeldefLoadToNewFromHandle(model_handle, source_filenum)"));
	REQUIRE(guard.find("scan_setup_modeldef_source_only_guard(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("SETUP_MODELDEF_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
}

TEST_CASE("body and head managers enforce source-only family handles before modeldef fallback", "[catalog][provider][static]")
{
	const std::string bodyMgr = readTextFile("port/src/catalog_mgr_bodies.c");
	const std::string headMgr = readTextFile("port/src/catalog_mgr_heads.c");
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");

	REQUIRE(bodyMgr.find("#include \"asset_source_debug.h\"") != std::string::npos);
	REQUIRE(bodyMgr.find("catalogManagerBodyModeldefPassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(bodyMgr.find("assetSourceDebugFatalHandleFallback(ASSET_BODY") !=
	        std::string::npos);
	REQUIRE(bodyMgr.find("assetSourceDebugHandleRequiresPublicFileSource(ASSET_BODY, handle)") !=
	        std::string::npos);
	REQUIRE(bodyMgr.find("body manager modeldef fallback") != std::string::npos);
	REQUIRE(bodyMgr.find("catalogManagerBodyModeldefPassesSourceOnlyCheck(bodynum, id") <
	        bodyMgr.find("modeldefLoadToNewFromHandle(handle"));

	REQUIRE(headMgr.find("#include \"asset_source_debug.h\"") != std::string::npos);
	REQUIRE(headMgr.find("catalogManagerHeadModeldefPassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(headMgr.find("assetSourceDebugFatalHandleFallback(ASSET_HEAD") !=
	        std::string::npos);
	REQUIRE(headMgr.find("assetSourceDebugHandleRequiresPublicFileSource(ASSET_HEAD, handle)") !=
	        std::string::npos);
	REQUIRE(headMgr.find("head manager modeldef fallback") != std::string::npos);
	REQUIRE(headMgr.find("catalogManagerHeadModeldefPassesSourceOnlyCheck(headnum, id") <
	        headMgr.find("modeldefLoadToNewFromHandle(handle"));

	REQUIRE(guard.find("scan_character_manager_modeldef_source_only_guards(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("CHARACTER_MANAGER_MODELDEF_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
}

TEST_CASE("music sequencer enforces source-only audio before legacy fallback", "[catalog][provider][static]")
{
	const std::string load = readTextFile("port/src/assetcatalog_load.c");
	const std::string loadH = readTextFile("port/include/assetcatalog_load.h");
	const std::string mod = readTextFile("port/src/mod.c");
	const std::string snd = readTextFile("src/lib/snd.c");
	const std::string mplayer = readTextFile("src/game/mplayer/mplayer.c");
	const std::string audio = readTextFile("port/src/audio.c");
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");
	const std::string modSequenceLoad = functionBlock(mod, "void *modSequenceLoad");

	REQUIRE(loadH.find("catalogResolveMusicSequence") != std::string::npos);
	REQUIRE(loadH.find("Public .pdsong track audio is routed through the streaming music path") !=
	        std::string::npos);
	REQUIRE(loadH.find("Public sequence.mid, sequence.json, and music.ini sources are") !=
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
	REQUIRE(load.find("assetSourceDebugIsEnabledFor(ASSET_AUDIO)") ==
	        std::string::npos);

	REQUIRE(mod.find("catalogResolveMusicSequence((s32)num)") !=
	        std::string::npos);
	REQUIRE(mod.find("#include \"asset_source_debug.h\"") ==
	        std::string::npos);
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
	REQUIRE(modSequenceLoad.find("catalogResolveMusicSequence((s32)num)") <
	        modSequenceLoad.find("modSequenceCompilePublicSource(&r, num, outSize)"));
	REQUIRE(modSequenceLoad.find("modSequenceCompilePublicSource(&r, num, outSize)") <
	        modSequenceLoad.find("r.source_only_blocked"));
	REQUIRE(modSequenceLoad.find("modSequenceCompilePublicSource(&r, num, outSize)") <
	        modSequenceLoad.find("fsFileSize(MOD_SEQUENCES_DIR \"/\")"));
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

	REQUIRE(guard.find("scan_music_sequence_source_only_guard(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("MUSIC_SEQUENCE_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
}

TEST_CASE("animation source-only refuses ROM DMA fallback after public source failure", "[catalog][provider][static]")
{
	const std::string mod = readTextFile("port/src/mod.c");
	const std::string anim = readTextFile("src/lib/anim.c");
	const std::string load_data =
		functionBlock(mod, "void *modAnimationLoadData");
	const std::string try_override =
		functionBlock(mod, "void *modAnimationTryCatalogOverride");
	const std::string load_frame = functionBlock(anim, "u8 animLoadFrame");
	const std::string load_header = functionBlock(anim, "void animLoadHeader");

	REQUIRE(mod.find("modAnimationFatalPublicSourceFailure") !=
	        std::string::npos);
	REQUIRE(mod.find("ASSET.SOURCE_ONLY: animation %d maps to public animation source") !=
	        std::string::npos);
	REQUIRE(mod.find("refusing ROM/static fallback") != std::string::npos);
	REQUIRE(load_data.find("catalogResolveAnim((s32)num)") !=
	        std::string::npos);
	REQUIRE(load_data.find("r.source_only_blocked") != std::string::npos);
	REQUIRE(load_data.find("assetSourceDebugIsEnabledFor(ASSET_ANIMATION)") ==
	        std::string::npos);
	REQUIRE(load_data.find("runtime clip compilation failed") !=
	        std::string::npos);
	REQUIRE(load_data.find("the selected public source is not editable GLTF/GLB animation source") !=
	        std::string::npos);
	REQUIRE(load_data.find("fsFileLoad(r.path") == std::string::npos);

	REQUIRE(try_override.find("catalogResolveAnim((s32)num)") !=
	        std::string::npos);
	REQUIRE(try_override.find("catalogGetAnimOverride") == std::string::npos);
	REQUIRE(try_override.find("r.source_only_blocked") != std::string::npos);
	REQUIRE(try_override.find("assetSourceDebugIsEnabledFor(ASSET_ANIMATION)") ==
	        std::string::npos);
	REQUIRE(try_override.find("runtime clip compilation failed") !=
	        std::string::npos);
	REQUIRE(try_override.find("the selected public source is not editable GLTF/GLB animation source") !=
	        std::string::npos);
	REQUIRE(try_override.find("fsFileLoad(path") == std::string::npos);

	REQUIRE(load_frame.find("modAnimationTryCatalogOverride(animnum)") <
	        load_frame.find("animDma(&g_AnimFrameByteSlots"));
	REQUIRE(load_header.find("modAnimationTryCatalogOverride(animnum)") <
	        load_header.find("animDma(&g_AnimHeaderByteSlots"));
}

TEST_CASE("MP3 file use enforces bounded source-only audio before ROM fallback", "[catalog][provider][static][b1036]")
{
	const std::string snd = readTextFile("src/lib/snd.c");
	const std::string mp3 = readTextFile("src/lib/mp3.c");
	const std::string adma = readTextFile("src/lib/audiodma.c");
	const std::string propsnd = readTextFile("src/game/propsnd.c");
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");
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
	REQUIRE(snd.find("static void sndMp3FreeSourceBuffer(void)") !=
	        std::string::npos);
	REQUIRE(snd.find("static s32 sndMp3LoadPublicSourceFile") !=
	        std::string::npos);
	REQUIRE(snd.find("static s32 sndMp3ResolvePublicSource") !=
	        std::string::npos);
	REQUIRE(snd.find("catalogResolveFile(filenum)") != std::string::npos);
	REQUIRE(snd.find("fsFileLoad(source.path, &size)") != std::string::npos);
	REQUIRE(snd.find("romExtractRelPathForFilenum") == std::string::npos);
	REQUIRE(snd.find("g_SndMp3SourceBytes = bytes") != std::string::npos);
	REQUIRE(snd.find("assetSourceDebugIsEnabledFor(ASSET_AUDIO)") ==
	        std::string::npos);
	REQUIRE(snd.find("ASSET.CHAIN: MP3 file") !=
	        std::string::npos);
	REQUIRE(snd.find("refusing loose extracted file or ROM playback fallback") !=
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
	REQUIRE(snd_mp3_load.find("mp3SourceAllocationSize(size, &allocation_size)") !=
	        std::string::npos);
	REQUIRE(snd_mp3_load.find("sysMemRealloc(bytes, allocation_size)") !=
	        std::string::npos);
	REQUIRE(snd_mp3_load.find("bzero((u8 *)bytes + size, allocation_size - size)") !=
	        std::string::npos);
	const std::string mp3_read = functionBlock(mp3,
		"s32 func00038ba8(s32 arg0, u8 *arg1, s32 arg2, s32 arg3)\n{");
	const std::string mp3_prefetch = functionBlock(mp3, "void mp3Dma(void)");
	REQUIRE(mp3_read.find("mp3SourceClampRead(g_Mp3Vars.filesize") !=
	        std::string::npos);
	REQUIRE(mp3_read.find("if (arg2 == 0)") != std::string::npos);
	REQUIRE(mp3_prefetch.find("mp3SourceClampRead(g_Mp3Vars.filesize") !=
	        std::string::npos);
	REQUIRE(mp3_prefetch.find("if (length == 0)") != std::string::npos);
	REQUIRE(mp3_prefetch.find("g_Mp3Vars.var8009c3c4, length, 0") !=
	        std::string::npos);
	REQUIRE(adma.find("ADMA_ITEM_SIZE 0x400") != std::string::npos);
	REQUIRE(adma.find("foundbuffer, ADMA_ITEM_SIZE") != std::string::npos);
	REQUIRE(propsnd.find("#include \"asset_source_debug.h\"") ==
	        std::string::npos);
	REQUIRE(propsnd.find("#include \"fs.h\"") != std::string::npos);
	REQUIRE(propsnd.find("#include \"romextract.h\"") == std::string::npos);
	REQUIRE(propsnd.find("#include \"assetcatalog_load.h\"") !=
	        std::string::npos);
	REQUIRE(propsnd.find("static s32 psMp3DurationGetPublicSourceSize") !=
	        std::string::npos);
	REQUIRE(propsnd.find("catalogResolveFile(filenum)") != std::string::npos);
	REQUIRE(propsnd.find("fsFileSize(source.path)") != std::string::npos);
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

	REQUIRE(guard.find("scan_mp3_audio_source_only_guard(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("MP3_AUDIO_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
	REQUIRE(guard.find("MP3_DURATION_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
}

TEST_CASE("file-source sound playback failure refuses source-only ROM fallback", "[catalog][provider][static]")
{
	const std::string snd = readTextFile("src/lib/snd.c");
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");

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

	REQUIRE(guard.find("scan_sound_file_source_only_guard(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("SOUND_FILE_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
}

TEST_CASE("file-source sound handles preserve native keymap pan volume and fx state", "[catalog][provider][static]")
{
	const std::string audio = readTextFile("port/src/audio.c");
	const std::string header = readTextFile("port/include/audio.h");
	const std::string modmusic = readTextFile("port/src/modmusic.c");
	const std::string modmusic_h = readTextFile("port/include/modmusic.h");
	const std::string file_sound_mix =
		functionBlock(audio, "static void audioMixFileSoundsInto");
	const std::string file_sound_load =
		functionBlock(audio, "static s32 audioLoadFilePcmStereo22050");

	REQUIRE(header.find("u8 sample_pan, u8 sample_volume") != std::string::npos);
	REQUIRE(header.find("u8 key_volume_index") != std::string::npos);
	REQUIRE(header.find("u8 fxmix, u8 fxbus") != std::string::npos);
	REQUIRE(header.find("u8 fxmix_key_offset") != std::string::npos);
	REQUIRE(header.find("f32 pitch, f32 base_pitch") != std::string::npos);
	REQUIRE(header.find("standard audio file") != std::string::npos);
	REQUIRE(modmusic_h.find("modMusicLoadAudioPcm22050") !=
	        std::string::npos);
	REQUIRE(modmusic.find("s16 *modMusicLoadAudioPcm22050") !=
	        std::string::npos);
	REQUIRE(modmusic.find("modmusic_loadMp3(path, outLen, outSourceRate)") !=
	        std::string::npos);
	REQUIRE(modmusic.find("modmusic_loadOgg(path, outLen, outSourceRate)") !=
	        std::string::npos);
	REQUIRE(modmusic.find("samplesPerChannel = stb_vorbis_decode_memory") !=
	        std::string::npos);
	REQUIRE(modmusic.find("samplesPerChannel = stb_vorbis_decode_filename") !=
	        std::string::npos);
	REQUIRE(modmusic.find("u32 totalSourceSamples = (u32)samplesPerChannel * (u32)channels") !=
	        std::string::npos);
	REQUIRE(modmusic.find("u32 rawBytes = totalSourceSamples * sizeof(s16)") !=
	        std::string::npos);
	REQUIRE(modmusic.find("*outLen = (u32)samplesPerChannel * (u32)channels") !=
	        std::string::npos);
	REQUIRE(modmusic.find("rawBytes = (u32)totalSamples * sizeof(s16)") ==
	        std::string::npos);
	REQUIRE(file_sound_load.find("SDL_LoadWAV(path, &wavSpec, &wavBuf, &wavLen)") <
	        file_sound_load.find("modMusicLoadAudioPcm22050(path, &decoded_samples"));
	REQUIRE(file_sound_load.find("*out_frames = decoded_samples / 2u") !=
	        std::string::npos);
	REQUIRE(audio.find("u8 sample_pan;") != std::string::npos);
	REQUIRE(audio.find("u8 sample_volume;") != std::string::npos);
	REQUIRE(audio.find("u8 key_volume_index;") != std::string::npos);
	REQUIRE(audio.find("extern u16  func00033ec4(u8 index)") != std::string::npos);
	REQUIRE(audio.find("#define AUDIO_FILE_SOUND_KEY_VOLUME_COUNT 9") !=
	        std::string::npos);
	REQUIRE(audio.find("static u16 audioFileSoundKeyVolume") != std::string::npos);
	REQUIRE(audio.find("return func00033ec4(index)") != std::string::npos);
	REQUIRE(audio.find("return volume ? volume : 0x7fff") == std::string::npos);
	REQUIRE(audio.find("static u16 audioFileSoundEffectiveVolume") != std::string::npos);
	REQUIRE(audio.find("((u32)volume * (u32)sample_volume) / 127u") !=
	        std::string::npos);
	REQUIRE(audio.find("audioFileSoundKeyVolume(key_volume_index)") !=
	        std::string::npos);
	REQUIRE(audio.find("static u8 audioFileSoundEffectivePan") != std::string::npos);
	REQUIRE(audio.find("(s32)pan + (s32)sample_pan - AL_PAN_CENTER") !=
	        std::string::npos);
	REQUIRE(audio.find("#define AL_SNDP_FX_EVT    0x0100") != std::string::npos);
	REQUIRE(audio.find("#define AL_SNDP_FXBUS_EVT 0x2000") != std::string::npos);
	REQUIRE(audio.find("#define AL_SNDP_4000_EVT  0x4000") != std::string::npos);
	REQUIRE(audio.find("#define AUDIO_FILE_SOUND_FX_BUS_COUNT 2") !=
	        std::string::npos);
	REQUIRE(audio.find("s_FileSoundFxDelay") != std::string::npos);
	REQUIRE(audio.find("s_FileSoundFxSend") != std::string::npos);
	REQUIRE(audio.find("static u8 audioFileSoundEffectiveFxmix") != std::string::npos);
	REQUIRE(audio.find("(raw_fxmix & 0x7f) + key_offset") != std::string::npos);
	REQUIRE(audio.find("return (u8)fxmix | (raw_fxmix & 0x80)") != std::string::npos);
	REQUIRE(audio.find("static f32 audioFileSoundFxSendScale") !=
	        std::string::npos);
	REQUIRE(audio.find("static void audioFileSoundFxAddSend") !=
	        std::string::npos);
	REQUIRE(audio.find("static void audioFileSoundFxMixInto") !=
	        std::string::npos);
	REQUIRE(audio.find("audioFileSoundFxAddSend(slot->state.fxbus, slot->effective_fxmix") !=
	        std::string::npos);
	REQUIRE(audio.find("audioFileSoundFxMixInto(dst, frames)") !=
	        std::string::npos);
	REQUIRE(audio.find("audioFileSoundsActive() ||") != std::string::npos);
	REQUIRE(audio.find("audioFileSoundFxActive()") != std::string::npos);
	REQUIRE(audio.find("slot->state.fxmix = fxmix") != std::string::npos);
	REQUIRE(audio.find("slot->state.fxbus = (fxbus >= 2) ? 0 : fxbus") !=
	        std::string::npos);
	REQUIRE(audio.find("slot->sample_pan = sample_pan") != std::string::npos);
	REQUIRE(audio.find("slot->sample_volume = sample_volume") != std::string::npos);
	REQUIRE(audio.find("slot->key_volume_index = key_volume_index & 0x1f") !=
	        std::string::npos);
	REQUIRE(audio.find("slot->volume = audioFileSoundEffectiveVolume(volume, sample_volume,") !=
	        std::string::npos);
	REQUIRE(audio.find("slot->pan = audioFileSoundEffectivePan(pan, sample_pan)") !=
	        std::string::npos);
	REQUIRE(audio.find("slot->volume = audioFileSoundEffectiveVolume((u16)data,") !=
	        std::string::npos);
	REQUIRE(audio.find("slot->sample_volume, slot->key_volume_index") !=
	        std::string::npos);
	REQUIRE(audio.find("slot->volume = audioFileSoundEffectiveVolume((u16)slot->state.vol,") !=
	        std::string::npos);
	REQUIRE(file_sound_mix.find("g_AudioMasterVolume * g_AudioGameplayVolume") ==
	        std::string::npos);
	REQUIRE(audio.find("slot->pan = audioFileSoundEffectivePan((u8)data, slot->sample_pan)") !=
	        std::string::npos);
	REQUIRE(audio.find("slot->base_pitch = base_pitch") != std::string::npos);
	REQUIRE(audio.find("slot->step = pitch * base_pitch") != std::string::npos);
	REQUIRE(audio.find("slot->state.basepitch = base_pitch") !=
	        std::string::npos);
	REQUIRE(audio.find("slot->step = slot->state.pitch * slot->base_pitch") !=
	        std::string::npos);
	REQUIRE(audio.find("case AL_SNDP_FX_EVT:") != std::string::npos);
	REQUIRE(audio.find("case AL_SNDP_4000_EVT:") != std::string::npos);
	REQUIRE(audio.find("case AL_SNDP_FXBUS_EVT:") != std::string::npos);
}

TEST_CASE("base first-person hand model files populate provider handles", "[catalog][provider][static]")
{
	const std::string baseExtended = readTextFile("port/src/assetcatalog_base_extended.c");
	const std::string bodyWalker = readTextFile("port/src/loader_walker_body.c");
	const std::string headWalker = readTextFile("port/src/loader_walker_head.c");
	const std::string bondgun = readTextFile("src/game/bondgun.c");

	/* BYOR completion (2026-05-03): hand-model probe migrated from
	 * g_HeadsAndBodies[i].handfilenum -> g_BodyData[i].handfilenum
	 * (authoring table). Pin updated to match the new source. */
	REQUIRE(baseExtended.find("g_BodyData[i].handfilenum") != std::string::npos);
	REQUIRE(baseExtended.find("catalogReadableModelIdForFile(handfilenum, \"hand\", \"hand\"") != std::string::npos);
	REQUIRE(baseExtended.find("e->runtime_index = -handfilenum") != std::string::npos);
	REQUIRE(baseExtended.find("e->source_filenum = handfilenum") != std::string::npos);
	REQUIRE(baseExtended.find("catalogBindPrimaryFromDiskOrRom(e, e->source_filenum)") != std::string::npos);
	REQUIRE(bodyWalker.find("loaderWalkerEnvelopePathCopy(manifest, manifest_len, \"hand_archive\"") != std::string::npos);
	REQUIRE(bodyWalker.find("s64 bodynum = -1;") != std::string::npos);
	REQUIRE(bodyWalker.find("s64 bodynum = 0;") == std::string::npos);
	REQUIRE(bodyWalker.find("LOADER.WALKER.BODY.RUNTIME_SLOT_MISSING") != std::string::npos);
	REQUIRE(headWalker.find("s64 headnum = -1;") != std::string::npos);
	REQUIRE(headWalker.find("s64 headnum = 0;") == std::string::npos);
	REQUIRE(headWalker.find("LOADER.WALKER.HEAD.RUNTIME_SLOT_MISSING") != std::string::npos);
	REQUIRE(bodyWalker.find("s_bodyWalkerManifestFileEnum(manifest, manifest_len, \"hand\")") != std::string::npos);
	REQUIRE(bodyWalker.find("catalogReadableModelIdForFile(hand_filenum, \"hand\", \"hand\"") != std::string::npos);
	REQUIRE(bodyWalker.find("assetCatalogRegister(hand_model_id, ASSET_MODEL)") != std::string::npos);
	REQUIRE(bodyWalker.find("hand_entry->source_filenum = hand_filenum") != std::string::npos);
	REQUIRE(bodyWalker.find("s_sourceModelPathFromMeshArchive") != std::string::npos);
	REQUIRE(bodyWalker.find("\"model.gltf\"") != std::string::npos);
	REQUIRE(bodyWalker.find("\"model.glb\"") != std::string::npos);
	REQUIRE(bodyWalker.find("fsFileSize(out)") != std::string::npos);
	REQUIRE(bodyWalker.find("s_sourceModelPathFromMeshArchive(hand_archive_path") != std::string::npos);
	REQUIRE(bodyWalker.find("s_sourceModelPathFromMeshArchive(mesh_archive_path") != std::string::npos);
	REQUIRE(bodyWalker.find("catalogSetPrimaryFile(hand_entry, hand_source_path)") != std::string::npos);
	REQUIRE(headWalker.find("s_sourceModelPathFromMeshArchive") != std::string::npos);
	REQUIRE(headWalker.find("\"model.gltf\"") != std::string::npos);
	REQUIRE(headWalker.find("\"model.glb\"") != std::string::npos);
	REQUIRE(headWalker.find("fsFileSize(out)") != std::string::npos);
	REQUIRE(headWalker.find("s_sourceModelPathFromMeshArchive(mesh_archive_path") != std::string::npos);
	REQUIRE(bondgun.find("catalogHandleByModelSourceFilenum(ASSET_NONE, filenum)") != std::string::npos);
	REQUIRE(bondgun.find("bgunQueuedLoadPassesSourceOnlyCheck") != std::string::npos);
	REQUIRE(bondgun.find("assetSourceDebugFatalHandleFallback(ASSET_MODEL") != std::string::npos);
	REQUIRE(bondgun.find("weapon model load-to-addr") != std::string::npos);
}

TEST_CASE("base menu hudpiece model has a catalog provider handle", "[catalog][provider][static]")
{
	const std::string baseExtended = readTextFile("port/src/assetcatalog_base_extended.c");
	const std::string meshExtractor = readTextFile("port/src/romextract_pdmesh.c");
	const std::string menu = readTextFile("src/game/menu.c");

	REQUIRE(baseExtended.find("#include \"files.h\"") != std::string::npos);
	REQUIRE(baseExtended.find("FILE_GHUDPIECE") != std::string::npos);
	REQUIRE(baseExtended.find("catalogReadableModelIdForFile(fnum, \"menu\", \"menu\"") != std::string::npos);
	REQUIRE(baseExtended.find("e->source_filenum = fnum") != std::string::npos);
	REQUIRE(baseExtended.find("catalogBindPrimaryFromDiskOrRom(e, e->source_filenum)") != std::string::npos);
	REQUIRE(baseExtended.find("weapon/menu-pipeline model files") != std::string::npos);
	REQUIRE(meshExtractor.find("catalogReadableModelIdForFile((s32)FILE_GHUDPIECE, \"menu\", \"menu\"") != std::string::npos);
	REQUIRE(meshExtractor.find("(u16)FILE_GHUDPIECE, \"menu\"") != std::string::npos);
	REQUIRE(meshExtractor.find("pdmesh_model_obj_mtx_v23_materials_hierarchy_parts_faces_json_relations_raw_mtx_render_commands_json_allmodels_menuhud_zero_tri_models") != std::string::npos);
	REQUIRE(menu.find("MENUMODELPARAMS_SET_FILENUM(FILE_GHUDPIECE)") != std::string::npos);
}

TEST_CASE("generated catalog IDs avoid numeric legacy handles", "[catalog][identity][static]")
{
	const std::array<const char *, 8> files = {
		"port/src/assetcatalog_base_extended.c",
		"port/src/romextract_pdanim_chr.c",
		"port/src/romextract_pdbody.c",
		"port/src/romextract_pdhead.c",
		"port/src/romextract_pdmesh.c",
		"port/src/romextract_pdsfx.c",
		"port/src/romextract_pdsong.c",
		"port/src/romextract_pdweapon.c",
	};
	const std::array<const char *, 12> banned = {
		"base:anim_%04x",
		"base:anim_chr_%04x",
		"base:tex_%04x",
		"base:sfx_%04x",
		"base:voice_%04x",
		"base:song_%04x",
		"base:model_%04x",
		"base:rom_g_%04x",
		"base:hand_model_%04x",
		"base:weapon_model_%04x",
		"base:cart_model_%s_%04x",
		"base:stage_%s_%04x",
	};
	std::vector<std::string> violations;

	for (const char *path : files) {
		const std::string text = readTextFile(path);
		for (const char *pattern : banned) {
			if (text.find(pattern) != std::string::npos) {
				violations.push_back(std::string(path) + ": " + pattern);
			}
		}
	}

	INFO("numeric generated catalog ID patterns:\n" << joinLines(violations));
	REQUIRE(violations.empty());

	const std::string helper = readTextFile("port/include/catalog_readable_ids.h");
	const std::string baseExtended = readTextFile("port/src/assetcatalog_base_extended.c");
	REQUIRE(helper.find("catalogReadableAnimationId") != std::string::npos);
	REQUIRE(helper.find("catalogReadableSongId") != std::string::npos);
	REQUIRE(helper.find("catalogReadableModelIdForFile") != std::string::npos);
	REQUIRE(baseExtended.find("catalogReadableSfxId") != std::string::npos);
	REQUIRE(baseExtended.find("catalogReadableStageSceneId") != std::string::npos);
}

TEST_CASE("Combat Simulator random bot bodies only draw from MP-selectable bodies",
	"[catalog][provider][combat-sim][static]")
{
	const std::string matchsetup = readTextFile("port/src/net/matchsetup.c");

	const size_t collect = matchsetup.find("static void ms_collect_random_body");
	const size_t mpIndexGuard = matchsetup.find("if (e->mp_index < 0) return;", collect);
	const size_t append = matchsetup.find("ctx->ids[ctx->count++] = e->id;", collect);
	REQUIRE(collect != std::string::npos);
	REQUIRE(mpIndexGuard != std::string::npos);
	REQUIRE(append != std::string::npos);
	REQUIRE(mpIndexGuard < append);
}

TEST_CASE("SP-in-MP setup overlay bounds-checks auxiliary setup props",
	"[setup][combat-sim][static]")
{
	const std::string setup = readTextFile("src/game/setup.c");

	REQUIRE(setup.find("setupResolvePropsInLoadedSetup") != std::string::npos);
	REQUIRE(setup.find("scenarioSourceLoadSetupForStage(") != std::string::npos);
	REQUIRE(setup.find("setupRequireScenarioSourceHandle(") != std::string::npos);
	REQUIRE(setup.find("public setup overlay source unavailable; skipping setup overlay") != std::string::npos);
	REQUIRE(setup.find("invalid SP setup props") != std::string::npos);
	REQUIRE(setup.find("nextobj > spSetupEnd") != std::string::npos);
	REQUIRE(setup.find("SP setup ended before OBJTYPE_END") != std::string::npos);
	REQUIRE(setup.find("g_StageSetup.props = savedMpProps") != std::string::npos);
}

TEST_CASE("texture audio and hud file fields populate provider handles", "[catalog][provider][static]")
{
	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	const std::string distrib = readTextFile("port/src/net/netdistrib.c");

	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.texture.file_path)") != std::string::npos);
	REQUIRE(scanner.find("const char *primary_file = audio_file[0] ? audio_file :") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"music_file\", iniGet(ini, \"midi_file\", \"\"))") != std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, primary_file)") != std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.hud.layout_file)") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.texture.file_path)") != std::string::npos);
	REQUIRE(distrib.find("const char *primary_file = audio_file[0] ? audio_file :") != std::string::npos);
	REQUIRE(distrib.find("iniGet(ini, \"music_file\", iniGet(ini, \"midi_file\", \"\"))") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, primary_file)") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.hud.layout_file)") != std::string::npos);
	REQUIRE(distrib.find("slot->id, 0, aname, cat, dur, fpath[0] ? fullfile : \"\")") != std::string::npos);
	REQUIRE(distrib.find("if (!populateExtFromIni(e, ASSET_AUDIO, destdir, &ini,") != std::string::npos);
	REQUIRE(distrib.find("if (prior) *e = prior_entry;") != std::string::npos);
	REQUIRE(distrib.find("distribParseAudioCategoryValue(") != std::string::npos);
}

TEST_CASE("file-backed catalog registration helpers populate provider handles", "[catalog][provider][static]")
{
	const std::string catalog = readTextFile("port/src/assetcatalog.c");
	const std::string header = readTextFile("port/include/assetcatalog.h");

	REQUIRE(header.find("catalogSetPrimaryFile(asset_entry_t *entry, const char *path)") != std::string::npos);
	REQUIRE(header.find("catalogHandleForSourceFile(const char *path)") != std::string::npos);
	REQUIRE(catalog.find("catalogSetPrimaryFile(asset_entry_t *entry, const char *path)") != std::string::npos);
	REQUIRE(catalog.find("catalogHandleForSourceFile(const char *path)") != std::string::npos);
	REQUIRE(catalog.find("fileProviderHandle(path)") != std::string::npos);
	REQUIRE(catalog.find("catalogSetPrimary(entry, handle)") != std::string::npos);
	REQUIRE(catalog.find("catalogSetPrimaryFile(entry, entry->ext.character.bodyfile)") != std::string::npos);
	REQUIRE(countOccurrences(catalog, "catalogSetPrimaryFile(entry, entry->ext.weapon.model_file)") == 0);
	REQUIRE(countOccurrences(catalog, "catalogSetPrimaryFile(entry, entry->ext.prop.model_file)") >= 1);
	REQUIRE(countOccurrences(catalog, "catalogSetPrimaryFile(entry, entry->ext.texture.file_path)") >= 1);
	REQUIRE(countOccurrences(catalog, "catalogSetPrimaryFile(entry, entry->ext.audio.file_path)") >= 1);
	REQUIRE(countOccurrences(catalog, "catalogSetPrimaryFile(entry, entry->ext.hud.texture_file)") == 0);
}

TEST_CASE("base and walked prop vehicle sources use activating provider primaries", "[catalog][provider][static]")
{
	const std::string base = readTextFile("port/src/assetcatalog_base_extended.c");
	const std::string walker = readTextFile("port/src/loader_walker_meta.c");

	const size_t prop_start = base.find("/* ---- props ---- */");
	const size_t material_start = base.find("/* ---- materials ---- */", prop_start);
	REQUIRE(prop_start != std::string::npos);
	REQUIRE(material_start != std::string::npos);
	const std::string prop_block = base.substr(prop_start, material_start - prop_start);
	REQUIRE(prop_block.find("catalogSetPrimaryFile(e, e->ext.prop.prop_file)") !=
		std::string::npos);
	REQUIRE(prop_block.find("catalogSetPrimaryFile(e, e->ext.prop.behavior_graph)") ==
		std::string::npos);

	const size_t vehicle_start = base.find("/* ---- vehicles ---- */");
	const size_t hand_start = base.find("/* ---- first-person hand models", vehicle_start);
	REQUIRE(vehicle_start != std::string::npos);
	REQUIRE(hand_start != std::string::npos);
	const std::string vehicle_block = base.substr(vehicle_start, hand_start - vehicle_start);
	REQUIRE(vehicle_block.find("const char *primary_file = e->ext.vehicle.model_file[0]") !=
		std::string::npos);
	REQUIRE(vehicle_block.find("e->ext.vehicle.behavior_graph") !=
		std::string::npos);
	REQUIRE(vehicle_block.find("catalogSetPrimaryFile(e, primary_file)") !=
		std::string::npos);
	REQUIRE(vehicle_block.find("catalogSetPrimaryFile(e, e->ext.vehicle.physics_file)") ==
		std::string::npos);

	REQUIRE(walker.find("{ ASSET_PROP,        \"prop\",       \"props\",       \".pdprop\",       \"prop.ini\",      { \"model_file\", \"prop_file\", \"behavior_graph\", NULL } }") !=
		std::string::npos);
	REQUIRE(walker.find("{ ASSET_PROP,        \"prop\",       \"props\",       \".pdprop\",       \"prop.ini\",      { \"model_file\", \"behavior_graph\", \"prop_file\", NULL } }") ==
		std::string::npos);
	REQUIRE(walker.find("{ ASSET_VEHICLE,     \"vehicle\",    \"vehicles\",    \".pdvehicle\",    \"vehicle.ini\",   { \"model_file\", \"behavior_graph\", \"physics_file\", NULL } }") !=
		std::string::npos);
	REQUIRE(walker.find("{ ASSET_VEHICLE,     \"vehicle\",    \"vehicles\",    \".pdvehicle\",    \"vehicle.ini\",   { \"physics_file\", \"behavior_graph\", \"model_file\", NULL } }") ==
		std::string::npos);
}

TEST_CASE("file provider handles stay behind catalog provider boundary", "[catalog][provider][static]")
{
	const std::vector<std::string> allowed = {
		"port/include/assetprovider.h",
		"port/src/assetprovider_file.c",
		"port/src/assetcatalog.c",
		"port/src/server_stubs.c",
	};
	std::vector<std::string> violations;

	for (const std::string &path : repoSourceFiles()) {
		const std::string stripped = stripComments(readTextFile(path.c_str()), true);
		if (stripped.find("fileProviderHandle(") != std::string::npos
				&& !isAllowedPath(path, allowed)) {
			violations.push_back(path);
		}
	}

	INFO("fileProviderHandle outside catalog/provider boundary:\n" << joinLines(violations));
	REQUIRE(violations.empty());
}

TEST_CASE("rom-backed catalog registration helpers populate provider handles", "[catalog][provider][static]")
{
	const std::string catalog = readTextFile("port/src/assetcatalog.c");
	const std::string header = readTextFile("port/include/assetcatalog.h");
	const std::string base = readTextFile("port/src/assetcatalog_base.c");
	const std::string baseExtended = readTextFile("port/src/assetcatalog_base_extended.c");

	REQUIRE(header.find("catalogSetPrimaryRomFilenum(asset_entry_t *entry, s32 filenum)") != std::string::npos);
	REQUIRE(catalog.find("catalogSetPrimaryRomFilenum(asset_entry_t *entry, s32 filenum)") != std::string::npos);
	REQUIRE(catalog.find("catalogSetPrimary(entry, romProviderHandle(filenum))") != std::string::npos);
	REQUIRE(base.find("#include \"assetprovider_internal.h\"") == std::string::npos);
	REQUIRE(baseExtended.find("#include \"assetprovider_internal.h\"") == std::string::npos);
	REQUIRE(countOccurrences(base, "catalogBindPrimaryFromDiskOrRom(e, e->source_filenum)") >= 4);
	REQUIRE(countOccurrences(baseExtended, "catalogBindPrimaryFromDiskOrRom(e, e->source_filenum)") >= 2);
	REQUIRE(base.find("catalogSetPrimary(e, romProviderHandle") == std::string::npos);
	REQUIRE(baseExtended.find("catalogSetPrimary(e, romProviderHandle") == std::string::npos);
}

TEST_CASE("typed catalog language lifecycle activates runtime payloads", "[catalog][provider][static]")
{
	const std::string catalogLoad = readTextFile("port/src/assetcatalog_load.c");
	const std::string catalog = readTextFile("port/include/assetcatalog.h");

	REQUIRE(catalog.find("ASSET_PAYLOAD_RUNTIME_ACTIVE") != std::string::npos);
	REQUIRE(catalogLoad.find("s_catalogLoadEntryLangPayload") != std::string::npos);
	REQUIRE(catalogLoad.find("langManifestEnsureId") != std::string::npos);
	REQUIRE(catalogLoad.find("entry->type == ASSET_LANG") != std::string::npos);
	REQUIRE(catalogLoad.find("assetRuntimeActivateCatalogEntry(entry, source_path)") != std::string::npos);
	REQUIRE(catalogLoad.find("language runtime adapter rejected missing strings source") != std::string::npos);
}

TEST_CASE("typed catalog audio lifecycle uses runtime activation", "[catalog][provider][static]")
{
	const std::string catalogLoad = readTextFile("port/src/assetcatalog_load.c");

	REQUIRE(catalogLoad.find("s_catalogLoadEntryAudioPayload") != std::string::npos);
	REQUIRE(catalogLoad.find("s_catalogTypeUsesAudioRuntimePayload") != std::string::npos);
	REQUIRE(catalogLoad.find("return type == ASSET_AUDIO;") != std::string::npos);
	REQUIRE(catalogLoad.find("entry->type == ASSET_AUDIO && assetHandleIsNull(handle)") != std::string::npos);
	REQUIRE(catalogLoad.find("audio payload has no provider handle") != std::string::npos);
	REQUIRE(catalogLoad.find("audio payload has no provider/path") == std::string::npos);
}

TEST_CASE("swarm debug scenarios enter through match setup", "[testscenarios][static]")
{
	const std::string source = readTextFile("port/src/testscenarios.c");
	const std::string header = readTextFile("port/include/testscenarios.h");

	const size_t swarmBlockStart = source.find("case TESTSCEN_SWARM_CPU:");
	REQUIRE(swarmBlockStart != std::string::npos);
	const size_t defaultBlockStart = source.find("default:", swarmBlockStart);
	REQUIRE(defaultBlockStart != std::string::npos);
	const std::string swarmBlock = source.substr(
		swarmBlockStart, defaultBlockStart - swarmBlockStart);

	REQUIRE(swarmBlock.find("matchStart()") != std::string::npos);
	REQUIRE(swarmBlock.find("pdguiForgeStartSessionOn(stagenum)") == std::string::npos);
	REQUIRE(swarmBlock.find("MP setup/manifest path") != std::string::npos);
	REQUIRE(swarmBlock.find("g_MatchConfig.timelimit      = 60") != std::string::npos);
	REQUIRE(swarmBlock.find("g_MatchConfig.scorelimit     = 100") != std::string::npos);
	REQUIRE(header.find("Swarm scenarios enter through matchStart()") != std::string::npos);
}

TEST_CASE("direct MP smoke start waits for CI player prop, not render frames", "[smoke][combat-sim][static][c3844]")
{
	const std::string main = readTextFile("port/src/main.c");
	const std::string block = functionBlock(main, "bootLaunchMpMatchTick");

	REQUIRE_FALSE(block.empty());
	REQUIRE(block.find("g_Vars.stagenum != STAGE_CITRAINING") != std::string::npos);
	REQUIRE(block.find("g_Vars.players[0]") != std::string::npos);
	REQUIRE(block.find("g_Vars.players[0]->prop") != std::string::npos);
	REQUIRE(block.find("g_Vars.players[0]->prop->chr") != std::string::npos);
	REQUIRE(block.find("g_Vars.lvframenum < 4") == std::string::npos);
	REQUIRE(block.find("matchStart()") != std::string::npos);
}

TEST_CASE("skedar swarm behavior intents stay wired for CPU and GPU benchmarks", "[testscenarios][static]")
{
	const std::string testscenarios = readTextFile("port/src/testscenarios.c");
	const std::string swarm = readTextFile("port/src/swarm_test.c");
	const std::string gpu = readTextFile("port/fast3d/swarm_gpu.cpp");
	const std::string surface = readTextFile("src/game/surface_loco.c");

	REQUIRE(testscenarios.find("? SWARM_METHOD_GPU_FULL : SWARM_METHOD_CPU") != std::string::npos);
	REQUIRE(testscenarios.find("? SWARM_METHOD_GPU_POS_ONLY : SWARM_METHOD_CPU") == std::string::npos);
	REQUIRE(testscenarios.find("explicit diagnostic switch back to GPU_POS_ONLY") != std::string::npos);

	REQUIRE(swarm.find("swarmTestApplyMovementIntent") != std::string::npos);
	REQUIRE(gpu.find("swarmTestApplyMovementIntent") != std::string::npos);
	REQUIRE(gpu.find("jump_request") != std::string::npos);
	REQUIRE(gpu.find("surface_request") != std::string::npos);
	REQUIRE(gpu.find("chr->actiontype != ACT_SKJUMP") != std::string::npos);

	REQUIRE(swarm.find("chrSurfaceLocoRequestWallAhead(chr, vel_hint)") != std::string::npos);
	REQUIRE(swarm.find("chrSurfaceLocoApplyContactPos") != std::string::npos);
	REQUIRE(surface.find("chrSurfaceLocoRequestWallAhead") != std::string::npos);
	REQUIRE(surface.find("chrSurfaceLocoApplyContactPos") != std::string::npos);

	REQUIRE(swarm.find("SWARM.BEHAVIOR.JUMP") != std::string::npos);
	REQUIRE(swarm.find("SWARM.BEHAVIOR.SURFACE") != std::string::npos);
	REQUIRE(swarm.find("SURFACE_LOCO.PIN") != std::string::npos);
	REQUIRE(swarm.find("SURFACE_LOCO.TRACE") != std::string::npos);
}

TEST_CASE("skedar swarm stress guards cover high-count cycles", "[testscenarios][static][b352]")
{
	const std::string swarm = readTextFile("port/src/swarm_test.c");
	const std::string gpu = readTextFile("port/fast3d/swarm_gpu.cpp");
	const std::string smoke =
		readTextFile("tools/smoke-verify/tests/swarm_gpu_b352_stress_smoke.json");

	const size_t despawn = swarm.find("static void despawn_all(void)");
	REQUIRE(despawn != std::string::npos);
	const size_t despawnLoop =
		swarm.find("for (s32 i = 0; i < TESTSCEN_SWARM_MAX_COUNT; i++)", despawn);
	REQUIRE(despawnLoop != std::string::npos);
	REQUIRE(swarm.find("swarmGpuInvalidateReadback();", despawn) < despawnLoop);
	REQUIRE(swarm.find("swarmGpuInvalidateFloorCache();", despawn) < despawnLoop);

	const size_t drain = swarm.find("static void swarm_drain_death_state");
	REQUIRE(drain != std::string::npos);
	const size_t fade = swarm.find("chr->fadealpha = -1;", drain);
	REQUIRE(fade != std::string::npos);
	const std::string drainBlock = swarm.substr(drain, fade - drain);
	const size_t dieGuard = drainBlock.find("if (chr->actiontype == ACT_DIE)");
	const size_t dieClear = drainBlock.find("chr->act_die.notifychrindex = 0", dieGuard);
	const size_t deadGuard = drainBlock.find("else if (chr->actiontype == ACT_DEAD)");
	const size_t deadClear = drainBlock.find("chr->act_dead.notifychrindex = 0", deadGuard);
	REQUIRE(dieGuard != std::string::npos);
	REQUIRE(dieClear != std::string::npos);
	REQUIRE(deadGuard != std::string::npos);
	REQUIRE(deadClear != std::string::npos);
	REQUIRE(dieGuard < dieClear);
	REQUIRE(deadGuard < deadClear);
	REQUIRE(drainBlock.find("clearing both is safe") == std::string::npos);

	REQUIRE(swarm.find("SWARM_SPAWN_MAX_FLOOR_DELTA") != std::string::npos);
	REQUIRE(swarm.find("SWARM_SPAWN_FLOOR_SNAP_OFFSET") != std::string::npos);
	REQUIRE(swarm.find("GEOFLAG_DIE") != std::string::npos);
	REQUIRE(swarm.find("swarm_finalize_spawn_candidate(&pos, &corrected_room") != std::string::npos);
	REQUIRE(swarm.find("static struct prop *swarm_live_prop_for_chr") != std::string::npos);
	REQUIRE(swarm.find("prop->type != PROPTYPE_CHR || prop->chr != chr") != std::string::npos);
	REQUIRE(swarm.find("swarm_clear_slot_after_stale_prop(i, \"despawn_all\")") != std::string::npos);
	REQUIRE(swarm.find("swarm_clear_slot_after_stale_prop(i, \"death_poll\")") != std::string::npos);
	REQUIRE(swarm.find("chrs[i] = swarm_live_prop_for_chr(s_Swarm[i].chr)") != std::string::npos);

	const size_t jumpGate = swarm.find("swarm_skjump_start_allowed(slot_index)");
	const size_t jumpCall = swarm.find("chrTrySkJump(chr", jumpGate);
	REQUIRE(jumpGate != std::string::npos);
	REQUIRE(jumpCall != std::string::npos);
	REQUIRE(jumpGate < jumpCall);
	REQUIRE(swarm.find("SWARM_SKJUMP_MAX_STARTS_PER_FRAME") != std::string::npos);

	REQUIRE(gpu.find("SWARM_READBACK_LATE_WAIT_NS") != std::string::npos);
	REQUIRE(gpu.find("GL_SYNC_FLUSH_COMMANDS_BIT") != std::string::npos);

	REQUIRE(smoke.find("\"scenario_name\": \"swarm_gpu_b352_stress_smoke\"") !=
		std::string::npos);
	REQUIRE(smoke.find("base:mp_felicity") != std::string::npos);
	REQUIRE(smoke.find("target=256") != std::string::npos);
	REQUIRE(smoke.find("despawn_all freed 128 chrs") != std::string::npos);
	REQUIRE(smoke.find("post-cycle target=256 actual=256") != std::string::npos);
	REQUIRE(smoke.find("BENCHMARK\\\\.SWARM\\\\.GPU\\\\.VEL: count=(128|256) active=[1-9]\\\\d*") !=
		std::string::npos);
	REQUIRE(smoke.find("BENCHMARK\\\\.SWARM\\\\.GPU\\\\.VEL: count=(128|256) active=0\\\\b") !=
		std::string::npos);
}

TEST_CASE("typed catalog metadata lifecycle uses runtime activation", "[catalog][provider][static]")
{
	const std::string catalogLoad = readTextFile("port/src/assetcatalog_load.c");

	REQUIRE(catalogLoad.find("s_catalogLoadEntryMetadataPayload") != std::string::npos);
	REQUIRE(catalogLoad.find("s_catalogPreloadBundledMetadataPayload") != std::string::npos);
	REQUIRE(catalogLoad.find("assetSourceDebugEntryRequiresPublicFileSource(entry)") <
	        catalogLoad.find("return s_catalogLoadEntryMetadataPayload(entry)"));
	REQUIRE(catalogLoad.find("s_catalogPreloadBundledMetadataPayload(mutable_entry)") !=
	        std::string::npos);
	REQUIRE(catalogLoad.find("(void)s_catalogLoadEntryMetadataPayload(mutable_entry)") ==
	        std::string::npos);
	REQUIRE(catalogLoad.find("s_catalogTypeUsesMetadataRuntimePayload") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_MAP") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_CHARACTER") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_ANIMATION") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_TEXTURES") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_SFX") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_MUSIC") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_UI") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_TOOL") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_PROP") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_VEHICLE") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_MISSION") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_HUD") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_BOT_PROFILE") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_ARENA") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_GAMEMODE") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_SKIN") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_BOT_VARIANT") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_EFFECT") != std::string::npos);
	REQUIRE(catalogLoad.find("metadata payload") != std::string::npos);
}

TEST_CASE("typed catalog texture lifecycle activates texture payloads", "[catalog][provider][static]")
{
	const std::string catalogLoad = readTextFile("port/src/assetcatalog_load.c");
	const std::string catalog = readTextFile("port/src/assetcatalog.c");
	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	const std::string netdistrib = readTextFile("port/src/net/netdistrib.c");
	const std::string mod = readTextFile("port/src/mod.c");
	const std::string source = readTextFile("port/src/mod_texture_source.c");
	const std::string tex = readTextFile("src/game/texdecompress.c");

	REQUIRE(catalogLoad.find("s_catalogLoadEntryTexturePayload") != std::string::npos);
	REQUIRE(catalogLoad.find("entry->type == ASSET_TEXTURE") != std::string::npos);
	REQUIRE(catalogLoad.find("texture payload has no provider handle") != std::string::npos);
	REQUIRE(catalogLoad.find("texture payload failed to load from") == std::string::npos);
	REQUIRE(catalogLoad.find("activated texture payload") != std::string::npos);
	REQUIRE(catalog.find("entry->source_texnum = texture_id") != std::string::npos);
	REQUIRE(scanner.find("e->source_texnum = e->ext.texture.texture_id") != std::string::npos);
	REQUIRE(netdistrib.find("e->source_texnum = e->ext.texture.texture_id") != std::string::npos);
	REQUIRE(source.find("stbi_load_from_memory") != std::string::npos);
	REQUIRE(source.find("g_NotLoadMod && (!entry || !entry->bundled)") != std::string::npos);
	REQUIRE(source.find("assetSourceDebugIsEnabledFor(ASSET_TEXTURE)") == std::string::npos);
	REQUIRE(source.find("the selected public source is not an editable image source") != std::string::npos);
	REQUIRE(tex.find("texLoadPublicRgba32Source") != std::string::npos);
	REQUIRE(tex.find("tex->gbiformat = G_IM_FMT_RGBA") != std::string::npos);
	REQUIRE(tex.find("tex->depth = G_IM_SIZ_32b") != std::string::npos);
	REQUIRE(mod.find("skipping legacy compressed texture loader") != std::string::npos);
	REQUIRE(mod.find("assetSourceDebugIsEnabledFor(ASSET_TEXTURE)") == std::string::npos);
	REQUIRE(mod.find("refusing legacy compressed texture fallback") != std::string::npos);
}

TEST_CASE("raw RomProvider handles stay inside catalog provider internals", "[catalog][provider][static]")
{
	const std::vector<std::string> allowed = {
		"port/include/assetprovider_internal.h",
		"port/src/assetprovider_rom.c",
		"port/src/assetload.c",
		"port/src/assetcatalog.c",
		"port/src/assetcatalog_api.c",
		"port/src/server_stubs.c",
	};
	std::vector<std::string> violations;

	for (const std::string &path : repoSourceFiles()) {
		const std::string stripped = stripComments(readTextFile(path.c_str()), true);

		if (stripped.find("romProviderHandle(") != std::string::npos
				&& !isAllowedPath(path, allowed)) {
			violations.push_back(path);
		}
	}

	INFO("romProviderHandle leaks:\n" << joinLines(violations));
	REQUIRE(violations.empty());
}

TEST_CASE("assetprovider internal header stays inside approved implementation files", "[catalog][provider][static]")
{
	const std::vector<std::string> allowed = {
		"port/src/assetload.c",
		"port/src/assetcatalog.c",
		"port/src/assetcatalog_api.c",
		"port/src/assetcatalog_scanner.c",
		"port/src/assetprovider_file.c",
		"port/src/server_stubs.c",
	};
	std::vector<std::string> violations;

	for (const std::string &path : repoSourceFiles()) {
		const std::string stripped = stripComments(readTextFile(path.c_str()), false);

		if (stripped.find("assetprovider_internal.h") != std::string::npos
				&& !isAllowedPath(path, allowed)) {
			violations.push_back(path);
		}
	}

	INFO("assetprovider_internal.h leaks:\n" << joinLines(violations));
	REQUIRE(violations.empty());
}

TEST_CASE("RomProvider-specific checks stay in approved bridge code", "[catalog][provider][static]")
{
	const std::vector<std::string> allowed = {
		"port/include/assetprovider_internal.h",
		"port/src/assetload.c",
		"port/src/assetprovider_rom.c",
		"port/src/server_stubs.c",
		"src/game/modeldef.c",
	};
	const std::array<const char *, 3> banned = {
		"handle.provider == romProvider()",
		"handle.provider != romProvider()",
		"romProviderFilenum(",
	};
	std::vector<std::string> violations;

	for (const std::string &path : repoSourceFiles()) {
		const std::string stripped = stripComments(readTextFile(path.c_str()), true);

		for (const char *pattern : banned) {
			if (stripped.find(pattern) != std::string::npos
					&& !isAllowedPath(path, allowed)) {
				violations.push_back(path + ": " + pattern);
			}
		}
	}

	INFO("RomProvider-specific checks outside bridge code:\n" << joinLines(violations));
	REQUIRE(violations.empty());
}
