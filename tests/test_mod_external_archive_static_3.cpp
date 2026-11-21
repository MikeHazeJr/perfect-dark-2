/*
 * Split out of tests/test_mod_external_archive_static.cpp for build time (T-TOOLING-018):
 * part 3 of 3 of its TEST_CASEs, in their original order, with
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

TEST_CASE("typed pdxxx example archives carry the implemented asset payloads",
          "[modding][pdxxx][examples][static][c3812][archive-inventory]") {
	const std::filesystem::path root =
		"examples/modding/typed-pdxxx-basic";
	REQUIRE(std::filesystem::is_directory(root));

	const std::vector<ExpectedPdxxxArchive> implementedArchives = {
		{
			"character",
			".pdcharacter",
			"characters/tri_character.pdcharacter",
			{ "character.ini", "portrait.png",
			  "dependencies/assets/body/tri_body.pdbody",
			  "dependencies/assets/head/tri_head.pdhead",
			  "_meta/manifest.json" },
		},
		{
			"head",
			".pdhead",
			"heads/tri_head.pdhead",
			{ "head.ini", "mesh.pdmesh", "_meta/manifest.json" },
		},
		{
			"body",
			".pdbody",
			"bodies/tri_body.pdbody",
			{ "body.ini", "mesh.pdmesh", "hand.pdmesh",
			  "_meta/manifest.json" },
		},
		{
			"arena",
			".pdarena",
			"arenas/tri_arena.pdarena",
			{ "arena.ini",
			  "dependencies/assets/scenarios/tri_scenario.pdscenario",
			  "_meta/manifest.json" },
		},
		{
			"mesh/model",
			".pdmesh",
			"meshes/tri_mesh.pdmesh",
			{ "mesh.ini", "model.gltf",
			  "_meta/manifest.json" },
		},
		{
			"material",
			".pdmaterial",
			"materials/tri_material.pdmaterial",
			{ "material.ini",
			  "material.json",
			  "dependencies/assets/texture/tri_texture.pdtexture",
			  "dependencies/assets/effects/tri_effect.pdeffect",
			  "_meta/manifest.json" },
		},
		{
			"texture",
			".pdtexture",
			"textures/tri_texture.pdtexture",
			{ "texture.ini", "texture.png", "_meta/manifest.json" },
		},
		{
			"skin",
			".pdskin",
			"skins/tri_skin.pdskin",
			{ "skin.ini", "texture.tga", "swatches.json",
			  "dependencies/assets/material/tri_material.pdmaterial",
			  "_meta/manifest.json" },
		},
		{
			"effect",
			".pdeffect",
			"effects/tri_effect.pdeffect",
			{ "effect.ini", "effect.graph.json", "_meta/manifest.json" },
		},
		{
			"prop",
			".pdprop",
			"props/tri_prop.pdprop",
			{ "prop.ini", "model.gltf", "behavior.graph.json",
			  "_meta/manifest.json" },
		},
		{
			"vehicle",
			".pdvehicle",
			"vehicles/tri_vehicle.pdvehicle",
			{ "vehicle.ini", "model.gltf", "physics.json",
			  "behavior.graph.json", "_meta/manifest.json" },
		},
		{
			"mission",
			".pdmission",
			"missions/tri_mission.pdmission",
			{ "mission.ini", "mission.graph.json", "objectives.json",
			  "dependencies/assets/scenarios/tri_scenario.pdscenario",
			  "_meta/manifest.json" },
		},
		{
			"gamemode",
			".pdgamemode",
			"gamemodes/tri_gamemode.pdgamemode",
			{ "gamemode.ini", "rules.json", "_meta/manifest.json" },
		},
		{
			"bot profile",
			".pdbotprofile",
			"botprofiles/tri_botprofile.pdbotprofile",
			{ "botprofile.ini", "profile.json", "_meta/manifest.json" },
		},
		{
			"HUD",
			".pdhud",
			"hud/tri_hud.pdhud",
			{ "hud.ini", "texture.png", "layout.json",
			  "_meta/manifest.json" },
		},
		{
			"theme",
			".pdtheme",
			"themes/tri_theme.pdtheme",
			{ "theme.ini", "theme.json",
			  "dependencies/assets/ui/tri_reticle.pdui",
			  "dependencies/assets/font/tri_theme_font.pdfont",
			  "dependencies/assets/audio/tri_click.pdsfx",
			  "dependencies/assets/music/tri_song.pdsong",
			  "_meta/manifest.json" },
		},
		{
			"entity",
			".pdentity",
			"entities/tri_entity.pdentity",
			{ "entity.ini", "bindings.json", "behavior.graph.json",
			  "composition.json", "_meta/manifest.json" },
		},
		{
			"projectile",
			".pdprojectile",
			"projectiles/tri_projectile.pdprojectile",
			{ "projectile.ini", "behavior.graph.json",
			  "_meta/manifest.json" },
		},
		{
			"weapon",
			".pdweapon",
			"weapons/tri_weapon.pdweapon",
			{ "weapon.ini", "behavior/primary.graph.json",
			  "behavior/secondary.graph.json", "behavior/settings.json",
			  "behavior/variables.json", "behavior/shared-context.json",
			  "bindings/presentation.json",
			  "dependencies/assets/models/weapon.pdmesh",
			  "dependencies/assets/projectiles/primary.pdprojectile",
			  "_meta/manifest.json" },
		},
		{
			"weapon animation",
			".pdanim",
			"animations/weapon_idle.pdanim",
			{ "animation.ini", "animation.gltf", "_meta/manifest.json" },
		},
		{
			"character animation",
			".pdanim",
			"animations/character_skeletal.pdanim",
			{ "animation.ini", "animation.gltf", "_meta/manifest.json" },
		},
		{
			"sound effect",
			".pdsfx",
			"audio/sfx/tri_click.pdsfx",
			{ "sound.ini", "sample.wav", "_meta/manifest.json" },
		},
		{
			"voice",
			".pdvoice",
			"audio/voice/tri_voice.pdvoice",
			{ "voice.ini", "sample.wav", "_meta/manifest.json" },
		},
		{
			"music",
			".pdsong",
			"audio/music/tri_song.pdsong",
			{ "music.ini", "sequence.mid", "sequence.json",
			  "_meta/manifest.json" },
		},
		{
			"UI texture",
			".pdui",
			"ui/tri_reticle.pdui",
			{ "ui.ini", "texture.png", "layout.json", "nineslice.ini",
			  "_meta/manifest.json" },
		},
		{
			"font",
			".pdfont",
			"fonts/tri_font.pdfont",
			{ "font.ini", "glyphs.pgm", "font.metrics.json",
			  "_meta/manifest.json" },
		},
		{
			"language",
			".pdlang",
			"lang/tri_lang.pdlang",
			{ "lang.ini", "strings.json", "_meta/manifest.json" },
		},
		{
			"scenario",
			".pdscenario",
			"scenarios/tri_scenario.pdscenario",
			{ "scenario.ini", "scene.glb",
			  "pads.json", "spawns.json", "volumes.json", "objects.json",
			  "setup.fields.json", "ai/ailists.json", "objectives.json",
			  "navigation/waypoints.json", "navigation/waygroups.json",
			  "navigation/covers.json", "navigation/paths.json",
			  "navigation.ini", "level.graph.json",
			  "_meta/generated-collision.json", "_meta/generated-navmesh.json",
			  "_meta/manifest.json" },
		},
	};

	for (const ExpectedPdxxxArchive &spec : implementedArchives) {
		const std::filesystem::path archivePath = root / spec.relPath;
		INFO(spec.assetType << " " << archivePath.generic_string());
		REQUIRE(std::filesystem::is_regular_file(archivePath));
		char err[256];
		REQUIRE(assetArchiveValidateFile(archivePath.string().c_str(),
			ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) == 0);

		OpenArchive opened;
		opened.archive = modArchiveOpen(archivePath.string().c_str());
		REQUIRE(opened.archive != nullptr);

		for (const char *entryPath : spec.requiredEntries) {
			INFO(spec.relPath << "::" << entryPath);
			s32 idx = modArchiveFindEntry(opened.archive, entryPath);
			REQUIRE(idx >= 0);
			REQUIRE(modArchiveGetEntrySize(opened.archive, idx) > 0);
		}

		for (s32 i = 0; i < modArchiveGetEntryCount(opened.archive); i++) {
			const char *entryName = modArchiveGetEntryName(opened.archive, i);
			REQUIRE(entryName != nullptr);
			INFO(spec.relPath << "::" << entryName);
			REQUIRE(std::string(entryName).find(".bin") == std::string::npos);
			REQUIRE(std::string(entryName).find("..") == std::string::npos);
		}
	}
}

TEST_CASE("typed pdxxx example archives keep declared source refs self-contained",
          "[modding][pdxxx][examples][static][c3812][archive-refs]") {
	const std::filesystem::path root =
		"examples/modding/typed-pdxxx-basic";

	struct ArchiveRefSpec {
		const char *relPath;
		const char *descriptorEntry;
		std::vector<const char *> iniPathKeys;
		std::vector<const char *> gltfEntries;
		std::vector<const char *> objEntries;
	};

	const std::vector<ArchiveRefSpec> specs = {
		{
			"characters/tri_character.pdcharacter",
			"character.ini",
			{ "body_archive", "head_archive", "portrait_file" },
			{},
			{},
		},
		{
			"heads/tri_head.pdhead",
			"head.ini",
			{ "mesh_archive" },
			{},
			{},
		},
		{
			"bodies/tri_body.pdbody",
			"body.ini",
			{ "mesh_archive", "hand_archive" },
			{},
			{},
		},
		{
			"arenas/tri_arena.pdarena",
			"arena.ini",
			{ "scenario_archive" },
			{},
			{},
		},
		{
			"meshes/tri_mesh.pdmesh",
			"mesh.ini",
			{ "model_file" },
			{ "model.gltf" },
			{},
		},
		{
			"materials/tri_material.pdmaterial",
			"material.ini",
			{ "material_file", "texture_archive", "effect_archive" },
			{},
			{},
		},
		{
			"textures/tri_texture.pdtexture",
			"texture.ini",
			{ "texture_file" },
			{},
			{},
		},
		{
			"skins/tri_skin.pdskin",
			"skin.ini",
			{ "texture_file", "swatches_file", "material_archive", "texture_archive" },
			{},
			{},
		},
		{
			"effects/tri_effect.pdeffect",
			"effect.ini",
			{ "effect_file", "timeline_file" },
			{ "effect.graph.json", "timeline.json" },
			{},
		},
		{
			"props/tri_prop.pdprop",
			"prop.ini",
			{ "model_file", "behavior_graph" },
			{ "model.gltf", "behavior.graph.json" },
			{},
		},
		{
			"vehicles/tri_vehicle.pdvehicle",
			"vehicle.ini",
			{ "model_file", "physics_file", "behavior_graph" },
			{ "model.gltf", "physics.json", "behavior.graph.json" },
			{},
		},
		{
			"missions/tri_mission.pdmission",
			"mission.ini",
			{ "mission_graph_file", "scenario_archive", "objectives_file" },
			{},
			{},
		},
		{
			"gamemodes/tri_gamemode.pdgamemode",
			"gamemode.ini",
			{ "rules_file" },
			{ "rules.json" },
			{},
		},
		{
			"botprofiles/tri_botprofile.pdbotprofile",
			"botprofile.ini",
			{ "profile_file" },
			{ "profile.json" },
			{},
		},
		{
			"hud/tri_hud.pdhud",
			"hud.ini",
			{ "texture_file", "layout_file" },
			{ "layout.json" },
			{},
		},
		{
			"themes/tri_theme.pdtheme",
			"theme.ini",
			{ "theme_file", "ui_archive", "font_archive", "audio_archive",
			  "music_archive", "effect_archive" },
			{ "theme.json" },
			{},
		},
		{
			"entities/tri_entity.pdentity",
			"entity.ini",
			{ "bindings_file", "behavior_graph", "composition_file" },
			{ "behavior.graph.json", "bindings.json", "composition.json" },
			{},
		},
		{
			"projectiles/tri_projectile.pdprojectile",
			"projectile.ini",
			{ "behavior_graph" },
			{ "behavior.graph.json" },
			{},
		},
		{
			"weapons/tri_weapon.pdweapon",
			"weapon.ini",
			{ "model_file", "primary_graph", "secondary_graph",
			  "settings_file", "variables_file", "shared_context_file",
			  "presentation_file", "primary_projectile_archive",
			  "deployed_entity_archive" },
			{},
			{},
		},
		{
			"animations/weapon_idle.pdanim",
			"animation.ini",
			{ "animation_file" },
			{ "animation.gltf" },
			{},
		},
		{
			"animations/character_skeletal.pdanim",
			"animation.ini",
			{ "animation_file" },
			{ "animation.gltf" },
			{},
		},
		{
			"audio/sfx/tri_click.pdsfx",
			"sound.ini",
			{ "file_path" },
			{},
			{},
		},
		{
			"audio/voice/tri_voice.pdvoice",
			"voice.ini",
			{ "file_path" },
			{},
			{},
		},
		{
			"audio/music/tri_song.pdsong",
			"music.ini",
			{ "music_file", "midi_file", "events_file" },
			{},
			{},
		},
		{
			"ui/tri_reticle.pdui",
			"ui.ini",
			{ "texture_file", "layout_file", "nineslice_file" },
			{},
			{},
		},
		{
			"fonts/tri_font.pdfont",
			"font.ini",
			{ "font_file", "glyphs_file", "metrics_file" },
			{},
			{},
		},
		{
			"lang/tri_lang.pdlang",
			"lang.ini",
			{ "strings_file" },
			{},
			{},
		},
		{
			"scenarios/tri_scenario.pdscenario",
			"scenario.ini",
			{ "scene_file", "runtime_source_file", "portals_file", "pads_file",
			  "spawns_file", "volumes_file", "objects_file",
			  "setup_fields_file", "objectives_file", "navigation_file",
			  "waypoints_file", "waygroups_file", "covers_file",
			  "paths_file", "level_graph_file" },
			{},
			{},
		},
	};

	for (const ArchiveRefSpec &spec : specs) {
		const std::filesystem::path archivePath = root / spec.relPath;
		INFO(archivePath.generic_string());
		REQUIRE(std::filesystem::is_regular_file(archivePath));

		OpenArchive opened;
		opened.archive = modArchiveOpen(archivePath.string().c_str());
		REQUIRE(opened.archive != nullptr);

		requireIniPathRefsResolve(opened.archive, spec.relPath,
			spec.descriptorEntry, spec.iniPathKeys);

		for (const char *gltfEntry : spec.gltfEntries) {
			requireGltfUrisResolve(opened.archive, spec.relPath, gltfEntry);
		}
		for (const char *objEntry : spec.objEntries) {
			requireObjRefsResolve(opened.archive, spec.relPath, objEntry);
		}
	}
}

TEST_CASE("Modding Hub exposes typed pdxxx examples as pack input",
          "[modding][pdxxx][examples][static][c3811]") {
	const std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(!hub.empty());

	REQUIRE(hub.find("Typed .pdxxx samples: examples/modding/typed-pdxxx-basic/") !=
	        std::string::npos);
	REQUIRE(hub.find("Use Sample Folder") != std::string::npos);
	REQUIRE(hub.find("examples/modding/typed-pdxxx-basic/") != std::string::npos);
	REQUIRE(hub.find("mods/typed-pdxxx-basic.pdmod") != std::string::npos);
	REQUIRE(hub.find("Edit the .pdxxx archives first; pack .pdmod only for transport.") !=
	        std::string::npos);
	REQUIRE(hub.find(".pdmod output is for sharing, Public Mods, or online delivery") !=
	        std::string::npos);
	REQUIRE(hub.find("editable content is inside the typed .pdxxx asset archives") !=
	        std::string::npos);
	REQUIRE(hub.find("IMPORT .pdmod") != std::string::npos);
	REQUIRE(hub.find("modmgrInstallArchiveFile") != std::string::npos);
	REQUIRE(hub.find("Enable after import") != std::string::npos);
	REQUIRE(hub.find("Pack/import .pdmod archives") != std::string::npos);
	REQUIRE(hub.find(".pdpack") == std::string::npos);
}

TEST_CASE("Map Import stages editable source layouts instead of native map dumps",
          "[modding][pdxxx][mapimport][source][static][c3844]") {
	const std::string mapimport = readFile("port/src/mapimport.c");
	const std::string mapimport_h = readFile("port/include/mapimport.h");
	const std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(!mapimport.empty());
	REQUIRE(!mapimport_h.empty());
	REQUIRE(!hub.empty());

	REQUIRE(mapimport.find("isForbiddenNativePayload") != std::string::npos);
	REQUIRE(mapimport.find("MAPIMPORT_ERR_FORBIDDEN_NATIVE_PAYLOAD") != std::string::npos);
	REQUIRE(mapimport.find("scene.glb") != std::string::npos);
	REQUIRE(mapimport.find("scene.gltf") != std::string::npos);
	REQUIRE(mapimport.find("geometry.obj") != std::string::npos);
	REQUIRE(mapimport.find(".pdarena") != std::string::npos);
	REQUIRE(mapimport.find(".pdscenario") != std::string::npos);
	REQUIRE(mapimport.find("copyDirRecursive(ctx->source_dir") != std::string::npos);
	REQUIRE(mapimport.find("snprintf(dstpath, sizeof(dstpath), \"%s/%s.bg\"") ==
	        std::string::npos);
	REQUIRE(mapimport.find("PD-native binary format importer") == std::string::npos);
	REQUIRE(mapimport.find("PD BG files are typically") == std::string::npos);
	REQUIRE(mapimport.find("Expected a .bg or .bin file") == std::string::npos);
	REQUIRE(mapimport_h.find("has_source_geometry") != std::string::npos);
	REQUIRE(mapimport_h.find("MAPIMPORT_ERR_NO_BG_FILE") == std::string::npos);
	REQUIRE(mapimport_h.find("MAPIMPORT_ERR_CORRUPT_BG") == std::string::npos);
	REQUIRE(hub.find("Native .bg/.bin/.pad/.setup payloads are rejected.") !=
	        std::string::npos);
	REQUIRE(hub.find("Import PD-format map files") == std::string::npos);
}

TEST_CASE("Modding Hub exposes weapon graph browser tab",
          "[modding][pdxxx][weapon_graph][ui][c3814]") {
	const std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(!hub.empty());

	REQUIRE(hub.find("\"Weapons\"") != std::string::npos);
	REQUIRE(hub.find("renderWeaponTool") != std::string::npos);
	REQUIRE(hub.find("assetCatalogGetByIndex") != std::string::npos);
	REQUIRE(hub.find("ASSET_WEAPON") != std::string::npos);
	REQUIRE(hub.find("weaponGraphArchiveReadTextFile") != std::string::npos);
	REQUIRE(hub.find("behavior.graph.json") != std::string::npos);
	REQUIRE(hub.find("nested_payloads.json") != std::string::npos);
	REQUIRE(hub.find("weapon.ini") != std::string::npos);
	REQUIRE(hub.find("s_WeaponSelectedId") != std::string::npos);
}

TEST_CASE("Modding Hub weapon tool supports template imports and pdweapon save",
          "[modding][pdxxx][weapon_graph][ui][editor][c3814]") {
	const std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(!hub.empty());
	REQUIRE(!scanner.empty());

	REQUIRE(hub.find("Use as Template") != std::string::npos);
	REQUIRE(hub.find("Open Creator") != std::string::npos);
	REQUIRE(hub.find("Create Weapon Mod") != std::string::npos);
	REQUIRE(hub.find("Weapon Mod Creation") != std::string::npos);
	REQUIRE(hub.find("Back to Weapons") != std::string::npos);
	REQUIRE(hub.find("weaponToolStartTemplate") != std::string::npos);
	REQUIRE(hub.find("weaponToolPopulateTemplateRefs") != std::string::npos);
	REQUIRE(hub.find("weaponIniGetValue") != std::string::npos);
	REQUIRE(hub.find("weaponCopyCatalogOrArchiveRef") != std::string::npos);
	REQUIRE(hub.find("s_WeaponEditTemplateModelFile") != std::string::npos);
	REQUIRE(hub.find("s_WeaponEditTemplateAnimationFile") != std::string::npos);
	REQUIRE(hub.find("models/held_hi.pdmesh") != std::string::npos);
	REQUIRE(hub.find("weaponArchiveReadNestedCatalogId") != std::string::npos);
	REQUIRE(hub.find("weaponArchiveHasEntry") != std::string::npos);
	REQUIRE(hub.find("weaponJsonFindFirstStringField") != std::string::npos);
	REQUIRE(hub.find("weaponJsonFindFieldForMatchedObject") != std::string::npos);
	REQUIRE(hub.find("models/held_hi.pdmesh") != std::string::npos);
	REQUIRE(hub.find("bindings/animations.json") != std::string::npos);
	REQUIRE(hub.find("bindings/audio.json") != std::string::npos);
	REQUIRE(hub.find("animations_manifest.tsv") == std::string::npos);
	REQUIRE(hub.find("audio_manifest.tsv") == std::string::npos);
	REQUIRE(hub.find("\"projectile_ref\"") != std::string::npos);
	REQUIRE(hub.find("\"entity_ref\"") != std::string::npos);
	REQUIRE(hub.find("\"payload_ref\"") != std::string::npos);
	REQUIRE(hub.find("weaponRenderTemplateWindow") == std::string::npos);
	REQUIRE(hub.find("ImGui::Begin(\"Create Weapon Mod\"") == std::string::npos);
	REQUIRE(hub.find("##modhub_weapon_creator") != std::string::npos);
	REQUIRE(hub.find("weaponCreatorView") != std::string::npos);
	REQUIRE(hub.find("weaponRenderTemplateEditor") != std::string::npos);
	REQUIRE(hub.find("s_WeaponTemplateMenuOpen") != std::string::npos);
	REQUIRE(hub.find("weaponToolSaveCustom") != std::string::npos);
	REQUIRE(hub.find("Save Weapon Mod") != std::string::npos);
	REQUIRE(hub.find("Save Weapon Mod##weapon_save_options") != std::string::npos);
	REQUIRE(hub.find("Creator") != std::string::npos);
	REQUIRE(hub.find("InputText(\"Display Name\", s_WeaponSaveDisplayName") !=
	        std::string::npos);
	REQUIRE(hub.find("InputText(\"Mod Name\", s_WeaponSave") == std::string::npos);
	REQUIRE(hub.find("weaponCatalogSlugForDisplayName") != std::string::npos);
	REQUIRE(hub.find("weaponCatalogIdForDisplayName") != std::string::npos);
	REQUIRE(hub.find("Catalog Name: mod:%s") != std::string::npos);
	REQUIRE(hub.find("WEAPONMOD.SAVE.BEGIN") != std::string::npos);
	/* What the creator cannot put into a clean archive yet is refused by
	 * name instead of being left out of the saved mod. */
	REQUIRE(hub.find("weaponSaveFail(\"DEPENDENCY_CHANGE_UNSUPPORTED\"") !=
	        std::string::npos);
	REQUIRE(hub.find("WEAPONMOD.SAVE.PAYLOAD_FAIL") == std::string::npos);
	REQUIRE(hub.find("weaponSaveFail(\"GRAPH_FAIL\"") != std::string::npos);
	/* Discovery, the manifest verdict, enabling and applying belong to the
	 * mod author's runtime half, shared with the --mod-author boot flag. */
	const std::string authorRuntime = readFile("port/src/mod_author_runtime.c");
	REQUIRE(!authorRuntime.empty());
	REQUIRE(authorRuntime.find("the mod manager did not find %s") != std::string::npos);
	REQUIRE(authorRuntime.find("the mod manager rejected %s: %s") != std::string::npos);
	REQUIRE(authorRuntime.find("modmgrApplyChangesChecked(1, &apply)") != std::string::npos);
	REQUIRE(hub.find("weaponSaveFail(\"MODINVALID\"") == std::string::npos);
	REQUIRE(hub.find("weaponSaveFail(\"MODDISCOVER_FAIL\"") == std::string::npos);
	REQUIRE(hub.find("WEAPONMOD.SAVE.OK") != std::string::npos);
	REQUIRE(hub.find("Create + Enable") != std::string::npos);
	REQUIRE(hub.find("identityGetActiveProfile") != std::string::npos);
	REQUIRE(hub.find("modmgrGetModValid") != std::string::npos);
	REQUIRE(hub.find("modAuthorRuntimeSave(&request, &result, &restartCount") !=
	        std::string::npos);
	REQUIRE(hub.find("Saved, enabled, and catalog updated") != std::string::npos);
	REQUIRE(hub.find("s_WeaponEditWeaponId") == std::string::npos);
	REQUIRE(hub.find("InputInt(\"Weapon ID\"") == std::string::npos);
	REQUIRE(hub.find("\"weapon_id = %d") == std::string::npos);
	REQUIRE(hub.find("Runtime Index:") == std::string::npos);
	REQUIRE(hub.find("BeginTabItem(\"Details\")") != std::string::npos);
	REQUIRE(hub.find("BeginTabItem(\"Assets\")") != std::string::npos);
	REQUIRE(hub.find("BeginTabItem(\"Primary Graph\")") != std::string::npos);
	REQUIRE(hub.find("BeginTabItem(\"Secondary Graph\")") != std::string::npos);
	REQUIRE(hub.find("BeginTabItem(\"Payloads\")") != std::string::npos);
	REQUIRE(hub.find("weaponRenderGraphBuilder(\"primary\", \"Primary Graph\"") !=
	        std::string::npos);
	REQUIRE(hub.find("weaponRenderGraphBuilder(\"secondary\", \"Secondary Graph\"") !=
	        std::string::npos);
	REQUIRE(hub.find("BeginTabItem(\"Template\")") == std::string::npos);
	REQUIRE(hub.find("weaponRenderTemplateEditor(scale);\n        ImGui::EndChild();") ==
	        std::string::npos);
	REQUIRE(hub.find("Show non-weapon meshes") != std::string::npos);
	REQUIRE(hub.find("Select Weapon Mesh") != std::string::npos);
	REQUIRE(hub.find("pdguiPopupDarkenBehind") != std::string::npos);
	REQUIRE(hub.find("ImGuiCol_PopupBg") != std::string::npos);
	REQUIRE(hub.find("weaponMeshEntryIsWeaponMesh") != std::string::npos);
	REQUIRE(hub.find("pdguiModelPreviewDrawEx") != std::string::npos);
	REQUIRE(hub.find("PDGUI_MP_WEAPON") != std::string::npos);
	REQUIRE(hub.find("ASSET_MODEL") != std::string::npos);
	/* Save hands the template and the edits to the mod author, which writes
	 * the clean layout; the creator writes no archive of its own. */
	REQUIRE(hub.find("modArchiveBegin(archivePath)") == std::string::npos);
	REQUIRE(hub.find("weaponCopyTemplatePayloads") == std::string::npos);
	REQUIRE(hub.find("weaponAddCatalogAssetArchive") == std::string::npos);
	REQUIRE(hub.find("weaponGraphEditorSplit(s_WeaponEditGraph") != std::string::npos);
	REQUIRE(hub.find("weaponGraphEditorCompose(") != std::string::npos);
	REQUIRE(hub.find("weaponGraphCompileWeaponSourceJson(") != std::string::npos);
	REQUIRE(hub.find("\"behavior/primary.graph.json\"") != std::string::npos);
	REQUIRE(hub.find("\"behavior/secondary.graph.json\"") != std::string::npos);
	REQUIRE(hub.find("\"behavior/shared-context.json\"") != std::string::npos);
	REQUIRE(hub.find("overrides[overrideCount].entry = s_WeaponEditTemplateModelFile") !=
	        std::string::npos);
	/* A template is never opened, or saved, with made-up behavior in place
	 * of its own. */
	REQUIRE(hub.find("WEAPONMOD.TEMPLATE.GRAPH_FAIL") != std::string::npos);
	REQUIRE(hub.find("WEAPONMOD.TEMPLATE.EDITOR_FAIL") != std::string::npos);
	REQUIRE(hub.find("WEAPON_IMPORT_PROJECTILE") != std::string::npos);
	REQUIRE(hub.find("WEAPON_IMPORT_ENTITY") != std::string::npos);
	REQUIRE(hub.find("Import Projectile") != std::string::npos);
	REQUIRE(hub.find("Import Entity") != std::string::npos);
	REQUIRE(hub.find("behavior_graph = behavior.graph.json") == std::string::npos);
	REQUIRE(hub.find("nested_payloads = nested_payloads.json") == std::string::npos);
	REQUIRE(hub.find("[references]") == std::string::npos);
	REQUIRE(hub.find("descriptor[0].key = \"dual_wieldable\"") != std::string::npos);
	REQUIRE(hub.find("weaponGraphValidateJson") != std::string::npos);
	/* The catalog name shown before saving is the mod author's own answer. */
	REQUIRE(hub.find("modAuthorNames(&request, &names") != std::string::npos);
	REQUIRE(hub.find("\"user.%s.weapon\"") == std::string::npos);
	REQUIRE(hub.find("\"user:%s\"") == std::string::npos);
	REQUIRE(hub.find("mods/Weapons/%s") != std::string::npos);
	REQUIRE(scanner.find("catalog_id = mod:weapon_catalog_name") !=
	        std::string::npos);
	REQUIRE(scanner.find("weapon_id = -1") == std::string::npos);
	REQUIRE(hub.find("pdguiFileBrowserOpen(label, \"mods\", filters)") !=
	        std::string::npos);
	REQUIRE(hub.find("ASSET_PROJECTILE") != std::string::npos);
	REQUIRE(hub.find("ASSET_ENTITY") != std::string::npos);
}

TEST_CASE("Modding Hub weapon tool builds visual graph modules without raw JSON authoring",
          "[modding][pdxxx][weapon_graph][ui][editor][c3814]") {
	const std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	const std::string editor =
		readFile("port/fast3d/pdgui_weapon_graph_node_editor.cpp");
	const std::string header =
		readFile("port/include/pdgui_weapon_graph_node_editor.h");
	REQUIRE(!hub.empty());
	REQUIRE(!editor.empty());
	REQUIRE(!header.empty());

	REQUIRE(hub.find("pdgui_weapon_graph_node_editor.h") != std::string::npos);
	REQUIRE(hub.find("Weapon Behavior Graph") != std::string::npos);
	REQUIRE(hub.find("pdguiWeaponGraphNodeEditorRender") != std::string::npos);
	REQUIRE(hub.find("weaponGraphBuilderLoadEditModelFromJson") != std::string::npos);
	REQUIRE(hub.find("s_WeaponGraphNodes") != std::string::npos);
	REQUIRE(hub.find("s_WeaponGraphEdges") != std::string::npos);
	REQUIRE(hub.find("weaponGraphBuilderSyncJson") != std::string::npos);
	REQUIRE(hub.find("s_WeaponGraphContextDefs") != std::string::npos);
	REQUIRE(hub.find("shared_context") != std::string::npos);
	REQUIRE(hub.find("subgraphs") != std::string::npos);
	REQUIRE(hub.find("context_refs") != std::string::npos);
	REQUIRE(hub.find("subgraph") != std::string::npos);
	REQUIRE(hub.find("Seed Single Shot") != std::string::npos);
	REQUIRE(hub.find("Seed Dual Fire Modes") != std::string::npos);
	REQUIRE(hub.find("Seed Automatic") != std::string::npos);
	REQUIRE(hub.find("Seed Projectile") != std::string::npos);
	REQUIRE(hub.find("Seed Mine Link") != std::string::npos);
	REQUIRE(hub.find("Seed Laptop Control") != std::string::npos);
	REQUIRE(editor.find("Shared Context") != std::string::npos);
	/* Settings that do not fit are an error; cut-off text would be saved as
	 * a broken weapon. The largest base weapon function needs about 850
	 * bytes. */
	REQUIRE(editor.find("has more settings text than the editor holds") !=
	        std::string::npos);
	REQUIRE(editor.find("the graph has more than %d nodes") != std::string::npos);
	REQUIRE(editor.find(".dump(0)") == std::string::npos);
	REQUIRE(header.find("#define PDGUI_WEAPON_GRAPH_PARAM_LEN  2048") !=
	        std::string::npos);
	REQUIRE(hub.find("\"event.trigger_pressed\"") != std::string::npos);
	REQUIRE(hub.find("\"event.trigger_held\"") != std::string::npos);
	REQUIRE(hub.find("\"event.trigger_released\"") != std::string::npos);
	REQUIRE(hub.find("weaponGraphBuilderAddTriggeredModuleInScope") !=
	        std::string::npos);
	REQUIRE(hub.find("Owner Player") != std::string::npos);
	REQUIRE(hub.find("Detonator Link") != std::string::npos);
	REQUIRE(hub.find("Target Policy Override") != std::string::npos);
	REQUIRE(hub.find("hacked_by_player") != std::string::npos);
	REQUIRE(hub.find("Advanced JSON") != std::string::npos);
	REQUIRE(hub.find("\\\"editor\\\"") != std::string::npos);
	REQUIRE(hub.find("\\\"layout\\\"") != std::string::npos);
	REQUIRE(hub.find("\"fire.hitscan\"") != std::string::npos);
	REQUIRE(hub.find("\"spawn.fired_projectile\"") != std::string::npos);
	REQUIRE(hub.find("\"special.remote_detonator\"") != std::string::npos);
	REQUIRE(hub.find("\"gate.target_lock\"") != std::string::npos);
	REQUIRE(hub.find("\\\"exports\\\"") != std::string::npos);
	REQUIRE(editor.find("imgui_node_editor.h") != std::string::npos);
	REQUIRE(editor.find("Weapon Behavior Graph Canvas") != std::string::npos);
	REQUIRE(editor.find("QueryNewLink") != std::string::npos);
	REQUIRE(editor.find("AcceptNewItem") != std::string::npos);
	REQUIRE(editor.find("renderPinSocket") != std::string::npos);
	REQUIRE(editor.find("InvisibleButton") != std::string::npos);
	REQUIRE(editor.find("##exec_out_pin") != std::string::npos);
	REQUIRE(editor.find("kWeaponGraphNodeWidth") != std::string::npos);
	REQUIRE(editor.find("BeginTable(\"##exec_pin_row\"") != std::string::npos);
	REQUIRE(editor.find("TableSetupColumn(\"##out_pin\"") != std::string::npos);
	REQUIRE(editor.find("PinPivotAlignment(ImVec2(0.0f, 0.5f))") !=
	        std::string::npos);
	REQUIRE(editor.find("PinPivotAlignment(ImVec2(1.0f, 0.5f))") !=
	        std::string::npos);
	REQUIRE(editor.find("linkColorForPinKind(2)") != std::string::npos);
	REQUIRE(editor.find("Connect Selected") != std::string::npos);
	REQUIRE(editor.find("Connect To") != std::string::npos);
	REQUIRE(editor.find("Visible links: %d/%d") != std::string::npos);
	REQUIRE(editor.find("Current Links") != std::string::npos);
	REQUIRE(editor.find("Remove Link") != std::string::npos);
	REQUIRE(editor.find("BeginDelete") != std::string::npos);
	REQUIRE(editor.find("Add Node") != std::string::npos);
	REQUIRE(editor.find("Delete Node") != std::string::npos);
	REQUIRE(editor.find("Duplicate Node") != std::string::npos);
	REQUIRE(editor.find("Set Primary") != std::string::npos);
	REQUIRE(editor.find("Set Secondary") != std::string::npos);
	REQUIRE(editor.find("Break Pin Links") != std::string::npos);
	REQUIRE(editor.find("Alt-click pin") != std::string::npos);
	REQUIRE(editor.find("Node Parameters") != std::string::npos);
	REQUIRE(editor.find("Advanced JSON") != std::string::npos);
	REQUIRE(editor.find("InputFloat") != std::string::npos);
	REQUIRE(editor.find("Checkbox") != std::string::npos);
	REQUIRE(editor.find("Node parameter updated") != std::string::npos);
	REQUIRE(editor.find("Node Context Refs") != std::string::npos);
	REQUIRE(editor.find("Choose a compatible node from the add-node menu") !=
	        std::string::npos);
	REQUIRE(editor.find("pdguiWeaponGraphModelLoadJson") != std::string::npos);
	REQUIRE(editor.find("scope_filter") != std::string::npos);
	REQUIRE(editor.find("nodeVisibleForScope") != std::string::npos);
	REQUIRE(editor.find("seedMissingNodeLayout") != std::string::npos);
	REQUIRE(editor.find("Links must stay inside the visible graph tab") !=
	        std::string::npos);
	REQUIRE(header.find("scope_filter") != std::string::npos);
	REQUIRE(header.find("scope_label") != std::string::npos);
	REQUIRE(header.find("PdWeaponGraphEditModel") != std::string::npos);
	REQUIRE(header.find("PD_WEAPON_GRAPH_EDITOR_ACTION_SET_PRIMARY") !=
	        std::string::npos);
}

TEST_CASE("shared gameplay graph editor foundation owns typed pins and adapter boundaries",
          "[modding][pdxxx][graph_editor][c3835]") {
	const std::string sharedHeader =
		readFile("port/include/pdgui_gameplay_graph_editor.h");
	const std::string sharedImpl =
		readFile("port/fast3d/pdgui_gameplay_graph_editor.cpp");
	const std::string weaponHeader =
		readFile("port/include/pdgui_weapon_graph_node_editor.h");
	const std::string weaponEditor =
		readFile("port/fast3d/pdgui_weapon_graph_node_editor.cpp");
	const std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(!sharedHeader.empty());
	REQUIRE(!sharedImpl.empty());
	REQUIRE(!weaponHeader.empty());
	REQUIRE(!weaponEditor.empty());
	REQUIRE(!hub.empty());

	REQUIRE(sharedHeader.find("PdGameplayGraphPinType") != std::string::npos);
	REQUIRE(sharedHeader.find("PdGameplayGraphEditorAdapter") != std::string::npos);
	REQUIRE(sharedHeader.find("pdguiGameplayGraphPinsCompatible") != std::string::npos);
	REQUIRE(sharedImpl.find("pdguiGameplayGraphLinkColor") != std::string::npos);
	REQUIRE(sharedImpl.find("PDGAMEPLAY_GRAPH_PIN_CONTEXT_REF") != std::string::npos);
	REQUIRE(weaponHeader.find("const PdGameplayGraphEditorAdapter *adapter") !=
	        std::string::npos);
	REQUIRE(weaponEditor.find("pdguiGameplayGraphCategoryColor(kind)") !=
	        std::string::npos);
	REQUIRE(weaponEditor.find("pinTypeForWeaponPinKind") != std::string::npos);
	REQUIRE(weaponEditor.find("pdguiGameplayGraphPinsCompatible") !=
	        std::string::npos);
	REQUIRE(hub.find("s_WeaponGraphEditorAdapter") != std::string::npos);
	REQUIRE(hub.find("\"behavior/primary.graph.json\"") != std::string::npos);
	REQUIRE(hub.find("\"behavior/secondary.graph.json\"") != std::string::npos);

	REQUIRE(pdguiGameplayGraphPinsCompatible(PDGAMEPLAY_GRAPH_PIN_EXEC,
		PDGAMEPLAY_GRAPH_PIN_OUTPUT, PDGAMEPLAY_GRAPH_PIN_EXEC,
		PDGAMEPLAY_GRAPH_PIN_INPUT));
	REQUIRE_FALSE(pdguiGameplayGraphPinsCompatible(PDGAMEPLAY_GRAPH_PIN_EXEC,
		PDGAMEPLAY_GRAPH_PIN_INPUT, PDGAMEPLAY_GRAPH_PIN_EXEC,
		PDGAMEPLAY_GRAPH_PIN_INPUT));
	REQUIRE(pdguiGameplayGraphPinsCompatible(PDGAMEPLAY_GRAPH_PIN_CATALOG_ID,
		PDGAMEPLAY_GRAPH_PIN_OUTPUT, PDGAMEPLAY_GRAPH_PIN_STRING,
		PDGAMEPLAY_GRAPH_PIN_INPUT));
	REQUIRE_FALSE(pdguiGameplayGraphPinsCompatible(PDGAMEPLAY_GRAPH_PIN_NUMBER,
		PDGAMEPLAY_GRAPH_PIN_OUTPUT, PDGAMEPLAY_GRAPH_PIN_ENTITY,
		PDGAMEPLAY_GRAPH_PIN_INPUT));

	ImVec4 execPin = pdguiGameplayGraphPinColor(PDGAMEPLAY_GRAPH_PIN_EXEC);
	ImVec4 execWire = pdguiGameplayGraphLinkColor(PDGAMEPLAY_GRAPH_PIN_EXEC,
		PDGAMEPLAY_GRAPH_PIN_EXEC);
	REQUIRE(execWire.x == execPin.x);
	REQUIRE(execWire.y == execPin.y);
	REQUIRE(execWire.z == execPin.z);
}

TEST_CASE("asset utility contracts cover clean typed archive families",
          "[modding][pdxxx][utilities][c3834]") {
	const std::string header = readFile("port/include/asset_mod_utility_contract.h");
	const std::string impl = readFile("port/src/asset_mod_utility_contract.c");
	const std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(!header.empty());
	REQUIRE(!impl.empty());
	REQUIRE(!hub.empty());

	REQUIRE(header.find("ASSET_MOD_UTIL_CREATE") != std::string::npos);
	REQUIRE(header.find("ASSET_MOD_UTIL_IMPORT") != std::string::npos);
	REQUIRE(header.find("ASSET_MOD_UTIL_CLONE") != std::string::npos);
	REQUIRE(header.find("ASSET_MOD_UTIL_EDIT") != std::string::npos);
	REQUIRE(header.find("ASSET_MOD_UTIL_VALIDATE") != std::string::npos);
	REQUIRE(header.find("ASSET_MOD_UTIL_PACKAGE") != std::string::npos);
	REQUIRE(impl.find(".pdweapon") != std::string::npos);
	REQUIRE(impl.find(".pdscenario") != std::string::npos);
	REQUIRE(impl.find(".pdbotprofile") != std::string::npos);
	REQUIRE(impl.find(".pdtool") != std::string::npos);
	REQUIRE(hub.find("Archive Utility Contracts") != std::string::npos);
	REQUIRE(hub.find("assetModUtilityContractCount()") != std::string::npos);

	const asset_mod_utility_contract_t *weapon =
		assetModUtilityContractForExtension(".pdweapon");
	REQUIRE(weapon != nullptr);
	REQUIRE(std::string(weapon->descriptor) == "weapon.ini");
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_CREATE));
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_IMPORT));
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_CLONE));
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_EDIT));
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_VALIDATE));
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_PACKAGE));
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_HOT_ENABLE));
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_EMBED_DEPS));

	const asset_mod_utility_contract_t *bot =
		assetModUtilityContractForExtension(".pdbotprofile");
	REQUIRE(bot != nullptr);
	REQUIRE(assetModUtilitySupports(bot, ASSET_MOD_UTIL_TEMPLATE));
	REQUIRE(std::string(bot->cli_noun) == "bot-profile");

	const asset_mod_utility_contract_t *tool =
		assetModUtilityContractForExtension(".pdtool");
	REQUIRE(tool != nullptr);
	REQUIRE(assetModUtilitySupports(tool, ASSET_MOD_UTIL_SECURE_TOOL));
	REQUIRE_FALSE(assetModUtilitySupports(tool, ASSET_MOD_UTIL_CREATE));

	for (size_t i = 0; i < assetModUtilityContractCount(); i++) {
		const asset_mod_utility_contract_t *c = assetModUtilityContractAt(i);
		REQUIRE(c != nullptr);
		REQUIRE(c->extension[0] == '.');
		REQUIRE(std::string(c->descriptor).find(".ini") != std::string::npos);
		REQUIRE(assetModUtilitySupports(c, ASSET_MOD_UTIL_VALIDATE));
		REQUIRE(assetModUtilitySupports(c, ASSET_MOD_UTIL_PACKAGE));
	}
}

TEST_CASE("weapon graph parity modules wrap OG behavior families before retirement",
          "[modding][pdxxx][weapon_graph][parity][c3840]") {
	const std::string runtimeHeader = readFile("port/include/weapon_graph_runtime.h");
	const std::string runtimeImpl = readFile("port/src/weapon_graph_runtime.c");
	REQUIRE(!runtimeHeader.empty());
	REQUIRE(!runtimeImpl.empty());

	REQUIRE(runtimeHeader.find("weapon_graph_parity_module") != std::string::npos);
	REQUIRE(runtimeHeader.find("parity_module") != std::string::npos);
	REQUIRE(runtimeImpl.find("s_parity_modules") != std::string::npos);
	REQUIRE(runtimeImpl.find("og.projectile.motion") != std::string::npos);
	REQUIRE(runtimeImpl.find("og.entity.autogun") != std::string::npos);
	REQUIRE(runtimeImpl.find("og.entity.proxy_trigger") != std::string::npos);
	REQUIRE(runtimeImpl.find("og.fire.hitscan") != std::string::npos);

	const weapon_graph_parity_module_t *hitscan =
		weaponGraphParityModuleForOpcode(WEAPON_GRAPH_OP_FIRE_HITSCAN);
	REQUIRE(hitscan != nullptr);
	REQUIRE(std::string(hitscan->module_name) == "og.fire.hitscan");
	REQUIRE(std::string(hitscan->behavior_family) == "held_hitscan");
	REQUIRE(std::string(hitscan->legacy_backend).find("bondgun.c") !=
	        std::string::npos);

	const weapon_graph_parity_module_t *motion =
		weaponGraphParityModuleForOpcode(WEAPON_GRAPH_OP_PROJECTILE_MOTION);
	REQUIRE(motion != nullptr);
	REQUIRE(std::string(motion->module_name) == "og.projectile.motion");
	REQUIRE(std::string(motion->legacy_backend).find("propobj.c") !=
	        std::string::npos);

	const weapon_graph_parity_module_t *autogun =
		weaponGraphParityModuleForOpcode(WEAPON_GRAPH_OP_ENTITY_AUTOGUN);
	REQUIRE(autogun != nullptr);
	REQUIRE(std::string(autogun->module_name) == "og.entity.autogun");

	REQUIRE(weaponGraphParityModuleForOpcode(
		WEAPON_GRAPH_OP_EVENT_TRIGGER_PRESSED) == nullptr);
	REQUIRE(std::string(weaponGraphParityModuleNameForOpcode(
		WEAPON_GRAPH_OP_EVENT_TRIGGER_PRESSED)).empty());

	weaponGraphRuntimeClearAll();
	char err[256] = "";
	const char *weaponJson =
		"{ \"schema\": \"pd.weapon_graph.v1\","
		"  \"asset_id\": \"user:weapon_parity\","
		"  \"graph_id\": \"primary\","
		"  \"nodes\": ["
		"    { \"id\": \"fire\", \"kind\": \"fire.hitscan\","
		"      \"params\": { \"mode\": \"primary\", \"damage\": 8.0 } }"
		"  ],"
		"  \"edges\": [],"
		"  \"exports\": [ { \"name\": \"primary\", \"node\": \"fire\" } ] }";
	weapon_graph_ir_t ir;
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, weaponJson,
		(u32)std::strlen(weaponJson), &ir, err, sizeof(err)) == 0);
	REQUIRE(weaponGraphRuntimeRegisterHeldIr(7, &ir, err, sizeof(err)) == 0);
	const weapon_graph_held_function_t *held =
		weaponGraphRuntimeGetHeldFunction(7, 0);
	REQUIRE(held != nullptr);
	REQUIRE(std::string(held->parity_module) == "og.fire.hitscan");

	const char *projectileJson =
		"{ \"schema\": \"pd.projectile_graph.v1\","
		"  \"asset_id\": \"user:projectile_parity\","
		"  \"graph_id\": \"projectile\","
		"  \"nodes\": ["
		"    { \"id\": \"motion\", \"kind\": \"projectile.motion\","
		"      \"params\": { \"motion_kind\": \"powered\", \"speed\": 20.0 } }"
		"  ],"
		"  \"edges\": [],"
		"  \"exports\": [] }";
	REQUIRE(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE,
		"user:projectile_parity", projectileJson,
		(u32)std::strlen(projectileJson), err, sizeof(err)) == 0);
	const weapon_graph_projectile_runtime_t *projectile =
		weaponGraphRuntimeGetProjectile("user:projectile_parity");
	REQUIRE(projectile != nullptr);
	REQUIRE(std::string(projectile->parity_module) == "og.projectile.motion");
}

TEST_CASE("Modding Hub vendors imgui-node-editor for the in-game graph canvas",
          "[modding][pdxxx][weapon_graph][ui][editor][vendor][c3814]") {
	const std::string cmake = readFile("CMakeLists.txt");
	const std::string license =
		readFile("port/external/imgui-node-editor/LICENSE");
	const std::string api =
		readFile("port/external/imgui-node-editor/imgui_node_editor.h");
	const std::string backend = readFile("port/fast3d/pdgui_backend.cpp");
	REQUIRE(!cmake.empty());
	REQUIRE(!license.empty());
	REQUIRE(!api.empty());
	REQUIRE(!backend.empty());

	REQUIRE(cmake.find("port/external/imgui-node-editor") != std::string::npos);
	REQUIRE(license.find("MIT License") != std::string::npos);
	REQUIRE(api.find("CreateEditor") != std::string::npos);
	REQUIRE(api.find("QueryNewLink") != std::string::npos);
	REQUIRE(api.find("ShowBackgroundContextMenu") != std::string::npos);
	REQUIRE(backend.find("pdguiWeaponGraphNodeEditorInit") != std::string::npos);
	REQUIRE(backend.find("pdguiWeaponGraphNodeEditorShutdown") !=
	        std::string::npos);
}

TEST_CASE("base arena extractor emits zip-openable pdarena archives",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string arena = readFile("port/src/romextract_pdarena.c");
	REQUIRE(!arena.empty());

	REQUIRE(arena.find("s_existingZipArchive") != std::string::npos);
	REQUIRE(arena.find("modArchiveBeginDerived(full)") != std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddDescriptor(&asset_writer, \"arena.ini\"") != std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddManifestJson(&asset_writer") != std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterFinishMetadata(&asset_writer)") != std::string::npos);
	REQUIRE(arena.find("s_addScenarioArchiveDependency") != std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicDisk(writer, dst_name") !=
	        std::string::npos);
	REQUIRE(arena.find("\"dependencies/assets/scenarios/\"") != std::string::npos);
	REQUIRE(arena.find("scenario_archive = dependencies/assets/scenarios/%s.pdscenario") !=
	        std::string::npos);
	REQUIRE(arena.find("s_copyArchiveEntriesWithPrefix") == std::string::npos);
	REQUIRE(arena.find("\"scenario/scenario.ini\"") == std::string::npos);
	REQUIRE(arena.find("\"scenario/rooms.obj\"") == std::string::npos);
	REQUIRE(arena.find("\"scenario/visual/scene.obj\"") == std::string::npos);
	REQUIRE(arena.find("scenario_root = scenario") == std::string::npos);
	const auto scenarioEmit = arena.find("s_emitOnePdscenario(a, c->scenarios_dir");
	const auto arenaEmit = arena.find("s_emitOnePdarena(a, i, c->arenas_dir");
	REQUIRE(scenarioEmit != std::string::npos);
	REQUIRE(arenaEmit != std::string::npos);
	REQUIRE(scenarioEmit < arenaEmit);
	REQUIRE(arena.find("fsFileOpenWrite(relpath)") == std::string::npos);
	REQUIRE(arena.find("Emit one .pdarena JSON") == std::string::npos);
}

TEST_CASE("base scenario extractor emits standard map and text payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string arena = readFile("port/src/romextract_pdarena.c");
	const std::string conformance = readFile("tools/asset_archive_conformance.py");
	const std::string scenario_smoke =
		readFile("tools/smoke-verify/tests/scenario_pads_source_gate_smoke.json");
	REQUIRE(!arena.empty());
	REQUIRE(!conformance.empty());
	REQUIRE(!scenario_smoke.empty());
	REQUIRE(conformance.find("\"*.tsv\"") != std::string::npos);
	REQUIRE(conformance.find("read_tsv_rows") == std::string::npos);
	REQUIRE(conformance.find("count_tsv_data_rows") == std::string::npos);

	REQUIRE(arena.find("s_buildTilesExports") != std::string::npos);
	REQUIRE(arena.find("s_buildPadsJson") != std::string::npos);
	REQUIRE(arena.find("s_buildWordsTsv") == std::string::npos);
	REQUIRE(arena.find("#include \"lib/rzip.h\"") != std::string::npos);
	REQUIRE(arena.find("rzipIs1173") != std::string::npos);
	REQUIRE(arena.find("rzipInflate") != std::string::npos);
	REQUIRE(arena.find("preprocessTilesFile") != std::string::npos);
	REQUIRE(arena.find("preprocessPadsFile") != std::string::npos);
	REQUIRE(arena.find("s_pdarenaWork(i, &actx, verified);") != std::string::npos);
	REQUIRE(arena.find("s_pdscenarioStageWork(spec, &actx, verified || deferred);") != std::string::npos);
	REQUIRE(arena.find("bootPoolForRangeBlocking(0, count, s_hashStageTextureSource") != std::string::npos);
	REQUIRE(arena.find("s_buildBgVisualExports") != std::string::npos);
	REQUIRE(arena.find("preprocessBgSection1") != std::string::npos);
	REQUIRE(arena.find("preprocessBgRoom") != std::string::npos);
	REQUIRE(arena.find("G_NOOP") != std::string::npos);
	REQUIRE(arena.find("G_TRI1") != std::string::npos);
	REQUIRE(arena.find("G_TRI4") != std::string::npos);
	REQUIRE(arena.find("texInflateZlib") != std::string::npos);
	REQUIRE(arena.find("texInflateNonZlib") != std::string::npos);
	REQUIRE(arena.find("scene.glb") != std::string::npos);
	REQUIRE(arena.find("stbi_write_png_to_mem") != std::string::npos);
	REQUIRE(arena.find("bg_visual_scene_glb_v12_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_alphamask_materialextras_dualtex_alphablend") != std::string::npos);
	REQUIRE(arena.find("\\\"alphaMode\\\":\\\"MASK\\\",\\\"alphaCutoff\\\":0.01") != std::string::npos);
	REQUIRE(arena.find("s_existingArchiveEntryContains(check, \"scene.glb\",") !=
	        std::string::npos);
	REQUIRE(arena.find("PDSCENARIO_BG_VISUAL_EXPORT_VERSION)") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"texCoord\\\":0") != std::string::npos);
	REQUIRE(arena.find("s_bgMaterialUvScaleForGlb") != std::string::npos);
	REQUIRE(arena.find("s_bgMaterialGlbAuthorUv") != std::string::npos);
	REQUIRE(arena.find("s_bgMaterialGlbUv") != std::string::npos);
	REQUIRE(arena.find("\\\"TEXCOORD_1\\\":%u") != std::string::npos);
	REQUIRE(arena.find("\\\"COLOR_0\\\":%u") != std::string::npos);
	REQUIRE(arena.find("\\\"baseColorTexture\\\":{\\\"index\\\":%d,\\\"texCoord\\\":0}") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"extras\\\":{\\\"pd2_material\\\":") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"secondaryTexture\\\":{\\\"index\\\":%d,\\\"texCoord\\\":1}") !=
	        std::string::npos);
	REQUIRE(arena.find("s_bgMaterialTextureCommandName") != std::string::npos);
	REQUIRE(arena.find("s_bgGltfWrapMode") != std::string::npos);
	REQUIRE(arena.find("op == (u8)G_TEXTURE") != std::string::npos);
	REQUIRE(arena.find("m->shifts <= 10") != std::string::npos);
	REQUIRE(arena.find("m->shiftt <= 10") != std::string::npos);
	REQUIRE(arena.find("\\\"sampler\\\":%u,\\\"source\\\":%d") != std::string::npos);
	const std::string filebg = readFile("port/src/preprocess/filebg.c");
	const auto empty_room_guard =
		filebg.find("!dst_header->vertices && !dst_header->colours");
	const auto ptr_vertices_warn = filebg.find("ptr_vertices yields");
	REQUIRE(empty_room_guard != std::string::npos);
	REQUIRE(filebg.find("!dst_header->opablocks && !dst_header->xlublocks",
			empty_room_guard) != std::string::npos);
	REQUIRE(filebg.find("dst_header->numvertices == 0 && dst_header->numcolours == 0",
			empty_room_guard) != std::string::npos);
	REQUIRE(filebg.find("sizeof(struct roomgfxdata) - sizeof(struct roomblock)",
			empty_room_guard) != std::string::npos);
	REQUIRE(ptr_vertices_warn != std::string::npos);
	REQUIRE(empty_room_guard < ptr_vertices_warn);
	REQUIRE(conformance.find("validate_scene_glb_texture_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("TEXCOORD_0 range") != std::string::npos);
	REQUIRE(conformance.find("DCC-authoring UV range") != std::string::npos);
	REQUIRE(conformance.find("TEXCOORD_1 runtime UVs") != std::string::npos);
	REQUIRE(conformance.find("COLOR_0 vertex colors") != std::string::npos);
	REQUIRE(conformance.find("visible textures to TEXCOORD_0") !=
	        std::string::npos);
	REQUIRE(arena.find("mat_%03u_tex_%04x") != std::string::npos);
	REQUIRE(arena.find("texture_inventory_%04x") == std::string::npos);
	REQUIRE(arena.find("materials_tsv") == std::string::npos);
	REQUIRE(arena.find("tiles_tsv") == std::string::npos);
	REQUIRE(scenario_smoke.find(".tsv") == std::string::npos);
	REQUIRE(scenario_smoke.find("BG\\\\.ROOMS: entered room list missing terminator") !=
	        std::string::npos);
	REQUIRE(scenario_smoke.find("pads.json") != std::string::npos);
	REQUIRE(scenario_smoke.find("ai/ailists") != std::string::npos);
	REQUIRE(scenario_smoke.find("objectives") != std::string::npos);
	REQUIRE(arena.find("strncmp(mtl_texture_path, \"visual/\", 7)") !=
	        std::string::npos);
	REQUIRE(arena.find("scene_file = scene.glb") !=
	        std::string::npos);
	REQUIRE(arena.find("runtime_source_file = scene.glb") !=
	        std::string::npos);
	REQUIRE(arena.find("collision_source = collision.obj") !=
	        std::string::npos);
	REQUIRE(arena.find("collision_fallback = override") !=
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"collision.obj\"") !=
	        std::string::npos);
	REQUIRE(arena.find("blender_scene_file = scene.glb") !=
	        std::string::npos);
	REQUIRE(arena.find("pads.json") != std::string::npos);
	REQUIRE(arena.find("spawns.json") != std::string::npos);
	REQUIRE(arena.find("volumes.json") != std::string::npos);
	REQUIRE(arena.find("objects.json") != std::string::npos);
	REQUIRE(arena.find("ai/ailists.json") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.lists.source") != std::string::npos);
	REQUIRE(arena.find("scenario.pads.source") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_return_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_shot_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.return_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.face_entity") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.apply_gset_damage") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_damage_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.consider_grenade_throw") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.drop_item") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_run_from_target") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_jog_to_target_prop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_walk_to_target_prop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_run_to_target_prop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_go_to_cover_prop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_jog_to_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_walk_to_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_run_to_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_punch_dodge_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_shooting_at_me_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_dark_room_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_player_dead_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.jog_to_pad") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.go_to_pad_preset") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.walk_to_pad") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.run_to_pad") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_path") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.start_patrol") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_start_alarm") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.activate_alarm") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.deactivate_alarm") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_morale") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.add_morale") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_add_morale") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.subtract_morale") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_alertness") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.add_alertness") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_add_alertness") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.subtract_alertness") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.increase_squadron_alertness") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_hear_distance") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_view_distance") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_grenade_probability") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_chr_num") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_max_damage") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.add_health") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_shield") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_reaction_speed") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_recovery_speed") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_accuracy") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_dodge_rating") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_unarmed_dodge_rating") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.unset_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_has_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_set_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_unset_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_chr_has_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_stage_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.unset_stage_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_stage_flag_eq") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.open_door") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.close_door") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_door_state") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_object_is_door") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.lock_door") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.unlock_door") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_door_locked") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_lift_stationary") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.lift_go_to_stop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_lift_at_stop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.activate_lift") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_using_lift") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.configure_rain") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.configure_snow") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.switch_to_alt_sky") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_wind_speed") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_lights") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_room_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.show_cutscene_chrs") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.configure_environment") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_distance_to_target2_less_than") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_distance_to_target2_greater_than") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.play_sound_from_prop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.play_temporary_primary_track") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.play_x_track") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.stop_ambient_track") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_draw_weapon") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_draw_weapon_in_cutscene") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_player_force_speed") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_set_invincible") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_player_is_invincible") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_chr_has_no_gun") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_delete_weapon") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_trigger_shot_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.end_level") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.end_cutscene") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.warp_jo_to_pad") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.warp_jo_to_tag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.revoke_control") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.grant_control") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.player_fade_in") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.players_fade_out") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_colour_fade_complete") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.prepare_warp_orbit") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.begin_warp_latch") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_warp_latch_complete") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_camera_animation") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_in_cutscene") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_cutscene_button_pressed") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.reorient_for_cutscene_stop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.spawn_chr_at_pad") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.spawn_chr_at_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_equip_weapon") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_equip_hat") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_obj_image") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.object_do_animation") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_door_open") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.duplicate_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.enable_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.disable_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.enable_obj") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.disable_obj") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_move_to_pad") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_set_team") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.damage_chr_by_amount") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.do_preset_animation") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_player_chr_portal_distance_less_than") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_chr_reposition_valid") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.do_gun_command") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_distance_to_gun_less_than") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.recover_gun") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_copy_properties") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.player_auto_walk") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_player_auto_walk_finished") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_obj_in_room") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_kill") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.remove_weapon_from_inventory") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_music_event_queue_is_empty") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_coop_mode") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.remove_references_to_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_toggle_model_part") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.obj_set_model_part_visible") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_obj_health_less_than") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_obj_health") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_chr_special_death_animation") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_room_to_search") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_savefile_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.unset_savefile_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_savefile_flag_set") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_savefile_flag_unset") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.show_hudmsg") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.show_hudmsg_middle") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.show_hudmsg_top_middle") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.hovercar_begin_path") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_vehicle_speed") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_rotor_speed") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_explosions") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_tinted_glass_enabled") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.hovercopter_fire_rocket") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_adjust_motion_blur") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.punch_or_kick") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_target_to_eyespy_if_in_sight") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.mini_skedar_try_pounce") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_object_distance_to_pad_less_than") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.avoid") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.title_init_mode") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_exit_title") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_emit_sparks") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_dr_caroll_images") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.say_quip") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.say_ci_staff_quip") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.shuffle_ruins_pillars") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.shuffle_pelagic_switches") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_action") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_team_orders") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.retreat") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.find_cover") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.find_cover_within_dist") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.find_cover_outside_dist") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.go_to_cover") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.check_cover_out_of_sight") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.orbit_target") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_chr_preset_to_unalerted_teammate") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_squadron") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.face_cover") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.danger_cover") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.release_cover") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.rebuild_teams") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.rebuild_squadrons") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_set_listening") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_attack_amount") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_chr_preset") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_chr_target") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_pad_preset") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_set_pad_preset") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_copy_pad_preset") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_can_hear_alarm") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_patrolling") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_alarm_active") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_gas_active") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_hears_target") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_saw_injury") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_saw_death") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_los_to_target") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_los_to_attack_target") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_target_nearly_in_sight") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_nearly_in_targets_sight") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_pad_preset_to_pad_on_route_to_target") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_saw_target_recently") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_heard_target_recently") != std::string::npos);
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
		     "scenario.ai.condition.if_dangerous_object_nearby",
		     "scenario.ai.condition.if_heli_weapons_armed",
		     "scenario.ai.condition.if_hoverbot_next_step",
		     "scenario.ai.action.shuffle_investigation_terminals",
		     "scenario.ai.action.set_pad_preset_to_investigation_terminal",
		     "scenario.ai.action.heli_arm_weapons",
		     "scenario.ai.action.heli_unarm_weapons",
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
		     "scenario.ai.condition.if_waypoint_within_quadrant",
		     "scenario.ai.action.set_pad_preset_to_target_quadrant",
	     }) {
		REQUIRE(arena.find(kind) != std::string::npos);
	}
	REQUIRE(arena.find("ai_lists_file = ai/ailists.json") != std::string::npos);
	REQUIRE(arena.find("navigation/paths.json") != std::string::npos);
	REQUIRE(arena.find("scenario.navigation.paths.source") != std::string::npos);
	REQUIRE(arena.find("paths_file = navigation/paths.json") != std::string::npos);
	REQUIRE(arena.find("objectives.json") != std::string::npos);
	REQUIRE(arena.find("navigation.ini") != std::string::npos);
	REQUIRE(arena.find("level.graph.json") != std::string::npos);
	REQUIRE(arena.find("_meta/generated-collision.json") != std::string::npos);
	REQUIRE(arena.find("_meta/generated-navmesh.json") != std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"setup.tsv\"") ==
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"mpsetup.tsv\"") ==
	        std::string::npos);
	REQUIRE(arena.find("s_buildSetupTsv") == std::string::npos);
	REQUIRE(arena.find("s_buildMpSetupTsv") == std::string::npos);
	REQUIRE(arena.find("visual_segments.tsv") == std::string::npos);
	REQUIRE(arena.find("geometry_file = rooms.obj") == std::string::npos);
	REQUIRE(arena.find("geometry_format = OBJ") == std::string::npos);
	REQUIRE(arena.find("visual_scene_file = visual/scene.obj") ==
	        std::string::npos);
	REQUIRE(arena.find("visual_material_file = visual/scene.mtl") ==
	        std::string::npos);
	REQUIRE(arena.find("texture_manifest_file = visual/materials.tsv") ==
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntry(check, \"scene.glb\")") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntry(check, \"level.graph.json\")") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntry(check, \"rooms.obj\")") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntry(check, \"tiles.tsv\")") !=
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"scene.glb\"") !=
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"level.graph.json\"") !=
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"rooms.obj\"") ==
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"visual/scene.obj\"") ==
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
	        std::string::npos);
	REQUIRE(arena.find("map_material.png") == std::string::npos);
	REQUIRE(arena.find("data:application/octet-stream;base64,") ==
	        std::string::npos);

	REQUIRE(arena.find("geometry_file = geometry.bin") == std::string::npos);
	REQUIRE(arena.find("\\\"geometry\\\": \\\"geometry.bin\\\"") ==
	        std::string::npos);
	REQUIRE(arena.find("tiles_file = tiles.bin") == std::string::npos);
	REQUIRE(arena.find("pads_file = pads.bin") == std::string::npos);
	REQUIRE(arena.find("setup_file = setup.bin") == std::string::npos);
	REQUIRE(arena.find("mpsetup_file = mpsetup.bin") == std::string::npos);
	REQUIRE(arena.find("s_addStageBin") == std::string::npos);
	REQUIRE(arena.find("modArchiveAddFileDisk") == std::string::npos);
}

TEST_CASE("base character extractors emit zip-openable pdhead and pdbody archives",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string head = readFile("port/src/romextract_pdhead.c");
	const std::string body = readFile("port/src/romextract_pdbody.c");
	REQUIRE(!head.empty());
	REQUIRE(!body.empty());

	REQUIRE(head.find("s_existingZipArchive") != std::string::npos);
	REQUIRE(head.find("modArchiveBeginDerived(full)") != std::string::npos);
	REQUIRE(head.find("assetArchiveWriterAddDescriptor(&asset_writer, \"head.ini\"") != std::string::npos);
	REQUIRE(head.find("assetArchiveWriterAddManifestJson(&asset_writer") != std::string::npos);
	REQUIRE(head.find("s_addRequiredArchiveFile(&asset_writer, \"mesh.pdmesh\"") !=
	        std::string::npos);
	REQUIRE(head.find("assetArchiveWriterFinishMetadata(&asset_writer)") != std::string::npos);
	REQUIRE(head.find("s_existingArchiveHasEntry(relpath, \"mesh.pdmesh\")") !=
	        std::string::npos);
	REQUIRE(head.find("PDHEAD_FAST_CACHE_KIND") != std::string::npos);
	REQUIRE(head.find("s_existingArchiveEntryContains(relpath, \"head.ini\", \"headnum\")") !=
	        std::string::npos);
	REQUIRE(head.find("headnum = %d") == std::string::npos);
	REQUIRE(head.find("mesh = %s") == std::string::npos);
	REQUIRE(head.find("mesh_catalog_id = %s") != std::string::npos);
	REQUIRE(head.find("mesh_archive = %s") != std::string::npos);
	REQUIRE(head.find("\\\"mesh_archive\\\": \\\"mesh.pdmesh\\\"") !=
	        std::string::npos);
	REQUIRE(head.find("fsFileOpenWrite(relpath)") == std::string::npos);
	REQUIRE(head.find("emits one .pdhead JSON file") == std::string::npos);

	REQUIRE(body.find("s_existingZipArchive") != std::string::npos);
	REQUIRE(body.find("modArchiveBeginDerived(full)") != std::string::npos);
	REQUIRE(body.find("assetArchiveWriterAddDescriptor(&asset_writer, \"body.ini\"") != std::string::npos);
	REQUIRE(body.find("assetArchiveWriterAddManifestJson(&asset_writer") != std::string::npos);
	REQUIRE(body.find("s_addRequiredArchiveFile(&asset_writer, \"mesh.pdmesh\"") !=
	        std::string::npos);
	REQUIRE(body.find("s_addRequiredArchiveFile(&asset_writer, \"hand.pdmesh\"") !=
	        std::string::npos);
	REQUIRE(body.find("assetArchiveWriterFinishMetadata(&asset_writer)") != std::string::npos);
	REQUIRE(body.find("s_existingArchiveHasEntry(relpath, \"mesh.pdmesh\")") !=
	        std::string::npos);
	REQUIRE(body.find("s_existingArchiveHasEntry(relpath, \"hand.pdmesh\")") !=
	        std::string::npos);
	REQUIRE(body.find("PDBODY_FAST_CACHE_KIND") != std::string::npos);
	REQUIRE(body.find("s_existingArchiveEntryContains(relpath, \"body.ini\", \"bodynum\")") !=
	        std::string::npos);
	REQUIRE(body.find("bodynum = %d") == std::string::npos);
	REQUIRE(body.find("mesh = %s") == std::string::npos);
	REQUIRE(body.find("hand = %s") == std::string::npos);
	REQUIRE(body.find("mesh_catalog_id = %s") != std::string::npos);
	REQUIRE(body.find("hand_catalog_id = %s") != std::string::npos);
	REQUIRE(body.find("mesh_archive = %s") != std::string::npos);
	REQUIRE(body.find("hand_archive = hand.pdmesh") != std::string::npos);
	REQUIRE(body.find("\\\"mesh_archive\\\": \\\"mesh.pdmesh\\\"") !=
	        std::string::npos);
	REQUIRE(body.find("\\\"hand_archive\\\": \\\"hand.pdmesh\\\"") !=
	        std::string::npos);
	REQUIRE(body.find("fsFileOpenWrite(relpath)") == std::string::npos);
	REQUIRE(body.find("emits one .pdbody JSON file") == std::string::npos);
}

TEST_CASE("base character extractor emits canonical pdcharacter archives",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string character = readFile("port/src/romextract_pdcharacter.c");
	const std::string header = readFile("port/include/romextract_pd.h");
	const std::string main = readFile("port/src/main.c");
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	const std::string packer = readFile("port/src/modpack_pdmod.c");
	const std::string policy = readFile("port/src/asset_archive_policy.c");
	REQUIRE(!character.empty());

	REQUIRE(character.find(".pdcharacter") != std::string::npos);
	REQUIRE(character.find("assetArchiveWriterAddDescriptor(&asset_writer, \"character.ini\"") !=
	        std::string::npos);
	REQUIRE(character.find("assetArchiveWriterAddPublicDisk(writer, inner, full") !=
	        std::string::npos);
	REQUIRE(character.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
	        std::string::npos);
	REQUIRE(character.find("\"body.pdbody\"") != std::string::npos);
	REQUIRE(character.find("\"head.pdhead\"") != std::string::npos);
	REQUIRE(character.find("\\\"dependency_closure\\\": \\\"embedded.v2\\\"") !=
	        std::string::npos);
	REQUIRE(character.find("dependency_closure = embedded.v2") !=
	        std::string::npos);
	REQUIRE(character.find("s_existingArchiveEntryContains") !=
	        std::string::npos);
	REQUIRE(character.find("s_existingArchiveHasEntry(dst_rel, \"body.pdbody\")") !=
	        std::string::npos);
	REQUIRE(character.find("s_existingArchiveHasEntry(dst_rel, \"head.pdhead\")") !=
	        std::string::npos);
	REQUIRE(character.find("romextract pdcharacter") != std::string::npos);
	REQUIRE(header.find("romExtractAllPdcharacter") != std::string::npos);
	REQUIRE(main.find("romExtractAllPdcharacter(0)") != std::string::npos);
	REQUIRE(scanner.find("assetArchiveTypeForPath(path)") != std::string::npos);
	REQUIRE(policy.find("\".pdcharacter\"") != std::string::npos);
	REQUIRE(policy.find("\"character.ini\"") != std::string::npos);
	REQUIRE(packer.find("\"characters\", \"character.ini\", \"character\"") !=
	        std::string::npos);

	REQUIRE(character.find(".pdweapon") == std::string::npos);
}

TEST_CASE("base non-weapon zip emitters include root editable descriptors",
          "[modding][pdxxx][base][static][c3812]") {
	struct DescriptorEmitter {
		const char *path;
		const char *descriptor;
	};
	const DescriptorEmitter emitters[] = {
		{ "port/src/romextract_pdmesh.c", "mesh.ini" },
		{ "port/src/romextract_pdanim.c", "animation.ini" },
		{ "port/src/romextract_pdanim_chr.c", "animation.ini" },
		{ "port/src/romextract_pdsfx.c", "sound.ini" },
		{ "port/src/romextract_pdsfx.c", "voice.ini" },
		{ "port/src/romextract_pdsong.c", "music.ini" },
		{ "port/fast3d/pdgui_theme.cpp", "ui.ini" },
		{ "port/src/romextract_pdfont.c", "font.ini" },
		{ "port/src/romextract_pdlang.c", "lang.ini" },
		{ "port/src/romextract_pdarena.c", "scenario.ini" },
	};

	for (const DescriptorEmitter &emitter : emitters) {
		const std::string src = readFile(emitter.path);
		INFO(emitter.path);
		INFO(emitter.descriptor);
		REQUIRE(!src.empty());
		REQUIRE(src.find(emitter.descriptor) != std::string::npos);
		const bool literalAdd =
			src.find(std::string("modArchiveAddFileMem(aw, \"") +
			         emitter.descriptor + "\"") != std::string::npos;
		const bool descriptorVariableAdd =
			src.find("modArchiveAddFileMem(aw, descriptor_name") !=
			std::string::npos;
		const bool sharedDescriptorAdd =
			src.find("assetArchiveWriterAddDescriptor") !=
			std::string::npos;
		REQUIRE((literalAdd || descriptorVariableAdd || sharedDescriptorAdd));
	}
}

TEST_CASE("base audio extractors emit accessible wav payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string audio = readFile("port/src/romextract_pdsfx.c");
	REQUIRE(!audio.empty());

	REQUIRE(audio.find("s_buildWavPayload") != std::string::npos);
	REQUIRE(audio.find("s_decodeAdpcmFrame") != std::string::npos);
	REQUIRE(audio.find("AL_ADPCM_WAVE") != std::string::npos);
	REQUIRE(audio.find("AL_RAW16_WAVE") != std::string::npos);
	REQUIRE(audio.find("assetArchiveWriterAddPublicMem(&asset_writer, \"sample.wav\"") !=
	        std::string::npos);
	REQUIRE(audio.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
	        std::string::npos);
	REQUIRE(audio.find("assetArchiveWriterAddDescriptor(&asset_writer, descriptor_name") !=
	        std::string::npos);
	REQUIRE(audio.find("s_archiveEntryMatches(arc, \"sample.wav\", wav, wav_size)") !=
	        std::string::npos);
	REQUIRE(audio.find("s_existingArchiveEntryContains(dst_rel, \"_meta/manifest.json\"") !=
	        std::string::npos);
	REQUIRE(audio.find("file_path = sample.wav") != std::string::npos);
	REQUIRE(audio.find("\\\"data\\\": \\\"sample.wav\\\"") !=
	        std::string::npos);
	REQUIRE(audio.find("source_format = %s") != std::string::npos);
	REQUIRE(audio.find("key_base = %u") != std::string::npos);
	REQUIRE(audio.find("\\\"key_base\\\": %u") != std::string::npos);
	REQUIRE(audio.find("key_detune = %d") != std::string::npos);
	REQUIRE(audio.find("velocity_max = %u") != std::string::npos);
	REQUIRE(audio.find("\\\"velocity_max\\\": %u") != std::string::npos);
	REQUIRE(audio.find("attack_time_us = %u") != std::string::npos);
	REQUIRE(audio.find("\\\"attack_time_us\\\": %u") != std::string::npos);
	REQUIRE(audio.find("release_time_us = %u") != std::string::npos);
	REQUIRE(audio.find("\\\"release_time_us\\\": %u") != std::string::npos);
	REQUIRE(audio.find("s_configuredAliasArchivesComplete") != std::string::npos);
	REQUIRE(audio.find("s_emitConfiguredAliasSounds") != std::string::npos);
	REQUIRE(audio.find("s_configuredAliasMappedMp3Source") != std::string::npos);
	REQUIRE(audio.find("s_configuredMp3VoiceArchivesComplete") !=
	        std::string::npos);
	REQUIRE(audio.find("s_emitOneMp3VoiceAlias") != std::string::npos);
	REQUIRE(audio.find("s_emitOneDirectMp3Voice") != std::string::npos);
	REQUIRE(audio.find("s_emitDirectMp3Voices") != std::string::npos);
	REQUIRE(audio.find("s_directMp3VoiceArchivesComplete") != std::string::npos);
	REQUIRE(audio.find("romextractPdvoiceIsDirectMp3FileSymbol") !=
	        std::string::npos);
	REQUIRE(audio.find("strncmp(symbol, \"FILE_A\", 6)") !=
	        std::string::npos);
	REQUIRE(audio.find("symbol[len - 1] == 'M'") != std::string::npos);
	REQUIRE(audio.find("catalogReadableVoiceIdForFile(filenum") !=
	        std::string::npos);
	REQUIRE(audio.find("pdvoice_b1018_direct_mp3_bank22050") != std::string::npos);
	REQUIRE(audio.find("pdsfx_b943fields_bank22050") != std::string::npos);
	REQUIRE(audio.find("direct MP3 file archive(s) checked") !=
	        std::string::npos);
	REQUIRE(audio.find("\\\"source_reference_kind\\\": \\\"%s\\\"") !=
	        std::string::npos);
	REQUIRE(audio.find("mapped.mp3priority != 0") != std::string::npos);
	REQUIRE(audio.find("s_emitOneMp3VoiceSource((s32)mapped.id") !=
	        std::string::npos);
	REQUIRE(audio.find("romExtractRelPathForFilenum(source_filenum") !=
	        std::string::npos);
	REQUIRE(audio.find("assetArchiveWriterAddPublicMem(&asset_writer, \"sample.mp3\"") !=
	        std::string::npos);
	REQUIRE(audio.find("file_path = sample.mp3") != std::string::npos);
	REQUIRE(audio.find("\\\"data\\\": \\\"sample.mp3\\\"") !=
	        std::string::npos);
	REQUIRE(audio.find("\\\"source_filenum\\\": %d") !=
	        std::string::npos);
	REQUIRE(audio.find("source_filenum = %d") == std::string::npos);
	REQUIRE(audio.find("configured MP3 alias archive(s) checked") !=
	        std::string::npos);
	REQUIRE(audio.find("catalogReadableSoundRefId((s32)(u16)ref.packed") !=
	        std::string::npos);
	REQUIRE(audio.find("loaderEnumNameForSfxEnum(packed)") != std::string::npos);
	REQUIRE(audio.find("loaderEnumNameForFileEnum(source_filenum)") !=
	        std::string::npos);
	REQUIRE(audio.find("s_emitOneSound(leaf") != std::string::npos);

	REQUIRE(audio.find("modArchiveAddFileMem(aw, \"sample.bin\"") ==
	        std::string::npos);
	REQUIRE(audio.find("file_path = sample.bin") == std::string::npos);
	REQUIRE(audio.find("\\\"data\\\": \\\"sample.bin\\\"") ==
	        std::string::npos);
}

TEST_CASE("direct MP3 public voice extraction covers the complete file domain",
		"[modding][pdxxx][base][static][b1018]") {
	const std::string sfx = readFile("src/include/sfx.h");
	const std::string files = readFile("src/include/files.h");
	const std::regex mp3Define(
		R"(^\s*#define\s+MP3_[A-Za-z0-9_]+\s+\(?(FILE_[A-Za-z0-9_]+))");
	const std::regex fileDefine(
		R"(^\s*#define\s+(FILE_[A-Za-z0-9_]+)\s+0x[0-9A-Fa-f]+)");
	std::vector<std::string> mp3Files;
	std::vector<std::string> directAudioFiles;
	std::string line;
	std::smatch match;

	REQUIRE(!sfx.empty());
	REQUIRE(!files.empty());
	{
		std::istringstream lines(sfx);
		while (std::getline(lines, line)) {
			if (std::regex_search(line, match, mp3Define)) {
				mp3Files.push_back(match[1].str());
			}
		}
	}
	{
		std::istringstream lines(files);
		while (std::getline(lines, line)) {
			if (std::regex_search(line, match, fileDefine)) {
				const std::string symbol = match[1].str();
				if (symbol.size() > 6 && symbol.rfind("FILE_A", 0) == 0 &&
						symbol.back() == 'M') {
					directAudioFiles.push_back(symbol);
				}
			}
		}
	}

	std::sort(mp3Files.begin(), mp3Files.end());
	mp3Files.erase(std::unique(mp3Files.begin(), mp3Files.end()), mp3Files.end());
	std::sort(directAudioFiles.begin(), directAudioFiles.end());
	directAudioFiles.erase(
		std::unique(directAudioFiles.begin(), directAudioFiles.end()),
		directAudioFiles.end());

	REQUIRE(mp3Files.size() == 547);
	REQUIRE(directAudioFiles.size() == 548);
	for (const std::string &symbol : mp3Files) {
		INFO(symbol);
		REQUIRE(std::binary_search(
			directAudioFiles.begin(), directAudioFiles.end(), symbol));
	}
	REQUIRE(std::binary_search(directAudioFiles.begin(), directAudioFiles.end(),
		"FILE_ASAUCEREXP1M"));
}

TEST_CASE("base language extractor emits editable json payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string lang = readFile("port/src/romextract_pdlang.c");
	REQUIRE(!lang.empty());

	REQUIRE(lang.find("s_buildStringsJson") != std::string::npos);
	REQUIRE(lang.find("langSourceExportNative") != std::string::npos);
	const std::string source = readFile("port/src/lang_source.cpp");
	REQUIRE(source.find("language_native_table_extent_ambiguous") != std::string::npos);
	REQUIRE(source.find("if (!offset) json += \"null\"") != std::string::npos);
	REQUIRE(lang.find("rzipIs1173") != std::string::npos);
	REQUIRE(lang.find("rzipInflate") != std::string::npos);
	REQUIRE(lang.find("PDLANG_EXTRACT_VERSION") != std::string::npos);
	REQUIRE(lang.find("PDLANG_FAST_CACHE_KIND") != std::string::npos);
	REQUIRE(lang.find("s_existingArchiveHasEntry(dst_rel, \"strings.json\")") !=
	        std::string::npos);
	REQUIRE(lang.find("s_existingArchiveEntryContains(dst_rel, \"lang.ini\"") !=
	        std::string::npos);
	REQUIRE(lang.find("strings_file = strings.json") != std::string::npos);
	REQUIRE(lang.find("extract_version = %s\\n") != std::string::npos);
	REQUIRE(lang.find("source_filenum = %u") == std::string::npos);
	REQUIRE(lang.find("assetArchiveWriterAddPublicMem(&asset_writer, \"strings.json\"") !=
	        std::string::npos);
	REQUIRE(lang.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
	        std::string::npos);
	REQUIRE(lang.find("assetArchiveWriterAddDescriptor(&asset_writer, \"lang.ini\"") !=
	        std::string::npos);
	REQUIRE(lang.find("\\\"data\\\": \\\"strings.json\\\"") !=
	        std::string::npos);

	REQUIRE(lang.find("sha256Hash((const u8 *)json_text") == std::string::npos);
	REQUIRE(lang.find("modArchiveAddFileDisk(aw, \"data.bin\"") ==
	        std::string::npos);
	REQUIRE(lang.find("strings_file = data.bin") == std::string::npos);
	REQUIRE(lang.find("\\\"data\\\": \\\"data.bin\\\"") == std::string::npos);
}

TEST_CASE("base font extractor emits editable bitmap font payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string font = readFile("port/src/romextract_pdfont.c");
	REQUIRE(!font.empty());

	REQUIRE(font.find("s_buildFontExports") != std::string::npos);
	REQUIRE(font.find("glyphs.pgm") != std::string::npos);
	REQUIRE(font.find("font.metrics.json") != std::string::npos);
	REQUIRE(font.find("\\\"pd_kind\\\": \\\"font_metrics\\\"") != std::string::npos);
	REQUIRE(font.find("\\\"kerning\\\": [") != std::string::npos);
	REQUIRE(font.find("font_format = bitmap_ci4_atlas") != std::string::npos);
	REQUIRE(font.find("font_file = glyphs.pgm") != std::string::npos);
	REQUIRE(font.find("metrics_file = font.metrics.json") != std::string::npos);
	REQUIRE(font.find("s_existingArchiveHasEntry(dst_rel, \"glyphs.pgm\")") !=
	        std::string::npos);
	REQUIRE(font.find("s_existingArchiveHasEntry(dst_rel, \"font.metrics.json\")") !=
	        std::string::npos);
	REQUIRE(font.find("assetArchiveWriterAddPublicMem(&asset_writer, \"glyphs.pgm\"") !=
	        std::string::npos);
	REQUIRE(font.find("assetArchiveWriterAddPublicMem(&asset_writer, \"font.metrics.json\"") !=
	        std::string::npos);
	REQUIRE(font.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
	        std::string::npos);
	REQUIRE(font.find("metrics.tsv") == std::string::npos);
	REQUIRE(font.find("kerning.tsv") == std::string::npos);
	REQUIRE(font.find("kerning_file =") == std::string::npos);
	REQUIRE(font.find("\"data\": \"data.bin\"") == std::string::npos);
	REQUIRE(font.find("font_file = data.bin") == std::string::npos);
	REQUIRE(font.find("modArchiveAddFileDisk(aw, \"data.bin\"") ==
	        std::string::npos);
}

TEST_CASE("base song extractor emits midi and semantic event payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string song = readFile("port/src/romextract_pdsong.c");
	REQUIRE(!song.empty());

	REQUIRE(song.find("s_exportSequence") != std::string::npos);
	REQUIRE(song.find("s_buildMidiFile") != std::string::npos);
	REQUIRE(song.find("rzipInflate") != std::string::npos);
	REQUIRE(song.find("preprocessALCMidiHdr") != std::string::npos);
	REQUIRE(song.find("s_existingArchiveHasSongPayloads") !=
	        std::string::npos);
	REQUIRE(song.find("sequence.mid") != std::string::npos);
	REQUIRE(song.find("sequence.json") != std::string::npos);
	REQUIRE(song.find("song_sequence") != std::string::npos);
	REQUIRE(song.find("\\\"events\\\": [") != std::string::npos);
	REQUIRE(song.find("s_bytebufAppendJsonString") != std::string::npos);
	REQUIRE(song.find("music_file = sequence.mid") != std::string::npos);
	REQUIRE(song.find("midi_file = sequence.mid") != std::string::npos);
	REQUIRE(song.find("events_file = sequence.json") != std::string::npos);
	REQUIRE(song.find("assetArchiveWriterAddPublicMem(&asset_writer, \"sequence.mid\"") !=
	        std::string::npos);
	REQUIRE(song.find("assetArchiveWriterAddPublicMem(&asset_writer, \"sequence.json\"") !=
	        std::string::npos);
	REQUIRE(song.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
	        std::string::npos);
	REQUIRE(song.find("sequence.tsv") == std::string::npos);

	REQUIRE(song.find("music_file = data.bin") == std::string::npos);
	REQUIRE(song.find("\\\"data\\\": \\\"data.bin\\\"") == std::string::npos);
	REQUIRE(song.find("modArchiveAddFileMem(aw, \"data.bin\"") ==
	        std::string::npos);
}

TEST_CASE("base character animation extractor emits semantic gltf payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string anim = readFile("port/src/romextract_pdanim_chr.c");
	REQUIRE(!anim.empty());

	REQUIRE(anim.find("s_buildAnimationGltf") != std::string::npos);
	REQUIRE(anim.find("s_decodePartFrame") != std::string::npos);
	REQUIRE(anim.find("s_animDescriptorHeaderLen") != std::string::npos);
	REQUIRE(anim.find("s_existingArchiveHasAnimPayloads") !=
	        std::string::npos);
	REQUIRE(anim.find("PDANIM_CHR_SCHEMA_VERSION 5") !=
	        std::string::npos);
	REQUIRE(anim.find("PDANIM_CHR_GENERATOR") != std::string::npos);
	REQUIRE(anim.find("pd_zero_frame_placeholder") !=
	        std::string::npos);
	REQUIRE(anim.find("semantic extractor v5") != std::string::npos);
	/* Schema v5 (B-772/B-943 gap #2): the dropped ANIMFIELD_08 root-motion
	 * + ANIMFIELD_CAMERA channels are now captured verbatim in the versioned
	 * pd_special_parts extras for a byte-exact public round-trip. */
	REQUIRE(anim.find("pd_special_parts") != std::string::npos);
	REQUIRE(anim.find("s_emitSpecialPartsJson") != std::string::npos);
	REQUIRE(anim.find("s_decodeRawPartFields") != std::string::npos);
	REQUIRE(anim.find("ANIMFIELD_08") != std::string::npos);
	REQUIRE(anim.find("ANIMFIELD_CAMERA") != std::string::npos);
	REQUIRE(anim.find("\\\"runtime_source\\\": \\\"animation.gltf\\\"") !=
	        std::string::npos);
	REQUIRE(anim.find("animation.gltf") != std::string::npos);
	REQUIRE(anim.find("animation_file = animation.gltf") != std::string::npos);
	REQUIRE(anim.find("assetArchiveWriterAddPublicMem(&asset_writer, \"animation.gltf\"") !=
	        std::string::npos);
	REQUIRE(anim.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
	        std::string::npos);
	REQUIRE(anim.find("\\\"pd_anim_flags\\\"") != std::string::npos);
	REQUIRE(anim.find("\\\"pd_repeat_ranges\\\"") != std::string::npos);
	REQUIRE(anim.find("\\\"pd_cut_skip_frames\\\"") != std::string::npos);
	REQUIRE(anim.find("modArchiveExtractAlloc(arc, gltf_idx") !=
	        std::string::npos);
	REQUIRE(anim.find("\\\"generator\\\": \\\"\" PDANIM_CHR_GENERATOR") !=
	        std::string::npos);

	REQUIRE(anim.find("header.tsv") == std::string::npos);
	REQUIRE(anim.find("frames.tsv") == std::string::npos);
	REQUIRE(anim.find("frames_file = frames.bin") == std::string::npos);
	REQUIRE(anim.find("\\\"frames\\\": \\\"frames.bin\\\"") ==
	        std::string::npos);
	REQUIRE(anim.find("modArchiveAddFileMem(aw, \"frames.bin\"") ==
	        std::string::npos);
}

TEST_CASE("animation compiler reads full embedded gltf buffer uris",
          "[modding][pdxxx][base][static][c3812][B-772]") {
	const std::string compiler = readFile("port/src/modasset_compiler.c");
	REQUIRE(!compiler.empty());

	REQUIRE(compiler.find("char uri[4096]") == std::string::npos);
	REQUIRE(compiler.find("char uri[8192]") == std::string::npos);

	const std::string animationParser = sourceFunctionBlock(compiler,
		"static s32 parseGltfTextAnimationClip");
	const auto animationLoad = animationParser.find(
		"gltfLoadTextBuffer(NULL, root, source_path, 1, &bin, &bin_size, &error)");
	const auto animationParse = animationParser.find(
		"parseGltfAnimationClipFromJson(json, size, bin, bin_size, 1, clip)");
	REQUIRE(animationLoad != std::string::npos);
	REQUIRE(animationParse != std::string::npos);
	REQUIRE(animationLoad < animationParse);

	const std::string loader = sourceFunctionBlock(compiler,
		"static s32 gltfLoadTextBuffer");
	REQUIRE(loader.find("json_size = (size_t)(root.end - root.start)") !=
		std::string::npos);
	const auto uriAlloc = loader.find("uri = malloc(json_size + 1)");
	const auto declaration = loader.find(
		"modAssetGltfOptionalBufferDocument(root.start, json_size, 0,");
	const auto decode = loader.find("decodeGltfDataUri(uri, declared, &size)");
	REQUIRE(uriAlloc != std::string::npos);
	REQUIRE(declaration != std::string::npos);
	REQUIRE(decode != std::string::npos);
	REQUIRE(uriAlloc < declaration);
	REQUIRE(declaration < decode);
	const auto bufferless = loader.find("if (declared == 0)");
	REQUIRE(bufferless != std::string::npos);
	REQUIRE(declaration < bufferless);
	REQUIRE(bufferless < decode);
	REQUIRE(loader.find("jsonFindKeyInSpan(root, \"buffers\")") ==
		std::string::npos);
	REQUIRE(loader.find("uri, json_size + 1, &declared") != std::string::npos);
	REQUIRE(loader.find("free(uri)", decode) != std::string::npos);
	REQUIRE(loader.find("modAssetGltfBufferSize(declared, size, 0)", decode) !=
		std::string::npos);
	const std::string decoder = sourceFunctionBlock(compiler,
		"static u8 *decodeGltfDataUri");
	REQUIRE(decoder.find("modAssetGltfDataUriSize(uri, declared_size)") !=
		std::string::npos);
	REQUIRE(decoder.find("base64DecodeAlloc(comma + 1, strlen(comma + 1), out_size)") !=
		std::string::npos);

	REQUIRE(compiler.find("parseGltfAnimationNativeExtras") !=
	        std::string::npos);
	REQUIRE(compiler.find("pd_anim_flags") != std::string::npos);
	REQUIRE(compiler.find("pd_repeat_ranges") != std::string::npos);
	REQUIRE(compiler.find("pd_cut_skip_frames") != std::string::npos);
	REQUIRE(compiler.find("ANIMFLAG_HASREPEATFRAMES") !=
	        std::string::npos);
	REQUIRE(compiler.find("ANIMFLAG_HASCUTSKIPFRAMES") !=
	        std::string::npos);
	REQUIRE(compiler.find("writeBe16(p, 0xffff)") != std::string::npos);
}

TEST_CASE("animation compiler resamples gltf channels by authored input time",
          "[modding][pdxxx][base][static][c3812][B-804]") {
	const std::string compiler = readFile("port/src/modasset_compiler.c");
	REQUIRE(!compiler.empty());

	REQUIRE(compiler.find("gltfChannelReadInputTime") != std::string::npos);
	REQUIRE(compiler.find("gltfChannelFrameTime") != std::string::npos);
	REQUIRE(compiler.find("gltfChannelFindSampleForTime") != std::string::npos);
	REQUIRE(compiler.find("gltfChannelSampleFloat") != std::string::npos);
	REQUIRE(compiler.find("gltfChannelSampleQuat") != std::string::npos);
	REQUIRE(compiler.find("step_interpolation") != std::string::npos);
	REQUIRE(compiler.find("strcmp(sampler->interpolation, \"STEP\") == 0") !=
	        std::string::npos);
	REQUIRE(compiler.find("gltfAnimationSampleForFrame") == std::string::npos);

	const auto payload = compiler.find("static s32 gltfBuildAnimationClipPayload");
	REQUIRE(payload != std::string::npos);
	const auto translation = compiler.find("gltfChannelSampleFloat(channel, frame", payload);
	REQUIRE(translation != std::string::npos);
	const auto rotation = compiler.find("gltfChannelSampleQuat(channel, frame", payload);
	REQUIRE(rotation != std::string::npos);
	const auto scale = compiler.find("writeBeFloat(p + 0, gltfChannelSampleFloat(channel", payload);
	REQUIRE(scale != std::string::npos);
}

TEST_CASE("animation compiler preserves zero-frame no-op gltf clips at animtable boundary",
          "[modding][pdxxx][base][static][c3812][B-772]") {
	const std::string compiler = readFile("port/src/modasset_compiler.c");
	REQUIRE(!compiler.empty());

	const std::string payload = sourceFunctionBlock(compiler,
		"static s32 gltfBuildAnimationClipPayload");
	const auto frameClamp = payload.find("if (frame_count <= 0)");
	REQUIRE(frameClamp != std::string::npos);
	REQUIRE(payload.find("frame_count = 1;", frameClamp) != std::string::npos);
	REQUIRE(payload.find(
		"part_count = clip->part_count > 0 ? clip->part_count : 1;") !=
		std::string::npos);
	REQUIRE(payload.find("part_count = clip->zero_frame_placeholder") ==
		std::string::npos);

	const auto noChannel = payload.find("if (clip->channel_count <= 0)");
	REQUIRE(noChannel != std::string::npos);
	const auto normalChannel = payload.find("translation_for_part = malloc", noChannel);
	REQUIRE(normalChannel != std::string::npos);
	const std::string block = payload.substr(noChannel, normalChannel - noChannel);

	REQUIRE(block.find("descriptor_header_len = part_count;") !=
		std::string::npos);
	REQUIRE(block.find("data = calloc(1, (size_t)header_len);") !=
		std::string::npos);
	REQUIRE(block.find("animDescriptorStreamMatches") != std::string::npos);
	REQUIRE(block.find("out_entry->numframes = (u16)frame_count;") != std::string::npos);
	REQUIRE(block.find("out_entry->bytesperframe = 0;") != std::string::npos);
	REQUIRE(block.find("out_entry->data = 0xffffffff;") != std::string::npos);
	REQUIRE(block.find("out_entry->headerlen = (u16)header_len;") != std::string::npos);
	REQUIRE(block.find("out_entry->framelen = 16;") != std::string::npos);
	REQUIRE(block.find("*out_data_size = (u32)header_len;") != std::string::npos);
	REQUIRE(block.find("return 1;") != std::string::npos);
}

TEST_CASE("base weapon animation extractor emits zip-openable command payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string anim = readFile("port/src/romextract_pdanim.c");
	const std::string pool = readFile("port/src/loader_pool.c");
	REQUIRE(!anim.empty());
	REQUIRE(!pool.empty());

	REQUIRE(anim.find("s_existingArchiveHasAnimPayloads") !=
	        std::string::npos);
	REQUIRE(anim.find("\"\\\"animation\\\": \\\"invanim_\"") !=
	        std::string::npos);
	REQUIRE(anim.find("modArchiveBeginDerived(full)") != std::string::npos);
	REQUIRE(anim.find("assetArchiveWriterAddDescriptor(&asset_writer, \"animation.ini\"") !=
	        std::string::npos);
	REQUIRE(anim.find("assetArchiveWriterAddManifestJson(&asset_writer") !=
	        std::string::npos);
	REQUIRE(anim.find("assetArchiveWriterAddPublicMem(&asset_writer, \"commands.json\"") !=
	        std::string::npos);
	REQUIRE(anim.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
	        std::string::npos);
	REQUIRE(anim.find("category = weapon_animation") != std::string::npos);
	REQUIRE(anim.find("source_format = weapon_animation_commands") != std::string::npos);
	REQUIRE(anim.find("commands_file = commands.json") != std::string::npos);
	REQUIRE(anim.find("\\\"commands\\\": [") != std::string::npos);
	REQUIRE(anim.find("\\\"command\\\": ") != std::string::npos);
	REQUIRE(anim.find("s_weaponAnimCatalogIdFromName") != std::string::npos);
	REQUIRE(anim.find("snprintf(out, out_n, \"base:%s\", anim_name)") !=
	        std::string::npos);
	REQUIRE(anim.find("s_textbufAppendJsonString(out, aname ? aname : \"\")") ==
	        std::string::npos);
	REQUIRE(anim.find("catalogReadableSoundRefId") != std::string::npos);
	REQUIRE(anim.find("catalogReadableAnimationId") != std::string::npos);
	REQUIRE(anim.find("\\\"category\\\": \\\"weapon_animation\\\"") !=
	        std::string::npos);
	REQUIRE(pool.find("decodeCommandObject") != std::string::npos);
	REQUIRE(pool.find("jread_audio_catalog_or_enum") != std::string::npos);
	REQUIRE(pool.find("s_resolveAnimationCatalogOrEnumName") != std::string::npos);
	REQUIRE(pool.find("jstream_str_eq(&s->cur, \"commands\")") != std::string::npos);
	REQUIRE(pool.find("jstream_str_eq(&s->cur, \"name\")") != std::string::npos);
	REQUIRE(pool.find("loaderPoolParseAnimationSourceJson") != std::string::npos);
	REQUIRE(pool.find("s_ParseAnimationSourcePath") != std::string::npos);
	REQUIRE(anim.find("fsFileOpenWrite(relpath)") == std::string::npos);
	REQUIRE(anim.find("emits one JSON") == std::string::npos);
	REQUIRE(anim.find("opcodes_file = opcodes.json") == std::string::npos);

	const std::string weapon = readFile("port/src/romextract_pdweapon.c");
	REQUIRE(weapon.find("s_audioDependencyInfoForSfx") != std::string::npos);
	REQUIRE(weapon.find("ref.hasconfig") != std::string::npos);
	REQUIRE(weapon.find("audio/sfx/%s.pdsfx") != std::string::npos);
	REQUIRE(weapon.find("sample_catalog_id") != std::string::npos);
}

TEST_CASE("base mesh extractor emits standard obj geometry payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string mesh = readFile("port/src/romextract_pdmesh.c");
	const std::string gbi = readFile("port/src/preprocess/gbi.c");
	const std::string gbi_contract = readFile("include/PR/gbi.h");
	const std::string renderer = readFile("port/fast3d/gfx_pc.cpp");
	const std::string collision = readFile("src/lib/meshcollision.c");
	const std::string arena = readFile("port/src/romextract_pdarena.c");
	const std::string model_core = readFile("src/lib/model.c");
	const std::string render_stream_contract =
		readFile("tools/pdmesh_render_stream.py");
	REQUIRE(!mesh.empty());
	REQUIRE(!gbi.empty());
	REQUIRE(!gbi_contract.empty());
	REQUIRE(!renderer.empty());
	REQUIRE(!collision.empty());
	REQUIRE(!arena.empty());
	REQUIRE(!model_core.empty());

	REQUIRE(mesh.find("s_buildModelObj") != std::string::npos);
	REQUIRE(mesh.find("s_exportGdlToObj") != std::string::npos);
	REQUIRE(mesh.find("ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION_LABEL \"model_obj_mtx_v25_materials_hierarchy_parts_faces_json_relations_raw_mtx_render_commands_json_geometry_state_vertex_cache_provenance_zero_tri_top_level_policy_gbi_vtxcount_paircache_vtxcolour_jointflags_vtxmtx_collision19_texscale_b1154\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("ROMEXTRACT_PDMESH_FAST_CACHE_KIND \"pdmesh_fastcache_v29_allmodels_menuhud_\" \\\n\tROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION_LABEL") !=
	        std::string::npos);
	REQUIRE(mesh.find("cmd == (u8)G_SETGEOMETRYMODE") != std::string::npos);
	REQUIRE(mesh.find("cmd == (u8)G_CLEARGEOMETRYMODE") != std::string::npos);
	REQUIRE(mesh.find("s_objAppendGeometryRow") != std::string::npos);
	REQUIRE(mesh.find("s_objAppendVertexLoadRow") != std::string::npos);
	REQUIRE(mesh.find("s_objAppendVertexBoundaryRow") != std::string::npos);
	REQUIRE(mesh.find("vertex_load") != std::string::npos);
	REQUIRE(mesh.find("vertex_cache_slots") != std::string::npos);
	REQUIRE(mesh.find("vertex_scope") != std::string::npos);
	REQUIRE(mesh.find("vertex_cache_reset") != std::string::npos);
	REQUIRE(mesh.find("gundl->xlugdl &&") != std::string::npos);
	REQUIRE(mesh.find("dl->xlugdl &&") != std::string::npos);
	REQUIRE(mesh.find("slots_geometry_mode") != std::string::npos);
	REQUIRE(mesh.find("slots_geometry_known") != std::string::npos);
	REQUIRE(mesh.find("G_DL_NOPUSH") != std::string::npos);
	REQUIRE(mesh.find("MODASSET_RENDER_STREAM_SCHEMA_CURRENT") !=
	        std::string::npos);
	REQUIRE(mesh.find("geometry_command_count") != std::string::npos);
	REQUIRE(mesh.find("catalogReadableModelIdForFile((s32)FILE_GHUDPIECE, \"menu\", \"menu\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("(u16)FILE_GHUDPIECE, \"menu\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("#include \"preprocess.h\"") != std::string::npos);
	REQUIRE(mesh.find("#include \"game/modeldef.h\"") != std::string::npos);
	REQUIRE(mesh.find("#include \"lib/rzip.h\"") != std::string::npos);
	REQUIRE(mesh.find("s_loadSourceModelPreprocessed") != std::string::npos);
	REQUIRE(mesh.find("romextract pdmesh: preprocessed filenum") !=
	        std::string::npos);
	REQUIRE(mesh.find("trace 0x019c") == std::string::npos);
	REQUIRE(mesh.find("rzipIs1173") != std::string::npos);
	REQUIRE(mesh.find("rzipInflate") != std::string::npos);
	REQUIRE(mesh.find("preprocessModelFile") != std::string::npos);
	REQUIRE(mesh.find("preprocessGunFile") != std::string::npos);
	REQUIRE(mesh.find("s_modeldefOffsetsLookPromotable") !=
	        std::string::npos);
	REQUIRE(mesh.find("modelPromoteTypeToPointer") != std::string::npos);
	REQUIRE(mesh.find("modelPromoteOffsetsToPointers") != std::string::npos);
	REQUIRE(mesh.find("ROMEXTRACT_PDMESH_MODEL_VMA") != std::string::npos);
	REQUIRE(mesh.find("rawaddr &= ~(uintptr_t)1") != std::string::npos);
	REQUIRE(mesh.find("uintptr_t src = ((uintptr_t)w1) & ~(uintptr_t)1") !=
	        std::string::npos);
	REQUIRE(mesh.find("G_VTX") != std::string::npos);
	REQUIRE(mesh.find("GBI_VTX_COUNT_FROM_W0(w0)") !=
	        std::string::npos);
	REQUIRE(mesh.find("GBI_VTX_DEST_FROM_W0(w0)") != std::string::npos);
	REQUIRE(mesh.find("seg == SPSEGMENT_MODEL_VTX") !=
	        std::string::npos);
	REQUIRE(mesh.find("(w0 & 0xffffu) / sizeof(Vtx)") ==
	        std::string::npos);
	REQUIRE(gbi_contract.find("GBI_VTX_COUNT_FROM_W0") !=
	        std::string::npos);
	REQUIRE(gbi_contract.find("GBI_VTX_DEST_FROM_W0") !=
	        std::string::npos);
	REQUIRE(renderer.find("GBI_VTX_COUNT_FROM_W0(cmd->words.w0)") !=
	        std::string::npos);
	REQUIRE(renderer.find("C0(0, 16) / sizeof(Vtx)") ==
	        std::string::npos);
	REQUIRE(collision.find("GBI_VTX_COUNT_FROM_W0(w0)") !=
	        std::string::npos);
	REQUIRE(collision.find("GBI_VTX_COUNT_FROM_W0(vertex_word)") !=
	        std::string::npos);
	REQUIRE(collision.find("GBI_VTX_DEST_FROM_W0(vertex_word)") !=
	        std::string::npos);
	REQUIRE(collision.find("slots[slot].x") != std::string::npos);
	REQUIRE(collision.find("valid_slots[p0]") != std::string::npos);
	REQUIRE(collision.find("((w0 >> 4) & 0xf) + 1") ==
	        std::string::npos);
	REQUIRE(arena.find("GBI_VTX_COUNT_FROM_W0(w0)") !=
	        std::string::npos);
	REQUIRE(mesh.find("G_MTX") != std::string::npos);
	REQUIRE(mesh.find("G_POPMTX") != std::string::npos);
	REQUIRE(mesh.find("s_objApplyMtxCommand") != std::string::npos);
	REQUIRE(mesh.find("G_NOOP") != std::string::npos);
	REQUIRE(mesh.find("s_objEnsureTextureMaterial") != std::string::npos);
	REQUIRE(mesh.find("catalogReadableTextureId") != std::string::npos);
	REQUIRE(mesh.find("pd_texture_catalog = %s") != std::string::npos);
	REQUIRE(mesh.find("usemtl %s") != std::string::npos);
	REQUIRE(mesh.find("SPSEGMENT_MODEL_MTX") != std::string::npos);
	REQUIRE(mesh.find("s_objBuildDefaultModelMatrices") != std::string::npos);
	REQUIRE(mesh.find("ROMEXTRACT_PDMESH_NODE_DEPTH_CAP") != std::string::npos);
	REQUIRE(mesh.find("s_objMtxTransformPoint") != std::string::npos);
	REQUIRE(mesh.find("s_objMtxInverseTransformPoint") != std::string::npos);
	REQUIRE(mesh.find("s_writeNodeHierarchyJson") != std::string::npos);
	REQUIRE(mesh.find("s_writePartTableJson") != std::string::npos);
	REQUIRE(mesh.find("s_objRegisterNodes(&ctx, modeldef->rootnode, 0)") !=
	        std::string::npos);
	REQUIRE(mesh.find("if (!modeldef || !modeldef->rootnode) return 0;") ==
	        std::string::npos);
	REQUIRE(mesh.find("if (!modeldef->rootnode && modeldef->numparts > 0) return 0;") !=
	        std::string::npos);
	REQUIRE(mesh.find("if (s_writeNodeHierarchyJson(&ctx) != 0)") !=
	        std::string::npos);
	REQUIRE(mesh.find("if (s_writePartTableJson(&ctx) != 0)") !=
	        std::string::npos);
	REQUIRE(mesh.find("s_objNodeUnderHiddenGunToggle") != std::string::npos);
	REQUIRE(mesh.find("s_objHiddenGunToggleTargetsNode") != std::string::npos);
	REQUIRE(mesh.find("s_objStaticGunVertsLookDetachedEffect") != std::string::npos);
	REQUIRE(mesh.find("s_objMayCullDetachedGunEffects") != std::string::npos);
	REQUIRE(mesh.find("ctx.source_filenum = source_filenum") != std::string::npos);
	REQUIRE(mesh.find("FILE_GFALCON2LOD") != std::string::npos);
	REQUIRE(mesh.find("s_objTextLooksDetachedGunEffect") != std::string::npos);
	REQUIRE(mesh.find("s_exportGunDlToObj") != std::string::npos);
	REQUIRE(mesh.find("ctx.static_gun_model = loadtype == LOADTYPE_GUN") !=
	        std::string::npos);
	REQUIRE(mesh.find("MODELPART_0042") != std::string::npos);
	REQUIRE(mesh.find("MODELPART_FALCON2_002E") != std::string::npos);
	REQUIRE(mesh.find("MODELPART_FALCON2_002F") != std::string::npos);
	REQUIRE(mesh.find("MODELPART_GUN_LASERLIQUID") != std::string::npos);
	REQUIRE(mesh.find("MODELPART_GUN_MUZZLEFLASH1") != std::string::npos);
	REQUIRE(mesh.find("MODELPART_GUN_MUZZLEFLASH2") != std::string::npos);
	REQUIRE(mesh.find("MODELPART_GUN_MUZZLEFLASH3") != std::string::npos);
	REQUIRE(mesh.find("G_TRI1") != std::string::npos);
	REQUIRE(mesh.find("G_TRI4") != std::string::npos);
	REQUIRE(mesh.find("pdmesh_gdl_walk_state_t") != std::string::npos);
	REQUIRE(mesh.find("s_exportGdlToObjWalk") != std::string::npos);
	REQUIRE(mesh.find("modAssetRenderStreamTopLevelWalkDisposition") !=
	        std::string::npos);
	REQUIRE(mesh.find("MODASSET_RENDER_TOP_LEVEL_WALK_SKIP") !=
	        std::string::npos);
	REQUIRE(mesh.find("MODASSET_RENDER_TOP_LEVEL_WALK_EXECUTE") !=
	        std::string::npos);
	REQUIRE(mesh.find("resolved_gdl != NULL") != std::string::npos);
	REQUIRE(mesh.find("vertex_domain_readable") != std::string::npos);
	REQUIRE(mesh.find("state->slots_geometry_known") != std::string::npos);
	REQUIRE(mesh.find("!ctx || !raw_gdl || !vbuf || numverts <= 0 || !state || depth > 16") !=
	        std::string::npos);
	REQUIRE(mesh.find("PDMESH_GDL_FAILURE_UNTERMINATED") !=
	        std::string::npos);
	REQUIRE(mesh.find("s_exportGdlToObjWithState") != std::string::npos);
	REQUIRE(mesh.find("modAssetRenderStreamTopLevelPairSharesVertexCache") !=
	        std::string::npos);
	REQUIRE(mesh.find("star->unk00 * 4") != std::string::npos);
	REQUIRE(mesh.find("(w1 >> 0)  & 0xf") != std::string::npos);
	REQUIRE(mesh.find("(w0 >> 12) & 0xf") != std::string::npos);
	REQUIRE(mesh.find("gdl[cmdidx].tri4") == std::string::npos);
	REQUIRE(mesh.find("model.obj") != std::string::npos);
	REQUIRE(mesh.find("model.mtl") != std::string::npos);
	REQUIRE(mesh.find("model.nodes.json") != std::string::npos);
	REQUIRE(mesh.find("model.parts.json") != std::string::npos);
	REQUIRE(mesh.find("model.faces.json") != std::string::npos);
	REQUIRE(mesh.find("model.render.json") != std::string::npos);
	const std::string conformance = readFile("tools/asset_archive_conformance.py");
	REQUIRE(conformance.find("validate_pdmesh_hierarchy_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("model.render.json must contain a non-empty commands array") !=
	        std::string::npos);
	REQUIRE(conformance.find("model.faces.json row count") !=
	        std::string::npos);
	REQUIRE(render_stream_contract.find("model.render.json must reference every model.faces.json") !=
	        std::string::npos);
	REQUIRE(conformance.find("model.nodes.json render node") !=
	        std::string::npos);
	REQUIRE(conformance.find("node_unresolved") != std::string::npos);
	REQUIRE(conformance.find("node == -1 and node_unresolved is True") !=
	        std::string::npos);
	const std::string compiler = readFile("port/src/modasset_compiler.c");
	REQUIRE(compiler.find("node_id < 0 && node_unresolved") !=
	        std::string::npos);
	REQUIRE(compiler.find("tri->material_index >= 0 &&") !=
	        std::string::npos);
	REQUIRE(compiler.find("material ? material->render_class :") !=
	        std::string::npos);
	REQUIRE(mesh.find("\\\"node_unresolved\\\": %s") != std::string::npos);
	REQUIRE(mesh.find("model.nodes.tsv") == std::string::npos);
	REQUIRE(mesh.find("model.parts.tsv") == std::string::npos);
	REQUIRE(mesh.find("model.faces.tsv") == std::string::npos);
	REQUIRE(mesh.find("s_objAppendRenderRow") != std::string::npos);
	REQUIRE(mesh.find("\\\"pd_kind\\\": \\\"mesh_render_commands\\\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("\\\"face_index\\\": %d") != std::string::npos);
	REQUIRE(mesh.find("\\\"matrix_flags\\\": %u") != std::string::npos);
	REQUIRE(mesh.find("\\\"material_index\\\": %d") != std::string::npos);
	REQUIRE(mesh.find("model.render.tsv") == std::string::npos);
	REQUIRE(mesh.find("cmd == (u8)G_POPMTX && w1 != 0") == std::string::npos);
	REQUIRE(mesh.find("distance_near") != std::string::npos);
	REQUIRE(mesh.find("reorder_target_a") != std::string::npos);
	REQUIRE(mesh.find("export_version.txt") != std::string::npos);
	REQUIRE(mesh.find("source_format = PD_MODELDEF") != std::string::npos);
	REQUIRE(mesh.find("format = OBJ") != std::string::npos);
	REQUIRE(mesh.find("obj_export_version = %s") != std::string::npos);
	REQUIRE(mesh.find("geometry_file = model.obj") != std::string::npos);
	REQUIRE(mesh.find("material_file = model.mtl") != std::string::npos);
	REQUIRE(mesh.find("hierarchy_file = model.nodes.json") != std::string::npos);
	REQUIRE(mesh.find("parts_file = model.parts.json") != std::string::npos);
	REQUIRE(mesh.find("faces_file = model.faces.json") != std::string::npos);
	REQUIRE(mesh.find("render_stream_file = model.render.json") != std::string::npos);
	REQUIRE(mesh.find("model_scale = %.9g") != std::string::npos);
	REQUIRE(mesh.find("\\\"model_scale\\\": %.9g") != std::string::npos);
	REQUIRE(mesh.find("node_count = %u") != std::string::npos);
	REQUIRE(mesh.find("part_count = %u") != std::string::npos);
	REQUIRE(mesh.find("model_matrix_reference_count = %u") != std::string::npos);
	REQUIRE(mesh.find("render_command_count = %u") != std::string::npos);
	REQUIRE(mesh.find("material_count = %u") != std::string::npos);
	REQUIRE(mesh.find("\\\"textured_material_count\\\": %u") != std::string::npos);
	REQUIRE(mesh.find("s_existingArchiveHasCurrentPayload(dst_rel)") !=
	        std::string::npos);
	REQUIRE(mesh.find("s_archiveEntryMatches(arc, \"export_version.txt\"") !=
	        std::string::npos);
	for (const char *required : {
			"model.obj", "model.mtl", "model.nodes.json", "model.parts.json",
			"model.faces.json", "model.render.json", "_meta/inventory.json",
			"_meta/hashes.json", "_meta/provenance.json",
			"_meta/validation.json", "_meta/source-handles.json",
			"_meta/mesh.ini.sha256", "_meta/export_version.txt.sha256",
			"_meta/model.obj.sha256", "_meta/model.mtl.sha256",
			"_meta/model.nodes.json.sha256", "_meta/model.parts.json.sha256",
			"_meta/model.faces.json.sha256", "_meta/model.render.json.sha256"}) {
		CAPTURE(required);
		REQUIRE(mesh.find(required) != std::string::npos);
	}
	REQUIRE(mesh.find("assetArchiveWriterAddPublicMem(&asset_writer, \"export_version.txt\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("assetArchiveWriterAddPublicMem(&asset_writer, \"model.obj\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("assetArchiveWriterAddPublicMem(&asset_writer, \"model.mtl\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("assetArchiveWriterAddPublicMem(&asset_writer, \"model.nodes.json\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("assetArchiveWriterAddPublicMem(&asset_writer, \"model.parts.json\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("assetArchiveWriterAddPublicMem(&asset_writer, \"model.faces.json\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("assetArchiveWriterAddPublicMem(&asset_writer, \"model.render.json\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
	        std::string::npos);
	REQUIRE(mesh.find("const char *wanted_hint = hint ? hint : \"\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("strcmp(existing_id, wanted_id) == 0") !=
	        std::string::npos);
	REQUIRE(mesh.find("unique_mesh_jobs") != std::string::npos);
	REQUIRE(gbi.find("GBI_GDL_REWRITE_MAX_CMDS") != std::string::npos);
	REQUIRE(gbi.find("no ENDDL within") != std::string::npos);
	REQUIRE(model_core.find("MODEL_PROMOTE_NODE_VISIT_CAP") !=
	        std::string::npos);
	REQUIRE(model_core.find("promotion stopped at repeated/deep node") !=
	        std::string::npos);

	REQUIRE(mesh.find("modeldef.tsv") == std::string::npos);
	REQUIRE(mesh.find("geometry_file = geometry.bin") == std::string::npos);
	REQUIRE(mesh.find("\\\"geometry\\\": \\\"geometry.bin\\\"") ==
	        std::string::npos);
	REQUIRE(mesh.find("modArchiveAddFileDisk(aw, \"geometry.bin\"") ==
	        std::string::npos);
	REQUIRE(mesh.find("modArchiveAddFileMem(aw, \"geometry.bin\"") ==
	        std::string::npos);
	REQUIRE(mesh.find("s_buildModelObj((const u8 *)src_bytes") ==
	        std::string::npos);
	REQUIRE(mesh.find("const char mtl_buf[]") == std::string::npos);
	REQUIRE(mesh.find("map_Kd") == std::string::npos);
	REQUIRE(mesh.find("exported zero-triangle modeldef") !=
	        std::string::npos);
	REQUIRE(mesh.find("OBJ export produced no triangles") ==
	        std::string::npos);
}

TEST_CASE("embedded mesh public model descriptor selects standard geometry",
          "[modding][pdxxx][mesh-ingress][source]") {
	TempArchive mesh = writeTypedArchiveEntries("mesh-ingress-public", ".pdmesh", {
		{"mesh.ini", "[model]\ncatalog_id = example:visual\nkind = mesh\ngeometry_file = selected.obj\n"},
		{"selected.obj", "v 0 0 0\nv 3 0 0\nv 0 3 0\nf 1 2 3\n"},
	});
	const std::string bytes = readFile(mesh.path.string().c_str());
	char err[256] = {};
	REQUIRE(assetArchiveValidateBytes(bytes.data(), static_cast<u32>(bytes.size()),
		"visual.pdmesh", ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) == 0);
	loader_walker_mesh_source_plan_t plan = {};
	REQUIRE(loaderWalkerMeshSourcePlanManifest(nullptr, 0, "example:visual",
		"weapon.pdweapon::round.pdprojectile::visual.pdmesh", "selected.obj", -1,
		&plan, err, sizeof(err)) == 1);
	REQUIRE(std::string(plan.source_path) ==
		"weapon.pdweapon::round.pdprojectile::visual.pdmesh::selected.obj");
	REQUIRE(plan.source_filenum == -1);
}

TEST_CASE("embedded mesh admission rejects its missing selected public source",
          "[modding][pdxxx][mesh-ingress][source]") {
	TempArchive mesh = writeTypedArchiveEntries("mesh-ingress-missing", ".pdmesh", {
		{"mesh.ini", "[model]\ncatalog_id = example:visual\ngeometry_file = missing.obj\n"},
		{"model.obj", "v 0 0 0\nv 3 0 0\nv 0 3 0\nf 1 2 3\n"},
	});
	const std::string bytes = readFile(mesh.path.string().c_str());
	char err[256] = {};
	REQUIRE(assetArchiveValidateBytes(bytes.data(), static_cast<u32>(bytes.size()),
		"visual.pdmesh", ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) < 0);
	REQUIRE(std::string(err).find("missing.obj") != std::string::npos);
}

TEST_CASE("mesh sharing compares complete canonical source identity",
          "[modding][pdxxx][mesh-ingress][source]") {
	TempArchive a = writeTypedArchiveEntries("mesh-ingress-hash-a", ".pdmesh", {
		{"mesh.ini", "[model]\ncatalog_id = example:shared\ngeometry_file = model.obj\n"},
		{"model.obj", "v 0 0 0\nv 3 0 0\nv 0 3 0\nf 1 2 3\n"},
	});
	TempArchive b = writeTypedArchiveEntries("mesh-ingress-hash-b", ".pdmesh", {
		{"model.obj", "v 0 0 0\nv 3 0 0\nv 0 3 0\nf 1 2 3\n"},
		{"mesh.ini", "[model]\ncatalog_id = example:shared\ngeometry_file = model.obj\n"},
	});
	TempArchive c = writeTypedArchiveEntries("mesh-ingress-hash-c", ".pdmesh", {
		{"mesh.ini", "[model]\ncatalog_id = example:shared\ngeometry_file = model.obj\n"},
		{"model.obj", "v 0 0 0\nv 4 0 0\nv 0 4 0\nf 1 2 3\n"},
	});
	char ah[SHA256_HEX_SIZE] = {}, bh[SHA256_HEX_SIZE] = {}, ch[SHA256_HEX_SIZE] = {};
	REQUIRE(weaponGraphArchiveCanonicalSha256File(a.path.string().c_str(), ah) == 0);
	REQUIRE(weaponGraphArchiveCanonicalSha256File(b.path.string().c_str(), bh) == 0);
	REQUIRE(weaponGraphArchiveCanonicalSha256File(c.path.string().c_str(), ch) == 0);
	REQUIRE(std::string(ah) == bh);
	REQUIRE(std::string(ah) != ch);
	const std::string harness = readFile("port/src/weapon_nested_runtime_harness.c");
	REQUIRE(harness.find("harnessMeshAccept") != std::string::npos);
	REQUIRE(harness.find("harnessMeshVertices(catalogGetLoadedModeldef(mesh_id)") != std::string::npos);
	REQUIRE(harness.find("projectile->projectile_modelnum != mesh->runtime_index") != std::string::npos);
	REQUIRE(harness.find("held->source_filenum != held_mesh->source_filenum") != std::string::npos);
	REQUIRE(harness.find("harnessMeshSlotRollback") != std::string::npos);
}

TEST_CASE("nested weapon distribution ships parent archive and rebuilds hot indexes",
		"[modding][network][pdxxx][weapon][nested][T-ASSETS-022]") {
	const std::string distrib = readFile("port/src/net/netdistrib.c");
	const std::string netmsg = readFile("port/src/net/netmsg.c");
	const std::string walker = readFile("port/src/loader_walker_weapon.c");
	REQUIRE(distrib.find("buildTypedArchiveComponent(entry, typed_archive") !=
		std::string::npos);
	REQUIRE(distrib.find("assetCatalogScanExternalLayoutFolderDeferred(") !=
		std::string::npos);
	REQUIRE(distrib.find("catalogLoadInit();") != std::string::npos);
	REQUIRE(netmsg.find("strstr(e->dirpath, \"::\") != NULL") !=
		std::string::npos);
	const auto nestedRegister = walker.find(
		"assetCatalogRegisterBootWeaponNestedDependencies(id, file_path");
	const auto generalRegister = walker.find(
		"assetCatalogRegisterWeaponNestedDependencies(id, file_path", nestedRegister);
	const auto weaponParse = walker.find("loaderPoolParseWeaponJson", nestedRegister);
	REQUIRE(nestedRegister != std::string::npos);
	REQUIRE(generalRegister != std::string::npos);
	REQUIRE(weaponParse != std::string::npos);
	REQUIRE(generalRegister < weaponParse);
}

TEST_CASE("SP-74 nested meshes hydrate compatible stable rows transactionally",
		"[B-1105][SP-74][modding][pdxxx][weapon][nested]") {
	const std::string header = readFile("port/include/assetcatalog_scanner.h");
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	const std::string walker = readFile("port/src/loader_walker_mesh.c");
	const std::string binder =
		readFile("port/src/loader_walker_mesh_source_bind.c");

	/* Top-level and nested publication share one manifest planner/binder. */
	REQUIRE(walker.find("loaderWalkerMeshSourcePlanManifest") !=
		std::string::npos);
	REQUIRE(walker.find("loaderWalkerBindMeshSource") != std::string::npos);
	REQUIRE(walker.find(
		"loaderWalkerArchiveTextMember(archive_path, \"mesh.ini\"") !=
		std::string::npos);
	REQUIRE(walker.find("file_path, public_geometry") != std::string::npos);
	REQUIRE(binder.find("s32 loaderWalkerBindMeshSource") !=
		std::string::npos);
	REQUIRE(binder.find("loaderWalkerMeshSourceMatchesEntry") !=
		std::string::npos);
	REQUIRE(binder.find("loaderWalkerMeshSourceChangeAllowed") !=
		std::string::npos);
	REQUIRE(binder.find("Equal public archives share one stable first owner") !=
		std::string::npos);
	REQUIRE(scanner.find("loaderWalkerMeshSourcePlanManifest") !=
		std::string::npos);
	REQUIRE(scanner.find("loaderWalkerBindMeshSource") !=
		std::string::npos);
	REQUIRE(scanner.find("ASSET_ARCHIVE_META_MANIFEST_PATH") !=
		std::string::npos);

	/* Reused rows retain full identity/lifecycle state on success and restore
	 * that exact state, dependency edges, and provider paths on late failure. */
	REQUIRE(scanner.find("catalogEntrySnapshotCapture(&p->existing_snapshot, entry)") !=
		std::string::npos);
	REQUIRE(scanner.find("catalogEntrySnapshotRestorePreserved(entry, &pending[i].existing_snapshot)") !=
		std::string::npos);
	REQUIRE(scanner.find("fileProviderCheckpointCreate(&provider_checkpoint)") !=
		std::string::npos);
	REQUIRE(scanner.find("fileProviderCheckpointRestore(&provider_checkpoint)") !=
		std::string::npos);
	REQUIRE(scanner.find("PDWEAPON.NESTED.ROLLBACK") !=
		std::string::npos);
	REQUIRE(scanner.find("PDWEAPON.NESTED.MESH_SOURCE") !=
		std::string::npos);
	REQUIRE(header.find("preserve their stable") != std::string::npos);
}

TEST_CASE("pdeffect embedded typed dependencies publish through every ingress",
		"[modding][network][pdxxx][pdeffect][dependencies][b1025][v009]") {
	const std::string header = readFile("port/include/assetcatalog_scanner.h");
	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	/* Source call-chain checks accept checkout CRLF as well as LF. Keep the
	 * binary archive reader byte-exact for the archive mutation tests. */
	scanner.erase(std::remove(scanner.begin(), scanner.end(), '\r'), scanner.end());
	const std::string walker = readFile("port/src/loader_walker_meta.c");

	REQUIRE(header.find("assetCatalogRegisterEffectNestedDependencies") !=
		std::string::npos);
	REQUIRE(scanner.find("dependencies/assets/audio/") != std::string::npos);
	REQUIRE(scanner.find("dependencies/assets/materials/") != std::string::npos);
	REQUIRE(scanner.find("dependencies/assets/textures/") != std::string::npos);
	REQUIRE(scanner.find("embedded effect dependency type mismatch") !=
		std::string::npos);
	REQUIRE(scanner.find("is not referenced by graph") != std::string::npos);
	REQUIRE(scanner.find("embedded effect dependency ID collision") !=
		std::string::npos);
	REQUIRE(scanner.find("PDEFFECT.NESTED.REGISTER") != std::string::npos);
	REQUIRE(scanner.find("effectNestedRollback(registration)") !=
		std::string::npos);

	/* Direct local typed archives and received/mounted pdmods converge on the
	 * same registrar after strict public source parsing. */
	REQUIRE(scanner.find("assetCatalogRegisterEffectNestedDependencies(\n\t\t\t\t\teffect_source.catalog_id, descriptor_path") !=
		std::string::npos);
	REQUIRE(scanner.find("typed_archive_entry && ini_type == ASSET_EFFECT") !=
		std::string::npos);
	REQUIRE(scanner.find("typed_archive_entry && ini_type == ASSET_WEAPON") !=
		std::string::npos);
	REQUIRE(scanner.find("registerWeaponNestedDependenciesWithAnimationBatch(weapon_id,\n\t\t\t\t\t\t\tentry_ref") !=
		std::string::npos);
	REQUIRE(scanner.find("assetCatalogRegisterEffectNestedDependencies(effect_id,\n\t\t\t\t\t\t\tentry_ref") !=
		std::string::npos);
	REQUIRE(walker.find("assetCatalogRegisterEffectNestedDependencies(id, file_path, 1") !=
		std::string::npos);

	/* Weapon-nested effects join their children to the same outer rollback
	 * transaction; effect and weapon owners retain exact typed edges. */
	REQUIRE(scanner.find("effectNestedPrepareBytes(&p->effect_nested") !=
		std::string::npos);
	REQUIRE(scanner.find("weaponNestedPrepareRecursiveEffects(&pending") !=
		std::string::npos);
	REQUIRE(scanner.find("collectWeaponNestedEffectContainer") !=
		std::string::npos);
	REQUIRE(scanner.find("collectEmbeddedEffectArchive") !=
		std::string::npos);
	REQUIRE(scanner.find("recursive effect archive chain exceeds capacity") !=
		std::string::npos);
	REQUIRE(scanner.find("effectNestedCommit(&p->effect_nested") !=
		std::string::npos);
	REQUIRE(scanner.find("effectNestedRollback(&pending[i].effect_nested)") !=
		std::string::npos);
	REQUIRE(scanner.find("catalogDepRegisterTyped(weapon_id, dep->catalog_id") !=
		std::string::npos);
}

TEST_CASE("custom weapon reticle reaches production HUD through catalog pdui",
		"[modding][pdxxx][weapon][pdui][reticle][T-ASSETS-023]") {
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	const std::string runtime = readFile("port/src/weapon_graph_runtime.c");
	const std::string sight = readFile("src/game/sight.c");
	const std::string overlay = readFile("port/fast3d/pdgui_weapon_reticle.cpp");
	const std::string backend = readFile("port/fast3d/pdgui_backend.cpp");
	const std::string theme = readFile("port/fast3d/pdgui_theme.cpp");
	const std::string generator = readFile("tools/build_typed_pdxxx_examples.py");

	/* Public nested source is catalog-visible before presentation validation. */
	REQUIRE(scanner.find("type == ASSET_UI && pathEndsWithNoCase(path, \".pdui\")") !=
		std::string::npos);
	REQUIRE(scanner.find("pass == 0 && expected_type != ASSET_UI") !=
		std::string::npos);
	REQUIRE(runtime.find("table->reticle_ref") != std::string::npos);
	REQUIRE(runtime.find("assetCatalogResolve(held->reticle_ref)") !=
		std::string::npos);
	REQUIRE(runtime.find("entry->type != ASSET_UI") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphReticleImageHeaderValid") !=
		std::string::npos);
	REQUIRE(runtime.find("missing or corrupt public image source") !=
		std::string::npos);

	/* The real sight pass replaces only the reticle and keeps target logic. */
	REQUIRE(sight.find("pdguiWeaponReticleClear();") != std::string::npos);
	REQUIRE(sight.find("pdguiWeaponReticleQueue(held->reticle_ref") !=
		std::string::npos);
	REQUIRE(sight.find("} else switch (sight)") != std::string::npos);
	REQUIRE(sight.find("sightDrawTarget(gdl, crossx, crossy)") !=
		std::string::npos);
	REQUIRE(backend.find("pdguiWeaponReticleIsQueued()") != std::string::npos);
	REQUIRE(backend.find("pdguiWeaponReticleRender((s32)winW, (s32)winH)") !=
		std::string::npos);
	REQUIRE(overlay.find("gfx_current_game_window_viewport") !=
		std::string::npos);
	REQUIRE(overlay.find("AddImage") != std::string::npos);
	REQUIRE(overlay.find("actionmap") == std::string::npos);
	REQUIRE(overlay.find("SDL_") == std::string::npos);
	REQUIRE(theme.find("s_applyCatalogUiAsset(entry, nullptr)") !=
		std::string::npos);

	/* Creator example carries one authoritative catalog binding and archive. */
	REQUIRE(generator.find("reticle_archive = dependencies/assets/ui/reticle.pdui") !=
		std::string::npos);
	REQUIRE(generator.find("\\\"crosshair\\\": \\\"example:tri_reticle\\\"") !=
		std::string::npos);
}

TEST_CASE("pdtheme nested roles register fail closed across every transport",
		"[modding][network][pdxxx][pdtheme][dependencies][T-ASSETS-027]") {
	const std::string header = readFile("port/include/assetcatalog_scanner.h");
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	const std::string walker = readFile("port/src/loader_walker_meta.c");
	const std::string distrib = readFile("port/src/net/netdistrib.c");
	const std::string load = readFile("port/src/assetcatalog_load.c");

	/* One registrar owns strict role/type/source validation for local typed
	 * archives, mounted pdmods, network-extracted archives and base walking. */
	REQUIRE(header.find("assetCatalogRegisterThemeNestedDependencies") !=
		std::string::npos);
	REQUIRE(scanner.find("s_ThemeNestedRoles") != std::string::npos);
	REQUIRE(scanner.find("{ \"ui_archive\", \".pdui\", ASSET_UI }") !=
		std::string::npos);
	REQUIRE(scanner.find("{ \"font_archive\", \".pdfont\", ASSET_FONT }") !=
		std::string::npos);
	REQUIRE(scanner.find("{ \"audio_archive\", \".pdsfx\", ASSET_AUDIO }") !=
		std::string::npos);
	REQUIRE(scanner.find("{ \"music_archive\", \".pdsong\", ASSET_AUDIO }") !=
		std::string::npos);
	REQUIRE(scanner.find("theme effect_archive is retired") !=
		std::string::npos);
	REQUIRE(scanner.find("ASSET_ARCHIVE_VALIDATE_RELEASE") != std::string::npos);
	REQUIRE(scanner.find("theme catalog identity mismatch") != std::string::npos);
	REQUIRE(scanner.find("theme dependency ID/content collision") !=
		std::string::npos);
	REQUIRE(scanner.find("catalogDepReserve(pending_count)") !=
		std::string::npos);
	REQUIRE(scanner.find("catalogDepRegister(theme_id, pending[i].catalog_id") !=
		std::string::npos);
	REQUIRE(scanner.find("if (result < 0)") != std::string::npos);
	REQUIRE(scanner.find("PDTHEME.NESTED.REJECT") != std::string::npos);
	REQUIRE(scanner.find("scanExternalDescriptorPath(mod_dir, \"themes\"") ==
		std::string::npos);
	REQUIRE(walker.find("s_applyThemePublicDescriptor") != std::string::npos);
	REQUIRE(walker.find("assetCatalogRegisterThemeNestedDependencies(id, file_path") !=
		std::string::npos);
	REQUIRE(distrib.find("assetCatalogScanExternalLayoutFolderDeferred(") !=
		std::string::npos);
	REQUIRE(scanner.find("typed_archive_entry && ini_type == ASSET_THEME") !=
		std::string::npos);

	/* Catalog edges are the production retain/release and manifest-expansion
	 * ownership mechanism; dependencies are not theme-loader side storage. */
	REQUIRE(load.find("catalogDepActivationPlanBuild") != std::string::npos);
	REQUIRE(load.find("catalogDepActivationPlanRollback") != std::string::npos);
	REQUIRE(load.find("s_catalogRetainEntry") != std::string::npos);
	REQUIRE(load.find("s_catalogUnloadDepCallback") == std::string::npos);
	REQUIRE(load.find("s_catalogCollectThemeDep") == std::string::npos);
	REQUIRE(load.find("s_catalogRetainThemeDepCallback") == std::string::npos);
}

TEST_CASE("weapon animation command graphs cannot hijack character animnum routing",
		"[modding][pdxxx][animation][b1035]") {
	const std::string common = readFile("port/src/loader_walker_common.c");
	const std::string walker = readFile("port/src/loader_walker_anim.c");
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	const std::string distrib = readFile("port/src/net/netdistrib.c");

	const auto int_start = common.find("s32 loaderWalkerEnvelopeInt(");
	const auto int_end = common.find("s32 loaderWalkerArchiveMemberPath(",
		int_start);
	REQUIRE(int_start != std::string::npos);
	REQUIRE(int_end != std::string::npos);
	const std::string int_block = common.substr(int_start, int_end - int_start);
	REQUIRE(int_block.find("*out_value = 0") == std::string::npos);
	REQUIRE(int_block.find("*out_value = v * sign") != std::string::npos);

	REQUIRE(walker.find("assetCatalogAnimationCategoryUsesCharacterClip(category)") !=
		std::string::npos);
	REQUIRE(walker.find("e->source_animnum = -1") != std::string::npos);
	REQUIRE(scanner.find("assetCatalogAnimationCategoryUsesCharacterClip(e->category)") !=
		std::string::npos);
	REQUIRE(distrib.find("assetCatalogAnimationCategoryUsesCharacterClip(e->category)") !=
		std::string::npos);
}

TEST_CASE("B-1178: entity.autogun's beam_interval counts shots and its ticks60 name is refused",
          "[modding][weapon_graph][b1178]") {
	auto graph = [](const char *asset_id, const char *beam_param) {
		return std::string("{\n"
			"  \"schema\": \"pd.entity_graph.v1\",\n"
			"  \"asset_id\": \"") + asset_id + "\",\n"
			"  \"graph_id\": \"b1178_autogun_beam_v1\",\n"
			"  \"nodes\": [\n"
			"    { \"id\": \"autogun\", \"kind\": \"entity.autogun\", \"params\": {\n"
			"      \"target_filter\": \"hostile_chr\",\n"
			"      " + beam_param + "\n"
			"    } }\n"
			"  ],\n"
			"  \"edges\": [],\n"
			"  \"exports\": [ { \"name\": \"main\", \"node\": \"autogun\" } ]\n"
			"}\n";
	};
	char err[256] = {};
	weaponGraphRuntimeClearAll();

	// The public name: a tracer beam every N shots (the original's literal 4)
	const std::string shots = graph("base:test_b1178_shots", "\"beam_interval\": 3");
	const s32 shots_rc = weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_ENTITY,
		"base:test_b1178_shots", shots.data(), static_cast<u32>(shots.size()),
		err, sizeof(err));
	INFO(err);
	REQUIRE(shots_rc == 0);
	const weapon_graph_entity_runtime_t *entity =
		weaponGraphRuntimeGetEntity("base:test_b1178_shots");
	REQUIRE(entity != nullptr);
	REQUIRE(entity->has_autogun == 1);
	REQUIRE(entity->autogun_beam_every_shots == 3);

	// The old alias named a shot count a time in 60 Hz ticks: refused, not read
	const std::string old = graph("base:test_b1178_ticks60", "\"beam_interval_ticks60\": 60");
	err[0] = '\0';
	REQUIRE(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_ENTITY,
		"base:test_b1178_ticks60", old.data(), static_cast<u32>(old.size()),
		err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("beam_interval_ticks60 is not a time") != std::string::npos);
	REQUIRE(weaponGraphRuntimeGetEntity("base:test_b1178_ticks60") == nullptr);
	weaponGraphRuntimeClearAll();
}

TEST_CASE("mod magazines are flat ammoN_* settings with checked values (crystal pistol)",
		"[modding][pdxxx][weapon_graph][settings][crystal_pistol]") {
	const std::string variables =
		R"({"schema":"pd.weapon_variables.v1","base_damage":1.0,"pierce":0,"cam":"none"})";
	auto registerSettings = [&](const std::string &settings, char *err, size_t cap) {
		const std::string primary = kW5fPrimaryGraph;
		const std::string secondary = kW5fSecondaryGraph;
		const std::string shared = kW5fSharedContext;
		return weaponGraphRuntimeRegisterWeaponSourceJson(70, "base:w5f_weapon",
			primary.data(), static_cast<u32>(primary.size()),
			secondary.data(), static_cast<u32>(secondary.size()),
			shared.data(), static_cast<u32>(shared.size()),
			settings.data(), static_cast<u32>(settings.size()),
			variables.data(), static_cast<u32>(variables.size()),
			nullptr, 0, err, cap);
	};
	char err[256] = {};

	weaponGraphRuntimeClearAll();
	const std::string good = R"({"schema":"pd.weapon_settings.v1","ammo0_clip":6,"ammo0_reload":"regrow",)"
		R"("ammo0_reserve":"none","ammo0_regrow_ticks60":36,"ammo0_seat_part_base":110,)"
		R"("ammo1_clip":20,"ammo1_reserve":"pistol","gun_roll_deg":90})";
	REQUIRE(registerSettings(good, err, sizeof(err)) == 0);
	const weapon_graph_weapon_settings_t *table = weaponGraphRuntimeGetWeaponSettings(70);
	REQUIRE(table != nullptr);
	REQUIRE(table->setting_count == 8);
	weaponGraphRuntimeClearAll();

	const char *suffix = nullptr;
	REQUIRE(weaponGraphAmmoSettingSlot("ammo0_clip", &suffix) == 0);
	REQUIRE(std::string(suffix) == "clip");
	REQUIRE(weaponGraphAmmoSettingSlot("ammo1_regrow_ticks60", &suffix) == 1);
	REQUIRE(weaponGraphAmmoSettingSlot("ammo2_clip", nullptr) == -1);
	REQUIRE(weaponGraphAmmoSettingSlot("ammo0_belt", nullptr) == -1);
	REQUIRE(weaponGraphAmmoSettingSlot("ammo_clip", nullptr) == -1);
	REQUIRE(weaponGraphAmmoReserveType("none") == WEAPON_GRAPH_AMMO_SELFLOADING_TYPE);
	REQUIRE(weaponGraphAmmoReserveType("pistol") == 0x01);
	REQUIRE(weaponGraphAmmoReserveType("magnum") == 0x0a);
	REQUIRE(weaponGraphAmmoReserveType("cloak") == -1);
	REQUIRE(weaponGraphAmmoReserveType(nullptr) == -1);

	const std::vector<std::pair<std::string, std::string>> bad = {
		{ R"({"schema":"pd.weapon_settings.v1","ammo0_clip":0})", "1..255" },
		{ R"({"schema":"pd.weapon_settings.v1","ammo0_clip":256})", "1..255" },
		{ R"({"schema":"pd.weapon_settings.v1","ammo0_clip":"six"})", "1..255" },
		{ R"({"schema":"pd.weapon_settings.v1","ammo0_clip":6,"ammo0_reload":"belt"})", "magazine or regrow" },
		{ R"({"schema":"pd.weapon_settings.v1","ammo0_clip":6,"ammo0_reserve":"cloak"})", "native ammo type" },
		{ R"({"schema":"pd.weapon_settings.v1","ammo0_clip":6,"ammo0_regrow_ticks60":0})", "1..32767" },
		{ R"({"schema":"pd.weapon_settings.v1","ammo0_clip":6,"ammo0_regrow_sound_catalog_id":"nocolon"})", "catalog ID" },
		{ R"({"schema":"pd.weapon_settings.v1","gun_roll_deg":200})", "0..180" },
		{ R"({"schema":"pd.weapon_settings.v1","ammo3_clip":6})", "has no production consumer" },
	};
	for (const auto &c : bad) {
		INFO(c.first);
		err[0] = '\0';
		REQUIRE(registerSettings(c.first, err, sizeof(err)) != 0);
		REQUIRE(std::string(err).find(c.second) != std::string::npos);
		weaponGraphRuntimeClearAll();
	}

	/* The adapter turns the keys into the native magazine; pin its rules. */
	const std::string pool = readFile("port/src/loader_pool.c");
	REQUIRE(pool.find("out_ammo->casingeject = (u32)CASING_NONE;") != std::string::npos);
	REQUIRE(pool.find("out_ammo->reload_animation = NULL; /* mod weapons play no gun animations */") !=
		std::string::npos);
	REQUIRE(pool.find("out_ammo->flags = out_ext->selfloading ? AMMOFLAG_NORESERVE : 0;") !=
		std::string::npos);
	REQUIRE(pool.find("regrow needs ammo%d_reserve none") != std::string::npos);
	REQUIRE(pool.find("it fires without ammo (B-1151)") != std::string::npos);

	/* B-1211: a public weapon's bot preferences are derived, never left zeroed
	 * (botinvTick skips a function whose ammo-goal bit is clear, so bots went
	 * unarmed); a self-loading magazine has no ammo goals and its reload
	 * delay is the full regrow. */
	REQUIRE(pool.find("s_publicWeaponBotPrefsBuild(&s_BotPrefs[runtime_weapon_id], funcs, held,") !=
		std::string::npos);
	REQUIRE(pool.find("out->haspriammogoal = 1;") != std::string::npos);
	REQUIRE(pool.find("out->hassecammogoal = 1;") != std::string::npos);
	REQUIRE(pool.find("if (!ext->selfloading && clip > 0) {") != std::string::npos);
	REQUIRE(pool.find("(clip * ext->regrow_ticks60 + 59) / 60") != std::string::npos);
	REQUIRE(pool.find("memset(&s_BotPrefs[runtime_weapon_id], 0") == std::string::npos);
	const std::string botact = readFile("src/game/botact.c");
	REQUIRE(botact.find("if (aibot && botactIsSelfLoadingModFunction(weaponnum, funcnum)) {") !=
		std::string::npos);
	REQUIRE(botact.find("return bgunGetCapacityByAmmotype(botactGetAmmoTypeByFunction(weaponnum, funcnum));") !=
		std::string::npos);
	const std::string botinv = readFile("src/game/botinv.c");
	REQUIRE(botinv.find("&& bgunModFunctionsShareSelfLoadingAmmo(weaponnum, aibot->gunfunc, funcnum);") !=
		std::string::npos);
	REQUIRE(botinv.find("if ((changingfunc || changinggun) && !keepmagazine) {") != std::string::npos);
	REQUIRE(botinv.find("if (changingfunc && !changinggun && !keepmagazine) {") != std::string::npos);
}

TEST_CASE("crystal pistol charge fan spreads the rounds evenly over plus or minus launch_fan_deg",
		"[modding][weapon_graph][runtime][crystal_pistol]") {
	/* Six rounds at Mike's default of 15: -15, -9, -3, 3, 9, 15 degrees. */
	const f32 six[] = { -15.0f, -9.0f, -3.0f, 3.0f, 9.0f, 15.0f };
	for (s32 j = 0; j < 6; j++) {
		INFO(j);
		REQUIRE(weaponGraphChargeFanAngleDeg(j, 6, WEAPON_GRAPH_CHARGE_FAN_DEFAULT_DEG) == Approx(six[j]));
	}
	/* The ends are the authored half-angle whatever the count. */
	REQUIRE(weaponGraphChargeFanAngleDeg(0, 2, 20.0f) == Approx(-20.0f));
	REQUIRE(weaponGraphChargeFanAngleDeg(1, 2, 20.0f) == Approx(20.0f));
	REQUIRE(weaponGraphChargeFanAngleDeg(2, 5, 20.0f) == Approx(0.0f));
	/* One round goes straight along the aim; a zero fan sends all along it. */
	REQUIRE(weaponGraphChargeFanAngleDeg(0, 1, 15.0f) == Approx(0.0f));
	REQUIRE(weaponGraphChargeFanAngleDeg(4, 6, 0.0f) == Approx(0.0f));
	/* Out of range rounds read as the aim, never past the fan. */
	REQUIRE(weaponGraphChargeFanAngleDeg(-1, 6, 15.0f) == Approx(0.0f));
	REQUIRE(weaponGraphChargeFanAngleDeg(6, 6, 15.0f) == Approx(0.0f));
}

TEST_CASE("crystal pistol charge, cluster and arm-on-land params reach the runtime records",
		"[modding][pdxxx][weapon_graph][runtime][crystal_pistol]") {
	const std::string weaponGraph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"mod_test:charge\",\n"
		"  \"graph_id\": \"charge\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"primary_action\", \"kind\": \"fire.hitscan\",\n"
		"      \"params\": { \"mode\": \"primary\", \"function_type\": \"shoot_single\", \"ammo_slot\": 0 } },\n"
		"    { \"id\": \"secondary_action\", \"kind\": \"fire.charge_release\",\n"
		"      \"params\": { \"mode\": \"secondary\", \"function_type\": \"throw\", \"ammo_slot\": 0,\n"
		"        \"charge_time_ticks60\": 90, \"launch_part_base\": 110, \"launch_aim_part_base\": 130,\n"
		"        \"launch_speed\": 22.0, \"charge_hide_part\": 120, \"charge_show_part\": 121 } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"primary_action\" },\n"
		"    { \"name\": \"secondary\", \"node\": \"secondary_action\" }\n"
		"  ]\n"
		"}\n";
	weapon_graph_ir_t ir;
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, weaponGraph.data(),
		static_cast<u32>(weaponGraph.size()), &ir, err, sizeof(err)) == 0);
	weaponGraphRuntimeClearAll();
	weaponGraphRuntimeSetEnabled(1);
	REQUIRE(weaponGraphRuntimeRegisterHeldIr(12, &ir, err, sizeof(err)) == 0);
	const weapon_graph_held_function_t *primary = weaponGraphRuntimeGetHeldFunctionForGameplay(12, 0);
	const weapon_graph_held_function_t *secondary = weaponGraphRuntimeGetHeldFunctionForGameplay(12, 1);
	REQUIRE(primary != nullptr);
	REQUIRE(secondary != nullptr);
	REQUIRE(primary->has_charge == 0);
	REQUIRE(secondary->has_charge == 1);
	REQUIRE(secondary->charge_time_ticks60 == 90);
	REQUIRE(secondary->launch_part_base == 110);
	REQUIRE(secondary->launch_aim_part_base == 130);
	REQUIRE(secondary->launch_speed == Approx(22.0f));
	REQUIRE(secondary->charge_hide_part == 120);
	REQUIRE(secondary->charge_show_part == 121);
	REQUIRE(secondary->charge_sound == -1);
	REQUIRE(primary->launch_part_base == -1);
	/* Mike's pick (2026-11-15): the charged volley fans plus or minus 15
	 * degrees along the spine unless the node sets launch_fan_deg. */
	REQUIRE(WEAPON_GRAPH_CHARGE_FAN_DEFAULT_DEG == Approx(15.0f));
	REQUIRE(secondary->launch_fan_deg == Approx(15.0f));
	weaponGraphIrFree(&ir);

	/* launch_fan_deg is a tunable node param: 0..90 reaches the record, any
	 * other value is refused before a record is published (SP-76). */
	const auto fanGraph = [](const std::string &fan) {
		return std::string(
			"{\n  \"schema\": \"pd.weapon_graph.v1\",\n  \"asset_id\": \"mod_test:fan\",\n"
			"  \"graph_id\": \"fan\",\n  \"nodes\": [\n"
			"    { \"id\": \"secondary_action\", \"kind\": \"fire.charge_release\",\n"
			"      \"params\": { \"mode\": \"secondary\", \"function_type\": \"throw\", \"ammo_slot\": 0,\n"
			"        \"charge_time_ticks60\": 90, \"launch_fan_deg\": ") + fan + " } }\n"
			"  ],\n  \"edges\": [],\n"
			"  \"exports\": [ { \"name\": \"secondary\", \"node\": \"secondary_action\" } ]\n}\n";
	};
	const struct { const char *value; f32 expected; } fanAccepted[] = {
		{ "20.0", 20.0f }, { "0", 0.0f }, { "90", 90.0f }, { "7.5", 7.5f },
	};
	for (const auto &row : fanAccepted) {
		const std::string graph = fanGraph(row.value);
		INFO(graph);
		REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
			static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);
		const s32 fanResult = weaponGraphRuntimeRegisterHeldIr(13, &ir, err, sizeof(err));
		INFO(err);
		REQUIRE(fanResult == 0);
		const weapon_graph_held_function_t *fan = weaponGraphRuntimeGetHeldFunctionForGameplay(13, 1);
		REQUIRE(fan != nullptr);
		REQUIRE(fan->has_charge == 1);
		REQUIRE(fan->launch_fan_deg == Approx(row.expected));
		weaponGraphIrFree(&ir);
	}
	for (const char *value : { "-5", "120", "90.5", "\"wide\"" }) {
		const std::string graph = fanGraph(value);
		INFO(graph);
		char fanErr[256] = {};
		s32 result = weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
			static_cast<u32>(graph.size()), &ir, fanErr, sizeof(fanErr));
		if (result == 0) {
			result = weaponGraphRuntimeRegisterHeldIr(13, &ir, fanErr, sizeof(fanErr));
			weaponGraphIrFree(&ir);
		}
		INFO(fanErr);
		REQUIRE(result != 0);
		REQUIRE(std::string(fanErr).find("launch_fan_deg must be a number from 0 to 90") != std::string::npos);
	}

	const std::string projectileGraph =
		"{ \"schema\": \"pd.projectile_graph.v1\", \"asset_id\": \"mod_test:crystal\", \"graph_id\": \"p\",\n"
		"  \"nodes\": [ { \"id\": \"motion\", \"kind\": \"projectile.motion\", \"params\": { \"motion_kind\": \"powered\", \"speed\": 40.0 } },\n"
		"    { \"id\": \"stick\", \"kind\": \"projectile.sticky_attach\", \"params\": {\n"
		"    \"allow_background\": true, \"cluster_count\": 3, \"cluster_radius\": 150.0 } } ],\n"
		"  \"edges\": [], \"exports\": [ { \"name\": \"main\", \"node\": \"stick\" } ] }\n";
	REQUIRE(weaponGraphCompileJson(ASSET_PROJECTILE, projectileGraph.data(),
		static_cast<u32>(projectileGraph.size()), &ir, err, sizeof(err)) == 0);
	const s32 projectileResult = weaponGraphRuntimeRegisterProjectileIr(&ir, err, sizeof(err));
	INFO(err);
	REQUIRE(projectileResult == 0);
	const weapon_graph_projectile_runtime_t *projectile = weaponGraphRuntimeGetProjectile("mod_test:crystal");
	REQUIRE(projectile != nullptr);
	REQUIRE(projectile->has_sticky_attach == 1);
	REQUIRE(projectile->cluster_count == 3);
	REQUIRE(projectile->cluster_radius == Approx(150.0f));
	weaponGraphIrFree(&ir);

	const std::string entityGraph =
		"{ \"schema\": \"pd.entity_graph.v1\", \"asset_id\": \"mod_test:mine\", \"graph_id\": \"e\",\n"
		"  \"nodes\": [ { \"id\": \"proxy\", \"kind\": \"entity.proxy_trigger\", \"params\": {\n"
		"    \"radius\": 200, \"arm_on\": \"land\" } } ],\n"
		"  \"edges\": [], \"exports\": [ { \"name\": \"main\", \"node\": \"proxy\" } ] }\n";
	REQUIRE(weaponGraphCompileJson(ASSET_ENTITY, entityGraph.data(),
		static_cast<u32>(entityGraph.size()), &ir, err, sizeof(err)) == 0);
	const s32 entityResult = weaponGraphRuntimeRegisterEntityIr(&ir, err, sizeof(err));
	INFO(err);
	REQUIRE(entityResult == 0);
	const weapon_graph_entity_runtime_t *entity = weaponGraphRuntimeGetEntity("mod_test:mine");
	REQUIRE(entity != nullptr);
	REQUIRE(entity->has_proxy_trigger == 1);
	REQUIRE(entity->proxy_arm_on_land == 1);
	REQUIRE(entity->proxy_radius == Approx(200.0f));
	weaponGraphIrFree(&ir);

	/* SP-76 (CORE ask, 2026-11-15): an authored rule that cannot work is
	 * refused when the graph registers, not read as "no rule". */
	const struct { asset_type_e type; const char *params; const char *kind; const char *error; } refused[] = {
		{ ASSET_PROJECTILE, "\"cluster_count\": 1, \"cluster_radius\": 150.0", "projectile.sticky_attach",
			"cluster_count of 2 or more" },
		{ ASSET_PROJECTILE, "\"cluster_count\": 3, \"cluster_radius\": 0", "projectile.sticky_attach",
			"cluster_radius above 0" },
		{ ASSET_PROJECTILE, "\"cluster_count\": 3", "projectile.sticky_attach", "cluster_radius above 0" },
		{ ASSET_PROJECTILE, "\"cluster_radius\": 150.0", "projectile.sticky_attach",
			"cluster_count of 2 or more" },
		{ ASSET_PROJECTILE, "\"cluster_count\": \"three\", \"cluster_radius\": 150.0", "projectile.sticky_attach",
			"cluster_count of 2 or more" },
		{ ASSET_ENTITY, "\"radius\": 200, \"arm_on\": \"landing\"", "entity.proxy_trigger", "unsupported arm_on" },
	};
	for (const auto &row : refused) {
		const bool projectileRow = row.type == ASSET_PROJECTILE;
		const std::string graph = std::string("{ \"schema\": \"") +
			(projectileRow ? "pd.projectile_graph.v1" : "pd.entity_graph.v1") +
			"\", \"asset_id\": \"mod_test:refused\", \"graph_id\": \"r\",\n  \"nodes\": [ " +
			(projectileRow ? "{ \"id\": \"motion\", \"kind\": \"projectile.motion\", \"params\": "
				"{ \"motion_kind\": \"powered\", \"speed\": 40.0 } }, " : "") +
			"{ \"id\": \"n\", \"kind\": \"" + row.kind + "\", \"params\": { " + row.params + " } } ],\n"
			"  \"edges\": [], \"exports\": [ { \"name\": \"main\", \"node\": \"n\" } ] }\n";
		INFO(graph);
		char refusedErr[256] = {};
		/* Refused at compile (policy values) or at registration (rules
		 * that need the node's numbers); either way before any record. */
		s32 result = weaponGraphCompileJson(row.type, graph.data(), static_cast<u32>(graph.size()), &ir,
			refusedErr, sizeof(refusedErr));
		if (result == 0) {
			result = projectileRow
				? weaponGraphRuntimeRegisterProjectileIr(&ir, refusedErr, sizeof(refusedErr))
				: weaponGraphRuntimeRegisterEntityIr(&ir, refusedErr, sizeof(refusedErr));
			weaponGraphIrFree(&ir);
		}
		INFO(refusedErr);
		REQUIRE(result != 0);
		REQUIRE(std::string(refusedErr).find(row.error) != std::string::npos);
	}
	weaponGraphRuntimeSetEnabled(0);
	weaponGraphRuntimeClearAll();

	/* Where the game consumes them (src/game/bondgun_mod.c and its hooks). */
	const std::string mod = readFile("src/game/bondgun_mod.c");
	const std::string bondgun = readFile("src/game/bondgun.c");
	const std::string propobj = readFile("src/game/propobj.c");
	const std::string lv = readFile("src/game/lv.c");
	REQUIRE(mod.find("bool bgunModTickIncAttackCharge(") != std::string::npos);
	REQUIRE(mod.find("void bgunModChargeTick(void)") != std::string::npos);
	/* Every launch path (the player's, with or without sockets, and a bot's)
	 * fans by the function's launch_fan_deg; no fixed spacing is left. */
	{
		const std::string call = "weaponGraphChargeFanAngleDeg(j, charge->count, held->launch_fan_deg)";
		size_t calls = 0;
		for (size_t at = mod.find(call); at != std::string::npos; at = mod.find(call, at + 1)) {
			calls++;
		}
		REQUIRE(calls == 2);
		REQUIRE(mod.find("(6.0f * M_PI / 180.0f)") == std::string::npos);
		REQUIRE(mod.find("(5.0f * M_PI / 180.0f)") == std::string::npos);
	}
	REQUIRE(mod.find("void bgunModProjectileLanded(struct weaponobj *weapon)") != std::string::npos);
	REQUIRE(bondgun.find("finished = bgunModTickIncAttackCharge(info, handnum, hand, graph);") !=
		std::string::npos);
	REQUIRE(lv.find("bgunModChargeTick();") != std::string::npos);
	REQUIRE(propobj.find("bgunModProjectileLanded((struct weaponobj *) obj);") != std::string::npos);
	REQUIRE(propobj.find("bgunModProjectileForget(weapon);") != std::string::npos);
	/* MODS ask 6: bots, NPCs and players meet a mod mine's own radius and
	 * filters in the native proxy sweep; passing objects do not set it off. */
	REQUIRE(propobj.find("coordTriggerProxiesFor(chr, &pos, true);") != std::string::npos);
	REQUIRE(propobj.find("s32 decision = bgunModProxyDecision(weapon, chr, pos);") != std::string::npos);
	REQUIRE(mod.find("return entity->proxy_target_filter_mode == 1 ? 0 : -1;") != std::string::npos);
	/* B-1187: on a scenario-source stage the sticky hit test ray-casts the
	 * world collision mesh (rooms there have no vertex batches), so crystals
	 * and mines stick to floors instead of falling through them. */
	REQUIRE(propobj.find("usemesh = bgUsesScenarioMeshHits();") != std::string::npos);
	REQUIRE(propobj.find("bgTestHitScenarioMesh(&prop->pos, &sp1c4, true, &hitthing, NULL)") != std::string::npos);
	REQUIRE(propobj.find("for (i = 0; !usemesh && spcc[i] != -1; i++) {") != std::string::npos);
	const std::string bg = readFile("src/game/bg.c");
	REQUIRE(bg.find("bool bgIsScenarioSource(void)") != std::string::npos);
	/* B-1189: the other background hit tests on such a stage: hitscan (the
	 * Farsight's x-ray mode still skips the whole test and shoots through
	 * walls), the hitscan line-of-sight test, blood splats, spawn clearance
	 * (every surface) and explosion scorch marks. */
	REQUIRE(bg.find("mask = GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2 | GEOFLAG_BLOCK_SHOOT;") != std::string::npos);
	const std::string prop = readFile("src/game/prop.c");
	const size_t farsight = prop.find(
		"if (shotdata.gset.weaponnum != WEAPON_FARSIGHT || g_Vars.currentplayer->visionmode != VISIONMODE_XRAY) {");
	const size_t shotmesh = prop.find("bgTestHitScenarioMesh(&shotdata.gunpos3d, &hitpos, true, &sp664, &hitroom)");
	REQUIRE(farsight != std::string::npos);
	REQUIRE(shotmesh != std::string::npos);
	REQUIRE(farsight < shotmesh);
	REQUIRE(prop.find("for (i = 0; !usemesh && rooms[i] != -1; i++) {") != std::string::npos);
	REQUIRE(prop.find("bgTestHitScenarioMesh(&shotdata.gunpos3d, endpos3d, true, &sp664, NULL)") != std::string::npos);
	REQUIRE(readFile("src/game/splat.c").find(
		"bgTestHitScenarioMesh(&stackshotdata.gunpos3d, &endpos, true, &hitthing, &hitroom)") != std::string::npos);
	const std::string spawnpool = readFile("src/game/spawnpool.c");
	REQUIRE(spawnpool.find("bgTestHitScenarioMesh((struct coord *)pos, &endpoint, false, &hit, NULL)\n\t\t\t: bgTestHitInRoom(") !=
		std::string::npos);
	REQUIRE(spawnpool.find("? bgTestHitScenarioMesh((struct coord *)pos, &endpoint, false, &hit, NULL)\n\t\t\t: bgTestHitInRoom((struct coord *)pos, &endpoint, room, &hit)) {") !=
		std::string::npos);
	REQUIRE(readFile("src/game/explosions.c").find("if (g_Rooms[exp->room].gfxdata || bgIsScenarioSource()) {") !=
		std::string::npos);
	/* Spent rounds from a self-loading magazine are never pickups (a bot
	 * collected resting crystal mines as the whole weapon). */
	REQUIRE(mod.find("bool bgunModGsetFiresSpentRounds(struct gset *gset)") != std::string::npos);
	REQUIRE(propobj.find("if (bgunModGsetFiresSpentRounds(gset)) {\n\t\t\tweapon->base.flags |= OBJFLAG_UNCOLLECTABLE;") !=
		std::string::npos);
}

} // namespace
