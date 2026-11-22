/*
 * tests/test_mod_external_archive_static.cpp -- external-format content pins.
 *
 * Static guards for the c3809 implementation slices. Typed .pdxxx files are
 * the content-unit surface; .pdmod is the transport wrapper used for sharing
 * and online-required delivery.
 */

#include "catch.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "pdgui_gameplay_graph_editor.h"

extern "C" {
#include "asset_archive_policy.h"
#include "asset_archive_writer.h"
#include "asset_mod_utility_contract.h"
#include "assetcatalog_scanner.h"
#include "constants.h"
#include "effect_graph_runtime.h"
#include "modarchive.h"
#include "loader_walker_mesh_source.h"
#include "modpack_pdmod.h"
#include "modvfs.h"
#include "prop_graph_runtime.h"
#include "weapon_graph_archive.h"
#include "weapon_graph_runtime.h"

void testStubAssetCatalogResolveWith(const asset_entry_t *entry);
void testStubAssetCatalogResolvePair(const asset_entry_t *first, const asset_entry_t *second);
void testStubEffectAudio(const char *id, s32 category, s32 sound_id);
void testStubFsFileLoadWith(const char *path, const void *bytes, u32 size);
}

struct ScopedCatalogSourceStub {
	~ScopedCatalogSourceStub()
	{
		testStubAssetCatalogResolveWith(nullptr);
		testStubFsFileLoadWith(nullptr, nullptr, 0);
	}
};

struct ScopedGraphCatalogSources {
	asset_entry_t first{};
	asset_entry_t second{};
	ScopedGraphCatalogSources(const char *first_id = nullptr, s32 first_model = 0,
			const char *second_id = nullptr, s32 second_model = 0,
			const char *audio_id = nullptr, s32 sound_id = 0)
	{
		if (first_id) {
			std::snprintf(first.id, sizeof(first.id), "%s", first_id);
			first.type = ASSET_MODEL;
			first.runtime_index = first_model;
		}
		if (second_id) {
			std::snprintf(second.id, sizeof(second.id), "%s", second_id);
			second.type = ASSET_MODEL;
			second.runtime_index = second_model;
		}
		testStubAssetCatalogResolvePair(first_id ? &first : nullptr,
			second_id ? &second : nullptr);
		testStubEffectAudio(audio_id, 0, sound_id);
	}
	~ScopedGraphCatalogSources()
	{
		testStubAssetCatalogResolveWith(nullptr);
		testStubEffectAudio(nullptr, 0, 0);
	}
};

static size_t countOccurrences(const std::string &text,
		const std::string &needle)
{
	if (needle.empty()) {
		return 0;
	}

	size_t count = 0;
	size_t offset = 0;

	while ((offset = text.find(needle, offset)) != std::string::npos) {
		count++;
		offset += needle.size();
	}

	return count;
}

extern "C" const char *modiniTemplateForKind(const char *kind)
{
	(void)kind;
	return nullptr;
}

static std::string trimIniStub(std::string value)
{
	const char *ws = " \t\r\n";
	const size_t first = value.find_first_not_of(ws);
	if (first == std::string::npos) {
		return "";
	}
	const size_t last = value.find_last_not_of(ws);
	return value.substr(first, last - first + 1);
}

extern "C" s32 iniParse(const char *filepath, ini_section_t *out)
{
	if (!filepath || !out) {
		return 0;
	}
	std::ifstream in(filepath, std::ios::in | std::ios::binary);
	if (!in.good()) {
		return 0;
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	const std::string text = ss.str();
	return iniParseBuffer(filepath, text.data(), static_cast<u32>(text.size()), out);
}

extern "C" s32 iniParseBuffer(const char *label,
                              const char *data,
                              u32 len,
                              ini_section_t *out)
{
	(void)label;
	if (!data || !out) {
		return 0;
	}
	memset(out, 0, sizeof(*out));
	std::istringstream in(std::string(data, data + len));
	std::string line;
	while (std::getline(in, line)) {
		std::string text = trimIniStub(line);
		if (text.empty() || text[0] == ';' || text[0] == '#') {
			continue;
		}
		if (text.front() == '[' && text.back() == ']') {
			if (out->type[0] == '\0') {
				std::string section = trimIniStub(text.substr(1, text.size() - 2));
				if (section.size() >= sizeof(out->type)) {
					return 0;
				}
				strncpy(out->type, section.c_str(), sizeof(out->type) - 1);
			}
			continue;
		}
		const size_t eq = text.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		if (out->count >= INI_MAX_PAIRS) {
			return 0;
		}
		std::string key = trimIniStub(text.substr(0, eq));
		std::string value = trimIniStub(text.substr(eq + 1));
		if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
			value = value.substr(1, value.size() - 2);
		}
		if (key.empty()) {
			continue;
		}
		if (key.size() >= sizeof(out->pairs[out->count].key)
				|| value.size() >= sizeof(out->pairs[out->count].value)) {
			return 0;
		}
		strncpy(out->pairs[out->count].key, key.c_str(),
			sizeof(out->pairs[out->count].key) - 1);
		strncpy(out->pairs[out->count].value, value.c_str(),
			sizeof(out->pairs[out->count].value) - 1);
		out->count++;
	}
	return 1;
}

extern "C" const char *iniGet(const ini_section_t *ini,
                              const char *key,
                              const char *defval)
{
	if (!ini || !key) {
		return defval;
	}
	for (s32 i = 0; i < ini->count; i++) {
		if (strcmp(ini->pairs[i].key, key) == 0) {
			return ini->pairs[i].value;
		}
	}
	return defval;
}

namespace {

std::string readFile(const char *path) {
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

std::string sourceFunctionBlock(const std::string &text,
		const char *signature)
{
	const size_t begin = text.find(signature);
	REQUIRE(begin != std::string::npos);
	const size_t brace = text.find('{', begin);
	REQUIRE(brace != std::string::npos);

	size_t depth = 0;
	for (size_t pos = brace; pos < text.size(); ++pos) {
		if (text[pos] == '{') {
			++depth;
		} else if (text[pos] == '}') {
			REQUIRE(depth > 0);
			if (--depth == 0) {
				return text.substr(begin, pos - begin + 1);
			}
		}
	}

	FAIL("unterminated source function block");
	return {};
}

TEST_CASE("pdmod archive size clamp widens signed off_t before comparison",
		"[modding][pdmod][static][b1026]") {
	const std::string modmgr = readFile("port/src/modmgr.c");

	REQUIRE(modmgr.find("(u64)st.st_size > 0xFFFFFFFFull") !=
		std::string::npos);
	REQUIRE(modmgr.find("(off_t)0xFFFFFFFFu") == std::string::npos);
}

TEST_CASE("ordinary held public model publication retains its generation through stage teardown",
        "[modding][pdxxx][c3842][model-generation][static]") {
    const auto load = sourceFunctionBlock(readFile("src/game/bondgun.c"),
        "static struct modeldef *bgunQueuedLoadCatalogModelSource(");
    const auto pin = load.find("catalogModelGenerationPinStage(");
    const auto publish = load.find("*player->gunctrl.loadtomodeldef = modeldef;");
    REQUIRE(pin != std::string::npos); REQUIRE(publish != std::string::npos);
    REQUIRE(pin < publish);
    REQUIRE(load.substr(pin, publish - pin).find("return NULL;") != std::string::npos);
    const auto reset = sourceFunctionBlock(readFile("src/game/playermgr.c"), "void playermgrReset(");
    const auto release = reset.find("catalogModelGenerationReleaseStagePins();");
    REQUIRE(release != std::string::npos);
    const auto lastRoot = reset.rfind("= NULL;");
    REQUIRE(lastRoot != std::string::npos); REQUIRE(lastRoot < release);
}

TEST_CASE("retained native model queue bypasses mutable catalog only for a pinned source",
        "[modding][pdxxx][c3842][model-generation][static]") {
    const auto source = readFile("src/game/bondgun.c");
    const auto queue = sourceFunctionBlock(source, "bool bgunQueueRetainedModelLoad(");
    const auto pin = queue.find("catalogModelGenerationPinStage(generation)");
    const auto publish = queue.find("player->gunctrl.loadretainedmodel = source;");
    REQUIRE(pin != std::string::npos); REQUIRE(publish != std::string::npos); REQUIRE(pin < publish);
    REQUIRE(queue.substr(0, publish).find("return false;") != std::string::npos);
    const auto tick = sourceFunctionBlock(source, "void bgunTickGunLoad(");
    const auto consume = tick.find("if (player->gunctrl.loadretainedmodel)");
    const auto mutableCheck = tick.find("if (!bgunQueuedLoadCanUseHandle(player))");
    REQUIRE(consume != std::string::npos); REQUIRE(mutableCheck != std::string::npos); REQUIRE(consume < mutableCheck);
    REQUIRE(readFile("src/game/bondgunreset.c").find("gunctrl.loadretainedmodel = NULL") != std::string::npos);
}

struct TempArchive {
	std::filesystem::path path;

	~TempArchive() {
		if (!path.empty()) {
			std::error_code ec;
			std::filesystem::remove(path, ec);
		}
	}
};

struct TempDir {
	std::filesystem::path path;

	~TempDir() {
		if (!path.empty()) {
			std::error_code ec;
			std::filesystem::remove_all(path, ec);
		}
	}
};

struct OpenArchive {
	mod_archive_t *archive = nullptr;

	~OpenArchive() {
		if (archive) {
			modArchiveClose(archive);
		}
	}
};

TempArchive writeArchiveEntries(const std::string &stem,
                                const std::vector<std::pair<std::string, std::string>> &entries) {
	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive out{
		std::filesystem::temp_directory_path()
			/ ("pd2-" + stem + "-" + std::to_string(stamp) + ".zip")
	};

	mod_archive_writer_t *writer = modArchiveBegin(out.path.string().c_str());
	REQUIRE(writer != nullptr);
	for (const auto &entry : entries) {
		INFO("packing " << entry.first);
		if (modArchiveAddFileMem(writer, entry.first.c_str(),
				entry.second.data(), static_cast<u32>(entry.second.size())) != MODARCHIVE_OK) {
			modArchiveAbort(writer);
			FAIL("modArchiveAddFileMem failed for " << entry.first);
		}
	}
	REQUIRE(modArchiveFinish(writer) == MODARCHIVE_OK);
	return out;
}

TempArchive writeTypedArchiveEntries(const std::string &stem,
                                     const std::string &extension,
                                     const std::vector<std::pair<std::string, std::string>> &entries) {
	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive out{
		std::filesystem::temp_directory_path()
			/ ("pd2-" + stem + "-" + std::to_string(stamp) + extension)
	};

	mod_archive_writer_t *writer = modArchiveBegin(out.path.string().c_str());
	REQUIRE(writer != nullptr);
	for (const auto &entry : entries) {
		INFO("packing " << entry.first);
		if (modArchiveAddFileMem(writer, entry.first.c_str(),
				entry.second.data(), static_cast<u32>(entry.second.size())) != MODARCHIVE_OK) {
			modArchiveAbort(writer);
			FAIL("modArchiveAddFileMem failed for " << entry.first);
		}
	}
	REQUIRE(modArchiveFinish(writer) == MODARCHIVE_OK);
	return out;
}

std::string extractArchiveEntry(mod_archive_t *archive, const char *name) {
	const s32 idx = modArchiveFindEntry(archive, name);
	REQUIRE(idx >= 0);
	u32 size = 0;
	char *bytes = static_cast<char *>(modArchiveExtractAlloc(archive, idx, &size));
	REQUIRE(bytes != nullptr);
	std::string result(bytes, bytes + size);
	free(bytes);
	return result;
}

TEST_CASE("typed archive member replacement is atomic and preserves all other content",
          "[modding][pdxxx][archive][b955]") {
	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive archive{
		std::filesystem::temp_directory_path()
			/ ("pd2-archive-replace-" + std::to_string(stamp) + ".pdweapon")
	};

	mod_archive_writer_t *writer = modArchiveBegin(archive.path.string().c_str());
	REQUIRE(writer != nullptr);
	const std::string originalDescriptor =
		"[weapon]\ncatalog_id = example:before\nmodel_file = model.obj\n";
	const std::string payload = "o model\nv 0 0 0\n";
	REQUIRE(modArchiveAddFileMem(writer, "weapon.ini",
		originalDescriptor.data(), static_cast<u32>(originalDescriptor.size()))
		== MODARCHIVE_OK);
	REQUIRE(modArchiveAddFileMem(writer, "model.obj",
		payload.data(), static_cast<u32>(payload.size())) == MODARCHIVE_OK);
	modArchiveSetComment(writer, "{\"creator\":\"archive-test\"}");
	REQUIRE(modArchiveFinish(writer) == MODARCHIVE_OK);

	const std::string replacement =
		"[weapon]\ncatalog_id = example:after\nmodel_file = model.obj\n";
	REQUIRE(modArchiveReplaceFileMem(archive.path.string().c_str(), "weapon.ini",
		replacement.data(), static_cast<u32>(replacement.size())) == MODARCHIVE_OK);

	{
		OpenArchive opened;
		opened.archive = modArchiveOpen(archive.path.string().c_str());
		REQUIRE(opened.archive != nullptr);
		REQUIRE(extractArchiveEntry(opened.archive, "weapon.ini") == replacement);
		REQUIRE(extractArchiveEntry(opened.archive, "model.obj") == payload);
		REQUIRE(std::string(modArchiveGetComment(opened.archive))
			== "{\"creator\":\"archive-test\"}");
	}

	const std::string bytesBeforeMissingReplace =
		readFile(archive.path.string().c_str());
	REQUIRE(modArchiveReplaceFileMem(archive.path.string().c_str(), "wrong.ini",
		replacement.data(), static_cast<u32>(replacement.size()))
		== MODARCHIVE_ERR_NOTFOUND);
	REQUIRE(readFile(archive.path.string().c_str()) == bytesBeforeMissingReplace);
}

TEST_CASE("shared INI contract retains full audio descriptors and rejects overflow",
          "[modding][pdxxx][ini][b956]") {
	STATIC_REQUIRE(INI_MAX_PAIRS >= 37);

	std::ostringstream valid;
	valid << "[voice]\n";
	for (int i = 0; i < 37; i++) {
		valid << "field_" << i << " = value_" << i << "\n";
	}
	ini_section_t parsed = {};
	const std::string validText = valid.str();
	REQUIRE(iniParseBuffer("voice.ini", validText.data(),
		static_cast<u32>(validText.size()), &parsed) == 1);
	REQUIRE(parsed.count == 37);
	REQUIRE(std::string(iniGet(&parsed, "field_36", "")) == "value_36");

	std::ostringstream overflow;
	overflow << "[voice]\n";
	for (int i = 0; i <= INI_MAX_PAIRS; i++) {
		overflow << "field_" << i << " = value_" << i << "\n";
	}
	const std::string overflowText = overflow.str();
	REQUIRE(iniParseBuffer("overflow.ini", overflowText.data(),
		static_cast<u32>(overflowText.size()), &parsed) == 0);
}

TEST_CASE("Modding Hub edits the catalog entry descriptor instead of inventing a loose path",
          "[modding][pdxxx][hub][b954]") {
	const std::string catalog = readFile("port/include/assetcatalog.h");
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	const std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");

	REQUIRE(catalog.find("char descriptor_path[FS_MAXPATH]") !=
		std::string::npos);
	REQUIRE(scanner.find("e->descriptor_path, descriptor_path") !=
		std::string::npos);
	REQUIRE(hub.find("iniNameForEntry") != std::string::npos);
	REQUIRE(hub.find("case AUDIO_CAT_VOICE: return \"voice.ini\"") !=
		std::string::npos);
	REQUIRE(hub.find("case AUDIO_CAT_MUSIC: return \"music.ini\"") !=
		std::string::npos);
	REQUIRE(hub.find("default:              return \"sound.ini\"") !=
		std::string::npos);
	REQUIRE(hub.find("fsFileLoad(ie.descriptor_ref") != std::string::npos);
	REQUIRE(hub.find("modArchiveReplaceFileMem(") != std::string::npos);
	REQUIRE(hub.find("std::vector<IniKV> s_IniPairs") != std::string::npos);
	REQUIRE(hub.find("s_IniNumPairs < HUB_INI_MAX_KEYS") ==
		std::string::npos);
	REQUIRE(hub.find("\"%s/%s\", ie.dirpath, iniName") ==
		std::string::npos);
	REQUIRE(hub.find("selected.nested_archive") != std::string::npos);
}

TEST_CASE("gamemode public metadata controls the production selector and menu",
          "[modding][pdxxx][runtime][b953]") {
	const std::string scenarios = readFile("src/game/mplayer/scenarios.c");
	REQUIRE(scenarios.find("scenarioBindingForEntry") != std::string::npos);
	REQUIRE(scenarios.find("assetRuntimePrimaryFileAccessible(binding)") !=
		std::string::npos);
	REQUIRE(scenarios.find("binding->gamemode_team_based") !=
		std::string::npos);
	REQUIRE(scenarios.find("binding->gamemode_min_players") !=
		std::string::npos);
	REQUIRE(scenarios.find("binding->gamemode_max_players") !=
		std::string::npos);
	REQUIRE(scenarios.find("binding->gamemode_name") != std::string::npos);
	REQUIRE(scenarios.find("binding->gamemode_description") !=
		std::string::npos);
	REQUIRE(scenarios.find("(uintptr_t)&mpMenuTextScenarioDescription") !=
		std::string::npos);
	REQUIRE(scenarios.find("CATALOG.GAMEMODE.RUNTIME_MISS") ==
		std::string::npos);
	REQUIRE(scenarios.find("using ext.gamemode.team_based") ==
		std::string::npos);
}

TEST_CASE("boot fails closed when any extraction or walker phase is incomplete",
          "[modding][pdxxx][boot][b957]") {
	const std::string main = readFile("port/src/main.c");
	const std::string extract = readFile("port/src/romextract.c");

	REQUIRE(main.find("bootRecordAssetPhase(\"rom-files\"") !=
		std::string::npos);
	REQUIRE(main.find("bootRecordAssetPhase(\"pdmesh\"") !=
		std::string::npos);
	REQUIRE(main.find("bootRecordAssetPhase(\"pdweapon-projectile-entity\"") !=
		std::string::npos);
	REQUIRE(main.find("bootRecordAssetPhase(\"pdarena-pdscenario\"") !=
		std::string::npos);
	REQUIRE(main.find("bootRecordAssetPhase(\"pdtheme\"") !=
		std::string::npos);
	REQUIRE(main.find("walker_result.total_envelope_failures") !=
		std::string::npos);
	REQUIRE(main.find("walker_result.total_register_failures") !=
		std::string::npos);
	REQUIRE(main.find("runtime cache build skipped") != std::string::npos);
	REQUIRE(main.find("return 2;") != std::string::npos);

	REQUIRE(extract.find("return failed > 0 ? -1 : written;") !=
		std::string::npos);
	REQUIRE(extract.find("return failed > 0 ? -1 : corrected;") !=
		std::string::npos);
}

TEST_CASE("every typed-family emitter reports partial output as failure",
          "[modding][pdxxx][boot][b958]") {
	const std::vector<std::string> singleFamilyEmitters = {
		"port/src/romextract_pdanim_chr.c",
		"port/src/romextract_pdanim.c",
		"port/src/romextract_pdbody.c",
		"port/src/romextract_pdcharacter.c",
		"port/src/romextract_pdfont.c",
		"port/src/romextract_pdhead.c",
		"port/src/romextract_pdlang.c",
		"port/src/romextract_pdmesh.c",
		"port/src/romextract_pdsfx.c",
		"port/src/romextract_pdsong.c",
		"port/src/romextract_pdweapon.c",
	};
	for (const auto &path : singleFamilyEmitters) {
		INFO(path);
		const std::string source = readFile(path.c_str());
		REQUIRE(source.find("return failed ? -1 : written;") !=
			std::string::npos);
	}

	const std::string arena = readFile("port/src/romextract_pdarena.c");
	REQUIRE(arena.find("return (arenas_failed || scenarios_failed)") !=
		std::string::npos);

	const std::string ui = readFile("port/fast3d/pdgui_theme.cpp");
	REQUIRE(ui.find("return failed ? -1 : written;") != std::string::npos);

	/* These families already had the correct aggregate contract when B-958
	 * was found. Keep them in the enumeration so the all-family guard stays
	 * exhaustive rather than protecting only the originally broken files. */
	const std::string texture = readFile("port/src/romextract_pdtexture.c");
	const std::string metadata = readFile("port/src/romextract_pdmeta.c");
	const std::string theme =
		readFile("port/fast3d/pdgui_theme_loader.cpp");
	REQUIRE(texture.find("return failed ? -1 : written;") !=
		std::string::npos);
	REQUIRE(metadata.find("return failed ? -1 : written;") !=
		std::string::npos);
	REQUIRE(theme.find("return failed ? -1 : written;") !=
		std::string::npos);

	const std::string main = readFile("port/src/main.c");
	REQUIRE(main.find(
		"bootRecordAssetPhase(\"pdtexture-late-ui-repair\", texture_result)") !=
		std::string::npos);
	REQUIRE(main.find(
		"bootRecordAssetPhase(\"pdui-late-repair\", written)") !=
		std::string::npos);
	REQUIRE(main.find("kr.envelope_failures + kr.register_failures") !=
		std::string::npos);
	REQUIRE(main.find("late UI walker rejected") != std::string::npos);
}

TEST_CASE("selected public sources never fall through to native or opaque data",
          "[modding][pdxxx][runtime][b960]") {
	const std::string fonts = readFile("src/game/game_1531a0.c");
	REQUIRE(fonts.find("ASSET.CHAIN: font face") != std::string::npos);
	REQUIRE(fonts.find("assetFallbackRecord(ASSET_FONT") ==
		std::string::npos);
	REQUIRE(fonts.find("public source unavailable -> ROM segment") ==
		std::string::npos);

	const std::string mod = readFile("port/src/mod.c");
	const std::string texture_source =
		readFile("port/src/mod_texture_source.c");
	REQUIRE(mod.find("assetSourceDebugIsEnabledFor") ==
		std::string::npos);
	REQUIRE(texture_source.find("assetSourceDebugIsEnabledFor") ==
		std::string::npos);
	REQUIRE(mod.find("fsFileLoadTo(r.path") == std::string::npos);
	REQUIRE(mod.find("sequence source compile failed -> legacy") ==
		std::string::npos);
	REQUIRE(mod.find("falling back to legacy sequence") ==
		std::string::npos);
	REQUIRE(mod.find("fsFileLoad(r.path") == std::string::npos);

	const std::string sound = readFile("src/lib/snd.c");
	REQUIRE(sound.find("assetSourceDebugIsEnabledFor") ==
		std::string::npos);
	REQUIRE(sound.find("falling back to ROM") == std::string::npos);
	REQUIRE(sound.find("romExtractRelPathForFilenum") ==
		std::string::npos);
	REQUIRE(sound.find("loose extracted source") == std::string::npos);
	REQUIRE(sound.find("fileGetRomAddress(filenum)") == std::string::npos);
	REQUIRE(sound.find("fileGetRomSize(filenum)") == std::string::npos);
	REQUIRE(sound.find("sndMp3ResolvePublicSource") != std::string::npos);

	const std::string propsnd = readFile("src/game/propsnd.c");
	REQUIRE(propsnd.find("assetSourceDebugIsEnabledFor") ==
		std::string::npos);
	REQUIRE(propsnd.find("romExtractRelPathForFilenum") ==
		std::string::npos);
	REQUIRE(propsnd.find("fileGetRomSize(filenum)") ==
		std::string::npos);
	REQUIRE(propsnd.find("psMp3DurationGetPublicSource60") !=
		std::string::npos);
	REQUIRE(propsnd.find("ASSET.CHAIN: MP3 file") != std::string::npos);
}

TEST_CASE("typed asset archive policy enforces the two-zone migration boundary",
          "[modding][pdxxx][policy][c3824]") {
	char err[256];

	auto clean = writeTypedArchiveEntries("policy-clean", ".pdweapon", {
		{ "weapon.ini",
		  "[weapon]\n"
		  "catalog_id = mod:weapon_policy_clean\n"
		  "manifest = _meta/manifest.json\n"
		  "model_file = model.obj\n" },
		{ "model.obj", "o model\n" },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"weapon\" }\n" },
		{ "_meta/provenance.json", "{ \"source_path\": \"C:/rom/source/model.obj\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(clean.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) == 0);

	OpenArchive opened;
	opened.archive = modArchiveOpen(clean.path.string().c_str());
	REQUIRE(opened.archive != nullptr);
	const s32 metaIdx = assetArchiveFindMetadataEntry(opened.archive,
		ASSET_ARCHIVE_META_MANIFEST);
	REQUIRE(metaIdx >= 0);
	REQUIRE(std::string(modArchiveGetEntryName(opened.archive, metaIdx))
		== "_meta/manifest.json");

	auto legacyRoot = writeTypedArchiveEntries("policy-legacy-root", ".pdweapon", {
		{ "weapon.ini",
		  "[weapon]\n"
		  "catalog_id = mod:weapon_policy_legacy\n"
		  "manifest = manifest.json\n" },
		{ "manifest.json", "{ \"pd_kind\": \"weapon\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(legacyRoot.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_MIGRATION, err, sizeof(err)) == 0);
	REQUIRE(assetArchiveValidateFile(legacyRoot.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("archive root") != std::string::npos);

	auto legacyTsv = writeTypedArchiveEntries("policy-legacy-tsv", ".pdscenario", {
		{ "scenario.ini",
		  "[scenario]\n"
		  "catalog_id = mod:scenario_policy_legacy_tsv\n"
		  "scene_file = scene.glb\n" },
		{ "scene.glb", "glb" },
		{ "objects.tsv", "object_id\tkind\nobject_0000\tprop\n" },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"scenario\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(legacyTsv.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_MIGRATION, err, sizeof(err)) == 0);
	REQUIRE(assetArchiveValidateFile(legacyTsv.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("TSV") != std::string::npos);
	const std::string legacyTsvBytes = readFile(legacyTsv.path.string().c_str());
	REQUIRE(assetArchiveValidateBytes(legacyTsvBytes.data(),
		static_cast<u32>(legacyTsvBytes.size()),
		legacyTsv.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("objects.tsv") != std::string::npos);

	auto legacyMesh = writeTypedArchiveEntries("policy-legacy-mesh", ".pdmesh", {
		{ "model.ini",
		  "[model]\n"
		  "catalog_id = mod:model_policy_legacy\n"
		  "geometry_file = model.obj\n" },
		{ "model.obj", "o mesh\n" },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"mesh\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(legacyMesh.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_MIGRATION, err, sizeof(err)) == 0);
	REQUIRE(assetArchiveValidateFile(legacyMesh.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("mesh.ini") != std::string::npos);

	auto missingGltfUri = writeTypedArchiveEntries("policy-missing-gltf-uri", ".pdhead", {
		{ "head.ini",
		  "[head]\n"
		  "catalog_id = mod:head_policy_missing_gltf_uri\n"
		  "model_file = model.gltf\n" },
		{ "model.gltf",
		  "{ \"asset\": { \"version\": \"2.0\" }, "
		  "\"images\": [ { \"uri\": \"texture.png\" } ] }\n" },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"head\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(missingGltfUri.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("texture.png") != std::string::npos);
	const std::string missingGltfBytes =
		readFile(missingGltfUri.path.string().c_str());
	REQUIRE(assetArchiveValidateBytes(missingGltfBytes.data(),
		static_cast<u32>(missingGltfBytes.size()),
		missingGltfUri.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("texture.png") != std::string::npos);

	auto objMtlChain = writeTypedArchiveEntries("policy-obj-mtl-chain", ".pdmesh", {
		{ "mesh.ini",
		  "[mesh]\n"
		  "catalog_id = mod:mesh_policy_obj_mtl_chain\n"
		  "geometry_file = geometry/model.obj\n" },
		{ "geometry/model.obj",
		  "mtllib materials/model.mtl\n"
		  "v 0 0 0\n"
		  "v 1 0 0\n"
		  "v 0 1 0\n"
		  "usemtl mat\n"
		  "f 1 2 3\n" },
		{ "geometry/materials/model.mtl",
		  "newmtl mat\n"
		  "map_Kd textures/diffuse.png\n" },
		{ "geometry/materials/textures/diffuse.png", "png" },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"mesh\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(objMtlChain.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) == 0);

	auto missingObjTexture = writeTypedArchiveEntries("policy-missing-obj-texture", ".pdmesh", {
		{ "mesh.ini",
		  "[mesh]\n"
		  "catalog_id = mod:mesh_policy_missing_obj_texture\n"
		  "geometry_file = geometry/model.obj\n" },
		{ "geometry/model.obj", "mtllib materials/model.mtl\n" },
		{ "geometry/materials/model.mtl",
		  "newmtl mat\n"
		  "map_Kd textures/diffuse.png\n" },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"mesh\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(missingObjTexture.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("textures/diffuse.png") != std::string::npos);

	auto missingNestedArchive = writeTypedArchiveEntries("policy-missing-nested", ".pdcharacter", {
		{ "character.ini",
		  "[character]\n"
		  "catalog_id = mod:character_policy_missing_nested\n"
		  "body_archive = dependencies/assets/body/tri_body.pdbody\n" },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"character\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(missingNestedArchive.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("tri_body.pdbody") != std::string::npos);

	const char manifestMissingDependency[] =
		"{\n"
		"  \"pd_kind\": \"lang\",\n"
		"  \"dependencies\": [\n"
		"    {\n"
		"      \"role\": \"font\",\n"
		"      \"type\": \"font\",\n"
		"      \"id\": \"base:font_body\",\n"
		"      \"archive\": \"dependencies/assets/font/base_font_body.pdfont\",\n"
		"      \"required\": true,\n"
		"      \"version\": \"\",\n"
		"      \"sha256\": \"\",\n"
		"      \"fallback\": { \"id\": \"\", \"reason\": \"\" }\n"
		"    }\n"
		"  ],\n"
		"  \"dependency_schema\": {\n"
		"    \"root\": \"dependencies/assets\",\n"
		"    \"required_fields\": [\"role\", \"type\", \"id\", \"archive\", \"required\", \"version\", \"sha256\", \"fallback.id\", \"fallback.reason\"]\n"
		"  }\n"
		"}\n";
	auto missingManifestDependency = writeTypedArchiveEntries(
		"policy-missing-manifest-dependency", ".pdlang", {
		{ "lang.ini",
		  "[lang]\n"
		  "catalog_id = mod:lang_policy_missing_manifest_dependency\n"
		  "manifest = _meta/manifest.json\n"
		  "strings_file = strings.json\n" },
		{ "strings.json",
		  "{ \"pd_kind\": \"language_strings\", \"strings\": [ { \"index\": 0, \"text\": \"Missing dependency\" } ] }\n" },
		{ "_meta/manifest.json", manifestMissingDependency },
	});
	REQUIRE(assetArchiveValidateFile(missingManifestDependency.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("no explicit fallback") != std::string::npos);
	const std::string missingManifestBytes =
		readFile(missingManifestDependency.path.string().c_str());
	REQUIRE(assetArchiveValidateBytes(missingManifestBytes.data(),
		static_cast<u32>(missingManifestBytes.size()),
		missingManifestDependency.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("no explicit fallback") != std::string::npos);

	const char manifestBaseFallback[] =
		"{\n"
		"  \"pd_kind\": \"lang\",\n"
		"  \"dependencies\": [\n"
		"    {\n"
		"      \"role\": \"font\",\n"
		"      \"type\": \"font\",\n"
		"      \"id\": \"base:font_body\",\n"
		"      \"archive\": \"dependencies/assets/font/base_font_body.pdfont\",\n"
		"      \"required\": false,\n"
		"      \"version\": \"\",\n"
		"      \"sha256\": \"\",\n"
		"      \"fallback\": { \"id\": \"base:font_body\", \"reason\": \"base asset supplied by runtime\" }\n"
		"    }\n"
		"  ],\n"
		"  \"dependency_schema\": {\n"
		"    \"root\": \"dependencies/assets\",\n"
		"    \"required_fields\": [\"role\", \"type\", \"id\", \"archive\", \"required\", \"version\", \"sha256\", \"fallback.id\", \"fallback.reason\"]\n"
		"  }\n"
		"}\n";
	auto fallbackManifestDependency = writeTypedArchiveEntries(
		"policy-fallback-manifest-dependency", ".pdlang", {
		{ "lang.ini",
		  "[lang]\n"
		  "catalog_id = mod:lang_policy_fallback_manifest_dependency\n"
		  "manifest = _meta/manifest.json\n"
		  "strings_file = strings.json\n" },
		{ "strings.json",
		  "{ \"pd_kind\": \"language_strings\", \"strings\": [ { \"index\": 0, \"text\": \"Fallback dependency\" } ] }\n" },
		{ "_meta/manifest.json", manifestBaseFallback },
	});
	REQUIRE(assetArchiveValidateFile(fallbackManifestDependency.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) == 0);

	const char manifestUnsafeDependency[] =
		"{\n"
		"  \"pd_kind\": \"lang\",\n"
		"  \"dependencies\": [\n"
		"    {\n"
		"      \"role\": \"font\",\n"
		"      \"type\": \"font\",\n"
		"      \"id\": \"base:font_body\",\n"
		"      \"archive\": \"../base_font_body.pdfont\",\n"
		"      \"required\": false,\n"
		"      \"version\": \"\",\n"
		"      \"sha256\": \"\",\n"
		"      \"fallback\": { \"id\": \"base:font_body\", \"reason\": \"base asset supplied by runtime\" }\n"
		"    }\n"
		"  ],\n"
		"  \"dependency_schema\": {\n"
		"    \"root\": \"dependencies/assets\",\n"
		"    \"required_fields\": [\"role\", \"type\", \"id\", \"archive\", \"required\", \"version\", \"sha256\", \"fallback.id\", \"fallback.reason\"]\n"
		"  }\n"
		"}\n";
	auto unsafeManifestDependency = writeTypedArchiveEntries(
		"policy-unsafe-manifest-dependency", ".pdlang", {
		{ "lang.ini",
		  "[lang]\n"
		  "catalog_id = mod:lang_policy_unsafe_manifest_dependency\n"
		  "manifest = _meta/manifest.json\n"
		  "strings_file = strings.json\n" },
		{ "strings.json",
		  "{ \"pd_kind\": \"language_strings\", \"strings\": [ { \"index\": 0, \"text\": \"Unsafe dependency\" } ] }\n" },
		{ "_meta/manifest.json", manifestUnsafeDependency },
	});
	REQUIRE(assetArchiveValidateFile(unsafeManifestDependency.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("unsafe dependency archive") !=
		std::string::npos);

	auto badNestedBody = writeTypedArchiveEntries("policy-bad-nested-body", ".pdbody", {
		{ "body.ini",
		  "[body]\n"
		  "catalog_id = mod:body_policy_bad_nested\n"
		  "model_file = model.gltf\n" },
		{ "model.gltf",
		  "{ \"asset\": { \"version\": \"2.0\" }, "
		  "\"images\": [ { \"uri\": \"missing_nested_texture.png\" } ] }\n" },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"body\" }\n" },
	});
	const std::string badNestedBytes =
		readFile(badNestedBody.path.string().c_str());
	auto recursiveNested = writeTypedArchiveEntries("policy-recursive-nested", ".pdcharacter", {
		{ "character.ini",
		  "[character]\n"
		  "catalog_id = mod:character_policy_recursive_nested\n"
		  "body_archive = dependencies/assets/body/bad_body.pdbody\n" },
		{ "dependencies/assets/body/bad_body.pdbody", badNestedBytes },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"character\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(recursiveNested.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("missing_nested_texture.png") != std::string::npos);

	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive plain{
		std::filesystem::temp_directory_path()
			/ ("pd2-policy-plain-" + std::to_string(stamp) + ".pdhead")
	};
	{
		std::ofstream out(plain.path, std::ios::binary);
		out << "[head]\nmodel_file = model.gltf\n";
	}
	REQUIRE(assetArchiveValidateFile(plain.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("zip-openable") != std::string::npos);
	REQUIRE(assetArchivePathIsDeprecated("weapons/old.pdwpn") == 1);
}

struct MountedArchive {
	std::string modId;
	bool mounted = false;

	~MountedArchive() {
		if (mounted) {
			modVfsUnmount(modId.c_str());
		}
	}
};

TempArchive packArchiveFixture() {
	const std::filesystem::path root =
		"tests/fixtures/modpipe/pdxxx-content";
	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive out{
		std::filesystem::temp_directory_path()
			/ ("pd2-modpipe-fixture-" + std::to_string(stamp) + ".pdmod")
	};

	std::vector<std::pair<std::string, std::filesystem::path>> files;
	for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
		if (!entry.is_regular_file()) {
			continue;
		}
		const std::string rel =
			std::filesystem::relative(entry.path(), root).generic_string();
		files.emplace_back(rel, entry.path());
	}
	std::sort(files.begin(), files.end(),
		[](const auto &a, const auto &b) { return a.first < b.first; });
	REQUIRE_FALSE(files.empty());

	mod_archive_writer_t *writer = modArchiveBegin(out.path.string().c_str());
	REQUIRE(writer != nullptr);
	modArchiveSetComment(writer,
		"{\"id\":\"fixture_pdxxx_content\",\"layout\":\"pdxxx-transport\"}");

	for (const auto &file : files) {
		INFO("packing " << file.first);
		if (modArchiveAddFileDisk(writer, file.first.c_str(),
				file.second.string().c_str()) != MODARCHIVE_OK) {
			modArchiveAbort(writer);
			FAIL("modArchiveAddFileDisk failed for " << file.first);
		}
	}

	REQUIRE(modArchiveFinish(writer) == MODARCHIVE_OK);
	return out;
}

std::string readMountedText(const char *modId, const char *path) {
	u32 size = 0;
	void *bytes = modVfsResolveAllocFromMount(modId, path, &size);
	REQUIRE(bytes != nullptr);
	std::string text(static_cast<const char *>(bytes), size);
	free(bytes);
	return text;
}

std::string readArchiveEntryText(const char *archivePath, const char *entryPath) {
	OpenArchive opened;
	opened.archive = modArchiveOpen(archivePath);
	REQUIRE(opened.archive != nullptr);
	s32 idx = modArchiveFindEntry(opened.archive, entryPath);
	REQUIRE(idx >= 0);
	u32 size = 0;
	void *bytes = modArchiveExtractAlloc(opened.archive, idx, &size);
	REQUIRE(bytes != nullptr);
	std::string text(static_cast<const char *>(bytes), size);
	free(bytes);
	return text;
}

std::string readArchiveEntryText(mod_archive_t *archive, const char *entryPath) {
	REQUIRE(archive != nullptr);
	s32 idx = modArchiveFindEntry(archive, entryPath);
	REQUIRE(idx >= 0);
	u32 size = 0;
	void *bytes = modArchiveExtractAlloc(archive, idx, &size);
	REQUIRE(bytes != nullptr);
	std::string text(static_cast<const char *>(bytes), size);
	free(bytes);
	return text;
}

TEST_CASE("bundled pdmod manifests declare explicit stable ids",
          "[modding][pdmod][static][c3844]") {
	const std::string baseUi =
		readArchiveEntryText("mods/base-ui.pdmod", "mod.json");
	const std::string modernUi =
		readArchiveEntryText("mods/pd-modern-ui.pdmod", "mod.json");
	const std::string modernSource =
		readFile("mods/pd-modern-ui.legacy_backup/mod.json");

	REQUIRE(baseUi.find("\"id\": \"base-ui\"") != std::string::npos);
	REQUIRE(modernUi.find("\"id\": \"pd-modern-ui\"") != std::string::npos);
	REQUIRE(modernSource.find("\"id\": \"pd-modern-ui\"") !=
	        std::string::npos);
}

TEST_CASE("shared typed archive writer emits c3838 metadata contract",
          "[modding][pdxxx][writer][c3838]") {
	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive out{
		std::filesystem::temp_directory_path()
			/ ("pd2-writer-contract-" + std::to_string(stamp) + ".pdlang")
	};

	mod_archive_writer_t *raw = modArchiveBegin(out.path.string().c_str());
	REQUIRE(raw != nullptr);

	asset_archive_writer_t writer;
	REQUIRE(assetArchiveWriterInit(&writer, raw, "lang",
		"base:lang_writer_contract_en") == MODARCHIVE_OK);
	assetArchiveWriterSetProvenance(&writer, "pd-tests",
		"data/test/lang.raw", 7, "FILE_LTESTE");

	const char descriptor[] =
		"[lang]\n"
		"catalog_id = base:lang_writer_contract_en\n"
		"manifest = _meta/manifest.json\n"
		"strings_file = strings.json\n";
	const char manifest[] =
		"{\n"
		"  \"pd_kind\": \"lang\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"base:lang_writer_contract_en\",\n"
		"  \"data\": \"strings.json\",\n"
		"  \"dependencies\": [\n"
		"    {\n"
		"      \"role\": \"font\",\n"
		"      \"type\": \"font\",\n"
		"      \"id\": \"base:font_body\",\n"
		"      \"archive\": \"dependencies/assets/font/base_font_body.pdfont\",\n"
		"      \"required\": false,\n"
		"      \"version\": \"1\",\n"
		"      \"sha256\": \"\",\n"
		"      \"fallback\": {\n"
		"        \"id\": \"base:font_body\",\n"
		"        \"reason\": \"base asset supplied by runtime\"\n"
		"      }\n"
		"    }\n"
		"  ],\n"
		"  \"dependency_schema\": {\n"
		"    \"root\": \"dependencies/assets\",\n"
		"    \"required_fields\": [\"role\", \"type\", \"id\", \"archive\", \"required\", \"version\", \"sha256\", \"fallback.id\", \"fallback.reason\"]\n"
		"  }\n"
		"}\n";
	const char strings[] =
		"{\n"
		"  \"pd_kind\": \"language_strings\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"strings\": [\n"
		"    { \"index\": 0, \"text\": \"Hello\" },\n"
		"    { \"index\": 1, \"text\": \"Archive\" }\n"
		"  ]\n"
		"}\n";

	REQUIRE(assetArchiveWriterAddDescriptor(&writer, "lang.ini",
		descriptor, (u32)strlen(descriptor)) == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterAddManifestJson(&writer, manifest,
		(u32)strlen(manifest)) == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterAddPublicMem(&writer, "strings.json",
		strings, (u32)strlen(strings), "strings") == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterFinishMetadata(&writer) == MODARCHIVE_OK);
	REQUIRE(modArchiveFinish(raw) == MODARCHIVE_OK);

	char err[256];
	REQUIRE(assetArchiveValidateFile(out.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) == 0);

	OpenArchive opened;
	opened.archive = modArchiveOpen(out.path.string().c_str());
	REQUIRE(opened.archive != nullptr);

	REQUIRE(modArchiveFindEntry(opened.archive, "lang.ini") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive, "strings.json") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive, "_meta/manifest.json") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive, "_meta/inventory.json") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive, "_meta/hashes.json") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive, "_meta/provenance.json") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive, "_meta/validation.json") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive,
		"_meta/source-handles.json") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive, "_meta/lang.ini.sha256") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive,
		"_meta/strings.json.sha256") >= 0);

	const std::string inventory =
		readArchiveEntryText(opened.archive, "_meta/inventory.json");
	REQUIRE(inventory.find("\"path\": \"lang.ini\"") != std::string::npos);
	REQUIRE(inventory.find("\"role\": \"descriptor\"") != std::string::npos);
	REQUIRE(inventory.find("\"path\": \"strings.json\"") != std::string::npos);
	REQUIRE(inventory.find("\"role\": \"strings\"") != std::string::npos);
	REQUIRE(inventory.find("\"dependency_archive_root\": \"dependencies/assets\"") !=
		std::string::npos);

	const std::string hashes =
		readArchiveEntryText(opened.archive, "_meta/hashes.json");
	REQUIRE(hashes.find("\"schema\": \"pd2.asset.hashes.v1\"") != std::string::npos);
	REQUIRE(hashes.find("\"path\": \"lang.ini\"") != std::string::npos);
	REQUIRE(hashes.find("\"path\": \"strings.json\"") != std::string::npos);
	REQUIRE(hashes.find("\"role\": \"descriptor\"") != std::string::npos);
	REQUIRE(hashes.find("\"role\": \"strings\"") != std::string::npos);

	const std::string validation =
		readArchiveEntryText(opened.archive, "_meta/validation.json");
	REQUIRE(validation.find("\"status\": \"writer_checked\"") !=
		std::string::npos);
	REQUIRE(validation.find("\"fallback.id\"") != std::string::npos);
	REQUIRE(validation.find("\"fallback.reason\"") != std::string::npos);

	const std::string sourceHandles =
		readArchiveEntryText(opened.archive, "_meta/source-handles.json");
	REQUIRE(sourceHandles.find("\"path\": \"data/test/lang.raw\"") !=
		std::string::npos);
	REQUIRE(sourceHandles.find("\"symbol\": \"FILE_LTESTE\"") !=
		std::string::npos);
}

TEST_CASE("shared typed archive writer infers c3838 dependency records",
          "[modding][pdxxx][writer][c3838]") {
	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive out{
		std::filesystem::temp_directory_path()
			/ ("pd2-writer-deps-" + std::to_string(stamp) + ".pdlang")
	};

	mod_archive_writer_t *raw = modArchiveBegin(out.path.string().c_str());
	REQUIRE(raw != nullptr);

	asset_archive_writer_t writer;
	REQUIRE(assetArchiveWriterInit(&writer, raw, "lang",
		"base:lang_writer_dependency_en") == MODARCHIVE_OK);

	const char descriptor[] =
		"[lang]\n"
		"catalog_id = base:lang_writer_dependency_en\n"
		"manifest = _meta/manifest.json\n"
		"strings_file = strings.json\n"
		"font_archive = dependencies/assets/font/base_font_body.pdfont\n";
	const char strings[] =
		"{ \"pd_kind\": \"language_strings\", \"strings\": [ { \"index\": 0, \"text\": \"Dependency\" } ] }\n";
	const char nested[] = "placeholder nested archive bytes";

	REQUIRE(assetArchiveWriterAddDescriptor(&writer, "lang.ini",
		descriptor, (u32)strlen(descriptor)) == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterAddPublicMem(&writer, "strings.json",
		strings, (u32)strlen(strings), "strings") == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterAddPublicMem(&writer,
		"dependencies/assets/font/base_font_body.pdfont",
		nested, (u32)strlen(nested), "font") == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterFinishMetadata(&writer) == MODARCHIVE_OK);
	REQUIRE(modArchiveFinish(raw) == MODARCHIVE_OK);

	OpenArchive opened;
	opened.archive = modArchiveOpen(out.path.string().c_str());
	REQUIRE(opened.archive != nullptr);

	const std::string manifest =
		readArchiveEntryText(opened.archive, "_meta/manifest.json");
	REQUIRE(manifest.find("\"dependencies\": [") != std::string::npos);
	REQUIRE(manifest.find("\"role\": \"font\"") != std::string::npos);
	REQUIRE(manifest.find("\"type\": \"font\"") != std::string::npos);
	REQUIRE(manifest.find("\"id\": \"base:font_body\"") != std::string::npos);
	REQUIRE(manifest.find("\"archive\": \"dependencies/assets/font/base_font_body.pdfont\"") !=
		std::string::npos);
	REQUIRE(manifest.find("\"required\": true") != std::string::npos);
	REQUIRE(manifest.find("\"sha256\": \"") != std::string::npos);
	REQUIRE(manifest.find("\"fallback\": {") != std::string::npos);
	REQUIRE(manifest.find("\"id\": \"\",\n        \"reason\": \"\"") !=
		std::string::npos);

	const std::string validation =
		readArchiveEntryText(opened.archive, "_meta/validation.json");
	REQUIRE(validation.find("\"dependency_archive_count\": 1") !=
		std::string::npos);
	REQUIRE(validation.find("\"fallback.reason\"") != std::string::npos);
}

TEST_CASE("shared typed archive writer preserves projectile entity source members",
          "[modding][pdxxx][writer][projectile][entity][c3838]") {
	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive out{
		std::filesystem::temp_directory_path()
			/ ("pd2-writer-entity-source-" + std::to_string(stamp) + ".pdentity")
	};

	mod_archive_writer_t *raw = modArchiveBegin(out.path.string().c_str());
	REQUIRE(raw != nullptr);

	asset_archive_writer_t writer;
	REQUIRE(assetArchiveWriterInit(&writer, raw, "entity",
		"base:writer_entity_source") == MODARCHIVE_OK);

	const char descriptor[] =
		"[entity]\n"
		"catalog_id = base:writer_entity_source\n"
		"bindings_file = bindings.json\n"
		"behavior_graph = behavior.graph.json\n"
		"composition_file = composition.json\n";
	const char bindings[] = "{ \"schema\": \"pd.entity.bindings.v1\" }\n";
	const char graph[] = "{ \"schema\": \"pd.entity_graph.v1\", \"nodes\": [] }\n";
	const char composition[] = "{ \"schema\": \"pd.entity.composition.v1\" }\n";

	REQUIRE(assetArchiveWriterAddDescriptor(&writer, "entity.ini",
		descriptor, (u32)strlen(descriptor)) == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterAddPublicMem(&writer, "bindings.json",
		bindings, (u32)strlen(bindings), "bindings") == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterAddPublicMem(&writer, "behavior.graph.json",
		graph, (u32)strlen(graph), "behavior") == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterAddPublicMem(&writer, "composition.json",
		composition, (u32)strlen(composition), "composition") == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterFinishMetadata(&writer) == MODARCHIVE_OK);
	REQUIRE(modArchiveFinish(raw) == MODARCHIVE_OK);

	OpenArchive opened;
	opened.archive = modArchiveOpen(out.path.string().c_str());
	REQUIRE(opened.archive != nullptr);

	const std::string manifest =
		readArchiveEntryText(opened.archive, "_meta/manifest.json");
	REQUIRE(manifest.find("\"bindings_file\": \"bindings.json\"") != std::string::npos);
	REQUIRE(manifest.find("\"behavior_graph\": \"behavior.graph.json\"") != std::string::npos);
	REQUIRE(manifest.find("\"composition_file\": \"composition.json\"") != std::string::npos);
	REQUIRE(manifest.find("\"dependencies\": [") != std::string::npos);
}

struct ExpectedPdxxxArchive {
	const char *assetType;
	const char *extension;
	const char *relPath;
	std::vector<const char *> requiredEntries;
};

std::string trimCopy(const std::string &value) {
	const char *ws = " \t\r\n";
	const size_t first = value.find_first_not_of(ws);
	if (first == std::string::npos) {
		return "";
	}
	const size_t last = value.find_last_not_of(ws);
	return value.substr(first, last - first + 1);
}

bool startsWith(const std::string &value, const char *prefix) {
	return value.rfind(prefix, 0) == 0;
}

bool archiveHasEntry(mod_archive_t *archive, const std::string &entryPath) {
	return archive && modArchiveFindEntry(archive, entryPath.c_str()) >= 0;
}

bool isTypedPdxxxPath(const std::filesystem::path &path) {
	const std::string ext = path.extension().string();
	const char *known[] = {
		".pdanim", ".pdarena", ".pdbody", ".pdbotprofile",
		".pdcharacter", ".pdeffect", ".pdentity", ".pdfont",
		".pdgamemode", ".pdhead", ".pdhud", ".pdlang",
		".pdmaterial", ".pdmesh", ".pdmission", ".pdprojectile",
		".pdprop", ".pdscenario", ".pdskin", ".pdsfx",
		".pdsong", ".pdtexture", ".pdtheme", ".pdui",
		".pdvehicle", ".pdvoice", ".pdweapon",
	};
	for (const char *value : known) {
		if (ext == value) {
			return true;
		}
	}
	return false;
}

std::string readArchiveEntryBytes(mod_archive_t *archive,
                                  const std::string &entryPath) {
	REQUIRE(archive != nullptr);
	const s32 idx = modArchiveFindEntry(archive, entryPath.c_str());
	REQUIRE(idx >= 0);
	u32 size = 0;
	void *bytes = modArchiveExtractAlloc(archive, idx, &size);
	REQUIRE(bytes != nullptr);
	std::string data(static_cast<const char *>(bytes), static_cast<size_t>(size));
	free(bytes);
	return data;
}

bool archiveNestedHasEntry(mod_archive_t *archive,
                           const char *nestedEntry,
                           const char *innerEntry) {
	REQUIRE(archive != nullptr);
	const s32 idx = modArchiveFindEntry(archive, nestedEntry);
	REQUIRE(idx >= 0);
	u32 nestedSize = 0;
	void *nestedBytes = modArchiveExtractAlloc(archive, idx, &nestedSize);
	REQUIRE(nestedBytes != nullptr);
	u32 innerSize = 0;
	void *innerBytes = modArchiveExtractMemAlloc(nestedBytes, nestedSize,
		innerEntry, &innerSize);
	free(nestedBytes);
	if (!innerBytes) {
		return false;
	}
	free(innerBytes);
	(void)innerSize;
	return true;
}

std::string readNestedArchiveEntryText(mod_archive_t *archive,
                                       const char *nestedEntry,
                                       const char *innerEntry) {
	REQUIRE(archive != nullptr);
	const s32 idx = modArchiveFindEntry(archive, nestedEntry);
	REQUIRE(idx >= 0);
	u32 nestedSize = 0;
	void *nestedBytes = modArchiveExtractAlloc(archive, idx, &nestedSize);
	REQUIRE(nestedBytes != nullptr);
	u32 innerSize = 0;
	void *innerBytes = modArchiveExtractMemAlloc(nestedBytes, nestedSize,
		innerEntry, &innerSize);
	free(nestedBytes);
	REQUIRE(innerBytes != nullptr);
	std::string text(static_cast<const char *>(innerBytes),
		static_cast<size_t>(innerSize));
	free(innerBytes);
	return text;
}

std::string readArchiveChainEntryText(
		const char *archivePath,
		const std::vector<const char *> &nestedEntries,
		const char *innerEntry) {
	std::string archiveBytes = readFile(archivePath);

	for (const char *nestedEntry : nestedEntries) {
		u32 nestedSize = 0;
		void *nestedBytes = modArchiveExtractMemAlloc(
			archiveBytes.data(), static_cast<u32>(archiveBytes.size()),
			nestedEntry, &nestedSize);
		INFO(archivePath << " -> " << nestedEntry);
		REQUIRE(nestedBytes != nullptr);
		archiveBytes.assign(static_cast<const char *>(nestedBytes),
			static_cast<size_t>(nestedSize));
		free(nestedBytes);
	}

	u32 innerSize = 0;
	void *innerBytes = modArchiveExtractMemAlloc(
		archiveBytes.data(), static_cast<u32>(archiveBytes.size()),
		innerEntry, &innerSize);
	INFO(archivePath << " -> " << innerEntry);
	REQUIRE(innerBytes != nullptr);
	std::string text(static_cast<const char *>(innerBytes),
		static_cast<size_t>(innerSize));
	free(innerBytes);
	return text;
}

std::string entryRelativePath(const std::string &baseEntry,
                              const std::string &relative) {
	const size_t slash = baseEntry.find_last_of('/');
	if (slash == std::string::npos) {
		return relative;
	}
	return baseEntry.substr(0, slash + 1) + relative;
}

bool archiveHasRootOrRelativeEntry(mod_archive_t *archive,
                                   const std::string &baseEntry,
                                   const std::string &value) {
	return archiveHasEntry(archive, value) ||
		archiveHasEntry(archive, entryRelativePath(baseEntry, value));
}

void requireIniPathRefsResolve(mod_archive_t *archive,
                               const char *archiveRel,
                               const char *descriptorEntry,
                               const std::vector<const char *> &keys) {
	const std::string descriptor = readArchiveEntryText(archive, descriptorEntry);
	std::istringstream in(descriptor);
	std::string line;
	while (std::getline(in, line)) {
		std::string text = trimCopy(line);
		if (text.empty() || text[0] == ';' || text[0] == '#') {
			continue;
		}
		const size_t eq = text.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		const std::string key = trimCopy(text.substr(0, eq));
		const std::string value = trimCopy(text.substr(eq + 1));
		if (value.empty()) {
			continue;
		}
		for (const char *expectedKey : keys) {
			if (key == expectedKey) {
				INFO(std::string(archiveRel) + "::" + descriptorEntry
					+ " -> " + key + " = " + value);
				REQUIRE(value.find("..") == std::string::npos);
				REQUIRE(value.find(':') == std::string::npos);
				REQUIRE(archiveHasRootOrRelativeEntry(archive,
					descriptorEntry, value));
			}
		}
	}
}

void requireGltfUrisResolve(mod_archive_t *archive,
                            const char *archiveRel,
                            const char *gltfEntry) {
	const std::string gltf = readArchiveEntryText(archive, gltfEntry);
	const std::regex uriPattern("\"uri\"\\s*:\\s*\"([^\"]+)\"");
	for (auto it = std::sregex_iterator(gltf.begin(), gltf.end(), uriPattern);
			it != std::sregex_iterator(); ++it) {
		const std::string uri = (*it)[1].str();
		INFO(std::string(archiveRel) + "::" + gltfEntry + " uri=" + uri);
		if (startsWith(uri, "data:")) {
			continue;
		}
		REQUIRE(uri.find("..") == std::string::npos);
		REQUIRE(uri.find(':') == std::string::npos);
		REQUIRE(archiveHasRootOrRelativeEntry(archive, gltfEntry, uri));
	}
}

void requireObjRefsResolve(mod_archive_t *archive,
                           const char *archiveRel,
                           const char *objEntry) {
	const std::string obj = readArchiveEntryText(archive, objEntry);
	std::istringstream in(obj);
	std::string line;
	while (std::getline(in, line)) {
		std::string text = trimCopy(line);
		if (!startsWith(text, "mtllib ")) {
			continue;
		}
		const std::string mtl = trimCopy(text.substr(strlen("mtllib ")));
		INFO(std::string(archiveRel) + "::" + objEntry + " mtllib=" + mtl);
		REQUIRE(mtl.find("..") == std::string::npos);
		REQUIRE(mtl.find(':') == std::string::npos);
		const std::string mtlEntry = archiveHasEntry(archive, mtl)
			? mtl
			: entryRelativePath(objEntry, mtl);
		REQUIRE(archiveHasEntry(archive, mtlEntry));

		const std::string mtlText = readArchiveEntryText(archive, mtlEntry.c_str());
		std::istringstream mtlIn(mtlText);
		std::string mtlLine;
		while (std::getline(mtlIn, mtlLine)) {
			std::string mtlTrimmed = trimCopy(mtlLine);
			if (!startsWith(mtlTrimmed, "map_")) {
				continue;
			}
			const size_t space = mtlTrimmed.find(' ');
			REQUIRE(space != std::string::npos);
			const std::string texture = trimCopy(mtlTrimmed.substr(space + 1));
			INFO(std::string(archiveRel) + "::" + mtl + " texture=" + texture);
			REQUIRE(texture.find("..") == std::string::npos);
			REQUIRE(texture.find(':') == std::string::npos);
			REQUIRE(archiveHasRootOrRelativeEntry(archive, mtlEntry, texture));
		}
	}
}

std::string readVfsText(const char *path) {
	u32 size = 0;
	void *bytes = modVfsResolveAnyAlloc(path, &size, nullptr, 0);
	REQUIRE(bytes != nullptr);
	std::string text(static_cast<const char *>(bytes), size);
	free(bytes);
	return text;
}

}  /* anonymous namespace */

TEST_CASE("external pdmod contract forbids authored bin and TSV payloads",
          "[modding][pdmod][static][c3809]") {
	std::string spec = readFile("context/designs/modding/external-format-pdmod-pipeline.md");
	REQUIRE(spec.find("Authored mod content and `.pdmod` transport payloads must not contain `.bin` or public `.tsv` files") != std::string::npos);
	REQUIRE(spec.find("Typed `*.pdxxx` files are the preferred content-unit format") != std::string::npos);
	REQUIRE(spec.find("No `.bin` alternate is valid") != std::string::npos);
	REQUIRE(spec.find("public TSV tables are not valid final source") != std::string::npos);

	std::string modmgr = readFile("port/src/modmgr.c");
	std::string modmgrHeader = readFile("port/include/modmgr.h");
	REQUIRE(modmgr.find("modmgrArchiveFindForbiddenPublicPayload") != std::string::npos);
	REQUIRE(modmgr.find("modmgrArchiveEntryHasForbiddenPublicTsvPayload") != std::string::npos);
	REQUIRE(modmgr.find("assetArchiveValidateBytes(nested, nested_size") != std::string::npos);
	REQUIRE(modmgr.find("External-format archives cannot contain %s payloads") != std::string::npos);
	REQUIRE(modmgr.find("modmgrArchiveEntryHasForbiddenBinPayload") != std::string::npos);
	REQUIRE(modmgr.find("modmgrValidateArchiveFile") != std::string::npos);
	REQUIRE(modmgr.find("modmgrInstallArchiveFile") != std::string::npos);
	REQUIRE(modmgr.find("modmgrHasPdmodExtension(archive_path)") != std::string::npos);
	REQUIRE(modmgr.find("modmgrCopyFileAtomic(archive_path, dst)") != std::string::npos);
	REQUIRE(modmgr.find("modmgrPathEqualsNoCase(modmgrPathLeaf(mod->archive_path), safe)") != std::string::npos);
	REQUIRE(modmgrHeader.find("modmgrValidateArchiveFile") != std::string::npos);
	REQUIRE(modmgrHeader.find("modmgrInstallArchiveFile") != std::string::npos);

	std::string packer = readFile("port/src/modpack_pdmod.c");
	REQUIRE(packer.find("\"scenario_archive\", \"geometry_file\", \"geometry\"") != std::string::npos);
	REQUIRE(packer.find("\"music_file\", \"midi_file\", \"track_file\", \"file_path\"") != std::string::npos);
	REQUIRE(packer.find("pathEndsWithNoCase(descriptorRel, \".pdsong\") ? musicKeys : audioKeys") != std::string::npos);
	REQUIRE(packer.find("RUN_FAMILY(\"audio/music\", \"music.ini\", \"music\", musicKeys") != std::string::npos);
	REQUIRE(packer.find("entryHasForbiddenBinPayload(e->entry_name)") != std::string::npos);
	REQUIRE(packer.find("entryHasForbiddenPublicTsvPayload(e->entry_name)") != std::string::npos);
	REQUIRE(packer.find("validateNoForbiddenPublicPayloadsRecurse(srcFolder") != std::string::npos);
	REQUIRE(packer.find("Authored .bin files are not allowed in mod content or .pdmod transport archives") != std::string::npos);
	REQUIRE(packer.find("Public .tsv files are not allowed in mod content or .pdmod transport archives") != std::string::npos);
	REQUIRE(packer.find("points at forbidden public .tsv payload") != std::string::npos);
}

TEST_CASE("pdmod packers reject public TSV transport payloads",
          "[modding][pdmod][c3844][s110]") {
	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempDir folder{
		std::filesystem::temp_directory_path()
			/ ("pd2-pdmod-tsv-folder-" + std::to_string(stamp))
	};
	REQUIRE(std::filesystem::create_directories(folder.path));
	{
		std::ofstream manifest(folder.path / "mod.json", std::ios::binary);
		manifest << "{ \"id\": \"mod_tsv_reject\", \"name\": \"TSV Reject\", \"version\": \"1.0.0\" }\n";
	}
	{
		std::ofstream stale(folder.path / "layout.tsv", std::ios::binary);
		stale << "row\tvalue\n0\tlegacy\n";
	}
	TempArchive folderOut{
		std::filesystem::temp_directory_path()
			/ ("pd2-pdmod-tsv-folder-" + std::to_string(stamp) + ".pdmod")
	};
	REQUIRE(modpackPdmodFromFolder(folder.path.string().c_str(),
		folderOut.path.string().c_str()) == MODPACK_PDMOD_ERR_LAYOUT);
	REQUIRE(std::string(modpackPdmodLastError()).find("Public .tsv files are not allowed") !=
		std::string::npos);

	const char manifest[] =
		"{ \"id\": \"mod_tsv_reject_single\", \"name\": \"TSV Reject\", \"version\": \"1.0.0\" }\n";
	const char tsv[] = "row\tvalue\n0\tlegacy\n";
	modpack_entry_t entries[] = {
		{ "tables/layout.tsv", tsv, static_cast<u32>(strlen(tsv)) },
	};
	TempArchive singleOut{
		std::filesystem::temp_directory_path()
			/ ("pd2-pdmod-tsv-single-" + std::to_string(stamp) + ".pdmod")
	};
	REQUIRE(modpackPdmodWriteSingle(singleOut.path.string().c_str(),
		manifest, static_cast<u32>(strlen(manifest)), entries, 1) ==
		MODPACK_PDMOD_ERR_LAYOUT);
	REQUIRE(std::string(modpackPdmodLastError()).find("Public .tsv files are not allowed") !=
		std::string::npos);
}

TEST_CASE("production pdmod packer round-trips the all-family modder example",
          "[modding][pdmod][c3844][s110]") {
	const std::filesystem::path root =
		"examples/modding/typed-pdxxx-basic";
	REQUIRE(std::filesystem::is_directory(root));

	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive out{
		std::filesystem::temp_directory_path()
			/ ("pd2-all-family-production-pack-" + std::to_string(stamp) + ".pdmod")
	};
	const s32 packResult = modpackPdmodFromFolder(root.string().c_str(),
		out.path.string().c_str());
	INFO(modpackPdmodLastError());
	REQUIRE(packResult == MODPACK_PDMOD_OK);
	REQUIRE(std::filesystem::is_regular_file(out.path));

	OpenArchive opened;
	opened.archive = modArchiveOpen(out.path.string().c_str());
	REQUIRE(opened.archive != nullptr);
	REQUIRE(archiveHasEntry(opened.archive, "mod.json"));

	u32 archiveCount = 0;
	for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
		if (!entry.is_regular_file() || !isTypedPdxxxPath(entry.path())) {
			continue;
		}
		const std::string rel =
			std::filesystem::relative(entry.path(), root).generic_string();
		INFO(rel);
		REQUIRE(archiveHasEntry(opened.archive, rel));
		REQUIRE(readArchiveEntryBytes(opened.archive, rel) ==
			readFile(entry.path().generic_string().c_str()));
		archiveCount++;
	}

	REQUIRE(archiveCount >= 28);
}

TEST_CASE("folder to pdmod packer validates layout and generates INI templates",
          "[modding][pdmod][static][c3809]") {
	std::string header = readFile("port/include/modpack_pdmod.h");
	REQUIRE(header.find("MODPACK_PDMOD_ERR_LAYOUT") != std::string::npos);
	REQUIRE(header.find("MODPACK_PDMOD_ERR_TEMPLATE") != std::string::npos);
	REQUIRE(header.find("modpackPdmodLastError") != std::string::npos);

	std::string packer = readFile("port/src/modpack_pdmod.c");
	REQUIRE(packer.find("validateExternalFolderLayout") != std::string::npos);
	REQUIRE(packer.find("r = validateExternalFolderLayout(src_folder, out_path)") != std::string::npos);
	REQUIRE(packer.find("modArchiveBegin(out_path)") != std::string::npos);
	REQUIRE(packer.find("validateTypedPdDescriptorsRecurse(srcFolder") != std::string::npos);
	REQUIRE(packer.find("assetArchivePathIsTyped(childRel)") != std::string::npos);
	REQUIRE(packer.find("assetArchiveValidateFile(descriptorAbs, ASSET_ARCHIVE_VALIDATE_RELEASE") != std::string::npos);
	REQUIRE(packer.find("MODPACK.PDMOD: validated %u typed .pd* content descriptor(s) before packing") != std::string::npos);
	REQUIRE(packer.find("processCanonicalFamily(srcFolder") != std::string::npos);
	REQUIRE(packer.find("writeTemplateIfMissing") != std::string::npos);
	REQUIRE(packer.find("ensureParentDirsLocal") != std::string::npos);
	REQUIRE(packer.find("modiniTemplateForKind(kind)") != std::string::npos);
	REQUIRE(packer.find("pads.ini - generated external map pad/spawn template") != std::string::npos);
	REQUIRE(packer.find("setup.ini - generated external map setup template") != std::string::npos);
	REQUIRE(packer.find("pad_id\\troom_ref") == std::string::npos);
	REQUIRE(packer.find("pd2.scenario.pads.v1") != std::string::npos);
	REQUIRE(packer.find("pd2.scenario.setup.fields.v1") != std::string::npos);
	REQUIRE(packer.find("pd2.scenario.ai.lists.v1") != std::string::npos);
	REQUIRE(packer.find("pd2.scenario.portals.v1") != std::string::npos);
	REQUIRE(packer.find("pd2.scenario.waypoints.v1") != std::string::npos);
	REQUIRE(packer.find("pd2.scenario.waygroups.v1") != std::string::npos);
	REQUIRE(packer.find("pd2.scenario.covers.v1") != std::string::npos);
	REQUIRE(packer.find("pd2.scenario.paths.v1") != std::string::npos);
	REQUIRE(packer.find("\"pads.json\"") != std::string::npos);
	REQUIRE(packer.find("\"portals.json\"") != std::string::npos);
	REQUIRE(packer.find("\"spawns.json\"") != std::string::npos);
	REQUIRE(packer.find("\"volumes.json\"") != std::string::npos);
	REQUIRE(packer.find("\"objects.json\"") != std::string::npos);
	REQUIRE(packer.find("\"setup.fields.json\"") != std::string::npos);
	REQUIRE(packer.find("\"ai/ailists.json\"") != std::string::npos);
	REQUIRE(packer.find("\"objectives.json\"") != std::string::npos);
	REQUIRE(packer.find("\"navigation/waypoints.json\"") != std::string::npos);
	REQUIRE(packer.find("\"navigation/waygroups.json\"") != std::string::npos);
	REQUIRE(packer.find("\"navigation/covers.json\"") != std::string::npos);
	REQUIRE(packer.find("\"navigation/paths.json\"") != std::string::npos);
	REQUIRE(packer.find("waypoints_file = navigation/waypoints.json") != std::string::npos);
	REQUIRE(packer.find("waygroups_file = navigation/waygroups.json") != std::string::npos);
	REQUIRE(packer.find("covers_file = navigation/covers.json") != std::string::npos);
	REQUIRE(packer.find("paths_file = navigation/paths.json") != std::string::npos);
	REQUIRE(packer.find("generated_cache = _meta/generated-navmesh.json") != std::string::npos);
	REQUIRE(packer.find("\"level.graph.json\"") != std::string::npos);
	REQUIRE(packer.find("weapons\", \"weapon.ini\", \"weapon\"") != std::string::npos);
	REQUIRE(packer.find("characters/heads\", \"head.ini\", \"head\"") != std::string::npos);
	REQUIRE(packer.find("maps\", \"arena.ini\", \"arena\"") != std::string::npos);
	REQUIRE(packer.find("\"blender_scene_file\"") != std::string::npos);
	REQUIRE(packer.find("\"visual_scene_file\"") != std::string::npos);
	REQUIRE(packer.find("\"visual_material_file\"") != std::string::npos);
	REQUIRE(packer.find("\"visual_materials_file\"") != std::string::npos);
	REQUIRE(packer.find("\"texture_manifest_file\"") != std::string::npos);
	REQUIRE(packer.find("\"visual_source_file\"") != std::string::npos);
	REQUIRE(packer.find("audio/music\", \"music.ini\", \"music\"") != std::string::npos);
	REQUIRE(packer.find("animations/character\", \"animation.ini\", \"animation\"") != std::string::npos);
	REQUIRE(packer.find("references missing source file") != std::string::npos);
	REQUIRE(packer.find("uses unsafe source path") != std::string::npos);

	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("waypoints_file = navigation/waypoints.json") != std::string::npos);
	REQUIRE(scanner.find("waygroups_file = navigation/waygroups.json") != std::string::npos);
	REQUIRE(scanner.find("covers_file = navigation/covers.json") != std::string::npos);
	REQUIRE(scanner.find("paths_file = navigation/paths.json") != std::string::npos);
	REQUIRE(scanner.find("voice.ini - external voice metadata") != std::string::npos);
	REQUIRE(scanner.find("audio_category = voice") != std::string::npos);
	REQUIRE(scanner.find("music.ini - external music metadata") != std::string::npos);
	REQUIRE(scanner.find("audio_category = music") != std::string::npos);
	REQUIRE(scanner.find("file_path = track.ogg") != std::string::npos);

	std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(hub.find("modpackPdmodLastError()") != std::string::npos);
	REQUIRE(hub.find("MODPACK_PDMOD_ERR_LAYOUT") != std::string::npos);
	REQUIRE(hub.find("MODPACK_PDMOD_ERR_TEMPLATE") != std::string::npos);
	REQUIRE(hub.find("standard files + INI/JSON, no .bin/.tsv") != std::string::npos);
}

TEST_CASE("archive mods scan INI descriptors through the shared catalog scanner",
          "[modding][pdmod][static][c3809]") {
	std::string header = readFile("port/include/assetcatalog_scanner.h");
	REQUIRE(header.find("iniParseBuffer") != std::string::npos);
	REQUIRE(header.find("iniWriteBuffer") != std::string::npos);
	REQUIRE(header.find("iniWriteFile") != std::string::npos);
	REQUIRE(header.find("modiniTemplateForKind") != std::string::npos);
	REQUIRE(header.find("assetCatalogScanExternalLayoutFolder") != std::string::npos);
	REQUIRE(header.find("assetCatalogScanComponentsFromArchive") != std::string::npos);

	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("iniParseBuffer") != std::string::npos);
	REQUIRE(scanner.find("iniWriteBuffer") != std::string::npos);
	/* The parse loop is ini_parse.c (no log, no catalog; linked into the tests);
	 * the scanner keeps the logging wrapper. The first section names the type. */
	REQUIRE(scanner.find("iniParseText(data, len, out, why, sizeof(why))") != std::string::npos);
	std::string iniParser = readFile("port/src/ini_parse.c");
	REQUIRE(iniParser.find("if (out->type[0] == '\\0')") != std::string::npos);
	REQUIRE(iniParser.find("is not a comment, a section or key = value") != std::string::npos);
	REQUIRE(scanner.find("modArchiveExtractAlloc") != std::string::npos);
	REQUIRE(scanner.find("archiveIniIsDescriptor") != std::string::npos);
	REQUIRE(scanner.find("archiveEntryIsDescriptor") != std::string::npos);
	REQUIRE(scanner.find("typedPdContentTypeForPath(entry_name)") != std::string::npos);
	REQUIRE(scanner.find("typedPdDescriptorComponentDir(entry_name") != std::string::npos);
	REQUIRE(scanner.find("typedPdArchiveDescriptorLeaf(entry_name)") != std::string::npos);
	REQUIRE(scanner.find("assetArchiveExtractDescriptorMemAlloc") != std::string::npos);
	REQUIRE(scanner.find("assetPathJoinChecked(entry_ref, FS_MAXPATH, archive_path, \"::\",") != std::string::npos);
	REQUIRE(scanner.find("qualifyTypedArchiveSourcePaths(&ini, entry_ref)") != std::string::npos);
	REQUIRE(scanner.find("qualifyTypedArchiveSourcePaths(&ini, entry_name)") == std::string::npos);
	REQUIRE(scanner.find("\"theme_file\"") != std::string::npos);
	REQUIRE(scanner.find("qualifyArchiveIniPaths") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"catalog_id\", iniGet(ini, \"id\"") != std::string::npos);
	REQUIRE(scanner.find("\"blender_scene_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"visual_scene_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"visual_material_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"visual_materials_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"texture_manifest_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"visual_source_file\"") != std::string::npos);

	std::string modmgr = readFile("port/src/modmgr.c");
	const auto mount = modmgr.find("modVfsMount(mod->id, mod->archive_handle)");
	const auto scan = modmgr.find("assetCatalogScanComponentsFromArchive(mod->id, mod->archive_handle)");
	REQUIRE(mount != std::string::npos);
	REQUIRE(scan != std::string::npos);
	REQUIRE(mount < scan);
}

TEST_CASE("typed archive source path qualification covers strict archive members",
          "[modding][pdxxx][c3842][source][static]") {
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	const std::string pathContract = readFile("port/include/asset_path_contract.h");
	// Inspect each function, independent of checkout newlines or nearby comments.
	const auto typedBlock = sourceFunctionBlock(scanner,
		"static s32 qualifyTypedArchiveSourcePaths");
	const auto folderBlock = sourceFunctionBlock(scanner,
		"static s32 qualifyIniSourcePaths");
	const auto archiveBlock = sourceFunctionBlock(scanner,
		"static s32 qualifyArchiveIniPaths");
	const char *requiredKeys[] = {
		"\"scenario_archive\"",
		"\"mesh_archive\"",
		"\"hand_archive\"",
		"\"behavior_graph\"",
		"\"primary_graph\"",
		"\"secondary_graph\"",
		"\"settings_file\"",
		"\"variables_file\"",
		"\"shared_context_file\"",
		"\"presentation_file\"",
		"\"primary_projectile_archive\"",
		"\"deployed_entity_archive\"",
		"\"fire_sound_archive\"",
		"\"idle_animation_archive\"",
		"\"reticle_archive\"",
		"\"layout_file\"",
		"\"nineslice_file\"",
		"\"ui_archive\"",
		"\"font_archive\"",
		"\"audio_archive\"",
		"\"music_archive\"",
		"\"effect_archive\"",
		"\"waypoints_file\"",
		"\"waygroups_file\"",
		"\"covers_file\"",
		"\"paths_file\"",
	};

	for (const char *key : requiredKeys) {
		REQUIRE(pathContract.find(key) != std::string::npos);
	}
	REQUIRE(typedBlock.find("g_AssetPathSourceKeys") != std::string::npos);
	REQUIRE(folderBlock.find("g_AssetPathSourceKeys") != std::string::npos);
	REQUIRE(archiveBlock.find("g_AssetPathSourceKeys") != std::string::npos);
	REQUIRE(typedBlock.find("qualifyTypedArchiveSourcePath") != std::string::npos);
	REQUIRE(folderBlock.find("qualifyIniSourcePath") != std::string::npos);
	REQUIRE(archiveBlock.find("qualifyArchiveIniPath") != std::string::npos);
}

TEST_CASE("folder and archive fixtures use the same external descriptor layout",
          "[modding][pdmod][static][c3809]") {
	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("assetCatalogScanExternalLayoutFolder") != std::string::npos);
	REQUIRE(scanner.find("scanTypedPdDescriptorsRecurse(mod_dir") != std::string::npos);
	REQUIRE(scanner.find("registerTypedPdDescriptorFile") != std::string::npos);
	REQUIRE(scanner.find("{ \"maps\", \"arena.ini\", ASSET_ARENA }") != std::string::npos);
	REQUIRE(scanner.find("{ \"characters/heads\", \"head.ini\", ASSET_HEAD }") != std::string::npos);
	REQUIRE(scanner.find("{ \"animations/weapon\", \"animation.ini\", ASSET_ANIMATION }") != std::string::npos);
	REQUIRE(scanner.find("{ \"animations/character\", \"animation.ini\", ASSET_ANIMATION }") != std::string::npos);
	REQUIRE(scanner.find("specs[i].relative_dir, specs[i].leaf, specs[i].expected") != std::string::npos);
	REQUIRE(scanner.find("registerComponentIniFile(component_dir, ini_path") != std::string::npos);

	std::string modmgr = readFile("port/src/modmgr.c");
	const auto modjson = modmgr.find("modmgrRegisterModJsonContent(mod)");
	const auto folderScan = modmgr.find("assetCatalogScanExternalLayoutFolder(mod->id, mod->dirpath)");
	REQUIRE(modjson != std::string::npos);
	REQUIRE(folderScan != std::string::npos);
	REQUIRE(folderScan < modjson);
	REQUIRE(modmgr.find("if (scan_result < 0)", folderScan) != std::string::npos);
	REQUIRE(modmgr.find("modmgrDirHasDirectManifest") != std::string::npos);

	std::string folderManifest = readFile("tests/fixtures/modpipe/external-folder/mod.json");
	std::string folderArena = readFile("tests/fixtures/modpipe/external-folder/maps/tri_arena/arena.ini");
	std::string folderObj = readFile("tests/fixtures/modpipe/external-folder/maps/tri_arena/geometry.obj");
	std::string folderHead = readFile("tests/fixtures/modpipe/external-folder/characters/heads/tri_head/head.ini");
	std::string folderGltf = readFile("tests/fixtures/modpipe/external-folder/characters/heads/tri_head/model.gltf");
	std::string folderAnim = readFile("tests/fixtures/modpipe/external-folder/animations/weapon/idle/animation.ini");
	std::string folderCharacterAnim = readFile("tests/fixtures/modpipe/external-folder/animations/character/idle/animation.ini");
	std::string folderCharacterAnimGltf = readFile("tests/fixtures/modpipe/external-folder/animations/character/idle/animation.gltf");
	std::string folderSkeletalAnim = readFile("tests/fixtures/modpipe/external-folder/animations/character/skeletal/animation.ini");
	std::string folderSkeletalGltf = readFile("tests/fixtures/modpipe/external-folder/animations/character/skeletal/animation.gltf");

	REQUIRE(folderManifest.find("\"id\": \"fixture_external_folder\"") != std::string::npos);
	REQUIRE(folderArena.find("geometry_file = geometry.obj") != std::string::npos);
	REQUIRE(folderArena.find("pads_file = pads.ini") != std::string::npos);
	REQUIRE(folderObj.find("f 1 2 3") != std::string::npos);
	REQUIRE(folderHead.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(folderGltf.find("data:application/octet-stream;base64,") != std::string::npos);
	REQUIRE(folderGltf.find("\"meshes\"") != std::string::npos);
	REQUIRE(folderAnim.find("animation_file = animation.gltf") != std::string::npos);
	REQUIRE(folderAnim.find("catalog_id = fixture:weapon_idle") != std::string::npos);
	REQUIRE(folderAnim.find("category = weapon_animation") != std::string::npos);
	REQUIRE(folderCharacterAnim.find("catalog_id = fixture:character_idle") != std::string::npos);
	REQUIRE(folderCharacterAnim.find("category = character_animation") != std::string::npos);
	REQUIRE(folderCharacterAnim.find("target_body = fixture:tri_body") != std::string::npos);
	REQUIRE(folderCharacterAnimGltf.find("\"animations\"") != std::string::npos);
	REQUIRE(folderSkeletalAnim.find("catalog_id = fixture:character_skeletal") != std::string::npos);
	REQUIRE(folderSkeletalGltf.find("\"channels\"") != std::string::npos);
	REQUIRE(folderSkeletalGltf.find("\"path\": \"translation\"") != std::string::npos);

	std::string archiveManifest = readFile("tests/fixtures/modpipe/external-archive-entries/mod.json");
	std::string archiveArena = readFile("tests/fixtures/modpipe/external-archive-entries/maps/tri_arena/arena.ini");
	std::string archiveObj = readFile("tests/fixtures/modpipe/external-archive-entries/maps/tri_arena/geometry.obj");
	std::string archivePads = readFile("tests/fixtures/modpipe/external-archive-entries/maps/tri_arena/pads.ini");
	std::string archiveSetup = readFile("tests/fixtures/modpipe/external-archive-entries/maps/tri_arena/setup.ini");
	std::string archiveHead = readFile("tests/fixtures/modpipe/external-archive-entries/characters/heads/tri_head/head.ini");
	std::string archiveGltf = readFile("tests/fixtures/modpipe/external-archive-entries/characters/heads/tri_head/model.gltf");
	std::string archiveWeaponAnim = readFile("tests/fixtures/modpipe/external-archive-entries/animations/weapon/idle/animation.ini");
	std::string archiveCharacterAnim = readFile("tests/fixtures/modpipe/external-archive-entries/animations/character/idle/animation.ini");
	std::string archiveSkeletalAnim = readFile("tests/fixtures/modpipe/external-archive-entries/animations/character/skeletal/animation.ini");
	std::string archiveSkeletalGltf = readFile("tests/fixtures/modpipe/external-archive-entries/animations/character/skeletal/animation.gltf");

	REQUIRE(archiveManifest.find("\"id\": \"fixture_external_archive\"") != std::string::npos);
	REQUIRE(archiveArena.find("geometry_file = geometry.obj") != std::string::npos);
	REQUIRE(archiveObj.find("f 1 2 3") != std::string::npos);
	REQUIRE(archivePads.find("default_spawn = 0,0,0") != std::string::npos);
	REQUIRE(archiveSetup.find("props = none") != std::string::npos);
	REQUIRE(archiveHead.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(archiveGltf.find("data:application/octet-stream;base64,") != std::string::npos);
	REQUIRE(archiveWeaponAnim.find("catalog_id = fixture:weapon_idle") != std::string::npos);
	REQUIRE(archiveWeaponAnim.find("category = weapon_animation") != std::string::npos);
	REQUIRE(archiveCharacterAnim.find("catalog_id = fixture:character_idle") != std::string::npos);
	REQUIRE(archiveCharacterAnim.find("category = character_animation") != std::string::npos);
	REQUIRE(archiveSkeletalAnim.find("catalog_id = fixture:character_skeletal") != std::string::npos);
	REQUIRE(archiveSkeletalGltf.find("\"channels\"") != std::string::npos);
	REQUIRE(archiveSkeletalGltf.find("\"path\": \"translation\"") != std::string::npos);

	REQUIRE(folderManifest.find(".bin") == std::string::npos);
	REQUIRE(folderArena.find(".bin") == std::string::npos);
	REQUIRE(folderObj.find(".bin") == std::string::npos);
	REQUIRE(folderHead.find(".bin") == std::string::npos);
	REQUIRE(folderGltf.find(".bin") == std::string::npos);
	REQUIRE(folderAnim.find(".bin") == std::string::npos);
	REQUIRE(folderCharacterAnim.find(".bin") == std::string::npos);
	REQUIRE(folderCharacterAnimGltf.find(".bin") == std::string::npos);
	REQUIRE(folderSkeletalAnim.find(".bin") == std::string::npos);
	REQUIRE(folderSkeletalGltf.find(".bin") == std::string::npos);
	REQUIRE(archiveManifest.find(".bin") == std::string::npos);
	REQUIRE(archiveArena.find(".bin") == std::string::npos);
	REQUIRE(archiveObj.find(".bin") == std::string::npos);
	REQUIRE(archivePads.find(".bin") == std::string::npos);
	REQUIRE(archiveSetup.find(".bin") == std::string::npos);
	REQUIRE(archiveHead.find(".bin") == std::string::npos);
	REQUIRE(archiveGltf.find(".bin") == std::string::npos);
	REQUIRE(archiveWeaponAnim.find(".bin") == std::string::npos);
	REQUIRE(archiveCharacterAnim.find(".bin") == std::string::npos);
	REQUIRE(archiveSkeletalAnim.find(".bin") == std::string::npos);
	REQUIRE(archiveSkeletalGltf.find(".bin") == std::string::npos);
}

TEST_CASE("session package admission propagates scan rollback and exact nested sharing",
		"[modding][network][pdxxx][b1051][b1052][T-ASSETS-030]") {
	const std::string modmgr = readFile("port/src/modmgr.c");
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");

	REQUIRE(modmgr.find("static bool modmgrLoadMod(modinfo_t *mod)") !=
		std::string::npos);
	REQUIRE(modmgr.find("if (!modmgrLoadMod(mod) || !mod->loaded)") !=
		std::string::npos);
	REQUIRE(modmgr.find("failed transactional catalog admission") !=
		std::string::npos);
	REQUIRE(modmgr.find("mod->loaded = true", modmgr.find("scan_result < 0")) !=
		std::string::npos);

	REQUIRE(scanner.find("nestedContentMatchesExisting(existing, nested,") !=
		std::string::npos);
	REQUIRE(scanner.find("nestedContentMatchesExisting(existing, p->bytes,") !=
		std::string::npos);
	REQUIRE(scanner.find("!strstr(existing->dirpath, \"::\")") ==
		std::string::npos);
}

TEST_CASE("typed pd content fixture keeps pdxxx files as content units",
          "[modding][pdmod][static][c3809]") {
	std::string manifest = readFile("tests/fixtures/modpipe/pdxxx-content/mod.json");
	std::string head = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/heads/tri_head.pdhead",
		"head.ini");
	std::string headGltf = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/heads/tri_head.pdhead",
		"model.gltf");
	std::string arena = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/arenas/tri_arena.pdarena",
		"arena.ini");
	std::string geometry = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/arenas/tri_arena.pdarena",
		"geometry.obj");
	std::string pads = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/arenas/tri_arena.pdarena",
		"pads.ini");
	std::string setup = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/arenas/tri_arena.pdarena",
		"setup.ini");
	std::string weaponAnim = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/animations/weapon_idle.pdanim",
		"animation.ini");
	std::string weaponGltf = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/animations/weapon_idle.pdanim",
		"animation.gltf");
	std::string skeletalAnim = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/animations/character_skeletal.pdanim",
		"animation.ini");
	std::string skeletalGltf = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/animations/character_skeletal.pdanim",
		"animation.gltf");

	REQUIRE(manifest.find("\"id\": \"fixture_pdxxx_content\"") != std::string::npos);
	{
		OpenArchive opened;
		opened.archive = modArchiveOpen(
			"tests/fixtures/modpipe/pdxxx-content/heads/tri_head.pdhead");
		REQUIRE(opened.archive != nullptr);
	}
	REQUIRE(head.find("; tri_head.pdhead") != std::string::npos);
	REQUIRE(head.find("[head]") != std::string::npos);
	REQUIRE(head.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(headGltf.find("\"meshes\"") != std::string::npos);
	REQUIRE(arena.find("; tri_arena.pdarena") != std::string::npos);
	REQUIRE(arena.find("[arena]") != std::string::npos);
	REQUIRE(arena.find("geometry_file = geometry.obj") != std::string::npos);
	REQUIRE((geometry.find("f 1 2 3") != std::string::npos
		|| geometry.find("f 1/1 2/2 3/3") != std::string::npos));
	REQUIRE(pads.find("default_spawn = 0,0,0") != std::string::npos);
	REQUIRE(setup.find("props = none") != std::string::npos);
	REQUIRE(weaponAnim.find("catalog_id = fixture:weapon_idle") != std::string::npos);
	REQUIRE(weaponAnim.find("category = weapon_animation") != std::string::npos);
	REQUIRE(weaponGltf.find("\"animations\"") != std::string::npos);
	REQUIRE(skeletalAnim.find("catalog_id = fixture:character_skeletal") != std::string::npos);
	REQUIRE(skeletalAnim.find("category = character_animation") != std::string::npos);
	REQUIRE(skeletalGltf.find("\"channels\"") != std::string::npos);

	const std::string *texts[] = {
		&manifest, &head, &headGltf, &arena, &geometry, &pads, &setup,
		&weaponAnim, &weaponGltf, &skeletalAnim, &skeletalGltf
	};
	for (const std::string *text : texts) {
		REQUIRE(text->find(".bin") == std::string::npos);
	}
	REQUIRE_FALSE(std::filesystem::exists(
		"tests/fixtures/modpipe/pdxxx-content/heads/tri_head/model.gltf"));
	REQUIRE_FALSE(std::filesystem::exists(
		"tests/fixtures/modpipe/pdxxx-content/arenas/tri_arena/geometry.obj"));
}

TEST_CASE("external archive scanner recognizes canonical descriptor families",
          "[modding][pdmod][static][c3809]") {
	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	std::string policy = readFile("port/src/asset_archive_policy.c");
	REQUIRE(scanner.find("assetArchiveDescriptorForPath(path)") != std::string::npos);
	REQUIRE(scanner.find("assetArchiveTypeForPath(path)") != std::string::npos);
	REQUIRE(scanner.find("if (strcmp(section, \"sfx\") == 0)          return ASSET_AUDIO;") != std::string::npos);
	REQUIRE(scanner.find("if (strcmp(section, \"voice\") == 0)        return ASSET_AUDIO;") != std::string::npos);
	REQUIRE(scanner.find("if (strcmp(section, \"music\") == 0)        return ASSET_AUDIO;") != std::string::npos);
	REQUIRE(policy.find("\"weapon.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"head.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"body.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"arena.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"scenario.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"sound.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"ui.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"font.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"lang.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"animation.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"mesh.ini\"") != std::string::npos);
	REQUIRE(policy.find("\".pdhead\"") != std::string::npos);
	REQUIRE(policy.find("\".pdarena\"") != std::string::npos);
	REQUIRE(policy.find("\".pdanim\"") != std::string::npos);
	REQUIRE(policy.find("\".pdmesh\"") != std::string::npos);

	struct TypedFamily {
		const char *path;
		const char *descriptor;
		asset_type_e type;
	};
	const TypedFamily families[] = {
		{ "weapons/x.pdweapon", "weapon.ini", ASSET_WEAPON },
		{ "projectiles/x.pdprojectile", "projectile.ini", ASSET_PROJECTILE },
		{ "entities/x.pdentity", "entity.ini", ASSET_ENTITY },
		{ "materials/x.pdmaterial", "material.ini", ASSET_MATERIAL },
		{ "textures/x.pdtexture", "texture.ini", ASSET_TEXTURE },
		{ "characters/x.pdcharacter", "character.ini", ASSET_CHARACTER },
		{ "heads/x.pdhead", "head.ini", ASSET_HEAD },
		{ "bodies/x.pdbody", "body.ini", ASSET_BODY },
		{ "arenas/x.pdarena", "arena.ini", ASSET_ARENA },
		{ "scenarios/x.pdscenario", "scenario.ini", ASSET_SCENARIO },
		{ "meshes/x.pdmesh", "mesh.ini", ASSET_MODEL },
		{ "animations/x.pdanim", "animation.ini", ASSET_ANIMATION },
		{ "audio/x.pdsfx", "sound.ini", ASSET_AUDIO },
		{ "audio/x.pdvoice", "voice.ini", ASSET_AUDIO },
		{ "audio/x.pdsong", "music.ini", ASSET_AUDIO },
		{ "ui/x.pdui", "ui.ini", ASSET_UI },
		{ "fonts/x.pdfont", "font.ini", ASSET_FONT },
		{ "lang/x.pdlang", "lang.ini", ASSET_LANG },
		{ "skins/x.pdskin", "skin.ini", ASSET_SKIN },
		{ "effects/x.pdeffect", "effect.ini", ASSET_EFFECT },
		{ "props/x.pdprop", "prop.ini", ASSET_PROP },
		{ "vehicles/x.pdvehicle", "vehicle.ini", ASSET_VEHICLE },
		{ "missions/x.pdmission", "mission.ini", ASSET_MISSION },
		{ "gamemodes/x.pdgamemode", "gamemode.ini", ASSET_GAMEMODE },
		{ "botprofiles/x.pdbotprofile", "botprofile.ini", ASSET_BOT_PROFILE },
		{ "hud/x.pdhud", "hud.ini", ASSET_HUD },
		{ "themes/x.pdtheme", "theme.ini", ASSET_THEME },
		{ "ai/x.pdai", "ai.ini", ASSET_AI_LISTS },
	};
	for (const TypedFamily &family : families) {
		INFO(family.path);
		REQUIRE(assetArchivePathIsTyped(family.path) == 1);
		REQUIRE(std::string(assetArchiveDescriptorForPath(family.path)) == family.descriptor);
		REQUIRE(assetArchiveTypeForPath(family.path) == family.type);
	}
	REQUIRE(assetArchivePathIsTyped("tools/x.pdtool") == 0);
}

TEST_CASE("component enable state persists every user-manageable catalog family",
          "[modding][pdmod][static][c3809]") {
	std::string modmgr = readFile("port/src/modmgr.c");
	REQUIRE(!modmgr.empty());

	const std::string adapter = readFile("port/src/modmgr_component_catalog.cpp");
	REQUIRE(!adapter.empty());
	const std::string marker = "const asset_type_e manageable[] = {";
	const size_t begin = adapter.find(marker);
	REQUIRE(begin != std::string::npos);
	const size_t end = adapter.find("};", begin);
	REQUIRE(end != std::string::npos);
	const std::string saveList = adapter.substr(begin, end - begin);

	const char *families[] = {
		"ASSET_MAP",
		"ASSET_CHARACTER",
		"ASSET_SKIN",
		"ASSET_BOT_VARIANT",
		"ASSET_WEAPON",
		"ASSET_PROJECTILE",
		"ASSET_ENTITY",
		"ASSET_ARENA",
		"ASSET_BODY",
		"ASSET_HEAD",
		"ASSET_MODEL",
		"ASSET_ANIMATION",
		"ASSET_TEXTURES",
		"ASSET_TEXTURE",
		"ASSET_MATERIAL",
		"ASSET_EFFECT",
		"ASSET_SFX",
		"ASSET_MUSIC",
		"ASSET_AUDIO",
		"ASSET_PROP",
		"ASSET_VEHICLE",
		"ASSET_MISSION",
		"ASSET_GAMEMODE",
		"ASSET_BOT_PROFILE",
		"ASSET_SCENARIO",
		"ASSET_HUD",
		"ASSET_UI",
		"ASSET_FONT",
		"ASSET_LANG",
		"ASSET_THEME",
		"ASSET_AI_LISTS",
		"ASSET_TOOL",
	};
	for (const char *family : families) {
		INFO(family);
		REQUIRE(saveList.find(family) != std::string::npos);
	}

	REQUIRE(modmgr.find("modmgrLoadComponentState") != std::string::npos);
	REQUIRE(adapter.find("assetCatalogSetEnabledChecked(id, 0)") != std::string::npos);
    REQUIRE(adapter.find("assetCatalogIterateByTypeIncludingDisabled") != std::string::npos);
    REQUIRE(modmgr.find("modmgrSaveCatalogComponentState(") != std::string::npos);
    REQUIRE(modmgr.find("modmgrReplayCatalogComponentState(") != std::string::npos);
    const size_t rebuildBegin = modmgr.rfind("static int modmgrRebuildCatalogChecked(");
    REQUIRE(rebuildBegin != std::string::npos);
    const size_t rebuildEnd = modmgr.find("void modmgrSyncCatalogToRegistry", rebuildBegin);
    REQUIRE(rebuildEnd != std::string::npos);
    const auto rebuild = modmgr.substr(rebuildBegin, rebuildEnd - rebuildBegin);
    const auto registration = rebuild.find("modmgrLoadMod(");
    const auto replay = rebuild.find("modmgrLoadComponentStateChecked(");
    REQUIRE(registration != std::string::npos);
    REQUIRE(replay != std::string::npos);
    REQUIRE(registration < replay);
}

TEST_CASE("network catalog info advertises every user-manageable catalog family",
          "[modding][pdmod][static][c3809]") {
	std::string netmsg = readFile("port/src/net/netmsg.c");
	std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(!netmsg.empty());
	REQUIRE(!distrib.empty());

	const std::string marker = "static const asset_type_e s_CatalogInfoTypes[] = {";
	const size_t begin = netmsg.find(marker);
	REQUIRE(begin != std::string::npos);
	const size_t end = netmsg.find("ASSET_NONE  /* sentinel */", begin);
	REQUIRE(end != std::string::npos);
	const std::string collectList = netmsg.substr(begin, end - begin);

	const char *families[] = {
		"ASSET_MAP",
		"ASSET_CHARACTER",
		"ASSET_SKIN",
		"ASSET_BOT_VARIANT",
		"ASSET_WEAPON",
		"ASSET_PROJECTILE",
		"ASSET_ENTITY",
		"ASSET_ARENA",
		"ASSET_BODY",
		"ASSET_HEAD",
		"ASSET_MODEL",
		"ASSET_ANIMATION",
		"ASSET_TEXTURES",
		"ASSET_TEXTURE",
		"ASSET_MATERIAL",
		"ASSET_EFFECT",
		"ASSET_SFX",
		"ASSET_MUSIC",
		"ASSET_AUDIO",
		"ASSET_PROP",
		"ASSET_VEHICLE",
		"ASSET_MISSION",
		"ASSET_GAMEMODE",
		"ASSET_BOT_PROFILE",
		"ASSET_SCENARIO",
		"ASSET_HUD",
		"ASSET_UI",
		"ASSET_FONT",
		"ASSET_LANG",
		"ASSET_THEME",
		"ASSET_TOOL",
	};
	for (const char *family : families) {
		INFO(family);
		REQUIRE(collectList.find(family) != std::string::npos);
	}
	/* global-ai-lists.md: AI lists never travel with a host's mods, so the
	 * advertised (client-fetchable) list leaves the type out. */
	REQUIRE(collectList.find("ASSET_AI_LISTS") == std::string::npos);

	REQUIRE(netmsg.find("netmsgSvcCatalogInfoWriteChunk") != std::string::npos);
	REQUIRE(netmsg.find("batch_offset") != std::string::npos);
	REQUIRE(netmsg.find("total_count") != std::string::npos);
	REQUIRE(netmsg.find("CATALOG_COLLECT_MAX") == std::string::npos);
	REQUIRE(netmsg.find("count > 256") == std::string::npos);
	REQUIRE(netmsg.find("missing_ids[256]") == std::string::npos);
	const std::string queue = readFile("port/src/net/distrib_queue.c");
	const std::string queue_header = readFile("port/include/net/distrib_queue.h");
	REQUIRE(distrib.find("distribQueuePush(&s_Queue, &request)") != std::string::npos);
	REQUIRE(distrib.find("distribQueuePop(&s_Queue, &request)") != std::string::npos);
	REQUIRE(queue.find("realloc(queue->entries, capacity * sizeof(*entries))") != std::string::npos);
	REQUIRE(queue.find("queue->capacity * 2") != std::string::npos);
	REQUIRE(queue_header.find("DISTRIB_QUEUE_MAX_PENDING 32768") != std::string::npos);
	REQUIRE(queue_header.find("DISTRIB_QUEUE_MAX_PER_CONNECTION 8192") != std::string::npos);
	REQUIRE(distrib.find("distribSendEnd(cl, id, 0)") != std::string::npos);
	REQUIRE(distrib.find("transfer queue full, dropping") == std::string::npos);
	REQUIRE(distrib.find("missing_ids[256]") == std::string::npos);
}

TEST_CASE("mod pack and authoring UIs enumerate every user-manageable catalog family",
          "[modding][pdmod][static][c3809]") {
	const char *families[] = {
		"ASSET_MAP",
		"ASSET_CHARACTER",
		"ASSET_SKIN",
		"ASSET_BOT_VARIANT",
		"ASSET_WEAPON",
		"ASSET_PROJECTILE",
		"ASSET_ENTITY",
		"ASSET_ARENA",
		"ASSET_BODY",
		"ASSET_HEAD",
		"ASSET_MODEL",
		"ASSET_ANIMATION",
		"ASSET_TEXTURES",
		"ASSET_TEXTURE",
		"ASSET_MATERIAL",
		"ASSET_EFFECT",
		"ASSET_SFX",
		"ASSET_MUSIC",
		"ASSET_AUDIO",
		"ASSET_PROP",
		"ASSET_VEHICLE",
		"ASSET_MISSION",
		"ASSET_GAMEMODE",
		"ASSET_BOT_PROFILE",
		"ASSET_SCENARIO",
		"ASSET_HUD",
		"ASSET_UI",
		"ASSET_FONT",
		"ASSET_LANG",
		"ASSET_THEME",
		"ASSET_AI_LISTS",
		"ASSET_TOOL",
	};

	const std::string modpack = readFile("port/src/modpack.c");
	const size_t modpackTypesBegin =
		modpack.find("static const asset_type_e kTypes[] = {");
	REQUIRE(modpackTypesBegin != std::string::npos);
	const size_t modpackTypesEnd = modpack.find("ASSET_NONE", modpackTypesBegin);
	REQUIRE(modpackTypesEnd != std::string::npos);
	const std::string modpackTypes =
		modpack.substr(modpackTypesBegin, modpackTypesEnd - modpackTypesBegin);
	for (const char *family : families) {
		INFO(std::string("modpack:") + family);
		REQUIRE(modpackTypes.find(family) != std::string::npos);
	}

	const char *descriptorNames[] = {
		"\"arena.ini\"",
		"\"body.ini\"",
		"\"head.ini\"",
		"\"mesh.ini\"",
		"\"model.ini\"",
		"\"animation.ini\"",
	};
	for (const char *descriptor : descriptorNames) {
		INFO(descriptor);
		REQUIRE(modpack.find(descriptor) != std::string::npos);
	}

	const std::string modmgr = readFile("port/fast3d/pdgui_menu_modmgr.cpp");
	const size_t mgrAllBegin =
		modmgr.find("static const asset_type_e s_AllTypes[] = {");
	REQUIRE(mgrAllBegin != std::string::npos);
	const size_t mgrAllEnd = modmgr.find("};", mgrAllBegin);
	REQUIRE(mgrAllEnd != std::string::npos);
	const std::string mgrAll = modmgr.substr(mgrAllBegin, mgrAllEnd - mgrAllBegin);
	const size_t mgrUserBegin =
		modmgr.find("static const asset_type_e userTypes[] = {");
	REQUIRE(mgrUserBegin != std::string::npos);
	const size_t mgrUserEnd = modmgr.find("};", mgrUserBegin);
	REQUIRE(mgrUserEnd != std::string::npos);
	const std::string mgrUser =
		modmgr.substr(mgrUserBegin, mgrUserEnd - mgrUserBegin);
	for (const char *family : families) {
		INFO(std::string("modmgr all:") + family);
		REQUIRE(mgrAll.find(family) != std::string::npos);
		INFO(std::string("modmgr user:") + family);
		REQUIRE(mgrUser.find(family) != std::string::npos);
	}

	const std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	const size_t hubAllBegin =
		hub.find("static const asset_type_e s_AllTypes[] = {");
	REQUIRE(hubAllBegin != std::string::npos);
	const size_t hubAllEnd = hub.find("};", hubAllBegin);
	REQUIRE(hubAllEnd != std::string::npos);
	const std::string hubAll =
		hub.substr(hubAllBegin, hubAllEnd - hubAllBegin);
	for (const char *family : families) {
		INFO(std::string("modhub:") + family);
		REQUIRE(hubAll.find(family) != std::string::npos);
	}
	for (const char *descriptor : descriptorNames) {
		INFO(std::string("hub:") + descriptor);
		REQUIRE(hub.find(descriptor) != std::string::npos);
	}
}

TEST_CASE("global AI list bank is a typed .pdai family that stays on the host",
          "[modding][pdmod][static][global_ailists]") {
	/* context/designs/modding/global-ai-lists.md: appended after ASSET_THEME
	 * so no stored type number moves; the Theater ceiling stays ASSET_THEME. */
	REQUIRE((int)ASSET_AI_LISTS == (int)ASSET_THEME + 1);
	REQUIRE((int)ASSET_TYPE_COUNT == (int)ASSET_AI_LISTS + 1);
	const std::string theater = readFile("port/src/theater.c");
	REQUIRE(theater.find("_Static_assert(THEATER_FORMAT_CATALOG_TYPE_MAX == ASSET_THEME,") !=
		std::string::npos);

	/* Archive policy row. */
	REQUIRE(assetArchivePathIsTyped("ai/base_ai_global.pdai") == 1);
	REQUIRE(std::string(assetArchiveDescriptorForPath("ai/base_ai_global.pdai")) == "ai.ini");
	REQUIRE(assetArchiveTypeForPath("ai/base_ai_global.pdai") == ASSET_AI_LISTS);
	REQUIRE(assetArchiveLegacyDescriptorForPath("ai/base_ai_global.pdai") == nullptr);

	char err[256];
	auto bank = writeTypedArchiveEntries("ai-lists-bank", ".pdai", {
		{ "ai.ini",
		  "[ai_lists]\n"
		  "catalog_id = base:ai_global\n"
		  "ai_lists_file = ai/global_ailists.json\n" },
		{ "ai/global_ailists.json",
		  "{ \"schema\": \"pd2.scenario.ai.lists.v1\", \"rows\": [] }\n" },
		{ "_meta/manifest.json",
		  "{ \"pd_kind\": \"ai_lists\", \"pd_schema_version\": 1, \"id\": \"base:ai_global\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(bank.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) == 0);
	auto noRows = writeTypedArchiveEntries("ai-lists-no-rows", ".pdai", {
		{ "ai.ini",
		  "[ai_lists]\n"
		  "catalog_id = mod:ai_missing_rows\n"
		  "ai_lists_file = ai/global_ailists.json\n" },
	});
	REQUIRE(assetArchiveValidateFile(noRows.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	auto noDescriptor = writeTypedArchiveEntries("ai-lists-no-descriptor", ".pdai", {
		{ "ai/global_ailists.json",
		  "{ \"schema\": \"pd2.scenario.ai.lists.v1\", \"rows\": [] }\n" },
	});
	REQUIRE(assetArchiveValidateFile(noDescriptor.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);

	/* Mod utility contract row: its own component type, utilities like a
	 * language bank. */
	const asset_mod_utility_contract_t *ai =
		assetModUtilityContractForExtension(".pdai");
	REQUIRE(ai != nullptr);
	REQUIRE(ai == assetModUtilityContractForType(ASSET_AI_LISTS));
	REQUIRE(std::string(ai->family) == "ai_lists");
	REQUIRE(std::string(ai->descriptor) == "ai.ini");
	REQUIRE(std::string(ai->hub_tool) == "AI Lists");
	REQUIRE(std::string(ai->cli_noun) == "ai-lists");
	const asset_mod_utility_contract_t *lang =
		assetModUtilityContractForType(ASSET_LANG);
	REQUIRE(lang != nullptr);
	REQUIRE(ai->ops == lang->ops);

	/* Scanner: content folder, section, descriptor specs, .pdmod paths and
	 * the rows member. */
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("if (strcmp(dirname, \"ai\") == 0)          return ASSET_AI_LISTS;") !=
		std::string::npos);
	REQUIRE(scanner.find("if (strcmp(section, \"ai_lists\") == 0)     return ASSET_AI_LISTS;") !=
		std::string::npos);
	// .pdai is the only public unit, as .pdtheme: no loose ai.ini layout.
	REQUIRE(scanner.find("{ \"\", \"ai.ini\", ASSET_AI_LISTS },") == std::string::npos);
	REQUIRE(scanner.find("{ \"ai\", \"ai.ini\", ASSET_AI_LISTS },") == std::string::npos);
	REQUIRE(scanner.find("/* .pdai is the only public AI list unit; loose ai.ini is rejected. */") != std::string::npos);
	REQUIRE(scanner.find("|| strcmp(leaf, \"ai.ini\") == 0") != std::string::npos);
	REQUIRE(scanner.find("if (strcmp(seg0, \"ai\") == 0) return ASSET_AI_LISTS;") !=
		std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"ai_lists_file\", iniGet(ini, \"file_path\", \"\"))") !=
		std::string::npos);

	/* Base walker: .pdai archives in data/<romid>/ai with the fixed public
	 * member. */
	const std::string walker = readFile("port/src/loader_walker_meta.c");
	REQUIRE(walker.find("{ ASSET_AI_LISTS,    \"ai_lists\",   \"ai\",          \".pdai\",") !=
		std::string::npos);
	REQUIRE(walker.find("s_applyAiListsPublicDescriptor(entry, file_path)") !=
		std::string::npos);
	REQUIRE(walker.find("strcmp(lists_member, \"ai/global_ailists.json\") != 0") !=
		std::string::npos);

	/* Catalog lifecycle and runtime binding. */
	const std::string load = readFile("port/src/assetcatalog_load.c");
	REQUIRE(load.find("|| type == ASSET_AI_LISTS;") != std::string::npos);
	const std::string runtime = readFile("port/src/asset_runtime.c");
	REQUIRE(runtime.find("entry->ext.ai_lists.lists_file") != std::string::npos);

	/* Network: AI lists never travel with a host's mods. The client maps no
	 * received ai.ini and the host refuses a typed request for one. */
	const std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("strcmp(ini_name, \"ai.ini\")") == std::string::npos);
	REQUIRE(distrib.find("\"ai.ini\", NULL") == std::string::npos);
	REQUIRE(distrib.find("if (entry->type == ASSET_AI_LISTS) {") != std::string::npos);
	const std::string netmsg = readFile("port/src/net/netmsg.c");
	const size_t infoBegin =
		netmsg.find("static const asset_type_e s_CatalogInfoTypes[] = {");
	REQUIRE(infoBegin != std::string::npos);
	const size_t infoEnd = netmsg.find("};", infoBegin);
	REQUIRE(infoEnd != std::string::npos);
	REQUIRE(netmsg.substr(infoBegin, infoEnd - infoBegin).find("ASSET_AI_LISTS") ==
		std::string::npos);
}

TEST_CASE("legacy pd asset walkers remain registered during external pdmod migration",
          "[modding][pdmod][static][c3809]") {
	std::string common_h = readFile("port/include/loader_walker_common.h");
	std::string common = readFile("port/src/loader_walker_common.c");
	std::string walker = readFile("port/src/loader_walker.c");

	REQUIRE(common_h.find("auto-detecting plain JSON vs ZIP compound") != std::string::npos);
	REQUIRE(common.find("if (strcmp(name + nlen - ext_len, desc->extension) != 0) continue;") != std::string::npos);
	REQUIRE(common.find("bytes[0] == 'P' && bytes[1] == 'K'") != std::string::npos);
	REQUIRE(common.find("assetArchiveFindMetadataEntry") != std::string::npos);

	const char *scanCalls[] = {
		"loaderWalkerScanMeshes(data_root, &kr)",
		"loaderWalkerScanAnimations(data_root, &kr)",
		"loaderWalkerScanWeapons(data_root, &kr)",
		"loaderWalkerScanHeads(data_root, &kr)",
		"loaderWalkerScanBodies(data_root, &kr)",
		"loaderWalkerScanScenarios(data_root, &kr)",
		"loaderWalkerScanArenas(data_root, &kr)",
		"loaderWalkerScanSfx(data_root, &kr)",
		"loaderWalkerScanVoices(data_root, &kr)",
		"loaderWalkerScanSongs(data_root, &kr)",
		"loaderWalkerScanMetadataFamilies(data_root, &mr)",
		"loaderWalkerScanUi(data_root, &kr)",
		"loaderWalkerScanFonts(data_root, &kr)",
		"loaderWalkerScanLangs(data_root, &kr)",
	};
	for (const char *call : scanCalls) {
		INFO(call);
		REQUIRE(walker.find(call) != std::string::npos);
	}

	const std::pair<const char *, const char *> legacyWalkers[] = {
		{ "port/src/loader_walker_weapon.c",   "\"weapon\", \"weapons\", \".pdweapon\"" },
		{ "port/src/loader_walker_head.c",     "\"head\", \"heads\", \".pdhead\"" },
		{ "port/src/loader_walker_body.c",     "\"body\", \"bodies\", \".pdbody\"" },
		{ "port/src/loader_walker_arena.c",    "\"arena\", \"arenas\", \".pdarena\"" },
		{ "port/src/loader_walker_mesh.c",     "\"mesh\", \"meshes\", \".pdmesh\"" },
		{ "port/src/loader_walker_anim.c",     "\"animation\", \"animations\", \".pdanim\"" },
		{ "port/src/loader_walker_sfx.c",      ".kind_str = \"sfx\"" },
		{ "port/src/loader_walker_voice.c",    ".kind_str = \"voice\"" },
		{ "port/src/loader_walker_song.c",     ".kind_str = \"song\"" },
		{ "port/src/loader_walker_ui.c",       "\"ui\", \"ui\", \".pdui\"" },
		{ "port/src/loader_walker_font.c",     "\"font\", \"fonts\", \".pdfont\"" },
		{ "port/src/loader_walker_lang.c",     "\"lang\", \"lang\", \".pdlang\"" },
		{ "port/src/loader_walker_scenario.c", ".kind_str = \"scenario\"" },
	};
	for (const auto &pin : legacyWalkers) {
		INFO(pin.first);
		std::string src = readFile(pin.first);
		REQUIRE(src.find(pin.second) != std::string::npos);
		if (std::string(pin.first) == "port/src/loader_walker_anim.c") {
			const auto registration = src.find("loaderWalkerScanKind(tier_dir, &desc, s_register, &registered)");
			const auto compilation = src.find("loaderWalkerScanKind(tier_dir, &commands, s_compile, &compiled)");
			REQUIRE(registration != std::string::npos);
			REQUIRE(compilation != std::string::npos);
			REQUIRE(registration < compilation);
			REQUIRE(walker.find("loaderWalkerScanSfx(data_root, &kr)") < walker.find("loaderWalkerScanAnimations(data_root, &kr)"));
			REQUIRE(walker.find("loaderWalkerScanVoices(data_root, &kr)") < walker.find("loaderWalkerScanAnimations(data_root, &kr)"));
		} else {
			REQUIRE(src.find("loaderWalkerScanKind(tier_dir, &desc, s_register, out)") != std::string::npos);
		}
	}

	const std::string metaWalker = readFile("port/src/loader_walker_meta.c");
	REQUIRE(metaWalker.find(".pdcharacter") != std::string::npos);
	REQUIRE(metaWalker.find(".pdskin") != std::string::npos);
	REQUIRE(metaWalker.find(".pdprop") != std::string::npos);
	REQUIRE(metaWalker.find(".pdvehicle") != std::string::npos);
	REQUIRE(metaWalker.find(".pdmission") != std::string::npos);
	REQUIRE(metaWalker.find(".pdgamemode") != std::string::npos);
	REQUIRE(metaWalker.find(".pdbotprofile") != std::string::npos);
	REQUIRE(metaWalker.find(".pdhud") != std::string::npos);
	REQUIRE(metaWalker.find(".pdeffect") != std::string::npos);
	REQUIRE(metaWalker.find(".pdmaterial") != std::string::npos);
	REQUIRE(metaWalker.find(".pdtheme") != std::string::npos);
	REQUIRE(metaWalker.find("desc.always_invoke = 1") != std::string::npos);
	REQUIRE(metaWalker.find("assetCatalogGetMutable(id)") != std::string::npos);
	REQUIRE(metaWalker.find("entry->type != type") != std::string::npos);
	REQUIRE(metaWalker.find("loaderWalkerArchiveMemberPath") != std::string::npos);
	REQUIRE(metaWalker.find("catalogSetPrimaryFile") != std::string::npos);
	REQUIRE(metaWalker.find("loaderWalkerMarkBaseArchiveEntry") != std::string::npos);
	REQUIRE(metaWalker.find("s_manifestMemberPath") != std::string::npos);
	REQUIRE(metaWalker.find("s_copyManifestMemberPath") != std::string::npos);
	REQUIRE(metaWalker.find("\"body_archive\"") != std::string::npos);
	REQUIRE(metaWalker.find("\"head_archive\"") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.character.bodyfile") != std::string::npos);
	REQUIRE(metaWalker.find("characterSourceApplyIni(entry, &ini)") != std::string::npos);
	const std::string characterSource = readFile("port/src/character_head_policy.c");
	REQUIRE(characterSource.find("entry->ext.character.headfile") != std::string::npos);
	REQUIRE(characterSource.find("entry->ext.character.portrait_file") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.character.headfile, source_path") == std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.prop.model_file") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.prop.behavior_graph") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.vehicle.model_file") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.vehicle.physics_file") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.vehicle.behavior_graph") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.mission.scenario_archive") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.mission.objectives_file") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.mission.briefing_file") == std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.mission.mission_graph_file") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.hud.texture_file") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.hud.layout_file") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.material.material_file") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.material.texture_archive") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.material.effect_archive") != std::string::npos);
	REQUIRE(metaWalker.find("\"material_file\", \"file_path\", \"texture_archive\", \"effect_archive\"") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.theme.theme_file") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.theme.ui_archive") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.theme.font_archive") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.theme.audio_archive") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.theme.music_archive") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.theme.effect_archive") != std::string::npos);
	REQUIRE(metaWalker.find("\"theme_file\", \"ui_archive\", \"font_archive\", \"audio_archive\", \"music_archive\", NULL") != std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.vehicle.physics_file, source_path") == std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.hud.texture_file, source_path") == std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.material.texture_archive, source_path") == std::string::npos);
	REQUIRE(metaWalker.find("entry->ext.material.effect_archive, source_path") == std::string::npos);
}

TEST_CASE("weapon content pipeline accepts pdweapon only",
          "[modding][pdxxx][weapon][static][c3814]") {
	const std::string removedWeaponExt = std::string(".pd") + "wpn";
	const char *liveFiles[] = {
		"port/src/loader_walker_weapon.c",
		"port/src/assetcatalog_scanner.c",
		"port/src/modmgr.c",
		"port/src/romextract_pdweapon.c",
		"port/include/romextract_pd.h",
	};

	for (const char *path : liveFiles) {
		INFO(path);
		std::string src = readFile(path);
		if (std::string(path) == "port/src/assetcatalog_scanner.c") {
			REQUIRE(src.find("assetArchiveTypeForPath(path)") != std::string::npos);
			REQUIRE(src.find("ASSET_WEAPON") != std::string::npos);
		} else {
			REQUIRE(src.find(".pdweapon") != std::string::npos);
		}
		REQUIRE(src.find(removedWeaponExt) == std::string::npos);
	}

	const std::string policy = readFile("port/src/asset_archive_policy.c");
	const std::string packer = readFile("port/src/modpack_pdmod.c");
	REQUIRE(policy.find(removedWeaponExt) != std::string::npos);
	REQUIRE(packer.find("Deprecated .pdwpn files are not allowed") != std::string::npos);

	const std::string extractor = readFile("port/src/romextract_pdweapon.c");
	REQUIRE(extractor.find("nested_payloads = \" PDWEAPON_NESTED_PAYLOADS_ENTRY") != std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_NESTED_PAYLOADS_ENTRY \"_meta/nested-payloads.json\"") != std::string::npos);
	REQUIRE(extractor.find("runtime.graph.json") == std::string::npos);
	REQUIRE(extractor.find("behavior_graph = \" PDWEAPON_RUNTIME_GRAPH_ENTRY") == std::string::npos);
	REQUIRE(extractor.find("primary_graph = \" PDWEAPON_PRIMARY_GRAPH_ENTRY") != std::string::npos);
	REQUIRE(extractor.find("secondary_graph = \" PDWEAPON_SECONDARY_GRAPH_ENTRY") != std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_PRIMARY_GRAPH_ENTRY \"behavior/primary.graph.json\"") != std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_SECONDARY_GRAPH_ENTRY \"behavior/secondary.graph.json\"") != std::string::npos);
	REQUIRE(extractor.find("\"shared_context\"") != std::string::npos);
	REQUIRE(extractor.find("\"subgraph\"") != std::string::npos);
	REQUIRE(extractor.find("primary_trigger") != std::string::npos);
	REQUIRE(extractor.find("secondary_trigger") != std::string::npos);
	REQUIRE(extractor.find("event.trigger_pressed") != std::string::npos);
	REQUIRE(extractor.find("event.trigger_held") != std::string::npos);
	REQUIRE(extractor.find("event.trigger_released") != std::string::npos);
	REQUIRE(extractor.find("damage_credit_player") != std::string::npos);
	REQUIRE(extractor.find("base_legacy_adapter") == std::string::npos);
	REQUIRE(extractor.find("spawn.fired_projectile") != std::string::npos);
	REQUIRE(extractor.find("spawn.thrown_physical") != std::string::npos);
	REQUIRE(extractor.find("fire.hitscan") != std::string::npos);
	REQUIRE(extractor.find("fire.auto_cadence") != std::string::npos);
	REQUIRE(extractor.find("fire.burst") != std::string::npos);
	REQUIRE(extractor.find("fire.charge_release") != std::string::npos);
	REQUIRE(extractor.find("fire.beam_tick") != std::string::npos);
	REQUIRE(extractor.find("special.remote_detonator") != std::string::npos);
	REQUIRE(extractor.find("device.activate") != std::string::npos);
	REQUIRE(extractor.find("projectile.ini") != std::string::npos);
	REQUIRE(extractor.find("entity.ini") != std::string::npos);
	REQUIRE(extractor.find("bindings.json") != std::string::npos);
	REQUIRE(extractor.find("model_catalog_id = %s") != std::string::npos);
	REQUIRE(extractor.find("model_ref = %s") == std::string::npos);
	REQUIRE(extractor.find("projectile_model_catalog_id") != std::string::npos);
	REQUIRE(extractor.find("projectile_model_ref") == std::string::npos);
	REQUIRE(extractor.find("shoot_sound_catalog_id") != std::string::npos);
	REQUIRE(extractor.find("special_sound_catalog_id") != std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_DEPENDENCY_CLOSURE_MARKER \"embedded.v22\"") != std::string::npos);
	REQUIRE(extractor.find("dependency_closure = \" PDWEAPON_DEPENDENCY_CLOSURE_MARKER") != std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_FAST_CACHE_KIND \"pdweapon_embedded_v14_clean_public_b943fields_projref_fuseprogram_contactrules_devicerules_sentryrules_armrules_heldprograms_heldtriggers_heldshots_heldreloads_heldcharges_flightrules\"") != std::string::npos);
	REQUIRE(extractor.find("\\\"primary_graph\\\": \\\"") != std::string::npos);
	REQUIRE(extractor.find("\\\"shared_context_file\\\": \\\"") != std::string::npos);
	REQUIRE(extractor.find("shared_context = \" PDWEAPON_SHARED_CONTEXT_ENTRY") != std::string::npos);
	REQUIRE(extractor.find("settings = \" PDWEAPON_SETTINGS_ENTRY") != std::string::npos);
	REQUIRE(extractor.find("variables = \" PDWEAPON_VARIABLES_ENTRY") != std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_DEP_MODELS \"dependencies/assets/models\"") != std::string::npos);
	REQUIRE(extractor.find("/held_hi.pdmesh") != std::string::npos);
	REQUIRE(extractor.find("/held_lo.pdmesh") != std::string::npos);
	REQUIRE(extractor.find("bindings/animations.json") != std::string::npos);
	REQUIRE(extractor.find("bindings/audio.json") != std::string::npos);
	REQUIRE(extractor.find("model_archive = \" PDWEAPON_DEP_MODELS \"/visual.pdmesh") != std::string::npos);
	REQUIRE(extractor.find("s_addWeaponMeshDependency") != std::string::npos);
	REQUIRE(extractor.find("s_addWeaponAnimationDependency") != std::string::npos);
	REQUIRE(extractor.find("s_addWeaponAudioDependency") != std::string::npos);
	REQUIRE(extractor.find("s_addProjectileModelDependency") != std::string::npos);
	REQUIRE(extractor.find("s_emitWeaponManifestDependencies") != std::string::npos);
	REQUIRE(extractor.find("s_emitWeaponManifestDependencySchema") != std::string::npos);
	REQUIRE(extractor.find("\"dependency_schema\"") != std::string::npos);
	REQUIRE(extractor.find("required_fields") != std::string::npos);
	REQUIRE(extractor.find("\"fallback.reason\"") != std::string::npos);
	REQUIRE(extractor.find("s_existingArchiveEntryContains") != std::string::npos);
	REQUIRE(extractor.find("weaponGraphArchiveCanonicalSha256File") != std::string::npos);
	REQUIRE(extractor.find("assetArchiveWriterAddPublicDisk(&asset_writer") != std::string::npos);
	REQUIRE(extractor.find("modArchiveAddFileDisk(aw, p->archive_entry") == std::string::npos);

	const std::string main = readFile("port/src/main.c");
	const auto meshEmit = main.find("romExtractAllPdmesh(0)");
	const auto animEmit = main.find("romExtractAllPdanim(0)");
	const auto sfxEmit = main.find("romExtractAllPdsfx(0)");
	const auto weaponEmit = main.find("romExtractAllPdweapon(0)");
	REQUIRE(meshEmit != std::string::npos);
	REQUIRE(animEmit != std::string::npos);
	REQUIRE(sfxEmit != std::string::npos);
	REQUIRE(weaponEmit != std::string::npos);
	REQUIRE(meshEmit < weaponEmit);
	REQUIRE(animEmit < weaponEmit);
	REQUIRE(sfxEmit < weaponEmit);

	const std::string mesh = readFile("port/src/romextract_pdmesh.c");
	REQUIRE(mesh.find("s_pdmeshAddWeaponFuncPayloadWork") != std::string::npos);
	REQUIRE(mesh.find("sp->projectilemodelnum") != std::string::npos);
	REQUIRE(mesh.find("tw->projectilemodelnum") != std::string::npos);
}

TEST_CASE("projectile and entity asset kinds are catalog and manifest visible",
          "[modding][pdxxx][projectile_entity][static][c3814]") {
	std::string catalog = readFile("port/include/assetcatalog.h");
	REQUIRE(catalog.find("ASSET_PROJECTILE") != std::string::npos);
	REQUIRE(catalog.find("ASSET_ENTITY") != std::string::npos);
	REQUIRE(catalog.find("} projectile;") != std::string::npos);
	REQUIRE(catalog.find("} entity;") != std::string::npos);

	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("assetArchiveTypeForPath(path)") != std::string::npos);
	REQUIRE(scanner.find("ASSET_PROJECTILE") != std::string::npos);
	REQUIRE(scanner.find("ASSET_ENTITY") != std::string::npos);
	REQUIRE(scanner.find("\"projectile.ini\"") != std::string::npos);
	REQUIRE(scanner.find("\"entity.ini\"") != std::string::npos);
	REQUIRE(scanner.find("projectile.ini - physical projectile behavior metadata") != std::string::npos);
	REQUIRE(scanner.find("entity.ini - deployed/stuck behavior archetype metadata") != std::string::npos);
	REQUIRE(scanner.find("catalogDepRegister(e->id, e->ext.projectile.entity_ref") != std::string::npos);
	{
		const size_t projectile_start = scanner.find("case ASSET_PROJECTILE:");
		const size_t entity_start = scanner.find("case ASSET_ENTITY:",
			projectile_start);
		const size_t prop_start = scanner.find("case ASSET_PROP:",
			entity_start);
		REQUIRE(projectile_start != std::string::npos);
		REQUIRE(entity_start != std::string::npos);
		REQUIRE(prop_start != std::string::npos);
		const std::string projectile_case =
			scanner.substr(projectile_start, entity_start - projectile_start);
		const std::string entity_case =
			scanner.substr(entity_start, prop_start - entity_start);
		REQUIRE(projectile_case.find("catalogSetPrimaryFile(e, e->ext.projectile.behavior_graph)") !=
		        std::string::npos);
		REQUIRE(projectile_case.find("catalogSetPrimaryFile(e, e->ext.projectile.model_file)") ==
		        std::string::npos);
		REQUIRE(entity_case.find("catalogSetPrimaryFile(e, e->ext.entity.behavior_graph)") !=
		        std::string::npos);
		REQUIRE(entity_case.find("catalogSetPrimaryFile(e, e->ext.entity.model_file)") ==
		        std::string::npos);
	}

	std::string packer = readFile("port/src/modpack_pdmod.c");
	REQUIRE(packer.find("assetArchiveTypeForPath(path)") != std::string::npos);
	REQUIRE(packer.find("ASSET_PROJECTILE") != std::string::npos);
	REQUIRE(packer.find("ASSET_ENTITY") != std::string::npos);
	REQUIRE(packer.find("RUN_FAMILY(\"projectiles\", \"projectile.ini\", \"projectile\"") != std::string::npos);
	REQUIRE(packer.find("RUN_FAMILY(\"entities\", \"entity.ini\", \"entity\"") != std::string::npos);

	std::string manifest_h = readFile("port/include/net/netmanifest.h");
	REQUIRE(manifest_h.find("MANIFEST_TYPE_PROJECTILE") != std::string::npos);
	REQUIRE(manifest_h.find("MANIFEST_TYPE_ENTITY") != std::string::npos);
	REQUIRE(manifest_h.find("MANIFEST_TYPE_ASSET") != std::string::npos);
	REQUIRE(manifest_h.find("slot_index stores asset_type_e") != std::string::npos);

	std::string manifest_c = readFile("port/src/net/netmanifest.c");
	std::string manifest_type = readFile("port/src/net/net_manifest_type.c");
	REQUIRE(manifest_type.find("case ASSET_PROJECTILE: return MANIFEST_TYPE_PROJECTILE") != std::string::npos);
	REQUIRE(manifest_type.find("case ASSET_ENTITY:     return MANIFEST_TYPE_ENTITY") != std::string::npos);
	REQUIRE(manifest_type.find("default:               return MANIFEST_TYPE_ASSET") != std::string::npos);
	REQUIRE(manifest_type.find("manifest_type != netManifestTypeForCatalogAsset(asset_type)") != std::string::npos);
	REQUIRE(manifest_type.find("slot_index == (u8)asset_type") != std::string::npos);
	REQUIRE(manifest_c.find("s_manifestEntryCatalogAssetType") != std::string::npos);
	REQUIRE(manifest_c.find("mtype == MANIFEST_TYPE_ASSET") != std::string::npos);
	REQUIRE(manifest_c.find("netManifestTypeAcceptsCatalogAsset(") != std::string::npos);

	std::string netmsg = readFile("port/src/net/netmsg.c");
	REQUIRE(netmsg.find("ASSET_WEAPON, ASSET_PROJECTILE, ASSET_ENTITY") != std::string::npos);

	std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("projectile.ini\") == 0) return ASSET_PROJECTILE") != std::string::npos);
	REQUIRE(distrib.find("entity.ini\") == 0)    return ASSET_ENTITY") != std::string::npos);
	{
		const size_t projectile_start = distrib.find("case ASSET_PROJECTILE:");
		const size_t entity_start = distrib.find("case ASSET_ENTITY:",
			projectile_start);
		const size_t anim_start = distrib.find("case ASSET_ANIMATION:",
			entity_start);
		REQUIRE(projectile_start != std::string::npos);
		REQUIRE(entity_start != std::string::npos);
		REQUIRE(anim_start != std::string::npos);
		const std::string projectile_case =
			distrib.substr(projectile_start, entity_start - projectile_start);
		const std::string entity_case =
			distrib.substr(entity_start, anim_start - entity_start);
		REQUIRE(projectile_case.find("distribSetPrimaryFromFile(e, dirpath, e->ext.projectile.behavior_graph)") !=
		        std::string::npos);
		REQUIRE(projectile_case.find("distribSetPrimaryFromFile(e, dirpath, e->ext.projectile.model_file)") ==
		        std::string::npos);
		REQUIRE(entity_case.find("distribSetPrimaryFromFile(e, dirpath, e->ext.entity.behavior_graph)") !=
		        std::string::npos);
		REQUIRE(entity_case.find("distribSetPrimaryFromFile(e, dirpath, e->ext.entity.model_file)") ==
		        std::string::npos);
	}
	REQUIRE(distrib.find("const asset_entry_t *existing = assetCatalogResolve(slot->id)") != std::string::npos);
	REQUIRE(distrib.find("preserved_entry = *existing") != std::string::npos);
	const size_t populate = distrib.find("s32 populated = populateExtFromIni(e, type, destdir, &ini,");
	const size_t refreshed = distrib.find("e = assetCatalogGetMutable(slot->id)", populate);
	const size_t rejected = distrib.find("if (!populated)", populate);
	REQUIRE(populate != std::string::npos);
	REQUIRE(refreshed != std::string::npos);
	REQUIRE(rejected != std::string::npos);
	REQUIRE(populate < refreshed);
	REQUIRE(refreshed < rejected);
	REQUIRE(distrib.find("if (existing) *e = preserved_entry;") != std::string::npos);
	REQUIRE(distrib.find("else assetCatalogUnregister(slot->id);") != std::string::npos);
	REQUIRE(distrib.find("preserved_weapon_requirefeature") != std::string::npos);
	REQUIRE(distrib.find("iniGetInt(ini, \"dual_wieldable\",\n"
	                     "                    preserved_dual_wieldable)") != std::string::npos);

	std::string modmgr = readFile("port/src/modmgr.c");
	REQUIRE(modmgr.find(".pdprojectile") != std::string::npos);
	REQUIRE(modmgr.find(".pdentity") != std::string::npos);
}

TEST_CASE("base ROM extraction skips empty file slots before size lookup",
          "[modding][pdxxx][c3844][static]") {
	const std::string extractor = readFile("port/src/romextract.c");
	const std::string firstLaunchNeedle =
		"u8 *data = romdataFileGetData(fileNum);\n"
		"        if (data == NULL) {\n"
		"            skippedEmpty++;\n"
		"            continue;\n"
		"        }\n"
		"\n"
		"        s32 size = romdataFileGetSize(fileNum);";
	const std::string verifyNeedle =
		"u8 *romData = romdataFileGetData(fileNum);\n"
		"    if (romData == NULL) {\n"
		"        th->skippedEmpty++;\n"
		"        return;\n"
		"    }\n"
		"\n"
		"    s32 romSize = romdataFileGetSize(fileNum);";
	REQUIRE(extractor.find(firstLaunchNeedle) != std::string::npos);
	REQUIRE(extractor.find(verifyNeedle) != std::string::npos);
}

TEST_CASE("runtime fallback telemetry excludes ROM extraction bootstrap",
          "[modding][pdxxx][c3844][static][fallback]") {
	const std::string extractor = readFile("port/src/romextract.c");
	const std::string extractor_header = readFile("port/include/romextract.h");
	const std::string romdata = readFile("port/src/romdata.c");

	REQUIRE(extractor_header.find("s32 romExtractIsBootstrapping(void);") !=
	        std::string::npos);
	REQUIRE(extractor.find("static s32 s_BootstrapDepth") != std::string::npos);
	REQUIRE(extractor.find("s_bootstrapEnter();\n    bootProgressUpdate(0, ROMEXTRACT_MAX_FILES);") !=
	        std::string::npos);
	REQUIRE(extractor.find("s_bootstrapLeave();\n\n    sysLogPrintf(LOG_NOTE,\n        \"ROMEXTRACT: complete.") !=
	        std::string::npos);
	REQUIRE(romdata.find("if (!romExtractIsBootstrapping()) {\n"
	                     "\t\t\t\tsysLoudFailf(\"FALLBACK\"") !=
	        std::string::npos);
	REQUIRE(romdata.find("assetFallbackRecord(ASSET_NONE, fileNum,\n"
	                     "\t\t\t\t\t\"extracted cache missing -> raw ROM\");") !=
	        std::string::npos);
}

TEST_CASE("runtime fallback telemetry fatal cutover is wired",
          "[modding][pdxxx][c3849][static][fallback][cutover]") {
	const std::string telemetry = readFile("port/src/asset_fallback_telemetry.c");

	REQUIRE(telemetry.find("assetFallbackFamilyIsFatalCutover") !=
	        std::string::npos);
	REQUIRE(telemetry.find("case ASSET_TEXTURE:") != std::string::npos);
	REQUIRE(telemetry.find("case ASSET_ANIMATION:") != std::string::npos);
	REQUIRE(telemetry.find("case ASSET_SFX:") != std::string::npos);
	REQUIRE(telemetry.find("case ASSET_MUSIC:") != std::string::npos);
	REQUIRE(telemetry.find("case ASSET_SCENARIO:") != std::string::npos);
	REQUIRE(telemetry.find("case ASSET_MODEL:") != std::string::npos);
	REQUIRE(telemetry.find("#ifndef PD_TESTS\n    if (assetFallbackFatalCutoverPending())") !=
	        std::string::npos);
	REQUIRE(telemetry.find("sysFatalError(\n            \"ASSET.FALLBACK: fatal cutover") !=
	        std::string::npos);
}

TEST_CASE("scenario path extraction uses runtime setup pointer resolution",
          "[modding][pdxxx][c3844][static]") {
	const std::string arena = readFile("port/src/romextract_pdarena.c");

	REQUIRE(arena.find("static s32 s_resolveSetupPointer") !=
	        std::string::npos);
	REQUIRE(arena.find("raw >= base && raw < end") != std::string::npos);
	REQUIRE(arena.find("raw < (uintptr_t)size") != std::string::npos);
	REQUIRE(arena.find("s_resolveSetupPointer(data, size, setup->paths") !=
	        std::string::npos);
	REQUIRE(arena.find("setup->paths, sizeof(void *)") !=
	        std::string::npos);
	REQUIRE(arena.find("sizeof(paths[i].pads)") != std::string::npos);
	REQUIRE(arena.find("s_resolveSetupPointer(data, size, paths[i].pads") !=
	        std::string::npos);
	REQUIRE(arena.find("if (!paths_ofs) {") != std::string::npos);
	REQUIRE(arena.find("s_textbufAppend(paths_json, \"  ]\\n}\\n\")") !=
	        std::string::npos);
}

TEST_CASE("random meta arenas do not resolve through the stage table",
          "[modding][pdxxx][c3844][static]") {
	const std::string arena = readFile("port/src/romextract_pdarena.c");

	REQUIRE(arena.find("static s32 s_arenaStageIndexForScenario") !=
	        std::string::npos);
	REQUIRE(arena.find("stagenum == STAGE_MP_RANDOM_MULTI") !=
	        std::string::npos);
	REQUIRE(arena.find("stagenum == STAGE_MP_RANDOM_SOLO") !=
	        std::string::npos);
	REQUIRE(arena.find("return stageGetIndex(stagenum);") !=
	        std::string::npos);
	REQUIRE(arena.find("stageGetIndex(a->stagenum)") == std::string::npos);
	REQUIRE(arena.find("s_arenaStageIndexForScenario(a->stagenum) >= 0") !=
	        std::string::npos);
	REQUIRE(arena.find("s32 stage_idx = s_arenaStageIndexForScenario(a->stagenum);") !=
	        std::string::npos);
}

TEST_CASE("base UI chrome is emitted and loaded as pdui source",
          "[modding][pdxxx][c3844][static]") {
	const std::string theme = readFile("port/fast3d/pdgui_theme.cpp");

	REQUIRE(theme.find("\"base:ui_chrome_frame\"") != std::string::npos);
	REQUIRE(theme.find("\"ui_chrome_frame\"") != std::string::npos);
	REQUIRE(theme.find("s_generateChromeFrameBgra(bgra)") !=
	        std::string::npos);
	REQUIRE(theme.find("g_TcGeneralConfigs") != std::string::npos);
	REQUIRE(theme.find("cfg_copy = g_TcGeneralConfigs[e->tex_index]") !=
		std::string::npos);
	REQUIRE(theme.find("s_decodeUiConfigWithTempPool") != std::string::npos);
	REQUIRE(theme.find("texInitPool(&pool") != std::string::npos);
	REQUIRE(theme.find("extractor-only") != std::string::npos);
	REQUIRE(theme.find("deferred to render-loop trigger") ==
		std::string::npos);
	REQUIRE(theme.find("assetArchiveWriterAddPublicMem(&asset_writer, \"texture.tga\"") !=
	        std::string::npos);
	REQUIRE(theme.find("s_loadPduiTexture(chrome, &w, &h)") !=
	        std::string::npos);
	REQUIRE(theme.find("UI.CHROME: loaded base chrome from ui_chrome_frame.pdui") !=
	        std::string::npos);
	REQUIRE(theme.find("mods/base-game/ui-chrome") == std::string::npos);
	REQUIRE(theme.find("base-game chrome missing") == std::string::npos);
	REQUIRE(theme.find("ui_chrome_frame.tga") == std::string::npos);
}

TEST_CASE("weapon graph archive helpers derive IDs and validate graph roots",
          "[modding][pdxxx][weapon_graph][archive][c3814]") {
	REQUIRE(std::string(weaponGraphArchiveDescriptorForType(ASSET_WEAPON)) == "weapon.ini");
	REQUIRE(std::string(weaponGraphArchiveDescriptorForType(ASSET_PROJECTILE)) == "projectile.ini");
	REQUIRE(std::string(weaponGraphArchiveDescriptorForType(ASSET_ENTITY)) == "entity.ini");
	REQUIRE(weaponGraphArchiveGraphRequired(ASSET_WEAPON) == 1);
	REQUIRE(weaponGraphArchiveGraphRequired(ASSET_PROJECTILE) == 1);
	REQUIRE(weaponGraphArchiveGraphRequired(ASSET_ENTITY) == 1);

	char id[CATALOG_ID_LEN];
	REQUIRE(weaponGraphArchiveDerivedNestedId("base:slayer", ASSET_PROJECTILE,
		"Fly By Wire Rocket", id, sizeof(id)) == 0);
	REQUIRE(std::string(id) == "base:slayer__projectile_fly_by_wire_rocket");
	REQUIRE(weaponGraphArchiveDerivedNestedId("base:laptopgun", ASSET_ENTITY,
		"deployed-autogun", id, sizeof(id)) == 0);
	REQUIRE(std::string(id) == "base:laptopgun__entity_deployed_autogun");
	REQUIRE(weaponGraphArchiveDerivedNestedId("base:laptopgun", ASSET_PROP,
		"bad", id, sizeof(id)) != 0);

	const std::string weaponIni =
		"[weapon]\n"
		"catalog_id = base:test_weapon\n"
		"manifest = _meta/manifest.json\n"
		"behavior_graph = behavior.graph.json\n"
		"nested_payloads = nested_payloads.json\n";
	TempArchive weapon = writeArchiveEntries("weapon-graph-root", {
		{ "weapon.ini", weaponIni },
		{ "_meta/manifest.json", "{}\n" },
		{ "behavior.graph.json", "{\"schema\":\"pd.weapon_graph.v1\"}\n" },
		{ "nested_payloads.json", "{\"schema\":\"pd.weapon_nested_payloads.v1\",\"payloads\":[]}\n" },
	});

	char err[256] = {};
	REQUIRE(weaponGraphArchiveValidateRootFile(weapon.path.string().c_str(),
		ASSET_WEAPON, err, sizeof(err)) == 0);

	weapon_graph_archive_descriptor_t desc;
	REQUIRE(weaponGraphArchiveReadDescriptorFile(weapon.path.string().c_str(),
		ASSET_WEAPON, &desc, err, sizeof(err)) == 0);
	REQUIRE(std::string(desc.catalog_id) == "base:test_weapon");
	REQUIRE(std::string(desc.behavior_graph) == "behavior.graph.json");
	REQUIRE(std::string(desc.nested_payloads) == "nested_payloads.json");

	const std::string projectileIni =
		"[projectile]\n"
		"catalog_id = base:test_projectile\n"
		"model_file = model.gltf\n";
	TempArchive brokenProjectile = writeArchiveEntries("projectile-graph-root", {
		{ "projectile.ini", projectileIni },
	});
	REQUIRE(weaponGraphArchiveValidateRootFile(brokenProjectile.path.string().c_str(),
		ASSET_PROJECTILE, err, sizeof(err)) != 0);

	const std::string entityIni =
		"[entity]\n"
		"catalog_id = base:test_entity\n"
		"archetype = autogun\n"
		"behavior_graph = behavior.graph.json\n";
	TempArchive brokenEntity = writeArchiveEntries("entity-graph-root", {
		{ "entity.ini", entityIni },
	});
	REQUIRE(weaponGraphArchiveValidateRootFile(brokenEntity.path.string().c_str(),
		ASSET_ENTITY, err, sizeof(err)) != 0);
}

TEST_CASE("weapon graph canonical archive SHA ignores entry order",
          "[modding][pdxxx][weapon_graph][sha256][c3814]") {
	const std::string projectileIni =
		"[projectile]\n"
		"catalog_id = base:rocket\n"
		"behavior_graph = behavior.graph.json\n";
	const std::string graph =
		"{\"schema\":\"pd.projectile_graph.v1\",\"nodes\":[]}\n";

	TempArchive a = writeArchiveEntries("canonical-a", {
		{ "projectile.ini", projectileIni },
		{ "behavior.graph.json", graph },
	});
	TempArchive b = writeArchiveEntries("canonical-b", {
		{ "behavior.graph.json", graph },
		{ "projectile.ini", projectileIni },
	});
	TempArchive c = writeArchiveEntries("canonical-c", {
		{ "projectile.ini", projectileIni },
		{ "behavior.graph.json", "{\"schema\":\"pd.projectile_graph.v1\",\"nodes\":[1]}\n" },
	});

	char ha[SHA256_HEX_SIZE] = {};
	char hb[SHA256_HEX_SIZE] = {};
	char hc[SHA256_HEX_SIZE] = {};
	char hbBytes[SHA256_HEX_SIZE] = {};
	REQUIRE(weaponGraphArchiveCanonicalSha256File(a.path.string().c_str(), ha) == 0);
	REQUIRE(weaponGraphArchiveCanonicalSha256File(b.path.string().c_str(), hb) == 0);
	REQUIRE(weaponGraphArchiveCanonicalSha256File(c.path.string().c_str(), hc) == 0);
	REQUIRE(std::string(ha).size() == 64);
	REQUIRE(std::string(ha) == std::string(hb));
	REQUIRE(std::string(ha) != std::string(hc));

	const std::string bytes = readFile(b.path.string().c_str());
	REQUIRE(weaponGraphArchiveCanonicalSha256Bytes(bytes.data(),
		static_cast<u32>(bytes.size()), hbBytes) == 0);
	REQUIRE(std::string(hbBytes) == std::string(hb));
}

TEST_CASE("canonical archive hash accepts filesystem resolved nested archive bytes",
          "[modding][pdxxx][weapon_graph][sha256][nested-source]") {
	TempArchive mesh = writeArchiveEntries("canonical-nested-mesh", {
		{"mesh.ini", "[model]\ncatalog_id=example:shared\nkind=mesh\ngeometry_file=model.obj\n"},
		{"model.obj", "v 0 0 0\nv 2 0 0\nv 0 2 0\nf 1 2 3\n"},
	});
	const std::string meshBytes = readFile(mesh.path.string().c_str());
	TempArchive body = writeArchiveEntries("canonical-nested-body", {
		{"mesh.pdmesh", meshBytes},
	});
	const std::string bodyBytes = readFile(body.path.string().c_str());
	u32 nestedSize = 0;
	void *nested = modArchiveExtractMemAlloc(bodyBytes.data(),
		static_cast<u32>(bodyBytes.size()), "mesh.pdmesh", &nestedSize);
	REQUIRE(nested != nullptr);
	const std::string resolvedBytes(static_cast<const char *>(nested), nestedSize);
	std::free(nested);
	REQUIRE(resolvedBytes == meshBytes);
	const std::string nestedPath = body.path.string() + "::mesh.pdmesh";
	char diskHash[SHA256_HEX_SIZE] = {}, bytesHash[SHA256_HEX_SIZE] = {};
	char nestedHash[SHA256_HEX_SIZE] = {};
	REQUIRE(weaponGraphArchiveCanonicalSha256File(mesh.path.string().c_str(), diskHash) == 0);
	REQUIRE(weaponGraphArchiveCanonicalSha256Bytes(resolvedBytes.data(),
		static_cast<u32>(resolvedBytes.size()), bytesHash) == 0);
	/* pd-tests supplies filesystem bytes through its existing seam. The
	 * installed body/head harness exercises the real nested fs reader/binder. */
	ScopedCatalogSourceStub reset;
	testStubFsFileLoadWith(nestedPath.c_str(), resolvedBytes.data(),
		static_cast<u32>(resolvedBytes.size()));
	REQUIRE(weaponGraphArchiveCanonicalSha256File(nestedPath.c_str(), nestedHash) == 0);
	REQUIRE(std::string(nestedHash) == diskHash);
	REQUIRE(std::string(nestedHash) == bytesHash);

	const char malformed[] = "not a ZIP archive";
	testStubFsFileLoadWith(nestedPath.c_str(), malformed, sizeof(malformed) - 1);
	std::strcpy(nestedHash, "stale");
	REQUIRE(weaponGraphArchiveCanonicalSha256File(nestedPath.c_str(), nestedHash) < 0);
	REQUIRE(nestedHash[0] == '\0');
	testStubFsFileLoadWith(nullptr, nullptr, 0);
	std::strcpy(nestedHash, "stale");
	REQUIRE(weaponGraphArchiveCanonicalSha256File(nestedPath.c_str(), nestedHash) < 0);
	REQUIRE(nestedHash[0] == '\0');
}

TEST_CASE("weapon graph nested payload inventory derives IDs and content hashes",
          "[modding][pdxxx][weapon_graph][inventory][c3814]") {
	const std::string projectileIni =
		"[projectile]\n"
		"name = Rocket\n"
		"behavior_graph = behavior.graph.json\n";
	const std::string entityIni =
		"[entity]\n"
		"name = Deployed Autogun\n"
		"archetype = autogun\n"
		"behavior_graph = behavior.graph.json\n";
	TempArchive projectile = writeArchiveEntries("nested-projectile", {
		{ "projectile.ini", projectileIni },
		{ "behavior.graph.json", "{\"schema\":\"pd.projectile_graph.v1\"}\n" },
	});
	TempArchive entity = writeArchiveEntries("nested-entity", {
		{ "entity.ini", entityIni },
		{ "behavior.graph.json", "{\"schema\":\"pd.entity_graph.v1\"}\n" },
	});

	const std::string projectileBytes = readFile(projectile.path.string().c_str());
	const std::string entityBytes = readFile(entity.path.string().c_str());
	const std::string parentIni =
		"[weapon]\n"
		"catalog_id = base:laptopgun\n"
		"behavior_graph = behavior.graph.json\n"
		"nested_payloads = nested_payloads.json\n";
	TempArchive parent = writeArchiveEntries("nested-parent", {
		{ "weapon.ini", parentIni },
		{ "behavior.graph.json", "{\"schema\":\"pd.weapon_graph.v1\"}\n" },
		{ "nested_payloads.json", "{\"schema\":\"pd.weapon_nested_payloads.v1\",\"payloads\":[]}\n" },
		{ "projectiles/thrown_laptop.pdprojectile", projectileBytes },
		{ "entities/deployed_autogun.pdentity", entityBytes },
	});

	weapon_graph_archive_inventory_t inv;
	char err[256] = {};
	REQUIRE(weaponGraphArchiveScanNestedPayloadsFile(parent.path.string().c_str(),
		"base:laptopgun", &inv, err, sizeof(err)) == 2);
	REQUIRE(inv.count == 2);

	char projectileHash[SHA256_HEX_SIZE] = {};
	REQUIRE(weaponGraphArchiveCanonicalSha256File(projectile.path.string().c_str(),
		projectileHash) == 0);

	bool sawProjectile = false;
	bool sawEntity = false;
	for (s32 i = 0; i < inv.count; i++) {
		const weapon_graph_archive_payload_t &p = inv.payloads[i];
		if (p.type == ASSET_PROJECTILE) {
			sawProjectile = true;
			REQUIRE(std::string(p.local_slug) == "thrown_laptop");
			REQUIRE(std::string(p.catalog_id) == "base:laptopgun__projectile_thrown_laptop");
			REQUIRE(std::string(p.archive_entry) == "projectiles/thrown_laptop.pdprojectile");
			REQUIRE(std::string(p.canonical_sha256) == std::string(projectileHash));
		}
		if (p.type == ASSET_ENTITY) {
			sawEntity = true;
			REQUIRE(std::string(p.local_slug) == "deployed_autogun");
			REQUIRE(std::string(p.catalog_id) == "base:laptopgun__entity_deployed_autogun");
			REQUIRE(std::string(p.archive_entry) == "entities/deployed_autogun.pdentity");
			REQUIRE(std::string(p.canonical_sha256).size() == 64);
		}
	}
	REQUIRE(sawProjectile);
	REQUIRE(sawEntity);

	char json[2048];
	REQUIRE(weaponGraphArchiveFormatNestedPayloadsJson("base:laptopgun",
		&inv, json, sizeof(json)) == 0);
	const std::string inventoryJson(json);
	REQUIRE(inventoryJson.find("\"schema\": \"pd.weapon_nested_payloads.v1\"") != std::string::npos);
	REQUIRE(inventoryJson.find("base:laptopgun__projectile_thrown_laptop") != std::string::npos);
	REQUIRE(inventoryJson.find("base:laptopgun__entity_deployed_autogun") != std::string::npos);
	REQUIRE(inventoryJson.find(projectileHash) != std::string::npos);
}

TEST_CASE("weapon graph runtime compiler validates modules and deterministic IR",
          "[modding][pdxxx][weapon_graph][compiler][c3814]") {
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_weapon\",\n"
		"  \"graph_id\": \"unit_test_graph_v1\",\n"
		"  \"nodes\": [\n"
		"    {\n"
		"      \"id\": \"primary_action\",\n"
		"      \"kind\": \"fire.hitscan\",\n"
		"      \"params\": {\n"
		"        \"recoverytime_ticks60\": 12,\n"
		"        \"projectile_ref\": null,\n"
		"        \"damage\": 1.25,\n"
		"        \"ammo_slot\": 0,\n"
		"        \"mode\": \"primary\"\n"
		"      }\n"
		"    },\n"
		"    {\n"
		"      \"id\": \"secondary_action\",\n"
		"      \"kind\": \"spawn.fired_projectile\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"secondary\",\n"
		"        \"projectile_ref\": \"base:test_weapon__projectile_rocket\",\n"
		"        \"timer_ticks60\": 180\n"
		"      }\n"
		"    }\n"
		"  ],\n"
		"  \"edges\": [ { \"from\": \"primary_action\", \"to\": \"secondary_action\" } ],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"primary_action\" },\n"
		"    { \"name\": \"secondary\", \"node\": \"secondary_action\" }\n"
		"  ]\n"
		"}\n";

	weapon_graph_ir_t a;
	weapon_graph_ir_t b;
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &a, err, sizeof(err)) == 0);
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &b, err, sizeof(err)) == 0);
	REQUIRE(a.node_count == 2);
	REQUIRE(a.edge_count == 1);
	REQUIRE(a.export_count == 2);
	REQUIRE(a.nodes[0].opcode == WEAPON_GRAPH_OP_FIRE_HITSCAN);
	REQUIRE(a.nodes[1].opcode == WEAPON_GRAPH_OP_SPAWN_FIRED_PROJECTILE);
	REQUIRE(std::string(a.source_sha256).size() == 64);
	REQUIRE(std::string(a.ir_sha256).size() == 64);
	REQUIRE(std::string(a.ir_sha256) == std::string(b.ir_sha256));
	REQUIRE(std::string(a.params[a.nodes[0].param_start].key) == "ammo_slot");

	REQUIRE(weaponGraphOpcodeForKind(ASSET_PROJECTILE,
		"projectile.fly_by_wire") == WEAPON_GRAPH_OP_PROJECTILE_FLY_BY_WIRE);
	REQUIRE(weaponGraphOpcodeForKind(ASSET_ENTITY,
		"entity.autogun") == WEAPON_GRAPH_OP_ENTITY_AUTOGUN);
	REQUIRE(weaponGraphOpcodeForKind(ASSET_WEAPON,
		"projectile.fly_by_wire") == WEAPON_GRAPH_OP_INVALID);

	const std::string weaponIni =
		"[weapon]\n"
		"catalog_id = base:test_weapon\n"
		"behavior_graph = behavior.graph.json\n";
	TempArchive weapon = writeArchiveEntries("weapon-graph-compiler", {
		{ "weapon.ini", weaponIni },
		{ "behavior.graph.json", graph },
	});
	weapon_graph_ir_t fromArchive;
	REQUIRE(weaponGraphCompileArchiveFile(weapon.path.string().c_str(),
		ASSET_WEAPON, &fromArchive, err, sizeof(err)) == 0);
	REQUIRE(std::string(fromArchive.ir_sha256) == std::string(a.ir_sha256));

	/* B-917: real archives author the *_file spelling (romextract_pdweapon.c);
	 * the fixture must match or the descriptor-key drift can regrow unseen. */
	const std::string cleanWeaponIni =
		"[weapon]\n"
		"catalog_id = base:test_weapon\n"
		"primary_graph = behavior/primary.graph.json\n"
		"secondary_graph = behavior/secondary.graph.json\n"
		"shared_context_file = behavior/shared-context.json\n";
	const std::string primaryGraph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_weapon\",\n"
		"  \"graph_id\": \"primary\",\n"
		"  \"nodes\": [ { \"id\": \"primary_action\", \"kind\": \"fire.hitscan\","
		" \"params\": { \"mode\": \"primary\", \"damage\": 1.25 } } ],\n"
		"  \"exports\": [ { \"name\": \"primary\", \"node\": \"primary_action\" } ]\n"
		"}\n";
	const std::string secondaryGraph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_weapon\",\n"
		"  \"graph_id\": \"secondary\",\n"
		"  \"nodes\": [ { \"id\": \"secondary_action\", \"kind\": \"spawn.fired_projectile\","
		" \"params\": { \"mode\": \"secondary\", \"projectile_ref\": \"base:test_weapon__projectile_rocket\" } } ],\n"
		"  \"exports\": [ { \"name\": \"secondary\", \"node\": \"secondary_action\" } ]\n"
		"}\n";
	const std::string sharedContext =
		"{ \"schema\": \"pd.weapon_shared_context.v1\", \"asset_id\": \"base:test_weapon\","
		" \"contexts\": [ { \"name\": \"owner_player\", \"scope\": \"player\","
		" \"source\": \"equipped_player\", \"type\": \"player_ref\" } ] }\n";
	TempArchive cleanWeapon = writeArchiveEntries("weapon-clean-graph-compiler", {
		{ "weapon.ini", cleanWeaponIni },
		{ "behavior/primary.graph.json", primaryGraph },
		{ "behavior/secondary.graph.json", secondaryGraph },
		{ "behavior/shared-context.json", sharedContext },
	});
	weapon_graph_ir_t fromCleanArchive;
	REQUIRE(weaponGraphCompileArchiveFile(cleanWeapon.path.string().c_str(),
		ASSET_WEAPON, &fromCleanArchive, err, sizeof(err)) == 0);
	REQUIRE(fromCleanArchive.context_count == 1);
	REQUIRE(fromCleanArchive.node_count == 2);
	REQUIRE(fromCleanArchive.export_count == 2);
	REQUIRE(fromCleanArchive.nodes[0].opcode == WEAPON_GRAPH_OP_FIRE_HITSCAN);
	REQUIRE(fromCleanArchive.nodes[1].opcode == WEAPON_GRAPH_OP_SPAWN_FIRED_PROJECTILE);

	/* B-917: the bare legacy spelling stays accepted (first-set-wins alias),
	 * so older fixtures and hand-authored archives keep compiling. */
	const std::string legacyWeaponIni =
		"[weapon]\n"
		"catalog_id = base:test_weapon\n"
		"primary_graph = behavior/primary.graph.json\n"
		"secondary_graph = behavior/secondary.graph.json\n"
		"shared_context = behavior/shared-context.json\n";
	TempArchive legacyWeapon = writeArchiveEntries("weapon-legacy-graph-compiler", {
		{ "weapon.ini", legacyWeaponIni },
		{ "behavior/primary.graph.json", primaryGraph },
		{ "behavior/secondary.graph.json", secondaryGraph },
		{ "behavior/shared-context.json", sharedContext },
	});
	weapon_graph_ir_t fromLegacyArchive;
	REQUIRE(weaponGraphCompileArchiveFile(legacyWeapon.path.string().c_str(),
		ASSET_WEAPON, &fromLegacyArchive, err, sizeof(err)) == 0);
	REQUIRE(fromLegacyArchive.context_count == 1);
	REQUIRE(std::string(fromLegacyArchive.ir_sha256) ==
		std::string(fromCleanArchive.ir_sha256));
}

TEST_CASE("weapon mod save contract supports shotgun template dual wield save",
          "[modding][pdxxx][weapon_graph][compiler][ui][c3814]") {
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"mod:weapon_needler\",\n"
		"  \"graph_id\": \"shotgun_template_save\",\n"
		"  \"nodes\": [\n"
		"    {\n"
		"      \"id\": \"primary_action\",\n"
		"      \"kind\": \"fire.hitscan\",\n"
		"      \"params\": {\n"
		"        \"recoverytime_ticks60\": 12,\n"
		"        \"projectile_ref\": null,\n"
		"        \"damage\": 1.25,\n"
		"        \"ammo_slot\": 0,\n"
		"        \"mode\": \"primary\"\n"
		"      }\n"
		"    },\n"
		"    {\n"
		"      \"id\": \"secondary_action\",\n"
		"      \"kind\": \"fire.hitscan\",\n"
		"      \"params\": {\n"
		"        \"recoverytime_ticks60\": 18,\n"
		"        \"projectile_ref\": null,\n"
		"        \"damage\": 1.75,\n"
		"        \"ammo_slot\": 0,\n"
		"        \"mode\": \"secondary\"\n"
		"      }\n"
		"    }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"primary_action\" },\n"
		"    { \"name\": \"secondary\", \"node\": \"secondary_action\" }\n"
		"  ]\n"
		"}\n";

	char err[256] = {};
	weapon_graph_ir_t ir;
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);
	REQUIRE(ir.export_count == 2);

	const std::string weaponIni =
		"[weapon]\n"
		"schema = pd.weapon.v1\n"
		"dependency_closure = embedded.v2\n"
		"catalog_id = mod:weapon_needler\n"
		"name = Needler\n"
		"manifest = _meta/manifest.json\n"
		"behavior_graph = behavior.graph.json\n"
		"nested_payloads = nested_payloads.json\n"
		"model_file = models/held_hi.pdmesh\n"
		"dual_wieldable = 1\n"
		"\n"
		"[references]\n"
		"template = base:shotgun\n"
		"model_ref = base:model_shotgun_hi\n"
		"animation_ref = base:invanim_shotgun_singleshot\n"
		"animation_archive = animations/invanim_shotgun_singleshot.pdanim\n";
	const std::string manifest =
		"{\n"
		"  \"schema\": \"pd.weapon.manifest.v1\",\n"
		"  \"catalog_id\": \"mod:weapon_needler\",\n"
		"  \"name\": \"Needler\",\n"
		"  \"creator\": \"Agent\",\n"
		"  \"template\": \"base:shotgun\",\n"
		"  \"dependency_closure\": \"embedded.v2\"\n"
		"}\n";
	const std::string nested =
		"{\"schema\":\"pd.weapon_nested_payloads.v1\",\"asset_id\":\"mod:weapon_needler\",\"payloads\":[]}\n";
	TempArchive saved = writeArchiveEntries("shotgun-template-save", {
		{ "weapon.ini", weaponIni },
		{ "_meta/manifest.json", manifest },
		{ "behavior.graph.json", graph },
		{ "nested_payloads.json", nested },
		{ "animations/invanim_shotgun_singleshot.pdanim", "pdanim" },
	});

	REQUIRE(weaponGraphArchiveValidateRootFile(saved.path.string().c_str(),
		ASSET_WEAPON, err, sizeof(err)) == 0);
	weapon_graph_archive_descriptor_t desc;
	REQUIRE(weaponGraphArchiveReadDescriptorFile(saved.path.string().c_str(),
		ASSET_WEAPON, &desc, err, sizeof(err)) == 0);
	REQUIRE(std::string(desc.catalog_id) == "mod:weapon_needler");
	REQUIRE(weaponGraphCompileArchiveFile(saved.path.string().c_str(),
		ASSET_WEAPON, &ir, err, sizeof(err)) == 0);

	const std::string savedIni =
		readArchiveEntryText(saved.path.string().c_str(), "weapon.ini");
	REQUIRE(savedIni.find("catalog_id = mod:weapon_needler") != std::string::npos);
	REQUIRE(savedIni.find("name = Needler") != std::string::npos);
	REQUIRE(savedIni.find("model_file = models/held_hi.pdmesh") != std::string::npos);
	REQUIRE(savedIni.find("template = base:shotgun") != std::string::npos);
	REQUIRE(savedIni.find("animation_archive = animations/invanim_shotgun_singleshot.pdanim") !=
	        std::string::npos);
	REQUIRE(savedIni.find("dual_wieldable = 1") != std::string::npos);
	REQUIRE(savedIni.find("weapon_id") == std::string::npos);
}

TEST_CASE("weapon graph runtime preserves modular subgraphs and shared context",
          "[modding][pdxxx][weapon_graph][compiler][context][c3814]") {
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:remotemine\",\n"
		"  \"graph_id\": \"modular_context_test\",\n"
		"  \"shared_context\": [\n"
		"    { \"name\": \"owner_player\", \"scope\": \"player\", "
		"\"source\": \"equipped_player\", \"type\": \"player_ref\", "
		"\"lifetime\": \"weapon_instance\" },\n"
		"    { \"name\": \"detonator_link_group\", \"scope\": \"weapon\", "
		"\"source\": \"weapon_instance\", \"type\": \"link_group_ref\", "
		"\"lifetime\": \"player_life\" }\n"
		"  ],\n"
		"  \"subgraphs\": [\n"
		"    { \"id\": \"primary\", \"entry\": \"throw_mine\", "
		"\"shared_context\": [\"owner_player\", \"detonator_link_group\"] },\n"
		"    { \"id\": \"secondary\", \"entry\": \"detonate_mines\", "
		"\"shared_context\": [\"owner_player\", \"detonator_link_group\"] }\n"
		"  ],\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"throw_mine\", \"kind\": \"spawn.thrown_physical\", "
		"\"subgraph\": \"primary\", \"params\": { \"mode\": \"primary\", "
		"\"payload_ref\": \"base:remotemine__entity_armed_remote_mine\", "
		"\"activation_time_ticks60\": 240, "
		"\"context_refs\": [\"owner_player\", \"detonator_link_group\"] } },\n"
		"    { \"id\": \"detonate_mines\", \"kind\": \"special.remote_detonator\", "
		"\"subgraph\": \"secondary\", \"params\": { \"mode\": \"secondary\", "
		"\"detonator_ref\": \"base:remotemine__entity_armed_remote_mine\", "
		"\"context_refs\": [\"owner_player\", \"detonator_link_group\"] } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"throw_mine\" },\n"
		"    { \"name\": \"secondary\", \"node\": \"detonate_mines\" }\n"
		"  ]\n"
		"}\n";

	weapon_graph_ir_t ir;
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);
	REQUIRE(ir.context_count == 2);
	REQUIRE(std::string(ir.contexts[0].name) == "owner_player");
	REQUIRE(std::string(ir.contexts[1].type) == "link_group_ref");
	REQUIRE(ir.subgraph_count == 2);
	REQUIRE(std::string(ir.subgraphs[0].id) == "primary");
	REQUIRE(ir.subgraphs[0].entry_node == 0);
	REQUIRE(std::string(ir.nodes[0].subgraph) == "primary");
	REQUIRE(std::string(ir.nodes[1].subgraph) == "secondary");
	REQUIRE(std::string(ir.ir_sha256).size() == 64);
}

TEST_CASE("weapon graph editor layout metadata does not affect runtime IR hash",
          "[modding][pdxxx][weapon_graph][compiler][layout][c3814]") {
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:falcon2\",\n"
		"  \"graph_id\": \"layout_hash_test\",\n"
		"  \"shared_context\": [\n"
		"    { \"name\": \"owner_player\", \"scope\": \"player\", "
		"\"source\": \"equipped_player\", \"type\": \"player_ref\", "
		"\"lifetime\": \"weapon_instance\" }\n"
		"  ],\n"
		"  \"subgraphs\": [\n"
		"    { \"id\": \"primary\", \"entry\": \"single\", "
		"\"shared_context\": [\"owner_player\"] }\n"
		"  ],\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"single\", \"kind\": \"fire.hitscan\", "
		"\"subgraph\": \"primary\", \"params\": { \"mode\": \"primary\", "
		"\"damage\": 8.0, \"context_refs\": [\"owner_player\"] } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [ { \"name\": \"primary\", \"node\": \"single\" } ]\n"
		"}\n";
	const std::string graphWithLayout =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:falcon2\",\n"
		"  \"graph_id\": \"layout_hash_test\",\n"
		"  \"shared_context\": [\n"
		"    { \"name\": \"owner_player\", \"scope\": \"player\", "
		"\"source\": \"equipped_player\", \"type\": \"player_ref\", "
		"\"lifetime\": \"weapon_instance\" }\n"
		"  ],\n"
		"  \"subgraphs\": [\n"
		"    { \"id\": \"primary\", \"entry\": \"single\", "
		"\"shared_context\": [\"owner_player\"] }\n"
		"  ],\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"single\", \"kind\": \"fire.hitscan\", "
		"\"subgraph\": \"primary\", \"params\": { \"mode\": \"primary\", "
		"\"damage\": 8.0, \"context_refs\": [\"owner_player\"] } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [ { \"name\": \"primary\", \"node\": \"single\" } ],\n"
		"  \"editor\": {\n"
		"    \"layout\": {\n"
		"      \"nodes\": [ { \"id\": \"single\", \"x\": 64.0, \"y\": 88.0 } ]\n"
		"    }\n"
		"  }\n"
		"}\n";

	weapon_graph_ir_t baseIr;
	weapon_graph_ir_t layoutIr;
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &baseIr, err, sizeof(err)) == 0);
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graphWithLayout.data(),
		static_cast<u32>(graphWithLayout.size()), &layoutIr, err, sizeof(err)) == 0);
	REQUIRE(std::string(baseIr.ir_sha256) == std::string(layoutIr.ir_sha256));
	REQUIRE(std::string(baseIr.source_sha256) != std::string(layoutIr.source_sha256));
}

TEST_CASE("weapon graph runtime compiler rejects unsafe or ambiguous graphs",
          "[modding][pdxxx][weapon_graph][compiler][c3814]") {
	char err[256] = {};
	weapon_graph_ir_t ir;

	const std::string unsupported =
		"{\"schema\":\"pd.weapon_graph.v1\",\"asset_id\":\"base:test\","
		"\"graph_id\":\"bad\",\"nodes\":[{\"id\":\"n\",\"kind\":\"script.lua\","
		"\"params\":{}}],\"edges\":[],\"exports\":[]}";
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, unsupported.data(),
		static_cast<u32>(unsupported.size()), &ir, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("unsupported") != std::string::npos);

	const std::string badUnit =
		"{\"schema\":\"pd.weapon_graph.v1\",\"asset_id\":\"base:test\","
		"\"graph_id\":\"bad_unit\",\"nodes\":[{\"id\":\"n\","
		"\"kind\":\"fire.hitscan\",\"params\":{\"timer\":5}}],"
		"\"edges\":[],\"exports\":[]}";
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, badUnit.data(),
		static_cast<u32>(badUnit.size()), &ir, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("units") != std::string::npos);

	const std::string badRef =
		"{\"schema\":\"pd.weapon_graph.v1\",\"asset_id\":\"base:test\","
		"\"graph_id\":\"bad_ref\",\"nodes\":[{\"id\":\"n\","
		"\"kind\":\"spawn.fired_projectile\","
		"\"params\":{\"projectile_ref\":\"rocket\"}}],"
		"\"edges\":[],\"exports\":[]}";
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, badRef.data(),
		static_cast<u32>(badRef.size()), &ir, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("catalog id") != std::string::npos);

	const std::string cycle =
		"{\"schema\":\"pd.projectile_graph.v1\",\"asset_id\":\"base:p\","
		"\"graph_id\":\"cycle\",\"nodes\":["
		"{\"id\":\"a\",\"kind\":\"projectile.spawn_state\",\"params\":{}},"
		"{\"id\":\"b\",\"kind\":\"projectile.motion\",\"params\":{}}],"
		"\"edges\":[{\"from\":\"a\",\"to\":\"b\"},{\"from\":\"b\",\"to\":\"a\"}],"
		"\"exports\":[]}";
	REQUIRE(weaponGraphCompileJson(ASSET_PROJECTILE, cycle.data(),
		static_cast<u32>(cycle.size()), &ir, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("cycle") != std::string::npos);

	const std::string badSubgraph =
		"{\"schema\":\"pd.weapon_graph.v1\",\"asset_id\":\"base:test\","
		"\"graph_id\":\"bad_subgraph\","
		"\"shared_context\":[{\"name\":\"owner_player\",\"scope\":\"player\","
		"\"source\":\"equipped_player\"}],"
		"\"subgraphs\":[{\"id\":\"primary\",\"entry\":\"missing\"}],"
		"\"nodes\":[{\"id\":\"n\",\"kind\":\"fire.hitscan\",\"params\":{}}],"
		"\"edges\":[],\"exports\":[]}";
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, badSubgraph.data(),
		static_cast<u32>(badSubgraph.size()), &ir, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("missing entry") != std::string::npos);

	const std::string dupContext =
		"{\"schema\":\"pd.weapon_graph.v1\",\"asset_id\":\"base:test\","
		"\"graph_id\":\"dup_context\","
		"\"shared_context\":["
		"{\"name\":\"owner_player\",\"scope\":\"player\",\"source\":\"a\"},"
		"{\"name\":\"owner_player\",\"scope\":\"player\",\"source\":\"b\"}],"
		"\"nodes\":[{\"id\":\"n\",\"kind\":\"fire.hitscan\",\"params\":{}}],"
		"\"edges\":[],\"exports\":[]}";
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, dupContext.data(),
		static_cast<u32>(dupContext.size()), &ir, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("duplicate shared context") != std::string::npos);
}

TEST_CASE("weapon graph runtime cutover retires the user-facing toggle",
          "[modding][pdxxx][weapon_graph][cutover][c3849]") {
	const std::string runtime = readFile("port/src/weapon_graph_runtime.c");
	REQUIRE(runtime.find("static s32 s_runtime_enabled = 1;") != std::string::npos);
	REQUIRE(runtime.find("#ifndef PD_TESTS") != std::string::npos);
	REQUIRE(runtime.find("return 1;") != std::string::npos);
	REQUIRE(runtime.find("Debug.WeaponGraphRuntime") == std::string::npos);
	REQUIRE(runtime.find("configRegisterInt(\"Debug.WeaponGraphRuntime\"") == std::string::npos);
	REQUIRE(runtime.find("weaponGraphRuntimeEnabled") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphRuntimeSetEnabled") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphRuntimeNetLatchEnabled") == std::string::npos);

	const std::string settings = readFile("port/fast3d/pdgui_menu_mainmenu.cpp");
	REQUIRE(settings.find("Weapon Graph Runtime") == std::string::npos);
	REQUIRE(settings.find("weaponGraphRuntimeEnabled()") == std::string::npos);
	REQUIRE(settings.find("weaponGraphRuntimeSetEnabled") == std::string::npos);
}

TEST_CASE("weapon graph runtime registers held weapon IR for gameplay gate",
          "[modding][pdxxx][weapon_graph][runtime][held][c3814]") {
	ScopedGraphCatalogSources sources(nullptr, 0, nullptr, 0,
		"base:sfx_test_shot", SFX_804D);
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_weapon\",\n"
		"  \"graph_id\": \"held_runtime_test_v1\",\n"
		"  \"nodes\": [\n"
		"    {\n"
		"      \"id\": \"primary_action\",\n"
		"      \"kind\": \"fire.hitscan\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"primary\",\n"
		"        \"function_type\": \"shoot_single\",\n"
		"        \"ammo_slot\": 0,\n"
		"        \"flags\": 64,\n"
		"        \"damage\": 7.5,\n"
		"        \"spread\": 1.25,\n"
		"        \"recoil_anim_unk24\": 2,\n"
		"        \"recoil_anim_unk25\": 3,\n"
		"        \"recoil_anim_unk26\": 4,\n"
		"        \"recoil_anim_unk27\": 5,\n"
		"        \"recoildist\": 6.5,\n"
		"        \"recoilangle\": 7.5,\n"
		"        \"slidemax\": 8.5,\n"
		"        \"impactforce\": 2.25,\n"
		"        \"duration_ticks60\": 5,\n"
		"        \"shoot_sound_catalog_id\": \"base:sfx_test_shot\",\n"
		"        \"penetration\": 3\n"
		"      }\n"
		"    },\n"
		"    {\n"
		"      \"id\": \"secondary_action\",\n"
		"      \"kind\": \"fire.auto_cadence\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"secondary\",\n"
		"        \"function_type\": \"shoot_automatic\",\n"
		"        \"initial_rpm\": 300,\n"
		"        \"turret_accel\": 8,\n"
		"        \"turret_decel\": 12,\n"
		"        \"max_rpm\": 900\n"
		"      }\n"
		"    }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"primary_action\" },\n"
		"    { \"name\": \"secondary\", \"node\": \"secondary_action\" }\n"
		"  ]\n"
		"}\n";

	weapon_graph_ir_t ir;
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);

	weaponGraphRuntimeClearAll();
	weaponGraphRuntimeSetEnabled(0);
	REQUIRE(weaponGraphRuntimeRegisterHeldIr(7, &ir, err, sizeof(err)) == 0);
	REQUIRE(weaponGraphRuntimeGetHeldFunctionForGameplay(7, 0) == nullptr);

	weaponGraphRuntimeSetEnabled(1);
	const weapon_graph_held_function_t *primary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(7, 0);
	const weapon_graph_held_function_t *secondary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(7, 1);
	REQUIRE(primary != nullptr);
	REQUIRE(secondary != nullptr);
	REQUIRE(primary->opcode == WEAPON_GRAPH_OP_FIRE_HITSCAN);
	REQUIRE(primary->function_type_id == INVENTORYFUNCTYPE_SHOOT_SINGLE);
	REQUIRE(primary->has_damage == 1);
	REQUIRE(primary->damage == Approx(7.5f));
	REQUIRE(primary->has_spread == 1);
	REQUIRE(primary->spread == Approx(1.25f));
	REQUIRE(primary->has_recoil_anim_unk24 == 1);
	REQUIRE(primary->recoil_anim_unk24 == 2);
	REQUIRE(primary->has_recoil_anim_unk25 == 1);
	REQUIRE(primary->recoil_anim_unk25 == 3);
	REQUIRE(primary->has_recoil_anim_unk26 == 1);
	REQUIRE(primary->recoil_anim_unk26 == 4);
	REQUIRE(primary->has_recoil_anim_unk27 == 1);
	REQUIRE(primary->recoil_anim_unk27 == 5);
	REQUIRE(primary->has_recoildist == 1);
	REQUIRE(primary->recoildist == Approx(6.5f));
	REQUIRE(primary->has_recoilangle == 1);
	REQUIRE(primary->recoilangle == Approx(7.5f));
	REQUIRE(primary->has_slidemax == 1);
	REQUIRE(primary->slidemax == Approx(8.5f));
	REQUIRE(primary->has_impactforce == 1);
	REQUIRE(primary->impactforce == Approx(2.25f));
	REQUIRE(primary->has_duration_ticks60 == 1);
	REQUIRE(primary->duration_ticks60 == 5);
	REQUIRE(primary->has_shootsound == 1);
	REQUIRE(primary->shootsound == SFX_804D);
	REQUIRE(primary->has_penetration == 1);
	REQUIRE(primary->penetration == 3);
	REQUIRE(primary->flags == 64);
	REQUIRE(secondary->opcode == WEAPON_GRAPH_OP_FIRE_AUTO_CADENCE);
	REQUIRE(secondary->has_initial_rpm == 1);
	REQUIRE(secondary->initial_rpm == Approx(300.0f));
	REQUIRE(secondary->has_max_rpm == 1);
	REQUIRE(secondary->max_rpm == Approx(900.0f));
	REQUIRE(secondary->has_turret_accel == 1);
	REQUIRE(secondary->turret_accel == 8);
	REQUIRE(secondary->has_turret_decel == 1);
	REQUIRE(secondary->turret_decel == 12);

	weaponGraphRuntimeSetEnabled(0);
	weaponGraphRuntimeClearAll();
}

TEST_CASE("weapon graph runtime captures projectile and entity adapter payloads",
          "[modding][pdxxx][weapon_graph][runtime][projectile][entity][c3814]") {
	ScopedGraphCatalogSources sources("base:model_chrdyrocketmis", MODEL_CHRDYROCKETMIS,
		"base:model_chrautogun", MODEL_CHRAUTOGUN,
		"base:sfx_launch_rocket", SFX_LAUNCH_ROCKET_8053);
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_projectile_weapon\",\n"
		"  \"graph_id\": \"projectile_runtime_test_v1\",\n"
		"  \"nodes\": [\n"
		"    {\n"
		"      \"id\": \"primary_action\",\n"
		"      \"kind\": \"spawn.fired_projectile\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"primary\",\n"
		"        \"function_type\": \"shoot_projectile\",\n"
		"        \"ammo_slot\": 0,\n"
		"        \"flags\": 134217728,\n"
		"        \"projectile_ref\": \"base:dyrocket_projectile\",\n"
		"        \"projectile_model_catalog_id\": \"base:model_chrdyrocketmis\",\n"
		"        \"scale\": 1.5,\n"
		"        \"speed\": 300,\n"
		"        \"travel_distance\": 900,\n"
		"        \"timer_ticks60\": 120,\n"
		"        \"reflect_angle\": 0.75,\n"
		"        \"projectile_sound_catalog_id\": \"base:sfx_launch_rocket\"\n"
		"      }\n"
		"    },\n"
		"    {\n"
		"      \"id\": \"secondary_action\",\n"
		"      \"kind\": \"spawn.thrown_physical\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"secondary\",\n"
		"        \"function_type\": \"throw\",\n"
		"        \"ammo_slot\": 1,\n"
		"        \"payload_ref\": \"base:laptop_autogun\",\n"
		"        \"entity_ref\": \"base:laptop_autogun\",\n"
		"        \"projectile_model_catalog_id\": \"base:model_chrautogun\",\n"
		"        \"activation_time_ticks60\": 90,\n"
		"        \"recovery_time_ticks60\": 45,\n"
		"        \"damage\": 12.5\n"
		"      }\n"
		"    }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"primary_action\" },\n"
		"    { \"name\": \"secondary\", \"node\": \"secondary_action\" }\n"
		"  ]\n"
		"}\n";

	weapon_graph_ir_t ir;
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);

	weaponGraphRuntimeClearAll();
	weaponGraphRuntimeSetEnabled(1);
	REQUIRE(weaponGraphRuntimeRegisterHeldIr(8, &ir, err, sizeof(err)) == 0);

	const weapon_graph_held_function_t *primary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(8, 0);
	const weapon_graph_held_function_t *secondary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(8, 1);
	REQUIRE(primary != nullptr);
	REQUIRE(secondary != nullptr);
	REQUIRE(primary->opcode == WEAPON_GRAPH_OP_SPAWN_FIRED_PROJECTILE);
	REQUIRE(primary->function_type_id == INVENTORYFUNCTYPE_SHOOT_PROJECTILE);
	REQUIRE(primary->has_projectile_modelnum == 1);
	REQUIRE(primary->projectile_modelnum == MODEL_CHRDYROCKETMIS);
	REQUIRE(std::string(primary->projectile_ref) == "base:dyrocket_projectile");
	REQUIRE(primary->has_scale == 1);
	REQUIRE(primary->scale == Approx(1.5f));
	REQUIRE(primary->has_speed == 1);
	REQUIRE(primary->speed == Approx(300.0f));
	REQUIRE(primary->has_travel_distance == 1);
	REQUIRE(primary->travel_distance == 900);
	REQUIRE(primary->has_timer_ticks60 == 1);
	REQUIRE(primary->timer_ticks60 == 120);
	REQUIRE(primary->has_reflect_angle == 1);
	REQUIRE(primary->reflect_angle == Approx(0.75f));
	REQUIRE(primary->has_soundnum == 1);
	REQUIRE(primary->soundnum == SFX_LAUNCH_ROCKET_8053);
	REQUIRE(secondary->opcode == WEAPON_GRAPH_OP_SPAWN_THROWN_PHYSICAL);
	REQUIRE(secondary->function_type_id == INVENTORYFUNCTYPE_THROW);
	REQUIRE(secondary->has_projectile_modelnum == 1);
	REQUIRE(secondary->projectile_modelnum == MODEL_CHRAUTOGUN);
	REQUIRE(std::string(secondary->payload_ref) == "base:laptop_autogun");
	REQUIRE(std::string(secondary->entity_ref) == "base:laptop_autogun");
	REQUIRE(secondary->has_activation_time_ticks60 == 1);
	REQUIRE(secondary->activation_time_ticks60 == 90);
	REQUIRE(secondary->has_recovery_time_ticks60 == 1);
	REQUIRE(secondary->recovery_time_ticks60 == 45);

	weaponGraphRuntimeSetEnabled(0);
	weaponGraphRuntimeClearAll();
}

TEST_CASE("weapon graph runtime registers projectile and entity behavior assets",
          "[modding][pdxxx][weapon_graph][runtime][projectile][entity][c3814][c3838]") {
	ScopedGraphCatalogSources sources("base:model_chrskrocketmis", MODEL_CHRSKROCKETMIS,
		"base:model_chrproximitymine", MODEL_CHRPROXIMITYMINE,
		"base:sfx_launch_rocket", SFX_LAUNCH_ROCKET_8053);
	const std::string projectileGraph =
		"{\n"
		"  \"schema\": \"pd.projectile_graph.v1\",\n"
		"  \"asset_id\": \"base:test_projectile_payload\",\n"
		"  \"graph_id\": \"projectile_asset_runtime_test_v1\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"spawn\", \"kind\": \"projectile.spawn_state\", \"params\": {\n"
		"      \"model_catalog_id\": \"base:model_chrskrocketmis\",\n"
		"      \"model_archive\": \"dependencies/assets/model/visual.pdmesh\",\n"
		"      \"source_mode\": \"secondary\",\n"
		"      \"source_function_type\": \"shoot_projectile\",\n"
		"      \"scale\": 2.1,\n"
		"      \"damage\": 7,\n"
		"      \"flags\": 72\n"
		"    } },\n"
		"    { \"id\": \"motion\", \"kind\": \"projectile.motion\", \"params\": {\n"
		"      \"motion_kind\": \"powered\",\n"
		"      \"speed\": 60,\n"
		"      \"travel_distance\": 5,\n"
		"      \"timer60\": -1,\n"
		"      \"activation_time60\": 0,\n"
		"      \"recovery_time60\": 0,\n"
		"      \"reflect_angle\": 0.05,\n"
		"      \"powered\": true,\n"
		"      \"calculate_trajectory\": false\n"
		"    } },\n"
		"    { \"id\": \"homing\", \"kind\": \"projectile.homing\", \"params\": {\n"
		"      \"target_source\": \"current_lock\",\n"
		"      \"runtime_constants\": \"extract_in_runtime_adapter\"\n"
		"    } },\n"
		"    { \"id\": \"fly\", \"kind\": \"projectile.fly_by_wire\", \"params\": {\n"
		"      \"control_source\": \"owner\",\n"
		"      \"bot_route_policy\": \"runtime_existing\",\n"
		"      \"turn_rate\": 1.5\n"
		"    } },\n"
		"    { \"id\": \"wall\", \"kind\": \"projectile.wall_hugger\", \"params\": {\n"
		"      \"stick_surface_filter\": \"background\",\n"
		"      \"post_fall_timer60\": 360\n"
		"    } },\n"
		"    { \"id\": \"impact\", \"kind\": \"projectile.impact\", \"params\": {\n"
		"      \"impact_filter\": \"any\",\n"
		"      \"hit_sound\": \"base:sfx_launch_rocket\",\n"
		"      \"consume_on_hit\": true,\n"
		"      \"stick_on_hit\": false\n"
		"    } },\n"
		"    { \"id\": \"transition\", \"kind\": \"projectile.transition_to_entity\", \"params\": {\n"
		"      \"entity_ref\": \"base:test_entity_payload\",\n"
		"      \"when\": \"at_rest\",\n"
		"      \"transfer_owner\": true,\n"
		"      \"transfer_ammo\": true,\n"
		"      \"transfer_position\": true,\n"
		"      \"delete_carrier\": true\n"
		"    } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [ { \"name\": \"main\", \"node\": \"transition\" } ]\n"
		"}\n";
	const std::string entityGraph =
		"{\n"
		"  \"schema\": \"pd.entity_graph.v1\",\n"
		"  \"asset_id\": \"base:test_entity_payload\",\n"
		"  \"graph_id\": \"entity_asset_runtime_test_v1\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"armed\", \"kind\": \"entity.armed_explosive\", \"params\": {\n"
		"      \"archetype\": \"armed_proxy_explosive\",\n"
		"      \"model_catalog_id\": \"base:model_chrproximitymine\",\n"
		"      \"model_archive\": \"dependencies/assets/model/visual.pdmesh\",\n"
		"      \"source_mode\": \"secondary\",\n"
		"      \"activation_time60\": 240,\n"
		"      \"recovery_time60\": 60,\n"
		"      \"flags\": 262144,\n"
		"      \"arm_delay_ticks60\": 240,\n"
		"      \"detonation_policy\": \"explode_once\",\n"
		"      \"delete_on_detonate\": true\n"
		"    } },\n"
		"    { \"id\": \"proxy\", \"kind\": \"entity.proxy_trigger\", \"params\": {\n"
		"      \"radius\": 300,\n"
		"      \"target_filter\": \"hostile_chr\",\n"
		"      \"team_filter\": \"enemy_only\",\n"
		"      \"line_of_sight\": true,\n"
		"      \"on_trigger\": \"detonate\"\n"
		"    } },\n"
		"    { \"id\": \"remote\", \"kind\": \"entity.remote_detonatable\", \"params\": {\n"
		"      \"detonator_ref\": \"base:remote_mine_detonator\",\n"
		"      \"owner_slot_source\": \"owner_inventory\",\n"
		"      \"on_remote_signal\": \"detonate\"\n"
		"    } },\n"
		"    { \"id\": \"timed\", \"kind\": \"entity.timed_detonatable\", \"params\": {\n"
		"      \"timer_ticks60\": 180,\n"
		"      \"starts_when\": \"armed\",\n"
		"      \"on_expire\": \"detonate\"\n"
		"    } },\n"
		"    { \"id\": \"storm\", \"kind\": \"entity.nbomb_storm\", \"params\": {\n"
		"      \"storm_ref\": \"base:nbomb_storm\",\n"
		"      \"activation_policy\": \"on_timer_or_proxy\",\n"
		"      \"delete_carrier\": true\n"
		"    } },\n"
		"    { \"id\": \"autogun\", \"kind\": \"entity.autogun\", \"params\": {\n"
		"      \"target_filter\": \"hostile_chr\",\n"
		"      \"team_policy\": \"owner_team\",\n"
		"      \"aim_distance\": 4000,\n"
		"      \"turn_speed\": 6,\n"
		"      \"fire_cadence\": 1000,\n"
		"      \"ammo_reserve\": 400,\n"
		"      \"net_authority\": \"server\",\n"
		"      \"friendly_fire_suppression\": true,\n"
		"      \"pickup_recover\": true\n"
		"    } },\n"
		"    { \"id\": \"cleanup\", \"kind\": \"entity.owner_cleanup\", \"params\": {\n"
		"      \"replace_existing_policy\": \"explode_previous\",\n"
		"      \"max_active_per_owner\": 1\n"
		"    } },\n"
		"    { \"id\": \"interaction\", \"kind\": \"entity.interaction\", \"params\": {\n"
		"      \"interact_filter\": \"owner\",\n"
		"      \"action\": \"recover_weapon\",\n"
		"      \"transfer_payload\": \"base:laptopgun\"\n"
		"    } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [ { \"name\": \"main\", \"node\": \"armed\" } ]\n"
		"}\n";

	char err[256] = {};
	weaponGraphRuntimeClearAll();
	weaponGraphRuntimeSetEnabled(0);
	REQUIRE(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE,
		"base:test_projectile_payload", projectileGraph.data(),
		static_cast<u32>(projectileGraph.size()), err, sizeof(err)) == 0);
	REQUIRE(weaponGraphRuntimeGetProjectileForGameplay(
		"base:test_projectile_payload") == nullptr);
	weaponGraphRuntimeSetEnabled(1);
	const weapon_graph_projectile_runtime_t *projectile =
		weaponGraphRuntimeGetProjectileForGameplay("base:test_projectile_payload");
	REQUIRE(projectile != nullptr);
	REQUIRE(projectile->has_projectile_modelnum == 1);
	REQUIRE(projectile->projectile_modelnum == MODEL_CHRSKROCKETMIS);
	REQUIRE(std::string(projectile->source_mode) == "secondary");
	REQUIRE(projectile->source_function_type_id == INVENTORYFUNCTYPE_SHOOT_PROJECTILE);
	REQUIRE(projectile->has_scale == 1);
	REQUIRE(projectile->scale == Approx(2.1f));
	REQUIRE(projectile->has_damage == 1);
	REQUIRE(projectile->damage == Approx(7.0f));
	REQUIRE(projectile->flags == 72);
	REQUIRE(std::string(projectile->motion_kind) == "powered");
	REQUIRE(projectile->has_speed == 1);
	REQUIRE(projectile->speed == Approx(60.0f));
	REQUIRE(projectile->has_timer60 == 1);
	REQUIRE(projectile->timer60 == -1);
	REQUIRE(projectile->powered == 1);
	REQUIRE(projectile->calculate_trajectory == 0);
	REQUIRE(projectile->has_homing == 1);
	REQUIRE(std::string(projectile->homing_target_source) == "current_lock");
	REQUIRE(projectile->has_fly_by_wire == 1);
	REQUIRE(std::string(projectile->fly_bot_route_policy) == "runtime_existing");
	REQUIRE(projectile->has_wall_hugger == 1);
	REQUIRE(projectile->wall_post_fall_timer60 == 360);
	REQUIRE(projectile->has_impact == 1);
	REQUIRE(projectile->impact_hit_sound == SFX_LAUNCH_ROCKET_8053);
	REQUIRE(projectile->has_transition_to_entity == 1);
	REQUIRE(std::string(projectile->entity_ref) == "base:test_entity_payload");
	REQUIRE(projectile->transfer_owner == 1);
	REQUIRE(projectile->delete_carrier == 1);

	REQUIRE(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_ENTITY,
		"base:test_entity_payload", entityGraph.data(),
		static_cast<u32>(entityGraph.size()), err, sizeof(err)) == 0);
	const weapon_graph_entity_runtime_t *entity =
		weaponGraphRuntimeGetEntityForGameplay("base:test_entity_payload");
	REQUIRE(entity != nullptr);
	REQUIRE(entity->has_armed_explosive == 1);
	REQUIRE(std::string(entity->archetype) == "armed_proxy_explosive");
	REQUIRE(entity->projectile_modelnum == MODEL_CHRPROXIMITYMINE);
	REQUIRE(entity->activation_time60 == 240);
	REQUIRE(entity->recovery_time60 == 60);
	REQUIRE(entity->arm_delay_ticks60 == 240);
	REQUIRE(entity->delete_on_detonate == 1);
	REQUIRE(entity->has_proxy_trigger == 1);
	REQUIRE(entity->proxy_radius == Approx(300.0f));
	REQUIRE(entity->proxy_line_of_sight == 1);
	REQUIRE(entity->has_remote_detonatable == 1);
	REQUIRE(std::string(entity->detonator_ref) == "base:remote_mine_detonator");
	REQUIRE(entity->has_timed_detonatable == 1);
	REQUIRE(entity->timed_timer_ticks60 == 180);
	REQUIRE(entity->has_nbomb_storm == 1);
	REQUIRE(entity->storm_delete_carrier == 1);
	REQUIRE(entity->has_autogun == 1);
	REQUIRE(entity->autogun_ammo_reserve == 400);
	REQUIRE(entity->autogun_friendly_fire_suppression == 1);
	REQUIRE(entity->has_owner_cleanup == 1);
	REQUIRE(entity->max_active_per_owner == 1);
	REQUIRE(entity->has_interaction == 1);
	REQUIRE(std::string(entity->transfer_payload) == "base:laptopgun");

	const std::string heldGraph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_runtime_bridge_weapon\",\n"
		"  \"graph_id\": \"runtime_bridge_refs_v1\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"primary\", \"kind\": \"spawn.fired_projectile\", \"params\": {\n"
		"      \"mode\": \"primary\",\n"
		"      \"function_type\": \"shoot_projectile\",\n"
		"      \"projectile_ref\": \"base:test_projectile_payload\"\n"
		"    } },\n"
		"    { \"id\": \"secondary\", \"kind\": \"spawn.thrown_physical\", \"params\": {\n"
		"      \"mode\": \"secondary\",\n"
		"      \"function_type\": \"throw\",\n"
		"      \"payload_ref\": \"base:test_entity_payload\",\n"
		"      \"entity_ref\": \"base:test_entity_payload\"\n"
		"    } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"primary\" },\n"
		"    { \"name\": \"secondary\", \"node\": \"secondary\" }\n"
		"  ]\n"
		"}\n";
	weapon_graph_ir_t heldIr;
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, heldGraph.data(),
		static_cast<u32>(heldGraph.size()), &heldIr, err, sizeof(err)) == 0);
	REQUIRE(weaponGraphRuntimeRegisterHeldIr(10, &heldIr, err, sizeof(err)) == 0);
	const weapon_graph_held_function_t *heldPrimary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(10, 0);
	const weapon_graph_held_function_t *heldSecondary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(10, 1);
	REQUIRE(weaponGraphRuntimeGetProjectileForHeldFunction(heldPrimary) == projectile);
	REQUIRE(weaponGraphRuntimeGetEntityForHeldFunction(heldSecondary) == entity);
	REQUIRE(weaponGraphRuntimeGetEntityForProjectile(projectile) == entity);

	const std::string projectileIni =
		"[projectile]\n"
		"catalog_id = base:test_projectile_payload\n"
		"behavior_graph = behavior.graph.json\n";
	TempArchive projectileArchive = writeArchiveEntries("projectile-runtime", {
		{ "projectile.ini", projectileIni },
		{ "behavior.graph.json", projectileGraph },
	});
	weaponGraphRuntimeClearAsset("base:test_projectile_payload");
	REQUIRE(weaponGraphRuntimeRegisterBehaviorArchive(ASSET_PROJECTILE,
		projectileArchive.path.string().c_str(), err, sizeof(err)) == 0);
	REQUIRE(weaponGraphRuntimeGetProjectile(
		"base:test_projectile_payload") != nullptr);

	const std::string entityIni =
		"[entity]\n"
		"catalog_id = base:test_entity_payload\n"
		"behavior_graph = behavior.graph.json\n";
	TempArchive entityArchive = writeArchiveEntries("entity-runtime", {
		{ "entity.ini", entityIni },
		{ "behavior.graph.json", entityGraph },
	});
	weaponGraphRuntimeClearAsset("base:test_entity_payload");
	REQUIRE(weaponGraphRuntimeRegisterBehaviorArchive(ASSET_ENTITY,
		entityArchive.path.string().c_str(), err, sizeof(err)) == 0);
	REQUIRE(weaponGraphRuntimeGetEntity("base:test_entity_payload") != nullptr);

	weaponGraphRuntimeSetEnabled(0);
	weaponGraphRuntimeClearAll();
}

TEST_CASE("weapon graph runtime captures special and device adapter payloads",
          "[modding][pdxxx][weapon_graph][runtime][special][device][c3814]") {
	ScopedGraphCatalogSources sources(nullptr, 0, nullptr, 0,
		"base:sfx_launch_rocket", SFX_LAUNCH_ROCKET_8053);
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_device_weapon\",\n"
		"  \"graph_id\": \"special_device_runtime_test_v1\",\n"
		"  \"nodes\": [\n"
		"    {\n"
		"      \"id\": \"primary_action\",\n"
		"      \"kind\": \"special.remote_detonator\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"primary\",\n"
		"        \"function_type\": \"special\",\n"
		"        \"ammo_slot\": 0,\n"
		"        \"specialfunc\": 5,\n"
		"        \"recovery_time_ticks60\": 30,\n"
		"        \"special_sound_catalog_id\": \"base:sfx_launch_rocket\"\n"
		"      }\n"
		"    },\n"
		"    {\n"
		"      \"id\": \"secondary_action\",\n"
		"      \"kind\": \"device.activate\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"secondary\",\n"
		"        \"function_type\": \"device\",\n"
		"        \"device\": 64\n"
		"      }\n"
		"    }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"primary_action\" },\n"
		"    { \"name\": \"secondary\", \"node\": \"secondary_action\" }\n"
		"  ]\n"
		"}\n";

	weapon_graph_ir_t ir;
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);

	weaponGraphRuntimeClearAll();
	weaponGraphRuntimeSetEnabled(1);
	REQUIRE(weaponGraphRuntimeRegisterHeldIr(9, &ir, err, sizeof(err)) == 0);

	const weapon_graph_held_function_t *primary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(9, 0);
	const weapon_graph_held_function_t *secondary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(9, 1);
	REQUIRE(primary != nullptr);
	REQUIRE(secondary != nullptr);
	REQUIRE(primary->opcode == WEAPON_GRAPH_OP_SPECIAL_REMOTE_DETONATOR);
	REQUIRE(primary->function_type_id == INVENTORYFUNCTYPE_SPECIAL);
	REQUIRE(primary->has_specialfunc == 1);
	REQUIRE(primary->specialfunc == HANDATTACKTYPE_DETONATE);
	REQUIRE(primary->has_recovery_time_ticks60 == 1);
	REQUIRE(primary->recovery_time_ticks60 == 30);
	REQUIRE(primary->has_soundnum == 1);
	REQUIRE(primary->soundnum == SFX_LAUNCH_ROCKET_8053);
	REQUIRE(secondary->opcode == WEAPON_GRAPH_OP_DEVICE_ACTIVATE);
	REQUIRE(secondary->function_type_id == INVENTORYFUNCTYPE_DEVICE);
	REQUIRE(secondary->has_device == 1);
	REQUIRE(secondary->device == DEVICE_CLOAKDEVICE);

	weaponGraphRuntimeSetEnabled(0);
	weaponGraphRuntimeClearAll();
}

TEST_CASE("weapon graph runtime captures presentation adapter payloads",
          "[modding][pdxxx][weapon_graph][runtime][presentation][c3814]") {
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_presentation_weapon\",\n"
		"  \"graph_id\": \"presentation_runtime_test_v1\",\n"
		"  \"nodes\": [\n"
		"    {\n"
		"      \"id\": \"primary_presentation\",\n"
		"      \"kind\": \"presentation.reticle_overlay_camera\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"primary\",\n"
		"        \"function_type\": \"none\",\n"
		"        \"sight\": \"zoom\",\n"
		"        \"zoom_fov\": 12.5,\n"
		"        \"reticle_ref\": \"base:hud_zoom_reticle\",\n"
		"        \"overlay_ref\": \"base:hud_zoom_overlay\",\n"
		"        \"camera_effect\": \"none\"\n"
		"      }\n"
		"    }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"primary_presentation\" }\n"
		"  ]\n"
		"}\n";

	weapon_graph_ir_t ir;
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);

	weaponGraphRuntimeClearAll();
	weaponGraphRuntimeSetEnabled(1);
	REQUIRE(weaponGraphRuntimeRegisterHeldIr(11, &ir, err, sizeof(err)) == 0);

	const weapon_graph_held_function_t *primary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(11, 0);
	REQUIRE(primary != nullptr);
	REQUIRE(primary->opcode ==
		WEAPON_GRAPH_OP_PRESENTATION_RETICLE_OVERLAY_CAMERA);
	REQUIRE(primary->has_sight == 1);
	REQUIRE(primary->sight == SIGHT_ZOOM);
	REQUIRE(primary->has_zoom_fov == 1);
	REQUIRE(primary->zoom_fov == Approx(12.5f));
	REQUIRE(std::string(primary->reticle_ref) == "base:hud_zoom_reticle");
	REQUIRE(std::string(primary->overlay_ref) == "base:hud_zoom_overlay");
	REQUIRE(std::string(primary->camera_effect) == "none");

	weaponGraphRuntimeSetEnabled(0);
	weaponGraphRuntimeClearAll();
}

TEST_CASE("held weapon graph adapter is wired into runtime callsites",
          "[modding][pdxxx][weapon_graph][runtime][static][c3814]") {
	const std::string walker = readFile("port/src/loader_walker_weapon.c");
	REQUIRE(walker.find("weaponGraphRuntimeRegisterWeaponArchive") != std::string::npos);
	REQUIRE(walker.find("behavior.graph.json") == std::string::npos);

	const std::string accessors = readFile("src/game/game_0b0fd0.c");
	REQUIRE(accessors.find("weapon_graph_runtime.h") != std::string::npos);
	REQUIRE(accessors.find("weaponGraphRuntimeGetHeldFunctionForGameplay") != std::string::npos);
	REQUIRE(accessors.find("gsetGetDamage") != std::string::npos);
	REQUIRE(accessors.find("weaponGetNumTicksPerShot") != std::string::npos);
	REQUIRE(accessors.find("graph->function_type_id") != std::string::npos);
	REQUIRE(accessors.find("graph->has_device") != std::string::npos);
	REQUIRE(accessors.find("graph->has_zoom_fov") != std::string::npos);
	REQUIRE(accessors.find("graph->has_sight") != std::string::npos);

	const std::string bondgun = readFile("src/game/bondgun.c");
	REQUIRE(bondgun.find("weapon_graph_runtime.h") != std::string::npos);
	REQUIRE(bondgun.find("bgunGetHeldGraph") != std::string::npos);
	REQUIRE(bondgun.find("bgunGetProjectileGraph") != std::string::npos);
	REQUIRE(bondgun.find("bgunGetEntityGraph") != std::string::npos);
	REQUIRE(bondgun.find("projectileApplyGraphRuntime") != std::string::npos);
	REQUIRE(bondgun.find("graph->has_max_rpm") != std::string::npos);
	REQUIRE(bondgun.find("bgunGetAmmoIndexFromGraph") != std::string::npos);
	REQUIRE(bondgun.find("graph->has_recovery_time_ticks60") != std::string::npos);
	REQUIRE(bondgun.find("graph->has_projectile_modelnum") != std::string::npos);

	const std::string bondmove = readFile("src/game/bondmove.c");
	REQUIRE(bondmove.find("weapon_graph_runtime.h") != std::string::npos);
	REQUIRE(bondmove.find("graph->function_type_id") != std::string::npos);

	const std::string botact = readFile("src/game/botact.c");
	REQUIRE(botact.find("weapon_graph_runtime.h") != std::string::npos);
	REQUIRE(botact.find("graph->recoil_anim_unk24 + graph->recoil_anim_unk25") != std::string::npos);

	const std::string chraction = readFile("src/game/chraction.c");
	REQUIRE(chraction.find("weapon_graph_runtime.h") != std::string::npos);
	REQUIRE(chraction.find("weaponGraphRuntimeGetProjectileForHeldFunction") != std::string::npos);
	REQUIRE(chraction.find("graph->has_projectile_modelnum") != std::string::npos);
	REQUIRE(chraction.find("projectileApplyGraphRuntime") != std::string::npos);

	const std::string propobj = readFile("src/game/propobj.c");
	REQUIRE(propobj.find("weapon_graph_runtime.h") != std::string::npos);
	REQUIRE(propobj.find("projectileApplyGraphRuntime") != std::string::npos);
	REQUIRE(propobj.find("weaponGraphRuntimeGetEntityForHeldFunction") != std::string::npos);
	REQUIRE(propobj.find("entitygraph->proxy_radius") != std::string::npos);
	REQUIRE(propobj.find("entitygraph->autogun_aim_distance") != std::string::npos);
	/* B-912 first slice: the projectile-impact IR's production consumer +
	 * B-914 homing widen must stay wired in propobj.c. */
	REQUIRE(propobj.find("weaponGetContactImpactGraph") != std::string::npos);
	REQUIRE(propobj.find("impact_consume_on_hit") != std::string::npos);
	REQUIRE(propobj.find("PROJECTILEFLAG_HOMING") != std::string::npos);
	REQUIRE(propobj.find("gsetHasFunctionFlags(&homingwobj->gset, FUNCFLAG_HOMINGROCKET)") != std::string::npos);

	const std::string runtime = readFile("port/src/weapon_graph_runtime.c");
	REQUIRE(runtime.find("weaponGraphRuntimeGetProjectileForHeldFunction") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphRuntimeGetEntityForHeldFunction") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphRuntimeGetEntityForProjectile") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphArchiveScanNestedPayloadsFile") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphCompileArchiveBytes") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphRuntimeRegisterWeaponArchiveDependencies") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphRuntimeRegisterBehaviorIrOwned(&ir") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphRuntimeClearWeaponDependencies") != std::string::npos);
	REQUIRE(runtime.find("ownerBitsSet") != std::string::npos);

	const std::string lifecycle = readFile("port/src/assetcatalog_load.c");
	REQUIRE(lifecycle.find("s_catalogTypeUsesWeaponGraphRuntime") != std::string::npos);
	REQUIRE(lifecycle.find("type == ASSET_WEAPON") != std::string::npos);
	REQUIRE(lifecycle.find("s_catalogExtractTypedArchiveRoot") != std::string::npos);
	REQUIRE(lifecycle.find("\".pdweapon\"") != std::string::npos);
	REQUIRE(lifecycle.find("weaponGraphRuntimeRegisterWeaponArchive(entry->runtime_index") != std::string::npos);
	REQUIRE(lifecycle.find("s_catalogActivateLooseWeaponGraphRuntime") != std::string::npos);
	REQUIRE(lifecycle.find("weaponGraphRuntimeRegisterWeaponGraphJson") == std::string::npos);
	REQUIRE(lifecycle.find("weaponGraphRuntimeRegisterWeaponSourceJson") != std::string::npos);
	/* c3849 Wave 5f Unit 2: the loose path passes the tunables trio through
	 * the EXTENDED RegisterWeaponSourceJson signature. */
	REQUIRE(lifecycle.find("settings, settings_size, variables, variables_size") != std::string::npos);
	REQUIRE(lifecycle.find("presentation, presentation_size") != std::string::npos);
	REQUIRE(lifecycle.find("fsFileLoad(source_path") != std::string::npos);
	REQUIRE(lifecycle.find("weaponGraphRuntimeRegisterBehaviorGraphJson") != std::string::npos);
	REQUIRE(lifecycle.find("weaponGraphRuntimeClearAsset") != std::string::npos);
	REQUIRE(lifecycle.find("weaponGraphRuntimeClearWeapon(entry->runtime_index") != std::string::npos);
}

TEST_CASE("wave 5 unit 0 bug-fix pins stay wired",
          "[modding][pdxxx][weapon_graph][c3849][static]") {
	/* B-916: both timed_timer_ticks60 consumers in bondgun.c must zero-guard.
	 * Base timed mines author no timer param (field 0); an unguarded read
	 * arms timer240 = 0 and detonates on the first tick with the runtime
	 * toggle ON. */
	const std::string bondgun = readFile("src/game/bondgun.c");
	const std::string::size_type firstguard =
		bondgun.find("entity->timed_timer_ticks60 > 0");
	REQUIRE(firstguard != std::string::npos);
	REQUIRE(bondgun.find("entity->timed_timer_ticks60 > 0", firstguard + 1)
		!= std::string::npos);

	/* B-917: real archives write the *_file descriptor spellings; the parser
	 * must match them, with the bare spellings kept as first-set-wins
	 * aliases (model/model_file precedent). */
	const std::string archive = readFile("port/src/weapon_graph_archive.c");
	REQUIRE(archive.find("strcmp(key, \"shared_context_file\") == 0") != std::string::npos);
	REQUIRE(archive.find("strcmp(key, \"settings_file\") == 0") != std::string::npos);
	REQUIRE(archive.find("strcmp(key, \"variables_file\") == 0") != std::string::npos);
	REQUIRE(archive.find("strcmp(key, \"shared_context\") == 0 && out->shared_context[0] == '\\0'") != std::string::npos);
	REQUIRE(archive.find("strcmp(key, \"settings\") == 0 && out->settings[0] == '\\0'") != std::string::npos);
	REQUIRE(archive.find("strcmp(key, \"variables\") == 0 && out->variables[0] == '\\0'") != std::string::npos);

	/* Dangling targetprop: projectilesUnrefOwner must clear targetprop along
	 * with ownerprop (projectileTick homing dereferences it unguarded), and
	 * the TICKOP_FREE arm in prop.c must unref before propFree because
	 * pickup paths free threat-trackable props without objFree/chrRemove. */
	const std::string propobj = readFile("src/game/propobj.c");
	REQUIRE(propobj.find("g_Projectiles[i].targetprop == prop") != std::string::npos);
	const std::string prop = readFile("src/game/prop.c");
	const std::string::size_type unref = prop.find("projectilesUnrefOwner(prop);");
	REQUIRE(unref != std::string::npos);
	REQUIRE(prop.find("propFree(prop);", unref) != std::string::npos);

	/* Wave 5 Unit 1a: shared helper family. The contact-impact helper is
	 * reimplemented on top of the module-precondition-free lookup, and the
	 * entity sibling exists for the Unit 6/7 arms. */
	REQUIRE(propobj.find("weaponGetCustomProjectileGraph") != std::string::npos);
	REQUIRE(propobj.find("weaponGetEntityGraphForGameplay") != std::string::npos);

	/* Wave 5 Unit 3 Slice A: per-projectile steering gains. The custom
	 * branch must use the per-projectile prev error, and the OG branch must
	 * keep its mainOverrideVariable hooks and shared static verbatim. */
	REQUIRE(propobj.find("projectile->hominggain") != std::string::npos);
	REQUIRE(propobj.find("homingpreverr") != std::string::npos);
	REQUIRE(propobj.find("mainOverrideVariable(\"kkg\"") != std::string::npos);

	/* Wave 5 Unit 3 Slice D: fly-by-wire numerics latched + consumed. */
	REQUIRE(propobj.find("flysmokeinterval240") != std::string::npos);
	REQUIRE(propobj.find("flyproxradius") != std::string::npos);

	/* Wave 5 Unit 1c: s16 timer sinks saturate (propobj + bondgun). */
	REQUIRE(propobj.find("weaponGraphClampTicks240") != std::string::npos);

	/* Wave 5 Unit 3 Slice B: trajectory closure in bondgun.c. */
	REQUIRE(bondgun.find("has_trajectory_correction") != std::string::npos);
	REQUIRE(bondgun.find("trajectory_max_angle") != std::string::npos);
	REQUIRE(bondgun.find("bgunGraphClampTicks240") != std::string::npos);

	/* Wave 5 Unit 3 Slice C: AI launcher widen + wrong-owner fix. */
	const std::string chraction = readFile("src/game/chraction.c");
	REQUIRE(chraction.find("chrGsetCustomProjectileGraph") != std::string::npos);
	REQUIRE(chraction.find("|| customlaunch") != std::string::npos);
	REQUIRE(chraction.find(
		"weaponCreateProjectileFromGset(projectilemodelnum, &gset, chr)")
		!= std::string::npos);
	REQUIRE(chraction.find(
		"weaponCreateProjectileFromGset(projectilemodelnum, &gset, g_Vars.currentplayer")
		== std::string::npos);

	/* Wave 5 Unit 3 Slice D: player-path fbw constants route through the
	 * latched fields with the OG literals as 0-fallbacks. */
	const std::string player = readFile("src/game/player.c");
	REQUIRE(player.find("flyturnrate") != std::string::npos);
	REQUIRE(player.find("flyaccel") != std::string::npos);

	/* Wave 5 Unit 3: struct projectile grew the PC guidance fields. */
	const std::string types = readFile("src/include/types.h");
	REQUIRE(types.find("f32 hominggain;") != std::string::npos);
	REQUIRE(types.find("f32 homingpreverr;") != std::string::npos);
	REQUIRE(types.find("f32 flyturnrate;") != std::string::npos);
	REQUIRE(types.find("s32 flylosttimeout240;") != std::string::npos);
	REQUIRE(types.find("s32 flysmokeinterval240;") != std::string::npos);

	/* Wave 5 Unit 1b/1c: unified explosion resolver + entity sentinels. */
	const std::string runtime = readFile("port/src/weapon_graph_runtime.c");
	REQUIRE(runtime.find("s32 weaponGraphResolveExplosionRef(const char *ref)")
		!= std::string::npos);
	REQUIRE(runtime.find("runtime->armed_exptype = -1;") != std::string::npos);
	REQUIRE(runtime.find("runtime->autogun_alternate_muzzles = -1;") != std::string::npos);
	REQUIRE(runtime.find("runtime->autogun_friendly_fire_suppression = -1;") != std::string::npos);
	REQUIRE(runtime.find("runtime->autogun_pickup_recover = -1;") != std::string::npos);
	REQUIRE(runtime.find("runtime->storm_delete_carrier = -1;") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphWarnTimerOverlap") != std::string::npos);

	/* T-ASSETS-020: only an empty selection keeps the explicit callsite
	 * default. Every nonempty unresolved selection fails closed. */
	const std::string bridge = readFile("port/src/effect_graph_runtime.c");
	REQUIRE(bridge.find("weaponGraphRuntimeEnabled()") != std::string::npos);
	REQUIRE(bridge.find("return fallback_exptype;") != std::string::npos);
	REQUIRE(bridge.find("return fallback_sparktype;") != std::string::npos);

	/* Wave 5 Unit 4 (surface): combined custom projectile arm (B1 position
	 * 2), wall-hugger stick latch + fall vector, sticky_attach stick gate,
	 * impact_stick_on_hit absorption (B4), timer policies, bounce-slide
	 * event params and spawn latches. */
	REQUIRE(propobj.find("wall_stick_timer_ticks60") != std::string::npos);
	REQUIRE(propobj.find("wall_fall_vec") != std::string::npos);
	REQUIRE(propobj.find("sticky_allow_background") != std::string::npos);
	REQUIRE(propobj.find("impact_stick_on_hit") != std::string::npos);
	REQUIRE(propobj.find("timer_expire_policy") != std::string::npos);
	/* on_attach seed (stick site) + on_impact arming (BG-hit chain). */
	REQUIRE(propobj.find("timer_start_policy == 2") != std::string::npos);
	REQUIRE(propobj.find("timer_start_policy == 1") != std::string::npos);
	REQUIRE(propobj.find("bounce_limit") != std::string::npos);
	REQUIRE(propobj.find("bounce_rest_speed") != std::string::npos);
	/* The randomize-rotation latch must SET the flag (OG only reads it). */
	REQUIRE(propobj.find("flags |= PROJECTILEFLAG_00000100") != std::string::npos);

	/* Wave 5 Unit 5 (impact remainder + trail): filter-gated consume sites,
	 * hit sound arm, spark mirror, trail cadence, and strict selected-source
	 * suppression at the detonation/visual sites. */
	REQUIRE(propobj.find("impact_filter_mode") != std::string::npos);
	REQUIRE(propobj.find("impact_hit_sound") != std::string::npos);
	REQUIRE(propobj.find("graphtrailsmoketype") != std::string::npos);
	REQUIRE(propobj.find("WAVE6-EFFECT-HANDOFF") == std::string::npos);
	REQUIRE(propobj.find("effectGameplayRuntimeTriggerPropExplosion") != std::string::npos);
	REQUIRE(propobj.find("effectGameplayRuntimeTriggerSpark") != std::string::npos);
	REQUIRE(propobj.find("effectGraphResolveExplosionType") == std::string::npos);
	REQUIRE(propobj.find("effectGraphResolveSparkType") == std::string::npos);

	/* Wave 5 Units 4+5: struct projectile trail fields + slot-reuse reset. */
	REQUIRE(types.find("s32 graphtrailsmoketype;") != std::string::npos);
	REQUIRE(types.find("s32 graphtrailinterval240;") != std::string::npos);
	REQUIRE(propobj.find("projectile->graphtrailsmoketype = -1;") != std::string::npos);

	/* Wave 5 Unit 4: bounce_randomize_rotation absent-vs-authored-0 sentinel
	 * pre-set in the bounce parse case. */
	REQUIRE(runtime.find("out->bounce_randomize_rotation = -1;") != std::string::npos);

	/* Wave 5 Unit 6 (entity-armed + nbomb-storm Step 2): CUSTOM ENTITY ARM
	 * in weaponTick (B1 position 6), with the Unit 1a entity helper consumed
	 * beyond its definition (objDamage), the parse-latched damage response
	 * mode wired, and the proxy LOS pair (cdTestLos05 gated on
	 * proxy_line_of_sight, radius-pass only). */
	REQUIRE(propobj.find("CUSTOM ENTITY ARM") != std::string::npos);
	{
		const std::string::size_type entityhelper =
			propobj.find("weaponGetEntityGraphForGameplay(");
		REQUIRE(entityhelper != std::string::npos);
		REQUIRE(propobj.find("weaponGetEntityGraphForGameplay(", entityhelper + 1)
			!= std::string::npos);
	}
	REQUIRE(propobj.find("armed_damage_response_mode") != std::string::npos);
	REQUIRE(propobj.find("proxy_line_of_sight") != std::string::npos);
	REQUIRE(propobj.find("cdTestLos05") != std::string::npos);

	/* Wave 5 Unit 6: objFree's unregister clause (a proxy left in g_Proxies
	 * = use-after-free in coordTriggerProxies). Since projectile slice 6 it is
	 * every weapon object's, not the custom slots' alone: a program can give
	 * the proximity arm to any weapon object. */
	REQUIRE(propobj.find("a proxy left in g_Proxies would dangle") != std::string::npos);
	REQUIRE(propobj.find("\t\tweaponUnregisterProxy(weapon);\n\n"
		"\t\t/* projectile-events.md slice 7a: what the object holds goes with it,") != std::string::npos);

	/* Wave 5 Unit 6: detonator provenance. Sidecar array beside the mask,
	 * the predicate exported from the runtime, the widened
	 * playerActivateRemoteMineDetonator signature at BOTH callers, and the
	 * sidecar reset at BOTH mask reset sites (alarmTick tail + setupReset). */
	REQUIRE(propobj.find("g_PlayersDetonatingWeaponnum") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphEntityRemoteSignalMatches") != std::string::npos);
	REQUIRE(prop.find(
		"playerActivateRemoteMineDetonator(g_Vars.currentplayernum, weaponnum)")
		!= std::string::npos);
	{
		const std::string bondmove = readFile("src/game/bondmove.c");
		REQUIRE(bondmove.find(
			"playerActivateRemoteMineDetonator(g_Vars.currentplayernum, bgunGetWeaponNum(HAND_RIGHT))")
			!= std::string::npos);
	}
	REQUIRE(propobj.find("g_PlayersDetonatingWeaponnum[i] = -1;") != std::string::npos);
	{
		const std::string setup = readFile("src/game/setup.c");
		REQUIRE(setup.find("g_PlayersDetonatingWeaponnum[i] = -1;") != std::string::npos);
	}
}

TEST_CASE("wave 5 unit 7 deployed-entity consumer pins stay wired",
          "[modding][pdxxx][weapon_graph][c3849][static]") {
	const std::string propobj = readFile("src/game/propobj.c");
	const std::string bondgun = readFile("src/game/bondgun.c");
	const std::string setup = readFile("src/game/setup.c");
	const std::string player = readFile("src/game/player.c");
	const std::string runtime = readFile("port/src/weapon_graph_runtime.c");

	/* Autogun completion: the deploy latch sidecar (binding spec B4 - NO
	 * autogunobj struct growth, B-323 stride class), the rpm conversion
	 * consumed at deploy, and the cadence/beam/ffsuppress consumers in
	 * autogunTickShoot. */
	REQUIRE(propobj.find("g_ThrownLaptopLatch") != std::string::npos);
	REQUIRE(propobj.find("weaponGraphAutogunFireInterval(") != std::string::npos);
	REQUIRE(propobj.find("autogun_beam_every_shots") != std::string::npos);
	REQUIRE(propobj.find("autogun_friendly_fire_suppression") != std::string::npos);
	REQUIRE(propobj.find("autogun_turn_speed") != std::string::npos);
	REQUIRE(propobj.find("autogun_target_filter") != std::string::npos);
	REQUIRE(propobj.find("latch->fireinterval > 1") != std::string::npos);
	REQUIRE(propobj.find("latch->ffsuppress != 0") != std::string::npos);

	/* Latch lifecycle: objFree clears the slot; propsReset clears the whole
	 * sidecar beside the g_ThrownLaptops reallocation. */
	REQUIRE(propobj.find("thrownLaptopLatchGet((struct autogunobj *) obj)") != std::string::npos);
	REQUIRE(propobj.find("void thrownLaptopLatchResetAll(void)") != std::string::npos);
	REQUIRE(setup.find("thrownLaptopLatchResetAll();") != std::string::npos);

	/* Transition (impact U5, B4 single bondgun.c gate widen): the first
	 * production caller of weaponGraphRuntimeGetEntityForProjectile, OG
	 * laptop clause first, and the laptopDeploy projectile-chain fallback. */
	REQUIRE(bondgun.find("weaponGraphRuntimeGetEntityForProjectile") != std::string::npos);
	/* projectile-events.md slice 5: the base Laptop's throw is the throw
	 * rule's choice now; a mod weapon's transition entity still deploys. */
	REQUIRE(bondgun.find("& PROJECTILE_MODULE_DEPLOY_SENTRY)\n\t\t\t\t|| transitionentity != NULL) {") != std::string::npos);
	REQUIRE(propobj.find("weaponGraphRuntimeGetEntityForProjectile(projectilerec)") != std::string::npos);

	/* Laptop ammo-transfer literals replaced by the carrier weaponnum
	 * (bit-identical for base WEAPON_LAPTOPGUN). */
	REQUIRE(propobj.find("botactTryRemoveAmmoFromReserve(chr->aibot, carrierweaponnum") != std::string::npos);
	REQUIRE(propobj.find("bgunGetAmmoQtyForWeapon(carrierweaponnum, FUNC_PRIMARY)") != std::string::npos);
	REQUIRE(propobj.find("bgunSetAmmoQtyForWeapon(carrierweaponnum, FUNC_PRIMARY, qty)") != std::string::npos);
	REQUIRE(propobj.find("botactTryRemoveAmmoFromReserve(chr->aibot, WEAPON_LAPTOPGUN") == std::string::npos);

	/* Owner-cleanup: the pending mask beside g_PlayersDetonatingMines, the
	 * player.c death-transition set site, BOTH reset sites, the weaponTick
	 * consumer and the alarmTick thrown-laptop pass. */
	REQUIRE(propobj.find("u32 g_PlayersOwnerCleanupPending") != std::string::npos);
	REQUIRE(player.find("g_PlayersOwnerCleanupPending |= 1 << cleanupindex") != std::string::npos);
	REQUIRE(propobj.find("g_PlayersOwnerCleanupPending = 0;") != std::string::npos);
	REQUIRE(setup.find("g_PlayersOwnerCleanupPending = 0;") != std::string::npos);
	REQUIRE(propobj.find("owner_death_behavior") != std::string::npos);
	REQUIRE(propobj.find("ownerdeathmode") != std::string::npos);
	REQUIRE(propobj.find("replace_existing_policy") != std::string::npos);
	REQUIRE(propobj.find("max_active_per_owner == 0") != std::string::npos);

	/* Sticky-device: the helper, the parse-latched gates at the stick
	 * decision / armed-pickup / landing arms, and the runtime-side latch of
	 * the four policy strings (sticky_attachment_filter et al). */
	REQUIRE(propobj.find("weaponGetStickyDeviceGraph") != std::string::npos);
	REQUIRE(propobj.find("sticky_attachment_bg_only") != std::string::npos);
	REQUIRE(propobj.find("sticky_pickup_none") != std::string::npos);
	REQUIRE(propobj.find("sticky_disable_shootable") != std::string::npos);
	REQUIRE(propobj.find("sticky_visible_hidden") != std::string::npos);
	REQUIRE(runtime.find("sticky_attachment_filter, \"background_only\"") != std::string::npos);
	REQUIRE(runtime.find("runtime->max_active_per_owner = -1;") != std::string::npos);
	REQUIRE(runtime.find("mission_behavior_ref") != std::string::npos);

	/* Interaction: recover latch consumption (recoverweaponnum replaces the
	 * WEAPON_LAPTOPGUN recover literals) and interaction_sound at the
	 * generic propPickupByPlayer sound site. */
	REQUIRE(propobj.find("recoverweaponnum") != std::string::npos);
	REQUIRE(propobj.find("invGiveSingleWeapon(recoverweaponnum)") != std::string::npos);
	REQUIRE(propobj.find("pickupentity->interaction_sound") != std::string::npos);
	REQUIRE(propobj.find("latch->pickupsound") != std::string::npos);
	REQUIRE(propobj.find("latch->interactanyone") != std::string::npos);
	REQUIRE(propobj.find("latch->recoverammonone") != std::string::npos);
}

TEST_CASE("wave 5 unit 1 explosion resolver and autogun cadence are pure",
          "[modding][pdxxx][weapon_graph][c3849]") {
	/* Every selected public ID stays unresolved at parse time. The installed
	 * public effect executor is the only source-to-native-row authority. */
	REQUIRE(weaponGraphResolveExplosionRef("base:explosion_rocket") == -1);
	REQUIRE(weaponGraphResolveExplosionRef("base:explosion_huge") == -1);
	REQUIRE(weaponGraphResolveExplosionRef("base:explosion_sdgrenade") == -1);
	REQUIRE(weaponGraphResolveExplosionRef("base:explosion_phoenix") == -1);
	REQUIRE(weaponGraphResolveExplosionRef("base:explosion_small") == -1);
	REQUIRE(weaponGraphResolveExplosionRef("base:explosion_laptop") == -1);
	REQUIRE(weaponGraphResolveExplosionRef(nullptr) == -1);
	REQUIRE(weaponGraphResolveExplosionRef("") == -1);
	REQUIRE(weaponGraphResolveExplosionRef("needler:pink_blast") == -1);
	REQUIRE(weaponGraphResolveExplosionRef("base:explosion_bogus") == -1);

	/* rpm -> autogun fire interval (1800 rpm OG baseline). */
	REQUIRE(weaponGraphAutogunFireInterval(1800.0f) == 1);
	REQUIRE(weaponGraphAutogunFireInterval(900.0f) == 2);
	REQUIRE(weaponGraphAutogunFireInterval(450.0f) == 4);
	REQUIRE(weaponGraphAutogunFireInterval(0.0f) == 1);
	REQUIRE(weaponGraphAutogunFireInterval(-60.0f) == 1);
	REQUIRE(weaponGraphAutogunFireInterval(1000000.0f) == 1);
}

TEST_CASE("selected effect bridges fail closed while empty refs keep defaults",
          "[modding][pdxxx][weapon_graph][t-assets-020][fail-closed]") {
	const s32 prev = weaponGraphRuntimeEnabled();

	/* Toggle OFF: any authored selection is unavailable. */
	weaponGraphRuntimeSetEnabled(0);
	REQUIRE(effectGraphResolveExplosionType("base:explosion_rocket", 5) ==
		EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveSparkType("base:spark_pink", 4) ==
		EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveSmokeType("base:smoke_large", 6) ==
		EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveSound("base:sfx_boom", 123) ==
		EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveExplosionType("", 7) == 7);

	/* Toggle ON without installed public sources: selected refs still fail. */
	weaponGraphRuntimeSetEnabled(1);
	REQUIRE(effectGraphResolveExplosionType("base:explosion_rocket", 5) ==
		EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveExplosionType("mod:unknown_boom", 5) ==
		EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveExplosionType(nullptr, 9) == 9);
	REQUIRE(effectGraphResolveSparkType("base:spark_pink", 4) ==
		EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveSmokeType("base:smoke_large", 6) ==
		EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveSound("base:sfx_boom", 123) ==
		EFFECT_GRAPH_RESOLVE_FAILED);

	weaponGraphRuntimeSetEnabled(prev);
}

TEST_CASE("effect sources activate before weapon admission with exact allocator cleanup",
	"[modding][pdxxx][effect][t-assets-020][lifecycle][static]") {
	const std::string walker = readFile("port/src/loader_walker.c");
	const std::string meta = readFile("port/src/loader_walker_meta.c");
	const std::string loader = readFile("port/src/assetcatalog_load.c");
	const std::string catalog = readFile("port/src/assetcatalog.c");
	const std::string deps = readFile("port/src/assetcatalog_deps.c");
	REQUIRE(walker.find("loaderWalkerScanMetadataFamilies(data_root, &mr)") <
		walker.find("loaderWalkerScanWeapons(data_root, &kr)"));
	REQUIRE(meta.find("assetCatalogRegisterEffectNestedDependencies(id, file_path, 1") !=
		std::string::npos);
	REQUIRE(meta.find("effectDependenciesCollectArchiveFile") == std::string::npos);
	REQUIRE(meta.find("catalogLoadTypedAsset(ASSET_EFFECT") != std::string::npos);
	/* The later catalogLoadInit preload checks ACTIVE before calling preload,
	 * so early walked activation cannot add a second parent reference. */
	REQUIRE(loader.find("mutable_entry->load_state < ASSET_STATE_ACTIVE") !=
		std::string::npos);
	REQUIRE(loader.find("free(graph)") == std::string::npos);
	REQUIRE(loader.find("free(primary)") == std::string::npos);
	REQUIRE(loader.find("sysMemFree(graph)") != std::string::npos);
	/* Disable/reset must retire the full owned closure before catalog IDs or
	 * their dependency edges disappear. Full reset clears base executor state;
	 * mod reset preserves bundled edges only. */
	REQUIRE(catalog.find("catalogDeactivateTypedAsset") != std::string::npos);
	REQUIRE(catalog.find("effectGraphRuntimeClearAll();") != std::string::npos);
	REQUIRE(catalog.find("catalogDepClear();") != std::string::npos);
	REQUIRE(catalog.find("catalogDepClearMods();") != std::string::npos);
	REQUIRE(catalog.find("catalogResetPlanBuild") !=
		std::string::npos);
	REQUIRE(catalog.find("catalogCanDeactivateTypedAsset") != std::string::npos);
	REQUIRE(catalog.find("assetRuntimeReset();") != std::string::npos);
	REQUIRE(catalog.find("weaponGraphRuntimeClearAll();") != std::string::npos);
	REQUIRE(catalog.find("s_typeOwnsEffectRuntime") == std::string::npos);
	REQUIRE(catalog.find("effectGraphRuntimeClearAsset") == std::string::npos);
	REQUIRE(deps.find("if (s_DepTable[i].is_bundled)") != std::string::npos);
	REQUIRE(loader.find("s_catalogResolveDeactivationNode") != std::string::npos);
	REQUIRE(loader.find("root->ref_count > 0 ? root->ref_count : 0") !=
		std::string::npos);
const auto setEnabledStart = catalog.find("asset_catalog_change_e assetCatalogSetEnabledChecked(");
	const auto setEnabledEnd = catalog.find("\nvoid assetCatalogSetCategoryById", setEnabledStart);
	REQUIRE(setEnabledStart != std::string::npos);
	REQUIRE(setEnabledEnd != std::string::npos);
	const std::string setEnabled = catalog.substr(setEnabledStart,
		setEnabledEnd - setEnabledStart);
	REQUIRE(setEnabled.find("CATALOG_UNLOCK();") <
		setEnabled.rfind("catalogDeactivateTypedAssetForReset"));
	REQUIRE(setEnabled.find("preflight failed; enabled state unchanged") !=
		std::string::npos);
	REQUIRE(setEnabled.find("toggle rolled back") != std::string::npos);
	REQUIRE(setEnabled.find("ASSET_STATE_ENABLED") != std::string::npos);
}

TEST_CASE("wave 5 unit 1c latches derived projectile fields at registration",
          "[modding][pdxxx][weapon_graph][c3849]") {
	const std::string richGraph =
		"{\n"
		"  \"schema\": \"pd.projectile_graph.v1\",\n"
		"  \"asset_id\": \"base:test_derived_projectile\",\n"
		"  \"graph_id\": \"derived_projectile_v1\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"motion\", \"kind\": \"projectile.motion\", \"params\": {\n"
		"      \"motion_kind\": \"ballistic\"\n"
		"    } },\n"
		"    { \"id\": \"timer\", \"kind\": \"projectile.timer\", \"params\": {\n"
		"      \"timer_ticks60\": 180,\n"
		"      \"timer_starts\": \"on_impact\",\n"
		"      \"on_expire\": \"delete\"\n"
		"    } },\n"
		"    { \"id\": \"wall\", \"kind\": \"projectile.wall_hugger\", \"params\": {\n"
		"      \"stick_surface_filter\": \"background\",\n"
		"      \"fall_vector\": \"1,2,3\"\n"
		"    } },\n"
		"    { \"id\": \"impact\", \"kind\": \"projectile.impact\", \"params\": {\n"
		"      \"impact_filter\": \"background\",\n"
		"      \"explosion_ref\": \"base:explosion_rocket\",\n"
		"      \"consume_on_hit\": true\n"
		"    } },\n"
		"    { \"id\": \"trail\", \"kind\": \"projectile.trail\", \"params\": {\n"
		"      \"trail_type\": \"homing\",\n"
		"      \"interval_ticks60\": 24\n"
		"    } },\n"
		"    { \"id\": \"bounce\", \"kind\": \"projectile.bounce_slide\", \"params\": {\n"
		"      \"bounce_limit\": 9,\n"
		"      \"rest_speed\": 3.5,\n"
		"      \"slide_friction\": 0.25,\n"
		"      \"first_bounce_boost\": 0.4,\n"
		"      \"randomize_rotation\": false\n"
		"    } },\n"
		"    { \"id\": \"sticky\", \"kind\": \"projectile.sticky_attach\", \"params\": {\n"
		"      \"allow_background\": true,\n"
		"      \"allow_char\": false,\n"
		"      \"allow_obj\": true\n"
		"    } },\n"
		"    { \"id\": \"pickup\", \"kind\": \"projectile.pickup_recover\", \"params\": {\n"
		"      \"allowed_owner\": \"owner\",\n"
		"      \"recover_weapon_ref\": \"base:laptopgun\",\n"
		"      \"recover_ammo_policy\": \"none\"\n"
		"    } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [ { \"name\": \"main\", \"node\": \"motion\" } ]\n"
		"}\n";

	char err[256] = {};
	weaponGraphRuntimeClearAll();
	REQUIRE(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE,
		"base:test_derived_projectile", richGraph.data(),
		static_cast<u32>(richGraph.size()), err, sizeof(err)) == 0);

	const weapon_graph_projectile_runtime_t *rich =
		weaponGraphRuntimeGetProjectile("base:test_derived_projectile");
	REQUIRE(rich != nullptr);
	REQUIRE(rich->timer_start_policy == 1);
	REQUIRE(rich->timer_expire_policy == 1);
	REQUIRE(rich->wall_stick_bg_only == 1);
	REQUIRE(rich->wall_fall_vec[0] == Approx(1.0f));
	REQUIRE(rich->wall_fall_vec[1] == Approx(2.0f));
	REQUIRE(rich->wall_fall_vec[2] == Approx(3.0f));
	REQUIRE(rich->impact_filter_mode == 1);
	/* Selected public IDs are retained verbatim and resolve only against the
	 * active effect executor at the gameplay callsite. */
	REQUIRE(rich->impact_exptype == -1);
	REQUIRE(std::string(rich->impact_explosion_ref) ==
		"base:explosion_rocket");
	REQUIRE(rich->trail_smoketype == SMOKETYPE_HOMINGTAIL);
	REQUIRE(rich->trail_interval_ticks60 == 24);
	/* Wave 5 Unit 4 (surface): bounce-slide params + authored-false
	 * randomize_rotation (sentinel semantics: authored 0 stays 0). */
	REQUIRE(rich->has_bounce_slide == 1);
	REQUIRE(rich->bounce_limit == 9);
	REQUIRE(rich->bounce_rest_speed == Approx(3.5f));
	REQUIRE(rich->bounce_slide_friction == Approx(0.25f));
	REQUIRE(rich->bounce_first_boost == Approx(0.4f));
	REQUIRE(rich->bounce_randomize_rotation == 0);
	/* Wave 5 Unit 4 (surface): sticky_attach per-hit-class allows. */
	REQUIRE(rich->has_sticky_attach == 1);
	REQUIRE(rich->sticky_allow_background == 1);
	REQUIRE(rich->sticky_allow_char == 0);
	REQUIRE(rich->sticky_allow_obj == 1);
	REQUIRE(rich->pickup_owner_only == 1);
	REQUIRE(rich->recover_ammo_none == 1);
	/* pd-tests stubs the catalog, so the ref cannot resolve here; the
	 * unresolved sentinel is the contract. */
	REQUIRE(rich->recover_weaponnum == -1);

	const std::string downGraph =
		"{\n"
		"  \"schema\": \"pd.projectile_graph.v1\",\n"
		"  \"asset_id\": \"base:test_derived_projectile_down\",\n"
		"  \"graph_id\": \"derived_projectile_down_v1\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"motion\", \"kind\": \"projectile.motion\", \"params\": {\n"
		"      \"motion_kind\": \"ballistic\"\n"
		"    } },\n"
		"    { \"id\": \"wall\", \"kind\": \"projectile.wall_hugger\", \"params\": {\n"
		"      \"fall_vector\": \"down\"\n"
		"    } },\n"
		"    { \"id\": \"impact\", \"kind\": \"projectile.impact\", \"params\": {\n"
		"      \"explosion_ref\": \"mod:custom_boom\"\n"
		"    } },\n"
		"    { \"id\": \"bounce\", \"kind\": \"projectile.bounce_slide\", \"params\": {\n"
		"      \"bounce_limit\": 4\n"
		"    } },\n"
		"    { \"id\": \"trail\", \"kind\": \"projectile.trail\", \"params\": {\n"
		"      \"trail_type\": \"none\"\n"
		"    } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [ { \"name\": \"main\", \"node\": \"motion\" } ]\n"
		"}\n";
	REQUIRE(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE,
		"base:test_derived_projectile_down", downGraph.data(),
		static_cast<u32>(downGraph.size()), err, sizeof(err)) == 0);

	const weapon_graph_projectile_runtime_t *down =
		weaponGraphRuntimeGetProjectile("base:test_derived_projectile_down");
	REQUIRE(down != nullptr);
	REQUIRE(down->wall_fall_vec[0] == Approx(0.0f));
	REQUIRE(down->wall_fall_vec[1] == Approx(-10.0f));
	REQUIRE(down->wall_fall_vec[2] == Approx(0.0f));
	REQUIRE(down->wall_stick_bg_only == 0);
	REQUIRE(down->impact_exptype == -1);
	REQUIRE(down->trail_smoketype == -1);
	REQUIRE(down->timer_start_policy == 0);
	REQUIRE(down->timer_expire_policy == 0);
	/* randomize_rotation ABSENT inside an authored bounce node: the parse
	 * case pre-set keeps the -1 sentinel (absent != authored false). */
	REQUIRE(down->has_bounce_slide == 1);
	REQUIRE(down->bounce_limit == 4);
	REQUIRE(down->bounce_randomize_rotation == -1);

	const std::string garbageGraph =
		"{\n"
		"  \"schema\": \"pd.projectile_graph.v1\",\n"
		"  \"asset_id\": \"base:test_derived_projectile_garbage\",\n"
		"  \"graph_id\": \"derived_projectile_garbage_v1\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"motion\", \"kind\": \"projectile.motion\", \"params\": {\n"
		"      \"motion_kind\": \"ballistic\"\n"
		"    } },\n"
		"    { \"id\": \"timer\", \"kind\": \"projectile.timer\", \"params\": {\n"
		"      \"timer_starts\": \"whenever\",\n"
		"      \"on_expire\": \"confetti\"\n"
		"    } },\n"
		"    { \"id\": \"wall\", \"kind\": \"projectile.wall_hugger\", \"params\": {\n"
		"      \"fall_vector\": \"purple\"\n"
		"    } },\n"
		"    { \"id\": \"bounce\", \"kind\": \"projectile.bounce_slide\", \"params\": {\n"
		"      \"randomize_rotation\": true\n"
		"    } },\n"
		"    { \"id\": \"trail\", \"kind\": \"projectile.trail\", \"params\": {\n"
		"      \"trail_type\": \"sparkles\"\n"
		"    } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [ { \"name\": \"main\", \"node\": \"motion\" } ]\n"
		"}\n";
	REQUIRE(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE,
		"base:test_derived_projectile_garbage", garbageGraph.data(),
		static_cast<u32>(garbageGraph.size()), err, sizeof(err)) != 0);

	const weapon_graph_projectile_runtime_t *garbage =
		weaponGraphRuntimeGetProjectile("base:test_derived_projectile_garbage");
	REQUIRE(garbage == nullptr);

	/* Minimal fixture: sentinels hold without the source nodes. */
	const std::string minimalGraph =
		"{\n"
		"  \"schema\": \"pd.projectile_graph.v1\",\n"
		"  \"asset_id\": \"base:test_derived_projectile_minimal\",\n"
		"  \"graph_id\": \"derived_projectile_minimal_v1\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"motion\", \"kind\": \"projectile.motion\", \"params\": {\n"
		"      \"motion_kind\": \"ballistic\"\n"
		"    } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [ { \"name\": \"main\", \"node\": \"motion\" } ]\n"
		"}\n";
	REQUIRE(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE,
		"base:test_derived_projectile_minimal", minimalGraph.data(),
		static_cast<u32>(minimalGraph.size()), err, sizeof(err)) == 0);

	const weapon_graph_projectile_runtime_t *minimal =
		weaponGraphRuntimeGetProjectile("base:test_derived_projectile_minimal");
	REQUIRE(minimal != nullptr);
	REQUIRE(minimal->impact_exptype == -1);
	REQUIRE(minimal->trail_smoketype == -1);
	REQUIRE(minimal->recover_weaponnum == -1);
	REQUIRE(minimal->wall_fall_vec[1] == Approx(-10.0f));
	REQUIRE(minimal->timer_start_policy == 0);
	REQUIRE(minimal->timer_expire_policy == 0);
	REQUIRE(minimal->pickup_owner_only == 0);
	REQUIRE(minimal->recover_ammo_none == 0);
	/* No bounce node at all: consumers gate on has_bounce_slide, so the
	 * zero-init randomize value is never read. */
	REQUIRE(minimal->has_bounce_slide == 0);

	weaponGraphRuntimeClearAll();
}

TEST_CASE("wave 5 unit 1c latches derived entity modes and sentinels",
          "[modding][pdxxx][weapon_graph][c3849]") {
	const std::string richGraph =
		"{\n"
		"  \"schema\": \"pd.entity_graph.v1\",\n"
		"  \"asset_id\": \"base:test_derived_entity\",\n"
		"  \"graph_id\": \"derived_entity_v1\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"armed\", \"kind\": \"entity.armed_explosive\", \"params\": {\n"
		"      \"damage_response\": \"ignore\",\n"
		"      \"explosion_ref\": \"base:explosion_phoenix\"\n"
		"    } },\n"
		"    { \"id\": \"proxy\", \"kind\": \"entity.proxy_trigger\", \"params\": {\n"
		"      \"on_trigger\": \"storm\",\n"
		"      \"target_filter\": \"hostile_chr\",\n"
		"      \"team_filter\": \"enemy_only\",\n"
		"      \"owner_filter\": \"exclude_owner\"\n"
		"    } },\n"
		"    { \"id\": \"remote\", \"kind\": \"entity.remote_detonatable\", \"params\": {\n"
		"      \"on_remote_signal\": \"detonate\"\n"
		"    } },\n"
		"    { \"id\": \"timed\", \"kind\": \"entity.timed_detonatable\", \"params\": {\n"
		"      \"timer_ticks60\": 240,\n"
		"      \"starts_when\": \"armed\",\n"
		"      \"on_expire\": \"delete\"\n"
		"    } },\n"
		"    { \"id\": \"storm\", \"kind\": \"entity.nbomb_storm\", \"params\": {\n"
		"      \"delete_carrier\": false\n"
		"    } },\n"
		"    { \"id\": \"autogun\", \"kind\": \"entity.autogun\", \"params\": {\n"
		"      \"friendly_fire_suppression\": false,\n"
		"      \"pickup_recover\": false,\n"
		"      \"alternate_muzzles\": 0\n"
		"    } },\n"
		"    { \"id\": \"sticky\", \"kind\": \"entity.sticky_device\", \"params\": {\n"
		"      \"attachment_filter\": \"background_only\",\n"
		"      \"pickup_policy\": \"none\",\n"
		"      \"disable_policy\": \"shootable\",\n"
		"      \"visible_state\": \"hidden\"\n"
		"    } },\n"
		"    { \"id\": \"cleanup\", \"kind\": \"entity.owner_cleanup\", \"params\": {\n"
		"      \"max_active_per_owner\": 0\n"
		"    } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [ { \"name\": \"main\", \"node\": \"armed\" } ]\n"
		"}\n";

	char err[256] = {};
	weaponGraphRuntimeClearAll();
	REQUIRE(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_ENTITY,
		"base:test_derived_entity", richGraph.data(),
		static_cast<u32>(richGraph.size()), err, sizeof(err)) == 0);

	const weapon_graph_entity_runtime_t *rich =
		weaponGraphRuntimeGetEntity("base:test_derived_entity");
	REQUIRE(rich != nullptr);
	REQUIRE(rich->armed_damage_response_mode == 1);
	REQUIRE(rich->armed_exptype == -1);
	REQUIRE(std::string(rich->explosion_ref) == "base:explosion_phoenix");
	REQUIRE(rich->proxy_on_trigger_mode == 1);
	REQUIRE(rich->proxy_target_filter_mode == 1);
	REQUIRE(rich->proxy_team_filter_mode == 1);
	REQUIRE(rich->proxy_owner_filter_mode == 1);
	REQUIRE(rich->remote_signal_mode == 0);
	REQUIRE(rich->timed_on_expire_mode == 2);
	REQUIRE(rich->timed_starts_mode == 0);
	/* Authored-zero must be preserved (not confused with absent = -1). */
	REQUIRE(rich->storm_delete_carrier == 0);
	REQUIRE(rich->autogun_friendly_fire_suppression == 0);
	REQUIRE(rich->autogun_pickup_recover == 0);
	REQUIRE(rich->autogun_alternate_muzzles == 0);

	/* c3849 Wave 5 Unit 7: sticky-device parse latches (B3) and the
	 * authored-zero max_active_per_owner (deny at deploy). */
	REQUIRE(rich->sticky_attachment_bg_only == 1);
	REQUIRE(rich->sticky_pickup_none == 1);
	REQUIRE(rich->sticky_disable_shootable == 1);
	REQUIRE(rich->sticky_visible_hidden == 1);
	REQUIRE(rich->max_active_per_owner == 0);

	/* Minimal fixture: -1 sentinels hold when the params are absent. */
	const std::string minimalGraph =
		"{\n"
		"  \"schema\": \"pd.entity_graph.v1\",\n"
		"  \"asset_id\": \"base:test_derived_entity_minimal\",\n"
		"  \"graph_id\": \"derived_entity_minimal_v1\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"armed\", \"kind\": \"entity.armed_explosive\", \"params\": {} },\n"
		"    { \"id\": \"storm\", \"kind\": \"entity.nbomb_storm\", \"params\": {} },\n"
		"    { \"id\": \"autogun\", \"kind\": \"entity.autogun\", \"params\": {} },\n"
		"    { \"id\": \"sticky\", \"kind\": \"entity.sticky_device\", \"params\": {} }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [ { \"name\": \"main\", \"node\": \"armed\" } ]\n"
		"}\n";
	REQUIRE(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_ENTITY,
		"base:test_derived_entity_minimal", minimalGraph.data(),
		static_cast<u32>(minimalGraph.size()), err, sizeof(err)) == 0);

	const weapon_graph_entity_runtime_t *minimal =
		weaponGraphRuntimeGetEntity("base:test_derived_entity_minimal");
	REQUIRE(minimal != nullptr);
	REQUIRE(minimal->armed_exptype == -1);
	REQUIRE(minimal->autogun_alternate_muzzles == -1);
	REQUIRE(minimal->autogun_friendly_fire_suppression == -1);
	REQUIRE(minimal->autogun_pickup_recover == -1);
	REQUIRE(minimal->storm_delete_carrier == -1);
	REQUIRE(minimal->armed_damage_response_mode == 0);
	REQUIRE(minimal->proxy_on_trigger_mode == 0);
	REQUIRE(minimal->remote_signal_mode == 0);
	REQUIRE(minimal->timed_on_expire_mode == 0);
	REQUIRE(minimal->timed_starts_mode == 0);

	/* c3849 Wave 5 Unit 7: empty sticky params latch 0 (OG: stick anywhere,
	 * pickable, invincible flags, visible) and the absent max_active sentinel
	 * stays -1 (never confused with the authored-0 deny). */
	REQUIRE(minimal->sticky_attachment_bg_only == 0);
	REQUIRE(minimal->sticky_pickup_none == 0);
	REQUIRE(minimal->sticky_disable_shootable == 0);
	REQUIRE(minimal->sticky_visible_hidden == 0);
	REQUIRE(minimal->max_active_per_owner == -1);

	weaponGraphRuntimeClearAll();
}

TEST_CASE("wave 5 unit 6 remote signal provenance predicate",
          "[modding][pdxxx][weapon_graph][c3849]") {
	weapon_graph_entity_runtime_t entity;
	memset(&entity, 0, sizeof(entity));

	/* Empty/unauthored detonator_ref: ANY signal matches, including the -1
	 * wildcard weaponnum (OG parity - the OG mask carries no identity). */
	REQUIRE(weaponGraphEntityRemoteSignalMatches(&entity, 30) == 1);
	REQUIRE(weaponGraphEntityRemoteSignalMatches(&entity, 0) == 1);
	REQUIRE(weaponGraphEntityRemoteSignalMatches(&entity, -1) == 1);

	/* NULL entity behaves as unauthored (OG parity). */
	REQUIRE(weaponGraphEntityRemoteSignalMatches(nullptr, -1) == 1);

	/* Authored ref: the -1 wildcard does NOT satisfy it (authored refs
	 * demand exact provenance), and an unresolved ref never matches -
	 * pd-tests stubs the catalog, so resolution always fails here, which is
	 * exactly the unresolved contract. */
	strcpy(entity.detonator_ref, "base:remotemine");
	REQUIRE(weaponGraphEntityRemoteSignalMatches(&entity, -1) == 0);
	REQUIRE(weaponGraphEntityRemoteSignalMatches(&entity, 30) == 0);
}

/* ---------------------------------------------------------------------------
 * c3849 Wave 5f: settings/variables/presentation parse + consumers.
 * ------------------------------------------------------------------------- */

namespace {

const char *kW5fPrimaryGraph =
	"{\n"
	"  \"schema\": \"pd.weapon_graph.v1\",\n"
	"  \"asset_id\": \"base:w5f_weapon\",\n"
	"  \"graph_id\": \"primary\",\n"
	"  \"nodes\": [ { \"id\": \"primary_action\", \"kind\": \"fire.hitscan\","
	" \"params\": { \"mode\": \"primary\", \"function_type\": \"shoot_single\","
	" \"damage\": \"$base_damage\", \"penetration\": \"$pierce\","
	" \"camera_effect\": \"$cam\" } } ],\n"
	"  \"exports\": [ { \"name\": \"primary\", \"node\": \"primary_action\" } ]\n"
	"}\n";

const char *kW5fSecondaryGraph =
	"{\n"
	"  \"schema\": \"pd.weapon_graph.v1\",\n"
	"  \"asset_id\": \"base:w5f_weapon\",\n"
	"  \"graph_id\": \"secondary\",\n"
	"  \"nodes\": [ { \"id\": \"secondary_action\", \"kind\": \"fire.auto_cadence\","
	" \"params\": { \"mode\": \"secondary\", \"function_type\": \"shoot_automatic\","
	" \"spread\": 2.5 } } ],\n"
	"  \"exports\": [ { \"name\": \"secondary\", \"node\": \"secondary_action\" } ]\n"
	"}\n";

const char *kW5fSharedContext =
	"{ \"schema\": \"pd.weapon_shared_context.v1\", \"asset_id\": \"base:w5f_weapon\","
	" \"contexts\": [ { \"name\": \"owner_player\", \"scope\": \"player\","
	" \"source\": \"equipped_player\", \"type\": \"player_ref\" } ] }\n";

const char *kW5fWeaponIni =
	"[weapon]\n"
	"catalog_id = base:w5f_weapon\n"
	"primary_graph = behavior/primary.graph.json\n"
	"secondary_graph = behavior/secondary.graph.json\n"
	"shared_context_file = behavior/shared-context.json\n"
	"settings_file = behavior/settings.json\n"
	"variables_file = behavior/variables.json\n"
	"presentation_file = bindings/presentation.json\n";

TempArchive writeW5fTunablesArchive(const std::string &settings,
		const std::string &variables, const std::string &primary = kW5fPrimaryGraph) {
	return writeArchiveEntries("tunables-source-contract", {
		{ "weapon.ini", kW5fWeaponIni },
		{ "behavior/primary.graph.json", primary },
		{ "behavior/secondary.graph.json", kW5fSecondaryGraph },
		{ "behavior/shared-context.json", kW5fSharedContext },
		{ "behavior/settings.json", settings },
		{ "behavior/variables.json", variables },
		{ "bindings/presentation.json", "{\"schema\":\"pd.weapon.presentation.v1\"}" },
	});
}

}  /* anonymous namespace */

TEST_CASE("weapon archives reject retired material and grip binding surfaces",
		  "[modding][pdxxx][weapon_graph][t-assets-024]") {
	const std::string settingsJson =
		"{ \"schema\": \"pd.weapon_settings.v1\" }\n";
	const std::string variablesJson =
		"{ \"schema\": \"pd.weapon_variables.v1\", \"variables\": [] }\n";
	auto archiveEntries = [&](const std::string &weaponIni) {
		return std::vector<std::pair<std::string, std::string>>{
			{ "weapon.ini", weaponIni },
			{ "behavior/primary.graph.json", kW5fPrimaryGraph },
			{ "behavior/secondary.graph.json", kW5fSecondaryGraph },
			{ "behavior/shared-context.json", kW5fSharedContext },
			{ "behavior/settings.json", settingsJson },
			{ "behavior/variables.json", variablesJson },
		};
	};

	char err[256] = {};
	{
		auto entries = archiveEntries(std::string(kW5fWeaponIni) +
			"material_slots_file = bindings/material-slots.json\n");
		TempArchive weapon = writeArchiveEntries("t-assets-024-descriptor", entries);
		REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(70,
			weapon.path.string().c_str(), err, sizeof(err)) != 0);
		REQUIRE(std::string(err).find("retired weapon binding material_slots_file") !=
			std::string::npos);
	}
	{
		auto entries = archiveEntries(kW5fWeaponIni);
		entries.push_back({ "_meta/manifest.json",
			"{ \"material_slots_file\": \"bindings/material-slots.json\" }\n" });
		err[0] = '\0';
		TempArchive weapon = writeArchiveEntries("t-assets-024-manifest", entries);
		REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(70,
			weapon.path.string().c_str(), err, sizeof(err)) != 0);
		REQUIRE(std::string(err).find("retired weapon manifest binding material_slots_file") !=
			std::string::npos);
	}
	{
		auto entries = archiveEntries(kW5fWeaponIni);
		entries.push_back({ "bindings/grip-sockets.json",
			"{ \"schema\": \"pd.weapon.grip_sockets.v1\" }\n" });
		err[0] = '\0';
		TempArchive weapon = writeArchiveEntries("t-assets-024-member", entries);
		REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(70,
			weapon.path.string().c_str(), err, sizeof(err)) != 0);
		REQUIRE(std::string(err).find("retired weapon binding member bindings/grip-sockets.json") !=
			std::string::npos);
	}
}

TEST_CASE("weapon settings, variables and presentation register from real *_file archive",
          "[modding][pdxxx][weapon_graph][c3849][settings]") {
	/* Real archive contract: *_file descriptor spellings, canonical schema
	 * spellings, needler flat shapes, $name substitution in the graphs. */
	const std::string settingsJson =
		"{\n"
		"  \"schema\": \"pd.weapon_settings.v1\",\n"
		"  \"asset_id\": \"base:w5f_weapon\",\n"
		"  \"fire_cadence\": { \"value\": 8, \"unit\": \"centiseconds\" },\n"
		"  \"spread\": 1.5,\n"
		"  \"zoom_fov\": 30.0,\n"
		"  \"sight\": \"zoom\"\n"
		"}\n";
	const std::string variablesJson =
		"{\n"
		"  \"schema\": \"pd.weapon_variables.v1\",\n"
		"  \"base_damage\": 6.5,\n"
		"  \"pierce\": 3,\n"
		"  \"cam\": \"xray\"\n"
		"}\n";
	/* settings.json wins the zoom_fov collision; crosshair default is a
	 * no-op. */
	const std::string presentationJson =
		"{ \"schema\": \"pd.weapon.presentation.v1\", \"crosshair\": \"default\","
		" \"zoom_fov\": 45.0 }\n";

	TempArchive weapon = writeArchiveEntries("w5f-settings-weapon", {
		{ "weapon.ini", kW5fWeaponIni },
		{ "behavior/primary.graph.json", kW5fPrimaryGraph },
		{ "behavior/secondary.graph.json", kW5fSecondaryGraph },
		{ "behavior/shared-context.json", kW5fSharedContext },
		{ "behavior/settings.json", settingsJson },
		{ "behavior/variables.json", variablesJson },
		{ "bindings/presentation.json", presentationJson },
	});

	char err[256] = {};
	weaponGraphRuntimeClearAll();
	weaponGraphRuntimeSetEnabled(0);
	INFO(err);
	REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(70,
		weapon.path.string().c_str(), err, sizeof(err)) == 0);

	/* Toggle gate: the ungated accessor serves registration/editor surfaces;
	 * the ForGameplay form is NULL while the runtime is off. */
	const weapon_graph_weapon_settings_t *table =
		weaponGraphRuntimeGetWeaponSettings(70);
	REQUIRE(table != nullptr);
	REQUIRE(weaponGraphRuntimeGetWeaponSettingsForGameplay(70) == nullptr);
	weaponGraphRuntimeSetEnabled(1);
	REQUIRE(weaponGraphRuntimeGetWeaponSettingsForGameplay(70) == table);

	/* 4 settings rows (fire_cadence/spread/zoom_fov/sight; the presentation
	 * zoom_fov loses the collision, crosshair is consumed) + 3 variables. */
	REQUIRE(table->setting_count == 4);
	REQUIRE(table->variable_count == 3);
	bool sawCadenceUnit = false;
	for (s32 i = 0; i < table->setting_count; i++) {
		if (std::string(table->settings[i].key) == "fire_cadence") {
			REQUIRE(std::string(table->settings[i].unit) == "centiseconds");
			sawCadenceUnit = true;
		}
	}
	REQUIRE(sawCadenceUnit);

	const weapon_graph_held_function_t *primary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(70, 0);
	const weapon_graph_held_function_t *secondary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(70, 1);
	REQUIRE(primary != nullptr);
	REQUIRE(secondary != nullptr);

	/* B6.3 $name substitution: float, int and string variables resolve at
	 * compile, typed. */
	REQUIRE(primary->has_damage == 1);
	REQUIRE(primary->damage == Approx(6.5f));
	REQUIRE(primary->has_penetration == 1);
	REQUIRE(primary->penetration == 3);
	REQUIRE(std::string(primary->camera_effect) == "xray");
	REQUIRE(primary->camera_effect_mode == WEAPON_GRAPH_CAMERA_EFFECT_XRAY);

	/* Defaults layering: settings fill ONLY has_* == 0 fields. */
	REQUIRE(primary->has_spread == 1);
	REQUIRE(primary->spread == Approx(1.5f));
	REQUIRE(primary->has_zoom_fov == 1);
	REQUIRE(primary->zoom_fov == Approx(30.0f));
	REQUIRE(primary->has_sight == 1);
	REQUIRE(primary->sight == (u32)SIGHT_ZOOM);
	/* fire_cadence 8 centiseconds = 750 rpm; non-auto functions take the
	 * recovery-ticks mapping (3600/750 -> 5), never has_max_rpm (bondgun
	 * classifies automatics by that bit). */
	REQUIRE(primary->has_max_rpm == 0);
	REQUIRE(primary->has_recoverytime_ticks60 == 1);
	REQUIRE(primary->recoverytime_ticks60 == 5);

	/* Node params always win: secondary authored its own spread. */
	REQUIRE(secondary->has_spread == 1);
	REQUIRE(secondary->spread == Approx(2.5f));
	/* fire.auto_cadence functions take the rpm mapping. */
	REQUIRE(secondary->has_max_rpm == 1);
	REQUIRE(secondary->max_rpm == Approx(750.0f));
	REQUIRE(secondary->has_recoverytime_ticks60 == 0);

	weaponGraphRuntimeSetEnabled(0);
	weaponGraphRuntimeClearAll();
}
