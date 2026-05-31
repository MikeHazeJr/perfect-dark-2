#!/usr/bin/env python3
"""Normalize the checked-in typed .pdxxx example archives.

The examples intentionally contain real zip-openable typed archives, not loose
folders. This script rewrites the sample archives whose public layout depends
on other sample archives so the nested dependency copies stay in the same clean
shape as the top-level samples.
"""

from __future__ import annotations

import zipfile
import json
from collections.abc import Iterable
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1] / "examples" / "modding" / "typed-pdxxx-basic"
ZIP_TIME = (1980, 1, 1, 0, 0, 0)


def archive(rel: str) -> Path:
    return ROOT / rel


def read_entry(rel: str, entry: str) -> bytes:
    with zipfile.ZipFile(archive(rel), "r") as zf:
        return zf.read(entry)


def read_archive(rel: str) -> bytes:
    return archive(rel).read_bytes()


def write_archive(rel: str, entries: Iterable[tuple[str, bytes | str]]) -> None:
    path = archive(rel)
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for name, data in entries:
            if isinstance(data, str):
                data = data.encode("utf-8")
            info = zipfile.ZipInfo(name, ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, data)


def manifest(kind: str, catalog_id: str) -> str:
    return (
        "{\n"
        "  \"schema\": \"pd.asset_archive.manifest.v1\",\n"
        f"  \"pd_kind\": \"{kind}\",\n"
        f"  \"catalog_id\": \"{catalog_id}\"\n"
        "}\n"
    )


def update_animation(rel: str, catalog_id: str, name: str, category: str) -> None:
    animation = read_entry(rel, "animation.gltf")
    write_archive(rel, [
        ("animation.ini",
         f"; {Path(rel).name} - self-contained animation asset\n"
         "[animation]\n"
         f"catalog_id = {catalog_id}\n"
         f"name = {name}\n"
         "frame_count = 2\n"
         f"category = {category}\n"
         "\n[source]\n"
         "animation_file = animation.gltf\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("animation.gltf", animation),
        ("_meta/manifest.json", manifest("animation", catalog_id)),
    ])


def update_scenario() -> bytes:
    rel = "scenarios/tri_scenario.pdscenario"
    kept = {
        "scene.glb": read_entry(rel, "scene.glb"),
        "pads.tsv": read_entry(rel, "pads.tsv"),
        "spawns.tsv": read_entry(rel, "spawns.tsv"),
        "volumes.tsv": read_entry(rel, "volumes.tsv"),
        "objects.tsv": read_entry(rel, "objects.tsv"),
        "objectives.tsv": read_entry(rel, "objectives.tsv"),
        "_meta/generated-collision.json": read_entry(rel, "_meta/generated-collision.json"),
        "_meta/generated-navmesh.json": read_entry(rel, "_meta/generated-navmesh.json"),
    }
    navigation_ini = (
        "[navigation]\n"
        "source = scene.glb\n"
        "collision_source = scene.glb\n"
        "generator = deterministic.surface_graph.v1\n"
        "supports_walk = true\n"
        "supports_jump = true\n"
        "supports_wall = true\n"
        "supports_ceiling = true\n"
        "pads_file = pads.tsv\n"
        "spawns_file = spawns.tsv\n"
        "volumes_file = volumes.tsv\n"
        "waypoints_file = navigation/waypoints.tsv\n"
        "waygroups_file = navigation/waygroups.tsv\n"
        "covers_file = navigation/covers.tsv\n"
        "paths_file = navigation/paths.tsv\n"
        "generated_cache = _meta/generated-navmesh.json\n"
    )
    waypoints_tsv = "waypoint_id\tpad_ref\tgroup_ref\tstep\tneighbours\n"
    waygroups_tsv = "waygroup_id\tstep\twaypoints\tneighbours\n"
    covers_tsv = "cover_id\tflags\tpos_x\tpos_y\tpos_z\tlook_x\tlook_y\tlook_z\n"
    paths_tsv = (
        "path_ref\tflags\tpads\n"
        "path_0000\t0x00\tpad_0000\n"
    )
    setup_fields_tsv = (
        "record_id\tkind\tfield\ttype\tvalue\tcatalog_id\tref_record_id\n"
        "setup_0000\tprop\tcommand.order\ts32\t0\t\t\n"
        "setup_0000\tprop\tbase.pad\tpad_ref\tpad_0000\t\t\n"
        "setup_0000\tprop\tbase.flags\tu32_hex\t0x00000000\t\t\n"
    )
    objectives_tsv = (
        "objective_id\tkind\ttext_token\tdifficulty_mask\tgraph_node\toperand_kind\ttarget_ref\ttarget_record_ref\tpad_ref\tstate_ref\tmatch_value\tinitial_status\n"
        "objective_0000\tobjective\tobjective_text_primary\tall\tlevel.objective.0000\tobjective\t\t\t\t\t\t\n"
    )
    ai_lists_tsv = (
        "ailist_ref\tlist_id\tgraph_node\tcommand_index\toffset\topcode\topcode_name\toperands\tmodel_catalog_id\tweapon_catalog_id\tbody_catalog_id\thead_catalog_id\n"
        "ailist_0000\t0x0000\tscenario.ai.ailist_0000.command.0000\t0\t0\t0x0004\tend\t\t\t\t\t\n"
    )
    level_graph_json = (
        "{\n"
        "  \"schema\": \"pd2.level.graph.v1\",\n"
        "  \"scenario\": \"example:tri_scenario\",\n"
        "  \"source\": \"scene.glb\",\n"
        "  \"kind\": \"mp\",\n"
        "  \"tables\": {\n"
        "    \"pads\": \"pads.tsv\",\n"
        "    \"spawns\": \"spawns.tsv\",\n"
        "    \"volumes\": \"volumes.tsv\",\n"
        "    \"objects\": \"objects.tsv\",\n"
        "    \"setup_fields\": \"setup.fields.tsv\",\n"
        "    \"ai_lists\": \"ai/ailists.tsv\",\n"
        "    \"objectives\": \"objectives.tsv\",\n"
        "    \"waypoints\": \"navigation/waypoints.tsv\",\n"
        "    \"waygroups\": \"navigation/waygroups.tsv\",\n"
        "    \"covers\": \"navigation/covers.tsv\",\n"
        "    \"paths\": \"navigation/paths.tsv\"\n"
        "  },\n"
        "  \"nodes\": [\n"
        "    { \"id\": \"scenario.load\", \"kind\": \"event.scenario.load\" },\n"
        "    { \"id\": \"source.scene\", \"kind\": \"scenario.scene.source\", \"file\": \"scene.glb\" },\n"
        "    { \"id\": \"scenario.pads\", \"kind\": \"scenario.pads.source\", \"source\": \"pads.tsv\", \"pads\": 1 },\n"
        "    { \"id\": \"navigation.paths\", \"kind\": \"scenario.navigation.paths.source\", \"source\": \"navigation/paths.tsv\", \"paths\": 1 },\n"
        "    { \"id\": \"scenario.ai.lists\", \"kind\": \"scenario.ai.lists.source\", \"source\": \"ai/ailists.tsv\", \"lists\": 1 },\n"
        "    { \"id\": \"scenario.global.settings\", \"kind\": \"scenario.global.settings.source\", \"scenario\": \"example:tri_scenario\", \"source\": \"scenario.ini\", \"scene\": \"scene.glb\", \"collision\": \"scene.glb\", \"navigation\": \"navigation.ini\", \"pads\": 1, \"volumes\": 1 },\n"
        "    { \"id\": \"scenario.ai.action.jog_to_pad\", \"kind\": \"scenario.ai.action.jog_to_pad\", \"source\": \"ai/ailists.tsv\", \"pads\": \"pads.tsv\", \"opcode\": \"0x001d\", \"speed\": \"jog\" },\n"
        "    { \"id\": \"scenario.ai.action.go_to_pad_preset\", \"kind\": \"scenario.ai.action.go_to_pad_preset\", \"source\": \"ai/ailists.tsv\", \"pads\": \"pads.tsv\", \"opcode\": \"0x001e\", \"pad\": \"chr.padpreset1\" },\n"
        "    { \"id\": \"scenario.ai.action.walk_to_pad\", \"kind\": \"scenario.ai.action.walk_to_pad\", \"source\": \"ai/ailists.tsv\", \"pads\": \"pads.tsv\", \"opcode\": \"0x001f\", \"speed\": \"walk\" },\n"
        "    { \"id\": \"scenario.ai.action.run_to_pad\", \"kind\": \"scenario.ai.action.run_to_pad\", \"source\": \"ai/ailists.tsv\", \"pads\": \"pads.tsv\", \"opcode\": \"0x0020\", \"speed\": \"run\" },\n"
        "    { \"id\": \"scenario.ai.action.set_path\", \"kind\": \"scenario.ai.action.set_path\", \"source\": \"ai/ailists.tsv\", \"paths\": \"navigation/paths.tsv\", \"opcode\": \"0x0021\" },\n"
        "    { \"id\": \"scenario.ai.action.start_patrol\", \"kind\": \"scenario.ai.action.start_patrol\", \"source\": \"ai/ailists.tsv\", \"paths\": \"navigation/paths.tsv\", \"opcode\": \"0x0022\" },\n"
        "    { \"id\": \"trigger.volume.0000\", \"kind\": \"scenario.trigger.volume.source\", \"table\": \"volumes.tsv\", \"volume\": \"volume_pad_0000\", \"pad\": \"pad_0000\" }\n"
        "  ],\n"
        "  \"links\": [\n"
        "    { \"from\": \"scenario.load\", \"to\": \"source.scene\" },\n"
        "    { \"from\": \"scenario.load\", \"to\": \"scenario.pads\" },\n"
        "    { \"from\": \"source.scene\", \"to\": \"navigation.paths\" },\n"
        "    { \"from\": \"scenario.load\", \"to\": \"scenario.ai.lists\" },\n"
        "    { \"from\": \"scenario.load\", \"to\": \"scenario.global.settings\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.jog_to_pad\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.go_to_pad_preset\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.walk_to_pad\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.run_to_pad\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.set_path\" },\n"
        "    { \"from\": \"scenario.ai.lists\", \"to\": \"scenario.ai.action.start_patrol\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.jog_to_pad\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.go_to_pad_preset\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.walk_to_pad\" },\n"
        "    { \"from\": \"scenario.pads\", \"to\": \"scenario.ai.action.run_to_pad\" },\n"
        "    { \"from\": \"navigation.paths\", \"to\": \"scenario.ai.action.set_path\" },\n"
        "    { \"from\": \"navigation.paths\", \"to\": \"scenario.ai.action.start_patrol\" },\n"
        "    { \"from\": \"scenario.load\", \"to\": \"trigger.volume.0000\" }\n"
        "  ],\n"
        "  \"counts\": { \"rooms\": 1, \"triangles\": 1, \"pads\": 1, \"volumes\": 1, \"objects\": 1, \"objectives\": 1, \"ai_lists\": 1, \"waypoints\": 0, \"waygroups\": 0, \"covers\": 0, \"paths\": 1 }\n"
        "}\n"
    )
    scenario_ini = (
        "; tri_scenario.pdscenario - source-first scenario asset\n"
        "[scenario]\n"
        "catalog_id = example:tri_scenario\n"
        "name = Triangle Scenario\n"
        "mode = mp|solo\n"
        "scene_file = scene.glb\n"
        "scene_format = GLB\n"
        "runtime_source_file = scene.glb\n"
        "collision_source = scene.glb\n"
        "collision_fallback = scene\n"
        "pads_file = pads.tsv\n"
        "spawns_file = spawns.tsv\n"
        "volumes_file = volumes.tsv\n"
        "waypoints_file = navigation/waypoints.tsv\n"
        "waygroups_file = navigation/waygroups.tsv\n"
        "covers_file = navigation/covers.tsv\n"
        "paths_file = navigation/paths.tsv\n"
        "objects_file = objects.tsv\n"
        "setup_fields_file = setup.fields.tsv\n"
        "ai_lists_file = ai/ailists.tsv\n"
        "objectives_file = objectives.tsv\n"
        "navigation_file = navigation.ini\n"
        "level_graph_file = level.graph.json\n"
        "\n[meta]\n"
        "manifest = _meta/manifest.json\n"
    )
    scenario_manifest = (
        "{\n"
        "  \"pd_kind\": \"scenario\",\n"
        "  \"pd_schema_version\": 1,\n"
        "  \"id\": \"example:tri_scenario\",\n"
        "  \"scene\": \"scene.glb\",\n"
        "  \"scene_format\": \"GLB\",\n"
        "  \"runtime_source\": \"scene.glb\",\n"
        "  \"collision_source\": \"scene.glb\",\n"
        "  \"collision_fallback\": \"from_scene\",\n"
        "  \"navigation\": \"navigation.ini\",\n"
        "  \"level_graph\": \"level.graph.json\",\n"
        "  \"pads\": \"pads.tsv\",\n"
        "  \"spawns\": \"spawns.tsv\",\n"
        "  \"volumes\": \"volumes.tsv\",\n"
        "  \"waypoints\": \"navigation/waypoints.tsv\",\n"
        "  \"waygroups\": \"navigation/waygroups.tsv\",\n"
        "  \"covers\": \"navigation/covers.tsv\",\n"
        "  \"paths\": \"navigation/paths.tsv\",\n"
        "  \"objects\": \"objects.tsv\",\n"
        "  \"setup_fields\": \"setup.fields.tsv\",\n"
        "  \"ai_lists\": \"ai/ailists.tsv\",\n"
        "  \"objectives\": \"objectives.tsv\"\n"
        "}\n"
    )
    write_archive(rel, [
        ("scenario.ini", scenario_ini),
        ("scene.glb", kept["scene.glb"]),
        ("pads.tsv", kept["pads.tsv"]),
        ("spawns.tsv", kept["spawns.tsv"]),
        ("volumes.tsv", kept["volumes.tsv"]),
        ("navigation/waypoints.tsv", waypoints_tsv),
        ("navigation/waygroups.tsv", waygroups_tsv),
        ("navigation/covers.tsv", covers_tsv),
        ("navigation/paths.tsv", paths_tsv),
        ("objects.tsv", kept["objects.tsv"]),
        ("setup.fields.tsv", setup_fields_tsv),
        ("ai/ailists.tsv", ai_lists_tsv),
        ("objectives.tsv", objectives_tsv),
        ("navigation.ini", navigation_ini),
        ("level.graph.json", level_graph_json),
        ("_meta/generated-collision.json", kept["_meta/generated-collision.json"]),
        ("_meta/generated-navmesh.json", kept["_meta/generated-navmesh.json"]),
        ("_meta/manifest.json", scenario_manifest),
    ])
    return read_archive(rel)


def update_mesh() -> bytes:
    rel = "meshes/tri_mesh.pdmesh"
    gltf = json.loads(read_entry(rel, "model.gltf").decode("utf-8"))
    gltf.pop("images", None)
    gltf.pop("textures", None)
    gltf["materials"] = [{
        "name": "TriangleMaterial",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.2, 0.8, 1.0, 1.0],
            "metallicFactor": 0.0,
            "roughnessFactor": 0.8,
        },
    }]
    for mesh in gltf.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            primitive["material"] = 0

    write_archive(rel, [
        ("mesh.ini",
         "; tri_mesh.pdmesh - self-contained editable mesh asset\n"
         "[mesh]\n"
         "catalog_id = example:tri_mesh\n"
         "model_file = model.gltf\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("model.gltf", json.dumps(gltf, indent=2) + "\n"),
        ("_meta/manifest.json", manifest("mesh", "example:tri_mesh")),
    ])
    return read_archive(rel)


def update_head_and_body(mesh_bytes: bytes) -> tuple[bytes, bytes]:
    write_archive("heads/tri_head.pdhead", [
        ("head.ini",
         "; tri_head.pdhead - focused head asset with typed mesh dependency\n"
         "[head]\n"
         "catalog_id = example:tri_head\n"
         "rig_class = human_male_neck_standard\n"
         "mesh_archive = mesh.pdmesh\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("mesh.pdmesh", mesh_bytes),
        ("_meta/manifest.json", manifest("head", "example:tri_head")),
    ])
    write_archive("bodies/tri_body.pdbody", [
        ("body.ini",
         "; tri_body.pdbody - focused body asset with typed mesh dependencies\n"
         "[body]\n"
         "catalog_id = example:tri_body\n"
         "display_name = Triangle Body\n"
         "rig_class = human_male_neck_standard\n"
         "mesh_archive = mesh.pdmesh\n"
         "hand_archive = hand.pdmesh\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("mesh.pdmesh", mesh_bytes),
        ("hand.pdmesh", mesh_bytes),
        ("_meta/manifest.json", manifest("body", "example:tri_body")),
    ])
    return read_archive("heads/tri_head.pdhead"), read_archive("bodies/tri_body.pdbody")


def update_character(head_bytes: bytes, body_bytes: bytes) -> None:
    portrait = read_entry("characters/tri_character.pdcharacter", "portrait.png")
    write_archive("characters/tri_character.pdcharacter", [
        ("character.ini",
         "; tri_character.pdcharacter - assembler asset with typed body/head dependencies\n"
         "[character]\n"
         "catalog_id = example:tri_character\n"
         "display_name = Triangle Character\n"
         "body_archive = dependencies/assets/body/tri_body.pdbody\n"
         "head_archive = dependencies/assets/head/tri_head.pdhead\n"
         "portrait_file = portrait.png\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("portrait.png", portrait),
        ("dependencies/assets/body/tri_body.pdbody", body_bytes),
        ("dependencies/assets/head/tri_head.pdhead", head_bytes),
        ("_meta/manifest.json", manifest("character", "example:tri_character")),
    ])


def update_arena(scenario_bytes: bytes) -> None:
    write_archive("arenas/tri_arena.pdarena", [
        ("arena.ini",
         "; tri_arena.pdarena - multiplayer wrapper around a typed scenario dependency\n"
         "[arena]\n"
         "catalog_id = example:tri_arena\n"
         "load_mode = ARENA_LOADMODE_PLAYABLE\n"
         "scenario = example:tri_scenario\n"
         "scenario_archive = dependencies/assets/scenarios/tri_scenario.pdscenario\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("dependencies/assets/scenarios/tri_scenario.pdscenario", scenario_bytes),
        ("_meta/manifest.json", manifest("arena", "example:tri_arena")),
    ])


def update_weapon(mesh_bytes: bytes) -> None:
    material = read_archive("materials/tri_material.pdmaterial")
    texture = read_archive("textures/tri_texture.pdtexture")
    anim = read_archive("animations/weapon_idle.pdanim")
    sfx = read_archive("audio/sfx/tri_click.pdsfx")
    projectile = read_archive("projectiles/tri_projectile.pdprojectile")
    entity = read_archive("entities/tri_entity.pdentity")
    ui = read_archive("ui/tri_reticle.pdui")
    primary_graph = (
        "{\n"
        "  \"schema\": \"pd.weapon_graph.v1\",\n"
        "  \"asset_id\": \"example:tri_weapon\",\n"
        "  \"graph_id\": \"primary\",\n"
        "  \"nodes\": [\n"
        "    { \"id\": \"trigger_primary\", \"kind\": \"event.trigger_pressed\", \"params\": { \"mode\": \"primary\" } },\n"
        "    { \"id\": \"spawn_projectile\", \"kind\": \"spawn.projectile\", \"params\": { \"projectile\": \"example:tri_projectile\" } }\n"
        "  ],\n"
        "  \"edges\": [ { \"from\": \"trigger_primary.exec\", \"to\": \"spawn_projectile.exec\" } ],\n"
        "  \"exports\": [ { \"name\": \"primary\", \"node\": \"trigger_primary\" } ]\n"
        "}\n"
    )
    secondary_graph = (
        "{\n"
        "  \"schema\": \"pd.weapon_graph.v1\",\n"
        "  \"asset_id\": \"example:tri_weapon\",\n"
        "  \"graph_id\": \"secondary\",\n"
        "  \"nodes\": [\n"
        "    { \"id\": \"trigger_secondary\", \"kind\": \"event.trigger_pressed\", \"params\": { \"mode\": \"secondary\" } },\n"
        "    { \"id\": \"deploy_entity\", \"kind\": \"spawn.deployed_entity\", \"params\": { \"entity\": \"example:tri_entity\" } }\n"
        "  ],\n"
        "  \"edges\": [ { \"from\": \"trigger_secondary.exec\", \"to\": \"deploy_entity.exec\" } ],\n"
        "  \"exports\": [ { \"name\": \"secondary\", \"node\": \"trigger_secondary\" } ]\n"
        "}\n"
    )
    write_archive("weapons/tri_weapon.pdweapon", [
        ("weapon.ini",
         "; tri_weapon.pdweapon - clean weapon asset with typed dependency closure\n"
         "[weapon]\n"
         "catalog_id = example:tri_weapon\n"
         "name = Triangle Weapon\n"
         "dual_wieldable = false\n"
         "model_file = dependencies/assets/models/weapon.pdmesh\n"
         "primary_graph = behavior/primary.graph.json\n"
         "secondary_graph = behavior/secondary.graph.json\n"
         "settings_file = behavior/settings.json\n"
         "variables_file = behavior/variables.json\n"
         "shared_context_file = behavior/shared-context.json\n"
         "material_slots_file = bindings/material-slots.json\n"
         "grip_sockets_file = bindings/grip-sockets.json\n"
         "presentation_file = bindings/presentation.json\n"
         "primary_projectile_archive = dependencies/assets/projectiles/primary.pdprojectile\n"
         "deployed_entity_archive = dependencies/assets/entities/deployed.pdentity\n"
         "fire_sound_archive = dependencies/assets/audio/fire.pdsfx\n"
         "idle_animation_archive = dependencies/assets/animations/idle.pdanim\n"
         "reticle_archive = dependencies/assets/ui/reticle.pdui\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("behavior/primary.graph.json", primary_graph),
        ("behavior/secondary.graph.json", secondary_graph),
        ("behavior/settings.json",
         "{\n  \"schema\": \"pd.weapon.settings.v1\",\n  \"fire_cadence\": { \"value\": 25, \"unit\": \"centiseconds\" }\n}\n"),
        ("behavior/variables.json",
         "{\n  \"schema\": \"pd.weapon.variables.v1\",\n  \"ammo_clip\": 12,\n  \"ammo_reserve\": 120\n}\n"),
        ("behavior/shared-context.json",
         "{\n  \"schema\": \"pd.weapon.shared_context.v1\",\n  \"contexts\": [\"owner_player\", \"owner_team\", \"weapon_instance\", \"damage_credit_player\"]\n}\n"),
        ("bindings/material-slots.json",
         "{\n  \"schema\": \"pd.weapon.material_slots.v1\",\n  \"slots\": [{ \"name\": \"body\", \"material\": \"dependencies/assets/materials/default.pdmaterial\" }]\n}\n"),
        ("bindings/grip-sockets.json",
         "{\n  \"schema\": \"pd.weapon.grip_sockets.v1\",\n  \"sockets\": [{ \"name\": \"primary_grip\", \"mesh_socket\": \"grip\" }]\n}\n"),
        ("bindings/presentation.json",
         "{\n  \"schema\": \"pd.weapon.presentation.v1\",\n  \"reticle\": \"dependencies/assets/ui/reticle.pdui\"\n}\n"),
        ("dependencies/assets/models/weapon.pdmesh", mesh_bytes),
        ("dependencies/assets/materials/default.pdmaterial", material),
        ("dependencies/assets/textures/body.pdtexture", texture),
        ("dependencies/assets/animations/idle.pdanim", anim),
        ("dependencies/assets/audio/fire.pdsfx", sfx),
        ("dependencies/assets/projectiles/primary.pdprojectile", projectile),
        ("dependencies/assets/entities/deployed.pdentity", entity),
        ("dependencies/assets/ui/reticle.pdui", ui),
        ("_meta/manifest.json", manifest("weapon", "example:tri_weapon")),
    ])


def update_botprofile() -> None:
    profile = read_entry("botprofiles/tri_botprofile.pdbotprofile", "profile.json")
    write_archive("botprofiles/tri_botprofile.pdbotprofile", [
        ("botprofile.ini",
         "[bot_profile]\n"
         "catalog_id = example:tri_botprofile\n"
         "type_key = general\n"
         "difficulty_key = normal\n"
         "target_body = example:tri_body\n"
         "profile_file = profile.json\n"),
        ("profile.json", profile),
        ("_meta/manifest.json", manifest("botprofile", "example:tri_botprofile")),
    ])


def update_effect() -> None:
    graph = read_entry("effects/tri_effect.pdeffect", "effect.graph.json")
    write_archive("effects/tri_effect.pdeffect", [
        ("effect.ini",
         "[effect]\n"
         "catalog_id = example:tri_effect\n"
         "name = Triangle Glow\n"
         "effect_key = glow\n"
         "target_key = weapon\n"
         "effect_file = effect.graph.json\n"
         "shader_id = example_glow\n"
         "intensity = 0.75\n"),
        ("effect.graph.json", graph),
        ("_meta/manifest.json", manifest("effect", "example:tri_effect")),
    ])


def update_prop() -> None:
    model = read_entry("props/tri_prop.pdprop", "model.gltf")
    behavior = read_entry("props/tri_prop.pdprop", "behavior.graph.json")
    write_archive("props/tri_prop.pdprop", [
        ("prop.ini",
         "[prop]\n"
         "catalog_id = example:tri_prop\n"
         "name = Triangle Prop\n"
         "prop_key = object\n"
         "model_file = model.gltf\n"
         "health = 100\n"
         "behavior_graph = behavior.graph.json\n"),
        ("model.gltf", model),
        ("behavior.graph.json", behavior),
        ("_meta/manifest.json", manifest("prop", "example:tri_prop")),
    ])


def update_gamemode() -> None:
    rules = read_entry("gamemodes/tri_gamemode.pdgamemode", "rules.json")
    write_archive("gamemodes/tri_gamemode.pdgamemode", [
        ("gamemode.ini",
         "[gamemode]\n"
         "catalog_id = example:tri_gamemode\n"
         "name = Triangle Rules\n"
         "mode_key = custom\n"
         "min_players = 2\n"
         "max_players = 8\n"
         "team_based = 0\n"
         "rules_file = rules.json\n"),
        ("rules.json", rules),
        ("_meta/manifest.json", manifest("gamemode", "example:tri_gamemode")),
    ])


def update_hud() -> None:
    layout = read_entry("hud/tri_hud.pdhud", "layout.json")
    texture = read_entry("hud/tri_hud.pdhud", "texture.png")
    write_archive("hud/tri_hud.pdhud", [
        ("hud.ini",
         "[hud]\n"
         "catalog_id = example:tri_hud\n"
         "name = Triangle HUD\n"
         "hud_key = ammo\n"
         "texture_file = texture.png\n"
         "layout_file = layout.json\n"),
        ("layout.json", layout),
        ("texture.png", texture),
        ("_meta/manifest.json", manifest("hud", "example:tri_hud")),
    ])


def rewrite_without_directory_entries(rel: str) -> None:
    with zipfile.ZipFile(archive(rel), "r") as zf:
        entries = [(name, zf.read(name)) for name in zf.namelist()
                   if name and not name.endswith("/")]
    write_archive(rel, entries)


def main() -> int:
    update_animation(
        "animations/weapon_idle.pdanim",
        "example:weapon_idle",
        "Weapon Idle",
        "weapon_animation",
    )
    update_animation(
        "animations/character_skeletal.pdanim",
        "example:character_skeletal",
        "Character Skeletal",
        "character_animation",
    )

    mesh_bytes = update_mesh()
    scenario_bytes = update_scenario()
    head_bytes, body_bytes = update_head_and_body(mesh_bytes)
    update_character(head_bytes, body_bytes)
    update_arena(scenario_bytes)
    update_botprofile()
    update_effect()
    update_prop()
    update_gamemode()
    update_hud()

    rewrite_without_directory_entries("skins/tri_skin.pdskin")
    rewrite_without_directory_entries("themes/tri_theme.pdtheme")
    update_weapon(mesh_bytes)
    scenario_bytes = read_archive("scenarios/tri_scenario.pdscenario")
    rewrite_without_directory_entries("missions/tri_mission.pdmission")
    with zipfile.ZipFile(archive("missions/tri_mission.pdmission"), "r") as zf:
        mission_entries = [(name, zf.read(name)) for name in zf.namelist()
                           if name != "dependencies/assets/scenario/tri_scenario.pdscenario"]
    mission_graph = (
        "{\n"
        "  \"schema\": \"pd2.mission.graph.v1\",\n"
        "  \"catalog_id\": \"example:tri_mission\",\n"
        "  \"scenario_ref\": \"example:tri_scenario\",\n"
        "  \"nodes\": [\n"
        "    { \"id\": \"mission.load\", \"kind\": \"event.mission.load\", \"scenario\": \"example:tri_scenario\" },\n"
        "    { \"id\": \"mission.objectives\", \"kind\": \"mission.objectives.source\", \"file\": \"objectives.tsv\", \"scenario_table\": \"dependencies/assets/scenario/tri_scenario.pdscenario::objectives.tsv\" },\n"
        "    { \"id\": \"mission.objective.0000\", \"kind\": \"mission.objective.source\", \"source_row\": \"objective_0000\", \"scenario_node\": \"level.objective.0000\", \"text_token\": \"objective_text_primary\", \"difficulty_mask\": \"all\", \"criteria\": \"objective\", \"operand_kind\": \"objective\", \"target_ref\": \"\", \"target_record_ref\": \"\", \"pad_ref\": \"\", \"state_ref\": \"\", \"match_value\": \"\", \"initial_status\": \"\" },\n"
        "    { \"id\": \"mission.phase.load\", \"kind\": \"mission.phase.source\", \"phase\": \"load\" },\n"
        "    { \"id\": \"mission.phase.active\", \"kind\": \"mission.phase.source\", \"phase\": \"active\" },\n"
        "    { \"id\": \"mission.phase.complete\", \"kind\": \"mission.phase.source\", \"phase\": \"complete\" },\n"
        "    { \"id\": \"mission.phase.failed\", \"kind\": \"mission.phase.source\", \"phase\": \"failed\" },\n"
        "    { \"id\": \"mission.phase.end\", \"kind\": \"mission.phase.source\", \"phase\": \"end\" },\n"
        "    { \"id\": \"mission.parity_backend\", \"kind\": \"mission.behavior.parity_backend\", \"module\": \"og.mission.example\" }\n"
        "  ],\n"
        "  \"edges\": [\n"
        "    { \"from\": \"mission.load\", \"to\": \"mission.objectives\" },\n"
        "    { \"from\": \"mission.objectives\", \"to\": \"mission.objective.0000\" },\n"
        "    { \"from\": \"mission.objectives\", \"to\": \"mission.parity_backend\" }\n"
        "  ]\n"
        "}\n"
    ).encode("utf-8")
    mission_objectives = (
        "objective_id\tkind\ttext_token\tdifficulty_mask\tgraph_node\tscenario_source\toperand_kind\ttarget_ref\ttarget_record_ref\tpad_ref\tstate_ref\tmatch_value\tinitial_status\n"
        "objective_0000\tobjective\tobjective_text_primary\tall\tmission.objective.0000\tdependencies/assets/scenario/tri_scenario.pdscenario::objectives.tsv#objective_0000\tobjective\t\t\t\t\t\t\n"
    ).encode("utf-8")
    mission_entries = [
        (name, mission_graph if name == "mission.graph.json"
         else mission_objectives if name == "objectives.tsv" else data)
        for name, data in mission_entries
    ]
    mission_entries.insert(4, (
        "dependencies/assets/scenario/tri_scenario.pdscenario",
        scenario_bytes,
    ))
    write_archive("missions/tri_mission.pdmission", mission_entries)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
