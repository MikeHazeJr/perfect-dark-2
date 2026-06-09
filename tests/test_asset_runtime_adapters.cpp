#include <cstring>
#include <cstdlib>
#include <cstdio>
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

static void fillStrictScenario(asset_entry_t &scenario)
{
	std::strncpy(scenario.ext.scenario.scene_file, "scene.glb",
		sizeof(scenario.ext.scenario.scene_file) - 1);
	std::strncpy(scenario.ext.scenario.rooms_file, "rooms.json",
		sizeof(scenario.ext.scenario.rooms_file) - 1);
	std::strncpy(scenario.ext.scenario.portals_file, "portals.json",
		sizeof(scenario.ext.scenario.portals_file) - 1);
	std::strncpy(scenario.ext.scenario.pads_file, "pads.json",
		sizeof(scenario.ext.scenario.pads_file) - 1);
	std::strncpy(scenario.ext.scenario.spawns_file, "spawns.json",
		sizeof(scenario.ext.scenario.spawns_file) - 1);
	std::strncpy(scenario.ext.scenario.volumes_file, "volumes.json",
		sizeof(scenario.ext.scenario.volumes_file) - 1);
	std::strncpy(scenario.ext.scenario.objects_file, "objects.json",
		sizeof(scenario.ext.scenario.objects_file) - 1);
	std::strncpy(scenario.ext.scenario.setup_fields_file,
		"setup.fields.json", sizeof(scenario.ext.scenario.setup_fields_file) - 1);
	std::strncpy(scenario.ext.scenario.ai_lists_file, "ai/ailists.json",
		sizeof(scenario.ext.scenario.ai_lists_file) - 1);
	std::strncpy(scenario.ext.scenario.objectives_file, "objectives.json",
		sizeof(scenario.ext.scenario.objectives_file) - 1);
	std::strncpy(scenario.ext.scenario.navigation_file, "navigation.ini",
		sizeof(scenario.ext.scenario.navigation_file) - 1);
	std::strncpy(scenario.ext.scenario.navigation_waypoints_file,
		"navigation/waypoints.json",
		sizeof(scenario.ext.scenario.navigation_waypoints_file) - 1);
	std::strncpy(scenario.ext.scenario.navigation_waygroups_file,
		"navigation/waygroups.json",
		sizeof(scenario.ext.scenario.navigation_waygroups_file) - 1);
	std::strncpy(scenario.ext.scenario.navigation_covers_file,
		"navigation/covers.json",
		sizeof(scenario.ext.scenario.navigation_covers_file) - 1);
	std::strncpy(scenario.ext.scenario.navigation_paths_file,
		"navigation/paths.json",
		sizeof(scenario.ext.scenario.navigation_paths_file) - 1);
	std::strncpy(scenario.ext.scenario.level_graph_file, "level.graph.json",
		sizeof(scenario.ext.scenario.level_graph_file) - 1);
}

TEST_CASE("asset runtime adapters bind C-3838 file-backed families",
          "[modding][pdxxx][runtime][c3838][adapters]") {
	assetRuntimeReset();

	asset_entry_t character;
	initEntry(character, ASSET_CHARACTER, "mod:character_triangle");
	std::strncpy(character.ext.character.bodyfile,
		"dependencies/assets/body/tri_body.pdbody",
		sizeof(character.ext.character.bodyfile) - 1);
	std::strncpy(character.ext.character.headfile,
		"dependencies/assets/head/tri_head.pdhead",
		sizeof(character.ext.character.headfile) - 1);
	std::strncpy(character.ext.character.portrait_file,
		"portrait.png",
		sizeof(character.ext.character.portrait_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&character,
		"mods/demo/characters/tri.pdcharacter::dependencies/assets/body/tri_body.pdbody") == 1);
	const asset_runtime_binding_t *characterBinding =
		assetRuntimeFind("mod:character_triangle");
	REQUIRE(characterBinding != nullptr);
	REQUIRE(str(characterBinding->authored_file) ==
		"dependencies/assets/body/tri_body.pdbody");
	REQUIRE(str(characterBinding->dependency_a) ==
		"dependencies/assets/head/tri_head.pdhead");
	REQUIRE(str(characterBinding->dependency_b) == "portrait.png");

	asset_entry_t arena;
	initEntry(arena, ASSET_ARENA, "mod:arena_training");
	arena.ext.arena.stagenum = 0x5e;
	arena.ext.arena.load_mode = ARENA_LOADMODE_PLAYABLE;
	arena.ext.arena.requirefeature = 7;
	arena.ext.arena.name_langid = 1234;
	std::strncpy(arena.ext.arena.scenario_id, "example:tri_scenario",
		sizeof(arena.ext.arena.scenario_id) - 1);
	std::strncpy(arena.ext.arena.scenario_archive,
		"dependencies/assets/scenarios/tri_scenario.pdscenario",
		sizeof(arena.ext.arena.scenario_archive) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&arena,
		"mods/demo/arenas/tri_arena.pdarena::dependencies/assets/scenarios/tri_scenario.pdscenario") == 1);
	const asset_runtime_binding_t *arenaBinding =
		assetRuntimeFind("mod:arena_training");
	REQUIRE(arenaBinding != nullptr);
	REQUIRE(arenaBinding->type == ASSET_ARENA);
	REQUIRE(str(arenaBinding->authored_file) ==
		"dependencies/assets/scenarios/tri_scenario.pdscenario");
	REQUIRE(str(arenaBinding->target_id) == "example:tri_scenario");
	REQUIRE(arenaBinding->runtime_id == 0x5e);
	REQUIRE(arenaBinding->kind == ARENA_LOADMODE_PLAYABLE);
	REQUIRE(arenaBinding->target_kind == 7);
	REQUIRE(arenaBinding->name_langid == 1234);
	REQUIRE(arenaBinding->requirefeature == 7);

	asset_entry_t body;
	initEntry(body, ASSET_BODY, "mod:body_triangle");
	body.ext.body.bodynum = 17;
	body.ext.body.headnum = 5;
	body.ext.body.name_langid = 4321;
	body.ext.body.requirefeature = 9;
	std::strncpy(body.ext.body.display_name, "Triangle Agent",
		sizeof(body.ext.body.display_name) - 1);
	std::strncpy(body.ext.body.mesh_archive, "mesh.pdmesh",
		sizeof(body.ext.body.mesh_archive) - 1);
	std::strncpy(body.ext.body.hand_archive, "hand.pdmesh",
		sizeof(body.ext.body.hand_archive) - 1);
	std::strncpy(body.ext.body.rig_class, "human_male_neck_standard",
		sizeof(body.ext.body.rig_class) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&body,
		"mods/demo/bodies/tri_body.pdbody::mesh.pdmesh") == 1);
	const asset_runtime_binding_t *bodyBinding =
		assetRuntimeFind("mod:body_triangle");
	REQUIRE(bodyBinding != nullptr);
	REQUIRE(bodyBinding->type == ASSET_BODY);
	REQUIRE(str(bodyBinding->authored_file) == "mesh.pdmesh");
	REQUIRE(str(bodyBinding->dependency_a) == "hand.pdmesh");
	REQUIRE(str(bodyBinding->target_id) == "human_male_neck_standard");
	REQUIRE(bodyBinding->runtime_id == 17);
	REQUIRE(bodyBinding->kind == 5);
	REQUIRE(str(bodyBinding->display_name) == "Triangle Agent");
	REQUIRE(bodyBinding->name_langid == 4321);
	REQUIRE(bodyBinding->requirefeature == 9);

	asset_entry_t head;
	initEntry(head, ASSET_HEAD, "mod:head_triangle");
	head.ext.head.headnum = 5;
	head.ext.head.requirefeature = 6;
	std::strncpy(head.ext.head.mesh_archive, "mesh.pdmesh",
		sizeof(head.ext.head.mesh_archive) - 1);
	std::strncpy(head.ext.head.rig_class, "human_male_neck_standard",
		sizeof(head.ext.head.rig_class) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&head,
		"mods/demo/heads/tri_head.pdhead::mesh.pdmesh") == 1);
	const asset_runtime_binding_t *headBinding =
		assetRuntimeFind("mod:head_triangle");
	REQUIRE(headBinding != nullptr);
	REQUIRE(headBinding->type == ASSET_HEAD);
	REQUIRE(str(headBinding->authored_file) == "mesh.pdmesh");
	REQUIRE(str(headBinding->target_id) == "human_male_neck_standard");
	REQUIRE(headBinding->runtime_id == 5);
	REQUIRE(headBinding->requirefeature == 6);

	asset_entry_t skin;
	initEntry(skin, ASSET_SKIN, "mod:skin_blue");
	std::strncpy(skin.ext.skin.target_id, "base:character_joanna",
		sizeof(skin.ext.skin.target_id) - 1);
	std::strncpy(skin.ext.skin.skin_file, "skin.json",
		sizeof(skin.ext.skin.skin_file) - 1);
	std::strncpy(skin.ext.skin.texture_file, "texture.tga",
		sizeof(skin.ext.skin.texture_file) - 1);
	std::strncpy(skin.ext.skin.swatches_file, "swatches.json",
		sizeof(skin.ext.skin.swatches_file) - 1);
	std::strncpy(skin.ext.skin.material_archive,
		"dependencies/assets/material/agent.pdmaterial",
		sizeof(skin.ext.skin.material_archive) - 1);
	std::strncpy(skin.ext.skin.texture_archive,
		"dependencies/assets/texture/agent.pdtexture",
		sizeof(skin.ext.skin.texture_archive) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&skin,
		"mods/demo/skins/blue.pdskin::texture.tga") == 1);
	const asset_runtime_binding_t *skinBinding = assetRuntimeFind("mod:skin_blue");
	REQUIRE(skinBinding != nullptr);
	REQUIRE(skinBinding->type == ASSET_SKIN);
	REQUIRE(str(skinBinding->primary_path) == "mods/demo/skins/blue.pdskin::texture.tga");
	REQUIRE(str(skinBinding->authored_file) == "skin.json");
	REQUIRE(str(skinBinding->dependency_a) == "texture.tga");
	REQUIRE(str(skinBinding->dependency_b) == "swatches.json");
	REQUIRE(str(skinBinding->dependency_c) ==
		"dependencies/assets/material/agent.pdmaterial");
	REQUIRE(str(skinBinding->dependency_d) ==
		"dependencies/assets/texture/agent.pdtexture");
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
	std::strncpy(effect.ext.effect.timeline_file, "timeline.json",
		sizeof(effect.ext.effect.timeline_file) - 1);
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
	REQUIRE(str(effectBinding->dependency_a) == "timeline.json");
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
	std::strncpy(prop.ext.prop.behavior_graph, "behavior.graph.json",
		sizeof(prop.ext.prop.behavior_graph) - 1);
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
	REQUIRE(str(propBinding->dependency_b) == "behavior.graph.json");
	REQUIRE(str(propBinding->display_name) == "Laptop Decoy");
	REQUIRE(assetRuntimeFindByTypeKind(ASSET_PROP, 87) == propBinding);

	asset_entry_t weapon;
	initEntry(weapon, ASSET_WEAPON, "mod:weapon_training");
	weapon.runtime_index = 7;
	weapon.mp_index = 3;
	weapon.ext.weapon.weapon_id = 3;
	weapon.ext.weapon.dual_wieldable = 1;
	weapon.ext.weapon.requirefeature = 4;
	std::strncpy(weapon.ext.weapon.name, "Training Falcon",
		sizeof(weapon.ext.weapon.name) - 1);
	std::strncpy(weapon.ext.weapon.model_file, "model.gltf",
		sizeof(weapon.ext.weapon.model_file) - 1);
	std::strncpy(weapon.ext.weapon.primary_graph, "behavior/primary.graph.json",
		sizeof(weapon.ext.weapon.primary_graph) - 1);
	std::strncpy(weapon.ext.weapon.secondary_graph, "behavior/secondary.graph.json",
		sizeof(weapon.ext.weapon.secondary_graph) - 1);
	std::strncpy(weapon.ext.weapon.shared_context, "behavior/shared-context.json",
		sizeof(weapon.ext.weapon.shared_context) - 1);
	std::strncpy(weapon.ext.weapon.settings_file, "behavior/settings.json",
		sizeof(weapon.ext.weapon.settings_file) - 1);
	std::strncpy(weapon.ext.weapon.variables_file, "behavior/variables.json",
		sizeof(weapon.ext.weapon.variables_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&weapon,
		"mods/demo/weapons/training.pdweapon::model.gltf") == 1);
	const asset_runtime_binding_t *weaponBinding =
		assetRuntimeFind("mod:weapon_training");
	REQUIRE(weaponBinding != nullptr);
	REQUIRE(weaponBinding->type == ASSET_WEAPON);
	REQUIRE(str(weaponBinding->authored_file) == "behavior/primary.graph.json");
	REQUIRE(str(weaponBinding->dependency_a) == "behavior/secondary.graph.json");
	REQUIRE(str(weaponBinding->dependency_b) == "behavior/shared-context.json");
	REQUIRE(str(weaponBinding->dependency_c) == "behavior/settings.json");
	REQUIRE(str(weaponBinding->dependency_d) == "behavior/variables.json");
	REQUIRE(str(weaponBinding->dependency_e) == "model.gltf");
	REQUIRE(str(weaponBinding->weapon_model_file) == "model.gltf");
	REQUIRE(weaponBinding->runtime_id == 7);
	REQUIRE(weaponBinding->kind == 3);
	REQUIRE(weaponBinding->target_kind == 4);
	REQUIRE(weaponBinding->value0 == Approx(1.0f));
	REQUIRE(weaponBinding->params[0] == Approx(3.0f));
	REQUIRE(str(weaponBinding->display_name) == "Training Falcon");
	REQUIRE(weaponBinding->requirefeature == 4);
	REQUIRE(assetRuntimeFindByTypeKind(ASSET_WEAPON, 3) == weaponBinding);

	asset_entry_t projectile;
	initEntry(projectile, ASSET_PROJECTILE, "mod:projectile_rocket");
	std::strncpy(projectile.ext.projectile.name, "Training Rocket",
		sizeof(projectile.ext.projectile.name) - 1);
	std::strncpy(projectile.ext.projectile.model_file, "model.gltf",
		sizeof(projectile.ext.projectile.model_file) - 1);
	std::strncpy(projectile.ext.projectile.behavior_graph,
		"behavior.graph.json",
		sizeof(projectile.ext.projectile.behavior_graph) - 1);
	std::strncpy(projectile.ext.projectile.entity_ref,
		"mod:entity_explosion",
		sizeof(projectile.ext.projectile.entity_ref) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&projectile,
		"mods/demo/projectiles/rocket.pdprojectile::behavior.graph.json") == 1);
	const asset_runtime_binding_t *projectileBinding =
		assetRuntimeFind("mod:projectile_rocket");
	REQUIRE(projectileBinding != nullptr);
	REQUIRE(projectileBinding->type == ASSET_PROJECTILE);
	REQUIRE(str(projectileBinding->authored_file) == "behavior.graph.json");
	REQUIRE(str(projectileBinding->dependency_a) == "model.gltf");
	REQUIRE(str(projectileBinding->target_id) == "mod:entity_explosion");
	REQUIRE(str(projectileBinding->display_name) == "Training Rocket");

	asset_entry_t entity;
	initEntry(entity, ASSET_ENTITY, "mod:entity_explosion");
	std::strncpy(entity.ext.entity.name, "Timed Training Charge",
		sizeof(entity.ext.entity.name) - 1);
	std::strncpy(entity.ext.entity.archetype, "armed_timed_charge",
		sizeof(entity.ext.entity.archetype) - 1);
	std::strncpy(entity.ext.entity.model_file, "model.gltf",
		sizeof(entity.ext.entity.model_file) - 1);
	std::strncpy(entity.ext.entity.behavior_graph, "behavior.graph.json",
		sizeof(entity.ext.entity.behavior_graph) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&entity,
		"mods/demo/entities/timed_charge.pdentity::behavior.graph.json") == 1);
	const asset_runtime_binding_t *entityBinding =
		assetRuntimeFind("mod:entity_explosion");
	REQUIRE(entityBinding != nullptr);
	REQUIRE(entityBinding->type == ASSET_ENTITY);
	REQUIRE(str(entityBinding->authored_file) == "behavior.graph.json");
	REQUIRE(str(entityBinding->dependency_a) == "model.gltf");
	REQUIRE(str(entityBinding->target_id) == "armed_timed_charge");
	REQUIRE(str(entityBinding->display_name) == "Timed Training Charge");

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
	REQUIRE(str(missionBinding->dependency_b) == "objectives.json");
	REQUIRE(str(missionBinding->dependency_c) == "briefing.json");

	asset_entry_t hud;
	initEntry(hud, ASSET_HUD, "mod:hud_classic");
	hud.ext.hud.hud_id = 9;
	hud.ext.hud.element_type = HUD_ELEM_AMMO;
	std::strncpy(hud.ext.hud.texture_file, "texture.png",
		sizeof(hud.ext.hud.texture_file) - 1);
	std::strncpy(hud.ext.hud.layout_file, "layout.json",
		sizeof(hud.ext.hud.layout_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&hud,
		"mods/demo/hud/classic.pdhud::layout.json") == 1);
	const asset_runtime_binding_t *hudBinding = assetRuntimeFind("mod:hud_classic");
	REQUIRE(hudBinding != nullptr);
	REQUIRE(hudBinding->runtime_id == 9);
	REQUIRE(hudBinding->kind == HUD_ELEM_AMMO);
	REQUIRE(str(hudBinding->authored_file) == "layout.json");
	REQUIRE(str(hudBinding->dependency_a) == "texture.png");
	REQUIRE(assetRuntimeFindByTypeKind(ASSET_HUD, HUD_ELEM_AMMO) ==
		hudBinding);

	asset_entry_t ui;
	initEntry(ui, ASSET_UI, "mod:ui_chrome");
	std::strncpy(ui.ext.ui.texture_file, "texture.tga",
		sizeof(ui.ext.ui.texture_file) - 1);
	std::strncpy(ui.ext.ui.layout_file, "layout.json",
		sizeof(ui.ext.ui.layout_file) - 1);
	std::strncpy(ui.ext.ui.nineslice_file, "nineslice.ini",
		sizeof(ui.ext.ui.nineslice_file) - 1);
	std::strncpy(ui.ext.ui.texture_name, "chrome",
		sizeof(ui.ext.ui.texture_name) - 1);
	ui.ext.ui.width = 64;
	ui.ext.ui.height = 64;
	ui.ext.ui.data_size = 4096;
	ui.ext.ui.nineslice_left = 8;
	ui.ext.ui.nineslice_right = 8;
	ui.ext.ui.nineslice_top = 6;
	ui.ext.ui.nineslice_bottom = 6;
	std::strncpy(ui.ext.ui.nineslice_edge_mode, "stretch",
		sizeof(ui.ext.ui.nineslice_edge_mode) - 1);
	std::strncpy(ui.ext.ui.nineslice_center_mode, "tile",
		sizeof(ui.ext.ui.nineslice_center_mode) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&ui,
		"mods/demo/ui/chrome.pdui::texture.tga") == 1);
	const asset_runtime_binding_t *uiBinding = assetRuntimeFind("mod:ui_chrome");
	REQUIRE(uiBinding != nullptr);
	REQUIRE(str(uiBinding->authored_file) == "texture.tga");
	REQUIRE(str(uiBinding->dependency_a) == "layout.json");
	REQUIRE(str(uiBinding->dependency_b) == "nineslice.ini");
	REQUIRE(str(uiBinding->target_id) == "chrome");
	REQUIRE(uiBinding->ui_width == 64);
	REQUIRE(uiBinding->ui_height == 64);
	REQUIRE(uiBinding->ui_data_size == 4096);
	REQUIRE(uiBinding->ui_nineslice_left == 8);
	REQUIRE(uiBinding->ui_nineslice_right == 8);
	REQUIRE(uiBinding->ui_nineslice_top == 6);
	REQUIRE(uiBinding->ui_nineslice_bottom == 6);
	REQUIRE(str(uiBinding->ui_nineslice_edge_mode) == "stretch");
	REQUIRE(str(uiBinding->ui_nineslice_center_mode) == "tile");

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
	std::strncpy(theme.ext.theme.audio_archive,
		"dependencies/assets/audio/menu.pdsfx",
		sizeof(theme.ext.theme.audio_archive) - 1);
	std::strncpy(theme.ext.theme.music_archive,
		"dependencies/assets/music/menu.pdsong",
		sizeof(theme.ext.theme.music_archive) - 1);
	std::strncpy(theme.ext.theme.effect_archive,
		"dependencies/assets/effects/glow.pdeffect",
		sizeof(theme.ext.theme.effect_archive) - 1);
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
	REQUIRE(str(themeBinding->dependency_c) ==
		"dependencies/assets/audio/menu.pdsfx");
	REQUIRE(str(themeBinding->dependency_d) ==
		"dependencies/assets/music/menu.pdsong");
	REQUIRE(str(themeBinding->dependency_e) ==
		"dependencies/assets/effects/glow.pdeffect");

	asset_entry_t font;
	initEntry(font, ASSET_FONT, "mod:font_triangle");
	std::strncpy(font.ext.font.font_file, "glyphs.pgm",
		sizeof(font.ext.font.font_file) - 1);
	std::strncpy(font.ext.font.metrics_file, "font.metrics.json",
		sizeof(font.ext.font.metrics_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&font,
		"mods/demo/fonts/triangle.pdfont::glyphs.pgm") == 1);
	const asset_runtime_binding_t *fontBinding =
		assetRuntimeFind("mod:font_triangle");
	REQUIRE(fontBinding != nullptr);
	REQUIRE(fontBinding->type == ASSET_FONT);
	REQUIRE(str(fontBinding->authored_file) == "glyphs.pgm");
	REQUIRE(str(fontBinding->dependency_a) == "font.metrics.json");

	asset_entry_t vectorFont;
	initEntry(vectorFont, ASSET_FONT, "mod:font_vector");
	std::strncpy(vectorFont.ext.font.font_file, "font.otf",
		sizeof(vectorFont.ext.font.font_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&vectorFont,
		"mods/demo/fonts/vector.pdfont::font.otf") == 1);
	const asset_runtime_binding_t *vectorFontBinding =
		assetRuntimeFind("mod:font_vector");
	REQUIRE(vectorFontBinding != nullptr);
	REQUIRE(str(vectorFontBinding->authored_file) == "font.otf");
	REQUIRE(str(vectorFontBinding->dependency_a) == "");

	asset_entry_t lang;
	initEntry(lang, ASSET_LANG, "mod:lang_training");
	lang.ext.lang.bank_id = 12;
	std::strncpy(lang.ext.lang.locale, "en-US",
		sizeof(lang.ext.lang.locale) - 1);
	std::strncpy(lang.ext.lang.lang_category, "mp_ui",
		sizeof(lang.ext.lang.lang_category) - 1);
	lang.ext.lang.string_count = 42;
	std::strncpy(lang.ext.lang.strings_file, "strings.json",
		sizeof(lang.ext.lang.strings_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&lang,
		"mods/demo/lang/training.pdlang::strings.json") == 1);
	const asset_runtime_binding_t *langBinding =
		assetRuntimeFind("mod:lang_training");
	REQUIRE(langBinding != nullptr);
	REQUIRE(langBinding->type == ASSET_LANG);
	REQUIRE(str(langBinding->authored_file) == "strings.json");
	REQUIRE(str(langBinding->lang_locale) == "en-US");
	REQUIRE(str(langBinding->lang_category) == "mp_ui");
	REQUIRE(langBinding->runtime_id == 12);
	REQUIRE(langBinding->lang_string_count == 42);

	asset_entry_t scenario;
	initEntry(scenario, ASSET_SCENARIO, "mod:scenario_complex");
	scenario.ext.scenario.stagenum = 0x5e;
	scenario.ext.scenario.mode = 3;
	std::strncpy(scenario.ext.scenario.scene_file, "scene.glb",
		sizeof(scenario.ext.scenario.scene_file) - 1);
	std::strncpy(scenario.ext.scenario.collision_file, "collision.obj",
		sizeof(scenario.ext.scenario.collision_file) - 1);
	std::strncpy(scenario.ext.scenario.rooms_file, "rooms.json",
		sizeof(scenario.ext.scenario.rooms_file) - 1);
	std::strncpy(scenario.ext.scenario.portals_file, "portals.json",
		sizeof(scenario.ext.scenario.portals_file) - 1);
	std::strncpy(scenario.ext.scenario.pads_file, "pads.json",
		sizeof(scenario.ext.scenario.pads_file) - 1);
	std::strncpy(scenario.ext.scenario.spawns_file, "spawns.json",
		sizeof(scenario.ext.scenario.spawns_file) - 1);
	std::strncpy(scenario.ext.scenario.volumes_file, "volumes.json",
		sizeof(scenario.ext.scenario.volumes_file) - 1);
	std::strncpy(scenario.ext.scenario.objects_file, "objects.json",
		sizeof(scenario.ext.scenario.objects_file) - 1);
	std::strncpy(scenario.ext.scenario.setup_fields_file,
		"setup.fields.json", sizeof(scenario.ext.scenario.setup_fields_file) - 1);
	std::strncpy(scenario.ext.scenario.ai_lists_file, "ai/ailists.json",
		sizeof(scenario.ext.scenario.ai_lists_file) - 1);
	std::strncpy(scenario.ext.scenario.objectives_file, "objectives.json",
		sizeof(scenario.ext.scenario.objectives_file) - 1);
	std::strncpy(scenario.ext.scenario.navigation_file, "navigation.ini",
		sizeof(scenario.ext.scenario.navigation_file) - 1);
	std::strncpy(scenario.ext.scenario.navigation_waypoints_file,
		"navigation/waypoints.json",
		sizeof(scenario.ext.scenario.navigation_waypoints_file) - 1);
	std::strncpy(scenario.ext.scenario.navigation_waygroups_file,
		"navigation/waygroups.json",
		sizeof(scenario.ext.scenario.navigation_waygroups_file) - 1);
	std::strncpy(scenario.ext.scenario.navigation_covers_file,
		"navigation/covers.json",
		sizeof(scenario.ext.scenario.navigation_covers_file) - 1);
	std::strncpy(scenario.ext.scenario.navigation_paths_file,
		"navigation/paths.json",
		sizeof(scenario.ext.scenario.navigation_paths_file) - 1);
	std::strncpy(scenario.ext.scenario.level_graph_file, "level.graph.json",
		sizeof(scenario.ext.scenario.level_graph_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&scenario,
		"mods/demo/scenarios/complex.pdscenario::scene.glb") == 1);
	const asset_runtime_binding_t *scenarioBinding =
		assetRuntimeFind("mod:scenario_complex");
	REQUIRE(scenarioBinding != nullptr);
	REQUIRE(str(scenarioBinding->authored_file) == "scene.glb");
	REQUIRE(str(scenarioBinding->dependency_a) == "collision.obj");
	REQUIRE(str(scenarioBinding->dependency_b) == "level.graph.json");
	REQUIRE(str(scenarioBinding->scenario_rooms_file) == "rooms.json");
	REQUIRE(str(scenarioBinding->scenario_portals_file) == "portals.json");
	REQUIRE(str(scenarioBinding->scenario_pads_file) == "pads.json");
	REQUIRE(str(scenarioBinding->scenario_spawns_file) == "spawns.json");
	REQUIRE(str(scenarioBinding->scenario_volumes_file) == "volumes.json");
	REQUIRE(str(scenarioBinding->scenario_objects_file) == "objects.json");
	REQUIRE(str(scenarioBinding->scenario_setup_fields_file) ==
		"setup.fields.json");
	REQUIRE(str(scenarioBinding->scenario_ai_lists_file) == "ai/ailists.json");
	REQUIRE(str(scenarioBinding->scenario_objectives_file) == "objectives.json");
	REQUIRE(str(scenarioBinding->scenario_navigation_file) == "navigation.ini");
	REQUIRE(str(scenarioBinding->scenario_navigation_waypoints_file) ==
		"navigation/waypoints.json");
	REQUIRE(str(scenarioBinding->scenario_navigation_waygroups_file) ==
		"navigation/waygroups.json");
	REQUIRE(str(scenarioBinding->scenario_navigation_covers_file) ==
		"navigation/covers.json");
	REQUIRE(str(scenarioBinding->scenario_navigation_paths_file) ==
		"navigation/paths.json");
	REQUIRE(scenarioBinding->runtime_id == 0x5e);
	REQUIRE(scenarioBinding->kind == 3);

	REQUIRE(assetRuntimeCount(ASSET_SKIN) == 1);
	REQUIRE(assetRuntimeCount(ASSET_LANG) == 1);
	REQUIRE(assetRuntimeCount(ASSET_WEAPON) == 1);
	REQUIRE(assetRuntimeCount(ASSET_PROJECTILE) == 1);
	REQUIRE(assetRuntimeCount(ASSET_ENTITY) == 1);
	REQUIRE(assetRuntimeCount(ASSET_NONE) == 20);
}

TEST_CASE("asset runtime bindings grow past the old fixed table size",
          "[modding][pdxxx][runtime][c3838][adapters]") {
	assetRuntimeReset();

	for (int i = 0; i < 320; i++) {
		asset_entry_t mode;
		char id[CATALOG_ID_LEN];
		std::snprintf(id, sizeof(id), "mod:gamemode_scale_%03d", i);
		initEntry(mode, ASSET_GAMEMODE, id);
		mode.ext.gamemode.mode_id = i;
		mode.ext.gamemode.max_players = 8;
		std::strncpy(mode.ext.gamemode.rules_file, "rules.json",
			sizeof(mode.ext.gamemode.rules_file) - 1);
		REQUIRE(assetRuntimeActivateCatalogEntry(&mode,
			"mods/demo/gamemodes/scale.pdgamemode::rules.json") == 1);
	}

	REQUIRE(assetRuntimeCount(ASSET_GAMEMODE) == 320);
	REQUIRE(assetRuntimeCount(ASSET_NONE) == 320);
	REQUIRE(assetRuntimeFind("mod:gamemode_scale_000") != nullptr);
	REQUIRE(assetRuntimeFind("mod:gamemode_scale_319") != nullptr);

	assetRuntimeReset();
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
	std::strncpy(hud.ext.hud.layout_file, "layout.json",
		sizeof(hud.ext.hud.layout_file) - 1);

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

	asset_entry_t textureOnlyHud;
	initEntry(textureOnlyHud, ASSET_HUD, "mod:hud_texture_only");
	std::strncpy(textureOnlyHud.ext.hud.texture_file, "radar.png",
		sizeof(textureOnlyHud.ext.hud.texture_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&textureOnlyHud, path.c_str()) == 0);
	REQUIRE(assetRuntimeFind("mod:hud_texture_only") == nullptr);

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

	asset_entry_t dependencyOnlyMaterial;
	initEntry(dependencyOnlyMaterial, ASSET_MATERIAL, "mod:material_dependency_only");
	std::strncpy(dependencyOnlyMaterial.ext.material.texture_archive,
		"dependencies/assets/texture/steel.pdtexture",
		sizeof(dependencyOnlyMaterial.ext.material.texture_archive) - 1);
	std::strncpy(dependencyOnlyMaterial.ext.material.effect_archive,
		"dependencies/assets/effect/spark.pdeffect",
		sizeof(dependencyOnlyMaterial.ext.material.effect_archive) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&dependencyOnlyMaterial, "") == 0);
	REQUIRE(assetRuntimeFind("mod:material_dependency_only") == nullptr);
	std::strncpy(dependencyOnlyMaterial.ext.material.material_file,
		"material.json", sizeof(dependencyOnlyMaterial.ext.material.material_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&dependencyOnlyMaterial,
		"mods/demo/materials/steel.pdmaterial::material.json") == 1);
	const asset_runtime_binding_t *materialBinding =
		assetRuntimeFind("mod:material_dependency_only");
	REQUIRE(materialBinding != nullptr);
	REQUIRE(str(materialBinding->authored_file) == "material.json");
	REQUIRE(str(materialBinding->dependency_a) ==
		"dependencies/assets/texture/steel.pdtexture");
	REQUIRE(str(materialBinding->dependency_b) ==
		"dependencies/assets/effect/spark.pdeffect");
	assetRuntimeReleaseCatalogEntry("mod:material_dependency_only");
	REQUIRE(assetRuntimeFind("mod:material_dependency_only") == nullptr);

	asset_entry_t layoutOnlyUi;
	initEntry(layoutOnlyUi, ASSET_UI, "mod:ui_layout_only");
	std::strncpy(layoutOnlyUi.ext.ui.layout_file, "layout.json",
		sizeof(layoutOnlyUi.ext.ui.layout_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&layoutOnlyUi,
		"mods/demo/ui/chrome.pdui::layout.json") == 0);
	REQUIRE(assetRuntimeFind("mod:ui_layout_only") == nullptr);

	asset_entry_t ninesliceOnlyUi;
	initEntry(ninesliceOnlyUi, ASSET_UI, "mod:ui_nineslice_only");
	std::strncpy(ninesliceOnlyUi.ext.ui.nineslice_file, "nineslice.ini",
		sizeof(ninesliceOnlyUi.ext.ui.nineslice_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&ninesliceOnlyUi,
		"mods/demo/ui/chrome.pdui::nineslice.ini") == 0);
	REQUIRE(assetRuntimeFind("mod:ui_nineslice_only") == nullptr);

	asset_entry_t primaryOnlyUi;
	initEntry(primaryOnlyUi, ASSET_UI, "mod:ui_primary_only");
	REQUIRE(assetRuntimeActivateCatalogEntry(&primaryOnlyUi,
		"mods/demo/ui/chrome.pdui::texture.tga") == 0);
	REQUIRE(assetRuntimeFind("mod:ui_primary_only") == nullptr);

	asset_entry_t primaryOnlyFont;
	initEntry(primaryOnlyFont, ASSET_FONT, "mod:font_primary_only");
	REQUIRE(assetRuntimeActivateCatalogEntry(&primaryOnlyFont,
		"mods/demo/fonts/body.pdfont::glyphs.pgm") == 0);
	REQUIRE(assetRuntimeFind("mod:font_primary_only") == nullptr);

	asset_entry_t metricsOnlyFont;
	initEntry(metricsOnlyFont, ASSET_FONT, "mod:font_metrics_only");
	std::strncpy(metricsOnlyFont.ext.font.metrics_file, "font.metrics.json",
		sizeof(metricsOnlyFont.ext.font.metrics_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&metricsOnlyFont,
		"mods/demo/fonts/body.pdfont::font.metrics.json") == 0);
	REQUIRE(assetRuntimeFind("mod:font_metrics_only") == nullptr);

	asset_entry_t bitmapNoMetricsFont;
	initEntry(bitmapNoMetricsFont, ASSET_FONT, "mod:font_bitmap_no_metrics");
	std::strncpy(bitmapNoMetricsFont.ext.font.font_file, "glyphs.pgm",
		sizeof(bitmapNoMetricsFont.ext.font.font_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&bitmapNoMetricsFont,
		"mods/demo/fonts/body.pdfont::glyphs.pgm") == 0);
	REQUIRE(assetRuntimeFind("mod:font_bitmap_no_metrics") == nullptr);

	asset_entry_t primaryOnlyLang;
	initEntry(primaryOnlyLang, ASSET_LANG, "mod:lang_primary_only");
	REQUIRE(assetRuntimeActivateCatalogEntry(&primaryOnlyLang,
		"mods/demo/lang/en.pdlang::strings.json") == 0);
	REQUIRE(assetRuntimeFind("mod:lang_primary_only") == nullptr);

	asset_entry_t banklessLang;
	initEntry(banklessLang, ASSET_LANG, "mod:lang_bankless");
	banklessLang.ext.lang.bank_id = -1;
	std::strncpy(banklessLang.ext.lang.strings_file, "strings.json",
		sizeof(banklessLang.ext.lang.strings_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&banklessLang,
		"mods/demo/lang/en.pdlang::strings.json") == 0);
	REQUIRE(assetRuntimeFind("mod:lang_bankless") == nullptr);

	asset_entry_t primaryOnlyWeapon;
	initEntry(primaryOnlyWeapon, ASSET_WEAPON, "mod:weapon_primary_only");
	REQUIRE(assetRuntimeActivateCatalogEntry(&primaryOnlyWeapon,
		"mods/demo/weapons/training.pdweapon::behavior/primary.graph.json") == 0);
	REQUIRE(assetRuntimeFind("mod:weapon_primary_only") == nullptr);

	asset_entry_t modelOnlyWeapon;
	initEntry(modelOnlyWeapon, ASSET_WEAPON, "mod:weapon_model_only");
	std::strncpy(modelOnlyWeapon.ext.weapon.model_file, "model.gltf",
		sizeof(modelOnlyWeapon.ext.weapon.model_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&modelOnlyWeapon,
		"mods/demo/weapons/training.pdweapon::model.gltf") == 0);
	REQUIRE(assetRuntimeFind("mod:weapon_model_only") == nullptr);

	asset_entry_t missingVariablesWeapon;
	initEntry(missingVariablesWeapon, ASSET_WEAPON, "mod:weapon_missing_variables");
	std::strncpy(missingVariablesWeapon.ext.weapon.primary_graph,
		"behavior/primary.graph.json",
		sizeof(missingVariablesWeapon.ext.weapon.primary_graph) - 1);
	std::strncpy(missingVariablesWeapon.ext.weapon.secondary_graph,
		"behavior/secondary.graph.json",
		sizeof(missingVariablesWeapon.ext.weapon.secondary_graph) - 1);
	std::strncpy(missingVariablesWeapon.ext.weapon.shared_context,
		"behavior/shared-context.json",
		sizeof(missingVariablesWeapon.ext.weapon.shared_context) - 1);
	std::strncpy(missingVariablesWeapon.ext.weapon.settings_file,
		"behavior/settings.json",
		sizeof(missingVariablesWeapon.ext.weapon.settings_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&missingVariablesWeapon,
		"mods/demo/weapons/training.pdweapon::behavior/primary.graph.json") == 0);
	REQUIRE(assetRuntimeFind("mod:weapon_missing_variables") == nullptr);

	asset_entry_t primaryOnlyCharacter;
	initEntry(primaryOnlyCharacter, ASSET_CHARACTER, "mod:character_primary_only");
	REQUIRE(assetRuntimeActivateCatalogEntry(&primaryOnlyCharacter,
		"mods/demo/characters/tri.pdcharacter::dependencies/assets/body/tri_body.pdbody") == 0);
	REQUIRE(assetRuntimeFind("mod:character_primary_only") == nullptr);

	asset_entry_t portraitOnlyCharacter;
	initEntry(portraitOnlyCharacter, ASSET_CHARACTER, "mod:character_portrait_only");
	std::strncpy(portraitOnlyCharacter.ext.character.portrait_file, "portrait.png",
		sizeof(portraitOnlyCharacter.ext.character.portrait_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&portraitOnlyCharacter,
		"mods/demo/characters/tri.pdcharacter::portrait.png") == 0);
	REQUIRE(assetRuntimeFind("mod:character_portrait_only") == nullptr);

	asset_entry_t primaryOnlyBody;
	initEntry(primaryOnlyBody, ASSET_BODY, "mod:body_primary_only");
	REQUIRE(assetRuntimeActivateCatalogEntry(&primaryOnlyBody,
		"mods/demo/bodies/tri_body.pdbody::mesh.pdmesh") == 0);
	REQUIRE(assetRuntimeFind("mod:body_primary_only") == nullptr);

	asset_entry_t handOnlyBody;
	initEntry(handOnlyBody, ASSET_BODY, "mod:body_hand_only");
	std::strncpy(handOnlyBody.ext.body.hand_archive, "hand.pdmesh",
		sizeof(handOnlyBody.ext.body.hand_archive) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&handOnlyBody,
		"mods/demo/bodies/tri_body.pdbody::hand.pdmesh") == 0);
	REQUIRE(assetRuntimeFind("mod:body_hand_only") == nullptr);

	asset_entry_t primaryOnlyHead;
	initEntry(primaryOnlyHead, ASSET_HEAD, "mod:head_primary_only");
	REQUIRE(assetRuntimeActivateCatalogEntry(&primaryOnlyHead,
		"mods/demo/heads/tri_head.pdhead::mesh.pdmesh") == 0);
	REQUIRE(assetRuntimeFind("mod:head_primary_only") == nullptr);

	asset_entry_t dependencyOnlySkin;
	initEntry(dependencyOnlySkin, ASSET_SKIN, "mod:skin_dependency_only");
	std::strncpy(dependencyOnlySkin.ext.skin.material_archive,
		"dependencies/assets/material/agent.pdmaterial",
		sizeof(dependencyOnlySkin.ext.skin.material_archive) - 1);
	std::strncpy(dependencyOnlySkin.ext.skin.texture_archive,
		"dependencies/assets/texture/agent.pdtexture",
		sizeof(dependencyOnlySkin.ext.skin.texture_archive) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&dependencyOnlySkin, "") == 0);
	REQUIRE(assetRuntimeFind("mod:skin_dependency_only") == nullptr);
	REQUIRE(assetRuntimeActivateCatalogEntry(&dependencyOnlySkin,
		"mods/demo/skins/blue.pdskin::dependencies/assets/material/agent.pdmaterial") == 0);
	REQUIRE(assetRuntimeFind("mod:skin_dependency_only") == nullptr);
	std::strncpy(dependencyOnlySkin.ext.skin.skin_file, "skin.json",
		sizeof(dependencyOnlySkin.ext.skin.skin_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&dependencyOnlySkin,
		"mods/demo/skins/blue.pdskin::skin.json") == 1);
	const asset_runtime_binding_t *skinBinding =
		assetRuntimeFind("mod:skin_dependency_only");
	REQUIRE(skinBinding != nullptr);
	REQUIRE(str(skinBinding->authored_file) == "skin.json");
	REQUIRE(str(skinBinding->dependency_c) ==
		"dependencies/assets/material/agent.pdmaterial");
	REQUIRE(str(skinBinding->dependency_d) ==
		"dependencies/assets/texture/agent.pdtexture");
	assetRuntimeReleaseCatalogEntry("mod:skin_dependency_only");
	REQUIRE(assetRuntimeFind("mod:skin_dependency_only") == nullptr);

	asset_entry_t shaderOnlyEffect;
	initEntry(shaderOnlyEffect, ASSET_EFFECT, "mod:effect_shader_only");
	std::strncpy(shaderOnlyEffect.ext.effect.shader_id, "weapon_glow",
		sizeof(shaderOnlyEffect.ext.effect.shader_id) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&shaderOnlyEffect, "") == 0);
	REQUIRE(assetRuntimeFind("mod:effect_shader_only") == nullptr);
	REQUIRE(assetRuntimeActivateCatalogEntry(&shaderOnlyEffect,
		"mods/demo/effects/glow.pdeffect") == 0);
	REQUIRE(assetRuntimeFind("mod:effect_shader_only") == nullptr);
	std::strncpy(shaderOnlyEffect.ext.effect.effect_file, "effect.graph.json",
		sizeof(shaderOnlyEffect.ext.effect.effect_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&shaderOnlyEffect,
		"mods/demo/effects/glow.pdeffect::effect.graph.json") == 1);
	const asset_runtime_binding_t *effectBinding =
		assetRuntimeFind("mod:effect_shader_only");
	REQUIRE(effectBinding != nullptr);
	REQUIRE(str(effectBinding->authored_file) == "effect.graph.json");
	REQUIRE(str(effectBinding->shader_id) == "weapon_glow");
	assetRuntimeReleaseCatalogEntry("mod:effect_shader_only");
	REQUIRE(assetRuntimeFind("mod:effect_shader_only") == nullptr);

	asset_entry_t modelOnlyVehicle;
	initEntry(modelOnlyVehicle, ASSET_VEHICLE, "mod:vehicle_model_only");
	std::strncpy(modelOnlyVehicle.ext.vehicle.model_file, "model.gltf",
		sizeof(modelOnlyVehicle.ext.vehicle.model_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&modelOnlyVehicle,
		"mods/demo/vehicles/bike.pdvehicle::model.gltf") == 0);
	REQUIRE(assetRuntimeFind("mod:vehicle_model_only") == nullptr);

	asset_entry_t primaryOnlyArena;
	initEntry(primaryOnlyArena, ASSET_ARENA, "mod:arena_primary_only");
	REQUIRE(assetRuntimeActivateCatalogEntry(&primaryOnlyArena,
		"mods/demo/arenas/training.pdarena::geometry.obj") == 0);
	REQUIRE(assetRuntimeFind("mod:arena_primary_only") == nullptr);

	asset_entry_t scenarioOnlyArena;
	initEntry(scenarioOnlyArena, ASSET_ARENA, "mod:arena_scenario_only");
	std::strncpy(scenarioOnlyArena.ext.arena.scenario_archive,
		"dependencies/assets/scenarios/training.pdscenario",
		sizeof(scenarioOnlyArena.ext.arena.scenario_archive) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&scenarioOnlyArena,
		"mods/demo/arenas/training.pdarena::dependencies/assets/scenarios/training.pdscenario") == 1);
	const asset_runtime_binding_t *scenarioArenaBinding =
		assetRuntimeFind("mod:arena_scenario_only");
	REQUIRE(scenarioArenaBinding != nullptr);
	REQUIRE(str(scenarioArenaBinding->authored_file) ==
		"dependencies/assets/scenarios/training.pdscenario");
	assetRuntimeReleaseCatalogEntry("mod:arena_scenario_only");
	REQUIRE(assetRuntimeFind("mod:arena_scenario_only") == nullptr);

	asset_entry_t physicsOnlyVehicle;
	initEntry(physicsOnlyVehicle, ASSET_VEHICLE, "mod:vehicle_physics_only");
	std::strncpy(physicsOnlyVehicle.ext.vehicle.physics_file, "physics.json",
		sizeof(physicsOnlyVehicle.ext.vehicle.physics_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&physicsOnlyVehicle,
		"mods/demo/vehicles/bike.pdvehicle::physics.json") == 0);
	REQUIRE(assetRuntimeFind("mod:vehicle_physics_only") == nullptr);
	std::strncpy(physicsOnlyVehicle.ext.vehicle.behavior_graph,
		"behavior.graph.json",
		sizeof(physicsOnlyVehicle.ext.vehicle.behavior_graph) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&physicsOnlyVehicle,
		"mods/demo/vehicles/bike.pdvehicle::behavior.graph.json") == 1);
	const asset_runtime_binding_t *vehicleBinding =
		assetRuntimeFind("mod:vehicle_physics_only");
	REQUIRE(vehicleBinding != nullptr);
	REQUIRE(str(vehicleBinding->authored_file) == "");
	REQUIRE(str(vehicleBinding->dependency_a) == "physics.json");
	REQUIRE(str(vehicleBinding->dependency_b) == "behavior.graph.json");
	assetRuntimeReleaseCatalogEntry("mod:vehicle_physics_only");
	REQUIRE(assetRuntimeFind("mod:vehicle_physics_only") == nullptr);

	asset_entry_t primaryOnlyProp;
	initEntry(primaryOnlyProp, ASSET_PROP, "mod:prop_primary_only");
	REQUIRE(assetRuntimeActivateCatalogEntry(&primaryOnlyProp,
		"mods/demo/props/laptop_decoy.pdprop::model.gltf") == 0);
	REQUIRE(assetRuntimeFind("mod:prop_primary_only") == nullptr);

	asset_entry_t behaviorOnlyProp;
	initEntry(behaviorOnlyProp, ASSET_PROP, "mod:prop_behavior_only");
	std::strncpy(behaviorOnlyProp.ext.prop.behavior_graph,
		"behavior.graph.json",
		sizeof(behaviorOnlyProp.ext.prop.behavior_graph) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&behaviorOnlyProp,
		"mods/demo/props/laptop_decoy.pdprop::behavior.graph.json") == 0);
	REQUIRE(assetRuntimeFind("mod:prop_behavior_only") == nullptr);

	asset_entry_t primaryOnlyProjectile;
	initEntry(primaryOnlyProjectile, ASSET_PROJECTILE, "mod:projectile_primary_only");
	REQUIRE(assetRuntimeActivateCatalogEntry(&primaryOnlyProjectile,
		"mods/demo/projectiles/rocket.pdprojectile::behavior.graph.json") == 0);
	REQUIRE(assetRuntimeFind("mod:projectile_primary_only") == nullptr);

	asset_entry_t modelOnlyProjectile;
	initEntry(modelOnlyProjectile, ASSET_PROJECTILE, "mod:projectile_model_only");
	std::strncpy(modelOnlyProjectile.ext.projectile.model_file, "model.gltf",
		sizeof(modelOnlyProjectile.ext.projectile.model_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&modelOnlyProjectile,
		"mods/demo/projectiles/rocket.pdprojectile::model.gltf") == 0);
	REQUIRE(assetRuntimeFind("mod:projectile_model_only") == nullptr);
	std::strncpy(modelOnlyProjectile.ext.projectile.behavior_graph,
		"behavior.graph.json",
		sizeof(modelOnlyProjectile.ext.projectile.behavior_graph) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&modelOnlyProjectile,
		"mods/demo/projectiles/rocket.pdprojectile::behavior.graph.json") == 1);
	const asset_runtime_binding_t *projectileBinding =
		assetRuntimeFind("mod:projectile_model_only");
	REQUIRE(projectileBinding != nullptr);
	REQUIRE(str(projectileBinding->authored_file) == "behavior.graph.json");
	REQUIRE(str(projectileBinding->dependency_a) == "model.gltf");
	assetRuntimeReleaseCatalogEntry("mod:projectile_model_only");
	REQUIRE(assetRuntimeFind("mod:projectile_model_only") == nullptr);

	asset_entry_t primaryOnlyEntity;
	initEntry(primaryOnlyEntity, ASSET_ENTITY, "mod:entity_primary_only");
	REQUIRE(assetRuntimeActivateCatalogEntry(&primaryOnlyEntity,
		"mods/demo/entities/mine.pdentity::behavior.graph.json") == 0);
	REQUIRE(assetRuntimeFind("mod:entity_primary_only") == nullptr);

	asset_entry_t modelOnlyEntity;
	initEntry(modelOnlyEntity, ASSET_ENTITY, "mod:entity_model_only");
	std::strncpy(modelOnlyEntity.ext.entity.model_file, "model.gltf",
		sizeof(modelOnlyEntity.ext.entity.model_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&modelOnlyEntity,
		"mods/demo/entities/mine.pdentity::model.gltf") == 0);
	REQUIRE(assetRuntimeFind("mod:entity_model_only") == nullptr);
	std::strncpy(modelOnlyEntity.ext.entity.behavior_graph,
		"behavior.graph.json",
		sizeof(modelOnlyEntity.ext.entity.behavior_graph) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&modelOnlyEntity,
		"mods/demo/entities/mine.pdentity::behavior.graph.json") == 1);
	const asset_runtime_binding_t *entityBinding =
		assetRuntimeFind("mod:entity_model_only");
	REQUIRE(entityBinding != nullptr);
	REQUIRE(str(entityBinding->authored_file) == "behavior.graph.json");
	REQUIRE(str(entityBinding->dependency_a) == "model.gltf");
	assetRuntimeReleaseCatalogEntry("mod:entity_model_only");
	REQUIRE(assetRuntimeFind("mod:entity_model_only") == nullptr);

	asset_entry_t primaryOnlyMission;
	initEntry(primaryOnlyMission, ASSET_MISSION, "mod:mission_primary_only");
	REQUIRE(assetRuntimeActivateCatalogEntry(&primaryOnlyMission,
		"mods/demo/missions/rescue.pdmission::mission.graph.json") == 0);
	REQUIRE(assetRuntimeFind("mod:mission_primary_only") == nullptr);

	asset_entry_t graphOnlyMission;
	initEntry(graphOnlyMission, ASSET_MISSION, "mod:mission_graph_only");
	std::strncpy(graphOnlyMission.ext.mission.mission_graph_file,
		"mission.graph.json",
		sizeof(graphOnlyMission.ext.mission.mission_graph_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&graphOnlyMission,
		"mods/demo/missions/rescue.pdmission::mission.graph.json") == 0);
	REQUIRE(assetRuntimeFind("mod:mission_graph_only") == nullptr);

	asset_entry_t missingBriefingMission;
	initEntry(missingBriefingMission, ASSET_MISSION, "mod:mission_missing_briefing");
	std::strncpy(missingBriefingMission.ext.mission.mission_graph_file,
		"mission.graph.json",
		sizeof(missingBriefingMission.ext.mission.mission_graph_file) - 1);
	std::strncpy(missingBriefingMission.ext.mission.scenario_archive,
		"dependencies/assets/scenario/rescue.pdscenario",
		sizeof(missingBriefingMission.ext.mission.scenario_archive) - 1);
	std::strncpy(missingBriefingMission.ext.mission.objectives_file,
		"objectives.json",
		sizeof(missingBriefingMission.ext.mission.objectives_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&missingBriefingMission,
		"mods/demo/missions/rescue.pdmission::mission.graph.json") == 0);
	REQUIRE(assetRuntimeFind("mod:mission_missing_briefing") == nullptr);

	asset_entry_t primaryOnlyScenario;
	initEntry(primaryOnlyScenario, ASSET_SCENARIO, "mod:scenario_primary_only");
	REQUIRE(assetRuntimeActivateCatalogEntry(&primaryOnlyScenario,
		"mods/demo/scenarios/complex.pdscenario::scene.glb") == 0);
	REQUIRE(assetRuntimeFind("mod:scenario_primary_only") == nullptr);

	asset_entry_t sceneOnlyScenario;
	initEntry(sceneOnlyScenario, ASSET_SCENARIO, "mod:scenario_scene_only");
	std::strncpy(sceneOnlyScenario.ext.scenario.scene_file, "scene.glb",
		sizeof(sceneOnlyScenario.ext.scenario.scene_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&sceneOnlyScenario,
		"mods/demo/scenarios/complex.pdscenario::scene.glb") == 0);
	REQUIRE(assetRuntimeFind("mod:scenario_scene_only") == nullptr);

	asset_entry_t missingGraphScenario;
	initEntry(missingGraphScenario, ASSET_SCENARIO, "mod:scenario_missing_graph");
	fillStrictScenario(missingGraphScenario);
	missingGraphScenario.ext.scenario.level_graph_file[0] = '\0';
	REQUIRE(assetRuntimeActivateCatalogEntry(&missingGraphScenario,
		"mods/demo/scenarios/complex.pdscenario::scene.glb") == 0);
	REQUIRE(assetRuntimeFind("mod:scenario_missing_graph") == nullptr);

	asset_entry_t dependencyOnlyTheme;
	initEntry(dependencyOnlyTheme, ASSET_THEME, "mod:theme_dependency_only");
	std::strncpy(dependencyOnlyTheme.ext.theme.ui_archive,
		"dependencies/assets/ui/chrome.pdui",
		sizeof(dependencyOnlyTheme.ext.theme.ui_archive) - 1);
	std::strncpy(dependencyOnlyTheme.ext.theme.font_archive,
		"dependencies/assets/font/body.pdfont",
		sizeof(dependencyOnlyTheme.ext.theme.font_archive) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&dependencyOnlyTheme, "") == 0);
	REQUIRE(assetRuntimeFind("mod:theme_dependency_only") == nullptr);
	std::strncpy(dependencyOnlyTheme.ext.theme.theme_file, "theme.json",
		sizeof(dependencyOnlyTheme.ext.theme.theme_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&dependencyOnlyTheme,
		"mods/demo/themes/dark.pdtheme::theme.json") == 1);
	const asset_runtime_binding_t *themeBinding =
		assetRuntimeFind("mod:theme_dependency_only");
	REQUIRE(themeBinding != nullptr);
	REQUIRE(str(themeBinding->authored_file) == "theme.json");
	REQUIRE(str(themeBinding->dependency_a) ==
		"dependencies/assets/ui/chrome.pdui");
	REQUIRE(str(themeBinding->dependency_b) ==
		"dependencies/assets/font/body.pdfont");
	assetRuntimeReleaseCatalogEntry("mod:theme_dependency_only");
	REQUIRE(assetRuntimeFind("mod:theme_dependency_only") == nullptr);

	asset_entry_t mode;
	initEntry(mode, ASSET_GAMEMODE, "mod:gamemode_training");
	mode.ext.gamemode.mode_id = 44;
	mode.ext.gamemode.min_players = 1;
	mode.ext.gamemode.max_players = 6;
	mode.ext.gamemode.team_based = 0;
	mode.ext.gamemode.requirefeature = 7;
	std::strncpy(mode.ext.gamemode.name, "Training Rules",
		sizeof(mode.ext.gamemode.name) - 1);
	std::strncpy(mode.ext.gamemode.description,
		"Close-quarters training rules",
		sizeof(mode.ext.gamemode.description) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&mode, "") == 0);
	REQUIRE(assetRuntimeFind("mod:gamemode_training") == nullptr);
	REQUIRE(assetRuntimeActivateCatalogEntry(&mode,
		"mods/demo/gamemodes/training.pdgamemode") == 0);
	REQUIRE(assetRuntimeFind("mod:gamemode_training") == nullptr);
	std::strncpy(mode.ext.gamemode.rules_file, "rules.json",
		sizeof(mode.ext.gamemode.rules_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&mode,
		"mods/demo/gamemodes/training.pdgamemode::rules.json") == 1);
	const asset_runtime_binding_t *modeBinding =
		assetRuntimeFind("mod:gamemode_training");
	REQUIRE(modeBinding != nullptr);
	REQUIRE(modeBinding->runtime_id == 44);
	REQUIRE(modeBinding->kind == 0);
	REQUIRE(modeBinding->target_kind == 1);
	REQUIRE(str(modeBinding->gamemode_name) == "Training Rules");
	REQUIRE(str(modeBinding->gamemode_description) ==
		"Close-quarters training rules");
	REQUIRE(modeBinding->gamemode_min_players == 1);
	REQUIRE(modeBinding->gamemode_max_players == 6);
	REQUIRE(modeBinding->gamemode_team_based == 0);
	REQUIRE(modeBinding->gamemode_requirefeature == 7);
	REQUIRE(str(modeBinding->authored_file) == "rules.json");

	assetRuntimeReleaseCatalogEntry("mod:gamemode_training");
	REQUIRE(assetRuntimeFind("mod:gamemode_training") == nullptr);

	asset_entry_t bot;
	initEntry(bot, ASSET_BOT_PROFILE, "mod:bot_jumper");
	bot.ext.bot_profile.type = 2;
	bot.ext.bot_profile.difficulty = 3;
	bot.ext.bot_profile.body = -1;
	bot.ext.bot_profile.name_langid = 123;
	bot.ext.bot_profile.requirefeature = 9;
	REQUIRE(assetRuntimeActivateCatalogEntry(&bot, "") == 0);
	REQUIRE(assetRuntimeFind("mod:bot_jumper") == nullptr);
	REQUIRE(assetRuntimeActivateCatalogEntry(&bot,
		"mods/demo/bots/jumper.pdbotprofile") == 0);
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
	REQUIRE(botBinding->target_kind == -1);
	REQUIRE(botBinding->bot_profile_type == 2);
	REQUIRE(botBinding->bot_profile_difficulty == 3);
	REQUIRE(botBinding->bot_profile_body == -1);
	REQUIRE(botBinding->bot_profile_name_langid == 123);
	REQUIRE(botBinding->bot_profile_requirefeature == 9);
	REQUIRE(str(botBinding->authored_file) == "profile.json");
	REQUIRE(str(botBinding->target_id) == "base:body_training");
}
