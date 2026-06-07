#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "catch.hpp"

extern "C" {
#include "asset_runtime.h"
}

static void initEntry(asset_entry_t &entry, asset_type_e type, const char *id)
{
	std::memset(&entry, 0, sizeof(entry));
	entry.type = type;
	entry.enabled = 1;
	std::strncpy(entry.id, id, sizeof(entry.id) - 1);
}

static std::string str(const char *s)
{
	return s ? std::string(s) : std::string();
}

class TempRuntimeFile {
public:
	explicit TempRuntimeFile(const char *name, const char *contents)
	{
		path = std::filesystem::temp_directory_path() / name;
		std::ofstream out(path, std::ios::binary);
		out << contents;
	}

	~TempRuntimeFile()
	{
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}

	std::string string() const
	{
		return path.string();
	}

private:
	std::filesystem::path path;
};

TEST_CASE("asset runtime adapters bind C-3838 file-backed families",
          "[modding][pdxxx][runtime][c3838][adapters]") {
	assetRuntimeReset();

	asset_entry_t skin;
	initEntry(skin, ASSET_SKIN, "mod:skin_blue");
	std::strncpy(skin.ext.skin.target_id, "base:character_joanna",
		sizeof(skin.ext.skin.target_id) - 1);
	std::strncpy(skin.ext.skin.texture_file, "texture.tga",
		sizeof(skin.ext.skin.texture_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&skin,
		"mods/demo/skins/blue.pdskin::texture.tga") == 1);
	const asset_runtime_binding_t *skinBinding = assetRuntimeFind("mod:skin_blue");
	REQUIRE(skinBinding != nullptr);
	REQUIRE(skinBinding->type == ASSET_SKIN);
	REQUIRE(str(skinBinding->primary_path) == "mods/demo/skins/blue.pdskin::texture.tga");
	REQUIRE(str(skinBinding->authored_file) == "texture.tga");
	REQUIRE(str(skinBinding->target_id) == "base:character_joanna");
	REQUIRE(assetRuntimeFindByTarget(ASSET_SKIN, "base:character_joanna") ==
		skinBinding);

	asset_entry_t material;
	initEntry(material, ASSET_MATERIAL, "mod:material_steel");
	std::strncpy(material.ext.material.material_file, "material.json",
		sizeof(material.ext.material.material_file) - 1);
	std::strncpy(material.ext.material.texture_archive,
		"dependencies/assets/texture/steel.pdtexture",
		sizeof(material.ext.material.texture_archive) - 1);
	std::strncpy(material.ext.material.effect_archive,
		"dependencies/assets/effect/spark.pdeffect",
		sizeof(material.ext.material.effect_archive) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&material,
		"mods/demo/materials/steel.pdmaterial::material.json") == 1);
	const asset_runtime_binding_t *materialBinding =
		assetRuntimeFind("mod:material_steel");
	REQUIRE(materialBinding != nullptr);
	REQUIRE(materialBinding->type == ASSET_MATERIAL);
	REQUIRE(assetRuntimeFindByTypeAndId(ASSET_MATERIAL,
		"mod:material_steel") == materialBinding);
	REQUIRE(str(materialBinding->authored_file) == "material.json");
	REQUIRE(str(materialBinding->dependency_a) ==
		"dependencies/assets/texture/steel.pdtexture");
	REQUIRE(str(materialBinding->dependency_b) ==
		"dependencies/assets/effect/spark.pdeffect");

	asset_entry_t effect;
	initEntry(effect, ASSET_EFFECT, "mod:effect_glow");
	effect.ext.effect.effect_type = EFFECT_TYPE_GLOW;
	effect.ext.effect.target = EFFECT_TARGET_WEAPON;
	effect.ext.effect.intensity = 0.75f;
	std::strncpy(effect.ext.effect.effect_file, "effect.graph.json",
		sizeof(effect.ext.effect.effect_file) - 1);
	std::strncpy(effect.ext.effect.shader_id, "weapon_glow",
		sizeof(effect.ext.effect.shader_id) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&effect,
		"mods/demo/effects/glow.pdeffect::effect.graph.json") == 1);
	const asset_runtime_binding_t *effectBinding =
		assetRuntimeFind("mod:effect_glow");
	REQUIRE(effectBinding != nullptr);
	REQUIRE(effectBinding->kind == EFFECT_TYPE_GLOW);
	REQUIRE(effectBinding->target_kind == EFFECT_TARGET_WEAPON);
	REQUIRE(effectBinding->value0 == Approx(0.75f));
	REQUIRE(str(effectBinding->shader_id) == "weapon_glow");
	REQUIRE(assetRuntimeFindByTypeKind(ASSET_EFFECT, EFFECT_TYPE_GLOW) ==
		effectBinding);

	asset_entry_t vehicle;
	initEntry(vehicle, ASSET_VEHICLE, "mod:vehicle_hoverbike");
	std::strncpy(vehicle.ext.vehicle.model_file, "model.gltf",
		sizeof(vehicle.ext.vehicle.model_file) - 1);
	std::strncpy(vehicle.ext.vehicle.physics_file, "physics.json",
		sizeof(vehicle.ext.vehicle.physics_file) - 1);
	std::strncpy(vehicle.ext.vehicle.behavior_graph, "behavior.graph.json",
		sizeof(vehicle.ext.vehicle.behavior_graph) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&vehicle,
		"mods/demo/vehicles/bike.pdvehicle::model.gltf") == 1);
	const asset_runtime_binding_t *vehicleBinding =
		assetRuntimeFind("mod:vehicle_hoverbike");
	REQUIRE(vehicleBinding != nullptr);
	REQUIRE(str(vehicleBinding->authored_file) == "model.gltf");
	REQUIRE(str(vehicleBinding->dependency_a) == "physics.json");
	REQUIRE(str(vehicleBinding->dependency_b) == "behavior.graph.json");

	asset_entry_t prop;
	initEntry(prop, ASSET_PROP, "mod:prop_laptop_decoy");
	prop.ext.prop.prop_type = 87;
	prop.ext.prop.flags = 0x12;
	prop.ext.prop.health = 125.0f;
	std::strncpy(prop.ext.prop.name, "Laptop Decoy",
		sizeof(prop.ext.prop.name) - 1);
	std::strncpy(prop.ext.prop.model_file, "model.gltf",
		sizeof(prop.ext.prop.model_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&prop,
		"mods/demo/props/laptop_decoy.pdprop::model.gltf") == 1);
	const asset_runtime_binding_t *propBinding =
		assetRuntimeFind("mod:prop_laptop_decoy");
	REQUIRE(propBinding != nullptr);
	REQUIRE(propBinding->runtime_id == 87);
	REQUIRE(propBinding->kind == 87);
	REQUIRE(propBinding->target_kind == 0x12);
	REQUIRE(propBinding->value0 == Approx(125.0f));
	REQUIRE(str(propBinding->authored_file) == "model.gltf");
	REQUIRE(assetRuntimeFindByTypeKind(ASSET_PROP, 87) == propBinding);

	asset_entry_t mission;
	initEntry(mission, ASSET_MISSION, "mod:mission_rescue");
	std::strncpy(mission.ext.mission.mission_graph_file, "mission.graph.json",
		sizeof(mission.ext.mission.mission_graph_file) - 1);
	std::strncpy(mission.ext.mission.scenario_archive,
		"dependencies/assets/scenario/rescue.pdscenario",
		sizeof(mission.ext.mission.scenario_archive) - 1);
	std::strncpy(mission.ext.mission.objectives_file, "objectives.json",
		sizeof(mission.ext.mission.objectives_file) - 1);
	std::strncpy(mission.ext.mission.briefing_file, "briefing.json",
		sizeof(mission.ext.mission.briefing_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&mission,
		"mods/demo/missions/rescue.pdmission::mission.graph.json") == 1);
	const asset_runtime_binding_t *missionBinding =
		assetRuntimeFind("mod:mission_rescue");
	REQUIRE(missionBinding != nullptr);
	REQUIRE(str(missionBinding->authored_file) == "mission.graph.json");
	REQUIRE(str(missionBinding->dependency_a) ==
		"dependencies/assets/scenario/rescue.pdscenario");
	REQUIRE(str(missionBinding->dependency_b) == "objectives.ini");

	asset_entry_t hud;
	initEntry(hud, ASSET_HUD, "mod:hud_classic");
	hud.ext.hud.hud_id = 9;
	hud.ext.hud.element_type = HUD_ELEM_AMMO;
	std::strncpy(hud.ext.hud.texture_file, "texture.png",
		sizeof(hud.ext.hud.texture_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&hud,
		"mods/demo/hud/classic.pdhud::texture.png") == 1);
	const asset_runtime_binding_t *hudBinding = assetRuntimeFind("mod:hud_classic");
	REQUIRE(hudBinding != nullptr);
	REQUIRE(hudBinding->runtime_id == 9);
	REQUIRE(hudBinding->kind == HUD_ELEM_AMMO);
	REQUIRE(str(hudBinding->authored_file) == "texture.png");
	REQUIRE(assetRuntimeFindByTypeKind(ASSET_HUD, HUD_ELEM_AMMO) ==
		hudBinding);

	asset_entry_t theme;
	initEntry(theme, ASSET_THEME, "mod:theme_dark");
	std::strncpy(theme.ext.theme.theme_file, "theme.json",
		sizeof(theme.ext.theme.theme_file) - 1);
	std::strncpy(theme.ext.theme.ui_archive,
		"dependencies/assets/ui/chrome.pdui",
		sizeof(theme.ext.theme.ui_archive) - 1);
	std::strncpy(theme.ext.theme.font_archive,
		"dependencies/assets/font/body.pdfont",
		sizeof(theme.ext.theme.font_archive) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&theme,
		"mods/demo/themes/dark.pdtheme::theme.json") == 1);
	const asset_runtime_binding_t *themeBinding =
		assetRuntimeFind("mod:theme_dark");
	REQUIRE(themeBinding != nullptr);
	REQUIRE(str(themeBinding->authored_file) == "theme.json");
	REQUIRE(str(themeBinding->dependency_a) ==
		"dependencies/assets/ui/chrome.pdui");
	REQUIRE(str(themeBinding->dependency_b) ==
		"dependencies/assets/font/body.pdfont");

	REQUIRE(assetRuntimeCount(ASSET_SKIN) == 1);
	REQUIRE(assetRuntimeCount(ASSET_NONE) == 8);
}

TEST_CASE("asset runtime adapters validate primary file accessibility",
          "[modding][pdxxx][runtime][c3838][adapters][files]") {
	TempRuntimeFile file("pd2_asset_runtime_c3838_primary.txt",
		"runtime-primary-payload");
	assetRuntimeReset();

	asset_entry_t hud;
	initEntry(hud, ASSET_HUD, "mod:hud_accessible");
	hud.ext.hud.hud_id = 12;
	hud.ext.hud.element_type = HUD_ELEM_RADAR;
	std::strncpy(hud.ext.hud.texture_file, "radar.png",
		sizeof(hud.ext.hud.texture_file) - 1);

	const std::string path = file.string();
	REQUIRE(assetRuntimeActivateCatalogEntry(&hud, path.c_str()) == 1);
	const asset_runtime_binding_t *binding =
		assetRuntimeFindByTypeKind(ASSET_HUD, HUD_ELEM_RADAR);
	REQUIRE(binding != nullptr);
	REQUIRE(assetRuntimePrimaryFileAccessible(binding) == 1);
	REQUIRE(assetRuntimeAccessibleCount(ASSET_HUD) == 1);

	u32 size = 0;
	void *bytes = assetRuntimeLoadPrimaryFile(binding, &size);
	REQUIRE(bytes != nullptr);
	REQUIRE(size == std::string("runtime-primary-payload").size());
	std::free(bytes);

	asset_entry_t prop;
	initEntry(prop, ASSET_PROP, "mod:prop_accessible");
	prop.ext.prop.prop_type = 32;
	prop.ext.prop.health = 80.0f;
	std::strncpy(prop.ext.prop.model_file, "model.gltf",
		sizeof(prop.ext.prop.model_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&prop, path.c_str()) == 1);
	const asset_runtime_binding_t *propBinding =
		assetRuntimeFindByTypeKind(ASSET_PROP, 32);
	REQUIRE(propBinding != nullptr);
	REQUIRE(assetRuntimePrimaryFileAccessible(propBinding) == 1);
	REQUIRE(assetRuntimeAccessibleCount(ASSET_PROP) == 1);
}

TEST_CASE("asset runtime adapters reject file-backed families without accessible payloads",
          "[modding][pdxxx][runtime][c3838][adapters][files]") {
	assetRuntimeReset();

	asset_entry_t material;
	initEntry(material, ASSET_MATERIAL, "mod:material_empty");
	REQUIRE(assetRuntimeActivateCatalogEntry(&material, "") == 0);
	REQUIRE(assetRuntimeFind("mod:material_empty") == nullptr);

	asset_entry_t mode;
	initEntry(mode, ASSET_GAMEMODE, "mod:gamemode_training");
	mode.ext.gamemode.mode_id = 44;
	mode.ext.gamemode.min_players = 1;
	mode.ext.gamemode.team_based = 0;
	REQUIRE(assetRuntimeActivateCatalogEntry(&mode, "") == 0);
	REQUIRE(assetRuntimeFind("mod:gamemode_training") == nullptr);
	std::strncpy(mode.ext.gamemode.rules_file, "rules.json",
		sizeof(mode.ext.gamemode.rules_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&mode,
		"mods/demo/gamemodes/training.pdgamemode::rules.json") == 1);
	const asset_runtime_binding_t *modeBinding =
		assetRuntimeFind("mod:gamemode_training");
	REQUIRE(modeBinding != nullptr);
	REQUIRE(modeBinding->runtime_id == 44);
	REQUIRE(str(modeBinding->authored_file) == "rules.json");

	assetRuntimeReleaseCatalogEntry("mod:gamemode_training");
	REQUIRE(assetRuntimeFind("mod:gamemode_training") == nullptr);

	asset_entry_t bot;
	initEntry(bot, ASSET_BOT_PROFILE, "mod:bot_jumper");
	bot.ext.bot_profile.type = 2;
	bot.ext.bot_profile.difficulty = 3;
	REQUIRE(assetRuntimeActivateCatalogEntry(&bot, "") == 0);
	REQUIRE(assetRuntimeFind("mod:bot_jumper") == nullptr);
	std::strncpy(bot.ext.bot_profile.target_body, "base:body_training",
		sizeof(bot.ext.bot_profile.target_body) - 1);
	std::strncpy(bot.ext.bot_profile.profile_file, "profile.json",
		sizeof(bot.ext.bot_profile.profile_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&bot,
		"mods/demo/bots/jumper.pdbotprofile::profile.json") == 1);
	const asset_runtime_binding_t *botBinding = assetRuntimeFind("mod:bot_jumper");
	REQUIRE(botBinding != nullptr);
	REQUIRE(botBinding->runtime_id == 2);
	REQUIRE(botBinding->kind == 3);
	REQUIRE(str(botBinding->authored_file) == "profile.json");
	REQUIRE(str(botBinding->target_id) == "base:body_training");
}
