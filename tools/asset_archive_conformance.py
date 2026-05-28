#!/usr/bin/env python3
"""Strict conformance checks for PD2 typed asset archives.

The check is schema-specific by asset family. It intentionally does not accept
"at least one editable file" as proof of conformance: every family declares the
required public entries, definitive optional slots, forbidden stale entries,
and dependency archive requirements that match the clean c3824/c3842 contract.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import re
import sys
import zipfile
from dataclasses import dataclass, field
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

KEY_VALUE_RE = re.compile(r"^\s*([A-Za-z0-9_.-]+)\s*[:=]\s*(.*?)\s*$")
NUMERIC_LITERAL_RE = re.compile(r"^[+-]?(?:0x[0-9A-Fa-f]+|\d+)$")

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
    "head",
    "head_id",
    "head_ref",
    "headnum",
    "hit_sound",
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
    "music",
    "music_id",
    "projectile_model_ref",
    "projectile",
    "projectile_id",
    "scenario",
    "scenario_id",
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
            "pads.tsv",
            "spawns.tsv",
            "volumes.tsv",
            "objects.tsv",
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
            "pads.tsv",
            "spawns.tsv",
            "volumes.tsv",
            "objects.tsv",
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
        allowed=["mesh.ini", "model.gltf", "model.glb", "model.obj", "model.mtl", "export_version.txt"],
        allowed_globs=[
            "dependencies/assets/materials/*.pdmaterial",
            "dependencies/assets/textures/*.pdtexture",
        ],
    ),
    ".pdanim": schema(
        required=["animation.ini"],
        require_one_of=[
            [["animation.gltf"], ["animation.glb"], ["header.tsv", "frames.tsv"], ["opcodes.json"]],
        ],
        allowed=["animation.ini", "animation.gltf", "animation.glb", "header.tsv", "frames.tsv", "opcodes.json", "events.tsv", "notifies.tsv"],
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
            [["sequence.mid", "sequence.tsv"], ["track.wav"], ["track.ogg"], ["track.mp3"]],
        ],
        allowed=["music.ini", "sequence.mid", "sequence.tsv", "track.wav", "track.ogg", "track.mp3", "cues.tsv", "sections.tsv"],
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
        "export_version.txt": slot("exporter provenance marker", "mesh extractor", "records source exporter revision for stale-cache detection", "validator treats archive as source-authored without exporter provenance"),
        "dependencies/assets/materials/*.pdmaterial": slot("mesh material dependencies", "mesh importer", DEPENDENCY_LOADER, "mesh uses material declarations inside the model source"),
        "dependencies/assets/textures/*.pdtexture": slot("mesh texture dependencies", "mesh importer", DEPENDENCY_LOADER, "mesh uses embedded/material-declared textures inside the model source"),
    },
    ".pdanim": {
        "animation.gltf": slot("GLTF animation source", "animation importer", "loads animation curves as runtime source and cache seed", "another animation source slot must be present"),
        "animation.glb": slot("GLB animation source", "animation importer", "loads animation curves as runtime source and cache seed", "another animation source slot must be present"),
        "header.tsv": slot("decoded animation header table", "animation importer", "loads legacy-compatible animation metadata from editable table", "another animation source slot must be present"),
        "frames.tsv": slot("decoded animation frame table", "animation importer", "loads legacy-compatible animation frames from editable table", "another animation source slot must be present"),
        "opcodes.json": slot("decoded animation opcode source", "animation importer", "loads legacy-compatible opcode animation source", "another animation source slot must be present"),
        "events.tsv": slot("animation event markers", "animation importer", "loads authored event markers", "animation has no authored event markers"),
        "notifies.tsv": slot("animation notify markers", "animation importer", "loads authored notify markers", "animation has no authored notify markers"),
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
        "sequence.mid": slot("MIDI song source", "music importer", "loads authored sequence source", "track audio or sequence.tsv must supply playback source"),
        "sequence.tsv": slot("decoded sequence table", "music importer", "loads editable sequence events", "track audio or sequence.mid must supply playback source"),
        "track.wav": slot("WAV song source", "music importer", "loads direct audio track source", "sequence source or another track format must be present"),
        "track.ogg": slot("OGG song source", "music importer", "loads direct audio track source", "sequence source or another track format must be present"),
        "track.mp3": slot("MP3 song source", "music importer", "loads direct audio track source", "sequence source or another track format must be present"),
        "cues.tsv": slot("music cue table", "music importer", "loads authored cue points", "song has no authored cue table"),
        "sections.tsv": slot("music section table", "music importer", "loads authored section loop/transition data", "song has no authored section table"),
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


def is_forbidden_asset_ref_value(value: object) -> bool:
    text = clean_value(value)
    if NUMERIC_LITERAL_RE.match(text):
        return True
    return any(text.startswith(prefix) for prefix in LEGACY_ASSET_SYMBOL_PREFIXES)


def scan_json_asset_refs(value: object, path: str, errors: list[str],
                         label: str, entry_name: str) -> None:
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
            scan_json_asset_refs(child, next_path, errors, label, entry_name)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            scan_json_asset_refs(child, f"{path}[{index}]", errors, label,
                                 entry_name)


def scan_text_asset_refs(label: str, entry_name: str, text: str) -> list[str]:
    errors: list[str] = []
    if entry_name.lower().endswith(".json"):
        try:
            parsed = json.loads(text)
        except json.JSONDecodeError:
            parsed = None
        if parsed is not None:
            scan_json_asset_refs(parsed, "", errors, label, entry_name)
            return errors

    for line_num, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith(("#", ";", "//")):
            continue
        match = KEY_VALUE_RE.match(line)
        if not match:
            continue
        key = normalize_key(match.group(1))
        if key not in FORBIDDEN_NUMERIC_ASSET_REF_KEYS:
            continue
        value = match.group(2).split("#", 1)[0].split(";", 1)[0].strip()
        if is_forbidden_asset_ref_value(value):
            errors.append(
                f"{label} contains numeric/legacy asset reference "
                f"{entry_name}:{line_num} {match.group(1)}={clean_value(value)}; "
                "use a catalog ID"
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


def validate_archive_bytes(data: bytes, label: str, ext: str,
                           recurse: bool = True) -> ConformanceResult:
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
        from io import BytesIO
        with zipfile.ZipFile(BytesIO(data)) as zf:
            names = [normalize_name(n) for n in zf.namelist()]
            name_set = set(names)
            public = [n for n in public_names(names) if not n.endswith("/")]
            public_set = set(public)

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
                    result.errors.extend(scan_text_asset_refs(label, name, text))

            if ext == ".pdscenario" and descriptor in name_set:
                text = zf.read(descriptor).decode("utf-8", errors="replace")
                if "scene_file = scene.glb" not in text and "scene_file = scene.gltf" not in text:
                    result.errors.append(
                        f"{label} scenario.ini must declare scene_file = scene.glb or scene.gltf"
                    )
                if "runtime_source_file = scene.glb" not in text and "runtime_source_file = scene.gltf" not in text:
                    result.errors.append(
                        f"{label} scenario.ini must declare runtime_source_file matching the scene source"
                    )
                for stale_key in ("setup_file", "mpsetup_file", "rooms_file", "geometry_file", "visual_scene_file"):
                    if stale_key in text:
                        result.errors.append(
                            f"{label} scenario.ini still declares {stale_key}; use scene/native tables/graphs"
                        )

            if ext == ".pdarena" and descriptor in name_set:
                text = zf.read(descriptor).decode("utf-8", errors="replace")
                has_scenario_dep = matches_any_from_names(
                    name_set, "dependencies/assets/scenarios/*.pdscenario"
                )
                category = descriptor_value(text, "category").lower()
                slug = descriptor_value(text, "slug").lower()
                scenario = descriptor_value(text, "scenario")
                scenario_archive = descriptor_value(text, "scenario_archive")
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
                            nested, f"{label}::{name}", nested_ext, recurse=True
                        )
                        result.extend(nested_result)
    except zipfile.BadZipFile:
        result.errors.append(f"{label} is not zip-openable")
    return result


def validate_archive_path(path: Path, root: Path | None = None,
                          recurse: bool = True) -> ConformanceResult:
    label = path.as_posix()
    if root:
        try:
            label = path.relative_to(root).as_posix()
        except ValueError:
            pass
    return validate_archive_bytes(path.read_bytes(), label, path.suffix, recurse)


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

    for archive in sorted(archives):
        archive_result = validate_archive_path(archive, root, recurse)
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
