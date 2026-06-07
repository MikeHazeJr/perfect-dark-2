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
		REQUIRE(stripped.find("catalogLoadTypedAsset(") != std::string::npos);
		REQUIRE(stripped.find("catalogReleaseTypedAsset(") != std::string::npos);
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
	REQUIRE(catalogLoad.find("modeldefLoadToNewFromHandle") != std::string::npos);
	REQUIRE(catalogLoad.find("model payload has no provider handle") != std::string::npos);
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

TEST_CASE("weapon and prop model files populate provider handles", "[catalog][provider][static]")
{
	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	const std::string distrib = readTextFile("port/src/net/netdistrib.c");

	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.weapon.model_file)") != std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.prop.model_file)") != std::string::npos);
	REQUIRE(distrib.find("#include \"assetprovider.h\"") == std::string::npos);
	REQUIRE(distrib.find("\"prop.ini\"") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.character.bodyfile)") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.weapon.model_file)") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.prop.model_file)") != std::string::npos);
	REQUIRE(distrib.find("snprintf(fullpath, sizeof(fullpath), \"%s/%s\", dirpath, relpath)") != std::string::npos);
	REQUIRE(distrib.find("catalogSetPrimaryFile(e, path)") != std::string::npos);
}

TEST_CASE("Forge runtime model spawns enforce source-only family handles", "[catalog][provider][static]")
{
	const std::string forge = readTextFile("port/src/forge/forge_runtime.c");
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");

	REQUIRE(forge.find("#include \"asset_source_debug.h\"") != std::string::npos);
	REQUIRE(forge.find("s_forgeModelHandlePassesSourceOnlyCheck") != std::string::npos);
	REQUIRE(forge.find("assetSourceDebugFatalHandleFallback(type, context, catalog_id, handle)") !=
	        std::string::npos);
	REQUIRE(forge.find("assetSourceDebugHandleRequiresPublicFileSource(type, handle)") !=
	        std::string::npos);
	REQUIRE(forge.find("s_forgeModelHandlePassesSourceOnlyCheck(ASSET_PROP, o->catalog_id") <
	        forge.find("modeldefLoadToNewFromHandle(pr.handle, pr.filenum)"));
	REQUIRE(forge.find("s_forgeModelHandlePassesSourceOnlyCheck(ASSET_WEAPON") <
	        forge.find("modeldefLoadToNewFromHandle(wr.handle, wr.filenum)"));
	REQUIRE(forge.find("forge door modeldef") != std::string::npos);
	REQUIRE(forge.find("forge weapon pad modeldef") != std::string::npos);
	REQUIRE(forge.find("forge prop modeldef") != std::string::npos);
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
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");
	const std::string modSequenceLoad = functionBlock(mod, "void *modSequenceLoad");

	REQUIRE(loadH.find("catalogResolveMusicSequence") != std::string::npos);
	REQUIRE(loadH.find("Public .pdsong track audio is routed through the streaming music path") !=
	        std::string::npos);
	REQUIRE(loadH.find("Public sequence.mid, sequence.json, and music.ini sources are") !=
	        std::string::npos);
	REQUIRE(load.find("CatalogResolveResult catalogResolveMusicSequence") !=
	        std::string::npos);
	REQUIRE(load.find("e->ext.audio.category != AUDIO_CAT_MUSIC") !=
	        std::string::npos);
	REQUIRE(load.find("e->ext.audio.sound_id != tracknum") !=
	        std::string::npos);
	REQUIRE(load.find("r.path = entryGetFilePath(entry)") != std::string::npos);
	REQUIRE(load.find("r.is_mod_override = 1") != std::string::npos);
	REQUIRE(load.find("assetSourceDebugIsEnabledFor(ASSET_AUDIO)") !=
	        std::string::npos);
	REQUIRE(load.find("r.source_only_blocked = 1") != std::string::npos);

	REQUIRE(mod.find("catalogResolveMusicSequence((s32)num)") !=
	        std::string::npos);
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
	REQUIRE(mod.find("streaming playback failed") != std::string::npos);
	REQUIRE(mod.find("ASSET.SOURCE_ONLY: music sequence") !=
	        std::string::npos);
	REQUIRE(mod.find("sequencer-native public source compile failed") !=
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

	REQUIRE(guard.find("scan_music_sequence_source_only_guard(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("MUSIC_SEQUENCE_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
}

TEST_CASE("MP3 file use enforces source-only audio before ROM fallback", "[catalog][provider][static]")
{
	const std::string snd = readTextFile("src/lib/snd.c");
	const std::string propsnd = readTextFile("src/game/propsnd.c");
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");
	const std::string snd_start_mp3 = functionBlock(snd, "void sndStartMp3(s16");
	const std::string snd_mp3_resolve =
		functionBlock(snd, "static s32 sndMp3ResolveSourceOrFallback");

	REQUIRE(snd.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(snd.find("#include \"fs.h\"") != std::string::npos);
	REQUIRE(snd.find("#include \"romextract.h\"") != std::string::npos);
	REQUIRE(snd.find("#include \"assetcatalog_load.h\"") !=
	        std::string::npos);
	REQUIRE(snd.find("static void *g_SndMp3SourceBytes = NULL") !=
	        std::string::npos);
	REQUIRE(snd.find("static void sndMp3FreeSourceBuffer(void)") !=
	        std::string::npos);
	REQUIRE(snd.find("static s32 sndMp3LoadPublicSourceFile") !=
	        std::string::npos);
	REQUIRE(snd.find("static s32 sndMp3ResolveSourceOrFallback") !=
	        std::string::npos);
	REQUIRE(snd.find("catalogResolveFile(filenum)") != std::string::npos);
	REQUIRE(snd.find("fsFileLoad(source.path, &size)") != std::string::npos);
	REQUIRE(snd.find("romExtractRelPathForFilenum(filenum, relpath") !=
	        std::string::npos);
	REQUIRE(snd.find("fsFileLoad(relpath, &size)") != std::string::npos);
	REQUIRE(snd.find("g_SndMp3SourceBytes = bytes") != std::string::npos);
	REQUIRE(snd.find("assetSourceDebugIsEnabledFor(ASSET_AUDIO)") !=
	        std::string::npos);
	REQUIRE(snd.find("ASSET.SOURCE_ONLY: MP3 file") !=
	        std::string::npos);
	REQUIRE(snd.find("refusing loose extracted file or ROM/static playback fallback") !=
	        std::string::npos);
	REQUIRE(snd_start_mp3.find("sndMp3ResolveSourceOrFallback((s32)sp20.id") <
	        snd_start_mp3.find("mp3PlayFile(g_SndCurMp3.romaddr, g_SndCurMp3.romsize)"));
	REQUIRE(snd_mp3_resolve.find("sndMp3LoadPublicSourceFile(filenum, outaddr, outsize)") <
	        snd_mp3_resolve.find("fileGetRomAddress(filenum)"));
	const std::string snd_mp3_load =
		functionBlock(snd, "static s32 sndMp3LoadPublicSourceFile");
	REQUIRE(snd_mp3_load.find("catalogResolveFile(filenum)") <
	        snd_mp3_load.find("romExtractRelPathForFilenum(filenum, relpath"));
	REQUIRE(snd_mp3_load.find("assetSourceDebugIsEnabledFor(ASSET_AUDIO)") <
	        snd_mp3_load.find("romExtractRelPathForFilenum(filenum, relpath"));
	REQUIRE(propsnd.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(propsnd.find("#include \"fs.h\"") != std::string::npos);
	REQUIRE(propsnd.find("#include \"romextract.h\"") != std::string::npos);
	REQUIRE(propsnd.find("#include \"assetcatalog_load.h\"") !=
	        std::string::npos);
	REQUIRE(propsnd.find("static s32 psMp3DurationGetSourceOrFallbackSize") !=
	        std::string::npos);
	REQUIRE(propsnd.find("catalogResolveFile(filenum)") != std::string::npos);
	REQUIRE(propsnd.find("fsFileSize(source.path)") != std::string::npos);
	REQUIRE(propsnd.find("romExtractRelPathForFilenum(filenum, relpath") !=
	        std::string::npos);
	REQUIRE(propsnd.find("fsFileSize(relpath)") != std::string::npos);
	REQUIRE(propsnd.find("assetSourceDebugIsEnabledFor(ASSET_AUDIO)") !=
	        std::string::npos);
	REQUIRE(propsnd.find("ASSET.SOURCE_ONLY: MP3 file") !=
	        std::string::npos);
	REQUIRE(propsnd.find("refusing loose extracted file or ROM/static") !=
	        std::string::npos);
	REQUIRE(propsnd.find("psMp3DurationGetSourceOrFallbackSize((s32)soundnum.id)") !=
	        std::string::npos);
	REQUIRE(propsnd.find("catalogResolveFile(filenum)") <
	        propsnd.find("romExtractRelPathForFilenum(filenum, relpath"));
	REQUIRE(propsnd.find("assetSourceDebugIsEnabledFor(ASSET_AUDIO)") <
	        propsnd.find("romExtractRelPathForFilenum(filenum, relpath"));
	REQUIRE(propsnd.find("assetSourceDebugIsEnabledFor(ASSET_AUDIO)") <
	        propsnd.find("fileGetRomSize(filenum)"));
	REQUIRE(propsnd.find("fileGetRomSize(soundnum.id)") == std::string::npos);

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
	REQUIRE(snd.find("filepitch *= alCents2Ratio(cents)") !=
	        std::string::npos);
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
	REQUIRE(snd.find("ASSET.SOURCE_ONLY: sound %d maps to public file source") !=
	        std::string::npos);
	REQUIRE(snd.find("but file playback failed; refusing ROM/static fallback") !=
	        std::string::npos);
	REQUIRE(snd.find("assetSourceDebugIsEnabledFor(ASSET_AUDIO)",
		snd.find("audioStartFileSound(r.path, volume, pan")) <
	        snd.find("MOD: sound %d catalog override failed (%s), falling back to ROM"));

	REQUIRE(guard.find("scan_sound_file_source_only_guard(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("SOUND_FILE_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
}

TEST_CASE("base first-person hand model files populate provider handles", "[catalog][provider][static]")
{
	const std::string baseExtended = readTextFile("port/src/assetcatalog_base_extended.c");
	const std::string bondgun = readTextFile("src/game/bondgun.c");

	/* BYOR completion (2026-05-03): hand-model probe migrated from
	 * g_HeadsAndBodies[i].handfilenum -> g_BodyData[i].handfilenum
	 * (authoring table). Pin updated to match the new source. */
	REQUIRE(baseExtended.find("g_BodyData[i].handfilenum") != std::string::npos);
	REQUIRE(baseExtended.find("catalogReadableModelIdForFile(handfilenum, \"hand\", \"hand\"") != std::string::npos);
	REQUIRE(baseExtended.find("e->runtime_index = -handfilenum") != std::string::npos);
	REQUIRE(baseExtended.find("e->source_filenum = handfilenum") != std::string::npos);
	REQUIRE(baseExtended.find("catalogBindPrimaryFromDiskOrRom(e, e->source_filenum)") != std::string::npos);
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
	REQUIRE(setup.find("assetSourceDebugHandleUsesPublicFileSource(spStage.setup_handle)") != std::string::npos);
	REQUIRE(setup.find("public setup overlay source unavailable; skipping raw setup overlay") != std::string::npos);
	REQUIRE(setup.find("assetLoadGetLoadedSize(spStage.setup_handle)") != std::string::npos);
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
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.audio.file_path)") != std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.hud.texture_file)") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.texture.file_path)") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.audio.file_path)") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.hud.texture_file)") != std::string::npos);
	REQUIRE(distrib.find("slot->id, 0, aname, cat, dur, fpath[0] ? fullfile : \"\")") != std::string::npos);
}

TEST_CASE("file-backed catalog registration helpers populate provider handles", "[catalog][provider][static]")
{
	const std::string catalog = readTextFile("port/src/assetcatalog.c");
	const std::string header = readTextFile("port/include/assetcatalog.h");

	REQUIRE(header.find("catalogSetPrimaryFile(asset_entry_t *entry, const char *path)") != std::string::npos);
	REQUIRE(catalog.find("catalogSetPrimaryFile(asset_entry_t *entry, const char *path)") != std::string::npos);
	REQUIRE(catalog.find("fileProviderHandle(path)") != std::string::npos);
	REQUIRE(catalog.find("catalogSetPrimary(entry, handle)") != std::string::npos);
	REQUIRE(catalog.find("catalogSetPrimaryFile(entry, entry->ext.character.bodyfile)") != std::string::npos);
	REQUIRE(countOccurrences(catalog, "catalogSetPrimaryFile(entry, entry->ext.weapon.model_file)") >= 1);
	REQUIRE(countOccurrences(catalog, "catalogSetPrimaryFile(entry, entry->ext.prop.model_file)") >= 1);
	REQUIRE(countOccurrences(catalog, "catalogSetPrimaryFile(entry, entry->ext.texture.file_path)") >= 1);
	REQUIRE(countOccurrences(catalog, "catalogSetPrimaryFile(entry, entry->ext.audio.file_path)") >= 1);
	REQUIRE(countOccurrences(catalog, "catalogSetPrimaryFile(entry, entry->ext.hud.texture_file)") >= 1);
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
	REQUIRE(source.find("stbi_load_from_memory") != std::string::npos);
	REQUIRE(source.find("g_NotLoadMod && (!entry || !entry->bundled)") != std::string::npos);
	REQUIRE(tex.find("texLoadPublicRgba32Source") != std::string::npos);
	REQUIRE(tex.find("tex->gbiformat = G_IM_FMT_RGBA") != std::string::npos);
	REQUIRE(tex.find("tex->depth = G_IM_SIZ_32b") != std::string::npos);
	REQUIRE(mod.find("skipping legacy compressed texture loader") != std::string::npos);
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
