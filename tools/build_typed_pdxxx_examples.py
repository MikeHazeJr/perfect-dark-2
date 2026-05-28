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
        "navigation.ini": read_entry(rel, "navigation.ini"),
        "level.graph.json": read_entry(rel, "level.graph.json"),
        "_meta/generated-collision.json": read_entry(rel, "_meta/generated-collision.json"),
        "_meta/generated-navmesh.json": read_entry(rel, "_meta/generated-navmesh.json"),
    }
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
        "objects_file = objects.tsv\n"
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
        "  \"objects\": \"objects.tsv\",\n"
        "  \"objectives\": \"objectives.tsv\"\n"
        "}\n"
    )
    write_archive(rel, [
        ("scenario.ini", scenario_ini),
        ("scene.glb", kept["scene.glb"]),
        ("pads.tsv", kept["pads.tsv"]),
        ("spawns.tsv", kept["spawns.tsv"]),
        ("volumes.tsv", kept["volumes.tsv"]),
        ("objects.tsv", kept["objects.tsv"]),
        ("objectives.tsv", kept["objectives.tsv"]),
        ("navigation.ini", kept["navigation.ini"]),
        ("level.graph.json", kept["level.graph.json"]),
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
         "type = general\n"
         "difficulty = normal\n"
         "target_body = example:tri_body\n"
         "profile_file = profile.json\n"),
        ("profile.json", profile),
        ("_meta/manifest.json", manifest("botprofile", "example:tri_botprofile")),
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

    rewrite_without_directory_entries("skins/tri_skin.pdskin")
    rewrite_without_directory_entries("themes/tri_theme.pdtheme")
    update_weapon(mesh_bytes)
    scenario_bytes = read_archive("scenarios/tri_scenario.pdscenario")
    rewrite_without_directory_entries("missions/tri_mission.pdmission")
    with zipfile.ZipFile(archive("missions/tri_mission.pdmission"), "r") as zf:
        mission_entries = [(name, zf.read(name)) for name in zf.namelist()
                           if name != "dependencies/assets/scenario/tri_scenario.pdscenario"]
    mission_entries.insert(4, (
        "dependencies/assets/scenario/tri_scenario.pdscenario",
        scenario_bytes,
    ))
    write_archive("missions/tri_mission.pdmission", mission_entries)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
