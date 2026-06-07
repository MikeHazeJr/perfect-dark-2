#!/usr/bin/env python3
"""Strict conformance checks for PD2 typed asset archives.

The check is schema-specific by asset family. It intentionally does not accept
"at least one editable file" as proof of conformance: every family declares the
required public entries, definitive optional slots, forbidden stale entries,
and dependency archive requirements that match the clean c3824/c3842 contract.
"""

from __future__ import annotations

import argparse
import csv
import fnmatch
import hashlib
import json
import re
import struct
import sys
import zipfile
from dataclasses import dataclass, field
from io import BytesIO
from pathlib import Path


TYPED_DESCRIPTORS = {
    ".pdweapon": "weapon.ini",
    ".pdprojectile": "projectile.ini",
    ".pdentity": "entity.ini",
    ".pdmaterial": "material.ini",
    ".pdtexture": "texture.ini",
    ".pdcharacter": "character.ini",
    ".pdhead": "head.ini",
    ".pdbody": "body.ini",
    ".pdarena": "arena.ini",
    ".pdscenario": "scenario.ini",
    ".pdmesh": "mesh.ini",
    ".pdanim": "animation.ini",
    ".pdsfx": "sound.ini",
    ".pdvoice": "voice.ini",
    ".pdsong": "music.ini",
    ".pdui": "ui.ini",
    ".pdfont": "font.ini",
    ".pdlang": "lang.ini",
    ".pdskin": "skin.ini",
    ".pdeffect": "effect.ini",
    ".pdprop": "prop.ini",
    ".pdvehicle": "vehicle.ini",
    ".pdmission": "mission.ini",
    ".pdgamemode": "gamemode.ini",
    ".pdbotprofile": "botprofile.ini",
    ".pdhud": "hud.ini",
    ".pdtheme": "theme.ini",
}

FAMILY_NAMES = {
    ".pdweapon": "weapon",
    ".pdprojectile": "projectile",
    ".pdentity": "entity",
    ".pdmaterial": "material",
    ".pdtexture": "texture",
    ".pdcharacter": "character",
    ".pdhead": "head",
    ".pdbody": "body",
    ".pdarena": "arena",
    ".pdscenario": "scenario",
    ".pdmesh": "mesh",
    ".pdanim": "animation",
    ".pdsfx": "sfx",
    ".pdvoice": "voice",
    ".pdsong": "song",
    ".pdui": "ui",
    ".pdfont": "font",
    ".pdlang": "lang",
    ".pdskin": "skin",
    ".pdeffect": "effect",
    ".pdprop": "prop",
    ".pdvehicle": "vehicle",
    ".pdmission": "mission",
    ".pdgamemode": "gamemode",
    ".pdbotprofile": "botprofile",
    ".pdhud": "hud",
    ".pdtheme": "theme",
}

ROOT_METADATA = {
    "manifest.json",
    "inventory.json",
    "provenance.json",
    "validation.json",
    "source-handles.json",
    "hashes.tsv",
}

SCENARIO_GRAPH_CACHE_KIND = (
    "pdscenario_scene_glb_clean_public_v84_standalone_backfill_collision_obj_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_quip_shuffle_graph_portals_navhashes"
)

FORBIDDEN_COMMON_EXACT = {
    "data.bin",
    "geometry.bin",
    "setup.bin",
    "mpsetup.bin",
    "tiles.bin",
    "pads.bin",
    "runtime.graph.json",
    "behavior/runtime.graph.json",
    "nested_payloads.json",
}

FORBIDDEN_COMMON_GLOBS = {
    "*.pdwpn",
    "*.bin",
    "_meta/_meta/*",
}

FORBIDDEN_SCENARIO_EXACT = {
    "rooms.obj",
    "scenario.mtl",
    "tiles.tsv",
    "setup.tsv",
    "mpsetup.tsv",
    "visual_segments.tsv",
    "setup.ini",
    "mpsetup.ini",
    "props.ini",
}

FORBIDDEN_SCENARIO_GLOBS = {
    "visual/*",
}

FORBIDDEN_WEAPON_EXACT = {
    "behavior.graph.json",
    "model.gltf",
    "model.glb",
    "hand.gltf",
    "hand.glb",
    "texture.png",
    "weapon_texture.png",
    "reload.gltf",
    "fire.wav",
}

PUBLIC_TEXT_ENTRY_SUFFIXES = (
    ".ini",
    ".json",
    ".tsv",
    ".txt",
    ".csv",
)

SCENARIO_OBJECTIVES_HEADER = [
    "objective_id",
    "kind",
    "text_token",
    "difficulty_mask",
    "graph_node",
    "operand_kind",
    "target_ref",
    "target_record_ref",
    "pad_ref",
    "state_ref",
    "match_value",
    "initial_status",
]

SCENARIO_SPAWNS_HEADER = (
    "spawn_id\tpad_ref\troom_ref\tteam\tprofile\tpos_x\tpos_y\tpos_z\t"
    "look_x\tlook_y\tlook_z"
)
SCENARIO_PORTALS_HEADER = "portal_id\troom_a\troom_b\tflags\tvertices"

MISSION_OBJECTIVES_HEADER = [
    "objective_id",
    "kind",
    "text_token",
    "difficulty_mask",
    "graph_node",
    "scenario_source",
    "operand_kind",
    "target_ref",
    "target_record_ref",
    "pad_ref",
    "state_ref",
    "match_value",
    "initial_status",
]

OBJECTIVE_KIND_OPERANDS = {
    "objective": "objective",
    "objective_destroy_object": "tag_target",
    "objective_complete_flags": "stage_flag",
    "objective_fail_flags": "stage_flag",
    "objective_collect_object": "tag_target",
    "objective_throw_object": "tag_target",
    "objective_holograph": "tag_target_status",
    "objective_enter_room": "pad_status",
    "objective_throw_in_room": "throw_match_pad_status",
}

FORBIDDEN_SETUP_OBJECTIVE_FIELDS = {
    "objective_step.argument",
    "objective_holograph.object",
}

KEY_VALUE_RE = re.compile(r"^\s*([A-Za-z0-9_.-]+)\s*[:=]\s*(.*?)\s*$")
NUMERIC_LITERAL_RE = re.compile(r"^[+-]?(?:0x[0-9A-Fa-f]+|\d+)$")
CATALOG_ID_RE = re.compile(
    r"^[A-Za-z][A-Za-z0-9_.-]*:[A-Za-z0-9][A-Za-z0-9_.:-]*$"
)

FORBIDDEN_NUMERIC_ASSET_REF_KEYS = {
    "anim",
    "anim_id",
    "animation",
    "animation_id",
    "animation_ref",
    "animnum",
    "arena",
    "arena_id",
    "body",
    "body_id",
    "body_ref",
    "bodynum",
    "entity",
    "entity_id",
    "entity_ref",
    "file_id",
    "file_num",
    "filenum",
    "fire_animation",
    "effect_type",
    "element_type",
    "head",
    "head_id",
    "head_ref",
    "headnum",
    "hit_sound",
    "hud_id",
    "material",
    "material_id",
    "material_ref",
    "mesh",
    "mesh_id",
    "mesh_ref",
    "model",
    "model_id",
    "model_ref",
    "modelnum",
    "mode_id",
    "music",
    "music_id",
    "projectile_model_ref",
    "projectile",
    "projectile_id",
    "scenario",
    "scenario_id",
    "stagenum",
    "prop_type",
    "sfx",
    "sfx_id",
    "shoot_sound",
    "shoot_sound_ref",
    "shootsound",
    "sound",
    "sound_id",
    "sound_ref",
    "soundnum",
    "texnum",
    "texture",
    "texture_id",
    "voice",
    "voice_id",
    "weapon",
    "weapon_id",
    "weapon_ref",
    "weaponnum",
}

CATALOG_IDENTITY_KEYS = {
    "asset_id",
    "catalog_id",
}

CATALOG_REFERENCE_KEYS = FORBIDDEN_NUMERIC_ASSET_REF_KEYS | {
    "base_fallback",
    "body_catalog_id",
    "character_ref",
    "clip_ref",
    "detonator_ref",
    "effect_ref",
    "fallback_id",
    "hud_ref",
    "impact_effect_ref",
    "material_catalog_id",
    "model_catalog_id",
    "payload_ref",
    "projectile_model_catalog_id",
    "projectile_sound_catalog_id",
    "skin_ref",
    "sound_catalog_id",
    "special_sound_catalog_id",
    "stage_id",
    "template",
    "texture_catalog_id",
    "theme_ref",
    "transfer_payload",
    "ui_ref",
    "vehicle_ref",
}

NON_ASSET_ID_KEYS = {
    "graph_id",
    "node_id",
    "slot_id",
    "source_id",
    "subgraph_id",
}

LEGACY_ASSET_SYMBOL_PREFIXES = (
    "ANIM_",
    "BODY_",
    "FILE_",
    "HEAD_",
    "MODEL_",
    "MPSCENARIO_",
    "MPWEAPON_",
    "SFX_",
    "STAGE_",
    "TEX_",
    "WEAPON_",
    "invanim_",
)


@dataclass(frozen=True)
class Schema:
    required: frozenset[str] = frozenset()
    require_any: tuple[tuple[str, ...], ...] = ()
    require_one_of: tuple[tuple[tuple[str, ...], ...], ...] = ()
    allowed: frozenset[str] = frozenset()
    allowed_globs: tuple[str, ...] = ()
    forbidden: frozenset[str] = frozenset()
    forbidden_globs: tuple[str, ...] = ()


@dataclass(frozen=True)
class SlotJustification:
    semantic_role: str
    owner: str
    loader_behavior: str
    absence_behavior: str
    status: str = "final"


def slot(
    role: str,
    owner: str,
    loader: str,
    absent: str,
    status: str = "final",
) -> SlotJustification:
    return SlotJustification(
        semantic_role=role,
        owner=owner,
        loader_behavior=loader,
        absence_behavior=absent,
        status=status,
    )


def schema(
    *,
    required: list[str] | None = None,
    require_any: list[list[str]] | None = None,
    require_one_of: list[list[list[str]]] | None = None,
    allowed: list[str] | None = None,
    allowed_globs: list[str] | None = None,
    forbidden: list[str] | None = None,
    forbidden_globs: list[str] | None = None,
) -> Schema:
    return Schema(
        required=frozenset(required or []),
        require_any=tuple(tuple(group) for group in (require_any or [])),
        require_one_of=tuple(
            tuple(tuple(alt) for alt in rule)
            for rule in (require_one_of or [])
        ),
        allowed=frozenset(allowed or []),
        allowed_globs=tuple(allowed_globs or []),
        forbidden=frozenset(forbidden or []),
        forbidden_globs=tuple(forbidden_globs or []),
    )


SCHEMAS: dict[str, Schema] = {
    ".pdweapon": schema(
        required=[
            "weapon.ini",
            "behavior/primary.graph.json",
            "behavior/secondary.graph.json",
            "behavior/settings.json",
            "behavior/variables.json",
            "behavior/shared-context.json",
        ],
        require_one_of=[
            [
                ["bindings/animations.tsv", "bindings/audio.tsv"],
                [
                    "bindings/material-slots.json",
                    "bindings/grip-sockets.json",
                    "bindings/presentation.json",
                ],
            ],
        ],
        allowed=[
            "weapon.ini",
            "behavior/primary.graph.json",
            "behavior/secondary.graph.json",
            "behavior/settings.json",
            "behavior/variables.json",
            "behavior/shared-context.json",
            "bindings/animations.tsv",
            "bindings/audio.tsv",
            "bindings/material-slots.json",
            "bindings/grip-sockets.json",
            "bindings/presentation.json",
        ],
        allowed_globs=[
            "dependencies/assets/models/*.pdmesh",
            "dependencies/assets/materials/*.pdmaterial",
            "dependencies/assets/textures/*.pdtexture",
            "dependencies/assets/animations/*.pdanim",
            "dependencies/assets/audio/*.pdsfx",
            "dependencies/assets/projectiles/*.pdprojectile",
            "dependencies/assets/entities/*.pdentity",
            "dependencies/assets/ui/*.pdui",
        ],
        forbidden=sorted(FORBIDDEN_WEAPON_EXACT),
    ),
    ".pdprojectile": schema(
        required=["projectile.ini", "behavior.graph.json"],
        allowed=["projectile.ini", "behavior.graph.json"],
        allowed_globs=[
            "dependencies/assets/models/*.pdmesh",
            "dependencies/assets/materials/*.pdmaterial",
            "dependencies/assets/textures/*.pdtexture",
            "dependencies/assets/audio/*.pdsfx",
            "dependencies/assets/effects/*.pdeffect",
            "dependencies/assets/entities/*.pdentity",
        ],
    ),
    ".pdentity": schema(
        required=["entity.ini", "bindings.json", "behavior.graph.json"],
        allowed=["entity.ini", "bindings.json", "behavior.graph.json", "composition.json"],
        allowed_globs=[
            "dependencies/assets/props/*.pdprop",
            "dependencies/assets/models/*.pdmesh",
            "dependencies/assets/materials/*.pdmaterial",
            "dependencies/assets/textures/*.pdtexture",
            "dependencies/assets/audio/*.pdsfx",
            "dependencies/assets/effects/*.pdeffect",
        ],
    ),
    ".pdmaterial": schema(
        required=["material.ini"],
        require_any=[["material.json", "dependencies/assets/texture/*.pdtexture", "dependencies/assets/textures/*.pdtexture"]],
        allowed=["material.ini", "material.json"],
        allowed_globs=[
            "dependencies/assets/texture/*.pdtexture",
            "dependencies/assets/textures/*.pdtexture",
            "dependencies/assets/effects/*.pdeffect",
        ],
    ),
    ".pdtexture": schema(
        required=["texture.ini"],
        require_any=[["texture.png", "texture.tga", "texture.jpg", "texture.jpeg"]],
        allowed=["texture.ini", "texture.png", "texture.tga", "texture.jpg", "texture.jpeg"],
    ),
    ".pdcharacter": schema(
        required=["character.ini"],
        require_any=[["body.pdbody", "dependencies/assets/body/*.pdbody", "dependencies/assets/bodies/*.pdbody"]],
        allowed=["character.ini", "portrait.png", "body.pdbody", "head.pdhead"],
        allowed_globs=[
            "dependencies/assets/body/*.pdbody",
            "dependencies/assets/bodies/*.pdbody",
            "dependencies/assets/head/*.pdhead",
            "dependencies/assets/heads/*.pdhead",
            "dependencies/assets/skins/*.pdskin",
            "dependencies/assets/voice/*.pdvoice",
            "dependencies/assets/animations/*.pdanim",
            "dependencies/assets/ui/*.pdui",
        ],
    ),
    ".pdhead": schema(
        required=["head.ini", "mesh.pdmesh"],
        allowed=["head.ini", "mesh.pdmesh"],
        allowed_globs=[
            "dependencies/assets/materials/*.pdmaterial",
            "dependencies/assets/textures/*.pdtexture",
            "dependencies/assets/animations/*.pdanim",
        ],
        forbidden=["model.gltf", "model.glb", "texture.png", "texture.tga"],
    ),
    ".pdbody": schema(
        required=["body.ini", "mesh.pdmesh"],
        allowed=["body.ini", "mesh.pdmesh", "hand.pdmesh"],
        allowed_globs=[
            "dependencies/assets/materials/*.pdmaterial",
            "dependencies/assets/textures/*.pdtexture",
            "dependencies/assets/animations/*.pdanim",
        ],
        forbidden=["model.gltf", "model.glb", "hand.gltf", "hand.glb", "texture.png", "texture.tga"],
    ),
    ".pdarena": schema(
        required=["arena.ini"],
        allowed=["arena.ini", "preview.png", "thumbnail.png"],
        allowed_globs=["dependencies/assets/scenarios/*.pdscenario"],
        forbidden=["geometry.obj", "arena.mtl", "arena_texture.png", "pads.ini", "setup.ini"],
        forbidden_globs=["scenario/*"],
    ),
    ".pdscenario": schema(
        required=[
            "scenario.ini",
            "portals.tsv",
            "pads.tsv",
            "spawns.tsv",
            "volumes.tsv",
            "navigation/waypoints.tsv",  # decoded waypoint graph source
            "navigation/waygroups.tsv",
            "navigation/covers.tsv",
            "navigation/paths.tsv",
            "objects.tsv",
            "setup.fields.tsv",
            "ai/ailists.tsv",
            "objectives.tsv",
            "navigation.ini",
            "level.graph.json",
        ],
        require_any=[["scene.glb", "scene.gltf"]],
        allowed=[
            "scenario.ini",
            "scene.glb",
            "scene.gltf",
            "collision.glb",
            "collision.obj",
            "portals.tsv",
            "pads.tsv",
            "spawns.tsv",
            "volumes.tsv",
            "navigation/waypoints.tsv",
            "navigation/waygroups.tsv",
            "navigation/covers.tsv",
            "navigation/paths.tsv",
            "objects.tsv",
            "setup.fields.tsv",
            "ai/ailists.tsv",
            "objectives.tsv",
            "navigation.ini",
            "level.graph.json",
            "mission.graph.json",
        ],
        forbidden=sorted(FORBIDDEN_SCENARIO_EXACT),
        forbidden_globs=sorted(FORBIDDEN_SCENARIO_GLOBS),
    ),
    ".pdmesh": schema(
        required=["mesh.ini"],
        require_any=[["model.gltf", "model.glb", "model.obj"]],
        allowed=[
            "mesh.ini",
            "model.gltf",
            "model.glb",
            "model.obj",
            "model.mtl",
            "model.nodes.tsv",
            "model.parts.tsv",
            "model.faces.tsv",
            "model.render.json",
            "export_version.txt",
        ],
        allowed_globs=[
            "dependencies/assets/materials/*.pdmaterial",
            "dependencies/assets/textures/*.pdtexture",
        ],
        forbidden=["model.render.tsv"],
    ),
    ".pdanim": schema(
        required=["animation.ini"],
        require_one_of=[
            [["animation.gltf"], ["animation.glb"], ["commands.json"]],
        ],
        allowed=["animation.ini", "animation.gltf", "animation.glb", "commands.json"],
    ),
    ".pdsfx": schema(
        required=["sound.ini", "sample.wav"],
        allowed=["sound.ini", "sample.wav", "sample.ogg", "sample.flac"],
    ),
    ".pdvoice": schema(
        required=["voice.ini", "sample.wav"],
        allowed=["voice.ini", "sample.wav", "subtitle.tsv"],
        allowed_globs=["locales/*.wav", "locales/*.ogg"],
    ),
    ".pdsong": schema(
        required=["music.ini"],
        require_one_of=[
            [["sequence.mid", "sequence.json"], ["track.wav"], ["track.ogg"], ["track.mp3"]],
        ],
        allowed=["music.ini", "sequence.mid", "sequence.json", "track.wav", "track.ogg", "track.mp3", "cues.json", "sections.json"],
    ),
    ".pdui": schema(
        required=["ui.ini"],
        require_any=[["texture.png", "texture.tga", "textures/*.png", "textures/*.tga"]],
        allowed=["ui.ini", "texture.png", "texture.tga", "layout.tsv", "layout.json", "nineslice.ini"],
        allowed_globs=["textures/*.png", "textures/*.tga"],
    ),
    ".pdfont": schema(
        required=["font.ini"],
        require_one_of=[
            [["font.ttf"], ["font.otf"], ["glyphs.pgm", "metrics.tsv", "kerning.tsv"]],
        ],
        allowed=["font.ini", "font.ttf", "font.otf", "glyphs.pgm", "metrics.tsv", "kerning.tsv"],
    ),
    ".pdlang": schema(
        required=["lang.ini", "strings.tsv"],
        allowed=["lang.ini", "strings.tsv"],
    ),
    ".pdskin": schema(
        required=["skin.ini"],
        require_any=[["skin.json", "texture.png", "texture.tga", "swatches.tsv"]],
        allowed=["skin.ini", "skin.json", "texture.png", "texture.tga", "swatches.tsv"],
        allowed_globs=[
            "dependencies/assets/material/*.pdmaterial",
            "dependencies/assets/materials/*.pdmaterial",
            "dependencies/assets/texture/*.pdtexture",
            "dependencies/assets/textures/*.pdtexture",
        ],
    ),
    ".pdeffect": schema(
        required=["effect.ini"],
        require_any=[["effect.graph.json", "timeline.json"]],
        allowed=["effect.ini", "effect.graph.json", "timeline.json"],
        allowed_globs=[
            "dependencies/assets/materials/*.pdmaterial",
            "dependencies/assets/textures/*.pdtexture",
            "dependencies/assets/audio/*.pdsfx",
        ],
    ),
    ".pdprop": schema(
        required=["prop.ini"],
        require_any=[["prop.json", "model.gltf", "model.glb", "model.obj", "mesh.pdmesh", "dependencies/assets/models/*.pdmesh"]],
        allowed=["prop.ini", "prop.json", "model.gltf", "model.glb", "model.obj", "behavior.graph.json", "mesh.pdmesh"],
        allowed_globs=[
            "dependencies/assets/models/*.pdmesh",
            "dependencies/assets/materials/*.pdmaterial",
            "dependencies/assets/textures/*.pdtexture",
            "dependencies/assets/effects/*.pdeffect",
        ],
    ),
    ".pdvehicle": schema(
        required=["vehicle.ini", "physics.json"],
        require_any=[["behavior.graph.json", "model.gltf", "model.glb", "model.obj", "mesh.pdmesh", "dependencies/assets/models/*.pdmesh"]],
        allowed=["vehicle.ini", "physics.json", "model.gltf", "model.glb", "model.obj", "behavior.graph.json", "mesh.pdmesh"],
        allowed_globs=[
            "dependencies/assets/models/*.pdmesh",
            "dependencies/assets/materials/*.pdmaterial",
            "dependencies/assets/textures/*.pdtexture",
            "dependencies/assets/audio/*.pdsfx",
            "dependencies/assets/effects/*.pdeffect",
            "dependencies/assets/weapons/*.pdweapon",
        ],
    ),
    ".pdmission": schema(
        required=["mission.ini", "mission.graph.json", "objectives.tsv", "briefing.tsv"],
        require_any=[["dependencies/assets/scenario/*.pdscenario", "dependencies/assets/scenarios/*.pdscenario"]],
        allowed=["mission.ini", "mission.graph.json", "objectives.tsv", "briefing.tsv"],
        allowed_globs=[
            "dependencies/assets/scenario/*.pdscenario",
            "dependencies/assets/scenarios/*.pdscenario",
            "dependencies/assets/audio/*.pdsfx",
            "dependencies/assets/voice/*.pdvoice",
        ],
    ),
    ".pdgamemode": schema(
        required=["gamemode.ini", "rules.json"],
        allowed=["gamemode.ini", "rules.json"],
        allowed_globs=["dependencies/assets/hud/*.pdhud", "dependencies/assets/audio/*.pdsfx"],
    ),
    ".pdbotprofile": schema(
        required=["botprofile.ini", "profile.json"],
        allowed=["botprofile.ini", "profile.json"],
    ),
    ".pdhud": schema(
        required=["hud.ini", "layout.json"],
        allowed=["hud.ini", "layout.json", "texture.png", "texture.tga"],
        allowed_globs=[
            "dependencies/assets/ui/*.pdui",
            "dependencies/assets/fonts/*.pdfont",
            "dependencies/assets/lang/*.pdlang",
            "dependencies/assets/audio/*.pdsfx",
        ],
    ),
    ".pdtheme": schema(
        required=["theme.ini", "theme.json"],
        allowed=["theme.ini", "theme.json"],
        allowed_globs=[
            "dependencies/assets/ui/*.pdui",
            "dependencies/assets/font/*.pdfont",
            "dependencies/assets/fonts/*.pdfont",
            "dependencies/assets/audio/*.pdsfx",
            "dependencies/assets/music/*.pdsong",
            "dependencies/assets/effects/*.pdeffect",
        ],
    ),
}


DEPENDENCY_LOADER = (
    "catalog/provider opens the nested typed archive and resolves references by catalog ID"
)
DEPENDENCY_ABSENT = (
    "asset must either not use that dependency class or declare an explicit base fallback"
)

OPTIONAL_PUBLIC_SLOT_CONTRACT: dict[str, dict[str, SlotJustification]] = {
    ".pdweapon": {
        "bindings/animations.tsv": slot(
            "animation binding table",
            "weapon graph importer and Modding Hub",
            "maps graph animation requests to catalog animation dependencies",
            "weapon uses graph/default presentation timings",
        ),
        "bindings/audio.tsv": slot(
            "audio binding table",
            "weapon graph importer and audio adapter",
            "maps graph audio events to catalog sound dependencies",
            "weapon uses graph/default audio bindings",
        ),
        "bindings/material-slots.json": slot(
            "weapon material slot names",
            "weapon renderer adapter and Modding Hub",
            "binds mesh material slots to typed material dependencies",
            "renderer uses material declarations embedded in the mesh dependency",
        ),
        "bindings/grip-sockets.json": slot(
            "held-model grip and muzzle sockets",
            "weapon presentation adapter",
            "binds held model sockets to runtime hand and effect attachment points",
            "presentation adapter falls back to mesh default sockets",
        ),
        "bindings/presentation.json": slot(
            "reticle, zoom, and presentation bindings",
            "weapon presentation adapter",
            "loads view/HUD presentation values from authored source",
            "weapon uses graph/default presentation values",
        ),
        "dependencies/assets/models/*.pdmesh": slot(
            "held and world model dependencies",
            "weapon catalog importer",
            DEPENDENCY_LOADER,
            DEPENDENCY_ABSENT,
        ),
        "dependencies/assets/materials/*.pdmaterial": slot(
            "weapon material dependencies",
            "weapon catalog importer",
            DEPENDENCY_LOADER,
            DEPENDENCY_ABSENT,
        ),
        "dependencies/assets/textures/*.pdtexture": slot(
            "weapon texture dependencies",
            "weapon catalog importer",
            DEPENDENCY_LOADER,
            DEPENDENCY_ABSENT,
        ),
        "dependencies/assets/animations/*.pdanim": slot(
            "weapon animation dependencies",
            "weapon graph importer",
            DEPENDENCY_LOADER,
            DEPENDENCY_ABSENT,
        ),
        "dependencies/assets/audio/*.pdsfx": slot(
            "weapon sound dependencies",
            "weapon graph importer",
            DEPENDENCY_LOADER,
            DEPENDENCY_ABSENT,
        ),
        "dependencies/assets/projectiles/*.pdprojectile": slot(
            "spawned projectile dependencies",
            "weapon graph importer",
            DEPENDENCY_LOADER,
            "graph must not spawn a projectile unless one is embedded or declared as a base fallback",
        ),
        "dependencies/assets/entities/*.pdentity": slot(
            "deployed entity dependencies",
            "weapon graph importer",
            DEPENDENCY_LOADER,
            "graph must not deploy an entity unless one is embedded or declared as a base fallback",
        ),
        "dependencies/assets/ui/*.pdui": slot(
            "weapon UI dependency",
            "weapon presentation adapter",
            DEPENDENCY_LOADER,
            "weapon uses default HUD/presentation if no UI dependency is declared",
        ),
    },
    ".pdprojectile": {
        "dependencies/assets/models/*.pdmesh": slot("projectile model dependencies", "projectile graph importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/materials/*.pdmaterial": slot("projectile material dependencies", "projectile graph importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/textures/*.pdtexture": slot("projectile texture dependencies", "projectile graph importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/audio/*.pdsfx": slot("projectile sound dependencies", "projectile graph importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/effects/*.pdeffect": slot("projectile effect dependencies", "projectile graph importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/entities/*.pdentity": slot("projectile-spawned entity dependencies", "projectile graph importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
    },
    ".pdentity": {
        "composition.json": slot(
            "entity composition source",
            "entity graph importer",
            "loads child prop/model/effect composition from editable JSON",
            "entity is behavior-only when composition is absent",
        ),
        "dependencies/assets/props/*.pdprop": slot("entity prop dependencies", "entity graph importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/models/*.pdmesh": slot("entity model dependencies", "entity graph importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/materials/*.pdmaterial": slot("entity material dependencies", "entity graph importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/textures/*.pdtexture": slot("entity texture dependencies", "entity graph importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/audio/*.pdsfx": slot("entity sound dependencies", "entity graph importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/effects/*.pdeffect": slot("entity effect dependencies", "entity graph importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
    },
    ".pdmaterial": {
        "material.json": slot("inline material source", "material importer", "loads material constants as runtime material source", "material must embed typed texture dependencies"),
        "dependencies/assets/texture/*.pdtexture": slot("legacy-singular texture dependency path", "material importer", DEPENDENCY_LOADER, "material must use inline constant values or the plural texture dependency path"),
        "dependencies/assets/textures/*.pdtexture": slot("material texture dependencies", "material importer", DEPENDENCY_LOADER, "material must use inline constant values or the singular compatibility path"),
        "dependencies/assets/effects/*.pdeffect": slot("material effect dependencies", "material importer", DEPENDENCY_LOADER, "material has no animated/effect layer"),
    },
    ".pdtexture": {
        "texture.png": slot("PNG source image", "texture importer", "decodes standard image source into runtime texture cache", "one of the other image source slots must be present"),
        "texture.tga": slot("TGA source image", "texture importer", "decodes standard image source into runtime texture cache", "one of the other image source slots must be present"),
        "texture.jpg": slot("JPEG source image", "texture importer", "decodes standard image source into runtime texture cache", "one of the other image source slots must be present"),
        "texture.jpeg": slot("JPEG source image", "texture importer", "decodes standard image source into runtime texture cache", "one of the other image source slots must be present"),
    },
    ".pdcharacter": {
        "portrait.png": slot("character portrait source", "character/UI importer", "loads portrait for menus and character pickers", "menus use generated/default portrait"),
        "body.pdbody": slot("inline body dependency", "character importer", DEPENDENCY_LOADER, "body must be supplied through dependencies/assets/body or dependencies/assets/bodies"),
        "head.pdhead": slot("inline head dependency", "character importer", DEPENDENCY_LOADER, "head must be supplied through dependencies/assets/head or dependencies/assets/heads"),
        "dependencies/assets/body/*.pdbody": slot("singular body dependency path", "character importer", DEPENDENCY_LOADER, "body.pdbody or plural bodies path must be present"),
        "dependencies/assets/bodies/*.pdbody": slot("body dependency path", "character importer", DEPENDENCY_LOADER, "body.pdbody or singular body path must be present"),
        "dependencies/assets/head/*.pdhead": slot("singular head dependency path", "character importer", DEPENDENCY_LOADER, "head.pdhead or plural heads path must be present"),
        "dependencies/assets/heads/*.pdhead": slot("head dependency path", "character importer", DEPENDENCY_LOADER, "head.pdhead or singular head path must be present"),
        "dependencies/assets/skins/*.pdskin": slot("skin dependencies", "character importer", DEPENDENCY_LOADER, "character uses body/head default materials"),
        "dependencies/assets/voice/*.pdvoice": slot("voice dependencies", "character importer", DEPENDENCY_LOADER, "character uses default/no voice set"),
        "dependencies/assets/animations/*.pdanim": slot("character animation dependencies", "character importer", DEPENDENCY_LOADER, "character uses shared animation set"),
        "dependencies/assets/ui/*.pdui": slot("character UI dependencies", "character importer", DEPENDENCY_LOADER, "menus use default UI treatment"),
    },
    ".pdhead": {
        "dependencies/assets/materials/*.pdmaterial": slot("head material dependencies", "head importer", DEPENDENCY_LOADER, "head mesh material declarations are used"),
        "dependencies/assets/textures/*.pdtexture": slot("head texture dependencies", "head importer", DEPENDENCY_LOADER, "head mesh texture declarations are used"),
        "dependencies/assets/animations/*.pdanim": slot("head animation dependencies", "head importer", DEPENDENCY_LOADER, "head uses shared head/face animation set"),
    },
    ".pdbody": {
        "hand.pdmesh": slot("first-person hand mesh dependency", "body importer", DEPENDENCY_LOADER, "body uses mesh.pdmesh for both body and hand fallback"),
        "dependencies/assets/materials/*.pdmaterial": slot("body material dependencies", "body importer", DEPENDENCY_LOADER, "body mesh material declarations are used"),
        "dependencies/assets/textures/*.pdtexture": slot("body texture dependencies", "body importer", DEPENDENCY_LOADER, "body mesh texture declarations are used"),
        "dependencies/assets/animations/*.pdanim": slot("body animation dependencies", "body importer", DEPENDENCY_LOADER, "body uses shared animation set"),
    },
    ".pdarena": {
        "preview.png": slot("arena preview image", "arena UI importer", "loads menu preview art", "menu renders a generated/default preview"),
        "thumbnail.png": slot("arena thumbnail image", "arena UI importer", "loads compact selector art", "menu renders a generated/default thumbnail"),
        "dependencies/assets/scenarios/*.pdscenario": slot("playable scenario dependency", "arena importer", DEPENDENCY_LOADER, "only explicit Random selector arenas may omit it"),
    },
    ".pdscenario": {
        "scene.glb": slot("binary DCC-openable scene source", "scenario importer", "loads level mesh/materials as runtime source and cache seed", "scene.gltf must be present"),
        "scene.gltf": slot("text DCC-openable scene source", "scenario importer", "loads level mesh/materials as runtime source and cache seed", "scene.glb must be present"),
        "collision.glb": slot("binary collision override source", "scenario collision importer", "uses explicit collision mesh instead of deriving collision from scene source", "collision is deterministically generated from the scene source"),
        "collision.obj": slot("OBJ collision override source", "scenario collision importer", "uses explicit collision mesh instead of deriving collision from scene source", "collision is deterministically generated from the scene source"),
        "mission.graph.json": slot("scenario-local mission behavior graph", "mission graph importer", "loads mission/script graph when the scenario carries its own mission logic", "mission behavior comes from a linked .pdmission or base fallback"),
    },
    ".pdmesh": {
        "model.gltf": slot("text mesh source", "mesh importer", "loads geometry/UV/material declarations as runtime source and cache seed", "model.glb or model.obj must be present"),
        "model.glb": slot("binary mesh source", "mesh importer", "loads geometry/UV/material declarations as runtime source and cache seed", "model.gltf or model.obj must be present"),
        "model.obj": slot("OBJ mesh source", "mesh importer", "loads geometry/UV/material declarations as runtime source and cache seed", "model.gltf or model.glb must be present"),
        "model.mtl": slot("OBJ material companion", "mesh importer", "loads material names for model.obj import", "OBJ imports use default material values"),
        "model.nodes.tsv": slot("model hierarchy source", "mesh importer", "loads original node/matrix hierarchy for runtime modeldef reconstruction", "author-authored flat mesh sources may omit hierarchy metadata"),
        "model.parts.tsv": slot("model part-table source", "mesh importer", "loads original part-to-node lookup table for runtime modeldef reconstruction", "required when hierarchy metadata is present"),
        "model.faces.tsv": slot("model face-matrix source", "mesh importer", "loads original face-to-matrix bindings for runtime display-list reconstruction", "required when hierarchy metadata is present"),
        "model.render.json": slot("semantic render command source", "mesh importer", "preserves original matrix, material, and triangle command order for runtime modeldef reconstruction", "author-authored flat mesh sources may omit original render commands"),
        "export_version.txt": slot("exporter provenance marker", "mesh extractor", "records source exporter revision for stale-cache detection", "validator treats archive as source-authored without exporter provenance"),
        "dependencies/assets/materials/*.pdmaterial": slot("mesh material dependencies", "mesh importer", DEPENDENCY_LOADER, "mesh uses material declarations inside the model source"),
        "dependencies/assets/textures/*.pdtexture": slot("mesh texture dependencies", "mesh importer", DEPENDENCY_LOADER, "mesh uses embedded/material-declared textures inside the model source"),
    },
    ".pdanim": {
        "animation.gltf": slot("GLTF animation source", "animation importer", "loads animation curves as runtime source and cache seed", "another animation source slot must be present"),
        "animation.glb": slot("GLB animation source", "animation importer", "loads animation curves as runtime source and cache seed", "another animation source slot must be present"),
        "commands.json": slot("semantic weapon animation command source", "animation importer", "builds native weapon animation commands from editable source", "another animation source slot must be present"),
    },
    ".pdsfx": {
        "sample.ogg": slot("compressed sound source", "audio importer", "decodes standard audio into runtime sound cache", "sample.wav is authoritative"),
        "sample.flac": slot("lossless sound source", "audio importer", "decodes standard audio into runtime sound cache", "sample.wav is authoritative"),
    },
    ".pdvoice": {
        "subtitle.tsv": slot("voice subtitle/source text", "voice importer", "loads subtitle strings for UI and localization", "voice line has no authored subtitle"),
        "locales/*.wav": slot("localized WAV voice samples", "voice importer", "loads locale-specific voice source", "default sample.wav is used"),
        "locales/*.ogg": slot("localized OGG voice samples", "voice importer", "loads locale-specific voice source", "default sample.wav is used"),
    },
    ".pdsong": {
        "sequence.mid": slot("MIDI song source", "music importer", "loads authored sequence source", "track audio or sequence.json must supply playback source"),
        "sequence.json": slot("semantic sequence event source", "music importer", "loads editable named sequence events", "track audio or sequence.mid must supply playback source"),
        "track.wav": slot("WAV song source", "music importer", "loads direct audio track source", "sequence source or another track format must be present"),
        "track.ogg": slot("OGG song source", "music importer", "loads direct audio track source", "sequence source or another track format must be present"),
        "track.mp3": slot("MP3 song source", "music importer", "loads direct audio track source", "sequence source or another track format must be present"),
        "cues.json": slot("music cue source", "music importer", "loads authored cue points", "song has no authored cue source"),
        "sections.json": slot("music section source", "music importer", "loads authored section loop/transition data", "song has no authored section source"),
    },
    ".pdui": {
        "texture.png": slot("UI PNG source image", "UI importer", "loads standard image source into UI texture cache", "another UI texture source slot must be present"),
        "texture.tga": slot("UI TGA source image", "UI importer", "loads standard image source into UI texture cache", "another UI texture source slot must be present"),
        "layout.tsv": slot("UI layout table", "UI importer", "loads readable layout data", "UI asset is texture-only or uses layout.json"),
        "layout.json": slot("UI layout JSON", "UI importer", "loads structured layout data", "UI asset is texture-only or uses layout.tsv"),
        "nineslice.ini": slot("nine-slice UI metadata", "UI importer", "loads scaling/inset metadata", "UI asset is not nine-sliced"),
        "textures/*.png": slot("multi-part UI PNG texture slots", "UI importer", "loads named texture slots", "root texture source must be present"),
        "textures/*.tga": slot("multi-part UI TGA texture slots", "UI importer", "loads named texture slots", "root texture source must be present"),
    },
    ".pdfont": {
        "font.ttf": slot("TrueType font source", "font importer", "loads standard font source", "font.otf or glyph table source must be present"),
        "font.otf": slot("OpenType font source", "font importer", "loads standard font source", "font.ttf or glyph table source must be present"),
        "glyphs.pgm": slot("bitmap glyph atlas source", "font importer", "loads bitmap glyph source", "vector font source must be present"),
        "metrics.tsv": slot("font metrics table", "font importer", "loads bitmap font metrics", "vector font metrics are read from font source"),
        "kerning.tsv": slot("font kerning table", "font importer", "loads bitmap font kerning", "vector font kerning is read from font source"),
    },
    ".pdskin": {
        "skin.json": slot("skin material-binding source", "skin importer", "loads target material slots and color bindings", "skin must supply texture source, swatches, or typed dependencies"),
        "texture.png": slot("skin PNG source image", "skin importer", "loads standard image source into skin material binding", "texture dependency or another skin source slot must be present"),
        "texture.tga": slot("skin TGA source image", "skin importer", "loads standard image source into skin material binding", "texture dependency or another skin source slot must be present"),
        "swatches.tsv": slot("skin color swatch table", "skin importer", "loads editable color variants", "skin has no authored swatches"),
        "dependencies/assets/material/*.pdmaterial": slot("singular material dependency path", "skin importer", DEPENDENCY_LOADER, "skin uses plural material path, texture path, or inline source"),
        "dependencies/assets/materials/*.pdmaterial": slot("material dependencies", "skin importer", DEPENDENCY_LOADER, "skin uses singular material path, texture path, or inline source"),
        "dependencies/assets/texture/*.pdtexture": slot("singular texture dependency path", "skin importer", DEPENDENCY_LOADER, "skin uses plural texture path, material path, or inline source"),
        "dependencies/assets/textures/*.pdtexture": slot("texture dependencies", "skin importer", DEPENDENCY_LOADER, "skin uses singular texture path, material path, or inline source"),
    },
    ".pdeffect": {
        "effect.graph.json": slot("effect behavior graph", "effect importer", "loads effect graph source", "timeline.json must be present"),
        "timeline.json": slot("effect timeline source", "effect importer", "loads effect timeline source", "effect.graph.json must be present"),
        "dependencies/assets/materials/*.pdmaterial": slot("effect material dependencies", "effect importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/textures/*.pdtexture": slot("effect texture dependencies", "effect importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/audio/*.pdsfx": slot("effect sound dependencies", "effect importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
    },
    ".pdprop": {
        "prop.json": slot("prop archetype source", "prop importer", "loads prop category/archetype behavior source", "concrete visual props must supply a model source or typed model dependency"),
        "model.gltf": slot("prop GLTF model source", "prop importer", "loads prop model source directly", "mesh.pdmesh or another model source must be present"),
        "model.glb": slot("prop GLB model source", "prop importer", "loads prop model source directly", "mesh.pdmesh or another model source must be present"),
        "model.obj": slot("prop OBJ model source", "prop importer", "loads prop model source directly", "mesh.pdmesh or another model source must be present"),
        "behavior.graph.json": slot("prop behavior graph", "prop importer", "loads prop behavior source", "prop uses descriptor-only/default behavior"),
        "mesh.pdmesh": slot("inline prop mesh dependency", "prop importer", DEPENDENCY_LOADER, "model source or dependencies/assets/models path must be present"),
        "dependencies/assets/models/*.pdmesh": slot("prop model dependencies", "prop importer", DEPENDENCY_LOADER, "model source or mesh.pdmesh must be present"),
        "dependencies/assets/materials/*.pdmaterial": slot("prop material dependencies", "prop importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/textures/*.pdtexture": slot("prop texture dependencies", "prop importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/effects/*.pdeffect": slot("prop effect dependencies", "prop importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
    },
    ".pdvehicle": {
        "model.gltf": slot("vehicle GLTF model source", "vehicle importer", "loads vehicle model source directly", "mesh.pdmesh or another model source must be present"),
        "model.glb": slot("vehicle GLB model source", "vehicle importer", "loads vehicle model source directly", "mesh.pdmesh or another model source must be present"),
        "model.obj": slot("vehicle OBJ model source", "vehicle importer", "loads vehicle model source directly", "mesh.pdmesh or another model source must be present"),
        "behavior.graph.json": slot("vehicle behavior graph", "vehicle importer", "loads vehicle behavior source", "vehicle uses descriptor/default behavior"),
        "mesh.pdmesh": slot("inline vehicle mesh dependency", "vehicle importer", DEPENDENCY_LOADER, "model source or dependencies/assets/models path must be present"),
        "dependencies/assets/models/*.pdmesh": slot("vehicle model dependencies", "vehicle importer", DEPENDENCY_LOADER, "model source or mesh.pdmesh must be present"),
        "dependencies/assets/materials/*.pdmaterial": slot("vehicle material dependencies", "vehicle importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/textures/*.pdtexture": slot("vehicle texture dependencies", "vehicle importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/audio/*.pdsfx": slot("vehicle sound dependencies", "vehicle importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/effects/*.pdeffect": slot("vehicle effect dependencies", "vehicle importer", DEPENDENCY_LOADER, DEPENDENCY_ABSENT),
        "dependencies/assets/weapons/*.pdweapon": slot("vehicle weapon dependencies", "vehicle importer", DEPENDENCY_LOADER, "vehicle has no mounted weapon dependency"),
    },
    ".pdmission": {
        "dependencies/assets/scenario/*.pdscenario": slot("singular scenario dependency path", "mission importer", DEPENDENCY_LOADER, "plural scenario dependency path must be present"),
        "dependencies/assets/scenarios/*.pdscenario": slot("scenario dependency path", "mission importer", DEPENDENCY_LOADER, "singular scenario dependency path must be present"),
        "dependencies/assets/audio/*.pdsfx": slot("mission sound dependencies", "mission importer", DEPENDENCY_LOADER, "mission graph has no private sound dependencies"),
        "dependencies/assets/voice/*.pdvoice": slot("mission voice dependencies", "mission importer", DEPENDENCY_LOADER, "mission graph has no private voice dependencies"),
    },
    ".pdgamemode": {
        "dependencies/assets/hud/*.pdhud": slot("gamemode HUD dependency", "gamemode importer", DEPENDENCY_LOADER, "gamemode uses default HUD"),
        "dependencies/assets/audio/*.pdsfx": slot("gamemode sound dependencies", "gamemode importer", DEPENDENCY_LOADER, "gamemode uses default/no custom sounds"),
    },
    ".pdhud": {
        "texture.png": slot("HUD PNG source image", "HUD importer", "loads standard image source into HUD texture cache", "layout.json drives engine-rendered HUD composition"),
        "texture.tga": slot("HUD TGA source image", "HUD importer", "loads standard image source into HUD texture cache", "layout.json drives engine-rendered HUD composition"),
        "dependencies/assets/ui/*.pdui": slot("HUD UI dependencies", "HUD importer", DEPENDENCY_LOADER, "layout.json uses engine-rendered/base UI primitives"),
        "dependencies/assets/fonts/*.pdfont": slot("HUD font dependencies", "HUD importer", DEPENDENCY_LOADER, "HUD uses default font"),
        "dependencies/assets/lang/*.pdlang": slot("HUD language dependencies", "HUD importer", DEPENDENCY_LOADER, "HUD uses base language strings"),
        "dependencies/assets/audio/*.pdsfx": slot("HUD sound dependencies", "HUD importer", DEPENDENCY_LOADER, "HUD has no private sounds"),
    },
    ".pdtheme": {
        "dependencies/assets/ui/*.pdui": slot("theme UI dependencies", "theme importer", DEPENDENCY_LOADER, "theme.json uses palette/style tokens only"),
        "dependencies/assets/font/*.pdfont": slot("singular theme font dependency path", "theme importer", DEPENDENCY_LOADER, "theme.json uses the active/base font"),
        "dependencies/assets/fonts/*.pdfont": slot("theme font dependencies", "theme importer", DEPENDENCY_LOADER, "theme.json uses the active/base font"),
        "dependencies/assets/audio/*.pdsfx": slot("theme sound dependencies", "theme importer", DEPENDENCY_LOADER, "theme has no private sounds"),
        "dependencies/assets/music/*.pdsong": slot("theme music dependencies", "theme importer", DEPENDENCY_LOADER, "theme has no private music"),
        "dependencies/assets/effects/*.pdeffect": slot("theme effect dependencies", "theme importer", DEPENDENCY_LOADER, "theme has no private effects"),
    },
}

COMMON_META_SLOT_CONTRACT: dict[str, SlotJustification] = {
    "_meta/manifest.json": slot(
        "archive identity and dependency manifest",
        "asset_archive_writer and catalog scanner",
        "loaded before catalog registration and dependency validation",
        "invalid for release archives",
    ),
    "_meta/inventory.json": slot(
        "machine inventory of archive entries",
        "asset_archive_writer",
        "used by validation and diagnostics, never as authored source",
        "diagnostics reconstruct inventory from the zip entries",
    ),
    "_meta/provenance.json": slot(
        "extraction/source provenance",
        "asset_archive_writer",
        "used for stale-cache diagnostics and audit trails",
        "archive remains source-valid but has less provenance detail",
    ),
    "_meta/validation.json": slot(
        "last validation summary",
        "asset_archive_writer and validators",
        "used only for diagnostics and release checks",
        "validators recompute current status",
    ),
    "_meta/source-handles.json": slot(
        "private source-handle mapping",
        "asset_archive_writer and catalog provider",
        "keeps legacy ROM/source handles private while public identity stays catalog IDs",
        "archive source remains valid without legacy handle mapping",
    ),
    "_meta/hashes.tsv": slot(
        "public-entry hash index",
        "asset_archive_writer",
        "used for cache invalidation and duplicate detection",
        "hashes are recomputed from public entries",
    ),
}

META_SLOT_CONTRACT: dict[str, dict[str, SlotJustification]] = {
    ".pdscenario": {
        "_meta/generated-collision.json": slot(
            "generated collision-cache manifest",
            "scenario collision importer",
            "records deterministic cache inputs for collision generated from source",
            "collision cache is regenerated from scene/collision source",
        ),
        "_meta/generated-navmesh.json": slot(
            "generated navmesh-cache manifest",
            "scenario navigation importer",
            "records deterministic cache inputs for generated bot navigation",
            "navmesh cache is regenerated from scene/collision/volume source",
        ),
    },
    ".pdweapon": {
        "_meta/nested-payloads.json": slot(
            "private dependency-closure inventory",
            "weapon extractor and weapon archive validator",
            "validates embedded projectile/entity closure without becoming public source",
            "dependency closure is derived from behavior graphs and dependencies/assets",
        ),
    },
}

META_SOURCE_HASH_SIDECAR_CONTRACT = slot(
    "source entry hash sidecar",
    "asset_archive_writer",
    "ties a public source entry to cache invalidation and duplicate detection",
    "hash is recomputed from the matching public source entry",
)


@dataclass
class ConformanceResult:
    checked_archives: int = 0
    root_archives: int = 0
    families: set[str] = field(default_factory=set)
    errors: list[str] = field(default_factory=list)

    def extend(self, other: "ConformanceResult") -> None:
        self.checked_archives += other.checked_archives
        self.root_archives += other.root_archives
        self.families.update(other.families)
        self.errors.extend(other.errors)


def normalize_name(name: str) -> str:
    return name.replace("\\", "/")


def public_names(names: list[str]) -> list[str]:
    return [
        normalize_name(name)
        for name in names
        if name and not normalize_name(name).startswith("_meta/")
    ]


def is_meta_name(name: str) -> bool:
    return normalize_name(name).startswith("_meta/")


def matches_any(name: str, patterns: tuple[str, ...] | set[str]) -> bool:
    return any(fnmatch.fnmatchcase(name, pattern) for pattern in patterns)


def matches_any_from_names(names: set[str], pattern: str) -> bool:
    return any(fnmatch.fnmatchcase(name, pattern) for name in names)


def archive_has_pattern(names: set[str], pattern: str) -> bool:
    return pattern in names or matches_any_from_names(names, pattern)


def archive_has_all(names: set[str], patterns: tuple[str, ...]) -> bool:
    return all(archive_has_pattern(names, pattern) for pattern in patterns)


def validate_slot_justification(label: str, item: SlotJustification,
                                errors: list[str]) -> None:
    if item.status not in {"final", "transition"}:
        errors.append(f"{label} has invalid status {item.status}")
    if not item.semantic_role:
        errors.append(f"{label} is missing semantic_role")
    if not item.owner:
        errors.append(f"{label} is missing owner")
    if not item.loader_behavior:
        errors.append(f"{label} is missing loader_behavior")
    if not item.absence_behavior:
        errors.append(f"{label} is missing absence_behavior")


def validate_schema_definitions() -> list[str]:
    errors: list[str] = []
    for ext, spec in SCHEMAS.items():
        contract = OPTIONAL_PUBLIC_SLOT_CONTRACT.get(ext, {})
        optional_exact = set(spec.allowed) - set(spec.required)
        documented = set(contract)
        legal_documented = optional_exact | set(spec.allowed_globs)

        for name in sorted(optional_exact):
            if name not in contract:
                errors.append(f"{ext} optional public slot lacks contract: {name}")
        for pattern in spec.allowed_globs:
            if pattern not in contract:
                errors.append(f"{ext} optional public glob lacks contract: {pattern}")
        for name in sorted(documented - legal_documented):
            errors.append(f"{ext} documents non-schema optional slot: {name}")
        for name, item in contract.items():
            validate_slot_justification(f"{ext} optional slot {name}", item, errors)

    for name, item in COMMON_META_SLOT_CONTRACT.items():
        validate_slot_justification(f"common meta slot {name}", item, errors)
    for ext, contract in META_SLOT_CONTRACT.items():
        if ext not in SCHEMAS:
            errors.append(f"{ext} has meta-slot contract but no schema")
        for name, item in contract.items():
            validate_slot_justification(f"{ext} meta slot {name}", item, errors)
    validate_slot_justification("common meta source entry hash sidecar",
                                META_SOURCE_HASH_SIDECAR_CONTRACT, errors)
    return errors


def public_entry_allowed(name: str, spec: Schema) -> bool:
    if name in spec.allowed:
        return True
    if matches_any(name, spec.allowed_globs):
        return True
    return False


def public_entry_has_contract(name: str, ext: str, spec: Schema) -> bool:
    if name in spec.required:
        return True
    contract = OPTIONAL_PUBLIC_SLOT_CONTRACT.get(ext, {})
    if name in spec.allowed:
        return name in contract
    for pattern in spec.allowed_globs:
        if fnmatch.fnmatchcase(name, pattern):
            return pattern in contract
    return False


def _glb_json_and_bin(data: bytes) -> tuple[dict[str, object], bytes]:
    if len(data) < 20 or data[:4] != b"glTF":
        raise ValueError("not a glTF binary")
    version, total_length = struct.unpack_from("<II", data, 4)
    if version != 2:
        raise ValueError(f"unsupported glTF binary version {version}")
    if total_length != len(data):
        raise ValueError("glTF binary length header does not match archive entry")

    offset = 12
    gltf_json: dict[str, object] | None = None
    bin_chunk = b""

    while offset + 8 <= len(data):
        chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk = data[offset:offset + chunk_length]
        offset += chunk_length

        if chunk_type == 0x4e4f534a:
            gltf_json = json.loads(chunk.decode("utf-8").rstrip("\0 "))
        elif chunk_type == 0x004e4942:
            bin_chunk = chunk

    if gltf_json is None:
        raise ValueError("missing glTF JSON chunk")
    return gltf_json, bin_chunk


def _gltf_accessor_float2_values(gltf_json: dict[str, object], bin_chunk: bytes,
                                 accessor_index: int) -> list[tuple[float, float]]:
    accessors = gltf_json.get("accessors")
    buffer_views = gltf_json.get("bufferViews")
    if not isinstance(accessors, list) or not isinstance(buffer_views, list):
        raise ValueError("missing glTF accessors or bufferViews")
    if accessor_index < 0 or accessor_index >= len(accessors):
        raise ValueError(f"TEXCOORD_0 accessor index {accessor_index} out of range")

    accessor = accessors[accessor_index]
    if not isinstance(accessor, dict):
        raise ValueError(f"TEXCOORD_0 accessor {accessor_index} is not an object")
    if accessor.get("componentType") != 5126 or accessor.get("type") != "VEC2":
        raise ValueError(
            f"TEXCOORD_0 accessor {accessor_index} must be FLOAT VEC2"
        )

    view_index = accessor.get("bufferView")
    if not isinstance(view_index, int) or view_index < 0 or view_index >= len(buffer_views):
        raise ValueError(f"TEXCOORD_0 accessor {accessor_index} has invalid bufferView")
    buffer_view = buffer_views[view_index]
    if not isinstance(buffer_view, dict):
        raise ValueError(f"TEXCOORD_0 bufferView {view_index} is not an object")
    if buffer_view.get("buffer", 0) != 0:
        raise ValueError(f"TEXCOORD_0 bufferView {view_index} must use GLB buffer 0")

    count = accessor.get("count")
    if not isinstance(count, int) or count < 0:
        raise ValueError(f"TEXCOORD_0 accessor {accessor_index} has invalid count")

    stride = buffer_view.get("byteStride", 8)
    if not isinstance(stride, int) or stride < 8:
        raise ValueError(f"TEXCOORD_0 bufferView {view_index} has invalid stride")

    start = int(buffer_view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
    values: list[tuple[float, float]] = []

    for i in range(count):
        pos = start + i * stride
        if pos + 8 > len(bin_chunk):
            raise ValueError(f"TEXCOORD_0 accessor {accessor_index} overruns BIN chunk")
        values.append(struct.unpack_from("<ff", bin_chunk, pos))

    return values


def validate_scene_glb_texture_contract(label: str, data: bytes,
                                        errors: list[str]) -> None:
    """Catch stale or malformed scene.glb UV exports before DCC visual QA."""
    try:
        gltf_json, bin_chunk = _glb_json_and_bin(data)
    except (ValueError, json.JSONDecodeError, struct.error) as exc:
        errors.append(f"{label} scene.glb is not a valid glTF 2.0 binary: {exc}")
        return

    asset = gltf_json.get("asset")
    generator = asset.get("generator", "") if isinstance(asset, dict) else ""
    if (isinstance(generator, str)
            and generator.startswith("Perfect Dark 2 PDSCENARIO scene.glb exporter")
            and "bg_visual_scene_glb_v9_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0" not in generator):
        errors.append(
            f"{label} scene.glb uses stale scenario GLB exporter stamp {generator!r}"
        )
    generated_pd_scenario = (
        isinstance(generator, str)
        and generator.startswith("Perfect Dark 2 PDSCENARIO scene.glb exporter")
    )

    texcoord_accessors: set[int] = set()
    runtime_texcoord_accessors: set[int] = set()
    color_accessors: set[int] = set()
    meshes = gltf_json.get("meshes", [])
    if isinstance(meshes, list):
        for mesh in meshes:
            if not isinstance(mesh, dict):
                continue
            primitives = mesh.get("primitives", [])
            if not isinstance(primitives, list):
                continue
            for primitive in primitives:
                if not isinstance(primitive, dict):
                    continue
                attributes = primitive.get("attributes", {})
                if not isinstance(attributes, dict):
                    continue
                texcoord = attributes.get("TEXCOORD_0")
                if isinstance(texcoord, int):
                    texcoord_accessors.add(texcoord)
                runtime_texcoord = attributes.get("TEXCOORD_1")
                if isinstance(runtime_texcoord, int):
                    runtime_texcoord_accessors.add(runtime_texcoord)
                color = attributes.get("COLOR_0")
                if isinstance(color, int):
                    color_accessors.add(color)

    if not texcoord_accessors:
        return

    min_uv = 0.0
    max_uv = 0.0
    max_abs_uv = 0.0
    saw_uv = False
    for accessor_index in sorted(texcoord_accessors):
        try:
            values = _gltf_accessor_float2_values(gltf_json, bin_chunk, accessor_index)
        except (ValueError, struct.error) as exc:
            errors.append(f"{label} scene.glb has invalid TEXCOORD_0 data: {exc}")
            continue
        for u, v in values:
            if not saw_uv:
                min_uv = min(u, v)
                max_uv = max(u, v)
                saw_uv = True
            else:
                min_uv = min(min_uv, u, v)
                max_uv = max(max_uv, u, v)
            max_abs_uv = max(max_abs_uv, abs(u), abs(v))

    if generated_pd_scenario and not runtime_texcoord_accessors:
        errors.append(
            f"{label} scene.glb is missing TEXCOORD_1 runtime UVs for "
            "renderer-parity texture repeats"
        )
    if generated_pd_scenario and not color_accessors:
        errors.append(
            f"{label} scene.glb is missing COLOR_0 vertex colors for "
            "renderer-parity room shading"
        )

    if generated_pd_scenario:
        textures = gltf_json.get("textures", [])
        materials = gltf_json.get("materials", [])
        texture_count = len(textures) if isinstance(textures, list) else 0
        if isinstance(materials, list):
            for index, material in enumerate(materials):
                if not isinstance(material, dict):
                    continue
                pbr = material.get("pbrMetallicRoughness", {})
                if not isinstance(pbr, dict):
                    continue
                base_color = pbr.get("baseColorTexture")
                if not isinstance(base_color, dict):
                    continue
                texture_index = base_color.get("index")
                if not isinstance(texture_index, int):
                    errors.append(
                        f"{label} scene.glb material {index} baseColorTexture "
                        "must reference a texture index"
                    )
                    continue
                if texture_index < 0 or texture_index >= texture_count:
                    errors.append(
                        f"{label} scene.glb material {index} baseColorTexture "
                        f"index {texture_index} is out of range"
                    )
                texcoord = base_color.get("texCoord", 0)
                if texcoord != 0:
                    errors.append(
                        f"{label} scene.glb material {index} baseColorTexture "
                        f"uses texCoord {texcoord}; generated DCC previews must "
                        "bind visible textures to TEXCOORD_0, not runtime repeat UVs"
                    )

    max_abs_runtime_uv = 0.0
    for accessor_index in sorted(runtime_texcoord_accessors):
        try:
            values = _gltf_accessor_float2_values(gltf_json, bin_chunk, accessor_index)
        except (ValueError, struct.error) as exc:
            errors.append(f"{label} scene.glb has invalid TEXCOORD_1 data: {exc}")
            continue
        for u, v in values:
            max_abs_runtime_uv = max(max_abs_runtime_uv, abs(u), abs(v))

    if generated_pd_scenario and (min_uv < -0.01 or max_uv > 1.01):
        errors.append(
            f"{label} scene.glb TEXCOORD_0 range {min_uv:.3f}..{max_uv:.3f} "
            f"(max abs {max_abs_uv:.3f}) exceeds the DCC-authoring UV range; "
            "Blender/3DS Max would show tiny tiled textures instead of the "
            "user-editable scene scale"
        )

    if max_abs_runtime_uv > 256.0:
        errors.append(
            f"{label} scene.glb TEXCOORD_1 max abs {max_abs_runtime_uv:.3f} exceeds "
            "the bounded repeat range; this usually means stale unnormalized N64 "
            "texture coordinates that appear as tiny tiled textures in Blender/3DS Max"
        )


def meta_entry_allowed(name: str, ext: str, public: set[str]) -> bool:
    if name in COMMON_META_SLOT_CONTRACT:
        return True
    if name in META_SLOT_CONTRACT.get(ext, {}):
        return True
    if name.startswith("_meta/") and name.endswith(".sha256"):
        public_name = name[len("_meta/"):-len(".sha256")]
        return public_name in public
    return False


def normalize_key(key: str) -> str:
    return key.strip().lower().replace("-", "_")


def clean_value(value: object) -> str:
    if isinstance(value, str):
        text = value.strip()
    else:
        text = str(value).strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in ("'", '"'):
        text = text[1:-1].strip()
    return text


def is_catalog_id_value(value: object) -> bool:
    return CATALOG_ID_RE.match(clean_value(value)) is not None


def is_forbidden_asset_ref_value(value: object) -> bool:
    text = clean_value(value)
    if NUMERIC_LITERAL_RE.match(text):
        return True
    return any(text.startswith(prefix) for prefix in LEGACY_ASSET_SYMBOL_PREFIXES)


def is_catalog_ref_key(key: str, path: str = "") -> bool:
    normalized = normalize_key(key)
    if normalized in CATALOG_IDENTITY_KEYS:
        return False
    if normalized == "id":
        return path.endswith(".fallback.id") or ".fallback." in path
    if normalized in NON_ASSET_ID_KEYS:
        return False
    if normalized in CATALOG_REFERENCE_KEYS:
        return True
    return (
        normalized.endswith("_catalog_id")
        or normalized.endswith("_asset_ref")
        or normalized.endswith("_asset_id")
        or normalized.endswith("_ref")
    )


def is_delimited_catalog_ref_column(column: str) -> bool:
    """TSV/CSV table columns use pad_ref/room_ref too, so avoid generic _ref."""
    normalized = normalize_key(column)
    if normalized in CATALOG_IDENTITY_KEYS:
        return False
    if normalized in NON_ASSET_ID_KEYS:
        return False
    return (
        normalized in FORBIDDEN_NUMERIC_ASSET_REF_KEYS
        or normalized in CATALOG_REFERENCE_KEYS
        or normalized.endswith("_catalog_id")
        or normalized.endswith("_asset_ref")
        or normalized.endswith("_asset_id")
    )


def validate_catalog_id_reference(value: object, known_catalog_ids: set[str] | None,
                                  label: str, entry_name: str, path: str,
                                  errors: list[str]) -> None:
    if known_catalog_ids is None:
        return
    text = clean_value(value)
    if not CATALOG_ID_RE.match(text):
        return
    if text not in known_catalog_ids:
        errors.append(
            f"{label} contains unknown catalog ID reference "
            f"{entry_name}:{path}={text}; use an actual declared catalog ID"
        )


def scan_json_asset_refs(value: object, path: str, errors: list[str],
                         label: str, entry_name: str,
                         known_catalog_ids: set[str] | None = None) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            next_path = f"{path}.{key}" if path else str(key)
            normalized = normalize_key(str(key))
            if (normalized in FORBIDDEN_NUMERIC_ASSET_REF_KEYS and
                    not isinstance(child, (dict, list)) and
                    is_forbidden_asset_ref_value(child)):
                errors.append(
                    f"{label} contains numeric/legacy asset reference "
                    f"{entry_name}:{next_path}={clean_value(child)}; use a catalog ID"
                )
            if (not isinstance(child, (dict, list)) and
                    is_catalog_ref_key(str(key), next_path)):
                validate_catalog_id_reference(
                    child, known_catalog_ids, label, entry_name, next_path, errors
                )
            scan_json_asset_refs(child, next_path, errors, label, entry_name,
                                 known_catalog_ids)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            scan_json_asset_refs(child, f"{path}[{index}]", errors, label,
                                 entry_name, known_catalog_ids)


def scan_text_asset_refs(label: str, entry_name: str, text: str,
                         known_catalog_ids: set[str] | None = None) -> list[str]:
    errors: list[str] = []
    if entry_name.lower().endswith(".json"):
        try:
            parsed = json.loads(text)
        except json.JSONDecodeError:
            parsed = None
        if parsed is not None:
            scan_json_asset_refs(parsed, "", errors, label, entry_name,
                                 known_catalog_ids)
            return errors

    for line_num, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith(("#", ";", "//")):
            continue
        match = KEY_VALUE_RE.match(line)
        if not match:
            continue
        key = normalize_key(match.group(1))
        value = match.group(2).split("#", 1)[0].split(";", 1)[0].strip()
        if key in FORBIDDEN_NUMERIC_ASSET_REF_KEYS and is_forbidden_asset_ref_value(value):
            errors.append(
                f"{label} contains numeric/legacy asset reference "
                f"{entry_name}:{line_num} {match.group(1)}={clean_value(value)}; "
                "use a catalog ID"
            )
        if is_catalog_ref_key(match.group(1), match.group(1)):
            validate_catalog_id_reference(
                value, known_catalog_ids, label, entry_name,
                f"{line_num} {match.group(1)}", errors
            )
    return errors


def scan_delimited_asset_refs(label: str, entry_name: str, text: str,
                              known_catalog_ids: set[str] | None = None) -> list[str]:
    errors: list[str] = []
    lower = entry_name.lower()
    delimiter = "\t" if lower.endswith(".tsv") else ","

    try:
        rows = list(csv.reader(text.splitlines(), delimiter=delimiter))
    except csv.Error as exc:
        return [f"{label} has unreadable delimited table {entry_name}: {exc}"]

    if not rows:
        return errors

    header = rows[0]
    if not header:
        return errors

    checked_columns = [
        (index, column)
        for index, column in enumerate(header)
        if is_delimited_catalog_ref_column(column)
    ]
    if not checked_columns:
        return errors

    for row_num, row in enumerate(rows[1:], 2):
        if not row or not any(cell.strip() for cell in row):
            continue
        for index, column in checked_columns:
            if index >= len(row):
                continue
            value = row[index].strip()
            if not value:
                continue

            field_label = f"{row_num} {column}"
            if is_forbidden_asset_ref_value(value):
                errors.append(
                    f"{label} contains numeric/legacy asset reference "
                    f"{entry_name}:{field_label}={clean_value(value)}; "
                    "use a catalog ID"
                )
                continue
            if not is_catalog_id_value(value):
                errors.append(
                    f"{label} contains invalid catalog ID reference "
                    f"{entry_name}:{field_label}={clean_value(value)}; "
                    "use a catalog ID"
                )
                continue
            validate_catalog_id_reference(
                value, known_catalog_ids, label, entry_name, field_label, errors
            )
    return errors


def read_tsv_rows(label: str, entry_name: str, text: str) -> tuple[list[list[str]], list[str]]:
    try:
        return list(csv.reader(text.splitlines(), delimiter="\t")), []
    except csv.Error as exc:
        return [], [f"{label} has unreadable TSV table {entry_name}: {exc}"]


def count_tsv_data_rows(label: str, entry_name: str, text: str) -> tuple[int, list[str]]:
    rows, errors = read_tsv_rows(label, entry_name, text)
    if errors:
        return 0, errors
    if not rows:
        return 0, [f"{label} {entry_name} must contain a header"]
    return sum(1 for row in rows[1:] if any(cell.strip() for cell in row)), []


def validate_objectives_tsv_schema(label: str, entry_name: str, text: str,
                                   expected_header: list[str]) -> list[str]:
    rows, errors = read_tsv_rows(label, entry_name, text)
    if errors:
        return errors
    if not rows:
        return [f"{label} {entry_name} must contain the definitive objective source header"]

    header = rows[0]
    if header != expected_header:
        return [
            f"{label} {entry_name} must use definitive objective source columns: "
            + ", ".join(expected_header)
        ]

    col = {name: index for index, name in enumerate(header)}
    for row_num, row in enumerate(rows[1:], 2):
        if not row or not any(cell.strip() for cell in row):
            continue
        padded = row + [""] * (len(header) - len(row))
        kind = padded[col["kind"]].strip()
        operand = padded[col["operand_kind"]].strip()
        expected_operand = OBJECTIVE_KIND_OPERANDS.get(kind)
        if expected_operand and operand != expected_operand:
            errors.append(
                f"{label} {entry_name}:{row_num} kind {kind} must use operand_kind={expected_operand}"
            )
            continue
        if kind in {
            "objective_destroy_object",
            "objective_collect_object",
            "objective_throw_object",
            "objective_holograph",
        } and not padded[col["target_ref"]].strip().startswith("tag_"):
            errors.append(
                f"{label} {entry_name}:{row_num} {kind} must declare target_ref as tag_####"
            )
        if kind in {"objective_complete_flags", "objective_fail_flags"} and not padded[
            col["state_ref"]
        ].strip().startswith("stage_flag_0x"):
            errors.append(
                f"{label} {entry_name}:{row_num} {kind} must declare state_ref as stage_flag_0x########"
            )
        if kind in {"objective_enter_room", "objective_throw_in_room"} and not padded[
            col["pad_ref"]
        ].strip().startswith("pad_"):
            errors.append(
                f"{label} {entry_name}:{row_num} {kind} must declare pad_ref as pad_####"
            )
        if kind == "objective_throw_in_room" and not padded[col["match_value"]].strip():
            errors.append(
                f"{label} {entry_name}:{row_num} objective_throw_in_room must declare match_value"
            )
    return errors


def validate_setup_fields_objective_schema(label: str, entry_name: str,
                                           text: str) -> list[str]:
    rows, errors = read_tsv_rows(label, entry_name, text)
    if errors:
        return errors
    if not rows:
        return errors

    header = rows[0]
    col = {name: index for index, name in enumerate(header)}
    field_index = col.get("field")
    type_index = col.get("type")
    value_index = col.get("value")
    if field_index is None:
        return [f"{label} {entry_name} must include a field column"]

    for row_num, row in enumerate(rows[1:], 2):
        if field_index >= len(row):
            continue
        field = row[field_index].strip()
        value_type = row[type_index].strip() if type_index is not None and type_index < len(row) else ""
        value = row[value_index].strip() if value_index is not None and value_index < len(row) else ""
        if field in FORBIDDEN_SETUP_OBJECTIVE_FIELDS:
            errors.append(
                f"{label} {entry_name}:{row_num} uses raw {field}; use typed objective target/stage source fields"
            )
        if field in {"objective_step.target_tag", "objective_holograph.target_tag"}:
            if value_type != "tag_ref" or not value.startswith("tag_"):
                errors.append(
                    f"{label} {entry_name}:{row_num} {field} must be a tag_ref value"
                )
        if field == "objective_step.stage_flag":
            if value_type != "stage_flag_ref" or not value.startswith("stage_flag_0x"):
                errors.append(
                    f"{label} {entry_name}:{row_num} {field} must be a stage_flag_ref value"
                )
    return errors


def descriptor_value(text: str, key_name: str) -> str:
    wanted = normalize_key(key_name)
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith(("#", ";", "//")):
            continue
        match = KEY_VALUE_RE.match(line)
        if not match:
            continue
        if normalize_key(match.group(1)) == wanted:
            return clean_value(match.group(2).split("#", 1)[0].split(";", 1)[0])
    return ""


def collect_ini_catalog_ids(text: str) -> set[str]:
    ids: set[str] = set()
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith(("#", ";", "//")):
            continue
        match = KEY_VALUE_RE.match(line)
        if not match:
            continue
        key = normalize_key(match.group(1))
        if key not in CATALOG_IDENTITY_KEYS:
            continue
        value = clean_value(match.group(2).split("#", 1)[0].split(";", 1)[0])
        if CATALOG_ID_RE.match(value):
            ids.add(value)
    return ids


def collect_json_catalog_ids(value: object, entry_name: str = "") -> set[str]:
    ids: set[str] = set()
    if not isinstance(value, dict):
        return ids

    for key in ("catalog_id", "asset_id"):
        child = value.get(key)
        if isinstance(child, str) and CATALOG_ID_RE.match(child.strip()):
            ids.add(child.strip())

    manifest_id = value.get("id")
    if (entry_name == "_meta/manifest.json" and isinstance(manifest_id, str)
            and CATALOG_ID_RE.match(manifest_id.strip())):
        ids.add(manifest_id.strip())
    return ids


def collect_archive_catalog_ids(data: bytes, ext: str,
                                recurse: bool = True) -> set[str]:
    ids: set[str] = set()
    try:
        with zipfile.ZipFile(BytesIO(data)) as zf:
            names = [normalize_name(n) for n in zf.namelist()]
            for name in names:
                if name.endswith("/") or not name.lower().endswith(PUBLIC_TEXT_ENTRY_SUFFIXES):
                    continue
                try:
                    text = zf.read(name).decode("utf-8", errors="replace")
                except KeyError:
                    continue
                if name.lower().endswith(".json"):
                    try:
                        ids.update(collect_json_catalog_ids(json.loads(text), name))
                    except json.JSONDecodeError:
                        pass
                else:
                    ids.update(collect_ini_catalog_ids(text))

            if recurse:
                for name in public_names(names):
                    nested_ext = Path(name).suffix.lower()
                    if nested_ext in TYPED_DESCRIPTORS:
                        try:
                            ids.update(collect_archive_catalog_ids(
                                zf.read(name), nested_ext, recurse=True
                            ))
                        except KeyError:
                            continue
    except zipfile.BadZipFile:
        return ids
    return ids


def validate_manifest_dependency_refs(label: str, text: str,
                                      known_catalog_ids: set[str] | None,
                                      errors: list[str]) -> None:
    if known_catalog_ids is None:
        return
    try:
        manifest = json.loads(text)
    except json.JSONDecodeError:
        return
    dependencies = manifest.get("dependencies")
    if not isinstance(dependencies, list):
        return
    for index, dep in enumerate(dependencies):
        if not isinstance(dep, dict):
            continue
        dep_id = dep.get("id")
        if isinstance(dep_id, str) and CATALOG_ID_RE.match(dep_id.strip()):
            validate_catalog_id_reference(
                dep_id, known_catalog_ids, label, "_meta/manifest.json",
                f"dependencies[{index}].id", errors
            )
        fallback = dep.get("fallback")
        if isinstance(fallback, dict):
            fallback_id = fallback.get("id")
            if isinstance(fallback_id, str) and fallback_id.strip():
                validate_catalog_id_reference(
                    fallback_id, known_catalog_ids, label,
                    "_meta/manifest.json",
                    f"dependencies[{index}].fallback.id", errors
                )


def validate_archive_bytes(data: bytes, label: str, ext: str,
                           recurse: bool = True,
                           known_catalog_ids: set[str] | None = None) -> ConformanceResult:
    result = ConformanceResult()
    ext = ext.lower()
    descriptor = TYPED_DESCRIPTORS.get(ext)
    spec = SCHEMAS.get(ext)
    if not descriptor or not spec:
        result.errors.append(f"{label} has unknown typed archive extension {ext}")
        return result

    result.checked_archives += 1
    result.families.add(ext)

    try:
        with zipfile.ZipFile(BytesIO(data)) as zf:
            names = [normalize_name(n) for n in zf.namelist()]
            name_set = set(names)
            public = [n for n in public_names(names) if not n.endswith("/")]
            public_set = set(public)
            local_catalog_ids = collect_archive_catalog_ids(
                data, ext, recurse=True
            )
            active_catalog_ids = known_catalog_ids or local_catalog_ids

            for name in names:
                if name.endswith("/"):
                    result.errors.append(
                        f"{label} contains directory placeholder {name}; archives should contain files only"
                    )

            if descriptor not in name_set:
                result.errors.append(f"{label} missing root descriptor {descriptor}")
            if "_meta/manifest.json" not in name_set:
                result.errors.append(f"{label} missing _meta/manifest.json")
            for root_meta in ROOT_METADATA:
                if root_meta in name_set:
                    result.errors.append(
                        f"{label} contains root machine metadata {root_meta}; use _meta/{root_meta}"
                    )
            for name in names:
                if name.endswith("/") or not is_meta_name(name):
                    continue
                if not meta_entry_allowed(name, ext, public_set):
                    result.errors.append(
                        f"{label} contains _meta entry outside definitive {ext} contract: {name}"
                    )

            for required in spec.required:
                if required not in name_set:
                    result.errors.append(f"{label} missing required {required}")
            for group in spec.require_any:
                if not any(archive_has_pattern(name_set, item) for item in group):
                    result.errors.append(
                        f"{label} missing required alternative: one of {', '.join(group)}"
                    )
            if ext == ".pdmesh" and "model.nodes.tsv" in name_set and "model.parts.tsv" not in name_set:
                result.errors.append(
                    f"{label} has model.nodes.tsv but missing required model.parts.tsv"
                )
            if ext == ".pdmesh" and "model.nodes.tsv" in name_set and "model.faces.tsv" not in name_set:
                result.errors.append(
                    f"{label} has model.nodes.tsv but missing required model.faces.tsv"
                )
            for rule in spec.require_one_of:
                if not any(archive_has_all(name_set, alternative)
                           for alternative in rule):
                    rendered = [
                        " + ".join(alternative)
                        for alternative in rule
                    ]
                    result.errors.append(
                        f"{label} missing required shape: one of {'; '.join(rendered)}"
                    )

            for name in public:
                leaf = name.rsplit("/", 1)[-1].lower()
                if name in FORBIDDEN_COMMON_EXACT or leaf in FORBIDDEN_COMMON_EXACT:
                    result.errors.append(
                        f"{label} contains forbidden authored runtime/stale payload {name}"
                    )
                if matches_any(name, FORBIDDEN_COMMON_GLOBS):
                    result.errors.append(
                        f"{label} contains forbidden authored runtime/stale payload {name}"
                    )
                if name in spec.forbidden or leaf in spec.forbidden:
                    result.errors.append(
                        f"{label} contains stale entry forbidden by {ext} schema: {name}"
                    )
                if matches_any(name, spec.forbidden_globs):
                    result.errors.append(
                        f"{label} contains stale entry forbidden by {ext} schema: {name}"
                    )
                if not public_entry_allowed(name, spec):
                    result.errors.append(
                        f"{label} contains public entry outside definitive {ext} schema: {name}"
                    )
                elif not public_entry_has_contract(name, ext, spec):
                    result.errors.append(
                        f"{label} contains undocumented optional public entry in {ext}: {name}"
                    )

                if name.lower().endswith(PUBLIC_TEXT_ENTRY_SUFFIXES):
                    try:
                        text = zf.read(name).decode("utf-8", errors="replace")
                    except KeyError:
                        continue
                    if name.lower().endswith((".tsv", ".csv")):
                        result.errors.extend(scan_delimited_asset_refs(
                            label, name, text, active_catalog_ids
                        ))
                    result.errors.extend(scan_text_asset_refs(
                        label, name, text, active_catalog_ids
                    ))

            if "_meta/manifest.json" in name_set:
                manifest_text = zf.read("_meta/manifest.json").decode(
                    "utf-8", errors="replace"
                )
                if ext == ".pdweapon" and '"SFX_0000"' in manifest_text:
                    result.errors.append(
                        f"{label} _meta/manifest.json contains SFX_0000; "
                        "serialize zero/no-sound weapon shootsound as 0 or null"
                    )
                validate_manifest_dependency_refs(
                    label, manifest_text, active_catalog_ids, result.errors
                )

            if ext == ".pdscenario" and descriptor in name_set:
                text = zf.read(descriptor).decode("utf-8", errors="replace")
                if "scene.glb" in name_set:
                    validate_scene_glb_texture_contract(
                        label, zf.read("scene.glb"), result.errors
                    )
                if "scene_file = scene.glb" not in text and "scene_file = scene.gltf" not in text:
                    result.errors.append(
                        f"{label} scenario.ini must declare scene_file = scene.glb or scene.gltf"
                    )
                if "runtime_source_file = scene.glb" not in text and "runtime_source_file = scene.gltf" not in text:
                    result.errors.append(
                        f"{label} scenario.ini must declare runtime_source_file matching the scene source"
                    )
                if "setup_fields_file = setup.fields.tsv" not in text:
                    result.errors.append(
                        f"{label} scenario.ini must declare setup_fields_file = setup.fields.tsv"
                    )
                if "ai_lists_file = ai/ailists.tsv" not in text:
                    result.errors.append(
                        f"{label} scenario.ini must declare ai_lists_file = ai/ailists.tsv"
                    )
                if "paths_file = navigation/paths.tsv" not in text:
                    result.errors.append(
                        f"{label} scenario.ini must declare paths_file = navigation/paths.tsv"
                    )
                if "portals_file = portals.tsv" not in text:
                    result.errors.append(
                        f"{label} scenario.ini must declare portals_file = portals.tsv"
                    )
                for stale_key in ("setup_file", "mpsetup_file", "rooms_file", "geometry_file", "visual_scene_file"):
                    if stale_key in text:
                        result.errors.append(
                            f"{label} scenario.ini still declares {stale_key}; use scene/native tables/graphs"
                        )
                if "portals.tsv" in name_set:
                    portals_text = zf.read("portals.tsv").decode(
                        "utf-8", errors="replace"
                    )
                    first_line = portals_text.splitlines()[0] if portals_text.splitlines() else ""
                    if first_line != SCENARIO_PORTALS_HEADER:
                        result.errors.append(
                            f"{label} portals.tsv must use the definitive portal source header"
                        )
                if "objectives.tsv" in name_set:
                    objectives_text = zf.read("objectives.tsv").decode(
                        "utf-8", errors="replace"
                    )
                    result.errors.extend(validate_objectives_tsv_schema(
                        label, "objectives.tsv", objectives_text,
                        SCENARIO_OBJECTIVES_HEADER
                    ))
                if "spawns.tsv" in name_set:
                    spawns_text = zf.read("spawns.tsv").decode(
                        "utf-8", errors="replace"
                    )
                    first_line = spawns_text.splitlines()[0] if spawns_text.splitlines() else ""
                    if first_line != SCENARIO_SPAWNS_HEADER:
                        result.errors.append(
                            f"{label} spawns.tsv must use the definitive spawn source header"
                        )
                if "setup.fields.tsv" in name_set:
                    setup_fields_text = zf.read("setup.fields.tsv").decode(
                        "utf-8", errors="replace"
                    )
                    result.errors.extend(validate_setup_fields_objective_schema(
                        label, "setup.fields.tsv", setup_fields_text
                    ))
                if "ai/ailists.tsv" in name_set:
                    ai_text = zf.read("ai/ailists.tsv").decode(
                        "utf-8", errors="replace"
                    )
                    required_ai_header = (
                        "ailist_ref\tlist_id\tgraph_node\tcommand_index\toffset\t"
                        "opcode\topcode_name\toperands\tmodel_catalog_id\t"
                        "weapon_catalog_id\tbody_catalog_id\thead_catalog_id"
                    )
                    first_line = ai_text.splitlines()[0] if ai_text.splitlines() else ""
                    if first_line != required_ai_header:
                        result.errors.append(
                            f"{label} ai/ailists.tsv must use the definitive AI list source header"
                        )
                if "navigation/paths.tsv" in name_set:
                    paths_text = zf.read("navigation/paths.tsv").decode(
                        "utf-8", errors="replace"
                    )
                    first_line = paths_text.splitlines()[0] if paths_text.splitlines() else ""
                    if first_line != "path_ref\tflags\tpads":
                        result.errors.append(
                            f"{label} navigation/paths.tsv must use the definitive path source header"
                        )
                if "navigation.ini" in name_set:
                    navigation_ini_text = zf.read("navigation.ini").decode(
                        "utf-8", errors="replace"
                    )
                    for required_line, description in {
                        "supports_walk = true": "walk",
                        "supports_jump = true": "jump",
                        "supports_drop = true": "drop",
                        "supports_wall = true": "wall",
                        "supports_ceiling = true": "ceiling",
                    }.items():
                        if required_line not in navigation_ini_text:
                            result.errors.append(
                                f"{label} navigation.ini must declare {description} capability support"
                            )
                if "_meta/generated-navmesh.json" in name_set:
                    navmesh_text = zf.read("_meta/generated-navmesh.json").decode(
                        "utf-8", errors="replace"
                    )
                    try:
                        navmesh = json.loads(navmesh_text)
                    except json.JSONDecodeError as exc:
                        result.errors.append(
                            f"{label} _meta/generated-navmesh.json is not valid JSON: {exc.msg}"
                        )
                        navmesh = {}
                    expected_capabilities = ["walk", "jump", "drop", "wall", "ceiling"]
                    if navmesh.get("capabilities") != expected_capabilities:
                        result.errors.append(
                            f"{label} _meta/generated-navmesh.json must declare movement capabilities walk,jump,drop,wall,ceiling"
                        )
                    source_counts = navmesh.get("source_counts")
                    if not isinstance(source_counts, dict):
                        result.errors.append(
                            f"{label} _meta/generated-navmesh.json must declare source_counts"
                        )
                    else:
                        for source_name, count_key in {
                            "pads.tsv": "pads",
                            "volumes.tsv": "volumes",
                            "navigation/waypoints.tsv": "waypoints",
                            "navigation/waygroups.tsv": "waygroups",
                            "navigation/covers.tsv": "covers",
                            "navigation/paths.tsv": "paths",
                        }.items():
                            if source_name not in name_set:
                                result.errors.append(
                                    f"{label} _meta/generated-navmesh.json source_counts requires {source_name}"
                                )
                                continue
                            row_count, row_errors = count_tsv_data_rows(
                                label,
                                source_name,
                                zf.read(source_name).decode("utf-8", errors="replace"),
                            )
                            result.errors.extend(row_errors)
                            if source_counts.get(count_key) != row_count:
                                result.errors.append(
                                    f"{label} _meta/generated-navmesh.json source_counts.{count_key} must match {source_name} rows"
                                )
                    source_hashes = navmesh.get("source_hashes")
                    if not isinstance(source_hashes, dict):
                        result.errors.append(
                            f"{label} _meta/generated-navmesh.json must declare source_hashes"
                        )
                    else:
                        for source_name in [
                            "scene.glb",
                            "collision.obj",
                            "navigation.ini",
                            "portals.tsv",
                            "pads.tsv",
                            "spawns.tsv",
                            "volumes.tsv",
                            "navigation/waypoints.tsv",
                            "navigation/waygroups.tsv",
                            "navigation/covers.tsv",
                            "navigation/paths.tsv",
                        ]:
                            if source_name not in name_set:
                                result.errors.append(
                                    f"{label} _meta/generated-navmesh.json source_hashes requires {source_name}"
                                )
                                continue
                            source_hash = hashlib.sha256(
                                zf.read(source_name)
                            ).hexdigest()
                            if source_hashes.get(source_name) != source_hash:
                                result.errors.append(
                                    f"{label} _meta/generated-navmesh.json source_hashes.{source_name} must match public source bytes"
                                )
                if "level.graph.json" in name_set:
                    graph_text = zf.read("level.graph.json").decode(
                        "utf-8", errors="replace"
                    )
                    try:
                        graph = json.loads(graph_text)
                    except json.JSONDecodeError as exc:
                        result.errors.append(
                            f"{label} level.graph.json is not valid JSON: {exc.msg}"
                        )
                        graph = {}
                    nodes = graph.get("nodes")
                    tables = graph.get("tables")
                    if not isinstance(tables, dict) or tables.get("paths") != "navigation/paths.tsv":
                        result.errors.append(
                            f"{label} level.graph.json must bind paths table to navigation/paths.tsv"
                        )
                    if not isinstance(tables, dict) or tables.get("portals") != "portals.tsv":
                        result.errors.append(
                            f"{label} level.graph.json must bind portals table to portals.tsv"
                        )
                    if not isinstance(nodes, list) or not nodes:
                        result.errors.append(
                            f"{label} level.graph.json must contain executable scenario graph nodes"
                        )
                    elif isinstance(nodes, list):
                        node_kinds = [
                            str(node.get("kind", ""))
                            for node in nodes
                            if isinstance(node, dict)
                        ]
                        for required_kind, description in {
                            "scenario.global.settings.source": "global settings",
                            "scenario.portals.source": "portal source",
                            "scenario.pads.source": "pad source",
                            "scenario.ai.lists.source": "AI list source",
                            "scenario.navigation.paths.source": "navigation path source",
                            "scenario.ai.action.set_list": "AI set_list action",
                            "scenario.ai.action.set_return_list": "AI set_return_list action",
                            "scenario.ai.action.set_shot_list": "AI set_shot_list action",
                            "scenario.ai.action.return_list": "AI return_list action",
                            "scenario.ai.action.stop": "AI stop action",
                            "scenario.ai.action.kneel": "AI kneel action",
                            "scenario.ai.action.surrender": "AI surrender action",
                            "scenario.ai.action.fade_out": "AI fade_out action",
                            "scenario.ai.action.remove_chr": "AI remove_chr action",
                            "scenario.ai.action.try_sidestep": "AI try_sidestep action",
                            "scenario.ai.action.try_jump_out": "AI try_jump_out action",
                            "scenario.ai.action.try_run_sideways": "AI try_run_sideways action",
                            "scenario.ai.action.try_attack_walk": "AI try_attack_walk action",
                            "scenario.ai.action.try_attack_run": "AI try_attack_run action",
                            "scenario.ai.action.try_attack_roll": "AI try_attack_roll action",
                            "scenario.ai.action.try_attack_stand": "AI try_attack_stand action",
                            "scenario.ai.action.try_attack_kneel": "AI try_attack_kneel action",
                            "scenario.ai.action.try_attack_lie": "AI try_attack_lie action",
                            "scenario.ai.condition.if_attack_locked": "AI if_attack_locked condition",
                            "scenario.ai.condition.if_attacking": "AI if_attacking condition",
                            "scenario.ai.action.try_modify_attack": "AI try_modify_attack action",
                            "scenario.ai.action.face_entity": "AI face_entity action",
                            "scenario.ai.action.apply_gset_damage": "AI apply_gset_damage action",
                            "scenario.ai.action.chr_damage_chr": "AI chr_damage_chr action",
                            "scenario.ai.condition.consider_grenade_throw": "AI consider_grenade_throw condition",
                            "scenario.ai.action.drop_item": "AI drop_item action",
                            "scenario.ai.action.try_run_from_target": "AI try_run_from_target action",
                            "scenario.ai.action.try_jog_to_target_prop": "AI try_jog_to_target_prop action",
                            "scenario.ai.action.try_walk_to_target_prop": "AI try_walk_to_target_prop action",
                            "scenario.ai.action.try_run_to_target_prop": "AI try_run_to_target_prop action",
                            "scenario.ai.action.try_go_to_cover_prop": "AI try_go_to_cover_prop action",
                            "scenario.ai.action.try_jog_to_chr": "AI try_jog_to_chr action",
                            "scenario.ai.action.try_walk_to_chr": "AI try_walk_to_chr action",
                            "scenario.ai.action.try_run_to_chr": "AI try_run_to_chr action",
                            "scenario.ai.condition.if_can_hear_alarm": "AI if_can_hear_alarm condition",
                            "scenario.ai.condition.if_patrolling": "AI if_patrolling condition",
                            "scenario.ai.condition.if_alarm_active": "AI if_alarm_active condition",
                            "scenario.ai.condition.if_gas_active": "AI if_gas_active condition",
                            "scenario.ai.condition.if_hears_target": "AI if_hears_target condition",
                            "scenario.ai.condition.if_saw_injury": "AI if_saw_injury condition",
                            "scenario.ai.condition.if_saw_death": "AI if_saw_death condition",
                            "scenario.ai.condition.if_los_to_target": "AI if_los_to_target condition",
                            "scenario.ai.condition.if_los_to_attack_target": "AI if_los_to_attack_target condition",
                            "scenario.ai.condition.if_target_nearly_in_sight": "AI if_target_nearly_in_sight condition",
                            "scenario.ai.condition.if_nearly_in_targets_sight": "AI if_nearly_in_targets_sight condition",
                            "scenario.ai.action.set_pad_preset_to_pad_on_route_to_target": "AI set_pad_preset_to_pad_on_route_to_target action",
                            "scenario.ai.condition.if_saw_target_recently": "AI if_saw_target_recently condition",
                            "scenario.ai.condition.if_heard_target_recently": "AI if_heard_target_recently condition",
                            "scenario.ai.condition.if_los_to_chr": "AI if_los_to_chr condition",
                            "scenario.ai.condition.if_never_been_on_screen": "AI if_never_been_on_screen condition",
                            "scenario.ai.condition.if_on_screen": "AI if_on_screen condition",
                            "scenario.ai.condition.if_chr_in_on_screen_room": "AI if_chr_in_on_screen_room condition",
                            "scenario.ai.condition.if_room_is_on_screen": "AI if_room_is_on_screen condition",
                            "scenario.ai.condition.if_target_aiming_at_me": "AI if_target_aiming_at_me condition",
                            "scenario.ai.condition.if_near_miss": "AI if_near_miss condition",
                            "scenario.ai.condition.if_sees_suspicious_item": "AI if_sees_suspicious_item condition",
                            "scenario.ai.condition.if_target_in_fov_left": "AI if_target_in_fov_left condition",
                            "scenario.ai.condition.if_check_fov_with_target": "AI if_check_fov_with_target condition",
                            "scenario.ai.condition.if_target_out_of_fov_left": "AI if_target_out_of_fov_left condition",
                            "scenario.ai.condition.if_target_in_fov": "AI if_target_in_fov condition",
                            "scenario.ai.condition.if_target_out_of_fov": "AI if_target_out_of_fov condition",
                            "scenario.ai.condition.if_distance_to_target_less_than": "AI if_distance_to_target_less_than condition",
                            "scenario.ai.condition.if_distance_to_target_greater_than": "AI if_distance_to_target_greater_than condition",
                            "scenario.ai.condition.if_chr_distance_to_pad_less_than": "AI if_chr_distance_to_pad_less_than condition",
                            "scenario.ai.condition.if_chr_distance_to_pad_greater_than": "AI if_chr_distance_to_pad_greater_than condition",
                            "scenario.ai.condition.if_distance_to_chr_less_than": "AI if_distance_to_chr_less_than condition",
                            "scenario.ai.condition.if_distance_to_chr_greater_than": "AI if_distance_to_chr_greater_than condition",
                            "scenario.ai.condition.if_any_chr_near_self": "AI if_any_chr_near_self condition",
                            "scenario.ai.condition.if_distance_from_target_to_pad_less_than": "AI if_distance_from_target_to_pad_less_than condition",
                            "scenario.ai.condition.if_distance_from_target_to_pad_greater_than": "AI if_distance_from_target_to_pad_greater_than condition",
                            "scenario.ai.condition.if_chr_in_room": "AI if_chr_in_room condition",
                            "scenario.ai.condition.if_target_in_room": "AI if_target_in_room condition",
                            "scenario.ai.condition.if_chr_has_object": "AI if_chr_has_object condition",
                            "scenario.ai.condition.if_weapon_thrown": "AI if_weapon_thrown condition",
                            "scenario.ai.condition.if_weapon_thrown_on_object": "AI if_weapon_thrown_on_object condition",
                            "scenario.ai.condition.if_chr_has_weapon_equipped": "AI if_chr_has_weapon_equipped condition",
                            "scenario.ai.condition.if_gun_unclaimed": "AI if_gun_unclaimed condition",
                            "scenario.ai.condition.if_object_healthy": "AI if_object_healthy condition",
                            "scenario.ai.condition.if_chr_activated_object": "AI if_chr_activated_object condition",
                            "scenario.ai.action.obj_interact": "AI obj_interact action",
                            "scenario.ai.action.destroy_object": "AI destroy_object action",
                            "scenario.ai.action.drop_object_from_chr": "AI drop_object_from_chr action",
                            "scenario.ai.action.chr_drop_items": "AI chr_drop_items action",
                            "scenario.ai.action.chr_drop_weapon": "AI chr_drop_weapon action",
                            "scenario.ai.action.give_object_to_chr": "AI give_object_to_chr action",
                            "scenario.ai.action.object_move_to_pad": "AI object_move_to_pad action",
                            "scenario.ai.condition.if_waypoint_within_quadrant": "AI if_waypoint_within_quadrant condition",
                            "scenario.ai.action.set_pad_preset_to_target_quadrant": "AI set_pad_preset_to_target_quadrant action",
                            "scenario.ai.action.chr_do_animation": "AI chr_do_animation action",
                            "scenario.ai.action.be_surprised_one_hand": "AI be_surprised_one_hand action",
                            "scenario.ai.action.be_surprised_look_around": "AI be_surprised_look_around action",
                            "scenario.ai.action.be_surprised_surrender": "AI be_surprised_surrender action",
                            "scenario.ai.action.random": "AI random action",
                            "scenario.ai.condition.if_random_less_than": "AI if_random_less_than condition",
                            "scenario.ai.condition.if_random_greater_than": "AI if_random_greater_than condition",
                            "scenario.ai.action.print": "AI print action",
                            "scenario.ai.action.noop": "AI no-op action",
                            "scenario.ai.action.set_punch_dodge_list": "AI set_punch_dodge_list action",
                            "scenario.ai.action.set_shooting_at_me_list": "AI set_shooting_at_me_list action",
                            "scenario.ai.action.set_dark_room_list": "AI set_dark_room_list action",
                            "scenario.ai.action.set_player_dead_list": "AI set_player_dead_list action",
                            "scenario.ai.action.jog_to_pad": "AI jog_to_pad action",
                            "scenario.ai.action.go_to_pad_preset": "AI go_to_pad_preset action",
                            "scenario.ai.action.walk_to_pad": "AI walk_to_pad action",
                            "scenario.ai.action.run_to_pad": "AI run_to_pad action",
                            "scenario.ai.action.set_path": "AI set_path action",
                            "scenario.ai.action.start_patrol": "AI start_patrol action",
                            "scenario.ai.action.try_start_alarm": "AI try_start_alarm action",
                            "scenario.ai.action.activate_alarm": "AI activate_alarm action",
                            "scenario.ai.action.deactivate_alarm": "AI deactivate_alarm action",
                            "scenario.ai.action.set_morale": "AI set_morale action",
                            "scenario.ai.action.add_morale": "AI add_morale action",
                            "scenario.ai.action.chr_add_morale": "AI chr_add_morale action",
                            "scenario.ai.action.subtract_morale": "AI subtract_morale action",
                            "scenario.ai.action.set_alertness": "AI set_alertness action",
                            "scenario.ai.action.add_alertness": "AI add_alertness action",
                            "scenario.ai.action.chr_add_alertness": "AI chr_add_alertness action",
                            "scenario.ai.action.subtract_alertness": "AI subtract_alertness action",
                            "scenario.ai.action.increase_squadron_alertness": "AI increase_squadron_alertness action",
                            "scenario.ai.action.set_hear_distance": "AI set_hear_distance action",
                            "scenario.ai.action.set_view_distance": "AI set_view_distance action",
                            "scenario.ai.action.set_grenade_probability": "AI set_grenade_probability action",
                            "scenario.ai.action.set_chr_num": "AI set_chr_num action",
                            "scenario.ai.action.set_max_damage": "AI set_max_damage action",
                            "scenario.ai.action.add_health": "AI add_health action",
                            "scenario.ai.action.set_shield": "AI set_shield action",
                            "scenario.ai.action.set_reaction_speed": "AI set_reaction_speed action",
                            "scenario.ai.action.set_recovery_speed": "AI set_recovery_speed action",
                            "scenario.ai.action.set_accuracy": "AI set_accuracy action",
                            "scenario.ai.action.set_dodge_rating": "AI set_dodge_rating action",
                            "scenario.ai.action.set_unarmed_dodge_rating": "AI set_unarmed_dodge_rating action",
                            "scenario.ai.action.set_flag": "AI set_flag action",
                            "scenario.ai.action.unset_flag": "AI unset_flag action",
                            "scenario.ai.action.if_has_flag": "AI if_has_flag action",
                            "scenario.ai.action.chr_set_flag": "AI chr_set_flag action",
                            "scenario.ai.action.chr_unset_flag": "AI chr_unset_flag action",
                            "scenario.ai.action.if_chr_has_flag": "AI if_chr_has_flag action",
                            "scenario.ai.action.set_stage_flag": "AI set_stage_flag action",
                            "scenario.ai.action.unset_stage_flag": "AI unset_stage_flag action",
                            "scenario.ai.action.if_stage_flag_eq": "AI if_stage_flag_eq action",
                            "scenario.ai.action.set_chrflag": "AI set_chrflag action",
                            "scenario.ai.action.unset_chrflag": "AI unset_chrflag action",
                            "scenario.ai.action.if_has_chrflag": "AI if_has_chrflag action",
                            "scenario.ai.action.chr_set_chrflag": "AI chr_set_chrflag action",
                            "scenario.ai.action.chr_unset_chrflag": "AI chr_unset_chrflag action",
                            "scenario.ai.action.if_chr_has_chrflag": "AI if_chr_has_chrflag action",
                            "scenario.ai.action.chr_set_hidden_flag": "AI chr_set_hidden_flag action",
                            "scenario.ai.action.chr_unset_hidden_flag": "AI chr_unset_hidden_flag action",
                            "scenario.ai.action.if_chr_has_hidden_flag": "AI if_chr_has_hidden_flag action",
                            "scenario.ai.action.set_obj_flag": "AI set_obj_flag action",
                            "scenario.ai.action.unset_obj_flag": "AI unset_obj_flag action",
                            "scenario.ai.action.if_obj_has_flag": "AI if_obj_has_flag action",
                            "scenario.ai.action.open_door": "AI open_door action",
                            "scenario.ai.action.close_door": "AI close_door action",
                            "scenario.ai.action.if_door_state": "AI if_door_state action",
                            "scenario.ai.action.if_object_is_door": "AI if_object_is_door action",
                            "scenario.ai.action.lock_door": "AI lock_door action",
                            "scenario.ai.action.unlock_door": "AI unlock_door action",
                            "scenario.ai.action.if_door_locked": "AI if_door_locked action",
                            "scenario.ai.action.if_lift_stationary": "AI if_lift_stationary action",
                            "scenario.ai.action.lift_go_to_stop": "AI lift_go_to_stop action",
                            "scenario.ai.action.if_lift_at_stop": "AI if_lift_at_stop action",
                            "scenario.ai.action.activate_lift": "AI activate_lift action",
                            "scenario.ai.action.if_using_lift": "AI if_using_lift action",
                            "scenario.ai.action.configure_rain": "AI configure_rain action",
                            "scenario.ai.action.configure_snow": "AI configure_snow action",
                            "scenario.ai.action.switch_to_alt_sky": "AI switch_to_alt_sky action",
                            "scenario.ai.action.set_wind_speed": "AI set_wind_speed action",
                            "scenario.ai.action.set_lights": "AI set_lights action",
                            "scenario.ai.action.set_room_flag": "AI set_room_flag action",
                            "scenario.ai.action.show_cutscene_chrs": "AI show_cutscene_chrs action",
                            "scenario.ai.action.configure_environment": "AI configure_environment action",
                            "scenario.ai.condition.if_distance_to_target2_less_than": "AI if_distance_to_target2_less_than condition",
                            "scenario.ai.condition.if_distance_to_target2_greater_than": "AI if_distance_to_target2_greater_than condition",
                            "scenario.ai.action.speak": "AI speak action",
                            "scenario.ai.action.play_sound": "AI play_sound action",
                            "scenario.ai.action.assign_sound": "AI assign_sound action",
                            "scenario.ai.action.audio_mute_channel": "AI audio_mute_channel action",
                            "scenario.ai.condition.if_channel_free": "AI if_channel_free condition",
                            "scenario.ai.action.set_object_sound_volume": "AI set_object_sound_volume action",
                            "scenario.ai.action.set_object_sound_volume_by_distance": "AI set_object_sound_volume_by_distance action",
                            "scenario.ai.action.set_object_sound_playing": "AI set_object_sound_playing action",
                            "scenario.ai.action.play_repeating_sound_from_object": "AI play_repeating_sound_from_object action",
                            "scenario.ai.action.play_sound_from_entity": "AI play_sound_from_entity action",
                            "scenario.ai.action.play_repeating_sound_from_pad": "AI play_repeating_sound_from_pad action",
                            "scenario.ai.condition.if_object_sound_volume_less_than": "AI if_object_sound_volume_less_than condition",
                            "scenario.ai.action.play_sound_from_prop": "AI play_sound_from_prop action",
                            "scenario.ai.action.play_temporary_primary_track": "AI play_temporary_primary_track action",
                            "scenario.ai.action.play_x_track": "AI play_x_track action",
                            "scenario.ai.action.stop_x_track": "AI stop_x_track action",
                            "scenario.ai.action.play_track_isolated": "AI play_track_isolated action",
                            "scenario.ai.action.play_default_tracks": "AI play_default_tracks action",
                            "scenario.ai.action.play_cutscene_track": "AI play_cutscene_track action",
                            "scenario.ai.action.stop_cutscene_track": "AI stop_cutscene_track action",
                            "scenario.ai.action.play_temporary_track": "AI play_temporary_track action",
                            "scenario.ai.action.stop_ambient_track": "AI stop_ambient_track action",
                            "scenario.ai.action.chr_draw_weapon": "AI chr_draw_weapon action",
                            "scenario.ai.action.chr_draw_weapon_in_cutscene": "AI chr_draw_weapon_in_cutscene action",
                            "scenario.ai.action.set_player_force_speed": "AI set_player_force_speed action",
                            "scenario.ai.action.chr_set_invincible": "AI chr_set_invincible action",
                            "scenario.ai.condition.if_player_is_invincible": "AI if_player_is_invincible condition",
                            "scenario.ai.condition.if_chr_has_no_gun": "AI if_chr_has_no_gun condition",
                            "scenario.ai.action.chr_delete_weapon": "AI chr_delete_weapon action",
                            "scenario.ai.condition.if_trigger_shot_list": "AI if_trigger_shot_list condition",
                            "scenario.ai.action.end_level": "AI end_level action",
                            "scenario.ai.action.end_cutscene": "AI end_cutscene action",
                            "scenario.ai.action.warp_jo_to_pad": "AI warp_jo_to_pad action",
                            "scenario.ai.action.warp_jo_to_tag": "AI warp_jo_to_tag action",
                            "scenario.ai.action.revoke_control": "AI revoke_control action",
                            "scenario.ai.action.grant_control": "AI grant_control action",
                            "scenario.ai.action.player_fade_in": "AI player_fade_in action",
                            "scenario.ai.action.players_fade_out": "AI players_fade_out action",
                            "scenario.ai.condition.if_colour_fade_complete": "AI if_colour_fade_complete condition",
                            "scenario.ai.action.prepare_warp_orbit": "AI prepare_warp_orbit action",
                            "scenario.ai.action.begin_warp_latch": "AI begin_warp_latch action",
                            "scenario.ai.condition.if_warp_latch_complete": "AI if_warp_latch_complete condition",
                            "scenario.ai.action.set_camera_animation": "AI set_camera_animation action",
                            "scenario.ai.condition.if_in_cutscene": "AI if_in_cutscene condition",
                            "scenario.ai.condition.if_cutscene_button_pressed": "AI if_cutscene_button_pressed condition",
                            "scenario.ai.action.reorient_for_cutscene_stop": "AI reorient_for_cutscene_stop action",
                            "scenario.ai.action.spawn_chr_at_pad": "AI spawn_chr_at_pad action",
                            "scenario.ai.action.spawn_chr_at_chr": "AI spawn_chr_at_chr action",
                            "scenario.ai.action.try_equip_weapon": "AI try_equip_weapon action",
                            "scenario.ai.action.try_equip_hat": "AI try_equip_hat action",
                            "scenario.ai.action.set_obj_image": "AI set_obj_image action",
                            "scenario.ai.action.object_do_animation": "AI object_do_animation action",
                            "scenario.ai.action.set_door_open": "AI set_door_open action",
                            "scenario.ai.action.duplicate_chr": "AI duplicate_chr action",
                            "scenario.ai.action.enable_chr": "AI enable_chr action",
                            "scenario.ai.action.disable_chr": "AI disable_chr action",
                            "scenario.ai.action.enable_obj": "AI enable_obj action",
                            "scenario.ai.action.disable_obj": "AI disable_obj action",
                            "scenario.ai.action.chr_move_to_pad": "AI chr_move_to_pad action",
                            "scenario.ai.action.chr_set_team": "AI chr_set_team action",
                            "scenario.ai.action.damage_chr_by_amount": "AI damage_chr_by_amount action",
                            "scenario.ai.action.do_preset_animation": "AI do_preset_animation action",
                            "scenario.ai.condition.if_player_chr_portal_distance_less_than": "AI if_player_chr_portal_distance_less_than condition",
                            "scenario.ai.condition.if_chr_reposition_valid": "AI if_chr_reposition_valid condition",
                            "scenario.ai.action.do_gun_command": "AI do_gun_command action",
                            "scenario.ai.condition.if_distance_to_gun_less_than": "AI if_distance_to_gun_less_than condition",
                            "scenario.ai.action.recover_gun": "AI recover_gun action",
                            "scenario.ai.action.chr_copy_properties": "AI chr_copy_properties action",
                            "scenario.ai.action.player_auto_walk": "AI player_auto_walk action",
                            "scenario.ai.condition.if_player_auto_walk_finished": "AI if_player_auto_walk_finished condition",
                            "scenario.ai.condition.if_obj_in_room": "AI if_obj_in_room condition",
                            "scenario.ai.condition.if_player_looking_at_object": "AI if_player_looking_at_object condition",
                            "scenario.ai.condition.if_target_is_player": "AI if_target_is_player condition",
                            "scenario.ai.action.chr_kill": "AI chr_kill action",
                            "scenario.ai.action.remove_weapon_from_inventory": "AI remove_weapon_from_inventory action",
                            "scenario.ai.action.clear_inventory": "AI clear_inventory action",
                            "scenario.ai.action.release_object": "AI release_object action",
                            "scenario.ai.action.chr_grab_object": "AI chr_grab_object action",
                            "scenario.ai.action.toggle_p1p2": "AI toggle_p1p2 action",
                            "scenario.ai.action.chr_set_p1p2": "AI chr_set_p1p2 action",
                            "scenario.ai.action.chr_set_cloaked": "AI chr_set_cloaked action",
                            "scenario.ai.action.set_autogun_target_team": "AI set_autogun_target_team action",
                            "scenario.ai.condition.if_objective_complete": "AI if_objective_complete condition",
                            "scenario.ai.condition.if_objective_failed": "AI if_objective_failed condition",
                            "scenario.ai.condition.if_all_objectives_complete": "AI if_all_objectives_complete condition",
                            "scenario.ai.condition.if_difficulty_less_than": "AI if_difficulty_less_than condition",
                            "scenario.ai.condition.if_difficulty_greater_than": "AI if_difficulty_greater_than condition",
                            "scenario.ai.condition.if_stage_timer_less_than": "AI if_stage_timer_less_than condition",
                            "scenario.ai.condition.if_stage_timer_greater_than": "AI if_stage_timer_greater_than condition",
                            "scenario.ai.condition.if_stage_id_less_than": "AI if_stage_id_less_than condition",
                            "scenario.ai.condition.if_stage_id_greater_than": "AI if_stage_id_greater_than condition",
                            "scenario.ai.condition.if_num_players_less_than": "AI if_num_players_less_than condition",
                            "scenario.ai.condition.if_kill_count_greater_than": "AI if_kill_count_greater_than condition",
                            "scenario.ai.condition.if_num_knocked_out_chrs": "AI if_num_knocked_out_chrs condition",
                            "scenario.ai.action.kill_bond": "AI kill_bond action",
                            "scenario.ai.condition.if_num_arghs_less_than": "AI if_num_arghs_less_than condition",
                            "scenario.ai.condition.if_num_arghs_greater_than": "AI if_num_arghs_greater_than condition",
                            "scenario.ai.condition.if_num_close_arghs_less_than": "AI if_num_close_arghs_less_than condition",
                            "scenario.ai.condition.if_num_close_arghs_greater_than": "AI if_num_close_arghs_greater_than condition",
                            "scenario.ai.condition.if_chr_health_greater_than": "AI if_chr_health_greater_than condition",
                            "scenario.ai.condition.if_chr_health_less_than": "AI if_chr_health_less_than condition",
                            "scenario.ai.condition.if_chr_shield_less_than": "AI if_chr_shield_less_than condition",
                            "scenario.ai.condition.if_chr_shield_greater_than": "AI if_chr_shield_greater_than condition",
                            "scenario.ai.condition.if_injured": "AI if_injured condition",
                            "scenario.ai.condition.if_shield_damaged": "AI if_shield_damaged condition",
                            "scenario.ai.condition.if_morale_less_than": "AI if_morale_less_than condition",
                            "scenario.ai.condition.if_morale_less_than_random": "AI if_morale_less_than_random condition",
                            "scenario.ai.condition.if_alertness": "AI if_alertness condition",
                            "scenario.ai.condition.if_chr_alertness_less_than": "AI if_chr_alertness_less_than condition",
                            "scenario.ai.condition.if_alertness_less_than_random": "AI if_alertness_less_than_random condition",
                            "scenario.ai.condition.if_idle": "AI if_idle condition",
                            "scenario.ai.condition.if_stopped": "AI if_stopped condition",
                            "scenario.ai.condition.if_chr_dead": "AI if_chr_dead condition",
                            "scenario.ai.condition.if_chr_death_animation_finished": "AI if_chr_death_animation_finished condition",
                            "scenario.ai.condition.if_chr_knocked_out": "AI if_chr_knocked_out condition",
                            "scenario.ai.condition.if_can_see_target": "AI if_can_see_target condition",
                            "scenario.ai.condition.if_pouncebits_eq": "AI if_pouncebits_eq condition",
                            "scenario.ai.condition.if_training_pc_holographed": "AI if_training_pc_holographed condition",
                            "scenario.ai.condition.if_player_using_device": "AI if_player_using_device condition",
                            "scenario.ai.action.chr_begin_or_end_teleport": "AI chr_begin_or_end_teleport action",
                            "scenario.ai.condition.if_chr_teleport_full_white": "AI if_chr_teleport_full_white condition",
                            "scenario.ai.action.chr_set_cutscene_weapon": "AI chr_set_cutscene_weapon action",
                            "scenario.ai.action.fade_screen": "AI fade_screen action",
                            "scenario.ai.condition.if_fade_complete": "AI if_fade_complete condition",
                            "scenario.ai.action.set_chr_hudpiece_visible": "AI set_chr_hudpiece_visible action",
                            "scenario.ai.action.set_passive_mode": "AI set_passive_mode action",
                            "scenario.ai.action.chr_set_firing_in_cutscene": "AI chr_set_firing_in_cutscene action",
                            "scenario.ai.action.set_portal_flag": "AI set_portal_flag action",
                            "scenario.ai.condition.if_music_event_queue_is_empty": "AI if_music_event_queue_is_empty condition",
                            "scenario.ai.condition.if_coop_mode": "AI if_coop_mode condition",
                            "scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than": "AI if_chr_same_floor_distance_to_pad_less_than condition",
                            "scenario.ai.action.remove_references_to_chr": "AI remove_references_to_chr action",
                            "scenario.ai.action.chr_toggle_model_part": "AI chr_toggle_model_part action",
                            "scenario.ai.action.obj_set_model_part_visible": "AI obj_set_model_part_visible action",
                            "scenario.ai.action.if_obj_health_less_than": "AI if_obj_health_less_than action",
                            "scenario.ai.action.set_obj_health": "AI set_obj_health action",
                            "scenario.ai.action.set_chr_special_death_animation": "AI set_chr_special_death_animation action",
                            "scenario.ai.action.set_room_to_search": "AI set_room_to_search action",
                            "scenario.ai.action.set_savefile_flag": "AI set_savefile_flag action",
                            "scenario.ai.action.unset_savefile_flag": "AI unset_savefile_flag action",
                            "scenario.ai.action.if_savefile_flag_set": "AI if_savefile_flag_set action",
                            "scenario.ai.action.if_savefile_flag_unset": "AI if_savefile_flag_unset action",
                            "scenario.ai.action.restart_timer": "AI restart_timer action",
                            "scenario.ai.action.reset_timer": "AI reset_timer action",
                            "scenario.ai.action.pause_timer": "AI pause_timer action",
                            "scenario.ai.action.resume_timer": "AI resume_timer action",
                            "scenario.ai.action.if_timer_stopped": "AI if_timer_stopped action",
                            "scenario.ai.action.if_timer_greater_than_random": "AI if_timer_greater_than_random action",
                            "scenario.ai.action.if_timer_less_than": "AI if_timer_less_than action",
                            "scenario.ai.action.if_timer_greater_than": "AI if_timer_greater_than action",
                            "scenario.ai.action.show_countdown_timer": "AI show_countdown_timer action",
                            "scenario.ai.action.hide_countdown_timer": "AI hide_countdown_timer action",
                            "scenario.ai.action.set_countdown_timer": "AI set_countdown_timer action",
                            "scenario.ai.action.stop_countdown_timer": "AI stop_countdown_timer action",
                            "scenario.ai.action.start_countdown_timer": "AI start_countdown_timer action",
                            "scenario.ai.action.if_countdown_timer_stopped": "AI if_countdown_timer_stopped action",
                            "scenario.ai.action.if_countdown_timer_less_than": "AI if_countdown_timer_less_than action",
                            "scenario.ai.action.if_countdown_timer_greater_than": "AI if_countdown_timer_greater_than action",
                            "scenario.ai.action.show_hudmsg": "AI show_hudmsg action",
                            "scenario.ai.action.show_hudmsg_middle": "AI show_hudmsg_middle action",
                            "scenario.ai.action.show_hudmsg_top_middle": "AI show_hudmsg_top_middle action",
                            "scenario.ai.action.hovercar_begin_path": "AI hovercar_begin_path action",
                            "scenario.ai.action.set_vehicle_speed": "AI set_vehicle_speed action",
                            "scenario.ai.action.set_rotor_speed": "AI set_rotor_speed action",
                            "scenario.ai.action.chr_explosions": "AI chr_explosions action",
                            "scenario.ai.action.set_tinted_glass_enabled": "AI set_tinted_glass_enabled action",
                            "scenario.ai.action.hovercopter_fire_rocket": "AI hovercopter_fire_rocket action",
                            "scenario.ai.action.chr_adjust_motion_blur": "AI chr_adjust_motion_blur action",
                            "scenario.ai.action.punch_or_kick": "AI punch_or_kick action",
                            "scenario.ai.action.set_target_to_eyespy_if_in_sight": "AI set_target_to_eyespy_if_in_sight action",
                            "scenario.ai.action.mini_skedar_try_pounce": "AI mini_skedar_try_pounce action",
                            "scenario.ai.condition.if_object_distance_to_pad_less_than": "AI if_object_distance_to_pad_less_than condition",
                            "scenario.ai.action.avoid": "AI avoid action",
                            "scenario.ai.action.title_init_mode": "AI title_init_mode action",
                            "scenario.ai.action.try_exit_title": "AI try_exit_title action",
                            "scenario.ai.action.chr_emit_sparks": "AI chr_emit_sparks action",
                            "scenario.ai.action.set_dr_caroll_images": "AI set_dr_caroll_images action",
                            "scenario.ai.action.say_quip": "AI say_quip action",
                            "scenario.ai.action.say_ci_staff_quip": "AI say_ci_staff_quip action",
                            "scenario.ai.action.shuffle_ruins_pillars": "AI shuffle_ruins_pillars action",
                            "scenario.ai.action.shuffle_pelagic_switches": "AI shuffle_pelagic_switches action",
                            "scenario.ai.action.set_action": "AI set_action action",
                            "scenario.ai.action.set_team_orders": "AI set_team_orders action",
                            "scenario.ai.action.retreat": "AI retreat action",
                            "scenario.ai.action.find_cover": "AI find_cover action",
                            "scenario.ai.action.find_cover_within_dist": "AI find_cover_within_dist action",
                            "scenario.ai.action.find_cover_outside_dist": "AI find_cover_outside_dist action",
                            "scenario.ai.action.go_to_cover": "AI go_to_cover action",
                            "scenario.ai.action.check_cover_out_of_sight": "AI check_cover_out_of_sight action",
                            "scenario.ai.action.orbit_target": "AI orbit_target action",
                            "scenario.ai.action.set_chr_preset_to_unalerted_teammate": "AI set_chr_preset_to_unalerted_teammate action",
                            "scenario.ai.action.set_squadron": "AI set_squadron action",
                            "scenario.ai.action.face_cover": "AI face_cover action",
                            "scenario.ai.action.danger_cover": "AI danger_cover action",
                            "scenario.ai.action.release_cover": "AI release_cover action",
                            "scenario.ai.action.rebuild_teams": "AI rebuild_teams action",
                            "scenario.ai.action.rebuild_squadrons": "AI rebuild_squadrons action",
                            "scenario.ai.action.chr_set_listening": "AI chr_set_listening action",
                            "scenario.ai.condition.if_chr_not_talking": "AI if_chr_not_talking condition",
                            "scenario.ai.condition.if_orders": "AI if_orders condition",
                            "scenario.ai.condition.if_has_orders": "AI if_has_orders condition",
                            "scenario.ai.condition.if_chr_in_squadron_doing_action": "AI if_chr_in_squadron_doing_action condition",
                            "scenario.ai.condition.if_chr_listening": "AI if_chr_listening condition",
                            "scenario.ai.condition.if_not_listening": "AI if_not_listening condition",
                            "scenario.ai.condition.if_chr_injured_target": "AI if_chr_injured_target condition",
                            "scenario.ai.condition.if_action": "AI if_action condition",
                            "scenario.ai.condition.if_chr_ammo_quantity_less_than": "AI if_chr_ammo_quantity_less_than condition",
                            "scenario.ai.condition.if_chr_target": "AI if_chr_target condition",
                            "scenario.ai.condition.if_compare_chr_presets_team": "AI if_compare_chr_presets_team condition",
                            "scenario.ai.condition.if_human": "AI if_human condition",
                            "scenario.ai.condition.if_skedar": "AI if_skedar condition",
                            "scenario.ai.condition.if_prop_preset_blocking_sight_to_target": "AI if_prop_preset_blocking_sight_to_target condition",
                            "scenario.ai.action.remove_object_at_prop_preset": "AI remove_object_at_prop_preset action",
                            "scenario.ai.condition.if_prop_preset_height_less_than": "AI if_prop_preset_height_less_than condition",
                            "scenario.ai.action.set_target": "AI set_target action",
                            "scenario.ai.condition.if_presets_target_is_not_my_target": "AI if_presets_target_is_not_my_target condition",
                            "scenario.ai.action.set_chr_preset_to_chr_near_self": "AI set_chr_preset_to_chr_near_self action",
                            "scenario.ai.action.set_chr_preset_to_chr_near_pad": "AI set_chr_preset_to_chr_near_pad action",
                            "scenario.ai.condition.if_dangerous_object_nearby": "AI if_dangerous_object_nearby condition",
                            "scenario.ai.condition.if_heli_weapons_armed": "AI if_heli_weapons_armed condition",
                            "scenario.ai.condition.if_hoverbot_next_step": "AI if_hoverbot_next_step condition",
                            "scenario.ai.action.shuffle_investigation_terminals": "AI shuffle_investigation_terminals action",
                            "scenario.ai.action.set_pad_preset_to_investigation_terminal": "AI set_pad_preset_to_investigation_terminal action",
                            "scenario.ai.action.heli_arm_weapons": "AI heli_arm_weapons action",
                            "scenario.ai.action.heli_unarm_weapons": "AI heli_unarm_weapons action",
                            "scenario.ai.condition.if_safety2_less_than": "AI if_safety2_less_than condition",
                            "scenario.ai.condition.if_player_using_cmp_or_ar34": "AI if_player_using_cmp_or_ar34 condition",
                            "scenario.ai.condition.detect_enemy_on_same_floor": "AI detect_enemy_on_same_floor condition",
                            "scenario.ai.condition.detect_enemy": "AI detect_enemy condition",
                            "scenario.ai.condition.if_safety_less_than": "AI if_safety_less_than condition",
                            "scenario.ai.condition.if_target_moving_slowly": "AI if_target_moving_slowly condition",
                            "scenario.ai.condition.if_target_moving_closer": "AI if_target_moving_closer condition",
                            "scenario.ai.condition.if_target_moving_away": "AI if_target_moving_away condition",
                            "scenario.ai.condition.if_squadron_is_dead": "AI if_squadron_is_dead condition",
                            "scenario.ai.condition.if_true": "AI if_true condition",
                            "scenario.ai.condition.if_num_chrs_in_squadron_greater_than": "AI if_num_chrs_in_squadron_greater_than condition",
                            "scenario.ai.condition.if_natural_anim": "AI if_natural_anim condition",
                            "scenario.ai.condition.if_y": "AI if_y condition",
                            "scenario.ai.condition.if_sound_timer": "AI if_sound_timer condition",
                            "scenario.ai.condition.if_target_y_difference_less_than": "AI if_target_y_difference_less_than condition",
                            "scenario.ai.action.try_attack_amount": "AI try_attack_amount action",
                            "scenario.ai.action.set_chr_preset": "AI set_chr_preset action",
                            "scenario.ai.action.set_chr_target": "AI set_chr_target action",
                            "scenario.ai.action.set_pad_preset": "AI set_pad_preset action",
                            "scenario.ai.action.chr_set_pad_preset": "AI chr_set_pad_preset action",
                            "scenario.ai.action.chr_copy_pad_preset": "AI chr_copy_pad_preset action",
                        }.items():
                            if node_kinds.count(required_kind) != 1:
                                result.errors.append(
                                    f"{label} level.graph.json must include exactly one {description} graph node"
                                )
                        links = graph.get("links")
                        link_pairs = set()
                        if isinstance(links, list):
                            for link in links:
                                if isinstance(link, dict):
                                    link_pairs.add(
                                        (
                                            str(link.get("from", "")),
                                            str(link.get("to", "")),
                                        )
                                    )
                        for pair in {
                            ("scenario.load", "scenario.pads"),
                            ("scenario.ai.lists", "scenario.ai.action.set_list"),
                            ("scenario.ai.lists", "scenario.ai.action.set_return_list"),
                            ("scenario.ai.lists", "scenario.ai.action.set_shot_list"),
                            ("scenario.ai.lists", "scenario.ai.action.return_list"),
                            ("scenario.ai.lists", "scenario.ai.action.stop"),
                            ("scenario.ai.lists", "scenario.ai.action.kneel"),
                            ("scenario.ai.lists", "scenario.ai.action.surrender"),
                            ("scenario.ai.lists", "scenario.ai.action.fade_out"),
                            ("scenario.ai.lists", "scenario.ai.action.remove_chr"),
                            ("scenario.ai.lists", "scenario.ai.action.try_sidestep"),
                            ("scenario.ai.lists", "scenario.ai.action.try_jump_out"),
                            ("scenario.ai.lists", "scenario.ai.action.try_run_sideways"),
                            ("scenario.ai.lists", "scenario.ai.action.try_attack_walk"),
                            ("scenario.ai.lists", "scenario.ai.action.try_attack_run"),
                            ("scenario.ai.lists", "scenario.ai.action.try_attack_roll"),
                            ("scenario.ai.lists", "scenario.ai.action.try_attack_stand"),
                            ("scenario.ai.lists", "scenario.ai.action.try_attack_kneel"),
                            ("scenario.ai.lists", "scenario.ai.action.try_attack_lie"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_attack_locked"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_attacking"),
                            ("scenario.ai.lists", "scenario.ai.action.try_modify_attack"),
                            ("scenario.ai.lists", "scenario.ai.action.face_entity"),
                            ("scenario.ai.lists", "scenario.ai.action.apply_gset_damage"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_damage_chr"),
                            ("scenario.ai.lists", "scenario.ai.condition.consider_grenade_throw"),
                            ("scenario.ai.lists", "scenario.ai.action.drop_item"),
                            ("scenario.ai.lists", "scenario.ai.action.try_run_from_target"),
                            ("scenario.ai.lists", "scenario.ai.action.try_jog_to_target_prop"),
                            ("scenario.ai.lists", "scenario.ai.action.try_walk_to_target_prop"),
                            ("scenario.ai.lists", "scenario.ai.action.try_run_to_target_prop"),
                            ("scenario.ai.lists", "scenario.ai.action.try_go_to_cover_prop"),
                            ("scenario.ai.lists", "scenario.ai.action.try_jog_to_chr"),
                            ("scenario.ai.lists", "scenario.ai.action.try_walk_to_chr"),
                            ("scenario.ai.lists", "scenario.ai.action.try_run_to_chr"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_can_hear_alarm"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_patrolling"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_alarm_active"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_gas_active"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_hears_target"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_saw_injury"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_saw_death"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_los_to_target"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_los_to_attack_target"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_target_nearly_in_sight"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_nearly_in_targets_sight"),
                            ("scenario.ai.lists", "scenario.ai.action.set_pad_preset_to_pad_on_route_to_target"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_saw_target_recently"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_heard_target_recently"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_los_to_chr"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_never_been_on_screen"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_on_screen"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_in_on_screen_room"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_room_is_on_screen"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_target_aiming_at_me"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_near_miss"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_sees_suspicious_item"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_target_in_fov_left"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_check_fov_with_target"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_target_out_of_fov_left"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_target_in_fov"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_target_out_of_fov"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_distance_to_target_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_distance_to_target_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_distance_to_pad_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_distance_to_pad_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_distance_to_chr_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_distance_to_chr_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_any_chr_near_self"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_distance_from_target_to_pad_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_distance_from_target_to_pad_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_in_room"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_target_in_room"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_has_object"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_weapon_thrown"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_weapon_thrown_on_object"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_has_weapon_equipped"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_gun_unclaimed"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_object_healthy"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_activated_object"),
                            ("scenario.ai.lists", "scenario.ai.action.obj_interact"),
                            ("scenario.ai.lists", "scenario.ai.action.destroy_object"),
                            ("scenario.ai.lists", "scenario.ai.action.drop_object_from_chr"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_drop_items"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_drop_weapon"),
                            ("scenario.ai.lists", "scenario.ai.action.give_object_to_chr"),
                            ("scenario.ai.lists", "scenario.ai.action.object_move_to_pad"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_waypoint_within_quadrant"),
                            ("scenario.ai.lists", "scenario.ai.action.set_pad_preset_to_target_quadrant"),
                            ("scenario.pads", "scenario.ai.condition.if_room_is_on_screen"),
                            ("scenario.pads", "scenario.ai.condition.if_chr_distance_to_pad_less_than"),
                            ("scenario.pads", "scenario.ai.condition.if_chr_distance_to_pad_greater_than"),
                            ("scenario.pads", "scenario.ai.condition.if_distance_from_target_to_pad_less_than"),
                            ("scenario.pads", "scenario.ai.condition.if_distance_from_target_to_pad_greater_than"),
                            ("scenario.pads", "scenario.ai.condition.if_chr_in_room"),
                            ("scenario.pads", "scenario.ai.condition.if_target_in_room"),
                            ("setup.tables", "scenario.ai.condition.if_sees_suspicious_item"),
                            ("setup.tables", "scenario.ai.condition.if_chr_has_object"),
                            ("setup.tables", "scenario.ai.condition.if_weapon_thrown_on_object"),
                            ("setup.tables", "scenario.ai.condition.if_gun_unclaimed"),
                            ("setup.tables", "scenario.ai.condition.if_object_healthy"),
                            ("scenario.pads", "scenario.ai.action.object_move_to_pad"),
                            ("setup.tables", "scenario.ai.condition.if_chr_activated_object"),
                            ("setup.tables", "scenario.ai.action.obj_interact"),
                            ("setup.tables", "scenario.ai.action.destroy_object"),
                            ("setup.tables", "scenario.ai.action.drop_object_from_chr"),
                            ("setup.tables", "scenario.ai.action.chr_drop_items"),
                            ("setup.tables", "scenario.ai.action.chr_drop_weapon"),
                            ("setup.tables", "scenario.ai.action.give_object_to_chr"),
                            ("setup.tables", "scenario.ai.action.object_move_to_pad"),
                            ("scenario.pads", "scenario.ai.condition.if_waypoint_within_quadrant"),
                            ("scenario.pads", "scenario.ai.action.set_pad_preset_to_target_quadrant"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_do_animation"),
                            ("scenario.ai.lists", "scenario.ai.action.be_surprised_one_hand"),
                            ("scenario.ai.lists", "scenario.ai.action.be_surprised_look_around"),
                            ("scenario.ai.lists", "scenario.ai.action.be_surprised_surrender"),
                            ("scenario.ai.lists", "scenario.ai.action.random"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_random_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_random_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.action.set_punch_dodge_list"),
                            ("scenario.ai.lists", "scenario.ai.action.set_shooting_at_me_list"),
                            ("scenario.ai.lists", "scenario.ai.action.set_dark_room_list"),
                            ("scenario.ai.lists", "scenario.ai.action.set_player_dead_list"),
                            ("scenario.ai.lists", "scenario.ai.action.jog_to_pad"),
                            ("scenario.ai.lists", "scenario.ai.action.go_to_pad_preset"),
                            ("scenario.ai.lists", "scenario.ai.action.walk_to_pad"),
                            ("scenario.ai.lists", "scenario.ai.action.run_to_pad"),
                            ("scenario.ai.lists", "scenario.ai.action.set_path"),
                            ("scenario.ai.lists", "scenario.ai.action.start_patrol"),
                            ("scenario.ai.lists", "scenario.ai.action.try_start_alarm"),
                            ("scenario.ai.lists", "scenario.ai.action.activate_alarm"),
                            ("scenario.ai.lists", "scenario.ai.action.deactivate_alarm"),
                            ("scenario.ai.lists", "scenario.ai.action.set_morale"),
                            ("scenario.ai.lists", "scenario.ai.action.add_morale"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_add_morale"),
                            ("scenario.ai.lists", "scenario.ai.action.subtract_morale"),
                            ("scenario.ai.lists", "scenario.ai.action.set_alertness"),
                            ("scenario.ai.lists", "scenario.ai.action.add_alertness"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_add_alertness"),
                            ("scenario.ai.lists", "scenario.ai.action.subtract_alertness"),
                            ("scenario.ai.lists", "scenario.ai.action.increase_squadron_alertness"),
                            ("scenario.ai.lists", "scenario.ai.action.set_hear_distance"),
                            ("scenario.ai.lists", "scenario.ai.action.set_view_distance"),
                            ("scenario.ai.lists", "scenario.ai.action.set_grenade_probability"),
                            ("scenario.ai.lists", "scenario.ai.action.set_chr_num"),
                            ("scenario.ai.lists", "scenario.ai.action.set_max_damage"),
                            ("scenario.ai.lists", "scenario.ai.action.add_health"),
                            ("scenario.ai.lists", "scenario.ai.action.set_shield"),
                            ("scenario.ai.lists", "scenario.ai.action.set_reaction_speed"),
                            ("scenario.ai.lists", "scenario.ai.action.set_recovery_speed"),
                            ("scenario.ai.lists", "scenario.ai.action.set_accuracy"),
                            ("scenario.ai.lists", "scenario.ai.action.set_dodge_rating"),
                            ("scenario.ai.lists", "scenario.ai.action.set_unarmed_dodge_rating"),
                            ("scenario.ai.lists", "scenario.ai.action.set_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.unset_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.if_has_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_set_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_unset_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.if_chr_has_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.set_stage_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.unset_stage_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.if_stage_flag_eq"),
                            ("scenario.ai.lists", "scenario.ai.action.set_chrflag"),
                            ("scenario.ai.lists", "scenario.ai.action.unset_chrflag"),
                            ("scenario.ai.lists", "scenario.ai.action.if_has_chrflag"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_set_chrflag"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_unset_chrflag"),
                            ("scenario.ai.lists", "scenario.ai.action.if_chr_has_chrflag"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_set_hidden_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_unset_hidden_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.if_chr_has_hidden_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.set_obj_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.unset_obj_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.if_obj_has_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.open_door"),
                            ("scenario.ai.lists", "scenario.ai.action.close_door"),
                            ("scenario.ai.lists", "scenario.ai.action.if_door_state"),
                            ("scenario.ai.lists", "scenario.ai.action.if_object_is_door"),
                            ("scenario.ai.lists", "scenario.ai.action.lock_door"),
                            ("scenario.ai.lists", "scenario.ai.action.unlock_door"),
                            ("scenario.ai.lists", "scenario.ai.action.if_door_locked"),
                            ("scenario.ai.lists", "scenario.ai.action.if_lift_stationary"),
                            ("scenario.ai.lists", "scenario.ai.action.lift_go_to_stop"),
                            ("scenario.ai.lists", "scenario.ai.action.if_lift_at_stop"),
                            ("scenario.ai.lists", "scenario.ai.action.activate_lift"),
                            ("scenario.ai.lists", "scenario.ai.action.if_using_lift"),
                            ("scenario.ai.lists", "scenario.ai.action.configure_rain"),
                            ("scenario.ai.lists", "scenario.ai.action.configure_snow"),
                            ("scenario.ai.lists", "scenario.ai.action.switch_to_alt_sky"),
                            ("scenario.ai.lists", "scenario.ai.action.set_wind_speed"),
                            ("scenario.ai.lists", "scenario.ai.action.set_lights"),
                            ("scenario.ai.lists", "scenario.ai.action.set_room_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.show_cutscene_chrs"),
                            ("scenario.ai.lists", "scenario.ai.action.configure_environment"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_distance_to_target2_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_distance_to_target2_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.action.speak"),
                            ("scenario.ai.lists", "scenario.ai.action.play_sound"),
                            ("scenario.ai.lists", "scenario.ai.action.assign_sound"),
                            ("scenario.ai.lists", "scenario.ai.action.audio_mute_channel"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_channel_free"),
                            ("scenario.ai.lists", "scenario.ai.action.set_object_sound_volume"),
                            ("scenario.ai.lists", "scenario.ai.action.set_object_sound_volume_by_distance"),
                            ("scenario.ai.lists", "scenario.ai.action.set_object_sound_playing"),
                            ("scenario.ai.lists", "scenario.ai.action.play_repeating_sound_from_object"),
                            ("scenario.ai.lists", "scenario.ai.action.play_sound_from_entity"),
                            ("scenario.ai.lists", "scenario.ai.action.play_repeating_sound_from_pad"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_object_sound_volume_less_than"),
                            ("scenario.ai.lists", "scenario.ai.action.play_sound_from_prop"),
                            ("scenario.ai.lists", "scenario.ai.action.play_temporary_primary_track"),
                            ("scenario.ai.lists", "scenario.ai.action.play_x_track"),
                            ("scenario.ai.lists", "scenario.ai.action.stop_x_track"),
                            ("scenario.ai.lists", "scenario.ai.action.play_track_isolated"),
                            ("scenario.ai.lists", "scenario.ai.action.play_default_tracks"),
                            ("scenario.ai.lists", "scenario.ai.action.play_cutscene_track"),
                            ("scenario.ai.lists", "scenario.ai.action.stop_cutscene_track"),
                            ("scenario.ai.lists", "scenario.ai.action.play_temporary_track"),
                            ("scenario.ai.lists", "scenario.ai.action.stop_ambient_track"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_draw_weapon"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_draw_weapon_in_cutscene"),
                            ("scenario.ai.lists", "scenario.ai.action.set_player_force_speed"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_set_invincible"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_player_is_invincible"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_has_no_gun"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_delete_weapon"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_trigger_shot_list"),
                            ("scenario.ai.lists", "scenario.ai.action.end_level"),
                            ("scenario.ai.lists", "scenario.ai.action.end_cutscene"),
                            ("scenario.ai.lists", "scenario.ai.action.warp_jo_to_pad"),
                            ("scenario.pads", "scenario.ai.action.warp_jo_to_pad"),
                            ("scenario.ai.lists", "scenario.ai.action.warp_jo_to_tag"),
                            ("setup.tables", "scenario.ai.action.warp_jo_to_tag"),
                            ("scenario.ai.lists", "scenario.ai.action.revoke_control"),
                            ("scenario.ai.lists", "scenario.ai.action.grant_control"),
                            ("scenario.ai.lists", "scenario.ai.action.player_fade_in"),
                            ("scenario.ai.lists", "scenario.ai.action.players_fade_out"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_colour_fade_complete"),
                            ("scenario.ai.lists", "scenario.ai.action.prepare_warp_orbit"),
                            ("scenario.pads", "scenario.ai.action.prepare_warp_orbit"),
                            ("scenario.ai.lists", "scenario.ai.action.begin_warp_latch"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_warp_latch_complete"),
                            ("scenario.ai.lists", "scenario.ai.action.set_camera_animation"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_in_cutscene"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_cutscene_button_pressed"),
                            ("scenario.ai.lists", "scenario.ai.action.reorient_for_cutscene_stop"),
                            ("scenario.ai.lists", "scenario.ai.action.spawn_chr_at_pad"),
                            ("scenario.pads", "scenario.ai.action.spawn_chr_at_pad"),
                            ("scenario.ai.lists", "scenario.ai.action.spawn_chr_at_chr"),
                            ("scenario.ai.lists", "scenario.ai.action.try_equip_weapon"),
                            ("scenario.ai.lists", "scenario.ai.action.try_equip_hat"),
                            ("scenario.ai.lists", "scenario.ai.action.set_obj_image"),
                            ("setup.tables", "scenario.ai.action.set_obj_image"),
                            ("scenario.ai.lists", "scenario.ai.action.object_do_animation"),
                            ("setup.tables", "scenario.ai.action.object_do_animation"),
                            ("scenario.ai.lists", "scenario.ai.action.set_door_open"),
                            ("setup.tables", "scenario.ai.action.set_door_open"),
                            ("scenario.ai.lists", "scenario.ai.action.duplicate_chr"),
                            ("scenario.ai.lists", "scenario.ai.action.enable_chr"),
                            ("scenario.ai.lists", "scenario.ai.action.disable_chr"),
                            ("scenario.ai.lists", "scenario.ai.action.enable_obj"),
                            ("setup.tables", "scenario.ai.action.enable_obj"),
                            ("scenario.ai.lists", "scenario.ai.action.disable_obj"),
                            ("setup.tables", "scenario.ai.action.disable_obj"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_move_to_pad"),
                            ("scenario.pads", "scenario.ai.action.chr_move_to_pad"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_set_team"),
                            ("scenario.ai.lists", "scenario.ai.action.damage_chr_by_amount"),
                            ("scenario.ai.lists", "scenario.ai.action.do_preset_animation"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_player_chr_portal_distance_less_than"),
                            ("source.scene", "scenario.ai.condition.if_player_chr_portal_distance_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_reposition_valid"),
                            ("source.scene", "scenario.ai.condition.if_chr_reposition_valid"),
                            ("scenario.ai.lists", "scenario.ai.action.do_gun_command"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_distance_to_gun_less_than"),
                            ("scenario.ai.lists", "scenario.ai.action.recover_gun"),
                            ("setup.tables", "scenario.ai.action.do_gun_command"),
                            ("setup.tables", "scenario.ai.condition.if_distance_to_gun_less_than"),
                            ("setup.tables", "scenario.ai.action.recover_gun"),
                            ("source.scene", "scenario.ai.action.do_gun_command"),
                            ("source.scene", "scenario.ai.condition.if_distance_to_gun_less_than"),
                            ("source.scene", "scenario.ai.action.recover_gun"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_copy_properties"),
                            ("scenario.ai.lists", "scenario.ai.action.player_auto_walk"),
                            ("scenario.pads", "scenario.ai.action.player_auto_walk"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_player_auto_walk_finished"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_obj_in_room"),
                            ("setup.tables", "scenario.ai.condition.if_obj_in_room"),
                            ("scenario.pads", "scenario.ai.condition.if_obj_in_room"),
                            ("source.scene", "scenario.ai.condition.if_obj_in_room"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_player_looking_at_object"),
                            ("setup.tables", "scenario.ai.condition.if_player_looking_at_object"),
                            ("source.scene", "scenario.ai.condition.if_player_looking_at_object"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_target_is_player"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_kill"),
                            ("scenario.ai.lists", "scenario.ai.action.remove_weapon_from_inventory"),
                            ("scenario.ai.lists", "scenario.ai.action.clear_inventory"),
                            ("scenario.ai.lists", "scenario.ai.action.release_object"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_grab_object"),
                            ("setup.tables", "scenario.ai.action.chr_grab_object"),
                            ("source.scene", "scenario.ai.action.chr_grab_object"),
                            ("scenario.ai.lists", "scenario.ai.action.toggle_p1p2"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_set_p1p2"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_set_cloaked"),
                            ("scenario.ai.lists", "scenario.ai.action.set_autogun_target_team"),
                            ("setup.tables", "scenario.ai.action.set_autogun_target_team"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_objective_complete"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_objective_failed"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_all_objectives_complete"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_difficulty_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_difficulty_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_stage_timer_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_stage_timer_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_stage_id_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_stage_id_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_num_players_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_kill_count_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_num_knocked_out_chrs"),
                            ("scenario.ai.lists", "scenario.ai.action.kill_bond"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_num_arghs_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_num_arghs_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_num_close_arghs_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_num_close_arghs_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_health_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_health_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_shield_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_shield_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_injured"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_shield_damaged"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_morale_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_morale_less_than_random"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_alertness"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_alertness_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_alertness_less_than_random"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_idle"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_stopped"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_dead"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_death_animation_finished"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_knocked_out"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_can_see_target"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_pouncebits_eq"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_training_pc_holographed"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_player_using_device"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_begin_or_end_teleport"),
                            ("scenario.pads", "scenario.ai.action.chr_begin_or_end_teleport"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_teleport_full_white"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_set_cutscene_weapon"),
                            ("scenario.ai.lists", "scenario.ai.action.fade_screen"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_fade_complete"),
                            ("scenario.ai.lists", "scenario.ai.action.set_chr_hudpiece_visible"),
                            ("scenario.ai.lists", "scenario.ai.action.set_passive_mode"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_set_firing_in_cutscene"),
                            ("scenario.ai.lists", "scenario.ai.action.set_portal_flag"),
                            ("source.scene", "scenario.ai.action.set_portal_flag"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_music_event_queue_is_empty"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_coop_mode"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than"),
                            ("scenario.pads", "scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than"),
                            ("scenario.ai.lists", "scenario.ai.action.remove_references_to_chr"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_toggle_model_part"),
                            ("scenario.ai.lists", "scenario.ai.action.obj_set_model_part_visible"),
                            ("scenario.ai.lists", "scenario.ai.action.if_obj_health_less_than"),
                            ("scenario.ai.lists", "scenario.ai.action.set_obj_health"),
                            ("scenario.ai.lists", "scenario.ai.action.set_chr_special_death_animation"),
                            ("scenario.ai.lists", "scenario.ai.action.set_room_to_search"),
                            ("scenario.ai.lists", "scenario.ai.action.set_savefile_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.unset_savefile_flag"),
                            ("scenario.ai.lists", "scenario.ai.action.if_savefile_flag_set"),
                            ("scenario.ai.lists", "scenario.ai.action.if_savefile_flag_unset"),
                            ("scenario.ai.lists", "scenario.ai.action.restart_timer"),
                            ("scenario.ai.lists", "scenario.ai.action.reset_timer"),
                            ("scenario.ai.lists", "scenario.ai.action.pause_timer"),
                            ("scenario.ai.lists", "scenario.ai.action.resume_timer"),
                            ("scenario.ai.lists", "scenario.ai.action.if_timer_stopped"),
                            ("scenario.ai.lists", "scenario.ai.action.if_timer_greater_than_random"),
                            ("scenario.ai.lists", "scenario.ai.action.if_timer_less_than"),
                            ("scenario.ai.lists", "scenario.ai.action.if_timer_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.action.show_countdown_timer"),
                            ("scenario.ai.lists", "scenario.ai.action.hide_countdown_timer"),
                            ("scenario.ai.lists", "scenario.ai.action.set_countdown_timer"),
                            ("scenario.ai.lists", "scenario.ai.action.stop_countdown_timer"),
                            ("scenario.ai.lists", "scenario.ai.action.start_countdown_timer"),
                            ("scenario.ai.lists", "scenario.ai.action.if_countdown_timer_stopped"),
                            ("scenario.ai.lists", "scenario.ai.action.if_countdown_timer_less_than"),
                            ("scenario.ai.lists", "scenario.ai.action.if_countdown_timer_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.action.show_hudmsg"),
                            ("scenario.ai.lists", "scenario.ai.action.show_hudmsg_middle"),
                            ("scenario.ai.lists", "scenario.ai.action.show_hudmsg_top_middle"),
                            ("scenario.ai.lists", "scenario.ai.action.hovercar_begin_path"),
                            ("scenario.ai.lists", "scenario.ai.action.set_vehicle_speed"),
                            ("scenario.ai.lists", "scenario.ai.action.set_rotor_speed"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_explosions"),
                            ("scenario.ai.lists", "scenario.ai.action.set_tinted_glass_enabled"),
                            ("scenario.ai.lists", "scenario.ai.action.hovercopter_fire_rocket"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_adjust_motion_blur"),
                            ("scenario.ai.lists", "scenario.ai.action.punch_or_kick"),
                            ("scenario.ai.lists", "scenario.ai.action.set_target_to_eyespy_if_in_sight"),
                            ("scenario.ai.lists", "scenario.ai.action.mini_skedar_try_pounce"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_object_distance_to_pad_less_than"),
                            ("scenario.ai.lists", "scenario.ai.action.avoid"),
                            ("scenario.ai.lists", "scenario.ai.action.title_init_mode"),
                            ("scenario.ai.lists", "scenario.ai.action.try_exit_title"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_emit_sparks"),
                            ("scenario.ai.lists", "scenario.ai.action.set_dr_caroll_images"),
                            ("scenario.ai.lists", "scenario.ai.action.say_quip"),
                            ("scenario.ai.lists", "scenario.ai.action.say_ci_staff_quip"),
                            ("scenario.ai.lists", "scenario.ai.action.shuffle_ruins_pillars"),
                            ("scenario.ai.lists", "scenario.ai.action.shuffle_pelagic_switches"),
                            ("scenario.ai.lists", "scenario.ai.action.set_action"),
                            ("scenario.ai.lists", "scenario.ai.action.set_team_orders"),
                            ("scenario.ai.lists", "scenario.ai.action.retreat"),
                            ("scenario.ai.lists", "scenario.ai.action.find_cover"),
                            ("scenario.ai.lists", "scenario.ai.action.find_cover_within_dist"),
                            ("scenario.ai.lists", "scenario.ai.action.find_cover_outside_dist"),
                            ("scenario.ai.lists", "scenario.ai.action.go_to_cover"),
                            ("scenario.ai.lists", "scenario.ai.action.check_cover_out_of_sight"),
                            ("scenario.ai.lists", "scenario.ai.action.orbit_target"),
                            ("scenario.ai.lists", "scenario.ai.action.set_chr_preset_to_unalerted_teammate"),
                            ("scenario.ai.lists", "scenario.ai.action.set_squadron"),
                            ("scenario.ai.lists", "scenario.ai.action.face_cover"),
                            ("scenario.ai.lists", "scenario.ai.action.danger_cover"),
                            ("scenario.ai.lists", "scenario.ai.action.release_cover"),
                            ("scenario.ai.lists", "scenario.ai.action.rebuild_teams"),
                            ("scenario.ai.lists", "scenario.ai.action.rebuild_squadrons"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_set_listening"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_not_talking"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_orders"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_has_orders"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_in_squadron_doing_action"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_listening"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_not_listening"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_injured_target"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_action"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_ammo_quantity_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_chr_target"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_compare_chr_presets_team"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_human"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_skedar"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_prop_preset_blocking_sight_to_target"),
                            ("scenario.ai.lists", "scenario.ai.action.remove_object_at_prop_preset"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_prop_preset_height_less_than"),
                            ("scenario.ai.lists", "scenario.ai.action.set_target"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_presets_target_is_not_my_target"),
                            ("scenario.ai.lists", "scenario.ai.action.set_chr_preset_to_chr_near_self"),
                            ("scenario.ai.lists", "scenario.ai.action.set_chr_preset_to_chr_near_pad"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_dangerous_object_nearby"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_heli_weapons_armed"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_hoverbot_next_step"),
                            ("scenario.ai.lists", "scenario.ai.action.shuffle_investigation_terminals"),
                            ("scenario.ai.lists", "scenario.ai.action.set_pad_preset_to_investigation_terminal"),
                            ("scenario.ai.lists", "scenario.ai.action.heli_arm_weapons"),
                            ("scenario.ai.lists", "scenario.ai.action.heli_unarm_weapons"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_safety2_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_player_using_cmp_or_ar34"),
                            ("scenario.ai.lists", "scenario.ai.condition.detect_enemy_on_same_floor"),
                            ("scenario.ai.lists", "scenario.ai.condition.detect_enemy"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_safety_less_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_target_moving_slowly"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_target_moving_closer"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_target_moving_away"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_squadron_is_dead"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_true"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_num_chrs_in_squadron_greater_than"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_natural_anim"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_y"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_sound_timer"),
                            ("scenario.ai.lists", "scenario.ai.condition.if_target_y_difference_less_than"),
                            ("setup.tables", "scenario.ai.condition.if_prop_preset_blocking_sight_to_target"),
                            ("setup.tables", "scenario.ai.action.remove_object_at_prop_preset"),
                            ("setup.tables", "scenario.ai.condition.if_prop_preset_height_less_than"),
                            ("scenario.pads", "scenario.ai.action.set_chr_preset_to_chr_near_pad"),
                            ("setup.tables", "scenario.ai.condition.if_dangerous_object_nearby"),
                            ("setup.tables", "scenario.ai.action.shuffle_investigation_terminals"),
                            ("setup.tables", "scenario.ai.action.set_pad_preset_to_investigation_terminal"),
                            ("scenario.pads", "scenario.ai.action.set_pad_preset_to_investigation_terminal"),
                            ("setup.tables", "scenario.ai.action.shuffle_ruins_pillars"),
                            ("setup.tables", "scenario.ai.action.shuffle_pelagic_switches"),
                            ("source.scene", "scenario.ai.condition.detect_enemy_on_same_floor"),
                            ("source.scene", "scenario.ai.condition.detect_enemy"),
                            ("scenario.ai.lists", "scenario.ai.action.try_attack_amount"),
                            ("scenario.ai.lists", "scenario.ai.action.set_chr_preset"),
                            ("scenario.ai.lists", "scenario.ai.action.set_chr_target"),
                            ("scenario.ai.lists", "scenario.ai.action.set_pad_preset"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_set_pad_preset"),
                            ("scenario.ai.lists", "scenario.ai.action.chr_copy_pad_preset"),
                            ("scenario.pads", "scenario.ai.action.jog_to_pad"),
                            ("scenario.pads", "scenario.ai.action.go_to_pad_preset"),
                            ("scenario.pads", "scenario.ai.action.walk_to_pad"),
                            ("scenario.pads", "scenario.ai.action.run_to_pad"),
                            ("scenario.pads", "scenario.ai.action.set_pad_preset_to_pad_on_route_to_target"),
                            ("scenario.pads", "scenario.ai.action.set_pad_preset"),
                            ("scenario.pads", "scenario.ai.action.chr_set_pad_preset"),
                            ("scenario.pads", "scenario.ai.action.try_start_alarm"),
                            ("scenario.pads", "scenario.ai.action.set_lights"),
                            ("scenario.pads", "scenario.ai.action.if_lift_stationary"),
                            ("scenario.pads", "scenario.ai.action.lift_go_to_stop"),
                            ("scenario.pads", "scenario.ai.action.if_lift_at_stop"),
                            ("scenario.pads", "scenario.ai.action.activate_lift"),
                            ("scenario.pads", "scenario.ai.action.if_using_lift"),
                            ("scenario.global.settings", "scenario.ai.action.configure_rain"),
                            ("scenario.global.settings", "scenario.ai.action.configure_snow"),
                            ("scenario.global.settings", "scenario.ai.action.configure_environment"),
                            ("setup.tables", "scenario.ai.action.chr_toggle_model_part"),
                            ("setup.tables", "scenario.ai.action.obj_set_model_part_visible"),
                            ("setup.tables", "scenario.ai.action.if_obj_health_less_than"),
                            ("setup.tables", "scenario.ai.action.set_obj_health"),
                            ("source.scene", "scenario.ai.action.set_room_to_search"),
                            ("source.scene", "scenario.ai.action.set_room_flag"),
                            ("source.scene", "scenario.ai.action.configure_environment"),
                            ("setup.tables", "scenario.ai.action.set_obj_flag"),
                            ("setup.tables", "scenario.ai.action.unset_obj_flag"),
                            ("setup.tables", "scenario.ai.action.if_obj_has_flag"),
                            ("setup.tables", "scenario.ai.action.open_door"),
                            ("setup.tables", "scenario.ai.action.close_door"),
                            ("setup.tables", "scenario.ai.action.if_door_state"),
                            ("setup.tables", "scenario.ai.action.if_object_is_door"),
                            ("setup.tables", "scenario.ai.action.lock_door"),
                            ("setup.tables", "scenario.ai.action.unlock_door"),
                            ("setup.tables", "scenario.ai.action.if_door_locked"),
                            ("setup.tables", "scenario.ai.action.if_lift_stationary"),
                            ("setup.tables", "scenario.ai.action.lift_go_to_stop"),
                            ("setup.tables", "scenario.ai.action.if_lift_at_stop"),
                            ("setup.tables", "scenario.ai.action.activate_lift"),
                            ("setup.tables", "scenario.ai.action.if_using_lift"),
                            ("navigation.paths", "scenario.ai.action.set_path"),
                            ("navigation.paths", "scenario.ai.action.start_patrol"),
                            ("navigation.paths", "scenario.ai.action.set_pad_preset_to_pad_on_route_to_target"),
                            ("navigation.paths", "scenario.ai.action.hovercar_begin_path"),
                            ("setup.tables", "scenario.ai.condition.if_object_distance_to_pad_less_than"),
                            ("scenario.pads", "scenario.ai.condition.if_object_distance_to_pad_less_than"),
                            ("navigation.generate", "scenario.ai.condition.if_waypoint_within_quadrant"),
                            ("navigation.generate", "scenario.ai.action.set_pad_preset_to_target_quadrant"),
                        }:
                            if pair not in link_pairs:
                                result.errors.append(
                                    f"{label} level.graph.json must link {pair[0]} to {pair[1]}"
                                )

            if ext == ".pdmission":
                if descriptor in name_set:
                    mission_text = zf.read(descriptor).decode(
                        "utf-8", errors="replace"
                    )
                    scenario_graph_cache = descriptor_value(
                        mission_text, "scenario_graph_cache"
                    )
                    if scenario_graph_cache != SCENARIO_GRAPH_CACHE_KIND:
                        result.errors.append(
                            f"{label} mission.ini must declare scenario_graph_cache = {SCENARIO_GRAPH_CACHE_KIND}"
                        )
                if "mission.graph.json" in name_set:
                    graph_text = zf.read("mission.graph.json").decode(
                        "utf-8", errors="replace"
                    )
                    try:
                        graph = json.loads(graph_text)
                    except json.JSONDecodeError as exc:
                        result.errors.append(
                            f"{label} mission.graph.json is not valid JSON: {exc.msg}"
                        )
                        graph = {}
                    nodes = graph.get("nodes")
                    if not isinstance(nodes, list) or not nodes:
                        result.errors.append(
                            f"{label} mission.graph.json must contain executable mission/objective nodes"
                        )
                    elif not any(
                        isinstance(node, dict)
                        and str(node.get("kind", ""))
                        in {
                            "mission.objective.source",
                            "mission.objective.criteria.source",
                        }
                        for node in nodes
                    ):
                        result.errors.append(
                            f"{label} mission.graph.json must include mission objective graph nodes"
                        )
                    if isinstance(nodes, list) and nodes and not any(
                        isinstance(node, dict)
                        and str(node.get("kind", "")) == "mission.phase.source"
                        for node in nodes
                    ):
                        result.errors.append(
                            f"{label} mission.graph.json must include mission phase graph nodes"
                        )
                if "objectives.tsv" in name_set:
                    objectives_text = zf.read("objectives.tsv").decode(
                        "utf-8", errors="replace"
                    )
                    if "original_perfect_dark_setup" in objectives_text:
                        result.errors.append(
                            f"{label} objectives.tsv still points at original_perfect_dark_setup; use mission graph source nodes"
                        )
                    result.errors.extend(validate_objectives_tsv_schema(
                        label, "objectives.tsv", objectives_text,
                        MISSION_OBJECTIVES_HEADER
                    ))

            if ext == ".pdarena" and descriptor in name_set:
                text = zf.read(descriptor).decode("utf-8", errors="replace")
                has_scenario_dep = matches_any_from_names(
                    name_set, "dependencies/assets/scenarios/*.pdscenario"
                )
                category = descriptor_value(text, "category").lower()
                slug = descriptor_value(text, "slug").lower()
                scenario = descriptor_value(text, "scenario")
                scenario_archive = descriptor_value(text, "scenario_archive")
                scenario_graph_cache = descriptor_value(text, "scenario_graph_cache")
                if has_scenario_dep:
                    if not scenario_archive.startswith("dependencies/assets/scenarios/"):
                        result.errors.append(
                            f"{label} arena.ini must declare scenario_archive under dependencies/assets/scenarios/"
                        )
                    if scenario_archive and not scenario_archive.endswith(".pdscenario"):
                        result.errors.append(
                            f"{label} arena.ini scenario_archive must point to a .pdscenario dependency"
                        )
                    if scenario.lower() == "null":
                        result.errors.append(
                            f"{label} arena.ini cannot declare scenario = null when a scenario dependency is present"
                        )
                    if scenario_graph_cache != SCENARIO_GRAPH_CACHE_KIND:
                        result.errors.append(
                            f"{label} arena.ini must declare scenario_graph_cache = {SCENARIO_GRAPH_CACHE_KIND}"
                        )
                else:
                    is_random_selector = category == "random" and "random" in slug
                    if not is_random_selector:
                        result.errors.append(
                            f"{label} missing required alternative: one of dependencies/assets/scenarios/*.pdscenario"
                        )
                    if scenario_archive:
                        result.errors.append(
                            f"{label} arena.ini declares scenario_archive but archive has no .pdscenario dependency"
                        )
                    if scenario and scenario.lower() != "null":
                        result.errors.append(
                            f"{label} arena.ini declares scenario without a .pdscenario dependency"
                        )

            if ext == ".pdweapon" and descriptor in name_set:
                text = zf.read(descriptor).decode("utf-8", errors="replace")
                if "behavior_graph" in text:
                    result.errors.append(
                        f"{label} weapon.ini still declares behavior_graph; use primary_graph/secondary_graph"
                    )
                if "primary_graph = behavior/primary.graph.json" not in text:
                    result.errors.append(
                        f"{label} weapon.ini must declare primary_graph = behavior/primary.graph.json"
                    )
                if "secondary_graph = behavior/secondary.graph.json" not in text:
                    result.errors.append(
                        f"{label} weapon.ini must declare secondary_graph = behavior/secondary.graph.json"
                    )
                for stale_key in ("hand_model_file", "texture_file = weapon_texture.png", "animation_file = reload.gltf", "file_path = fire.wav"):
                    if stale_key in text:
                        result.errors.append(
                            f"{label} weapon.ini still declares loose weapon-owned dependency {stale_key}"
                        )

            if recurse:
                for name in public:
                    nested_ext = Path(name).suffix.lower()
                    if nested_ext in TYPED_DESCRIPTORS:
                        nested = zf.read(name)
                        nested_result = validate_archive_bytes(
                            nested, f"{label}::{name}", nested_ext, recurse=True,
                            known_catalog_ids=active_catalog_ids
                        )
                        result.extend(nested_result)
    except zipfile.BadZipFile:
        result.errors.append(f"{label} is not zip-openable")
    return result


def validate_archive_path(path: Path, root: Path | None = None,
                          recurse: bool = True,
                          known_catalog_ids: set[str] | None = None) -> ConformanceResult:
    label = path.as_posix()
    if root:
        try:
            label = path.relative_to(root).as_posix()
        except ValueError:
            pass
    return validate_archive_bytes(
        path.read_bytes(), label, path.suffix, recurse,
        known_catalog_ids=known_catalog_ids
    )


def validate_root(root: Path, require_all_families: bool = False,
                  recurse: bool = True) -> ConformanceResult:
    result = ConformanceResult()
    if not root.exists():
        result.errors.append(f"{root} is missing")
        return result
    archives = [
        p for p in root.rglob("*")
        if p.is_file() and p.suffix.lower() in TYPED_DESCRIPTORS
    ]
    if not archives:
        result.errors.append(f"{root} contains no typed asset archives")
        return result

    known_catalog_ids: set[str] = set()
    for archive in sorted(archives):
        known_catalog_ids.update(
            collect_archive_catalog_ids(
                archive.read_bytes(), archive.suffix, recurse=recurse
            )
        )

    for archive in sorted(archives):
        archive_result = validate_archive_path(
            archive, root, recurse, known_catalog_ids=known_catalog_ids
        )
        archive_result.root_archives = 1
        result.extend(archive_result)

    if require_all_families:
        missing = sorted(set(TYPED_DESCRIPTORS) - result.families)
        for ext in missing:
            result.errors.append(
                f"{root} missing required family {ext} ({FAMILY_NAMES[ext]})"
            )

    return result


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root",
        action="append",
        type=Path,
        required=True,
        help="Folder containing typed archives to validate. Can be repeated.",
    )
    parser.add_argument(
        "--require-all-families",
        action="store_true",
        help="Require coverage for every current typed asset family.",
    )
    parser.add_argument(
        "--no-recursive-dependencies",
        action="store_true",
        help="Do not recursively validate embedded typed dependency archives.",
    )
    args = parser.parse_args(argv)

    definition_errors = validate_schema_definitions()
    if definition_errors:
        print("asset-archive schema contract is incomplete:", file=sys.stderr)
        for error in definition_errors:
            print(f"  - {error}", file=sys.stderr)
        return 2

    total = ConformanceResult()
    for root in args.root:
        total.extend(validate_root(
            root,
            require_all_families=args.require_all_families,
            recurse=not args.no_recursive_dependencies,
        ))

    if total.errors:
        print("asset-archive conformance failed:", file=sys.stderr)
        for error in total.errors:
            print(f"  - {error}", file=sys.stderr)
        print(
            "\nContract: every typed archive must match its frozen public "
            "schema exactly; generated products stay under _meta/ or private "
            "source-hashed cache.",
            file=sys.stderr,
        )
        return 1

    family_list = ", ".join(sorted(total.families))
    print(
        "asset-archive conformance ok: "
        f"root_archives={total.root_archives} checked_archives={total.checked_archives} "
        f"families={family_list}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
