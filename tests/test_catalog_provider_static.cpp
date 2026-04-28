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

	for (const char *path : files) {
		const std::string stripped = stripComments(readTextFile(path), true);
		INFO(path);
		REQUIRE(stripped.find("catalogHandleBySourceFilenum(") != std::string::npos);
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

	REQUIRE(bondgun.find("temporary ROM fallback") == std::string::npos);
	REQUIRE(bondgun.find("assetLoadRomToAddr(player->gunctrl.loadfilenum") == std::string::npos);
	REQUIRE(bondgun.find("fileGetInflatedSize(player->gunctrl.loadfilenum") == std::string::npos);
	REQUIRE(player.find("temporary ROM fallback") == std::string::npos);
	REQUIRE(player.find("modeldefLoad((u16)wfn") == std::string::npos);
	REQUIRE(player.find("fileGetLoadedSize(wfn)") == std::string::npos);
	REQUIRE(menu.find("temporary ROM fallback") == std::string::npos);
	REQUIRE(menu.find("modeldefLoad((u16)source_filenum") == std::string::npos);
	REQUIRE(menu.find("fileGetInflatedSize(source_filenum, LOADTYPE_MODEL)") == std::string::npos);
	REQUIRE(title.find("temporary ROM fallback") == std::string::npos);
	REQUIRE(title.find("return modeldefLoad((u16)modelresult.filenum") == std::string::npos);
	REQUIRE(modelcatalog.find("temporary ROM fallback") == std::string::npos);
	REQUIRE(modelcatalog.find("modeldefLoadToNew(filenum)") == std::string::npos);

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
	REQUIRE(catalogLoad.find("case ASSET_WEAPON:") != std::string::npos);
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

TEST_CASE("base first-person hand model files populate provider handles", "[catalog][provider][static]")
{
	const std::string baseExtended = readTextFile("port/src/assetcatalog_base_extended.c");
	const std::string bondgun = readTextFile("src/game/bondgun.c");

	REQUIRE(baseExtended.find("g_HeadsAndBodies[i].handfilenum") != std::string::npos);
	REQUIRE(baseExtended.find("base:hand_model_%04x") != std::string::npos);
	REQUIRE(baseExtended.find("e->runtime_index = -handfilenum") != std::string::npos);
	REQUIRE(baseExtended.find("e->source_filenum = handfilenum") != std::string::npos);
	REQUIRE(baseExtended.find("catalogSetPrimary(e, romProviderHandle(e->source_filenum))") != std::string::npos);
	REQUIRE(bondgun.find("catalogHandleBySourceFilenum(ASSET_MODEL, filenum)") != std::string::npos);
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

TEST_CASE("typed catalog metadata lifecycle uses runtime activation", "[catalog][provider][static]")
{
	const std::string catalogLoad = readTextFile("port/src/assetcatalog_load.c");

	REQUIRE(catalogLoad.find("s_catalogLoadEntryMetadataPayload") != std::string::npos);
	REQUIRE(catalogLoad.find("s_catalogTypeUsesMetadataRuntimePayload") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_MAP") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_CHARACTER") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_ANIMATION") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_TEXTURES") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_SFX") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_MUSIC") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_UI") != std::string::npos);
	REQUIRE(catalogLoad.find("type == ASSET_TOOL") != std::string::npos);
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

	REQUIRE(catalogLoad.find("s_catalogLoadEntryTexturePayload") != std::string::npos);
	REQUIRE(catalogLoad.find("entry->type == ASSET_TEXTURE") != std::string::npos);
	REQUIRE(catalogLoad.find("texture payload has no provider handle") != std::string::npos);
	REQUIRE(catalogLoad.find("texture payload failed to load from") == std::string::npos);
	REQUIRE(catalogLoad.find("activated texture payload") != std::string::npos);
}

TEST_CASE("raw RomProvider handles stay inside catalog provider internals", "[catalog][provider][static]")
{
	const std::vector<std::string> allowed = {
		"port/include/assetprovider_internal.h",
		"port/src/assetprovider_rom.c",
		"port/src/assetload.c",
		"port/src/assetcatalog_api.c",
		"port/src/assetcatalog_base.c",
		"port/src/assetcatalog_base_extended.c",
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
		"port/src/assetcatalog_api.c",
		"port/src/assetcatalog_base.c",
		"port/src/assetcatalog_base_extended.c",
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
