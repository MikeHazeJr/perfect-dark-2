/*
 * Split out of tests/test_mod_external_archive_static.cpp for build time (T-TOOLING-018):
 * part 2 of 3 of its TEST_CASEs, in their original order, with
 * copies of the internal helpers they use. Link stubs stay in the original.
 */
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

namespace {

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

struct TempArchive {
	std::filesystem::path path;

	~TempArchive() {
		if (!path.empty()) {
			std::error_code ec;
			std::filesystem::remove(path, ec);
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

bool archiveHasEntry(mod_archive_t *archive, const std::string &entryPath) {
	return archive && modArchiveFindEntry(archive, entryPath.c_str()) >= 0;
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

std::string readVfsText(const char *path) {
	u32 size = 0;
	void *bytes = modVfsResolveAnyAlloc(path, &size, nullptr, 0);
	REQUIRE(bytes != nullptr);
	std::string text(static_cast<const char *>(bytes), size);
	free(bytes);
	return text;
}


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

TEST_CASE("weapon tunables accept legacy schema spellings and the array shape",
          "[modding][pdxxx][weapon_graph][c3849][settings]") {
	const std::string primaryGraph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:w5f_legacy\",\n"
		"  \"graph_id\": \"primary\",\n"
		"  \"nodes\": [ { \"id\": \"primary_action\", \"kind\": \"fire.hitscan\","
		" \"params\": { \"mode\": \"primary\", \"damage\": \"$dmg\" } } ],\n"
		"  \"exports\": [ { \"name\": \"primary\", \"node\": \"primary_action\" } ]\n"
		"}\n";
	const std::string secondaryGraph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:w5f_legacy\",\n"
		"  \"graph_id\": \"secondary\",\n"
		"  \"nodes\": [ { \"id\": \"secondary_action\", \"kind\": \"fire.hitscan\","
		" \"params\": { \"mode\": \"secondary\" } } ],\n"
		"  \"exports\": [ { \"name\": \"secondary\", \"node\": \"secondary_action\" } ]\n"
		"}\n";
	const std::string weaponIni =
		"[weapon]\n"
		"catalog_id = base:w5f_legacy\n"
		"primary_graph = behavior/primary.graph.json\n"
		"secondary_graph = behavior/secondary.graph.json\n"
		"shared_context_file = behavior/shared-context.json\n"
		"settings_file = behavior/settings.json\n"
		"variables_file = behavior/variables.json\n";
	const std::string sharedContext =
		"{ \"schema\": \"pd.weapon_shared_context.v1\", \"asset_id\": \"base:w5f_legacy\","
		" \"contexts\": [ { \"name\": \"owner_player\", \"scope\": \"player\","
		" \"source\": \"equipped_player\", \"type\": \"player_ref\" } ] }\n";
	/* Legacy needler spellings (accepted with a one-time LOG_WARNING) and the
	 * base "variables": [] ARRAY shape with {name,value,unit} rows. */
	const std::string settingsJson =
		"{ \"schema\": \"pd.weapon.settings.v1\", \"spread\": 2.0 }\n";
	const std::string variablesJson =
		"{ \"schema\": \"pd.weapon.variables.v1\","
		" \"variables\": [ { \"name\": \"dmg\", \"value\": 9.0 } ] }\n";

	TempArchive weapon = writeArchiveEntries("w5f-legacy-weapon", {
		{ "weapon.ini", weaponIni },
		{ "behavior/primary.graph.json", primaryGraph },
		{ "behavior/secondary.graph.json", secondaryGraph },
		{ "behavior/shared-context.json", sharedContext },
		{ "behavior/settings.json", settingsJson },
		{ "behavior/variables.json", variablesJson },
	});

	char err[256] = {};
	weaponGraphRuntimeClearAll();
	INFO(err);
	REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(71,
		weapon.path.string().c_str(), err, sizeof(err)) == 0);

	const weapon_graph_weapon_settings_t *table =
		weaponGraphRuntimeGetWeaponSettings(71);
	REQUIRE(table != nullptr);
	REQUIRE(table->setting_count == 1);
	REQUIRE(table->variable_count == 1);

	const weapon_graph_held_function_t *primary =
		weaponGraphRuntimeGetHeldFunction(71, 0);
	REQUIRE(primary != nullptr);
	REQUIRE(primary->has_damage == 1);
	REQUIRE(primary->damage == Approx(9.0f));
	REQUIRE(primary->has_spread == 1);
	REQUIRE(primary->spread == Approx(2.0f));

	weaponGraphRuntimeClearAll();
}

TEST_CASE("weapon tunables enforce explicit units and loud unresolved variables",
          "[modding][pdxxx][weapon_graph][c3849][settings]") {
	const std::string sharedContext =
		"{ \"schema\": \"pd.weapon_shared_context.v1\", \"asset_id\": \"base:w5f_bad\","
		" \"contexts\": [ { \"name\": \"owner_player\", \"scope\": \"player\","
		" \"source\": \"equipped_player\", \"type\": \"player_ref\" } ] }\n";
	const std::string weaponIni =
		"[weapon]\n"
		"catalog_id = base:w5f_bad\n"
		"primary_graph = behavior/primary.graph.json\n"
		"secondary_graph = behavior/secondary.graph.json\n"
		"shared_context_file = behavior/shared-context.json\n"
		"settings_file = behavior/settings.json\n"
		"variables_file = behavior/variables.json\n";
	const std::string okGraphPrimary =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:w5f_bad\",\n"
		"  \"graph_id\": \"primary\",\n"
		"  \"nodes\": [ { \"id\": \"primary_action\", \"kind\": \"fire.hitscan\","
		" \"params\": { \"mode\": \"primary\" } } ],\n"
		"  \"exports\": [ { \"name\": \"primary\", \"node\": \"primary_action\" } ]\n"
		"}\n";
	const std::string okGraphSecondary =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:w5f_bad\",\n"
		"  \"graph_id\": \"secondary\",\n"
		"  \"nodes\": [ { \"id\": \"secondary_action\", \"kind\": \"fire.hitscan\","
		" \"params\": { \"mode\": \"secondary\" } } ],\n"
		"  \"exports\": [ { \"name\": \"secondary\", \"node\": \"secondary_action\" } ]\n"
		"}\n";
	const std::string emptyVariables =
		"{ \"schema\": \"pd.weapon_variables.v1\", \"variables\": [] }\n";

	char err[256] = {};
	weaponGraphRuntimeClearAll();

	{
		/* Time-like settings key without an explicit unit: rejected via the
		 * keyNeedsUnit/keyHasUnit validator pattern. */
		const std::string badSettings =
			"{ \"schema\": \"pd.weapon_settings.v1\", \"arm_timer\": 5 }\n";
		TempArchive weapon = writeArchiveEntries("w5f-unit-reject", {
			{ "weapon.ini", weaponIni },
			{ "behavior/primary.graph.json", okGraphPrimary },
			{ "behavior/secondary.graph.json", okGraphSecondary },
			{ "behavior/shared-context.json", sharedContext },
			{ "behavior/settings.json", badSettings },
			{ "behavior/variables.json", emptyVariables },
		});
		err[0] = '\0';
		REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(72,
			weapon.path.string().c_str(), err, sizeof(err)) != 0);
		REQUIRE(std::string(err).find("explicit units") != std::string::npos);
	}

	{
		/* Unresolved $ref against a PRESENT (empty) variables table is a loud
		 * compile failure, like any other validation error. */
		const std::string dollarGraph =
			"{\n"
			"  \"schema\": \"pd.weapon_graph.v1\",\n"
			"  \"asset_id\": \"base:w5f_bad\",\n"
			"  \"graph_id\": \"primary\",\n"
			"  \"nodes\": [ { \"id\": \"primary_action\", \"kind\": \"fire.hitscan\","
			" \"params\": { \"mode\": \"primary\", \"damage\": \"$missing\" } } ],\n"
			"  \"exports\": [ { \"name\": \"primary\", \"node\": \"primary_action\" } ]\n"
			"}\n";
		const std::string okSettings =
			"{ \"schema\": \"pd.weapon_settings.v1\" }\n";
		TempArchive weapon = writeArchiveEntries("w5f-unresolved-ref", {
			{ "weapon.ini", weaponIni },
			{ "behavior/primary.graph.json", dollarGraph },
			{ "behavior/secondary.graph.json", okGraphSecondary },
			{ "behavior/shared-context.json", sharedContext },
			{ "behavior/settings.json", okSettings },
			{ "behavior/variables.json", emptyVariables },
		});
		err[0] = '\0';
		REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(72,
			weapon.path.string().c_str(), err, sizeof(err)) != 0);
		REQUIRE(std::string(err).find("unresolved weapon variable") != std::string::npos);
	}

	{
		/* Public settings are a production contract, not an arbitrary key/value
		 * bag. A key without a real gameplay consumer must fail registration. */
		const std::string unsupportedSettings =
			"{ \"schema\": \"pd.weapon_settings.v1\", \"ammo_clip\": 12 }\n";
		TempArchive weapon = writeArchiveEntries("w5f-unsupported-setting", {
			{ "weapon.ini", weaponIni },
			{ "behavior/primary.graph.json", okGraphPrimary },
			{ "behavior/secondary.graph.json", okGraphSecondary },
			{ "behavior/shared-context.json", sharedContext },
			{ "behavior/settings.json", unsupportedSettings },
			{ "behavior/variables.json", emptyVariables },
		});
		err[0] = '\0';
		REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(72,
			weapon.path.string().c_str(), err, sizeof(err)) != 0);
		REQUIRE(std::string(err).find("has no production consumer") !=
			std::string::npos);
	}

	{
		/* presentation.v1 likewise rejects fields that the production renderer
		 * does not consume. This prevents a creator-facing reticle path from
		 * appearing to work while being silently ignored. */
		const std::string presentationIni = weaponIni +
			"presentation_file = bindings/presentation.json\n";
		const std::string okSettings =
			"{ \"schema\": \"pd.weapon_settings.v1\" }\n";
		const std::string unsupportedPresentation =
			"{ \"schema\": \"pd.weapon.presentation.v1\", "
			"\"reticle\": \"example:tri_reticle\" }\n";
		TempArchive weapon = writeArchiveEntries("w5f-unsupported-presentation", {
			{ "weapon.ini", presentationIni },
			{ "behavior/primary.graph.json", okGraphPrimary },
			{ "behavior/secondary.graph.json", okGraphSecondary },
			{ "behavior/shared-context.json", sharedContext },
			{ "behavior/settings.json", okSettings },
			{ "behavior/variables.json", emptyVariables },
			{ "bindings/presentation.json", unsupportedPresentation },
		});
		err[0] = '\0';
		REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(72,
			weapon.path.string().c_str(), err, sizeof(err)) != 0);
		REQUIRE(std::string(err).find("has no production consumer") !=
			std::string::npos);
	}

	weaponGraphRuntimeClearAll();
}

TEST_CASE("base-shaped empty tunables layer zero defaults",
          "[modding][pdxxx][weapon_graph][c3849][settings]") {
	/* The base emitter authors settings with only envelope keys and an empty
	 * variables array (romextract_pdweapon.c); the defaults layer must stay
	 * empty so toggle-ON base behavior is untouched. */
	const std::string weaponIni =
		"[weapon]\n"
		"catalog_id = base:w5f_base_shape\n"
		"primary_graph = behavior/primary.graph.json\n"
		"secondary_graph = behavior/secondary.graph.json\n"
		"shared_context_file = behavior/shared-context.json\n"
		"settings_file = behavior/settings.json\n"
		"variables_file = behavior/variables.json\n";
	const std::string primaryGraph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:w5f_base_shape\",\n"
		"  \"graph_id\": \"primary\",\n"
		"  \"nodes\": [ { \"id\": \"primary_action\", \"kind\": \"fire.hitscan\","
		" \"params\": { \"mode\": \"primary\" } } ],\n"
		"  \"exports\": [ { \"name\": \"primary\", \"node\": \"primary_action\" } ]\n"
		"}\n";
	const std::string secondaryGraph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:w5f_base_shape\",\n"
		"  \"graph_id\": \"secondary\",\n"
		"  \"nodes\": [ { \"id\": \"secondary_action\", \"kind\": \"fire.hitscan\","
		" \"params\": { \"mode\": \"secondary\" } } ],\n"
		"  \"exports\": [ { \"name\": \"secondary\", \"node\": \"secondary_action\" } ]\n"
		"}\n";
	const std::string sharedContext =
		"{ \"schema\": \"pd.weapon_shared_context.v1\", \"asset_id\": \"base:w5f_base_shape\","
		" \"contexts\": [ { \"name\": \"owner_player\", \"scope\": \"player\","
		" \"source\": \"equipped_player\", \"type\": \"player_ref\" } ] }\n";
	const std::string baseSettings =
		"{\n"
		"  \"schema\": \"pd.weapon_settings.v1\",\n"
		"  \"asset_id\": \"base:w5f_base_shape\",\n"
		"  \"dependency_closure\": \"complete_typed_archive_dependencies\"\n"
		"}\n";
	const std::string baseVariables =
		"{\n"
		"  \"schema\": \"pd.weapon_variables.v1\",\n"
		"  \"asset_id\": \"base:w5f_base_shape\",\n"
		"  \"variables\": []\n"
		"}\n";

	TempArchive weapon = writeArchiveEntries("w5f-base-shape", {
		{ "weapon.ini", weaponIni },
		{ "behavior/primary.graph.json", primaryGraph },
		{ "behavior/secondary.graph.json", secondaryGraph },
		{ "behavior/shared-context.json", sharedContext },
		{ "behavior/settings.json", baseSettings },
		{ "behavior/variables.json", baseVariables },
	});

	char err[256] = {};
	weaponGraphRuntimeClearAll();
	INFO(err);
	REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(73,
		weapon.path.string().c_str(), err, sizeof(err)) == 0);

	const weapon_graph_weapon_settings_t *table =
		weaponGraphRuntimeGetWeaponSettings(73);
	REQUIRE(table != nullptr);
	REQUIRE(table->setting_count == 0);
	REQUIRE(table->variable_count == 0);

	const weapon_graph_held_function_t *primary =
		weaponGraphRuntimeGetHeldFunction(73, 0);
	REQUIRE(primary != nullptr);
	REQUIRE(primary->has_spread == 0);
	REQUIRE(primary->has_zoom_fov == 0);
	REQUIRE(primary->has_sight == 0);
	REQUIRE(primary->has_max_rpm == 0);
	REQUIRE(primary->has_recoverytime_ticks60 == 0);

	weaponGraphRuntimeClearAll();
}

TEST_CASE("camera_effect selects supported modes and rejects unknown authored modes",
          "[modding][pdxxx][weapon_graph][c3849][settings]") {
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:w5f_camera\",\n"
		"  \"graph_id\": \"camera_test\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"primary_action\", \"kind\": \"fire.beam_tick\","
		" \"params\": { \"mode\": \"primary\", \"camera_effect\": \"xray\" } },\n"
		"    { \"id\": \"secondary_action\", \"kind\": \"fire.beam_tick\","
		" \"params\": { \"mode\": \"secondary\", \"camera_effect\": \"none\" } }\n"
		"  ],\n"
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
	REQUIRE(weaponGraphRuntimeRegisterHeldIr(74, &ir, err, sizeof(err)) == 0);
	const weapon_graph_held_function_t *primary =
		weaponGraphRuntimeGetHeldFunction(74, 0);
	const weapon_graph_held_function_t *secondary =
		weaponGraphRuntimeGetHeldFunction(74, 1);
	REQUIRE(primary != nullptr);
	REQUIRE(secondary != nullptr);
	REQUIRE(primary->camera_effect_mode == WEAPON_GRAPH_CAMERA_EFFECT_XRAY);
	REQUIRE(secondary->camera_effect_mode == WEAPON_GRAPH_CAMERA_EFFECT_NONE);
	weaponGraphIrFree(&ir);
	std::string invalid = graph;
	invalid.replace(invalid.find("\"none\""), std::strlen("\"none\""), "\"wobble\"");
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, invalid.data(),
		static_cast<u32>(invalid.size()), &ir, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("camera_effect") != std::string::npos);
	weaponGraphRuntimeClearAll();
}

TEST_CASE("graph catalog references reject numeric identities and preserve optional nulls",
		"[modding][pdxxx][weapon_graph][c3842][source_authority]") {
	const char *keys[] = { "projectile_ref", "entity_ref", "payload_ref",
		"explosion_ref", "spark_ref", "recover_weapon_ref", "detonator_ref",
		"model_catalog_id", "model_ref", "projectile_model_catalog_id",
		"projectile_model_ref", "shoot_sound_catalog_id", "shoot_sound_ref",
		"shootsound", "projectile_sound_catalog_id", "special_sound_catalog_id",
		"sound_catalog_id", "soundnum", "hit_sound", "sound" };
	for (const char *key : keys) {
		for (const char *value : { "42", "42.0", "true", "[]", "{}",
				"\"MODEL_rocket\"", "\"SFX_804D\"", "null", "\"\"", "\"mod:source\"" }) {
			CAPTURE(key, value);
			const std::string graph = std::string(
				"{\"schema\":\"pd.weapon_graph.v1\",\"asset_id\":\"mod:refs\","
				"\"graph_id\":\"primary\",\"nodes\":[{\"id\":\"fire\",\"kind\":\"fire.hitscan\","
				"\"params\":{\"mode\":\"primary\",\"") + key + "\":" + value +
				"}}],\"exports\":[{\"name\":\"primary\",\"node\":\"fire\"}]}";
			weapon_graph_ir_t ir;
			char err[256] = {};
			const s32 result = weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
				static_cast<u32>(graph.size()), &ir, err, sizeof(err));
			const bool valid = std::string(value) == "null" ||
				std::string(value) == "\"\"" || std::string(value) == "\"mod:source\"";
			if (valid) {
				REQUIRE(result == 0);
				weaponGraphIrFree(&ir);
			} else {
				REQUIRE(result != 0);
				REQUIRE(std::string(err).find(key) != std::string::npos);
			}
		}
	}
}

TEST_CASE("graph model and audio activation requires catalog resolution for base and mod IDs",
		"[modding][pdxxx][weapon_graph][runtime][c3842][source_authority]") {
	struct Family { asset_type_e type; const char *schema; const char *kind; const char *model_key; };
	const Family families[] = {
		{ ASSET_WEAPON, "pd.weapon_graph.v1", "spawn.fired_projectile", "projectile_model_catalog_id" },
		{ ASSET_PROJECTILE, "pd.projectile_graph.v1", "projectile.spawn_state", "model_catalog_id" },
		{ ASSET_ENTITY, "pd.entity_graph.v1", "entity.armed_explosive", "model_catalog_id" },
	};
	for (const auto &family : families) {
		for (const char *model_id : { "base:model_chrdyrocketmis", "mod:rocket_mesh" }) {
			CAPTURE(family.schema, model_id);
			weaponGraphRuntimeClearAll();
			ScopedGraphCatalogSources sources;
			const std::string audio_node = family.type == ASSET_WEAPON ?
				",\"sound_catalog_id\":\"base:sfx_launch_rocket\"}}" :
				std::string("}},{\"id\":\"audio\",\"kind\":\"") +
				(family.type == ASSET_PROJECTILE ? "projectile.impact" : "entity.interaction") +
				"\",\"params\":{\"" + (family.type == ASSET_PROJECTILE ? "hit_sound" : "sound") +
				"\":\"base:sfx_launch_rocket\"}}";
			const std::string graph = std::string("{\"schema\":\"") + family.schema +
				"\",\"asset_id\":\"mod:resolved_graph\",\"graph_id\":\"primary\",\"nodes\":["
				"{\"id\":\"module\",\"kind\":\"" + family.kind + "\",\"params\":{"
				"\"mode\":\"primary\",\"" + family.model_key + "\":\"" + model_id +
				"\"" + audio_node + "],"
				"\"exports\":[{\"name\":\"primary\",\"node\":\"module\"}]}";
			weapon_graph_ir_t ir;
			char err[256] = {};
			REQUIRE(weaponGraphCompileJson(family.type, graph.data(),
				static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);
			auto activate = [&]() {
				if (family.type == ASSET_WEAPON) return weaponGraphRuntimeRegisterHeldIr(7, &ir, err, sizeof(err));
				if (family.type == ASSET_PROJECTILE) return weaponGraphRuntimeRegisterProjectileIr(&ir, err, sizeof(err));
				return weaponGraphRuntimeRegisterEntityIr(&ir, err, sizeof(err));
			};
			REQUIRE(activate() != 0);
			std::snprintf(sources.first.id, sizeof(sources.first.id), "%s", model_id);
			sources.first.type = ASSET_MODEL;
			sources.first.runtime_index = 117;
			testStubAssetCatalogResolveWith(&sources.first);
			REQUIRE(activate() != 0);
			REQUIRE(std::string(err).find("base:sfx_launch_rocket") != std::string::npos);
			testStubEffectAudio("base:sfx_launch_rocket", 0, -1);
			REQUIRE(activate() != 0);
			testStubEffectAudio("base:sfx_launch_rocket", 0, 901);
			REQUIRE(activate() == 0);
			if (family.type == ASSET_WEAPON) {
				const auto *runtime = weaponGraphRuntimeGetHeldFunction(7, 0);
				REQUIRE(runtime != nullptr);
				REQUIRE(runtime->projectile_modelnum == 117);
				REQUIRE(runtime->soundnum == 901);
			} else if (family.type == ASSET_PROJECTILE) {
				const auto *runtime = weaponGraphRuntimeGetProjectile("mod:resolved_graph");
				REQUIRE(runtime != nullptr);
				REQUIRE(runtime->projectile_modelnum == 117);
				REQUIRE(runtime->impact_hit_sound == 901);
			} else {
				const auto *runtime = weaponGraphRuntimeGetEntity("mod:resolved_graph");
				REQUIRE(runtime != nullptr);
				REQUIRE(runtime->projectile_modelnum == 117);
				REQUIRE(runtime->interaction_sound == 901);
			}
			weaponGraphIrFree(&ir);
			weaponGraphRuntimeClearAll();
		}
	}
}

TEST_CASE("graph policy vocabularies reject authored fallback and retain supported values",
		"[modding][pdxxx][weapon_graph][c3842][source_authority]") {
	struct Policy { asset_type_e type; const char *schema; const char *kind; const char *key; const char *good; const char *bad; };
	const Policy cases[] = {
		{ ASSET_PROJECTILE, "pd.projectile_graph.v1", "projectile.timer", "timer_starts", "on_attach", "whenever" },
		{ ASSET_PROJECTILE, "pd.projectile_graph.v1", "projectile.timer", "on_expire", "delete", "confetti" },
		{ ASSET_PROJECTILE, "pd.projectile_graph.v1", "projectile.impact", "impact_filter", "chr", "solid_or_chr" },
		{ ASSET_PROJECTILE, "pd.projectile_graph.v1", "projectile.trail", "trail_type", "rocket", "sparkles" },
		{ ASSET_PROJECTILE, "pd.projectile_graph.v1", "projectile.wall_hugger", "fall_vector", "1,2,3", "1,2,3junk" },
		{ ASSET_PROJECTILE, "pd.projectile_graph.v1", "projectile.wall_hugger", "fall_vector", "down", "NaN,2,3" },
		{ ASSET_ENTITY, "pd.entity_graph.v1", "entity.armed_explosive", "damage_response", "ignore", "maybe" },
		{ ASSET_ENTITY, "pd.entity_graph.v1", "entity.proxy_trigger", "on_trigger", "storm", "maybe" },
		{ ASSET_ENTITY, "pd.entity_graph.v1", "entity.proxy_trigger", "target_filter", "hostile_chr", "friendly_chr" },
		{ ASSET_ENTITY, "pd.entity_graph.v1", "entity.proxy_trigger", "team_filter", "enemy_only", "maybe" },
		{ ASSET_ENTITY, "pd.entity_graph.v1", "entity.proxy_trigger", "owner_filter", "owner_only", "maybe" },
		{ ASSET_ENTITY, "pd.entity_graph.v1", "entity.remote_detonatable", "on_remote_signal", "create_storm", "maybe" },
		{ ASSET_ENTITY, "pd.entity_graph.v1", "entity.timed_detonatable", "on_expire", "delete", "maybe" },
		{ ASSET_ENTITY, "pd.entity_graph.v1", "entity.timed_detonatable", "starts_when", "armed", "maybe" },
		{ ASSET_ENTITY, "pd.entity_graph.v1", "entity.autogun", "net_authority", "server", "client" },
	};
	for (const auto &policy : cases) {
		for (bool valid : { true, false }) {
			CAPTURE(policy.kind, policy.key, valid);
			const std::string graph = std::string("{\"schema\":\"") + policy.schema +
				"\",\"asset_id\":\"mod:policy\",\"graph_id\":\"main\",\"nodes\":["
				"{\"id\":\"module\",\"kind\":\"" + policy.kind + "\",\"params\":{\"" +
				policy.key + "\":\"" + (valid ? policy.good : policy.bad) + "\"}}]}";
			weapon_graph_ir_t ir;
			char err[256] = {};
			const s32 result = weaponGraphCompileJson(policy.type, graph.data(),
				static_cast<u32>(graph.size()), &ir, err, sizeof(err));
			if (valid) {
				REQUIRE(result == 0);
				weaponGraphIrFree(&ir);
			} else {
				REQUIRE(result != 0);
				REQUIRE(std::string(err).find(policy.key) != std::string::npos);
			}
		}
	}
}

TEST_CASE("weapon settings cadence units select real native timing or reject the archive",
		"[modding][pdxxx][weapon_graph][settings][c3842][source_authority]") {
	for (const char *shape : { "flat", "array", "nested_array" }) {
	for (const char *unit : { "rpm", "centiseconds", "ticks60", "frames_maybe" }) {
		CAPTURE(shape, unit);
		weaponGraphRuntimeClearAll();
		const std::string settingValue = std::string("{\"value\":60,\"unit\":\"") + unit + "\"}";
		const std::string settings = std::string("{\"schema\":\"pd.weapon_settings.v1\",") +
			(std::string(shape) == "flat" ? "\"fire_cadence\":" + settingValue :
			 std::string(shape) == "array" ?
			 std::string("\"settings\":[{\"name\":\"fire_cadence\",\"value\":60,\"unit\":\"") + unit + "\"}]" :
			 "\"settings\":[{\"name\":\"fire_cadence\",\"value\":" + settingValue +
			 ",\"unit\":\"ignored_outer_unit\"}]") + "}";
		TempArchive weapon = writeArchiveEntries("source-authority-cadence", {
			{ "weapon.ini", kW5fWeaponIni },
			{ "behavior/primary.graph.json", kW5fPrimaryGraph },
			{ "behavior/secondary.graph.json", kW5fSecondaryGraph },
			{ "behavior/shared-context.json", kW5fSharedContext },
			{ "behavior/settings.json", settings },
			{ "behavior/variables.json", "{\"schema\":\"pd.weapon_variables.v1\",\"base_damage\":1,\"pierce\":0,\"cam\":\"none\"}" },
			{ "bindings/presentation.json", "{\"schema\":\"pd.weapon.presentation.v1\"}" },
		});
		char err[256] = {};
		const s32 result = weaponGraphRuntimeRegisterWeaponArchive(70,
			weapon.path.string().c_str(), err, sizeof(err));
		if (std::string(unit) == "frames_maybe") {
			REQUIRE(result != 0);
			REQUIRE(std::string(err).find("fire_cadence") != std::string::npos);
			REQUIRE(weaponGraphRuntimeGetHeldFunction(70, 0) == nullptr);
		} else {
			REQUIRE(result == 0);
			const auto *primary = weaponGraphRuntimeGetHeldFunction(70, 0);
			const auto *secondary = weaponGraphRuntimeGetHeldFunction(70, 1);
			REQUIRE(primary != nullptr);
			REQUIRE(secondary != nullptr);
			const float rpm = std::string(unit) == "centiseconds" ? 100.0f : 60.0f;
			REQUIRE(primary->recoverytime_ticks60 == static_cast<s32>(3600.0f / rpm));
			REQUIRE(secondary->max_rpm == Approx(rpm));
		}
		weaponGraphRuntimeClearAll();
	}
	}
}

TEST_CASE("weapon tunables validate complete scalar values before runtime publication",
		"[modding][pdxxx][weapon_graph][settings][source_authority][T-ASSETS-041]") {
	for (const char *shape : { "flat", "array" }) {
	for (const char *source : { "settings", "variables" }) {
	for (const char *value : { "0", "9", "9.25", "1e20", "9oops", "1e1000", "1e100",
			"1e-1000", "1e-100", "4294967296", "-2147483649", "01", "+1", "0x10",
			".5", "1.", "truejunk", "falsejunk", "nulljunk" }) {
		CAPTURE(shape, source, value);
		weaponGraphRuntimeClearAll();
		const bool variables = std::string(source) == "variables";
		const bool valid = std::string(value) == "0" || std::string(value) == "9" ||
			std::string(value) == "9.25" || std::string(value) == "1e20";
		const std::string key = variables ? "base_damage" : "zoom_fov";
		std::string authored = std::string("{\"schema\":\"pd.weapon_") + source + ".v1\",";
		if (std::string(shape) == "array") {
			authored += std::string("\"") + source + "\":[{\"name\":\"" + key + "\",\"value\":" + value + "}]";
		} else {
			authored += "\"" + key + "\":" + value;
		}
		if (variables) authored += ",\"pierce\":0,\"cam\":\"none\"";
		authored += "}";
		TempArchive weapon = writeW5fTunablesArchive(
			variables ? "{\"schema\":\"pd.weapon_settings.v1\"}" : authored,
			variables ? authored : "{\"schema\":\"pd.weapon_variables.v1\",\"base_damage\":1,\"pierce\":0,\"cam\":\"none\"}");
		char err[256] = {};
		const s32 result = weaponGraphRuntimeRegisterWeaponArchive(70,
			weapon.path.string().c_str(), err, sizeof(err));
		INFO(err);
		if (valid) {
			REQUIRE(result == 0);
			const auto *held = weaponGraphRuntimeGetHeldFunction(70, 0);
			REQUIRE(held != nullptr);
			REQUIRE((variables ? held->damage : held->zoom_fov) == Approx(std::stof(value)));
		} else {
			REQUIRE(result != 0);
			const std::string token(value);
			const bool invalidDocument = token == "9oops" || token == "1e1000" ||
				token == "1e-1000" || token == "01" || token == "+1" || token == "0x10" ||
				token == ".5" || token == "1." || token == "truejunk" ||
				token == "falsejunk" || token == "nulljunk";
			REQUIRE(std::string(err).find(invalidDocument ?
				std::string("weapon ") + source + " source must be a complete strict JSON object" : key)
				!= std::string::npos);
			REQUIRE(weaponGraphRuntimeGetHeldFunction(70, 0) == nullptr);
		}
		weaponGraphRuntimeClearAll();
	}
	}
	}
}

TEST_CASE("weapon variable units and integer destinations validate before native capture",
		"[modding][pdxxx][weapon_graph][settings][source_authority][T-ASSETS-041]") {
	const std::string settings = "{\"schema\":\"pd.weapon_settings.v1\"}";
	const std::string baseVariables = "{\"schema\":\"pd.weapon_variables.v1\",\"base_damage\":1,\"pierce\":0,\"cam\":\"none\",";
	for (const char *unit : { "\"ticks60\"", "12", "null" }) {
		CAPTURE(unit);
		weaponGraphRuntimeClearAll();
		const std::string variables = baseVariables +
			"\"variables\":[{\"name\":\"warmup_time\",\"value\":12,\"unit\":" + unit + "}]}";
		std::string primary = kW5fPrimaryGraph;
		primary.replace(primary.find("\"camera_effect\":"), 16,
			"\"activation_time_ticks60\":\"$warmup_time\",\"camera_effect\":");
		TempArchive weapon = writeW5fTunablesArchive(settings, variables, primary);
		char err[256] = {};
		const s32 result = weaponGraphRuntimeRegisterWeaponArchive(70,
			weapon.path.string().c_str(), err, sizeof(err));
		if (std::string(unit) == "\"ticks60\"") {
			REQUIRE(result == 0);
			const auto *held = weaponGraphRuntimeGetHeldFunction(70, 0);
			REQUIRE(held != nullptr);
			REQUIRE(held->activation_time_ticks60 == 12);
		} else {
			REQUIRE(result != 0);
			REQUIRE(std::string(err).find("unit") != std::string::npos);
		}
		weaponGraphRuntimeClearAll();
	}
	for (const char *value : { "12.75", "\"12\"", "null", "\"\"",
			"2147483648.0", "\"2147483648\"", "\"12junk\"" }) {
		CAPTURE(value);
		weaponGraphRuntimeClearAll();
		std::string primary = kW5fPrimaryGraph;
		primary.replace(primary.find("\"camera_effect\":"), 16,
			std::string("\"flags\":\"$mask\",\"burst_count\":") + value + ",\"camera_effect\":");
		TempArchive weapon = writeW5fTunablesArchive(settings,
			baseVariables + "\"mask\":4294967295}", primary);
		char err[256] = {};
		const s32 result = weaponGraphRuntimeRegisterWeaponArchive(70,
			weapon.path.string().c_str(), err, sizeof(err));
		const bool omitted = std::string(value) == "null" || std::string(value) == "\"\"";
		if (std::string(value) == "12.75" || std::string(value) == "\"12\"" || omitted) {
			REQUIRE(result == 0);
			const auto *held = weaponGraphRuntimeGetHeldFunction(70, 0);
			REQUIRE(held != nullptr);
			REQUIRE(held->has_burst_count == (omitted ? 0 : 1));
			if (!omitted) REQUIRE(held->burst_count == 12);
			REQUIRE(held->flags == 0xffffffffu);
		} else {
			REQUIRE(result != 0);
			REQUIRE(std::string(err).find("burst_count") != std::string::npos);
		}
		weaponGraphRuntimeClearAll();
	}
}

TEST_CASE("weapon cadence conversion stays inside native counter and float ranges",
		"[modding][pdxxx][weapon_graph][settings][source_authority][T-ASSETS-041]") {
	for (const auto &setting : std::vector<std::pair<std::string, std::string>>{
			{ "1e-30", "rpm" }, { "1e-40", "centiseconds" }, { "1e30", "ticks60" } }) {
		CAPTURE(setting.first, setting.second);
		weaponGraphRuntimeClearAll();
		TempArchive weapon = writeW5fTunablesArchive(
			"{\"schema\":\"pd.weapon_settings.v1\",\"fire_cadence\":{\"value\":" +
			setting.first + ",\"unit\":\"" + setting.second + "\"}}",
			"{\"schema\":\"pd.weapon_variables.v1\",\"base_damage\":1,\"pierce\":0,\"cam\":\"none\"}");
		char err[256] = {};
		REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(70,
			weapon.path.string().c_str(), err, sizeof(err)) != 0);
		REQUIRE(std::string(err).find("timing range") != std::string::npos);
		REQUIRE(weaponGraphRuntimeGetHeldFunction(70, 0) == nullptr);
		weaponGraphRuntimeClearAll();
	}
	REQUIRE(weaponGraphAutogunFireInterval(1e-30f) == 2147483647);
	REQUIRE(weaponGraphAutogunFireInterval(900.0f) == 2);
}

TEST_CASE("graph compiler decodes semantic keys and IDs before runtime registration",
		"[modding][pdxxx][weapon_graph][compiler][source_authority][T-ASSETS-045]") {
	const std::string escaped = R"({
		"extras":{"schema":"ignored nested metadata","nodes":[false]},
		"sch\u0065ma":"pd.weapon_graph.v1","asset_id":"base:w5f_weapon","graph_id":"primary",
		"no\u0064es":[{"i\u0064":"fi\u0072e\u00e9","kind":"fire.hitscan",
		"pa\u0072ams":{"mode":"primary","function_type":"shoot_single","\u0064amage":9.25,
		"curve":[1,2.50,-3e0],"label":"line\nquote\"slash\\"}}],
		"exports":[{"name":"pri\u006dary","node":"fire\u00e9"}]})";
	std::string literal = escaped;
	for (const auto &pair : std::vector<std::pair<std::string, std::string>>{
			{ "\\u0065", "e" }, { "\\u0064", "d" }, { "\\u0072", "r" },
			{ "\\u006d", "m" }, { "\\u00e9", "\xc3\xa9" } }) {
		size_t pos;
		while ((pos = literal.find(pair.first)) != std::string::npos)
			literal.replace(pos, pair.first.size(), pair.second);
	}
	weapon_graph_ir_t a{}, b{};
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, escaped.data(),
		static_cast<u32>(escaped.size()), &a, err, sizeof(err)) == 0);
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, literal.data(),
		static_cast<u32>(literal.size()), &b, err, sizeof(err)) == 0);
	REQUIRE(std::string(a.nodes[0].id) == "fire\xc3\xa9");
	REQUIRE(std::string(a.exports[0].name) == "primary");
	REQUIRE(a.exports[0].node == 0);
	REQUIRE(std::string(a.ir_sha256) == b.ir_sha256);
	REQUIRE(std::string(a.source_sha256) != b.source_sha256);
	bool sawArray = false, sawLabel = false;
	for (s32 i = 0; i < a.param_count; i++) {
		if (std::string(a.params[i].key) == "curve") {
			sawArray = true;
			REQUIRE(a.params[i].type == WEAPON_GRAPH_PARAM_ARRAY);
			REQUIRE(std::string(a.params[i].value) == "[1,2.50,-3e0]");
		}
		if (std::string(a.params[i].key) == "label") {
			sawLabel = true;
			REQUIRE(std::string(a.params[i].value) == "line\nquote\"slash\\");
		}
	}
	REQUIRE(sawArray);
	REQUIRE(sawLabel);
	weaponGraphIrFree(&a);
	weaponGraphIrFree(&b);
	weaponGraphRuntimeClearAll();
	REQUIRE(weaponGraphRuntimeRegisterWeaponGraphJson(70, "base:w5f_weapon",
		escaped.data(), static_cast<u32>(escaped.size()), err, sizeof(err)) == 0);
	const auto *held = weaponGraphRuntimeGetHeldFunction(70, 0);
	REQUIRE(held != nullptr);
	REQUIRE(held->damage == Approx(9.25f));
	REQUIRE(std::string(held->node_id) == "fire\xc3\xa9");
	weaponGraphRuntimeClearAll();
}

TEST_CASE("graph JSON validation preserves numeric token types and unsigned masks",
		"[modding][pdxxx][weapon_graph][compiler][source_authority][T-ASSETS-045]") {
	std::string integerDigest;
	for (const char *number : { "1", "1.0", "1e0" }) {
		const std::string graph = std::string(R"({"schema":"pd.weapon_graph.v1",
			"asset_id":"base:w5f_weapon","graph_id":"primary","nodes":[{"id":"fire",
			"kind":"fire.hitscan","params":{"mode":"primary","flags":4294967295,"damage":)") +
			number + R"(}}],"exports":[{"name":"primary","node":"fire"}]})";
		weapon_graph_ir_t ir{};
		char err[256] = {};
		REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
			static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);
		REQUIRE(std::string(ir.params[0].key) == "damage");
		REQUIRE(ir.params[0].type == (std::string(number) == "1" ?
			WEAPON_GRAPH_PARAM_INT : WEAPON_GRAPH_PARAM_FLOAT));
		REQUIRE(std::string(ir.params[1].key) == "flags");
		REQUIRE(ir.params[1].type == WEAPON_GRAPH_PARAM_INT);
		REQUIRE(ir.params[1].i_value == -1);
		REQUIRE(std::string(ir.params[1].value) == "4294967295");
		if (std::string(number) == "1") integerDigest = ir.ir_sha256;
		else REQUIRE(std::string(ir.ir_sha256) != integerDigest);
		weaponGraphIrFree(&ir);
	}
}

TEST_CASE("decoded graph channel strings reach actual prop lifecycle callbacks",
		"[modding][pdxxx][weapon_graph][prop_graph][source_authority][T-ASSETS-045]") {
	const std::string graph = R"({"sch\u0065ma":"pd.prop_behavior.v1","asset_id":"mod:decoded_prop",
		"graph_id":"runtime","nodes":[{"id":"sp\u0061wn","kind":"event.spawn"},
		{"id":"channel\u00e9","kind":"action.set_channel",
		"params":{"ch\u0061nnel":"ready\n\u00e9\ud83d\ude80\/\\\"","value":true}}],
		"edges":[{"from":"spawn","to":"channel\u00e9"}]})";
	propGraphRuntimeReset();
	char err[256] = {};
	REQUIRE(propGraphRuntimeRegisterJson("mod:decoded_prop", graph.data(),
		static_cast<u32>(graph.size()), err, sizeof(err)) == 0);
	struct State { std::string channel; s32 value = 0; } state;
	prop_graph_runtime_target_t target = {
		&state, [](void *) -> s32 { return 1; }, [](void *, s32) {},
		[](void *, f32) {}, [](void *, s32) {},
		[](void *context, const char *channel, s32 value) {
			auto *state = static_cast<State *>(context);
			state->channel = channel;
			state->value = value;
		}
	};
	REQUIRE(propGraphRuntimeBind("mod:decoded_prop", 45, &target) == 1);
	REQUIRE(state.channel == "ready\n\xc3\xa9\xf0\x9f\x9a\x80/\\\"");
	REQUIRE(state.value == 1);
	propGraphRuntimeReset();
}

TEST_CASE("graph compiler rejects malformed documents rows exports and retained strings",
		"[modding][pdxxx][weapon_graph][compiler][source_authority][T-ASSETS-045]") {
	const std::string prefix = R"({"schema":"pd.weapon_graph.v1","asset_id":"base:w5f_weapon","graph_id":"primary",)";
	const std::string nodes = R"("nodes":[{"id":"fire","kind":"fire.hitscan","params":{"mode":"primary"}}])";
	const std::string exports = R"("exports":[{"name":"primary","node":"fire"}])";
	const std::string valid = prefix + nodes + ',' + exports + '}';
	std::vector<std::pair<std::string, std::string>> invalid = {
		{ valid + " trailing", "strict JSON" },
		{ prefix + nodes + ',' + exports + ",}", "strict JSON" },
		{ prefix + nodes + R"(,"no\u0064es":[],)" + exports + '}', "strict JSON" },
		{ prefix + nodes + ',' + exports + R"(,"ignored":{"x":1,"x":2}})", "strict JSON" },
		{ prefix + nodes + ',' + exports + R"(,"ignored":"\q"})", "strict JSON" },
		{ prefix + nodes + ',' + exports + R"(,"ignored":"\ud800"})", "strict JSON" },
		{ prefix + nodes + ',' + exports + R"(,"ignored":"\u0000"})", "strict JSON" },
		{ prefix + nodes + R"(,"exports":[{"name":"primary","node":"fire"},{"name":"pri\u006dary","node":"fire"}]})", "duplicate export name" },
		{ prefix + nodes + R"(,"exports":[{"name":"","node":"fire"}]})", "export missing name/node" },
		{ prefix + nodes + R"(,"exports":[{"name":"primary","node":"absent"}]})", "references missing node" },
		{ prefix + R"("nodes":[{"id":"fire","kind":"fire.hitscan","params":[]}],)" + exports + '}', "params must be an object" },
		{ prefix + R"("nodes":[{"id":"fire","kind":"fire.hitscan"},{"id":"fi\u0072e","kind":"fire.hitscan"}],)" + exports + '}', "duplicate graph node id" },
	};
	for (const char *key : { "nodes", "edges", "exports", "subgraphs", "shared_context" }) {
		for (const char *rows : { "false", "{}", "[false]", "[{},false]", "[1,2]" }) {
			std::string graph = prefix;
			if (std::string(key) != "nodes") graph += nodes + ',';
			if (std::string(key) != "exports") graph += exports + ',';
			graph += '"' + std::string(key) + "\":" + rows + '}';
			invalid.emplace_back(graph, key);
		}
	}
	for (const auto &entry : std::vector<std::pair<std::string, std::string>>{
			{ "id", std::string(WEAPON_GRAPH_IR_ID_LEN, 'x') },
			{ "subgraph", std::string(WEAPON_GRAPH_IR_ID_LEN, 'x') } }) {
		std::string graph = prefix + R"("nodes":[{)";
		if (entry.first != "id") graph += R"("id":"fire",)";
		graph += '"' + entry.first + "\":\"" + entry.second + R"(","kind":"fire.hitscan"}],)" + exports + '}';
		invalid.emplace_back(graph, entry.first == "id" ? "missing id" : "subgraph");
	}
	invalid.emplace_back(prefix + R"("nodes":[{"id":"fire","kind":"fire.hitscan","params":{")" +
		std::string(WEAPON_GRAPH_IR_KEY_LEN, 'x') + R"(":1}}],)" + exports + '}', "parameter key");
	invalid.emplace_back(prefix + R"("nodes":[{"id":"fire","kind":"fire.hitscan","params":{"label":")" +
		std::string(WEAPON_GRAPH_IR_VALUE_LEN, 'x') + R"("}}],)" + exports + '}', "string param label");
	for (const char *key : { "asset_id", "graph_id" }) {
		std::string graph = valid;
		const std::string oldValue = std::string(key) == "asset_id" ? "base:w5f_weapon" : "primary";
		const size_t valueStart = graph.find(std::string("\"") + key + "\":\"") + std::strlen(key) + 4;
		graph.replace(valueStart, oldValue.size(), std::string(CATALOG_ID_LEN + WEAPON_GRAPH_IR_ID_LEN, 'x'));
		invalid.emplace_back(graph, key);
	}
	for (const auto &entry : invalid) {
		INFO(entry.first);
		weapon_graph_ir_t ir{};
		char err[256] = {};
		REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, entry.first.data(),
			static_cast<u32>(entry.first.size()), &ir, err, sizeof(err)) != 0);
		INFO(err);
		REQUIRE(std::string(err).find(entry.second) != std::string::npos);
		weaponGraphRuntimeClearAll();
		REQUIRE(weaponGraphRuntimeRegisterWeaponGraphJson(70, "base:w5f_weapon",
			entry.first.data(), static_cast<u32>(entry.first.size()), err, sizeof(err)) != 0);
		REQUIRE(weaponGraphRuntimeGetHeldFunction(70, 0) == nullptr);
		weaponGraphRuntimeClearAll();
	}
}

TEST_CASE("graph decoded identifier capacity is measured in UTF8 bytes without truncation",
		"[modding][pdxxx][weapon_graph][compiler][source_authority][T-ASSETS-045]") {
	for (size_t bytes : { WEAPON_GRAPH_IR_ID_LEN - 1u, WEAPON_GRAPH_IR_ID_LEN + 0u }) {
		const std::string id = std::string(bytes - 2, 'a') + "\xc3\xa9";
		const std::string escapedId = std::string(bytes - 2, 'a') + "\\u00e9";
		const std::string graph = std::string(R"({"schema":"pd.weapon_graph.v1","asset_id":"base:w5f_weapon",
			"graph_id":"primary","nodes":[{"id":")") + escapedId +
			R"(","kind":"fire.hitscan","params":{"mode":"primary"}}],"exports":[{"name":"primary","node":")" + id + R"("}]})";
		weapon_graph_ir_t ir{};
		char err[256] = {};
		const s32 result = weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
			static_cast<u32>(graph.size()), &ir, err, sizeof(err));
		if (bytes < WEAPON_GRAPH_IR_ID_LEN) {
			REQUIRE(result == 0);
			REQUIRE(std::string(ir.nodes[0].id) == id);
			REQUIRE(ir.exports[0].node == 0);
			weaponGraphIrFree(&ir);
		} else {
			REQUIRE(result != 0);
			REQUIRE(std::string(err).find("missing id") != std::string::npos);
		}
	}
}

TEST_CASE("weapon source registration validates every split document and decodes tunables",
		"[modding][pdxxx][weapon_graph][settings][source_authority][T-ASSETS-045]") {
	const std::string settings = R"({"sch\u0065ma":"pd.weapon_settings.v1","settings":[{"n\u0061me":"fire_cadence","value":12,"unit":"ticks60"}]})";
	const std::string variables = R"({"schema":"pd.weapon_variables.v1","variables":[{"name":"caf\u00e9","value":7.25}],"pierce":0,"cam":"none"})";
	const std::string presentation = R"({"schema":"pd.weapon.presentation.v1","zoom_\u0066ov":33.5,"crosshair":"de\u0066ault"})";
	std::string primary = kW5fPrimaryGraph;
	primary.replace(primary.find("$base_damage"), std::strlen("$base_damage"), "$caf\xc3\xa9");
	const std::vector<std::string> sources = { primary, kW5fSecondaryGraph,
		kW5fSharedContext, settings, variables, presentation };
	auto registerSources = [](const std::vector<std::string> &s, char *err, size_t cap) {
		return weaponGraphRuntimeRegisterWeaponSourceJson(70, "base:w5f_weapon",
			s[0].data(), static_cast<u32>(s[0].size()), s[1].data(), static_cast<u32>(s[1].size()),
			s[2].data(), static_cast<u32>(s[2].size()), s[3].data(), static_cast<u32>(s[3].size()),
			s[4].data(), static_cast<u32>(s[4].size()), s[5].data(), static_cast<u32>(s[5].size()), err, cap);
	};
	weaponGraphRuntimeClearAll();
	char err[256] = {};
	for (const std::string &variableSource : std::vector<std::string>{ variables,
			R"({"schema":"pd.weapon_variables.v1","caf\u00e9":7.25,"pierce":0,"cam":"none"})" }) {
		auto positive = sources;
		positive[4] = variableSource;
		REQUIRE(registerSources(positive, err, sizeof(err)) == 0);
		const auto *held = weaponGraphRuntimeGetHeldFunction(70, 0);
		REQUIRE(held != nullptr);
		REQUIRE(held->damage == Approx(7.25f));
		REQUIRE(held->zoom_fov == Approx(33.5f));
		REQUIRE(held->recoverytime_ticks60 == 12);
		weaponGraphRuntimeClearAll();
	}
	for (size_t file = 0; file < sources.size(); file++) {
		for (const char *suffix : { " trailing", R"(,"ignored":{"duplicate":1,"dupli\u0063ate":2}})" }) {
			auto invalid = sources;
			if (suffix[0] == ',') {
				invalid[file].erase(invalid[file].find_last_of('}'));
				invalid[file] += suffix;
			} else invalid[file] += suffix;
			CAPTURE(file, suffix);
			REQUIRE(registerSources(invalid, err, sizeof(err)) != 0);
			REQUIRE(std::string(err).find("strict JSON") != std::string::npos);
			REQUIRE(weaponGraphRuntimeGetHeldFunction(70, 0) == nullptr);
		}
	}
	for (size_t file : { 3u, 4u }) {
		const std::string label = file == 3 ? "settings" : "variables";
		for (const std::string &rows : std::vector<std::string>{ "null", "[false]", "[{},3]",
				R"([{"name":false,"key":"zoom_fov","value":12}])",
				R"([{"name":"","value":12}])",
				R"([{"name":")" + std::string(WEAPON_GRAPH_IR_KEY_LEN, 'x') + R"(","value":12}])" }) {
			auto invalid = sources;
			invalid[file] = "{\"schema\":\"pd.weapon_" + label + ".v1\",\"" + label + "\":" + rows + '}';
			INFO(invalid[file]);
			REQUIRE(registerSources(invalid, err, sizeof(err)) != 0);
			REQUIRE(std::string(err).find(label) != std::string::npos);
			REQUIRE(weaponGraphRuntimeGetHeldFunction(70, 0) == nullptr);
		}
	}
	for (const auto &row : std::vector<std::pair<size_t, std::string>>{
			{ 0, "nodes" }, { 1, "exports" }, { 0, "edges" }, { 2, "contexts" } }) {
		auto invalid = sources;
		const size_t keyStart = invalid[row.first].find('"' + row.second + '"');
		if (keyStart == std::string::npos) {
			invalid[row.first].erase(invalid[row.first].find_last_of('}'));
			invalid[row.first] += ",\"" + row.second + "\":[false]}";
		} else {
			const size_t arrayStart = invalid[row.first].find('[', keyStart);
			invalid[row.first].insert(arrayStart + 1, "false,");
		}
		REQUIRE(registerSources(invalid, err, sizeof(err)) != 0);
		REQUIRE(std::string(err).find(row.second) != std::string::npos);
		REQUIRE(weaponGraphRuntimeGetHeldFunction(70, 0) == nullptr);
	}
	weaponGraphRuntimeClearAll();
}

TEST_CASE("graph catalog reference boundaries preserve exact projectile selection",
		"[modding][pdxxx][weapon_graph][source_authority][T-ASSETS-045]") {
	weaponGraphRuntimeClearAll();
	weaponGraphRuntimeSetEnabled(1);
	const std::string id = "mod:" + std::string(CATALOG_ID_LEN - 7, 'a') + "\xc3\xa9";
	REQUIRE(id.size() == CATALOG_ID_LEN - 1);
	const std::string projectile = std::string(R"({"schema":"pd.projectile_graph.v1","asset_id":")") + id +
		R"(","graph_id":"motion","nodes":[{"id":"motion","kind":"projectile.motion","params":{"motion_kind":"ballistic"}}]})";
	char err[256] = {};
	REQUIRE(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, id.c_str(),
		projectile.data(), static_cast<u32>(projectile.size()), err, sizeof(err)) == 0);
	const auto *selected = weaponGraphRuntimeGetProjectile(id.c_str());
	REQUIRE(selected != nullptr);
	for (const std::string &reference : { id, id + 'x' }) {
		weaponGraphRuntimeClearWeapon(70);
		const std::string graph = std::string(R"({"schema":"pd.weapon_graph.v1","asset_id":"base:w5f_weapon",
			"graph_id":"primary","nodes":[{"id":"fire","kind":"fire.hitscan","params":{"mode":"primary","projectile_ref":")") +
			reference + R"("}}],"exports":[{"name":"primary","node":"fire"}]})";
		const s32 result = weaponGraphRuntimeRegisterWeaponGraphJson(70, "base:w5f_weapon",
			graph.data(), static_cast<u32>(graph.size()), err, sizeof(err));
		if (reference == id) {
			REQUIRE(result == 0);
			const auto *held = weaponGraphRuntimeGetHeldFunction(70, 0);
			REQUIRE(held != nullptr);
			REQUIRE(std::string(held->projectile_ref) == id);
			REQUIRE(weaponGraphRuntimeGetProjectileForHeldFunction(held) == selected);
		} else {
			REQUIRE(result != 0);
			REQUIRE(std::string(err).find("projectile_ref") != std::string::npos);
			REQUIRE(weaponGraphRuntimeGetHeldFunction(70, 0) == nullptr);
			REQUIRE(weaponGraphRuntimeGetProjectile(id.c_str()) == selected);
		}
	}
	weaponGraphRuntimeClearAll();
}

TEST_CASE("graph finite fall vectors fit the exact runtime destination before capture",
		"[modding][pdxxx][weapon_graph][source_authority][T-ASSETS-045]") {
	for (size_t bytes : { 63u, 64u, 77u }) {
		weaponGraphRuntimeClearAll();
		const std::string vector = std::string(bytes - 7, ' ') + "0,-2,-3";
		const std::string graph = std::string(R"({"schema":"pd.projectile_graph.v1","asset_id":"mod:vector",
			"graph_id":"wall","nodes":[{"id":"wall","kind":"projectile.wall_hugger","params":{"fall_vector":")") + vector + R"("}}]})";
		char err[256] = {};
		const s32 result = weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE, "mod:vector",
			graph.data(), static_cast<u32>(graph.size()), err, sizeof(err));
		if (bytes == 63) {
			REQUIRE(result == 0);
			const auto *runtime = weaponGraphRuntimeGetProjectile("mod:vector");
			REQUIRE(runtime != nullptr);
			REQUIRE(std::string(runtime->wall_fall_vector) == vector);
			REQUIRE(runtime->wall_fall_vec[0] == Approx(0.0f));
			REQUIRE(runtime->wall_fall_vec[1] == Approx(-2.0f));
			REQUIRE(runtime->wall_fall_vec[2] == Approx(-3.0f));
		} else {
			REQUIRE(result != 0);
			REQUIRE(std::string(err).find("fall_vector exceeds runtime string capacity") != std::string::npos);
			REQUIRE(weaponGraphRuntimeGetProjectile("mod:vector") == nullptr);
		}
		weaponGraphRuntimeClearAll();
	}
}

TEST_CASE("graph runtime string fields reject overflow while retaining full metadata capacity",
		"[modding][pdxxx][weapon_graph][source_authority][T-ASSETS-045]") {
	for (size_t bytes : { 63u, 64u }) {
		weaponGraphRuntimeClearAll();
		const std::string policy(bytes, 'x');
		const std::string detail(WEAPON_GRAPH_IR_VALUE_LEN - 1, 'd');
		const std::string graph = std::string(R"({"schema":"pd.entity_graph.v1","asset_id":"mod:owner",
			"graph_id":"cleanup","nodes":[{"id":"owner","kind":"entity.owner_cleanup",
			"params":{"owner_lost_behavior":")") + policy + R"(","runtime_detail":")" + detail + R"("}}]})";
		char err[256] = {};
		const s32 result = weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_ENTITY, "mod:owner",
			graph.data(), static_cast<u32>(graph.size()), err, sizeof(err));
		if (bytes == 63) {
			REQUIRE(result == 0);
			const auto *runtime = weaponGraphRuntimeGetEntity("mod:owner");
			REQUIRE(runtime != nullptr);
			REQUIRE(std::string(runtime->owner_lost_behavior) == policy);
			REQUIRE(std::string(runtime->runtime_detail) == detail);
		} else {
			REQUIRE(result != 0);
			REQUIRE(std::string(err).find("owner_lost_behavior exceeds runtime string capacity") != std::string::npos);
			REQUIRE(weaponGraphRuntimeGetEntity("mod:owner") == nullptr);
		}
		weaponGraphRuntimeClearAll();
	}
}

TEST_CASE("weapon graph runtime test override remains scoped to pd-tests",
          "[modding][pdxxx][weapon_graph][c3849][cutover]") {
	weaponGraphRuntimeSetEnabled(0);
	REQUIRE(weaponGraphRuntimeEnabled() == 0);
	weaponGraphRuntimeSetEnabled(1);
	REQUIRE(weaponGraphRuntimeEnabled() == 1);
}

TEST_CASE("weapon graph MP agreement is protocol-versioned after cutover",
          "[modding][pdxxx][weapon_graph][c3849][netparity][static]") {
	const std::string constants = readFile("src/include/constants.h");
	REQUIRE(constants.find("MPOPTION_WEAPONGRAPH") == std::string::npos);

	const std::string mplayer = readFile("src/game/mplayer/mplayer.c");
	REQUIRE(mplayer.find("savebufferOr(buffer, g_MpSetup.options, 32)") != std::string::npos);
	REQUIRE(mplayer.find("g_MpSetup.options & ~MPOPTION_WEAPONGRAPH") == std::string::npos);

	const std::string netmsg = readFile("port/src/net/netmsg.c");
	REQUIRE(netmsg.find("MPOPTION_WEAPONGRAPH") == std::string::npos);
	REQUIRE(netmsg.find("weaponGraphRuntimeEnabled() ? MPOPTION_WEAPONGRAPH : 0") == std::string::npos);
	REQUIRE(netmsg.find("weaponGraphRuntimeNetLatchEnabled(") == std::string::npos);
	REQUIRE(netmsg.find("weaponGraphRuntimeNetRestoreEnabled()") == std::string::npos);

	const std::string net = readFile("port/src/net/net.c");
	REQUIRE(net.find("weaponGraphRuntimeNetRestoreEnabled()") == std::string::npos);

	const std::string netHeader = readFile("port/include/net/net.h");
	REQUIRE(netHeader.find("#define NET_PROTOCOL_VER 66") != std::string::npos);
	REQUIRE(netHeader.find("v54/v55") != std::string::npos);

	const std::string versions = readFile("tests/test_versions.cpp");
	REQUIRE(versions.find("g_TestExpectedNetProtocolVer  = 66") != std::string::npos);
}

TEST_CASE("bot-profile catalog identity reaches UI save wire manifest and runtime",
          "[modding][pdxxx][bot_profile][identity][static]") {
	const std::string types = readFile("src/include/types.h");
	REQUIRE(types.find("char profile_id[64]") != std::string::npos);

	const std::string matchHeader = readFile("port/include/net/matchsetup.h");
	REQUIRE(matchHeader.find("char profile_id[64]") != std::string::npos);
	REQUIRE(matchHeader.find("matchConfigAddBotWithProfile") != std::string::npos);
	REQUIRE(matchHeader.find("matchConfigSetBotProfile") != std::string::npos);

	const std::string mplayer = readFile("src/game/mplayer/mplayer.c");
	REQUIRE(mplayer.find("mpBotProfileRuntimeBindingById") != std::string::npos);
	REQUIRE(mplayer.find("mpCreateBotFromProfileId") != std::string::npos);
	REQUIRE(mplayer.find("savebufferWriteString_ext(buffer, (char *)profile_id, 64)") != std::string::npos);
	REQUIRE(mplayer.find("savebufferReadString_ext(buffer,") != std::string::npos);

	const std::string legacyMenu = readFile("src/game/mplayer/setup.c");
	REQUIRE(legacyMenu.find("ctx.result_id") != std::string::npos);
	REQUIRE(legacyMenu.find("mpCreateBotFromProfileId(botnum, ctx.result_id)") != std::string::npos);

	const std::string room = readFile("port/fast3d/pdgui_menu_room.cpp");
	REQUIRE(room.find("Set Profile") != std::string::npos);
	REQUIRE(room.find("matchConfigSetBotProfile") != std::string::npos);
	REQUIRE(room.find("matchConfigAddBotWithProfile(src->profile_id") != std::string::npos);

	const std::string match = readFile("port/src/net/matchsetup.c");
	REQUIRE(match.find("s_matchSlotApplyBotProfile") != std::string::npos);
	REQUIRE(match.find("mpBotProfileRuntimeBindingById(profile_id)") != std::string::npos);
	REQUIRE(match.find("strncpy(slot->profile_id, profile_id") != std::string::npos);
	REQUIRE(match.find("strncpy(participant->bot.profile_id, slot->profile_id") !=
		std::string::npos);
	REQUIRE(match.find("g_BotConfigsArray[bot_slot] = participant->bot") !=
		std::string::npos);

	const std::string scenario = readFile("port/src/scenario_save.c");
	REQUIRE(scenario.find("\"profileId\"") != std::string::npos);
	REQUIRE(scenario.find("matchConfigAddBotWithProfile(") != std::string::npos);

	const std::string save = readFile("port/src/savefile.c");
	REQUIRE(save.find("\"profile_id\"") != std::string::npos);
	REQUIRE(save.find("matchConfigAddBotWithProfile(profile_id") != std::string::npos);

	const std::string netmsg = readFile("port/src/net/netmsg.c");
	REQUIRE(netmsg.find("netbufWriteStr(dst, bot->profile_id)") != std::string::npos);
	REQUIRE(netmsg.find("bot profile has no exact hydrated binding") != std::string::npos);
	REQUIRE(netmsg.find("profile_session = catalogReadAssetRef(src)") != std::string::npos);
	REQUIRE(netmsg.find("prepared->profile_session = sessionCatalogGetId(profile_entry->id)") !=
		std::string::npos);

	const std::string manifest = readFile("port/src/net/netmanifest.c");
	REQUIRE(manifest.find("sl->profile_id[0] ? assetCatalogResolve(sl->profile_id)") != std::string::npos);
	REQUIRE(manifest.find("pe && pe->type == ASSET_BOT_PROFILE") != std::string::npos);

	const std::string setupHeader = readFile("port/include/mpsetups.h");
	REQUIRE(setupHeader.find("#define MPSETUP_VERSION 3") != std::string::npos);
	const std::string constants = readFile("src/include/constants.h");
	REQUIRE(constants.find("#define MPSETUP_BLOCKSIZE 4096") != std::string::npos);
}

TEST_CASE("MP setup JSON has one catalog-native weapon identity",
          "[modding][pdxxx][weapon][save][b964][static]") {
	const std::string save = readFile("port/src/savefile.c");

	REQUIRE(save.find("B-964: write one authoritative representation") !=
	        std::string::npos);
	REQUIRE(save.find("g_MatchConfig.weapon_ids[i][0]") !=
	        std::string::npos);
	REQUIRE(save.find("writeJsonStringValue(fp, wid ? wid : \"\")") !=
	        std::string::npos);
	REQUIRE(save.find("fprintf(fp, \"  \\\"weapons\\\": [\")") ==
	        std::string::npos);
	REQUIRE(save.find("s32 saw_weapon_ids = 0") != std::string::npos);
	REQUIRE(save.find("if (!saw_weapon_ids)") != std::string::npos);
	REQUIRE(save.find("is unavailable\", i, wid_buf)") !=
	        std::string::npos);
	REQUIRE(save.find("SAVE: MP setup bot profile") != std::string::npos);
	REQUIRE(save.find("savePreflightJson(data, \"mpsetup\", path)") !=
	        std::string::npos);
	REQUIRE(save.find("saveCaptureMpSetupSnapshot(&snapshot)") !=
	        std::string::npos);
	REQUIRE(save.find("memcpy(snapshot->player_configs, g_PlayerConfigsArray") !=
	        std::string::npos);
	REQUIRE(save.find("memcpy(g_PlayerConfigsArray, snapshot->player_configs") !=
	        std::string::npos);
	REQUIRE(save.find("saveRestoreMpSetupSnapshot(&snapshot)") !=
	        std::string::npos);
	REQUIRE(save.find("saveAtomicCommit(&transaction)") != std::string::npos);
}

TEST_CASE("binary MP setup files reject partial unsupported or invalid state",
          "[modding][pdxxx][save][b965][static]") {
	const std::string setups = readFile("port/src/mpsetups.c");

	REQUIRE(setups.find("truncated setup-file header") != std::string::npos);
	REQUIRE(setups.find("setupfile->version > MPSETUP_VERSION") !=
	        std::string::npos);
	REQUIRE(setups.find("setupfile->defaultsetup > setupfile->numsetups") !=
	        std::string::npos);
	REQUIRE(setups.find("truncated setup block %d of %u") !=
	        std::string::npos);
	REQUIRE(setups.find("memset(setupfile, 0, sizeof(*setupfile))") !=
	        std::string::npos);
	REQUIRE(setups.find("setupfile->version != MPSETUP_VERSION") !=
	        std::string::npos);
	REQUIRE(setups.find("failed to write setup-file header") !=
	        std::string::npos);
	REQUIRE(setups.find("failed to write setup block %d of %u") !=
	        std::string::npos);
	REQUIRE(setups.find("if (nwritten < 0)") != std::string::npos);
	REQUIRE(setups.find("struct mpsetupfile candidate") != std::string::npos);
	REQUIRE(setups.find("*setupfile = candidate") != std::string::npos);
	REQUIRE(setups.find("saveAtomicBegin(&transaction, filename)") !=
	        std::string::npos);
	REQUIRE(setups.find("saveAtomicAbort(&transaction)") != std::string::npos);
	REQUIRE(setups.find("saveAtomicCommit(&transaction)") != std::string::npos);
	REQUIRE(setups.find("prior setup file preserved") != std::string::npos);
}

TEST_CASE("archive-backed typed weapon sources keep transport-root chain",
          "[modding][pdxxx][weapon_graph][c3849][static][archive_vfs]") {
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("modArchiveGetPath(archive)") != std::string::npos);
	REQUIRE(scanner.find("assetPathJoinChecked(entry_ref, FS_MAXPATH, archive_path, \"::\",") != std::string::npos);
	REQUIRE(scanner.find("qualifyTypedArchiveSourcePaths(&ini, entry_ref)") != std::string::npos);
	REQUIRE(scanner.find("qualifyTypedArchiveSourcePaths(&ini, entry_name)") == std::string::npos);

	const std::string lifecycle = readFile("port/src/assetcatalog_load.c");
	REQUIRE(lifecycle.find("source_end = source_path + strlen(source_path)") != std::string::npos);
	REQUIRE(lifecycle.find("while (segment < source_end)") != std::string::npos);
	REQUIRE(lifecycle.find("segment = member_sep + 2") != std::string::npos);
	REQUIRE(lifecycle.find("size_t root_len = (size_t)(segment_end - source_path)") != std::string::npos);

	const std::string smoke = readFile("tools/smoke-verify/tests/needler_graph_runtime_visual_smoke.json");
	REQUIRE(smoke.find("dev-mods/needler") != std::string::npos);
	REQUIRE(smoke.find("mods/installed/needler.pdmod") != std::string::npos);
	REQUIRE(smoke.find("needler_visual_pd.ini") != std::string::npos);
	REQUIRE(smoke.find("needler_source_pd.ini") == std::string::npos);
	REQUIRE(smoke.find("mod_needler:needler") != std::string::npos);
	REQUIRE(smoke.find("--debug-force-first-person-look") == std::string::npos);
	REQUIRE(smoke.find("--debug-force-first-person-cam-offset") == std::string::npos);
	REQUIRE(smoke.find("--debug-effect-runtime-audit") != std::string::npos);
	REQUIRE(smoke.find("ACTION_AIM_DOWN") != std::string::npos);
	REQUIRE(smoke.find("ACTION_FIRE_MODE") != std::string::npos);
	REQUIRE(smoke.find("ACTION_FIRE_SECONDARY") == std::string::npos);
	REQUIRE(smoke.find("\"--no-sound\"") == std::string::npos);
	const std::string bondgunReset = readFile("src/game/bondgunreset.c");
	const std::string playerTypes = readFile("src/include/types.h");
	const std::string activeMenu = readFile("src/game/activemenu.c");
	REQUIRE(playerTypes.find("u32 customgunfuncs;") != std::string::npos);
	REQUIRE(bondgunReset.find("gunctrl.customgunfuncs = 0") != std::string::npos);
	REQUIRE(activeMenu.find("if (bgunIsUsingSecondaryFunction())") != std::string::npos);
	REQUIRE(smoke.find("EFFECT\\\\.GAMEPLAY\\\\.AUDIT: committed asset=mod_needler:pink_burst_effect") != std::string::npos);
	REQUIRE(smoke.find("EFFECT\\\\.PRESENTATION\\\\.RENDER\\\\.AUDIT") != std::string::npos);
	REQUIRE(smoke.find("\"screenshots\"") != std::string::npos);
	REQUIRE(smoke.find("BONDGUN\\\\.SOURCE: loaded catalog model source filenum=\\\\d+ id=mod_needler:needler_model") != std::string::npos);
	REQUIRE(smoke.find("MODASSET\\\\.RENDER: generated source modeldef rendered id=mod_needler:needler_model") != std::string::npos);

	const std::string bondgun = readFile("src/game/bondgun.c");
	REQUIRE(bondgun.find("_Static_assert(WEAPON_CUSTOM_COUNT <= 32") != std::string::npos);
	REQUIRE(bondgun.find("weaponnum >= WEAPON_CUSTOM_START && weaponnum < WEAPON_CUSTOM_END") != std::string::npos);
	REQUIRE(bondgun.find("gunctrl.customgunfuncs") != std::string::npos);
	REQUIRE(bondgun.find("bgunCanStoreFunctionSelection(g_Vars.currentplayer->gunctrl.weaponnum)") != std::string::npos);
	REQUIRE(bondgun.find("modelSourceResolvePath") != std::string::npos);
	REQUIRE(bondgun.find("#include \"model_source_path.h\"") != std::string::npos);
	REQUIRE(bondgun.find("player->gunctrl.loadhandle.provider == fileProvider()") != std::string::npos);
	REQUIRE(bondgun.find("BONDGUN.SOURCE: loaded catalog model source") != std::string::npos);
	REQUIRE(bondgun.find("player->gunctrl.gunmodeldef == NULL") != std::string::npos);
	REQUIRE(bondgun.find("noModel=%d") != std::string::npos);
	REQUIRE(bondgun.find("use_static_source_matrices = gunmodeldef != NULL") != std::string::npos);
	REQUIRE(bondgun.find("gunmodeldef->skel == NULL") != std::string::npos);
	REQUIRE(bondgun.find("modAssetCompilerModeldefIsGenerated(gunmodeldef)") != std::string::npos);
	/* B-1180: held generated guns keep the z-buffer like every other held gun. */
	REQUIRE(bondgun.find("renderdata.zbufferenabled = false;") == std::string::npos);
	REQUIRE(bondgun.find("renderdata.zbufferenabled = true;") != std::string::npos);
	REQUIRE(bondgun.find("modelUpdateMatrices(&renderdata, &hand->gunmodel)") != std::string::npos);
	REQUIRE(bondgun.find("modelSetMatricesWithAnim(&renderdata, &hand->gunmodel)") != std::string::npos);

	const std::string loaderPool = readFile("port/src/loader_pool.c");
	REQUIRE(loaderPool.find("model_file[FS_MAXPATH + 1]") != std::string::npos);
	REQUIRE(loaderPool.find("jstream_str_eq(&key, \"model_file\")") != std::string::npos);
	REQUIRE(loaderPool.find("weapon_id >= WEAPON_CUSTOM_START && weapon_id < WEAPON_CUSTOM_END") != std::string::npos);
	REQUIRE(loaderPool.find("w.flags |= WEAPONFLAG_00000040") != std::string::npos);

	const std::string modelSlots = readFile("port/src/assetcatalog_model_slots.c");
	REQUIRE(modelSlots.find("MODEL_CUSTOM_SOURCE_FILENUM_START = 0x7e0") != std::string::npos);
	REQUIRE(modelSlots.find("_Static_assert(MODEL_CUSTOM_SOURCE_FILENUM_END <= 0x800") != std::string::npos);

	const std::string runtime = readFile("port/src/weapon_graph_runtime.c");
	REQUIRE(runtime.find("s_registerEmbeddedMeshDeps") == std::string::npos);
	REQUIRE(runtime.find("assetCatalogResolveModelPrivateSlot") == std::string::npos);
	const std::string meshScanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(meshScanner.find("weaponNestedPrepareRecursiveMeshes(&pending") != std::string::npos);
	REQUIRE(meshScanner.find("weaponNestedPrepareMember(p, *pending, *count") != std::string::npos);
	REQUIRE(meshScanner.find("weaponNestedPrepareMember(p, pending, pending_count") != std::string::npos);
	REQUIRE(meshScanner.find("assetCatalogSnapshotCustomModelSlots()") != std::string::npos);
	REQUIRE(meshScanner.find("assetCatalogRestoreCustomModelSlots(model_slot_checkpoint)") != std::string::npos);
	REQUIRE(meshScanner.find("owner->source_filenum = mesh->source_filenum") != std::string::npos);
	REQUIRE(meshScanner.find("loaderWalkerBindMeshSource(entry, &p->mesh_source") != std::string::npos);

	const std::string weaponFile = readFile("src/game/game_0b0fd0.c");
	REQUIRE(weaponFile.find("if (weaponnum >= WEAPON_CUSTOM_START)") != std::string::npos);
	REQUIRE(weaponFile.find("weapon->hi_model == 0 && weaponnum >= WEAPON_CUSTOM_START") == std::string::npos);
	REQUIRE(weaponFile.find("catalogWeaponIdByRuntimeWeaponNum(weaponnum)") != std::string::npos);
	REQUIRE(weaponFile.find("catalogResolveWeapon(catalog_id, &weapon_result)") != std::string::npos);

	const std::string catalogApi = readFile("port/src/assetcatalog_api.c");
	REQUIRE(catalogApi.find("weapon_num >= WEAPON_CUSTOM_START && weapon_num < WEAPON_CUSTOM_END") != std::string::npos);
	REQUIRE(catalogApi.find("if (e->bundled) continue") != std::string::npos);
	REQUIRE(catalogApi.find("if (e->source_filenum <= 0) continue") != std::string::npos);
	REQUIRE(catalogApi.find("source_match = e->id") != std::string::npos);
	REQUIRE(catalogApi.find("if (source_match)") != std::string::npos);
}

TEST_CASE("presentation_file mirrors and camera clause pins stay wired",
          "[modding][pdxxx][weapon_graph][c3849][settings][static]") {
	/* ext.weapon.presentation_file exists and all THREE mirror sites copy it
	 * (field-for-field parity discipline). */
	const std::string catalog_h = readFile("port/include/assetcatalog.h");
	REQUIRE(catalog_h.find("char presentation_file[FS_MAXPATH]") != std::string::npos);

	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("e->ext.weapon.presentation_file") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"presentation_file\", iniGet(ini, \"presentation\", \"\"))") != std::string::npos);

	const std::string walker = readFile("port/src/loader_walker_weapon.c");
	REQUIRE(walker.find("e->ext.weapon.presentation_file") != std::string::npos);

	const std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("e->ext.weapon.presentation_file") != std::string::npos);

	/* Descriptor alias pair in the shared archive reader. */
	const std::string archive = readFile("port/src/weapon_graph_archive.c");
	REQUIRE(archive.find("strcmp(key, \"presentation_file\") == 0") != std::string::npos);

	/* bgunTick vision arm: OG clauses stay FIRST and verbatim; the graph
	 * clause consumes the latched s32 mode (no per-tick strcmp). */
	const std::string bondgun = readFile("src/game/bondgun.c");
	const std::string::size_type ogFarsight =
		bondgun.find("// Aiming with the Farsight");
	const std::string::size_type graphClause =
		bondgun.find("camera_effect_mode == WEAPON_GRAPH_CAMERA_EFFECT_XRAY");
	REQUIRE(ogFarsight != std::string::npos);
	REQUIRE(graphClause != std::string::npos);
	REQUIRE(ogFarsight < graphClause);

	/* B8: the Modding Hub presentation template authors the scalar zoom_fov
	 * the compiler consumes; the dead zoom_fovs array is gone. */
	const std::string moddinghub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(moddinghub.find("\\\"zoom_fov\\\": 60.0") != std::string::npos);
	REQUIRE(moddinghub.find("zoom_fovs") == std::string::npos);

	/* B6.4: the needler builder converged on the canonical spellings. */
	const std::string needler = readFile("tools/build_needler_mod.py");
	REQUIRE(needler.find("\"pd_schema_version\": 1") != std::string::npos);
	REQUIRE(needler.find("pd.weapon_settings.v1") != std::string::npos);
	REQUIRE(needler.find("pd.weapon_variables.v1") != std::string::npos);
	REQUIRE(needler.find("pd.weapon.settings.v1") == std::string::npos);
	REQUIRE(needler.find("pd.weapon.variables.v1") == std::string::npos);
	REQUIRE(needler.find("spawn_id = f\"{graph_id}_spawn_projectile\"") != std::string::npos);
	REQUIRE(needler.find("\"id\": \"spawn_projectile\"") == std::string::npos);
	REQUIRE(needler.find("\"spark_ref\": BURST_EFFECT_ID") != std::string::npos);
	REQUIRE(needler.find("\"spark_ref\": SPARK_TEXTURE_ID") == std::string::npos);
	REQUIRE(needler.find("BURST_SFX_ID = cid(\"pink_burst_sfx\")") != std::string::npos);
	REQUIRE(needler.find("\"audio_catalog_id\": BURST_SFX_ID") != std::string::npos);
	REQUIRE(needler.find("dependencies/assets/audio/pink_burst.pdsfx") != std::string::npos);
	REQUIRE(needler.find("track_type = rocket_launcher") != std::string::npos);
	REQUIRE(needler.find("\"functions\":") == std::string::npos);
	REQUIRE(needler.find("\"aimsettings\":") == std::string::npos);
}

TEST_CASE("checked-in legacy render streams declare schema v1 through every archive layer",
		"[modding][pdxxx][render_stream][compat][B-1086]") {
	struct RenderLeaf {
		const char *label;
		const char *outerArchive;
		std::vector<const char *> nestedArchives;
	};

	const std::vector<RenderLeaf> leaves = {
		{
			"Needler held mesh",
			"dev-mods/needler/needler.pdweapon",
			{ "dependencies/assets/models/weapon.pdmesh" },
		},
		{
			"body mesh",
			"examples/modding/typed-pdxxx-basic/bodies/tri_body.pdbody",
			{ "mesh.pdmesh" },
		},
		{
			"body hand mesh",
			"examples/modding/typed-pdxxx-basic/bodies/tri_body.pdbody",
			{ "hand.pdmesh" },
		},
		{
			"character body mesh",
			"examples/modding/typed-pdxxx-basic/characters/tri_character.pdcharacter",
			{ "dependencies/assets/body/tri_body.pdbody", "mesh.pdmesh" },
		},
		{
			"character hand mesh",
			"examples/modding/typed-pdxxx-basic/characters/tri_character.pdcharacter",
			{ "dependencies/assets/body/tri_body.pdbody", "hand.pdmesh" },
		},
		{
			"character head mesh",
			"examples/modding/typed-pdxxx-basic/characters/tri_character.pdcharacter",
			{ "dependencies/assets/head/tri_head.pdhead", "mesh.pdmesh" },
		},
		{
			"head mesh",
			"examples/modding/typed-pdxxx-basic/heads/tri_head.pdhead",
			{ "mesh.pdmesh" },
		},
		{
			"top-level mesh",
			"examples/modding/typed-pdxxx-basic/meshes/tri_mesh.pdmesh",
			{},
		},
		{
			"weapon held mesh",
			"examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon",
			{ "dependencies/assets/models/weapon.pdmesh" },
		},
	};
	const std::regex numericLegacySchema(
		"\"pd_schema_version\"\\s*:\\s*1\\s*[,}]");

	REQUIRE(leaves.size() == 9);
	for (const RenderLeaf &leaf : leaves) {
		CAPTURE(leaf.label, leaf.outerArchive);
		const std::string renderStream = readArchiveChainEntryText(
			leaf.outerArchive, leaf.nestedArchives, "model.render.json");
		REQUIRE(std::regex_search(renderStream, numericLegacySchema));
		REQUIRE(renderStream.find("\"schema\": \"pd2.mesh.render.v1\"") !=
			std::string::npos);
	}
}

TEST_CASE("public weapon activation hydrates and clears the loader-owned runtime adapter",
		"[modding][pdxxx][weapon][static][b1021]") {
	const std::string load = readFile("port/src/assetcatalog_load.c");
	const std::string pool = readFile("port/src/loader_pool.c");
	const std::string archive = readFile("port/src/weapon_graph_archive.c");
	const std::string weaponIni = readArchiveEntryText(
		"dev-mods/needler/needler.pdweapon", "weapon.ini");
	const std::string manifest = readArchiveEntryText(
		"dev-mods/needler/needler.pdweapon", "_meta/manifest.json");

	REQUIRE(load.find("loaderPoolInstallPublicWeaponAdapter") != std::string::npos);
	REQUIRE(load.find("weaponGraphRuntimeGetHeldFunction(entry->runtime_index, 0)") != std::string::npos);
	REQUIRE(load.find("loaderPoolClearPublicWeaponAdapter(entry->runtime_index)") != std::string::npos);
	REQUIRE(pool.find("s_PublicWeaponSlotActive") != std::string::npos);
	REQUIRE(pool.find("assetCatalogRefreshWeaponPrivateSlotDefaults(runtime_weapon_id") != std::string::npos);
	REQUIRE(pool.find("LOADER.POOL.WEAPON.PUBLIC_ADAPTER: installed") != std::string::npos);
	REQUIRE(archive.find("parseWeaponTrackType") != std::string::npos);
	REQUIRE(weaponIni.find("track_type = rocket_launcher") != std::string::npos);
	REQUIRE(manifest.find("\"functions\"") == std::string::npos);
	REQUIRE(manifest.find("\"aimsettings\"") == std::string::npos);
	REQUIRE(manifest.find("\"muzzlez\"") == std::string::npos);
}

TEST_CASE("function.empty is an absent held function, not an unsupported one",
		"[modding][pdxxx][weapon_graph][runtime][held][b1182]") {
	/* The shape every extracted base weapon uses for a missing secondary. */
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"mod_test:empty_secondary\",\n"
		"  \"graph_id\": \"empty_secondary_v1\",\n"
		"  \"nodes\": [\n"
		"    {\n"
		"      \"id\": \"primary_action\",\n"
		"      \"kind\": \"fire.hitscan\",\n"
		"      \"params\": { \"mode\": \"primary\", \"function_type\": \"shoot_single\", \"ammo_slot\": 0 }\n"
		"    },\n"
		"    {\n"
		"      \"id\": \"secondary_action\",\n"
		"      \"kind\": \"function.empty\",\n"
		"      \"params\": { \"mode\": \"secondary\", \"function_type\": \"none\", \"source\": \"empty_slot\" }\n"
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
	REQUIRE(weaponGraphRuntimeRegisterHeldIr(12, &ir, err, sizeof(err)) == 0);
	const weapon_graph_held_function_t *primary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(12, 0);
	const weapon_graph_held_function_t *secondary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(12, 1);
	REQUIRE(primary != nullptr);
	REQUIRE(secondary != nullptr);
	REQUIRE(secondary->opcode == WEAPON_GRAPH_OP_FUNCTION_EMPTY);
	REQUIRE(weaponGraphHeldFunctionIsExecutable(primary) == 1);
	REQUIRE(weaponGraphHeldFunctionIsExecutable(secondary) == 0);
	REQUIRE(weaponGraphHeldFunctionIsExecutable(nullptr) == 0);
	weaponGraphRuntimeSetEnabled(0);
	weaponGraphRuntimeClearAll();

	/* The mod weapon adapter builds a NULL native function for the empty slot
	 * instead of rejecting function_type 0 and rolling the weapon back. */
	const std::string pool = readFile("port/src/loader_pool.c");
	REQUIRE(pool.find("if (!weaponGraphHeldFunctionIsExecutable(held)) return 1;") !=
		std::string::npos);
	REQUIRE(pool.find("weapon.functions[1] = weaponGraphHeldFunctionIsExecutable(secondary) ?") !=
		std::string::npos);
	REQUIRE(pool.find("secondary && secondary->valid") == std::string::npos);
}

TEST_CASE("a weapon without an installed definition is never given, equipped or kept as the spawn pick",
		"[modding][pdxxx][weapon][static][b1181]") {
	const std::string router = readFile("port/src/catalog_mgr_weapons.c");
	REQUIRE(router.find("s32 catalogManagerWeaponIsUsable(s32 weapon_id)") != std::string::npos);
	REQUIRE(router.find("return weapon != NULL && weapon->aimsettings != NULL;") != std::string::npos);

	const std::string bondgun = readFile("src/game/bondgun.c");
	const size_t equip = bondgun.find("void bgunEquipWeapon(s32 weaponnum)");
	const size_t guard = bondgun.find(
		"if (weaponnum > WEAPON_UNARMED && !catalogManagerWeaponIsUsable(weaponnum)) {");
	const size_t shortcircuit = bondgun.find(
		"if (player->gunctrl.weaponnum == weaponnum && player->gunctrl.switchtoweaponnum == -1) {");
	REQUIRE(equip != std::string::npos);
	REQUIRE(guard != std::string::npos);
	REQUIRE(shortcircuit != std::string::npos);
	/* The guard runs before the hand can adopt or short-circuit on the id. */
	REQUIRE(equip < guard);
	REQUIRE(guard < shortcircuit);

	const std::string inv = readFile("src/game/inv.c");
	REQUIRE(inv.find("if (!invWeaponIsGivable(weaponnum)) {") != std::string::npos);
	REQUIRE(inv.find("if (!invWeaponIsGivable(weapon1) || !invWeaponIsGivable(weapon2)) {") !=
		std::string::npos);

	const char *spawnSites[] = { "src/game/player.c", "src/game/bot.c", "src/game/setup.c" };
	for (const char *path : spawnSites) {
		INFO(path);
		const std::string source = readFile(path);
		REQUIRE(source.find("&& catalogManagerWeaponIsUsable((s32)g_MatchConfig.spawnWeaponNum)") !=
			std::string::npos);
	}
}

TEST_CASE("a skeleton-less source hierarchy always gets a bounding box node",
		"[modding][pdxxx][mesh][static][b1183]") {
	/* A mod weapon's only model is also its dropped object; object physics
	 * (projectileFall) reads the box without a NULL check. */
	const std::string compiler = readFile("port/src/modasset_compiler.c");
	const size_t helper = compiler.find("static void generatedModeldefEnsureHierarchyBounds(");
	REQUIRE(helper != std::string::npos);
	REQUIRE(compiler.find("node_count <= 0 || skeleton != NULL", helper) != std::string::npos);
	REQUIRE(compiler.find("root->type == MODELNODETYPE_CHRINFO", helper) != std::string::npos);
	REQUIRE(compiler.find("generatedModeldefMeshBounds(mesh, &owner->bbox_rodata.bbox);", helper) !=
		std::string::npos);
	const size_t call = compiler.find(
		"generatedModeldefEnsureHierarchyBounds(owner, hierarchy.row_count, skeleton, mesh);");
	const size_t rwdata = compiler.find(
		"owner->def.rwdatalen = modelCalculateRwDataIndexes(owner->def.rootnode);", call);
	REQUIRE(call != std::string::npos);
	REQUIRE(rwdata != std::string::npos);
}

TEST_CASE("public weapon source registration fails closed",
          "[modding][pdxxx][weapon_graph][c3842][c3844][static]") {
	const std::string walker = readFile("port/src/loader_walker_weapon.c");
	REQUIRE(walker.find("weaponGraphRuntimeClearWeapon(runtime_weapon_id)") !=
		std::string::npos);
	REQUIRE(walker.find("e->enabled = 0") != std::string::npos);
	REQUIRE(walker.find("e->load_state = ASSET_STATE_REGISTERED") !=
		std::string::npos);
	REQUIRE(walker.find("return -1;") != std::string::npos);

	const std::string lifecycle = readFile("port/src/assetcatalog_load.c");
	REQUIRE(lifecycle.find(
		"held graph source is incomplete; refusing selected public weapon source") !=
		std::string::npos);
	REQUIRE(lifecycle.find(
		"held graph source is incomplete; skipping held graph registration") ==
		std::string::npos);

	const std::string runtime = readFile("port/src/weapon_graph_runtime.c");
	REQUIRE(runtime.find("has no production consumer") != std::string::npos);
	REQUIRE(runtime.find("has no consumer yet; stored only") == std::string::npos);
	REQUIRE(runtime.find("has no consumer yet; ignored") == std::string::npos);
}

TEST_CASE("PC weapon switching and function HUD consume action-map state",
          "[input][weapon][static][c3814]") {
	const std::string actionmap = readFile("port/src/actionmap.cpp");
	REQUIRE(actionmap.find("ACTION_WEAPON_NEXT,    VKL_Q") != std::string::npos);
	REQUIRE(actionmap.find("st->up_time_ms") != std::string::npos);
	REQUIRE(actionmap.find("hold_vis_grace_until_ms") != std::string::npos);

	const std::string bondmove = readFile("src/game/bondmove.c");
	REQUIRE(bondmove.find("bmoveHandleDirectWeaponSelect") != std::string::npos);
	REQUIRE(bondmove.find("ACTION_WEAPON_6") != std::string::npos);
	REQUIRE(bondmove.find("ACTION_WEAPON_PREV") != std::string::npos);
	REQUIRE(bondmove.find("actionWasTap((s32)contpad1, ACTION_WEAPON_NEXT") != std::string::npos);
	REQUIRE(bondmove.find("bmoveSelectInventoryWeaponIndex") != std::string::npos);

	const std::string bondgun = readFile("src/game/bondgun.c");
	REQUIRE(bondgun.find("ctrl->curfnstr = 0") != std::string::npos);
	REQUIRE(bondgun.find("displayweaponnum = player->gunctrl.weaponnum") != std::string::npos);
	REQUIRE(bondgun.find("weaponGetFunctionById(displayweaponnum, funcnum)") != std::string::npos);
	REQUIRE(bondgun.find("invGetWeaponNumByIndex(currentindex)") != std::string::npos);
	REQUIRE(bondgun.find("weaponGetFunctionById(hand->gset.weaponnum, funcnum)") == std::string::npos);
	REQUIRE(bondgun.find("if (ctrl->curfnstr != func->name)") != std::string::npos);
	REQUIRE(bondgun.find("ctrl->curfnstr != func->name && ctrl->fnfader > 128") == std::string::npos);

	const std::string activemenu = readFile("src/game/activemenu.c");
	REQUIRE(activemenu.find("equippedweaponnum = bgunGetWeaponNum(HAND_RIGHT)") != std::string::npos);
	REQUIRE(activemenu.find("weaponGetFunctionById(equippedweaponnum, FUNC_PRIMARY)") != std::string::npos);
	REQUIRE(activemenu.find("weaponGetFunction(&g_Vars.currentplayer->hands[HAND_RIGHT].gset, FUNC_PRIMARY)") == std::string::npos);

	const std::string accessors = readFile("src/game/game_0b0fd0.c");
	REQUIRE(accessors.find("if (which >= 2)") != std::string::npos);
}

TEST_CASE("public mods share folder mods as validated pdmod archives",
          "[modding][pdmod][static][c3809]") {
	std::string share = readFile("port/src/social_share.c");
	std::string public_test = readFile("tests/test_public_mods_static.cpp");
	std::string cmake = readFile("CMakeLists.txt");

	REQUIRE(cmake.find("tests/test_public_mods_static.cpp") != std::string::npos);
	REQUIRE(share.find("#include \"modpack_pdmod.h\"") != std::string::npos);
	REQUIRE(share.find("static s32 packFolderModForPublicShare") != std::string::npos);
	REQUIRE(share.find("modpackPdmodFromFolder(mod->dirpath, out_path)") != std::string::npos);
	REQUIRE(share.find("modpackPdmodLastError()") != std::string::npos);
	REQUIRE(share.find("fileTransferSendFile(from_handle, pdmod_path)") != std::string::npos);
	REQUIRE(share.find("mod manifest offer") == std::string::npos);
	REQUIRE(public_test.find("modpackPdmodFromFolder(mod->dirpath, out_path)") != std::string::npos);
}

TEST_CASE("metadata family INI support covers grouped templates and deps",
          "[modding][pdmod][static][c3809]") {
	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("modiniTemplateForKind") != std::string::npos);
	REQUIRE(scanner.find("weapon.ini - external weapon metadata") != std::string::npos);
	REQUIRE(scanner.find("head.ini - external multiplayer head metadata") != std::string::npos);
	REQUIRE(scanner.find("body.ini - external multiplayer body metadata") != std::string::npos);
	REQUIRE(scanner.find("arena.ini - external map/arena metadata") != std::string::npos);
	REQUIRE(scanner.find("material.ini - reusable render/surface material metadata") != std::string::npos);
	REQUIRE(scanner.find("effect.ini - reusable visual/feedback effect metadata") != std::string::npos);
	REQUIRE(scanner.find("prop.ini - reusable spawnable prop metadata") != std::string::npos);
	REQUIRE(scanner.find("gamemode.ini - Combat Simulator/custom rules metadata") != std::string::npos);
	REQUIRE(scanner.find("rules_file = rules.json") != std::string::npos);
	REQUIRE(scanner.find("botprofile.ini - reusable bot skill/personality metadata") != std::string::npos);
	REQUIRE(scanner.find("profile_file = profile.json") != std::string::npos);
	REQUIRE(scanner.find("; target_body = base:body_id") != std::string::npos);
	REQUIRE(scanner.find("theme.ini - menu/UI theme bundle metadata") != std::string::npos);
	REQUIRE(scanner.find("animation.ini - external animation metadata") != std::string::npos);
	REQUIRE(scanner.find("\"headnum = -1\\n\"") == std::string::npos);
	REQUIRE(scanner.find("\"bodynum = -1\\n\"") == std::string::npos);
	REQUIRE(scanner.find("\"anim_id = -1\\n\"") == std::string::npos);
	REQUIRE(scanner.find("\"sound_id = -1\\n\"") == std::string::npos);
	REQUIRE(scanner.find("\"load_mode = 0\\n\"") == std::string::npos);
	REQUIRE(scanner.find("iniGetInt(ini, \"bodynum\"") == std::string::npos);
	REQUIRE(scanner.find("iniGetInt(ini, \"headnum\"") == std::string::npos);
	REQUIRE(scanner.find("iniGetInt(ini, \"weapon_id\"") == std::string::npos);
	REQUIRE(scanner.find("iniGetInt(ini, \"anim_id\"") == std::string::npos);
	REQUIRE(scanner.find("iniGetInt(ini, \"texture_id\"") == std::string::npos);
	REQUIRE(scanner.find("iniGetInt(ini, \"sound_id\"") == std::string::npos);
	std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("iniGetInt(ini, \"bodynum\"") == std::string::npos);
	REQUIRE(distrib.find("iniGetInt(ini, \"headnum\"") == std::string::npos);
	REQUIRE(distrib.find("iniGetInt(ini, \"weapon_id\"") == std::string::npos);
	REQUIRE(distrib.find("iniGetInt(ini, \"anim_id\"") == std::string::npos);
	REQUIRE(distrib.find("iniGetInt(ini, \"texture_id\"") == std::string::npos);
	REQUIRE(distrib.find("iniGetInt(ini, \"sound_id\"") == std::string::npos);
	const size_t visitor = scanner.find("s32 assetCatalogVisitDeclaredDependencies(");
	const size_t callback = scanner.find("static s32 registerDeclaredDependency(", visitor);
	const size_t wrapper = scanner.find("static void registerDependencyList(", callback);
	const size_t wrapper_end = scanner.find("static s32 registerAnimationCommandSource(", wrapper);
	REQUIRE(visitor != std::string::npos);
	REQUIRE(callback != std::string::npos);
	REQUIRE(wrapper != std::string::npos);
	REQUIRE(wrapper_end != std::string::npos);
	REQUIRE(scanner.find("if (dep[0] && !visit(dep, userdata)) return 0;", visitor) < callback);
	REQUIRE(scanner.find("catalogDepRegister(owner->id, id, owner->bundled);", callback) < wrapper);
	REQUIRE(scanner.find("declared_dependency_owner_t owner = {owner_id, is_bundled};", wrapper) < wrapper_end);
	REQUIRE(scanner.find("assetCatalogVisitDeclaredDependencies(deps, registerDeclaredDependency, &owner);", wrapper) < wrapper_end);
	REQUIRE(scanner.find("registerDependencyList(e->id, iniGet(ini, \"deps\", \"\"), e->bundled);", wrapper_end)
		!= std::string::npos);
	REQUIRE(scanner.find("catalogDepRegister(e->ext.anim.target_body, e->id, e->bundled)") != std::string::npos);
	REQUIRE(distrib.find("distribRegisterDependencyList(e->id, iniGet(&ini, \"deps\", \"\"), e->bundled)") != std::string::npos);
	REQUIRE(distrib.find("catalogDepRegister(e->id, e->ext.projectile.entity_ref, e->bundled)") != std::string::npos);
	REQUIRE(distrib.find("catalogDepRegister(e->ext.anim.target_body, e->id, e->bundled)") != std::string::npos);
	REQUIRE(scanner.find("\"animation_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"rules_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"profile_file\"") != std::string::npos);
}

TEST_CASE("external audio descriptors use standard files through VFS-capable loaders",
          "[modding][pdmod][static][c3809]") {
	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("audio_category = sfx") != std::string::npos);
	REQUIRE(scanner.find("Music authoring supports OGG, MP3, or WAV") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"audio_category\"") != std::string::npos);

	std::string audio = readFile("port/src/audio.c");
	REQUIRE(audio.find("modMusicLoadAudioPcm22050(path, &samples, &source_rate)") != std::string::npos);
	REQUIRE(audio.find("SDL_LoadWAV_RW") == std::string::npos);

	std::string modmusic = readFile("port/src/modmusic.c");
	REQUIRE(modmusic.find("modVfsCanResolve(file_path)") != std::string::npos);
	REQUIRE(modmusic.find("fsFileLoad(path, &fileSize)") != std::string::npos);
	REQUIRE(modmusic.find("SDL_LoadWAV_RW") != std::string::npos);
	REQUIRE(modmusic.find("stb_vorbis_open_memory") != std::string::npos);
	REQUIRE(modmusic.find("stb_vorbis_get_samples_short_interleaved") != std::string::npos);
	REQUIRE(modmusic.find("stb_vorbis_close") != std::string::npos);

	std::string vorbis = readFile("port/include/external/stb_vorbis.h");
	REQUIRE(vorbis.find("stb_vorbis_open_memory") != std::string::npos);
	REQUIRE(vorbis.find("stb_vorbis_get_samples_short_interleaved") != std::string::npos);

	std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("distribParseAudioCategoryValue") != std::string::npos);
	REQUIRE(distrib.find("\"sound.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"voice.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"music.ini\"") != std::string::npos);
}

TEST_CASE("external UI font and language descriptors use standard files",
          "[modding][pdmod][static][c3809]") {
	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("ui.ini - external UI texture metadata") != std::string::npos);
	REQUIRE(scanner.find("UI texture authoring supports PNG or TGA") != std::string::npos);
	REQUIRE(scanner.find("e->ext.ui.texture_file") != std::string::npos);
	REQUIRE(scanner.find("e->ext.ui.layout_file") != std::string::npos);
	REQUIRE(scanner.find("e->ext.ui.nineslice_file") != std::string::npos);
	REQUIRE(scanner.find("e->ext.ui.nineslice_left") != std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, lf)") == std::string::npos);
	REQUIRE(scanner.find("font.ini - external UI font metadata") != std::string::npos);
	REQUIRE(scanner.find("Font authoring supports TTF or OTF") != std::string::npos);
	REQUIRE(scanner.find("lang.ini - external UTF-8 language-bank metadata") != std::string::npos);
	REQUIRE(scanner.find("iniGetInt(ini, \"bank_id\",") != std::string::npos);
	REQUIRE(scanner.find("iniGetInt(ini, \"source_bank\", -1)") != std::string::npos);
	REQUIRE(scanner.find("e->ext.lang.locale") != std::string::npos);
	REQUIRE(scanner.find("e->ext.lang.lang_category") != std::string::npos);
	REQUIRE(scanner.find("e->ext.lang.string_count") != std::string::npos);
	REQUIRE(scanner.find("strings_file = strings.json") != std::string::npos);
	REQUIRE(scanner.find("\"strings" "_tsv\"") == std::string::npos);

	std::string catalog = readFile("port/include/assetcatalog.h");
	REQUIRE(catalog.find("ASSET_FONT") != std::string::npos);
	REQUIRE(catalog.find("char nineslice_file[FS_MAXPATH]") != std::string::npos);
	REQUIRE(catalog.find("s32 nineslice_left") != std::string::npos);
	REQUIRE(catalog.find("char strings_file[FS_MAXPATH]") != std::string::npos);
	REQUIRE(catalog.find("char locale[16]") != std::string::npos);
	REQUIRE(catalog.find("char lang_category[32]") != std::string::npos);
	REQUIRE(catalog.find("u32 string_count") != std::string::npos);

	std::string theme = readFile("port/fast3d/pdgui_theme.cpp");
	REQUIRE(theme.find("stbi_load_from_memory") != std::string::npos);
	REQUIRE(theme.find("GL_MAX_TEXTURE_SIZE") != std::string::npos);
	REQUIRE(theme.find("pdguiFontMgrLoadFont(entry->id, path, 24.0f)") != std::string::npos);
	REQUIRE(theme.find("assetCatalogIterateByType(ASSET_UI, s_applyCatalogUiAsset") != std::string::npos);
	REQUIRE(theme.find("assetCatalogIterateByType(ASSET_FONT, s_applyCatalogUiAsset") != std::string::npos);

	std::string theme_loader = readFile("port/fast3d/pdgui_theme_loader.cpp");
	REQUIRE(theme_loader.find("assetCatalogRegister(k_BuiltinIds[i], ASSET_THEME)") != std::string::npos);
	REQUIRE(theme_loader.find("assetCatalogIterateByType(ASSET_THEME, register_catalog_theme_entry") != std::string::npos);
	REQUIRE(theme_loader.find("assetRuntimeFindByTypeAndId(ASSET_THEME") != std::string::npos);
	REQUIRE(theme_loader.find("ASSET.CHAIN: enabled theme") != std::string::npos);
	REQUIRE(theme_loader.find("fileProviderPath(entry->source.primary)") == std::string::npos);

	std::string lang = readFile("port/src/langmanifest.c");
	REQUIRE(lang.find("langManifestLoadExternalJson") != std::string::npos);
	REQUIRE(lang.find("fsFileLoad(path, &raw_size)") != std::string::npos);
	REQUIRE(lang.find("langManifestPrepareExternalJson(entry, &candidate)") != std::string::npos);
	REQUIRE(lang.find("langManifestPublishExternalJson(entry, &candidate)") != std::string::npos);
	REQUIRE(lang.find("g_LangBanks[bank] = candidate->data") != std::string::npos);
	REQUIRE(lang.find("candidate->data = NULL") != std::string::npos);

	std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("\"ui.ini\"") != std::string::npos);
	REQUIRE(distrib.find("e->ext.ui.texture_file") != std::string::npos);
	REQUIRE(distrib.find("e->ext.ui.layout_file") != std::string::npos);
	REQUIRE(distrib.find("e->ext.ui.nineslice_file") != std::string::npos);
	REQUIRE(distrib.find("e->ext.ui.nineslice_left") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, lf)") == std::string::npos);
	REQUIRE(distrib.find("\"font.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"lang.ini\"") != std::string::npos);
	REQUIRE(distrib.find("iniGetInt(ini, \"bank_id\",") != std::string::npos);
	REQUIRE(distrib.find("iniGetInt(ini, \"source_bank\", -1)") != std::string::npos);
	REQUIRE(distrib.find("e->ext.lang.locale") != std::string::npos);
	REQUIRE(distrib.find("e->ext.lang.lang_category") != std::string::npos);
	REQUIRE(distrib.find("e->ext.lang.string_count") != std::string::npos);
	REQUIRE(distrib.find("e->ext.lang.strings_file") != std::string::npos);

	std::string loader_ui = readFile("port/src/loader_walker_ui.c");
	REQUIRE(loader_ui.find("e->ext.ui.texture_file") != std::string::npos);
	REQUIRE(loader_ui.find("e->ext.ui.layout_file") != std::string::npos);
	REQUIRE(loader_ui.find("e->ext.ui.nineslice_file") != std::string::npos);
	REQUIRE(loader_ui.find("e->ext.ui.nineslice_left") != std::string::npos);

	std::string runtime = readFile("port/src/asset_runtime.c");
	REQUIRE(runtime.find("case ASSET_UI:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.ui.nineslice_file") != std::string::npos);
	REQUIRE(runtime.find("binding->ui_nineslice_left") != std::string::npos);
}

TEST_CASE("catalog game mode and bot profile assets drive live runtime selectors",
          "[modding][pdxxx][runtime][c3838]") {
	std::string scenarios = readFile("src/game/mplayer/scenarios.c");
	REQUIRE(scenarios.find("assetCatalogIterateUnlockedByType(ASSET_GAMEMODE, scenarioCountCb, &ctx)") != std::string::npos);
	REQUIRE(scenarios.find("assetCatalogIterateUnlockedByType(ASSET_GAMEMODE, scenarioPickByIndexCb, &ctx)") != std::string::npos);
	REQUIRE(scenarios.find("g_MpSetup.scenario = ctx.scenario_match") != std::string::npos);
	REQUIRE(scenarios.find("scenarioInit()") != std::string::npos);
	REQUIRE(scenarios.find("binding->gamemode_name") != std::string::npos);
	REQUIRE(scenarios.find("scenarioBindingForEntry") != std::string::npos);

	std::string setup = readFile("src/game/mplayer/setup.c");
	REQUIRE(setup.find("assetCatalogIterateUnlockedByType(ASSET_BOT_PROFILE, botprofileCountCb, &ctx)") != std::string::npos);
	REQUIRE(setup.find("botprofilePickByIndexCb, ctx") != std::string::npos);
	REQUIRE(setup.find("mpCreateBotFromProfileId(botnum, ctx.result_id)") != std::string::npos);
	REQUIRE(setup.find("g_BotConfigsArray[botnum].type = (u8)profile->bot_profile_type") != std::string::npos);
	REQUIRE(setup.find("g_BotProfiles[profnum]") == std::string::npos);
	REQUIRE(setup.find("assetCatalogIterateUnlockedByType(ASSET_BOT_PROFILE, botdiffCountCb, &ctx)") != std::string::npos);
	REQUIRE(setup.find("return (uintptr_t)langGet(L_MISC_082 + ctx.result_diffidx)") != std::string::npos);

	std::string base = readFile("port/src/assetcatalog_base_extended.c");
	REQUIRE(base.find("assetCatalogRegisterGameMode(") != std::string::npos);
	REQUIRE(base.find("assetCatalogRegisterBotProfile(") != std::string::npos);
	REQUIRE(base.find("e->mp_index = (s16)s_BaseGameModes[i].mode_id") != std::string::npos);
	REQUIRE(base.find("e->mp_index = (s16)i") != std::string::npos);

	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("case ASSET_GAMEMODE:") != std::string::npos);
	REQUIRE(scanner.find("e->ext.gamemode.mode_id = parseModeKeyValue(") != std::string::npos);
	REQUIRE(scanner.find("e->ext.gamemode.rules_file") != std::string::npos);
	REQUIRE(scanner.find("case ASSET_BOT_PROFILE:") != std::string::npos);
	REQUIRE(scanner.find("e->ext.bot_profile.type = parseBotTypeKeyValue(") != std::string::npos);
	REQUIRE(scanner.find("e->ext.bot_profile.difficulty = parseBotDifficultyKeyValue(") != std::string::npos);
	REQUIRE(scanner.find("e->ext.bot_profile.target_body") != std::string::npos);
	REQUIRE(scanner.find("e->ext.bot_profile.profile_file") != std::string::npos);

	std::string catalog = readFile("port/include/assetcatalog.h");
	REQUIRE(catalog.find("char rules_file[FS_MAXPATH]") != std::string::npos);
	REQUIRE(catalog.find("char target_body[CATALOG_ID_LEN]") != std::string::npos);
	REQUIRE(catalog.find("char profile_file[FS_MAXPATH]") != std::string::npos);

	std::string runtime = readFile("port/src/asset_runtime.c");
	REQUIRE(runtime.find("entry->ext.gamemode.rules_file") != std::string::npos);
	REQUIRE(runtime.find("binding->gamemode_name") != std::string::npos);
	REQUIRE(runtime.find("binding->gamemode_description") != std::string::npos);
	REQUIRE(runtime.find("binding->gamemode_max_players = entry->ext.gamemode.max_players") != std::string::npos);
	REQUIRE(runtime.find("binding->gamemode_requirefeature = entry->ext.gamemode.requirefeature") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.bot_profile.profile_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.bot_profile.target_body") != std::string::npos);
	REQUIRE(runtime.find("binding->bot_profile_type = entry->ext.bot_profile.type") != std::string::npos);
	REQUIRE(runtime.find("binding->bot_profile_difficulty = entry->ext.bot_profile.difficulty") != std::string::npos);
	REQUIRE(runtime.find("binding->bot_profile_name_langid = entry->ext.bot_profile.name_langid") != std::string::npos);
	REQUIRE(runtime.find("binding->bot_profile_requirefeature = entry->ext.bot_profile.requirefeature") != std::string::npos);

	std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("distribParseModeKeyValue") != std::string::npos);
	REQUIRE(distrib.find("distribParseBotTypeKeyValue") != std::string::npos);
	REQUIRE(distrib.find("distribParseBotDifficultyKeyValue") != std::string::npos);
	REQUIRE(distrib.find("e->ext.gamemode.rules_file") != std::string::npos);
	REQUIRE(distrib.find("e->ext.bot_profile.target_body") != std::string::npos);
}

TEST_CASE("skin assets expose saved texture payloads through catalog provider paths",
          "[modding][pdxxx][runtime][c3838][skin]") {
	const std::string catalog = readFile("port/include/assetcatalog.h");
	REQUIRE(catalog.find("char texture_file[FS_MAXPATH];    /* appearance payload for this skin */") != std::string::npos);
	REQUIRE(catalog.find("char swatches_file[FS_MAXPATH];   /* editable color swatch source */") != std::string::npos);
	REQUIRE(catalog.find("char material_archive[FS_MAXPATH]; /* typed material dependency archive */") != std::string::npos);
	REQUIRE(catalog.find("char texture_archive[FS_MAXPATH];  /* typed texture dependency archive */") != std::string::npos);

	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("case ASSET_SKIN:") != std::string::npos);
	REQUIRE(scanner.find("texture_file = texture.tga") != std::string::npos);
	REQUIRE(scanner.find("e->ext.skin.texture_file") != std::string::npos);
	REQUIRE(scanner.find("e->ext.skin.swatches_file") != std::string::npos);
	REQUIRE(scanner.find("e->ext.skin.material_archive") != std::string::npos);
	REQUIRE(scanner.find("e->ext.skin.texture_archive") != std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.skin.texture_file)") != std::string::npos);

	const std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.skin.texture_file)") != std::string::npos);
	REQUIRE(distrib.find("e->ext.skin.swatches_file") != std::string::npos);
	REQUIRE(distrib.find("e->ext.skin.material_archive") != std::string::npos);
	REQUIRE(distrib.find("e->ext.skin.texture_archive") != std::string::npos);

	const std::string editor = readFile("port/fast3d/pdgui_skin_editor.cpp");
	REQUIRE(editor.find("texture_file = texture.tga") != std::string::npos);
	REQUIRE(editor.find("catalogSetPrimaryFile(entry, entry->ext.skin.texture_file)") != std::string::npos);

	const std::string load = readFile("port/src/assetcatalog_load.c");
	REQUIRE(load.find("type == ASSET_SKIN") != std::string::npos);

	const std::string runtime = readFile("port/src/asset_runtime.c");
	REQUIRE(runtime.find("entry->ext.skin.swatches_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.material_archive") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.texture_archive") != std::string::npos);
	const std::string runtimeHeader = readFile("port/include/asset_runtime.h");
	REQUIRE(runtimeHeader.find("char dependency_d[FS_MAXPATH]") != std::string::npos);
	REQUIRE(runtimeHeader.find("char dependency_e[FS_MAXPATH]") != std::string::npos);

	const std::string walker = readFile("port/src/loader_walker_meta.c");
	REQUIRE(walker.find("\"swatches_file\"") != std::string::npos);
	REQUIRE(walker.find("\"material_archive\"") != std::string::npos);
	REQUIRE(walker.find("\"texture_archive\"") != std::string::npos);
}

TEST_CASE("metadata asset families expose primary authored files through catalog provider paths",
          "[modding][pdxxx][runtime][c3838][files]") {
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("material_file = material.json") != std::string::npos);
	REQUIRE(scanner.find("texture_file = texture.png") != std::string::npos);
	REQUIRE(scanner.find("effect_file = effect.graph.json") != std::string::npos);
	REQUIRE(scanner.find("timeline_file = timeline.json") != std::string::npos);
	REQUIRE(scanner.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(scanner.find("scenario_archive = dependencies/assets/scenario/mission.pdscenario") != std::string::npos);
	REQUIRE(scanner.find("case ASSET_MATERIAL:") != std::string::npos);
	REQUIRE(scanner.find("case ASSET_EFFECT:") != std::string::npos);
	REQUIRE(scanner.find("case ASSET_HUD:") != std::string::npos);
	REQUIRE(scanner.find("case ASSET_PROP:") != std::string::npos);
	REQUIRE(scanner.find("case ASSET_VEHICLE:") != std::string::npos);
	REQUIRE(scanner.find("case ASSET_MISSION:") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"material_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"effect_file\"") != std::string::npos);
	REQUIRE(scanner.find("e->ext.weapon.primary_graph") != std::string::npos);
	REQUIRE(scanner.find("e->ext.weapon.secondary_graph") != std::string::npos);
	REQUIRE(scanner.find("e->ext.weapon.shared_context") != std::string::npos);
	REQUIRE(scanner.find("e->ext.effect.timeline_file") != std::string::npos);
	REQUIRE(scanner.find("e->ext.prop.behavior_graph") != std::string::npos);
	REQUIRE(scanner.find("e->ext.vehicle.behavior_graph : e->ext.vehicle.physics_file") ==
		std::string::npos);
	{
		const auto vehicleStart = scanner.find("case ASSET_VEHICLE:");
		const auto missionStart = scanner.find("case ASSET_MISSION:", vehicleStart);
		REQUIRE(vehicleStart != std::string::npos);
		REQUIRE(missionStart != std::string::npos);
		const std::string vehicleCase =
			scanner.substr(vehicleStart, missionStart - vehicleStart);
		const auto primaryStart =
			vehicleCase.find("const char *pf = e->ext.vehicle.model_file[0]");
		const auto primaryEnd = vehicleCase.find("if (pf[0])", primaryStart);
		REQUIRE(primaryStart != std::string::npos);
		REQUIRE(primaryEnd != std::string::npos);
		const std::string primaryExpr =
			vehicleCase.substr(primaryStart, primaryEnd - primaryStart);
		REQUIRE(primaryExpr.find("physics_file") == std::string::npos);
	}
	{
		const auto missionStart = scanner.find("case ASSET_MISSION:");
		const auto themeStart = scanner.find("case ASSET_THEME:", missionStart);
		REQUIRE(missionStart != std::string::npos);
		REQUIRE(themeStart != std::string::npos);
		const std::string missionCase =
			scanner.substr(missionStart, themeStart - missionStart);
		REQUIRE(missionCase.find("e->ext.mission.mission_graph_file") !=
			std::string::npos);
		REQUIRE(missionCase.find("e->ext.mission.scenario_archive[0]") ==
			std::string::npos);
		REQUIRE(missionCase.find("e->ext.mission.objectives_file[0]") ==
			std::string::npos);
		REQUIRE(missionCase.find("e->ext.mission.briefing_file") ==
			std::string::npos);
	}
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.hud.layout_file)") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"scenario_archive\"") != std::string::npos);

	const std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("case ASSET_MATERIAL:") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_MODEL:") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_EFFECT:") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_HUD:") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_PROP:") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_VEHICLE:") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_MISSION:") != std::string::npos);
	REQUIRE(distrib.find("if (strcmp(ini_name, \"mesh.ini\") == 0)      return ASSET_MODEL;") != std::string::npos);
	REQUIRE(distrib.find("if (strcmp(ini_name, \"model.ini\") == 0)     return ASSET_MODEL;") != std::string::npos);
	REQUIRE(distrib.find("\"material.ini\", \"mesh.ini\", \"model.ini\", \"effect.ini\"") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, pf)") != std::string::npos);
	REQUIRE(distrib.find("e->ext.weapon.primary_graph") != std::string::npos);
	REQUIRE(distrib.find("e->ext.weapon.secondary_graph") != std::string::npos);
	REQUIRE(distrib.find("e->ext.weapon.shared_context") != std::string::npos);
	REQUIRE(distrib.find("e->ext.effect.timeline_file") != std::string::npos);
	REQUIRE(distrib.find("e->ext.prop.behavior_graph") != std::string::npos);
	REQUIRE(distrib.find("e->ext.vehicle.behavior_graph : e->ext.vehicle.physics_file") ==
		std::string::npos);
	{
		const auto vehicleStart = distrib.find("case ASSET_VEHICLE:");
		const auto missionStart = distrib.find("case ASSET_MISSION:", vehicleStart);
		REQUIRE(vehicleStart != std::string::npos);
		REQUIRE(missionStart != std::string::npos);
		const std::string vehicleCase =
			distrib.substr(vehicleStart, missionStart - vehicleStart);
		const auto primaryStart =
			vehicleCase.find("const char *pf = e->ext.vehicle.model_file[0]");
		const auto primaryEnd = vehicleCase.find("if (pf[0])", primaryStart);
		REQUIRE(primaryStart != std::string::npos);
		REQUIRE(primaryEnd != std::string::npos);
		const std::string primaryExpr =
			vehicleCase.substr(primaryStart, primaryEnd - primaryStart);
		REQUIRE(primaryExpr.find("physics_file") == std::string::npos);
	}
	{
		const auto missionStart = distrib.find("case ASSET_MISSION:");
		const auto themeStart = distrib.find("case ASSET_THEME:", missionStart);
		REQUIRE(missionStart != std::string::npos);
		REQUIRE(themeStart != std::string::npos);
		const std::string missionCase =
			distrib.substr(missionStart, themeStart - missionStart);
		REQUIRE(missionCase.find("e->ext.mission.mission_graph_file") !=
			std::string::npos);
		REQUIRE(missionCase.find("e->ext.mission.scenario_archive[0]") ==
			std::string::npos);
		REQUIRE(missionCase.find("e->ext.mission.objectives_file[0]") ==
			std::string::npos);
		REQUIRE(missionCase.find("e->ext.mission.briefing_file") ==
			std::string::npos);
	}
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.hud.layout_file)") != std::string::npos);
	REQUIRE(distrib.find("distribParseModeString(iniGet(ini, \"mode\", \"\"))") != std::string::npos);
	REQUIRE(distrib.find("strcmp(t, \"mp\") == 0") != std::string::npos);
	REQUIRE(distrib.find("strcmp(t, \"solo\") == 0") != std::string::npos);
	REQUIRE(distrib.find("strcmp(t, \"coop\") == 0") != std::string::npos);

	const std::string load = readFile("port/src/assetcatalog_load.c");
	REQUIRE(load.find("type == ASSET_VEHICLE") != std::string::npos);
	REQUIRE(load.find("type == ASSET_MISSION") != std::string::npos);
	REQUIRE(load.find("type == ASSET_HUD") != std::string::npos);
	REQUIRE(load.find("type == ASSET_EFFECT") != std::string::npos);
	REQUIRE(load.find("type == ASSET_MATERIAL") != std::string::npos);

	const std::string runtime = readFile("port/src/asset_runtime.c");
	REQUIRE(runtime.find("entry->ext.weapon.primary_graph") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.secondary_graph") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.shared_context") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.effect.timeline_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.prop.behavior_graph") != std::string::npos);
	REQUIRE(runtime.find("binding->dependency_b") != std::string::npos);
}

TEST_CASE("external models maps and animations compile from standard sources",
          "[modding][pdmod][static][c3809]") {
	std::string compiler_h = readFile("port/include/modasset_compiler.h");
	REQUIRE(compiler_h.find("MODASSET_COMPILER_VERSION") != std::string::npos);
	REQUIRE(compiler_h.find("modasset_compiled_result_t") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerIsExternalSource") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerIsAnimationSource") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerCompileReadable") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerEnsureCache") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerBuildColmesh") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerBuildObjColmesh") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerBuildModeldef") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerFreeModeldef") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerSetGeneratedModeldefRenderAudit") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerTraceGeneratedModeldefRender") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerBuildAnimationClip") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerFreeAnimationClip") != std::string::npos);
	REQUIRE(compiler_h.find(".bin suffix") != std::string::npos);

	std::string compiler = readFile("port/src/modasset_compiler.c");
	REQUIRE(compiler.find("u32 render_audit_step_mask;") != std::string::npos);
	REQUIRE(compiler.find("s32 needler_render_audit_witness_logged;") != std::string::npos);
	REQUIRE(compiler.find("generatedModeldefRenderAuditStageBit") != std::string::npos);
	REQUIRE(compiler.find("\"gundl-pre-opa\"") != std::string::npos);
	REQUIRE(compiler.find("\"dl-post-xlu-displaylist\"") != std::string::npos);
	REQUIRE(compiler.find("\"bgun-after-mtxF2L\"") != std::string::npos);
	REQUIRE(compiler.find("owner->render_audit_step_mask & stage_bit") != std::string::npos);
	REQUIRE(compiler.find("owner->render_audit_step_mask |= stage_bit") != std::string::npos);
	REQUIRE(compiler.find("!owner->needler_render_audit_witness_logged") != std::string::npos);
	REQUIRE(compiler.find("owner->needler_render_audit_witness_logged = 1") != std::string::npos);
	REQUIRE(compiler.find("endsWithNoCase(path, \".gltf\")") != std::string::npos);
	REQUIRE(compiler.find("endsWithNoCase(path, \".glb\")") != std::string::npos);
	REQUIRE(compiler.find("endsWithNoCase(path, \".obj\")") != std::string::npos);
	REQUIRE(compiler.find("parseObjSource") != std::string::npos);
	REQUIRE(compiler.find("parseObjFaceLine") != std::string::npos);
	REQUIRE(compiler.find("parseObjTexcoordLine") != std::string::npos);
	REQUIRE(compiler.find("resolveObjIndex(raw_texcoord, texcoord_count") != std::string::npos);
	REQUIRE(compiler.find("usemtl") != std::string::npos);
	REQUIRE(compiler.find("validateGeneratedMeshIntegerNativeBoundary") != std::string::npos);
	REQUIRE(compiler.find("mesh_native_coord_overflow") != std::string::npos);
	REQUIRE(compiler.find("mesh_native_uv_overflow") != std::string::npos);
	REQUIRE(compiler.find("collapsed_triangles=%d/%d") != std::string::npos);
	REQUIRE(compiler.find("modeldef native integer boundary failed") != std::string::npos);
	REQUIRE(compiler.find("generatedModeldefLoadMaterialMetadata") != std::string::npos);
	REQUIRE(compiler.find("modAssetMaterialCatalogTextureDirective(p,") != std::string::npos);
	const std::string material_directives = readFile("port/src/modasset_material_order.c");
	REQUIRE(material_directives.find("pd_texture_catalog") != std::string::npos);
	REQUIRE(material_directives.find("pd_secondary_texture_catalog") != std::string::npos);
	REQUIRE(compiler.find("objMaterialSetTextureMapPath") != std::string::npos);
	REQUIRE(compiler.find("map_Kd") != std::string::npos);
	REQUIRE(compiler.find("assetCatalogIterateByType(ASSET_TEXTURE") != std::string::npos);
	REQUIRE(compiler.find("did not resolve to a catalog texture") != std::string::npos);
	REQUIRE(compiler.find("while (*src && isspace((u8)*src))") != std::string::npos);
	REQUIRE(compiler.find("texLoadFromGdl") != std::string::npos);
	REQUIRE(compiler.find("\\\"texcoord_count\\\"") != std::string::npos);
	REQUIRE(compiler.find("\\\"triangle_texcoords\\\"") != std::string::npos);
	REQUIRE(compiler.find("writeObjMeshJson") != std::string::npos);
	REQUIRE(compiler.find("validateObjSource") != std::string::npos);
	REQUIRE(compiler.find("validateGltfSource") != std::string::npos);
	REQUIRE(compiler.find("validateGlbSource") != std::string::npos);
	REQUIRE(compiler.find("parseGltfLikeMeshSource") != std::string::npos);
	REQUIRE(compiler.find("gltf_external_binary_buffers_are_not_allowed") != std::string::npos);
	REQUIRE(compiler.find("\"$H/mod-cache\"") != std::string::npos);
	REQUIRE(compiler.find("$S/mod-cache") != std::string::npos);
	REQUIRE(compiler.find("$B/mod-cache") != std::string::npos);
	REQUIRE(compiler.find("cacheBuildPaths") != std::string::npos);
	REQUIRE(compiler.find("cacheBuildCompactPaths") != std::string::npos);
	REQUIRE(compiler.find("cacheHashField(&hash, entry->category)") != std::string::npos);
	REQUIRE(compiler.find("cacheHashField(&hash, entry->id)") != std::string::npos);
	REQUIRE(compiler.find("cacheHashField(&hash, source_digest_hex)") != std::string::npos);
	REQUIRE(compiler.find("\"$H/mc\", \"$S/mc\", \"$B/mc\"") != std::string::npos);
	REQUIRE(compiler.find("if (!selected) {\n\t\t\tstatic const char *compact_roots[]") != std::string::npos);
	REQUIRE(compiler.find("private cache root '%s' exceeds the platform path budget") !=
	        std::string::npos);
	REQUIRE(compiler.find(".pdmc") != std::string::npos);
	REQUIRE(compiler.find(".pdmesh.json") != std::string::npos);
	REQUIRE(compiler.find(".pdmodel.json") != std::string::npos);
	REQUIRE(compiler.find(".pdanimation.json") != std::string::npos);
	REQUIRE(compiler.find("char animation_digest_key[17]") == std::string::npos);
	REQUIRE(countOccurrences(compiler, "compiler_version, digest_key") >= 1);
	REQUIRE(compiler.find(
		"MODASSET_COMPILER_ANIMATION_VERSION, digest_key") !=
		std::string::npos);
	REQUIRE(compiler.find(
		"? MODASSET_COMPILER_ANIMATION_VERSION : MODASSET_COMPILER_VERSION") !=
		std::string::npos);
	REQUIRE(compiler.find("source_sha256") != std::string::npos);
	REQUIRE(compiler.find("runtime_boundary") != std::string::npos);
	REQUIRE(compiler.find("runtime_payload") != std::string::npos);
	REQUIRE(compiler.find("readable-generated-cache") != std::string::npos);
	REQUIRE(compiler.find("cache_hit") != std::string::npos);
	REQUIRE(compiler.find("parseGltfLikeAnimationClip") != std::string::npos);
	REQUIRE(compiler.find("gltf_skeletal_channel_pack") != std::string::npos);
	REQUIRE(compiler.find("ANIMFIELD_S32_TRANSLATE") != std::string::npos);
	REQUIRE(compiler.find("ANIMFIELD_F32_ROTATE") != std::string::npos);
	REQUIRE(compiler.find("ANIMFIELD_F32_SCALE") != std::string::npos);
	/* Schema v5 (B-772/B-943 gap #2): verbatim per-part native capture of
	 * ANIMFIELD_08 root-motion + ANIMFIELD_CAMERA so the public .pdanim
	 * round-trips those dropped channels byte-exact (not just via the
	 * native cache). */
	REQUIRE(compiler.find("pd_special_parts") != std::string::npos);
	REQUIRE(compiler.find("gltfBuildNativeFromSpecialParts") != std::string::npos);
	REQUIRE(compiler.find("parseGltfAnimationSpecialParts") != std::string::npos);
	REQUIRE(compiler.find("writeBitsMsb") != std::string::npos);
	REQUIRE(compiler.find("ANIMFIELD_08") != std::string::npos);
	REQUIRE(compiler.find("ANIMFIELD_CAMERA") != std::string::npos);
	REQUIRE(compiler.find("gltf_animation_weights_not_supported") != std::string::npos);
	REQUIRE(compiler.find("gltf_animation_channels_not_supported_yet") == std::string::npos);
	REQUIRE(compiler.find("runtime_boundary\\\": \\\"animtableentry") != std::string::npos);
	REQUIRE(compiler.find("meshAddTriangle(out_mesh") != std::string::npos);
	REQUIRE(compiler.find("buildGeneratedModeldefFromMesh") != std::string::npos);
	REQUIRE(compiler.find("generatedModeldefReadRenderStream") != std::string::npos);
	REQUIRE(compiler.find("model.render.json") != std::string::npos);
	REQUIRE(compiler.find("jsonObjectArray(root, \"commands\", &commands)") != std::string::npos);
	REQUIRE(compiler.find("jsonObjectString(object, \"command\"") != std::string::npos);
	REQUIRE(compiler.find("model.render.tsv") == std::string::npos);
	REQUIRE(compiler.find("MODASSET_RENDER_OP_MTX") != std::string::npos);
	REQUIRE(compiler.find("MODASSET_RENDER_OP_POP") != std::string::npos);
	REQUIRE(compiler.find("MODASSET_RENDER_OP_GEOMETRY_SET") != std::string::npos);
	REQUIRE(compiler.find("MODASSET_RENDER_OP_GEOMETRY_CLEAR") != std::string::npos);
	REQUIRE(compiler.find("MODASSET_RENDER_OP_VERTEX_LOAD") != std::string::npos);
	REQUIRE(compiler.find("MODASSET_RENDER_OP_VERTEX_SCOPE") != std::string::npos);
	REQUIRE(compiler.find("MODASSET_RENDER_OP_VERTEX_CACHE_RESET") != std::string::npos);
	REQUIRE(compiler.find("vertex_cache_slots") != std::string::npos);
	REQUIRE(compiler.find("jsonArrayNextContainerStrict(commands") !=
	        std::string::npos);
	REQUIRE(compiler.find("jsonObjectOptionalInt") != std::string::npos);
	REQUIRE(compiler.find("jsonObjectU32(object, \"geometry_mask\"") !=
	        std::string::npos);
	REQUIRE(compiler.find("vertex_geometry_mode") != std::string::npos);
	REQUIRE(compiler.find("vertex_geometry_known") != std::string::npos);
	REQUIRE(compiler.find("emitGeneratedKnownGeometryMode") !=
	        std::string::npos);
	REQUIRE(compiler.find("modAssetRenderStreamPreservesNodeCombine") !=
	        std::string::npos);
	REQUIRE(compiler.find("modAssetRenderStreamResolveSchema") !=
	        std::string::npos);
	REQUIRE(compiler.find("mcount == 3 || (use_render_stream &&") !=
	        std::string::npos);
	REQUIRE(compiler.find("generatedRenderStreamTriCount") != std::string::npos);
	REQUIRE(compiler.find("face_seen[row->face]") != std::string::npos);
	REQUIRE(compiler.find("generatedRenderStreamContractValid") !=
	        std::string::npos);
	REQUIRE(compiler.find("modAssetRenderStreamCompilerVertexBaseline") !=
	        std::string::npos);
	REQUIRE(compiler.find("invalid vertex scope") != std::string::npos);
	REQUIRE(compiler.find("invalid vertex cache reset") != std::string::npos);
	REQUIRE(compiler.find("invalid vertex cache reference") != std::string::npos);
	REQUIRE(compiler.find("modAssetRenderStreamVertexRelocationValid") !=
	        std::string::npos);
	REQUIRE(compiler.find("unrelocatable G_VTX state") != std::string::npos);
	REQUIRE(compiler.find("duplicate render owner") != std::string::npos);
	REQUIRE(compiler.find("generatedModeldefRegister(owner)") != std::string::npos);
	REQUIRE(compiler.find("generatedModeldefUnregister(owner)") != std::string::npos);
	REQUIRE(compiler.find("MODASSET.RENDER: generated source modeldef rendered") != std::string::npos);
	REQUIRE(compiler.find("NEEDLER SOURCE MODEL RENDERED") != std::string::npos);
	REQUIRE(compiler.find("MODELNODETYPE_POSITION") != std::string::npos);
	REQUIRE(compiler.find("MODELNODETYPE_DL") != std::string::npos);
	REQUIRE(compiler.find("gSPMatrix(gdl++") != std::string::npos);
	REQUIRE(compiler.find("gSPPopMatrix(gdl++") != std::string::npos);

	const std::string conformance = readFile("tools/asset_archive_conformance.py");
	const std::string render_stream_contract =
		readFile("tools/pdmesh_render_stream.py");
	const std::string render_state_verifier =
		readFile("tools/verify_pdmesh_render_state.py");
	REQUIRE(conformance.find("validate_pdmesh_integer_native_boundary") != std::string::npos);
	REQUIRE(conformance.find("validate_direct_model_source_integer_native_boundary") != std::string::npos);
	REQUIRE(conformance.find("validate_pdmesh_obj_integer_native_boundary") != std::string::npos);
	REQUIRE(conformance.find("validate_pdmesh_gltf_integer_native_boundary") != std::string::npos);
	REQUIRE(conformance.find("validate_vehicle_source_contract") != std::string::npos);
	REQUIRE(conformance.find("validate_mission_source_contract") != std::string::npos);
	REQUIRE(conformance.find("validate_mission_briefing_json_schema") == std::string::npos);
	REQUIRE(conformance.find("pdmesh_gltf_json_and_bin") != std::string::npos);
	// Standard glTF buffers use the shared source loader and archive-relative
	// validation; rejecting every external buffer is an obsolete source policy.
	REQUIRE(conformance.find("load_gltf_buffer(gltf, source_member, archive)") != std::string::npos);
	REQUIRE(conformance.find("POSITION vertex") != std::string::npos);
	REQUIRE(conformance.find("model.glb") != std::string::npos);
	REQUIRE(conformance.find("model.gltf") != std::string::npos);
	REQUIRE(conformance.find("ext in {\".pdprop\", \".pdvehicle\"}") != std::string::npos);
	REQUIRE(conformance.find("direct_model_source_members(name_set)") != std::string::npos);
	REQUIRE(conformance.find("weapon.ini model_file member {model_file} is missing") != std::string::npos);
	REQUIRE(conformance.find("optional_source_members =") != std::string::npos);
	REQUIRE(conformance.find("retired_weapon_bindings =") != std::string::npos);
	REQUIRE(conformance.find("contains retired member {member} with no production consumer") != std::string::npos);
	REQUIRE(conformance.find("manifest.get(key)") != std::string::npos);
	REQUIRE(conformance.find("vertex cannot quantize to native s16 coordinates") != std::string::npos);
	REQUIRE(conformance.find("texcoord cannot quantize to native s16 UV units") != std::string::npos);
	REQUIRE(conformance.find("render_root, len(faces), render_group_modes") !=
	        std::string::npos);
	REQUIRE(conformance.find("duplicates render group") != std::string::npos);
	REQUIRE(render_stream_contract.find("geometry_set") != std::string::npos);
	REQUIRE(render_stream_contract.find("geometry_clear") != std::string::npos);
	REQUIRE(render_stream_contract.find("geometry_mask") != std::string::npos);
	REQUIRE(render_stream_contract.find("vertex_load") != std::string::npos);
	REQUIRE(render_stream_contract.find("vertex_scope") != std::string::npos);
	REQUIRE(render_stream_contract.find("vertex_cache_reset") != std::string::npos);
	REQUIRE(render_stream_contract.find("vertex_cache_slots") != std::string::npos);
	REQUIRE(render_stream_contract.find("VERTEX_CACHE_SLOTS = 64") !=
	        std::string::npos);
	REQUIRE(render_stream_contract.find("MAX_MATRIX_INDEX = 32766") !=
	        std::string::npos);
	REQUIRE(render_stream_contract.find("TYPE3_COMPILER_BASELINE_KNOWN") !=
	        std::string::npos);
	REQUIRE(render_stream_contract.find("SCHEMA_GEOMETRY = 2") !=
	        std::string::npos);
	REQUIRE(render_stream_contract.find("SCHEMA_VERTEX_CACHE = 3") !=
	        std::string::npos);
	REQUIRE(render_stream_contract.find("SCHEMA_CURRENT = 4") !=
	        std::string::npos);
	REQUIRE(render_stream_contract.find("Type-3 render owner") !=
	        std::string::npos);
	REQUIRE(render_stream_contract.find("ambiguous G_VTX geometry state") !=
	        std::string::npos);
	REQUIRE(render_stream_contract.find("unrelocatable G_VTX geometry state") !=
	        std::string::npos);
	REQUIRE(render_stream_contract.find("G_LIGHTING = 0x00020000") !=
	        std::string::npos);
	REQUIRE(render_stream_contract.find("pd2.mesh.render.v1") !=
	        std::string::npos);
	REQUIRE(render_stream_contract.find(
		"RENDER_NODE_TYPES = {0x04, 0x16, 0x18}") != std::string::npos);
	REQUIRE(render_state_verifier.find("render, len(face_rows), render_nodes") !=
	        std::string::npos);
	REQUIRE(render_state_verifier.find("schema v3 required") !=
	        std::string::npos);
	REQUIRE(render_state_verifier.find("geometry_commands") !=
	        std::string::npos);
	REQUIRE(render_state_verifier.find("vertex_loads") != std::string::npos);
	REQUIRE(render_state_verifier.find("vertex_cache_resets") !=
	        std::string::npos);
	REQUIRE(render_state_verifier.find("retained_vertex_corners") !=
	        std::string::npos);
	REQUIRE(render_state_verifier.find("baseline") != std::string::npos);
	REQUIRE(render_state_verifier.find("vertex_lighting") !=
	        std::string::npos);
	REQUIRE(render_state_verifier.find("expected lighting") !=
	        std::string::npos);
	REQUIRE(render_state_verifier.find("PATH:STATE=COUNT") !=
	        std::string::npos);
	REQUIRE(render_state_verifier.find("vertex_lighting_counts") !=
	        std::string::npos);
	REQUIRE(compiler.find("gSPTexture(gdl++, 0, 0, 0, 0, 0)") != std::string::npos);
	/* B-938: the compiler no longer invents an all-white vertex colour -- it
	 * dedups authored per-vertex RGBA into a Col table and stores the index. */
	REQUIRE(compiler.find("dst->colour = (u8)((u32)colour_index << 2)") != std::string::npos);
	REQUIRE(compiler.find("dst->s = clampToS16(texcoord->u * 32.0f)") != std::string::npos);
	REQUIRE(compiler.find("dst->t = clampToS16((1.0f - texcoord->v) * 32.0f)") != std::string::npos);
	REQUIRE(compiler.find("uv_vertices=%d") != std::string::npos);
	REQUIRE(compiler.find("gSPClearGeometryMode(gdl++") != std::string::npos);
	REQUIRE(compiler.find("compiler_baseline_known & ~compiler_baseline_mode") !=
	        std::string::npos);
	REQUIRE(compiler.find("compiler_baseline_mode |") != std::string::npos);
	REQUIRE(compiler.find("gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE)") != std::string::npos);
	REQUIRE(compiler.find("G_NOOP") != std::string::npos);
	REQUIRE(compiler.find("textured_materials=%d") != std::string::npos);
	REQUIRE(compiler.find("gSPVertex(gdl++") != std::string::npos);
	REQUIRE(compiler.find("gSP1Triangle(gdl++") != std::string::npos);
	REQUIRE(compiler.find("endsWithNoCase(path, \".gltf\")") != std::string::npos);
	REQUIRE(compiler.find("endsWithNoCase(path, \".glb\")") != std::string::npos);
	REQUIRE(compiler.find("buildAnimationClipFromTsv") == std::string::npos);
	REQUIRE(compiler.find("animationSiblingPath(frames_path, \"header.tsv\"") ==
	        std::string::npos);

	std::string mesh_h = readFile("src/include/lib/meshcollision.h");
	REQUIRE(mesh_h.find("void meshInit(struct colmesh *mesh)") != std::string::npos);
	REQUIRE(mesh_h.find("bool meshAddTriangle(struct colmesh *mesh") != std::string::npos);

	std::string load = readFile("port/src/assetcatalog_load.c");
	REQUIRE(load.find("#include \"modasset_compiler.h\"") != std::string::npos);
	REQUIRE(load.find("modAssetCompilerIsAnimationSource(source_path)") != std::string::npos);
	REQUIRE(load.find("modAssetCompilerCompileReadable(entry") != std::string::npos);
	REQUIRE(load.find("catalogModelGenerationAcquireSource(entry, source_path,") != std::string::npos);
	REQUIRE(load.find("modeldef = catalogModelGenerationModeldef(generation)") != std::string::npos);
	const std::string generation = readFile("port/src/catalog_model_generation.cpp");
	REQUIRE(generation.find("modAssetCompilerBuildModeldefWithInputs(&inputs, entry, selected, &model)") != std::string::npos);
	REQUIRE(load.find("modAssetCompilerBuildColmesh(colmesh_source_path, mesh)") != std::string::npos);
	REQUIRE(load.find("modAssetCompilerBuildAnimationClip(entry") != std::string::npos);
	REQUIRE(load.find("ASSET_PAYLOAD_COLMESH") != std::string::npos);
	REQUIRE(load.find("ASSET_PAYLOAD_ANIMATION_CLIP") != std::string::npos);
	REQUIRE(load.find("catalogGetLoadedColmesh") != std::string::npos);
	REQUIRE(load.find("catalogGetLoadedAnimationClip") != std::string::npos);
	REQUIRE(load.find("activated external %s modeldef") != std::string::npos);
	REQUIRE(load.find("modAssetCompilerFreeModeldef") != std::string::npos);
	REQUIRE(load.find("modAssetCompilerFreeAnimationClip") != std::string::npos);
	REQUIRE(load.find("external %s source ready via private cache") != std::string::npos);
	REQUIRE(load.find("modAssetCompilerIsExternalSource(source_path)") != std::string::npos);
	REQUIRE(load.find("#include \"catalog_mgr_bodies.h\"") != std::string::npos);
	REQUIRE(load.find("#include \"catalog_mgr_heads.h\"") != std::string::npos);
	REQUIRE(load.find("catalogManagerResetBodyModeldef(entry->runtime_index)") != std::string::npos);
	REQUIRE(load.find("catalogManagerResetHeadModeldef(entry->runtime_index)") != std::string::npos);
	const size_t reset_body_modeldef =
		load.find("catalogManagerResetBodyModeldef(entry->runtime_index)");
	const size_t reset_head_modeldef =
		load.find("catalogManagerResetHeadModeldef(entry->runtime_index)");
	const size_t free_modeldef =
		load.find("modAssetCompilerFreeModeldef((struct modeldef *)entry->loaded_data)");
	REQUIRE(reset_body_modeldef != std::string::npos);
	REQUIRE(reset_head_modeldef != std::string::npos);
	REQUIRE(free_modeldef != std::string::npos);
	REQUIRE(reset_body_modeldef < free_modeldef);
	REQUIRE(reset_head_modeldef < free_modeldef);

	std::string main_c = readFile("port/src/main.c");
	REQUIRE(main_c.find("--debug-generated-mesh-render-audit") != std::string::npos);
	REQUIRE(main_c.find("modAssetCompilerSetGeneratedModeldefRenderAudit(1)") != std::string::npos);
	REQUIRE(main_c.find("--debug-force-first-person") != std::string::npos);
	REQUIRE(main_c.find("--debug-force-first-person-look") != std::string::npos);
	REQUIRE(main_c.find("--debug-force-first-person-cam-offset") != std::string::npos);
	REQUIRE(main_c.find("bootDebugForceFirstPersonPreRenderTick") != std::string::npos);
	REQUIRE(main_c.find("player0f0b9a20()") != std::string::npos);
	REQUIRE(main_c.find("bgunEquipWeapon2(HAND_RIGHT, queued_weapon)") != std::string::npos);
	REQUIRE(main_c.find("playerSetCameraMode(CAMERAMODE_DEFAULT)") != std::string::npos);
	REQUIRE(main_c.find("playerSetCamProperties(&pos") != std::string::npos);
	REQUIRE(main_c.find("g_BootDebugForceFirstPersonCamOffsetVec") != std::string::npos);
	REQUIRE(main_c.find("--debug-bot-body") != std::string::npos);
	REQUIRE(main_c.find("--debug-bot-head") != std::string::npos);
	REQUIRE(main_c.find("--debug-place-bot-near-player") != std::string::npos);
	REQUIRE(main_c.find("bootValidateDebugBotAsset(\"--debug-bot-body\"") != std::string::npos);
	REQUIRE(main_c.find("matchConfigAddBot(BOTTYPE_GENERAL, BOTDIFF_NORMAL") != std::string::npos);
	REQUIRE(main_c.find("debug_body_id, debug_head_id") != std::string::npos);
	REQUIRE(main_c.find("BOOT: --debug-bot-appearance applied") != std::string::npos);
	REQUIRE(main_c.find("bootDebugPlaceBotNearPlayerPreRenderTick") != std::string::npos);
	REQUIRE(main_c.find("cdFindGroundInfoAtCyl(&candidate, bot->radius, rooms") != std::string::npos);
	REQUIRE(main_c.find("meshFindFloor(&candidate, bot->radius, &meshnormaly)") != std::string::npos);
	REQUIRE(main_c.find("playerground = g_Vars.currentplayer->vv_ground") != std::string::npos);
	REQUIRE(main_c.find("fabsf(selectedground - playerground) <= 80.0f") != std::string::npos);
	REQUIRE(main_c.find("target.y = selectedground") != std::string::npos);
	REQUIRE(main_c.find("selectedsource = \"mesh\"") != std::string::npos);
	REQUIRE(main_c.find("ground_source=%s") != std::string::npos);
	REQUIRE(main_c.find("chrRelocateToFloorWithCachedGround(bot, &target") != std::string::npos);
	REQUIRE(main_c.find("playerprop->pos.y - playerground, rooms") != std::string::npos);
	REQUIRE(main_c.find("chrSetPosWithCachedGround(bot, &target, rooms") == std::string::npos);
	REQUIRE(main_c.find("chrSetPos(bot, &target, rooms") == std::string::npos);
	REQUIRE(main_c.find("chrMoveToPos(bot, &target, rooms") == std::string::npos);
	REQUIRE(main_c.find("struct coord candidate = g_Vars.currentplayer->cam_pos") != std::string::npos);
	REQUIRE(main_c.find("g_BootDebugPlaceBotNearPlayerProp = bot->prop") != std::string::npos);
	REQUIRE(main_c.find("BOT.RENDER.AUDIT: stage=%s") != std::string::npos);
	REQUIRE(main_c.find("k_BootDebugPlaceBotNearPlayerStableFrames") != std::string::npos);
	REQUIRE(main_c.find("--debug-place-bot-near-player-hold-sec") != std::string::npos);
	REQUIRE(main_c.find("g_BootDebugPlaceBotNearPlayerHoldDurationMs") != std::string::npos);
	REQUIRE(main_c.find("g_Vars.tickmode != TICKMODE_NORMAL") != std::string::npos);
	REQUIRE(main_c.find("camera_forward=(%f,%f)") != std::string::npos);

	std::string pdmain_c = readFile("port/src/pdmain.c");
	REQUIRE(pdmain_c.find("bootDebugForceFirstPersonPreRenderTick") != std::string::npos);
	REQUIRE(pdmain_c.find("bootDebugPlaceBotNearPlayerPreRenderTick") == std::string::npos);
	std::string lv_c = readFile("src/game/lv.c");
	const size_t props_tick = lv_c.find("propsTickPlayer(islastplayer)");
	const size_t scenario_tick = lv_c.find("scenarioTickChr(NULL)");
	const size_t place_bot = lv_c.find("bootDebugPlaceBotNearPlayerPreRenderTick");
	const size_t props_sort = lv_c.find("propsSort()");
	REQUIRE(props_tick != std::string::npos);
	REQUIRE(scenario_tick != std::string::npos);
	REQUIRE(place_bot != std::string::npos);
	REQUIRE(props_sort != std::string::npos);
	REQUIRE(props_tick < place_bot);
	REQUIRE(scenario_tick < place_bot);
	REQUIRE(place_bot < props_sort);
	REQUIRE(lv_c.find("g_ClientManifest.num_entries > 0") != std::string::npos);
	REQUIRE(lv_c.find("diff skipped during MP manifest ownership") != std::string::npos);
	const size_t manifest_owned_skip = lv_c.find("g_ClientManifest.num_entries > 0");
	const size_t stage_diff_compute = lv_c.find("catalogComputeStageDiff(stageAssetId");
	REQUIRE(manifest_owned_skip != std::string::npos);
	REQUIRE(stage_diff_compute != std::string::npos);
	REQUIRE(manifest_owned_skip < stage_diff_compute);
	std::string prop_c = readFile("src/game/prop.c");
	REQUIRE(prop_c.find("bootDebugPlaceBotNearPlayerTrace(\"propsSort-included\"") != std::string::npos);
	REQUIRE(prop_c.find("bootDebugPlaceBotNearPlayerTrace(\"propsRender-call\"") != std::string::npos);
	std::string bg_c = readFile("src/game/bg.c");
	REQUIRE(bg_c.find("bootDebugPlaceBotNearPlayerTrace(\"bg-room-assign-main\"") != std::string::npos);
	std::string chr_c = readFile("src/game/chr.c");
	REQUIRE(chr_c.find("bootDebugPlaceBotNearPlayerTrace(\"chrRender-modelRender\"") != std::string::npos);
	REQUIRE(chr_c.find("if (model->matrices == NULL)") != std::string::npos);
	REQUIRE(chr_c.find("bootDebugPlaceBotNearPlayerTrace(\"chrRender-matrix-ondemand\"") != std::string::npos);
	REQUIRE(chr_c.find("modelSetMatricesWithAnim(&renderdata, model)") != std::string::npos);
	REQUIRE(chr_c.find("modAssetCompilerModeldefIsGenerated(model->definition)") != std::string::npos);
	REQUIRE(chr_c.find("chrRender-matrix-ondemand") < chr_c.find("chrRender-modelRender"));

	std::string model_c = readFile("src/lib/model.c");
	REQUIRE(model_c.find("#include \"modasset_compiler.h\"") != std::string::npos);
	REQUIRE(model_c.find("modAssetCompilerTraceGeneratedModeldefRender(model->definition, node)") != std::string::npos);

	std::string weapon_smoke = readFile("tools/smoke-verify/tests/weapon_match_source_gate_smoke.json");
	REQUIRE(weapon_smoke.find("--debug-generated-mesh-render-audit") != std::string::npos);
	REQUIRE(weapon_smoke.find("--debug-force-first-person") != std::string::npos);
	REQUIRE(weapon_smoke.find("MODELDEF\\\\.SOURCE: loaded catalog model source type=23 filenum=843 id=base:model_chrdy357") != std::string::npos);
	REQUIRE(weapon_smoke.find("built hierarchy modeldef 'base:model_taxicab'") != std::string::npos);
	REQUIRE(weapon_smoke.find("nodes=[1-9]\\\\d*.*vertices=[1-9]\\\\d* tris=262") != std::string::npos);
	REQUIRE(weapon_smoke.find("BONDGUN\\\\.SOURCE: loaded catalog model source filenum=890 id=base:model_dy357_hi") != std::string::npos);
	REQUIRE(weapon_smoke.find("built hierarchy modeldef 'base:model_dy357_hi'") != std::string::npos);
	REQUIRE(weapon_smoke.find("tris=404") != std::string::npos);
	REQUIRE(weapon_smoke.find("MODASSET\\\\.RENDER: generated source modeldef rendered id=base:model_dy357_hi") != std::string::npos);
	REQUIRE(weapon_smoke.find("LOG\\\\.WPN\\\\.DIAG: playerRenderHud branch=fp_render cameramode=0") != std::string::npos);
	REQUIRE(weapon_smoke.find("LOG\\\\.WPN\\\\.DIAG: bgunRender enter player=0") != std::string::npos);
	REQUIRE(weapon_smoke.find("gunctrl_wpn=8 switchto=-1") != std::string::npos);

	std::string body_smoke = readFile("tools/smoke-verify/tests/custom_body_live_render_smoke.json");
	REQUIRE(body_smoke.find("examples/modding/typed-pdxxx-basic") != std::string::npos);
	REQUIRE(body_smoke.find("typed_pdxxx_basic_enabled.json") != std::string::npos);
	REQUIRE(body_smoke.find("--debug-generated-mesh-render-audit") != std::string::npos);
	REQUIRE(body_smoke.find("--debug-bot-body") != std::string::npos);
	REQUIRE(body_smoke.find("--debug-bot-head") != std::string::npos);
	REQUIRE(body_smoke.find("--debug-place-bot-near-player") != std::string::npos);
	REQUIRE(body_smoke.find("example:tri_body") != std::string::npos);
	REQUIRE(body_smoke.find("example:tri_head") != std::string::npos);
	REQUIRE(body_smoke.find("ASSETCATALOG\\\\.SCANNER\\\\.BODY\\\\.CUSTOM_SLOT") != std::string::npos);
	REQUIRE(body_smoke.find("ASSETCATALOG\\\\.SCANNER\\\\.HEAD\\\\.CUSTOM_SLOT") != std::string::npos);
	REQUIRE(body_smoke.find("path=.*example_typed_pdxxx_basic/bodies") != std::string::npos);
	REQUIRE(body_smoke.find("path=.*example_typed_pdxxx_basic/heads") != std::string::npos);
	REQUIRE(body_smoke.find("ASSETCATALOG\\\\.SCANNER\\\\.BODY\\\\.LOADER_SLOT") != std::string::npos);
	REQUIRE(body_smoke.find("ASSETCATALOG\\\\.SCANNER\\\\.HEAD\\\\.LOADER_SLOT") != std::string::npos);
	REQUIRE(body_smoke.find("BOOT: --debug-place-bot-near-player consumed") != std::string::npos);
	REQUIRE(body_smoke.find("BOT\\\\.RENDER\\\\.AUDIT: stage=propsSort-included") != std::string::npos);
	REQUIRE(body_smoke.find("BOT\\\\.RENDER\\\\.AUDIT: stage=chrRender-matrix-ondemand") != std::string::npos);
	REQUIRE(body_smoke.find("BOT\\\\.RENDER\\\\.AUDIT: stage=chrRender-modelRender") != std::string::npos);
	REQUIRE(body_smoke.find("MATCHSETUP: bot slot \\\\d+: .* body='example:tri_body'") != std::string::npos);
	REQUIRE(body_smoke.find("MODELDEF\\\\.SOURCE: loaded catalog model source .*id=example:tri_body") ==
	        std::string::npos);
	REQUIRE(body_smoke.find("MODASSET\\\\.RENDER: generated source modeldef rendered id=example:tri_body") != std::string::npos);
	REQUIRE(body_smoke.find("LOUDFAIL\\\\.FALLBACK") != std::string::npos);
	REQUIRE(body_smoke.find("ASSET\\\\.FALLBACK") != std::string::npos);
	REQUIRE(body_smoke.find("CATALOG: stage 0x[0-9a-fA-F]+ diff .*unload:[1-9]") != std::string::npos);
	REQUIRE(body_smoke.find("MANIFEST: unload 'example:tri_body'") != std::string::npos);
	REQUIRE(body_smoke.find("MANIFEST: unload 'example:tri_head'") != std::string::npos);
	REQUIRE(body_smoke.find("MANIFEST: unload 'example:tri_weapon'") != std::string::npos);

	std::string workflow_smoke = readFile("tools/smoke-verify/tests/pdxxx_modder_workflow_smoke.json");
	REQUIRE(workflow_smoke.find("examples/modding/typed-pdxxx-basic") != std::string::npos);
	REQUIRE(workflow_smoke.find("\"packed_fixtures\"") != std::string::npos);
	REQUIRE(workflow_smoke.find("tools/smoke-verify/fixtures/all_family_source_pd.ini") != std::string::npos);
	REQUIRE(workflow_smoke.find("mods/installed/example_typed_pdxxx_basic.pdmod") != std::string::npos);
	REQUIRE(workflow_smoke.find("typed_pdxxx_basic_enabled.json") != std::string::npos);
	REQUIRE(workflow_smoke.find("--debug-exit-after-catalog-probes") != std::string::npos);
	REQUIRE(workflow_smoke.find("--debug-load-catalog-assets-source-only") != std::string::npos);
	REQUIRE(workflow_smoke.find("--debug-play-catalog-audio-source-only") != std::string::npos);
	REQUIRE(workflow_smoke.find("model=example:tri_mesh") != std::string::npos);
	REQUIRE(workflow_smoke.find("texture=example:tri_texture") != std::string::npos);
	REQUIRE(workflow_smoke.find("material=example:tri_material") != std::string::npos);
	REQUIRE(workflow_smoke.find("animation=example:character_skeletal") != std::string::npos);
	REQUIRE(workflow_smoke.find("arena=example:tri_arena") != std::string::npos);
	REQUIRE(workflow_smoke.find("scenario=example:tri_scenario") != std::string::npos);
	REQUIRE(workflow_smoke.find("mission=example:tri_mission") != std::string::npos);
	REQUIRE(workflow_smoke.find("weapon=example:tri_weapon") != std::string::npos);
	REQUIRE(workflow_smoke.find("sfx=example:tri_click") != std::string::npos);
	REQUIRE(workflow_smoke.find("voice=example:tri_voice") != std::string::npos);
	REQUIRE(workflow_smoke.find("song=example:tri_song") != std::string::npos);
	REQUIRE(workflow_smoke.find("modmgr: parsed mod\\\\.json for 'example_typed_pdxxx_basic'.*archive=yes") != std::string::npos);
	REQUIRE(workflow_smoke.find("modmgr: loading mod 'example_typed_pdxxx_basic' from archive") != std::string::npos);
	REQUIRE(workflow_smoke.find("assetcatalog_scanner: archive example_typed_pdxxx_basic") != std::string::npos);
	REQUIRE(workflow_smoke.find("BOOT: --debug-load-catalog-assets result type=model id='example:tri_mesh' result=OK") != std::string::npos);
	REQUIRE(workflow_smoke.find("BOOT: --debug-load-catalog-assets result type=texture id='example:tri_texture' result=OK") != std::string::npos);
	REQUIRE(workflow_smoke.find("BOOT: --debug-load-catalog-assets result type=material id='example:tri_material' result=OK") != std::string::npos);
	REQUIRE(workflow_smoke.find("BOOT: --debug-load-catalog-assets result type=animation id='example:character_skeletal' result=OK") != std::string::npos);
	REQUIRE(workflow_smoke.find("BOOT: --debug-load-catalog-assets result type=arena id='example:tri_arena' result=OK") != std::string::npos);
	REQUIRE(workflow_smoke.find("BOOT: --debug-load-catalog-assets result type=scenario id='example:tri_scenario' result=OK") != std::string::npos);
	REQUIRE(workflow_smoke.find("BOOT: --debug-load-catalog-assets result type=mission id='example:tri_mission' result=OK") != std::string::npos);
	REQUIRE(workflow_smoke.find("BOOT: --debug-load-catalog-assets result type=weapon id='example:tri_weapon' result=OK") != std::string::npos);
	REQUIRE(workflow_smoke.find("BOOT: --debug-load-catalog-assets result type=audio id='example:tri_click' result=OK") != std::string::npos);
	REQUIRE(workflow_smoke.find("BOOT: --debug-load-catalog-assets result type=audio id='example:tri_voice' result=OK") != std::string::npos);
	REQUIRE(workflow_smoke.find("BOOT: --debug-load-catalog-assets result type=audio id='example:tri_song' result=OK") != std::string::npos);
	REQUIRE(workflow_smoke.find("BOOT: --debug-play-catalog-audio result kind=sfx id='example:tri_click' result=OK") != std::string::npos);
	REQUIRE(workflow_smoke.find("BOOT: --debug-play-catalog-audio result kind=voice id='example:tri_voice' result=OK") != std::string::npos);
	REQUIRE(workflow_smoke.find("BOOT: --debug-play-catalog-audio result kind=song id='example:tri_song' result=OK") != std::string::npos);
	REQUIRE(workflow_smoke.find("ASSET\\\\.FALLBACK") != std::string::npos);
	REQUIRE(workflow_smoke.find("LOUDFAIL\\\\.FALLBACK") != std::string::npos);
	REQUIRE(workflow_smoke.find("RomProvider:filenum") != std::string::npos);
	REQUIRE(workflow_smoke.find("authored \\\\.bin") != std::string::npos);
	REQUIRE(workflow_smoke.find("public \\\\.tsv") != std::string::npos);

	std::string mod = readFile("port/src/mod.c");
	REQUIRE(mod.find("modAnimationLoadCatalogClip") != std::string::npos);
	REQUIRE(mod.find("catalogGetLoadedAnimationClip(entry->id") != std::string::npos);
	REQUIRE(mod.find("modAnimationFatalPublicSourceFailure") != std::string::npos);
	REQUIRE(mod.find("refusing ROM/static fallback") != std::string::npos);
	REQUIRE(mod.find("External animation %04x failed to compile") == std::string::npos);

	std::string weapon_archive = readFile("port/src/weapon_graph_archive.c");
	REQUIRE(weapon_archive.find("#include \"modvfs.h\"") != std::string::npos);
	REQUIRE(weapon_archive.find("modVfsResolveAnyAlloc(archive_path") != std::string::npos);
	REQUIRE(weapon_archive.find("weaponGraphArchiveReadBytesFile") != std::string::npos);
	REQUIRE(weapon_archive.find("modArchiveExtractMemAlloc(archive_bytes") != std::string::npos);

	std::string catalog_load = readFile("port/src/assetcatalog_load.c");
	REQUIRE(catalog_load.find("weaponGraphRuntimeRegisterWeaponArchive(entry->runtime_index,\n            archive_path") != std::string::npos);
	REQUIRE(catalog_load.find("!fsPathIsAbsolute(archive_path)") != std::string::npos);

	std::string catalog_h = readFile("port/include/assetcatalog.h");
	REQUIRE(catalog_h.find("ASSET_PAYLOAD_COLMESH") != std::string::npos);
	REQUIRE(catalog_h.find("ASSET_PAYLOAD_ANIMATION_CLIP") != std::string::npos);

	std::string lv = readFile("src/game/lv.c");
	REQUIRE(lv.find("lvAddLoadedCatalogColmeshes") != std::string::npos);
	REQUIRE(lv.find("meshWorldAddMesh(mesh, NULL)") != std::string::npos);

	std::string resolve = readFile("port/src/assetcatalog_resolve.c");
	REQUIRE(resolve.find("entry->type == ASSET_SCENARIO && entry->ext.scenario.stagenum") != std::string::npos);
	REQUIRE(resolve.find("assetCatalogIterateByType(ASSET_SCENARIO, findMapCb, &ctx)") != std::string::npos);
	REQUIRE(resolve.find("assetCatalogIterateByType(ASSET_ARENA, findMapCb, &ctx)") != std::string::npos);

	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("scenario.ini - external scenario source metadata") != std::string::npos);
	REQUIRE(scanner.find("scene_file = scene.glb") != std::string::npos);
	REQUIRE(scanner.find("\"rooms_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"scene_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"runtime_source_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"rooms_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"scene_file\"") != std::string::npos);
	REQUIRE(scanner.find("e->source_animnum = -1") != std::string::npos);
	REQUIRE(scanner.find("e->source_animnum = slot") != std::string::npos);
	REQUIRE(scanner.find("#include \"loader_pool.h\"") != std::string::npos);
	REQUIRE(scanner.find("registerAnimationCommandSource(e->id, cf, batch)") != std::string::npos);
	REQUIRE(scanner.find("loaderAnimationSourceBatchStage(batch, json, json_size, source_path, id)") != std::string::npos);
	REQUIRE(scanner.find("loaderAnimationSourceBatchCommit(batch)") != std::string::npos);

	std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("\"head.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"body.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"arena.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"scenario.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"animation.ini\"") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_ANIMATION:") != std::string::npos);
	REQUIRE(distrib.find("#include \"loader_pool.h\"") != std::string::npos);
	REQUIRE(distrib.find("if (!distribRegisterAnimationCommandSource(e->id, dirpath,") != std::string::npos);
	REQUIRE(distrib.find("cf)) return 0;") != std::string::npos);
	REQUIRE(distrib.find("loaderAnimationSourceBatchStage(batch, json, json_size, path, id)") != std::string::npos);
	REQUIRE(distrib.find("e->ext.anim.bytes_per_frame = iniGetInt(ini, \"bytes_per_frame\", 0)") != std::string::npos);
	REQUIRE(distrib.find("e->ext.anim.header_len = iniGetInt(ini, \"header_len\", 0)") != std::string::npos);
	REQUIRE(distrib.find("e->ext.anim.framelen = iniGetInt(ini, \"framelen\", 0)") != std::string::npos);
	REQUIRE(distrib.find("e->ext.anim.flags = iniGetInt(ini, \"flags\", 0)") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_SCENARIO:") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_GAMEMODE:") != std::string::npos);
}

TEST_CASE("B-1102 schema-v5 animation framing is bounded and terminal",
		"[modding][animation][static][B-1102]")
{
	const std::string extractor = readFile("port/src/romextract_pdanim_chr.c");
	const std::string compiler = readFile("port/src/modasset_compiler.c");
	const std::string walker = readFile("port/src/loader_walker_anim.c");
	const std::string special = sourceFunctionBlock(compiler,
		"static s32 gltfBuildNativeFromSpecialParts");
	const std::string payload = sourceFunctionBlock(compiler,
		"static s32 gltfBuildAnimationClipPayload");
	const std::string parser = sourceFunctionBlock(compiler,
		"static s32 parseGltfAnimationSpecialParts");
	const std::string native_extras = sourceFunctionBlock(compiler,
		"static s32 parseGltfAnimationNativeExtras");
	const std::string stream_validator = sourceFunctionBlock(compiler,
		"static s32 animDescriptorStreamMatches");
	const std::string build = sourceFunctionBlock(compiler,
		"s32 modAssetCompilerBuildAnimationClipHashed");
	const std::string rotation_source = sourceFunctionBlock(walker,
		"static s32 s_readRotationBits");

	REQUIRE(rotation_source.find("\"animation.ini\"") != std::string::npos);
	REQUIRE(rotation_source.find("iniGet(ini, \"framelen\", NULL)") != std::string::npos);
	REQUIRE(walker.find("e->ext.anim.framelen = rotation_bits") != std::string::npos);
	REQUIRE(build.find("entry ? entry->ext.anim.framelen : 0") != std::string::npos);
	REQUIRE(payload.find("gltfBuildNativeFromSpecialParts(clip, frame_count, rotation_bits") !=
		std::string::npos);
	REQUIRE(special.find("rotation_bits > 16") != std::string::npos);
	REQUIRE(special.find("out_entry->framelen = rotation_bits ? (u8)rotation_bits : 16") !=
		std::string::npos);
	REQUIRE(build.find("(u8)frame_count, out_entry->framelen}") != std::string::npos);

	/* The public schema is ordered framing, not two independent optional
	 * properties: every header_hex descriptor is preceded by its flags byte. */
	REQUIRE(extractor.find("\\\"flags\\\": %u, \\\"header_hex\\\": \\\"") !=
		std::string::npos);
	REQUIRE(compiler.find("#include \"lib/anim_bits.h\"") !=
		std::string::npos);
	const size_t parsed_flags = parser.find(
		"jsonObjectInt(part_obj, \"flags\"");
	const size_t parsed_header = parser.find(
		"jsonObjectStringAlloc(part_obj, \"header_hex\"");
	REQUIRE(parsed_flags != std::string::npos);
	REQUIRE(parsed_header != std::string::npos);
	REQUIRE(parsed_flags < parsed_header);
	REQUIRE(parser.find("animFrameMeasurePartBounded") !=
		std::string::npos);
	REQUIRE(parser.find("GLTF_ANIM_SPECIAL_PARTS_SCHEMA_VERSION") !=
		std::string::npos);
	REQUIRE(parser.find("jsonArrayNextContainerStrict") !=
		std::string::npos);
	REQUIRE(parser.find("jsonArrayNextU32Strict") !=
		std::string::npos);
	REQUIRE(compiler.find("modAssetJsonParseS32Token") !=
		std::string::npos);
	REQUIRE(compiler.find("modAssetJsonParseBoolToken") !=
		std::string::npos);
	REQUIRE(compiler.find("modAssetJsonParseU32Token") !=
		std::string::npos);
	REQUIRE(parser.find("clip->part_count = count") != std::string::npos);
	REQUIRE(native_extras.find("pd_part_count") != std::string::npos);
	REQUIRE(native_extras.find("clip->declared_part_count = value") !=
		std::string::npos);
	REQUIRE(native_extras.find("pd_zero_frame_placeholder") !=
		std::string::npos);
	REQUIRE(native_extras.find("gltf_animation_special_frame_count_invalid") !=
		std::string::npos);

	/* Native reconstruction must share the bounded part and complete-stream
	 * measurements. The flags byte is part of the reserved descriptor span and
	 * is written before the verbatim header bytes. */
	REQUIRE(special.find("animFrameMeasurePartBounded") !=
		std::string::npos);
	REQUIRE(stream_validator.find(
		"animFrameMeasureDescriptorStreamBounded") !=
		std::string::npos);
	REQUIRE(special.find("descriptor_header_len += 1 + sp->header_len") !=
		std::string::npos);
	size_t writes_flags = special.find("*p++ = sp->flags");
	if (writes_flags == std::string::npos) {
		writes_flags = special.find("*p++ = (u8)sp->flags");
	}
	if (writes_flags == std::string::npos) {
		writes_flags = special.find("*p = sp->flags");
	}
	if (writes_flags == std::string::npos) {
		writes_flags = special.find("memcpy(p, &sp->flags");
	}
	REQUIRE(writes_flags != std::string::npos);
	const size_t writes_descriptor = special.find("memcpy(p, sp->header");
	const size_t validates_descriptor = special.find(
		"animDescriptorStreamMatches");
	const size_t publishes_payload = special.find("*out_data = data");
	REQUIRE(writes_descriptor != std::string::npos);
	REQUIRE(validates_descriptor != std::string::npos);
	REQUIRE(publishes_payload != std::string::npos);
	REQUIRE(writes_flags < writes_descriptor);
	REQUIRE(writes_descriptor < validates_descriptor);
	REQUIRE(validates_descriptor < publishes_payload);
	REQUIRE(stream_validator.find("ANIM_FRAME_LAYOUT_OK") !=
		std::string::npos);

	/* Once schema-v5 ownership is present, malformed native reconstruction is a
	 * terminal source error. The caller may not silently continue into the
	 * lossy GLTF transform-channel rebuild. */
	REQUIRE(payload.find("return gltfBuildNativeFromSpecialParts") !=
		std::string::npos);
	REQUIRE(payload.find("if (gltfBuildNativeFromSpecialParts") ==
		std::string::npos);
}

TEST_CASE("packed pdmod transport fixture resolves typed pdxxx content through production VFS",
          "[modding][pdmod][pdxxx][runtime][static][c3809][c3838][files]") {
	TempArchive temp = packArchiveFixture();
	OpenArchive opened;
	opened.archive = modArchiveOpen(temp.path.string().c_str());
	REQUIRE(opened.archive != nullptr);

	u32 manifestSize = 0;
	char *manifest = modArchiveReadManifest(opened.archive, &manifestSize);
	REQUIRE(manifest != nullptr);
	std::string manifestText(manifest, manifestSize);
	free(manifest);
	REQUIRE(manifestText.find("\"id\": \"fixture_pdxxx_content\"") != std::string::npos);

	const char *requiredEntries[] = {
		"mod.json",
		"heads/tri_head.pdhead",
		"arenas/tri_arena.pdarena",
		"animations/weapon_idle.pdanim",
		"animations/character_skeletal.pdanim",
	};

	for (const char *entry : requiredEntries) {
		INFO(entry);
		REQUIRE(modArchiveFindEntry(opened.archive, entry) >= 0);
	}

	for (s32 i = 0; i < modArchiveGetEntryCount(opened.archive); i++) {
		const char *entry = modArchiveGetEntryName(opened.archive, i);
		REQUIRE(entry != nullptr);
		REQUIRE(std::string(entry).find(".bin") == std::string::npos);
	}

	modVfsInit();
	MountedArchive mount{ "fixture_pdxxx_content", false };
	REQUIRE(modVfsMount(mount.modId.c_str(), opened.archive) == 1);
	mount.mounted = true;

	for (const char *entry : requiredEntries) {
		INFO(entry);
		REQUIRE(modVfsCanResolve(entry) == 1);
		REQUIRE(modVfsGetSize(entry) > 0);
	}
	REQUIRE(modVfsCanResolve("heads/tri_head.pdhead::head.ini") == 1);
	REQUIRE(modVfsGetSize("heads/tri_head.pdhead::head.ini") > 0);
	REQUIRE(modVfsCanResolve("arenas/tri_arena.pdarena::geometry.obj") == 1);
	REQUIRE(modVfsGetSize("arenas/tri_arena.pdarena::geometry.obj") > 0);

	const std::string arena =
		readVfsText("arenas/tri_arena.pdarena::arena.ini");
	const std::string geometry =
		readVfsText("arenas/tri_arena.pdarena::geometry.obj");
	const std::string pads =
		readVfsText("arenas/tri_arena.pdarena::pads.ini");
	const std::string head =
		readVfsText("heads/tri_head.pdhead::head.ini");
	const std::string model =
		readVfsText("heads/tri_head.pdhead::model.gltf");
	const std::string weaponAnim =
		readVfsText("animations/weapon_idle.pdanim::animation.ini");
	const std::string weaponGltf =
		readVfsText("animations/weapon_idle.pdanim::animation.gltf");
	const std::string skeletalAnim =
		readVfsText("animations/character_skeletal.pdanim::animation.ini");
	const std::string skeletalGltf =
		readVfsText("animations/character_skeletal.pdanim::animation.gltf");

	REQUIRE(arena.find("geometry_file = geometry.obj") != std::string::npos);
	REQUIRE((geometry.find("f 1 2 3") != std::string::npos
		|| geometry.find("f 1/1 2/2 3/3") != std::string::npos));
	REQUIRE(pads.find("default_spawn = 0,0,0") != std::string::npos);
	REQUIRE(head.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(model.find("data:application/octet-stream;base64,") != std::string::npos);
	REQUIRE(weaponAnim.find("catalog_id = fixture:weapon_idle") != std::string::npos);
	REQUIRE(weaponGltf.find("\"animations\"") != std::string::npos);
	REQUIRE(skeletalAnim.find("catalog_id = fixture:character_skeletal") != std::string::npos);
	REQUIRE(skeletalGltf.find("\"path\": \"translation\"") != std::string::npos);

	mod_vfs_stats_t stats;
	modVfsGetStats(&stats);
	REQUIRE(stats.mounts_active == 1);
	REQUIRE(stats.entries_resident >= 4);
}

TEST_CASE("typed pdxxx archive entries stay reachable through fs and FileProvider loaders",
          "[modding][pdxxx][runtime][static][c3838][files]") {
	const std::string fs = readFile("port/src/fs.c");
	REQUIRE(fs.find("fsLoadNestedArchiveEntry(name, outSize)") != std::string::npos);
	REQUIRE(fs.find("modVfsResolveAnyAlloc(archiveName, &archiveSize, NULL, 0)") != std::string::npos);
	REQUIRE(fs.find("modVfsResolveAnyAlloc(name, &vfsSize, NULL, 0)") != std::string::npos);
	REQUIRE(fs.find("modVfsGetSize(name)") != std::string::npos);

	const std::string provider = readFile("port/src/assetprovider_file.c");
	REQUIRE(provider.find("fsFileSize(path)") != std::string::npos);
	REQUIRE(provider.find("fsFileLoad(path, &size)") != std::string::npos);

	const std::string dispatcher = readFile("port/src/assetload.c");
	REQUIRE(dispatcher.find("return handle.provider->load(handle.provider, handle, buf, buf_size)") != std::string::npos);
}

TEST_CASE("modder examples are zip-openable typed pdxxx asset archives",
          "[modding][pdxxx][examples][static][c3811][c3812]") {
	const std::filesystem::path root =
		"examples/modding/typed-pdxxx-basic";
	REQUIRE(std::filesystem::is_directory(root));

	const std::string index = readFile("examples/modding/README.md");
	REQUIRE(index.find("typed `*.pdxxx` asset archives") != std::string::npos);
	REQUIRE(index.find("`.pdmod` is only the transport wrapper") != std::string::npos);

	const std::string readme =
		readFile("examples/modding/typed-pdxxx-basic/README.md");
	REQUIRE(readme.find("The content units are the typed `*.pdxxx` asset archives") != std::string::npos);
	REQUIRE(readme.find("Change any `.pdxxx` extension to `.zip`") != std::string::npos);
	REQUIRE(readme.find("The GLTF, OBJ, INI, JSON, and source media files inside each archive are the authored data") != std::string::npos);
	REQUIRE(readme.find("Machine-owned manifest and provenance data lives under `_meta/`") != std::string::npos);
	REQUIRE(readme.find("`.pdmod` is not the authoring format") != std::string::npos);

	const char *requiredFiles[] = {
		"mod.json",
		"characters/tri_character.pdcharacter",
		"heads/tri_head.pdhead",
		"bodies/tri_body.pdbody",
		"arenas/tri_arena.pdarena",
		"meshes/tri_mesh.pdmesh",
		"materials/tri_material.pdmaterial",
		"textures/tri_texture.pdtexture",
		"skins/tri_skin.pdskin",
		"effects/tri_effect.pdeffect",
		"props/tri_prop.pdprop",
		"vehicles/tri_vehicle.pdvehicle",
		"missions/tri_mission.pdmission",
		"gamemodes/tri_gamemode.pdgamemode",
		"botprofiles/tri_botprofile.pdbotprofile",
		"hud/tri_hud.pdhud",
		"themes/tri_theme.pdtheme",
		"entities/tri_entity.pdentity",
		"projectiles/tri_projectile.pdprojectile",
		"weapons/tri_weapon.pdweapon",
		"animations/weapon_idle.pdanim",
		"animations/character_skeletal.pdanim",
		"audio/sfx/tri_click.pdsfx",
		"audio/voice/tri_voice.pdvoice",
		"audio/music/tri_song.pdsong",
		"ui/tri_reticle.pdui",
		"fonts/tri_font.pdfont",
		"lang/tri_lang.pdlang",
		"scenarios/tri_scenario.pdscenario",
	};

	for (const char *rel : requiredFiles) {
		const std::filesystem::path path = root / rel;
		INFO(path.generic_string());
		REQUIRE(std::filesystem::is_regular_file(path));
		const std::string pathText = path.generic_string();
		REQUIRE_FALSE(readFile(pathText.c_str()).empty());
	}

	for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
		if (!entry.is_regular_file()) {
			continue;
		}
		const std::string rel =
			std::filesystem::relative(entry.path(), root).generic_string();
		INFO(rel);
		REQUIRE(rel.find(".pdmod") == std::string::npos);
		REQUIRE(rel.find(".bin") == std::string::npos);
	}

	const std::string manifest =
		readFile("examples/modding/typed-pdxxx-basic/mod.json");
	REQUIRE(manifest.find("\"id\": \"example_typed_pdxxx_basic\"") != std::string::npos);
	REQUIRE(manifest.find("Wrap as .pdmod only") != std::string::npos);

	const std::string characterArchivePath =
		"examples/modding/typed-pdxxx-basic/characters/tri_character.pdcharacter";
	const std::string headArchivePath =
		"examples/modding/typed-pdxxx-basic/heads/tri_head.pdhead";
	const std::string bodyArchivePath =
		"examples/modding/typed-pdxxx-basic/bodies/tri_body.pdbody";
	const std::string arenaArchivePath =
		"examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena";
	const std::string scenarioArchivePath =
		"examples/modding/typed-pdxxx-basic/scenarios/tri_scenario.pdscenario";
	const std::string meshArchivePath =
		"examples/modding/typed-pdxxx-basic/meshes/tri_mesh.pdmesh";
	const std::string materialArchivePath =
		"examples/modding/typed-pdxxx-basic/materials/tri_material.pdmaterial";
	const std::string textureArchivePath =
		"examples/modding/typed-pdxxx-basic/textures/tri_texture.pdtexture";
	const std::string skinArchivePath =
		"examples/modding/typed-pdxxx-basic/skins/tri_skin.pdskin";
	const std::string effectArchivePath =
		"examples/modding/typed-pdxxx-basic/effects/tri_effect.pdeffect";
	const std::string propArchivePath =
		"examples/modding/typed-pdxxx-basic/props/tri_prop.pdprop";
	const std::string vehicleArchivePath =
		"examples/modding/typed-pdxxx-basic/vehicles/tri_vehicle.pdvehicle";
	const std::string missionArchivePath =
		"examples/modding/typed-pdxxx-basic/missions/tri_mission.pdmission";
	const std::string gamemodeArchivePath =
		"examples/modding/typed-pdxxx-basic/gamemodes/tri_gamemode.pdgamemode";
	const std::string botprofileArchivePath =
		"examples/modding/typed-pdxxx-basic/botprofiles/tri_botprofile.pdbotprofile";
	const std::string hudArchivePath =
		"examples/modding/typed-pdxxx-basic/hud/tri_hud.pdhud";
	const std::string themeArchivePath =
		"examples/modding/typed-pdxxx-basic/themes/tri_theme.pdtheme";
	const std::string entityArchivePath =
		"examples/modding/typed-pdxxx-basic/entities/tri_entity.pdentity";
	const std::string projectileArchivePath =
		"examples/modding/typed-pdxxx-basic/projectiles/tri_projectile.pdprojectile";
	const std::string weaponArchiveFullPath =
		"examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon";
	const std::string weaponArchivePath =
		"examples/modding/typed-pdxxx-basic/animations/weapon_idle.pdanim";
	const std::string skeletalArchivePath =
		"examples/modding/typed-pdxxx-basic/animations/character_skeletal.pdanim";

	const std::string character = readArchiveEntryText(characterArchivePath.c_str(), "character.ini");
	REQUIRE(character.find("catalog_id = example:tri_character") != std::string::npos);
	REQUIRE(character.find("body_archive = dependencies/assets/body/tri_body.pdbody") != std::string::npos);
	REQUIRE(character.find("head_archive = dependencies/assets/head/tri_head.pdhead") != std::string::npos);
	REQUIRE(character.find("portrait_file = portrait.png") != std::string::npos);
	const std::string characterManifest = readArchiveEntryText(characterArchivePath.c_str(), "_meta/manifest.json");
	REQUIRE(characterManifest.find("\"body_archive\": \"dependencies/assets/body/tri_body.pdbody\"") != std::string::npos);
	REQUIRE(characterManifest.find("\"head_archive\": \"dependencies/assets/head/tri_head.pdhead\"") != std::string::npos);
	REQUIRE(characterManifest.find("\"portrait_file\": \"portrait.png\"") != std::string::npos);

	const std::string head = readArchiveEntryText(headArchivePath.c_str(), "head.ini");
	REQUIRE(head.find("catalog_id = example:tri_head") != std::string::npos);
	REQUIRE(head.find("mesh_archive = mesh.pdmesh") != std::string::npos);
	{
		OpenArchive opened;
		opened.archive = modArchiveOpen(headArchivePath.c_str());
		REQUIRE(opened.archive != nullptr);
		REQUIRE(archiveHasEntry(opened.archive, "mesh.pdmesh"));
		REQUIRE_FALSE(archiveHasEntry(opened.archive, "model.gltf"));
		REQUIRE(archiveNestedHasEntry(opened.archive, "mesh.pdmesh",
			"model.gltf"));
		REQUIRE(archiveNestedHasEntry(opened.archive, "mesh.pdmesh",
			"model.mtl"));
		REQUIRE(archiveNestedHasEntry(opened.archive, "mesh.pdmesh",
			"model.nodes.json"));
		REQUIRE(archiveNestedHasEntry(opened.archive, "mesh.pdmesh",
			"model.parts.json"));
		REQUIRE(archiveNestedHasEntry(opened.archive, "mesh.pdmesh",
			"model.faces.json"));
		REQUIRE(archiveNestedHasEntry(opened.archive, "mesh.pdmesh",
			"model.render.json"));
	}

	const std::string body = readArchiveEntryText(bodyArchivePath.c_str(), "body.ini");
	REQUIRE(body.find("catalog_id = example:tri_body") != std::string::npos);
	REQUIRE(body.find("rig_class = human_male_neck_standard") != std::string::npos);
	REQUIRE(body.find("mesh_archive = mesh.pdmesh") != std::string::npos);
	REQUIRE(body.find("hand_archive = hand.pdmesh") != std::string::npos);
	{
		OpenArchive opened;
		opened.archive = modArchiveOpen(bodyArchivePath.c_str());
		REQUIRE(opened.archive != nullptr);
		REQUIRE(archiveHasEntry(opened.archive, "mesh.pdmesh"));
		REQUIRE(archiveHasEntry(opened.archive, "hand.pdmesh"));
		REQUIRE_FALSE(archiveHasEntry(opened.archive, "model.gltf"));
		REQUIRE_FALSE(archiveHasEntry(opened.archive, "hand.gltf"));
		const char *nestedMeshes[] = { "mesh.pdmesh", "hand.pdmesh" };
		for (const char *nested : nestedMeshes) {
			REQUIRE(archiveNestedHasEntry(opened.archive, nested,
				"model.gltf"));
			REQUIRE(archiveNestedHasEntry(opened.archive, nested,
				"model.mtl"));
			REQUIRE(archiveNestedHasEntry(opened.archive, nested,
				"model.nodes.json"));
			REQUIRE(archiveNestedHasEntry(opened.archive, nested,
				"model.parts.json"));
			REQUIRE(archiveNestedHasEntry(opened.archive, nested,
				"model.faces.json"));
			REQUIRE(archiveNestedHasEntry(opened.archive, nested,
				"model.render.json"));
			const std::string meshIni =
				readNestedArchiveEntryText(opened.archive, nested, "mesh.ini");
			const std::string manifest =
				readNestedArchiveEntryText(opened.archive, nested,
					"_meta/manifest.json");
			REQUIRE(meshIni.find("model_scale = 1000") != std::string::npos);
			REQUIRE(manifest.find("\"model_scale\": 1000") != std::string::npos);
		}
	}

	const std::string arena = readArchiveEntryText(arenaArchivePath.c_str(), "arena.ini");
	REQUIRE(arena.find("catalog_id = example:tri_arena") != std::string::npos);
	REQUIRE(arena.find("scenario = example:tri_scenario") != std::string::npos);
	REQUIRE(arena.find("scenario_archive = dependencies/assets/scenarios/tri_scenario.pdscenario") != std::string::npos);
	REQUIRE(arena.find("geometry_file") == std::string::npos);
	{
		OpenArchive opened;
		opened.archive = modArchiveOpen(arenaArchivePath.c_str());
		REQUIRE(opened.archive != nullptr);
		REQUIRE(archiveHasEntry(opened.archive,
			"dependencies/assets/scenarios/tri_scenario.pdscenario"));
		REQUIRE_FALSE(archiveHasEntry(opened.archive, "geometry.obj"));
	}

	const std::string scenario = readArchiveEntryText(scenarioArchivePath.c_str(), "scenario.ini");
	const std::string scenarioManifest =
		readArchiveEntryText(scenarioArchivePath.c_str(), "_meta/manifest.json");
	REQUIRE(scenario.find("runtime_source_file = scene.glb") != std::string::npos);
	REQUIRE(scenarioManifest.find("\"runtime_source\": \"scene.glb\"") != std::string::npos);
	REQUIRE(scenario.find("portals_file = portals.json") != std::string::npos);
	REQUIRE(scenarioManifest.find("\"portals\": \"portals.json\"") != std::string::npos);
	REQUIRE(scenario.find("ai_lists_file = ai/ailists.json") != std::string::npos);
	REQUIRE(scenarioManifest.find("\"ai_lists\": \"ai/ailists.json\"") != std::string::npos);
	REQUIRE(scenario.find("objectives_file = objectives.json") != std::string::npos);
	REQUIRE(scenarioManifest.find("\"objectives\": \"objectives.json\"") != std::string::npos);
	REQUIRE(scenario.find("navigation_file = navigation.ini") != std::string::npos);
	REQUIRE(scenarioManifest.find("\"navigation\": \"navigation.ini\"") != std::string::npos);
	REQUIRE(scenario.find("level_graph_file = level.graph.json") != std::string::npos);
	REQUIRE(scenarioManifest.find("\"level_graph\": \"level.graph.json\"") != std::string::npos);

	const std::string mesh = readArchiveEntryText(meshArchivePath.c_str(), "mesh.ini");
	const std::string meshModel = readArchiveEntryText(meshArchivePath.c_str(), "model.gltf");
	const std::string meshRender =
		readArchiveEntryText(meshArchivePath.c_str(), "model.render.json");
	const std::string meshManifest =
		readArchiveEntryText(meshArchivePath.c_str(), "_meta/manifest.json");
	REQUIRE(mesh.find("catalog_id = example:tri_mesh") != std::string::npos);
	REQUIRE(mesh.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(mesh.find("material_file = model.mtl") != std::string::npos);
	REQUIRE(mesh.find("hierarchy_file = model.nodes.json") != std::string::npos);
	REQUIRE(mesh.find("parts_file = model.parts.json") != std::string::npos);
	REQUIRE(mesh.find("faces_file = model.faces.json") != std::string::npos);
	REQUIRE(mesh.find("render_stream_file = model.render.json") != std::string::npos);
	REQUIRE(meshRender.find("\"pd_schema_version\": 1") != std::string::npos);
	REQUIRE(meshManifest.find("\"geometry\": \"model.gltf\"") != std::string::npos);
	REQUIRE(meshManifest.find("\"model_file\": \"model.gltf\"") != std::string::npos);
	REQUIRE(meshManifest.find("\"material_file\": \"model.mtl\"") != std::string::npos);
	REQUIRE(meshManifest.find("\"hierarchy_file\": \"model.nodes.json\"") != std::string::npos);
	REQUIRE(meshManifest.find("\"parts_file\": \"model.parts.json\"") != std::string::npos);
	REQUIRE(meshManifest.find("\"faces_file\": \"model.faces.json\"") != std::string::npos);
	REQUIRE(meshManifest.find("\"render_stream_file\": \"model.render.json\"") != std::string::npos);
	REQUIRE(meshModel.find("\"TEXCOORD_0\"") != std::string::npos);
	REQUIRE(meshModel.find("\"baseColorFactor\"") != std::string::npos);

	const std::string material = readArchiveEntryText(materialArchivePath.c_str(), "material.ini");
	const std::string materialSource =
		readArchiveEntryText(materialArchivePath.c_str(), "material.json");
	const std::string materialManifest =
		readArchiveEntryText(materialArchivePath.c_str(), "_meta/manifest.json");
	REQUIRE(material.find("catalog_id = example:tri_material") != std::string::npos);
	REQUIRE(material.find("material_file = material.json") != std::string::npos);
	REQUIRE(material.find("texture_archive = dependencies/assets/texture/tri_texture.pdtexture") != std::string::npos);
	REQUIRE(material.find("effect_archive = dependencies/assets/effects/tri_effect.pdeffect") != std::string::npos);
	REQUIRE(materialSource.find("\"schema\": \"pd2.material.v1\"") != std::string::npos);
	REQUIRE(materialSource.find("\"texture_slots\"") != std::string::npos);
	REQUIRE(materialManifest.find("\"material_file\": \"material.json\"") != std::string::npos);
	REQUIRE(materialManifest.find("\"texture_archive\": \"dependencies/assets/texture/tri_texture.pdtexture\"") != std::string::npos);
	REQUIRE(materialManifest.find("\"effect_archive\": \"dependencies/assets/effects/tri_effect.pdeffect\"") != std::string::npos);

	const std::string texture = readArchiveEntryText(textureArchivePath.c_str(), "texture.ini");
	const std::string textureManifest =
		readArchiveEntryText(textureArchivePath.c_str(), "_meta/manifest.json");
	REQUIRE(texture.find("catalog_id = example:tri_texture") != std::string::npos);
	REQUIRE(texture.find("texture_file = texture.png") != std::string::npos);
	REQUIRE(textureManifest.find("\"texture_file\": \"texture.png\"") != std::string::npos);

	const std::string skin = readArchiveEntryText(skinArchivePath.c_str(), "skin.ini");
	REQUIRE(skin.find("catalog_id = example:tri_skin") != std::string::npos);
	REQUIRE(skin.find("texture_file = texture.tga") != std::string::npos);
	REQUIRE(skin.find("swatches_file = swatches.json") != std::string::npos);
	REQUIRE(skin.find("material_archive = dependencies/assets/material/tri_material.pdmaterial") != std::string::npos);
	REQUIRE(skin.find("texture_archive = dependencies/assets/texture/tri_texture.pdtexture") != std::string::npos);
	const std::string skinSwatches = readArchiveEntryText(skinArchivePath.c_str(), "swatches.json");
	REQUIRE(skinSwatches.find("\"schema\": \"pd2.skin.swatches.v1\"") != std::string::npos);

	const std::string effect = readArchiveEntryText(effectArchivePath.c_str(), "effect.ini");
	const std::string effectGraph = readArchiveEntryText(effectArchivePath.c_str(), "effect.graph.json");
	const std::string effectTimeline = readArchiveEntryText(effectArchivePath.c_str(), "timeline.json");
	const std::string effectManifest =
		readArchiveEntryText(effectArchivePath.c_str(), "_meta/manifest.json");
	REQUIRE(effect.find("catalog_id = example:tri_effect") != std::string::npos);
	REQUIRE(effect.find("effect_key = glow") != std::string::npos);
	REQUIRE(effect.find("target_key = weapon") != std::string::npos);
	REQUIRE(effect.find("effect_type") == std::string::npos);
	REQUIRE(effect.find("effect_file = effect.graph.json") != std::string::npos);
	REQUIRE(effect.find("timeline_file = timeline.json") != std::string::npos);
	REQUIRE(effectGraph.find("\"schema\": \"pd.effect_graph.v1\"") != std::string::npos);
	REQUIRE(effectTimeline.find("\"schema\": \"pd2.effect.timeline.v1\"") != std::string::npos);
	REQUIRE(effectManifest.find("\"effect_file\": \"effect.graph.json\"") != std::string::npos);
	REQUIRE(effectManifest.find("\"timeline_file\": \"timeline.json\"") != std::string::npos);

	const std::string prop = readArchiveEntryText(propArchivePath.c_str(), "prop.ini");
	const std::string propManifest = readArchiveEntryText(propArchivePath.c_str(), "_meta/manifest.json");
	REQUIRE(prop.find("catalog_id = example:tri_prop") != std::string::npos);
	REQUIRE(prop.find("prop_key = object") != std::string::npos);
	REQUIRE(prop.find("prop_type") == std::string::npos);
	REQUIRE(prop.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(prop.find("behavior_graph = behavior.graph.json") != std::string::npos);
	REQUIRE(propManifest.find("\"model_file\": \"model.gltf\"") != std::string::npos);
	REQUIRE(propManifest.find("\"behavior_graph\": \"behavior.graph.json\"") != std::string::npos);

	const std::string vehicle = readArchiveEntryText(vehicleArchivePath.c_str(), "vehicle.ini");
	REQUIRE(vehicle.find("catalog_id = example:tri_vehicle") != std::string::npos);
	REQUIRE(vehicle.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(vehicle.find("physics_file = physics.json") != std::string::npos);

	const std::string mission = readArchiveEntryText(missionArchivePath.c_str(), "mission.ini");
	const std::string missionManifest =
		readArchiveEntryText(missionArchivePath.c_str(), "_meta/manifest.json");
	REQUIRE(mission.find("catalog_id = example:tri_mission") != std::string::npos);
	REQUIRE(mission.find("mission_graph_file = mission.graph.json") != std::string::npos);
	REQUIRE(mission.find("scenario_archive = dependencies/assets/scenarios/tri_scenario.pdscenario") != std::string::npos);
	REQUIRE(mission.find("scenario_graph_cache = pdscenario_scene_glb_clean_public_v99_standalone_backfill_collision_obj_collision_flags_json_room_lights_json_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_alphamask_quip_shuffle_graph_portals_json_objects_json_setup_fields_json_ai_lists_json_ai_command_graph_navhashes_objectives_spawns_volumes_pads_paths_json_navtables_json") != std::string::npos);
	REQUIRE(mission.find("objectives_file = objectives.json") != std::string::npos);
	REQUIRE(mission.find("briefing_file") == std::string::npos);
	REQUIRE(missionManifest.find("\"objectives_file\": \"objectives.json\"") != std::string::npos);
	REQUIRE(missionManifest.find("\"briefing_file\"") == std::string::npos);
	REQUIRE(missionManifest.find(".tsv") == std::string::npos);
	{
		OpenArchive opened;
		opened.archive = modArchiveOpen(missionArchivePath.c_str());
		REQUIRE(opened.archive != nullptr);
		REQUIRE_FALSE(archiveHasEntry(opened.archive, "briefing.json"));
		const std::string setupFields = readNestedArchiveEntryText(
			opened.archive,
			"dependencies/assets/scenarios/tri_scenario.pdscenario",
			"setup.fields.json");
		REQUIRE(setupFields.find("\"kind\": \"briefing\"") !=
			std::string::npos);
		REQUIRE(setupFields.find("\"field\": \"briefing.kind\"") !=
			std::string::npos);
		REQUIRE(setupFields.find("\"field\": \"briefing.text_token\"") !=
			std::string::npos);
	}

	const std::string gamemode = readArchiveEntryText(gamemodeArchivePath.c_str(), "gamemode.ini");
	REQUIRE(gamemode.find("catalog_id = example:tri_gamemode") != std::string::npos);
	REQUIRE(gamemode.find("mode_key = combat") != std::string::npos);
	REQUIRE(gamemode.find("mode_id") == std::string::npos);
	REQUIRE(gamemode.find("rules_file = rules.json") != std::string::npos);

	const std::string botprofile = readArchiveEntryText(botprofileArchivePath.c_str(), "botprofile.ini");
	REQUIRE(botprofile.find("catalog_id = example:tri_botprofile") != std::string::npos);
	REQUIRE(botprofile.find("type_key = general") != std::string::npos);
	REQUIRE(botprofile.find("difficulty_key = normal") != std::string::npos);
	REQUIRE(botprofile.find("profile_file = profile.json") != std::string::npos);

	const std::string hud = readArchiveEntryText(hudArchivePath.c_str(), "hud.ini");
	const std::string hudManifest =
		readArchiveEntryText(hudArchivePath.c_str(), "_meta/manifest.json");
	REQUIRE(hud.find("catalog_id = example:tri_hud") != std::string::npos);
	REQUIRE(hud.find("hud_key = ammo") != std::string::npos);
	REQUIRE(hud.find("hud_id") == std::string::npos);
	REQUIRE(hud.find("element_type") == std::string::npos);
	REQUIRE(hud.find("texture_file = texture.png") != std::string::npos);
	REQUIRE(hud.find("layout_file = layout.json") != std::string::npos);
	REQUIRE(hudManifest.find("\"texture_file\": \"texture.png\"") != std::string::npos);
	REQUIRE(hudManifest.find("\"layout_file\": \"layout.json\"") != std::string::npos);

	const std::string theme = readArchiveEntryText(themeArchivePath.c_str(), "theme.ini");
	const std::string themeJson = readArchiveEntryText(themeArchivePath.c_str(), "theme.json");
	REQUIRE(theme.find("catalog_id = example:tri_theme") != std::string::npos);
	REQUIRE(theme.find("theme_file = theme.json") != std::string::npos);
	REQUIRE(theme.find("ui_archive = dependencies/assets/ui/tri_reticle.pdui") != std::string::npos);
	REQUIRE(theme.find("font_archive = dependencies/assets/font/tri_theme_font.pdfont") != std::string::npos);
	REQUIRE(theme.find("audio_archive = dependencies/assets/audio/tri_click.pdsfx") != std::string::npos);
	REQUIRE(theme.find("music_archive = dependencies/assets/music/tri_song.pdsong") != std::string::npos);
	REQUIRE(theme.find("effect_archive") == std::string::npos);
	const std::string themeManifest = readArchiveEntryText(themeArchivePath.c_str(), "_meta/manifest.json");
	REQUIRE(themeManifest.find("\"catalog_id\": \"example:tri_theme\"") != std::string::npos);
	REQUIRE(themeManifest.find("\"theme_file\"") == std::string::npos);
	REQUIRE(themeManifest.find("\"audio_archive\"") == std::string::npos);
	REQUIRE(themeManifest.find("\"music_archive\"") == std::string::npos);
	REQUIRE(themeManifest.find("\"effect_archive\"") == std::string::npos);
	REQUIRE(themeJson.find("\"schema\": \"pd2.theme.v1\"") != std::string::npos);
	REQUIRE(themeJson.find("\"name\": \"Triangle Theme\"") != std::string::npos);
	REQUIRE(themeJson.find("\"palette\"") != std::string::npos);
	REQUIRE(themeJson.find("\"dialog_border1\": \"66ccffff\"") != std::string::npos);
	REQUIRE(themeJson.find("\"title_glow\": \"66ccffff\"") != std::string::npos);
	REQUIRE(themeJson.find("\"accent\"") == std::string::npos);
	REQUIRE(themeJson.find("\"chrome\"") == std::string::npos);

	const std::string entity = readArchiveEntryText(entityArchivePath.c_str(), "entity.ini");
	const std::string bindings = readArchiveEntryText(entityArchivePath.c_str(), "bindings.json");
	const std::string entityManifest =
		readArchiveEntryText(entityArchivePath.c_str(), "_meta/manifest.json");
	REQUIRE(entity.find("catalog_id = example:tri_entity") != std::string::npos);
	REQUIRE(entity.find("bindings_file = bindings.json") != std::string::npos);
	REQUIRE(entityManifest.find("\"bindings_file\": \"bindings.json\"") != std::string::npos);
	REQUIRE(entity.find("behavior_graph = behavior.graph.json") != std::string::npos);
	REQUIRE(entityManifest.find("\"behavior_graph\": \"behavior.graph.json\"") != std::string::npos);
	REQUIRE(entity.find("composition_file = composition.json") != std::string::npos);
	REQUIRE(entityManifest.find("\"composition_file\": \"composition.json\"") != std::string::npos);
	REQUIRE(bindings.find("\"schema\": \"pd.entity.bindings.v1\"") != std::string::npos);

	const std::string projectile = readArchiveEntryText(projectileArchivePath.c_str(), "projectile.ini");
	const std::string projectileManifest =
		readArchiveEntryText(projectileArchivePath.c_str(), "_meta/manifest.json");
	REQUIRE(projectile.find("catalog_id = example:tri_projectile") != std::string::npos);
	REQUIRE(projectile.find("behavior_graph = behavior.graph.json") != std::string::npos);
	REQUIRE(projectileManifest.find("\"behavior_graph\": \"behavior.graph.json\"") != std::string::npos);

	const std::string weapon = readArchiveEntryText(weaponArchiveFullPath.c_str(), "weapon.ini");
	const std::string weaponManifest =
		readArchiveEntryText(weaponArchiveFullPath.c_str(), "_meta/manifest.json");
	const std::string weaponPrimaryGraph =
		readArchiveEntryText(weaponArchiveFullPath.c_str(), "behavior/primary.graph.json");
	const std::string weaponSecondaryGraph =
		readArchiveEntryText(weaponArchiveFullPath.c_str(), "behavior/secondary.graph.json");
	const std::string weaponSettings =
		readArchiveEntryText(weaponArchiveFullPath.c_str(), "behavior/settings.json");
	const std::string weaponVariables =
		readArchiveEntryText(weaponArchiveFullPath.c_str(), "behavior/variables.json");
	const std::string weaponPresentation =
		readArchiveEntryText(weaponArchiveFullPath.c_str(), "bindings/presentation.json");
	REQUIRE(weapon.find("catalog_id = example:tri_weapon") != std::string::npos);
	REQUIRE(weapon.find("model_file = dependencies/assets/models/weapon.pdmesh") != std::string::npos);
	REQUIRE(weaponManifest.find("\"model_file\": \"dependencies/assets/models/weapon.pdmesh\"") != std::string::npos);
	REQUIRE(weapon.find("primary_graph = behavior/primary.graph.json") != std::string::npos);
	REQUIRE(weaponManifest.find("\"primary_graph\": \"behavior/primary.graph.json\"") != std::string::npos);
	REQUIRE(weapon.find("secondary_graph = behavior/secondary.graph.json") != std::string::npos);
	REQUIRE(weaponManifest.find("\"secondary_graph\": \"behavior/secondary.graph.json\"") != std::string::npos);
	REQUIRE(weapon.find("settings_file = behavior/settings.json") != std::string::npos);
	REQUIRE(weaponManifest.find("\"settings_file\": \"behavior/settings.json\"") != std::string::npos);
	REQUIRE(weapon.find("variables_file = behavior/variables.json") != std::string::npos);
	REQUIRE(weaponManifest.find("\"variables_file\": \"behavior/variables.json\"") != std::string::npos);
	REQUIRE(weapon.find("shared_context_file = behavior/shared-context.json") != std::string::npos);
	REQUIRE(weaponManifest.find("\"shared_context_file\": \"behavior/shared-context.json\"") != std::string::npos);
	REQUIRE(weapon.find("material_slots_file") == std::string::npos);
	REQUIRE(weapon.find("grip_sockets_file") == std::string::npos);
	REQUIRE(weaponManifest.find("material_slots_file") == std::string::npos);
	REQUIRE(weaponManifest.find("grip_sockets_file") == std::string::npos);
	{
		OpenArchive opened;
		opened.archive = modArchiveOpen(weaponArchiveFullPath.c_str());
		REQUIRE(opened.archive != nullptr);
		REQUIRE_FALSE(archiveHasEntry(opened.archive,
			"bindings/material-slots.json"));
		REQUIRE_FALSE(archiveHasEntry(opened.archive,
			"bindings/grip-sockets.json"));
	}
	REQUIRE(weapon.find("presentation_file = bindings/presentation.json") != std::string::npos);
	REQUIRE(weaponManifest.find("\"presentation_file\": \"bindings/presentation.json\"") != std::string::npos);
	REQUIRE(weapon.find("primary_projectile_archive = dependencies/assets/projectiles/primary.pdprojectile") != std::string::npos);
	REQUIRE(weaponManifest.find("\"primary_projectile_archive\": \"dependencies/assets/projectiles/primary.pdprojectile\"") != std::string::npos);
	REQUIRE(weapon.find("deployed_entity_archive = dependencies/assets/entities/deployed.pdentity") != std::string::npos);
	REQUIRE(weaponManifest.find("\"deployed_entity_archive\": \"dependencies/assets/entities/deployed.pdentity\"") != std::string::npos);
	REQUIRE(weapon.find("fire_sound_archive") == std::string::npos);
	REQUIRE(weapon.find("idle_animation_archive") == std::string::npos);
	REQUIRE(weapon.find("reticle_archive = dependencies/assets/ui/reticle.pdui") != std::string::npos);
	REQUIRE(weaponManifest.find("\"reticle_archive\": \"dependencies/assets/ui/reticle.pdui\"") != std::string::npos);
	REQUIRE(weaponSettings.find("\"schema\": \"pd.weapon_settings.v1\"") != std::string::npos);
	REQUIRE(weaponVariables.find("\"schema\": \"pd.weapon_variables.v1\"") != std::string::npos);
	REQUIRE(weaponPresentation.find("\"crosshair\": \"example:tri_reticle\"") != std::string::npos);
	REQUIRE(weaponPresentation.find("\"zoom_fov\": 45.0") != std::string::npos);
	REQUIRE(weapon.find("behavior_graph") == std::string::npos);
	REQUIRE(weaponPrimaryGraph.find("\"schema\": \"pd.weapon_graph.v1\"") != std::string::npos);
	REQUIRE(weaponPrimaryGraph.find("\"asset_id\": \"example:tri_weapon\"") != std::string::npos);
	REQUIRE(weaponSecondaryGraph.find("\"graph_id\": \"secondary\"") != std::string::npos);
	REQUIRE(weapon.find("hand_model_file") == std::string::npos);
	REQUIRE(weapon.find("texture_file = weapon_texture.png") == std::string::npos);
	REQUIRE(weapon.find("animation_file = reload.gltf") == std::string::npos);
	REQUIRE(weapon.find("file_path = fire.wav") == std::string::npos);
	REQUIRE(weaponPrimaryGraph.find("\"kind\": \"spawn.fired_projectile\"") != std::string::npos);
	REQUIRE(weaponPrimaryGraph.find("\"projectile_ref\": \"example:tri_projectile\"") != std::string::npos);
	REQUIRE(weaponPrimaryGraph.find("\"from\": \"trigger_primary\"") != std::string::npos);
	REQUIRE(weaponPrimaryGraph.find("\"to\": \"spawn_projectile\"") != std::string::npos);
	REQUIRE(weaponPrimaryGraph.find("\"node\": \"spawn_projectile\"") != std::string::npos);
	REQUIRE(weaponPrimaryGraph.find("\"kind\": \"spawn.projectile\"") == std::string::npos);
	REQUIRE(weaponPrimaryGraph.find(".exec") == std::string::npos);
	REQUIRE(weaponSecondaryGraph.find("\"kind\": \"spawn.thrown_physical\"") != std::string::npos);
	REQUIRE(weaponSecondaryGraph.find("\"entity_ref\": \"example:tri_entity\"") != std::string::npos);
	REQUIRE(weaponSecondaryGraph.find("\"from\": \"trigger_secondary\"") != std::string::npos);
	REQUIRE(weaponSecondaryGraph.find("\"to\": \"deploy_entity\"") != std::string::npos);
	REQUIRE(weaponSecondaryGraph.find("\"node\": \"deploy_entity\"") != std::string::npos);
	REQUIRE(weaponSecondaryGraph.find("\"kind\": \"spawn.deployed_entity\"") == std::string::npos);
	REQUIRE(weaponSecondaryGraph.find(".exec") == std::string::npos);
	{
		weapon_graph_ir_t weaponIr;
		char err[256] = {};
		const s32 exampleCompileResult = weaponGraphCompileArchiveFile(weaponArchiveFullPath.c_str(),
			ASSET_WEAPON, &weaponIr, err, sizeof(err));
		INFO(err);
		REQUIRE(exampleCompileResult == 0);
		REQUIRE(weaponIr.context_count == 4);
		REQUIRE(std::string(weaponIr.contexts[0].name) == "owner_player");
		REQUIRE(std::string(weaponIr.contexts[0].source) == "equipped_player");
		REQUIRE(std::string(weaponIr.contexts[3].lifetime) == "projectile_life");
		weapon_graph_ir_t needlerIr;
		const std::string needlerPath = std::string(PD_SOURCE_DIR) + "/dev-mods/needler/needler.pdweapon";
		const s32 needlerCompileResult = weaponGraphCompileArchiveFile(needlerPath.c_str(),
			ASSET_WEAPON, &needlerIr, err, sizeof(err));
		INFO(err);
		REQUIRE(needlerCompileResult == 0);
		REQUIRE(needlerIr.context_count == 4);
		REQUIRE(std::string(needlerIr.contexts[1].source) == "equipped_player_team");
		REQUIRE(std::string(needlerIr.contexts[2].type) == "weapon_instance_ref");
		weaponGraphIrFree(&needlerIr);
		asset_entry_t reticle = {};
		strncpy(reticle.id, "example:tri_reticle", sizeof(reticle.id) - 1);
		reticle.type = ASSET_UI;
		reticle.enabled = 1;
		const std::string reticleTexture = weaponArchiveFullPath
			+ "::dependencies/assets/ui/reticle.pdui::texture.png";
		strncpy(reticle.ext.ui.texture_file, reticleTexture.c_str(),
			sizeof(reticle.ext.ui.texture_file) - 1);
		const u8 reticlePngHeader[24] = {
			0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
			0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
			0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10
		};
		testStubAssetCatalogResolveWith(&reticle);
		testStubFsFileLoadWith(reticleTexture.c_str(), reticlePngHeader,
			sizeof(reticlePngHeader));
		ScopedCatalogSourceStub stubReset;
		weaponGraphRuntimeClearAll();
		weaponGraphRuntimeSetEnabled(1);
		const s32 registerResult = weaponGraphRuntimeRegisterWeaponArchive(
			WEAPON_CUSTOM_START, weaponArchiveFullPath.c_str(), err, sizeof(err));
		INFO(std::string(err));
		REQUIRE(registerResult == 0);
		const weapon_graph_held_function_t *primary =
			weaponGraphRuntimeGetHeldFunctionForGameplay(WEAPON_CUSTOM_START, 0);
		const weapon_graph_held_function_t *secondary =
			weaponGraphRuntimeGetHeldFunctionForGameplay(WEAPON_CUSTOM_START, 1);
		REQUIRE(primary != nullptr);
		REQUIRE(secondary != nullptr);
		REQUIRE(primary->opcode == WEAPON_GRAPH_OP_SPAWN_FIRED_PROJECTILE);
		REQUIRE(std::string(primary->projectile_ref) == "example:tri_projectile");
		REQUIRE(secondary->opcode == WEAPON_GRAPH_OP_SPAWN_THROWN_PHYSICAL);
		REQUIRE(std::string(secondary->entity_ref) == "example:tri_entity");
		const weapon_graph_projectile_runtime_t *projectileRuntime =
			weaponGraphRuntimeGetProjectileForHeldFunction(primary);
		const weapon_graph_entity_runtime_t *entityRuntime =
			weaponGraphRuntimeGetEntityForHeldFunction(secondary);
		REQUIRE(projectileRuntime != nullptr);
		REQUIRE(entityRuntime != nullptr);
		REQUIRE(std::string(projectileRuntime->asset_id) == "example:tri_projectile");
		REQUIRE(std::string(projectileRuntime->motion_kind) == "authored");
		REQUIRE(projectileRuntime->has_speed == 1);
		REQUIRE(projectileRuntime->speed == Approx(18.0f));
		REQUIRE(projectileRuntime->has_timer60 == 1);
		REQUIRE(projectileRuntime->timer60 == 120);
		REQUIRE(projectileRuntime->has_transition_to_entity == 1);
		REQUIRE(std::string(projectileRuntime->entity_ref) == "example:tri_entity");
		REQUIRE(std::string(projectileRuntime->transition_when) == "at_rest");
		REQUIRE(projectileRuntime->transfer_owner == 1);
		REQUIRE(projectileRuntime->transfer_position == 1);
		REQUIRE(std::string(entityRuntime->asset_id) == "example:tri_entity");
		REQUIRE(entityRuntime->has_armed_explosive == 1);
		REQUIRE(std::string(entityRuntime->archetype) == "deployed_triangle");
		REQUIRE(entityRuntime->has_activation_time60 == 1);
		REQUIRE(entityRuntime->activation_time60 == 30);
		REQUIRE(entityRuntime->has_recovery_time60 == 1);
		REQUIRE(entityRuntime->recovery_time60 == 15);
		REQUIRE(entityRuntime->delete_on_detonate == 1);
		REQUIRE(entityRuntime->has_owner_cleanup == 1);
		REQUIRE(entityRuntime->max_active_per_owner == 1);
		REQUIRE(weaponGraphRuntimeGetEntityForProjectile(projectileRuntime) ==
			entityRuntime);
		weaponGraphRuntimeClearWeapon(WEAPON_CUSTOM_START);
		REQUIRE(weaponGraphRuntimeGetHeldFunctionForGameplay(
			WEAPON_CUSTOM_START, 0) == nullptr);
		REQUIRE(weaponGraphRuntimeGetHeldFunctionForGameplay(
			WEAPON_CUSTOM_START, 1) == nullptr);
		REQUIRE(weaponGraphRuntimeGetProjectile("example:tri_projectile") ==
			nullptr);
		REQUIRE(weaponGraphRuntimeGetEntity("example:tri_entity") ==
			nullptr);
		weaponGraphRuntimeSetEnabled(0);
		weaponGraphRuntimeClearAll();
	}

	const std::string weaponAnim = readArchiveEntryText(weaponArchivePath.c_str(), "animation.ini");
	const std::string weaponAnimManifest =
		readArchiveEntryText(weaponArchivePath.c_str(), "_meta/manifest.json");
	const std::string weaponGltf = readArchiveEntryText(weaponArchivePath.c_str(), "animation.gltf");
	const std::string skeletalAnim = readArchiveEntryText(skeletalArchivePath.c_str(), "animation.ini");
	const std::string skeletalAnimManifest =
		readArchiveEntryText(skeletalArchivePath.c_str(), "_meta/manifest.json");
	const std::string skeletalGltf = readArchiveEntryText(skeletalArchivePath.c_str(), "animation.gltf");
	REQUIRE(weaponAnim.find("catalog_id = example:weapon_idle") != std::string::npos);
	REQUIRE(weaponAnim.find("category = weapon_animation") != std::string::npos);
	REQUIRE(weaponAnimManifest.find("\"category\": \"weapon_animation\"") != std::string::npos);
	REQUIRE(weaponAnimManifest.find("\"animation\": \"animation.gltf\"") != std::string::npos);
	REQUIRE(weaponAnimManifest.find("\"runtime_source\": \"animation.gltf\"") != std::string::npos);
	REQUIRE(weaponAnimManifest.find("\"animation_file\": \"animation.gltf\"") != std::string::npos);
	REQUIRE(weaponGltf.find("\"animations\"") != std::string::npos);
	REQUIRE(skeletalAnim.find("catalog_id = example:character_skeletal") != std::string::npos);
	REQUIRE(skeletalAnim.find("category = character_animation") != std::string::npos);
	REQUIRE(skeletalAnimManifest.find("\"category\": \"character_animation\"") != std::string::npos);
	REQUIRE(skeletalAnimManifest.find("\"animation\": \"animation.gltf\"") != std::string::npos);
	REQUIRE(skeletalAnimManifest.find("\"runtime_source\": \"animation.gltf\"") != std::string::npos);
	REQUIRE(skeletalAnimManifest.find("\"animation_file\": \"animation.gltf\"") != std::string::npos);
	REQUIRE(skeletalGltf.find("\"path\": \"translation\"") != std::string::npos);

	REQUIRE_FALSE(std::filesystem::exists(root / "heads/tri_head/model.gltf"));
	REQUIRE_FALSE(std::filesystem::exists(root / "bodies/tri_body/model.gltf"));
	REQUIRE_FALSE(std::filesystem::exists(root / "arenas/tri_arena/geometry.obj"));
	REQUIRE_FALSE(std::filesystem::exists(root / "characters/tri_character/character.ini"));
	REQUIRE_FALSE(std::filesystem::exists(root / "projectiles/tri_projectile/projectile.ini"));
	REQUIRE_FALSE(std::filesystem::exists(root / "weapons/tri_weapon/model.gltf"));
	REQUIRE_FALSE(std::filesystem::exists(root / "animations/weapon_idle/animation.gltf"));
}

} // namespace
