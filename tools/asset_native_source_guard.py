#!/usr/bin/env python3
"""Guard the c3842/c3844 Asset Pipeline native-source contract.

The contract: typed asset archives expose directly editable public source
files, and the game client consumes those same files through catalog/provider
loading. Generated runtime products are source-hashed cache only, and runtime
ROM fallback after extraction is an asset-chain failure.
"""

from __future__ import annotations

import argparse
import io
import json
import re
import subprocess
import sys
import zipfile
from collections import Counter
from pathlib import Path

from asset_archive_conformance import (
    validate_root as validate_archive_conformance,
    validate_scene_glb_texture_contract,
)


ROOT_MARKERS = ("AGENTS.md", "tools/kanban/state.json")

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

FORBIDDEN_ARCHIVE_ENTRY_NAMES = {
    "data.bin",
    "geometry.bin",
    "setup.bin",
    "mpsetup.bin",
    "tiles.bin",
    "pads.bin",
    "runtime.graph.json",
}

PDSCENARIO_FORBIDDEN_PUBLIC_ENTRY_NAMES = {
    "setup.tsv",
    "mpsetup.tsv",
    "visual_segments.tsv",
    "setup.ini",
    "mpsetup.ini",
    "props.ini",
    "objectives.ini",
    "objects.tsv",
    "objectives.tsv",
    "setup.fields.tsv",
    "pads.tsv",
    "spawns.tsv",
    "volumes.tsv",
    "portals.tsv",
    "navigation/waypoints.tsv",
    "navigation/waygroups.tsv",
    "navigation/covers.tsv",
    "navigation/paths.tsv",
    "ai/ailists.tsv",
}

PDSCENARIO_REQUIRED_PUBLIC_ENTRY_NAMES = {
    "scene.glb",
    "portals.json",
    "pads.json",
    "spawns.json",
    "volumes.json",
    "navigation/waypoints.json",
    "navigation/waygroups.json",
    "navigation/covers.json",
    "navigation/paths.json",
    "objects.json",
    "setup.fields.json",
    "ai/ailists.json",
    "objectives.json",
    "navigation.ini",
    "level.graph.json",
}

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
    "music_ref",
    "projectile_model_ref",
    "projectile",
    "projectile_id",
    "projectile_ref",
    "prop_type",
    "scenario",
    "scenario_id",
    "scenario_ref",
    "stagenum",
    "sfx",
    "sfx_id",
    "sfx_ref",
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
    "texture_ref",
    "voice",
    "voice_id",
    "voice_ref",
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

PUBLIC_TEXT_ENTRY_SUFFIXES = (
    ".ini",
    ".json",
    ".txt",
    ".csv",
)

KEY_VALUE_RE = re.compile(r"^\s*([A-Za-z0-9_.-]+)\s*[:=]\s*(.*?)\s*$")
NUMERIC_LITERAL_RE = re.compile(r"^[+-]?(?:0x[0-9A-Fa-f]+|\d+)$")

CONTRACT_SENTINELS = {
    "AGENTS.md": [
        "Asset Pipeline c3842",
        "Public asset source is the game-facing source",
        "runtime ROM/RomProvider fallback after extraction as an asset-chain failure",
        "tools/asset_native_source_guard.py",
    ],
    ".agents/skills/pd2-large-change-sweep/SKILL.md": [
        "Asset Pipeline c3842 Gate",
        "public editable source",
        "runtime-cache-only",
        "asset-chain failure",
    ],
    "context/constraints.md": [
        "Public asset source is the game-facing source",
        "Runtime ROM fallback is an asset-chain failure",
        "source-hashed rebuildable cache",
    ],
    "context/designs/modding/asset-archive-clean-formats.md": [
        "The public authoring files are also the game-facing source of truth",
        "Runtime ROM fallback is an asset-chain failure",
        "source-hashed cache",
        "Allowed entries are definitive schema slots",
    ],
    "context/tasks.md": [
        "Public typed archives are the game-facing source",
        "Runtime ROM/RomProvider fallback after extraction is an asset-chain failure",
        "source-hashed cache",
        "c3844",
    ],
    "context/pillars/modding.md": [
        "Public asset source is the native game source",
        "Runtime ROM fallback is an asset-chain failure",
        "c3842",
        "c3844",
    ],
    ".githooks/pre-commit": [
        "pre-commit.py",
    ],
    ".githooks/pre-commit.py": [
        "asset_native_source_guard.py",
        "--staged",
    ],
    "tests/test_asset_native_source_contract.cpp": [
        "[modding][pdxxx][c3842]",
        "[modding][pdxxx][c3844]",
        "asset_native_source_guard.py",
        "asset_archive_conformance.py",
    ],
    "tools/asset_archive_conformance.py": [
        "Strict conformance checks for PD2 typed asset archives",
        "require_all_families",
        "OPTIONAL_PUBLIC_SLOT_CONTRACT",
        "META_SLOT_CONTRACT",
        "validate_schema_definitions",
        "source entry hash sidecar",
        "behavior/primary.graph.json",
        "dependencies/assets/scenarios/*.pdscenario",
    ],
    "CMakeLists.txt": [
        "tests/test_asset_native_source_contract.cpp",
    ],
}

ASSET_SENSITIVE_PREFIXES = (
    "port/src/asset",
    "port/include/asset",
    "port/src/modasset",
    "port/include/modasset",
    "port/src/romextract_pd",
    "port/src/net/netdistrib",
    "port/src/modpack",
    "port/src/modmgr",
    "port/src/modvfs",
    "examples/modding/",
)

ASSET_EVIDENCE_PREFIXES = (
    "tests/",
    "context/",
    "tools/asset_native_source_guard.py",
    "AGENTS.md",
    ".agents/skills/pd2-large-change-sweep/SKILL.md",
    ".githooks/",
)

GENERATED_SCENARIO_ROOTS = (
    "Build/data/ntsc-final/scenarios",
    "Build/data/ntsc-final/arenas",
)

AI_INTERPRETER_ONLY_FUNCTIONS = {
    "aiEndList",
    "aiGoToNext",
    "aiGoToFirst",
    "aiLabel",
    "aiYield",
}

AI_GRAPH_PENDING_FUNCTIONS = set()

AI_OPCODE_NAME_DEFAULT = "command"

SCENARIO_RUNTIME_COUNT_RULES = {
    "s_countRuntimePaths": {
        "s_aiGraphFindRuntimePath": ("missing navigation/paths.json source",),
        "s_aiGraphResolveOptionalRuntimePath": (
            "missing navigation/paths.json source",
        ),
        "s_aiGraphRequireRuntimePathPointer": (
            "missing navigation/paths.json source",
        ),
    },
    "s_countRuntimePads": {
        "s_aiGraphRequireRuntimePad": ("missing pads.json source",),
        "s_aiGraphRequireRuntimePadTable": ("missing pads.json source",),
        "s_aiGraphRequireRuntimeNavigationTables": (
            "missing pads.json source",
            "missing navigation/waypoints.json source",
            "missing navigation/waygroups.json source",
        ),
    },
    "s_countRuntimeCovers": {
        "s_aiGraphRequireRuntimeCoverTable": (
            "missing navigation/covers.json source",
        ),
    },
    "s_countRuntimeWaypoints": {
        "s_aiGraphRequireRuntimeNavigationTables": (
            "missing navigation/waypoints.json source",
            "missing navigation/waygroups.json source",
        ),
        "s_setupGraphValidateBlockedPathWaypoints": (
            "missing navigation/waypoints.json source",
        ),
    },
    "s_countRuntimeWaygroups": {
        "s_aiGraphRequireRuntimeNavigationTables": (
            "missing navigation/waygroups.json source",
        ),
    },
    "s_countRuntimeSetupObjectRows": {
        "s_aiGraphRequireRuntimeObjectTag": ("missing objects.json source",),
        "s_aiGraphResolveOptionalRuntimeObjectTag": ("missing objects.json source",),
        "s_aiGraphRequireRuntimeChopperPointer": (
            "missing objects.json source for vehicle weapon state",
        ),
        "s_aiGraphRequireRuntimeVehicleObjectPointer": (
            "missing objects.json source for vehicle state",
        ),
    },
    "s_countRuntimeSetupChrRows": {
        "s_aiGraphTryCountRuntimeSetupChrRowsFromSource": (
            "s_aiGraphRuntimeSetupCharacterSourceIsActive()",
        ),
    },
    "s_countRuntimeSetupTags": {
        "s_aiGraphRequireRuntimeSetupTag": ("missing setup.fields.json source",),
    },
}

SCENARIO_RUNTIME_COUNT_ERROR = "scenario runtime row counts must stay behind source-checked helper boundaries"

SCENARIO_PADFILE_GLOBAL_SOURCE_PROVEN_FUNCTIONS = {
    "s_countRuntimePads": (
        "g_PadsFile->numpads <= 0",
        "return g_PadsFile->numpads;",
    ),
    "s_countRuntimeCovers": (
        "g_PadsFile->numcovers <= 0",
        "return g_PadsFile->numcovers;",
    ),
}

SCENARIO_PADFILE_GLOBAL_ERROR = "scenario padfile globals must stay inside source-gated runtime count helpers"

SCENARIO_STAGE_SETUP_GLOBAL_SOURCE_PROVEN_FUNCTIONS = {
    "s_countRuntimePaths": (
        "if (!g_StageSetup.paths)",
        "g_StageSetup.paths[count].pads",
    ),
    "s_countRuntimePads": (
        "!g_StageSetup.padfiledata",
    ),
    "s_countRuntimeCovers": (
        "!g_StageSetup.padfiledata",
        "!g_StageSetup.cover",
    ),
    "s_countRuntimeSetupObjectRows": (
        "struct defaultobj *obj = (struct defaultobj *)g_StageSetup.props;",
        "obj->type != OBJTYPE_END",
    ),
    "s_countRuntimeSetupChrRows": (
        "struct defaultobj *obj = (struct defaultobj *)g_StageSetup.props;",
        "obj->type == OBJTYPE_CHR",
    ),
    "s_runtimeSetupContainsObjectRow": (
        "struct defaultobj *obj = (struct defaultobj *)g_StageSetup.props;",
        "if (obj == needle)",
    ),
    "s_runtimeSetupContainsChrnum": (
        "struct defaultobj *obj = (struct defaultobj *)g_StageSetup.props;",
        "packed->chrnum == chrnum",
    ),
    "s_countRuntimeWaypoints": (
        "if (!g_StageSetup.waypoints)",
        "g_StageSetup.waypoints[count].padnum >= 0",
    ),
    "s_countRuntimeWaygroups": (
        "if (!g_StageSetup.waygroups)",
        "g_StageSetup.waygroups[count].waypoints",
        "g_StageSetup.waygroups[count].neighbours",
    ),
    "s_aiGraphRequireRuntimePathPointer": (
        "missing navigation/paths.json source",
        "s_countRuntimePaths()",
        "if (&g_StageSetup.paths[i] == path)",
    ),
}

SCENARIO_STAGE_SETUP_GLOBAL_ERROR = "scenario g_StageSetup access must stay inside source-gated runtime table helpers"

SCENARIO_SETUP_TAG_GLOBAL_SOURCE_PROVEN_FUNCTIONS = {
    "s_countRuntimeSetupTags": (
        "struct tag *tag = g_TagsLinkedList;",
        "tag = tag->next;",
        "return count;",
    ),
}

SCENARIO_SETUP_TAG_GLOBAL_ERROR = "scenario setup tag globals must stay inside source-gated runtime setup tag helpers"

SCENARIO_SETUP_BEHAVIOR_LINK_SOURCE_ERROR = "scenario setup behavior links must validate source record targets before runtime registration"

SCENARIO_SOURCE_WIDE_PAD_CACHE_SOURCE_PROVEN_FUNCTIONS = {
    "s_clearSourceWidePadOffsets": (
        "g_SourceWidePadFile = NULL;",
        "g_SourceWidePadOffsets = NULL;",
        "g_SourceWidePadOffsetCount = 0;",
    ),
    "scenarioSourcePadsGetWideOffsets": (
        "padfiledata == g_SourceWidePadFile",
        "g_SourceWidePadOffsets && g_SourceWidePadOffsetCount >= 0",
        "*out_count = g_SourceWidePadOffsetCount;",
        "return g_SourceWidePadOffsets;",
    ),
    "s_buildPadfile": (
        "if (wide_offsets)",
        "g_SourceWidePadFile = buf;",
        "g_SourceWidePadOffsets = wide_offsets;",
        "g_SourceWidePadOffsetCount = row_count;",
        "SCENARIO.SOURCE: pads.json runtime uses 32-bit source pad offsets",
    ),
}

SCENARIO_SOURCE_WIDE_PAD_CACHE_ERROR = "scenario source wide pad offset cache must stay inside source padfile helpers"

SCENARIO_RUNTIME_LOOKUP_RULES = {
    "ailistFindById": {
        "scenarioSourceAiGraphExecuteSetList": (
            's_aiGraphRequireListControlNode("set_list"',
        ),
        "scenarioSourceAiGraphExecuteReturnList": (
            's_aiGraphRequireListControlNode("return_list"',
        ),
        "scenarioSourceAiGraphExecuteSpawnChrAtPad": (
            's_aiSetupSpawnGraphReady("spawn_chr_at_pad"',
        ),
        "scenarioSourceAiGraphExecuteSpawnChrAtChr": (
            's_aiSetupSpawnGraphReady("spawn_chr_at_chr"',
        ),
        "scenarioSourceAiGraphExecuteDuplicateChr": (
            's_aiEntityLifecycleGraphReady("duplicate_chr"',
        ),
    },
    "pathFindById": {
        "s_aiGraphFindRuntimePath": (
            "missing navigation/paths.json source",
            "s_countRuntimePaths()",
        ),
        "s_aiGraphResolveOptionalRuntimePath": (
            "missing navigation/paths.json source",
            "s_countRuntimePaths()",
        ),
    },
    "tagFindById": {
        "s_aiGraphRequireRuntimeObjectTag": (
            "missing objects.json source",
            "s_countRuntimeSetupObjectRows()",
        ),
        "s_aiGraphResolveOptionalRuntimeObjectTag": (
            "missing objects.json source",
            "s_countRuntimeSetupObjectRows()",
        ),
        "s_aiGraphRequireRuntimeObjectTagType": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "s_aiGraphRequireRuntimeObjectTagAnyType": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "s_aiGraphRequireRuntimeSetupTag": (
            "missing setup.fields.json source",
            "s_countRuntimeSetupTags()",
        ),
        "s_aiGraphCopyTagPlacementChecked": (
            "s_aiGraphRequireRuntimeSetupTag(",
        ),
        "scenarioSourceAiGraphExecuteWarpJoToTag": (
            "s_aiGraphRequireRuntimeSetupTag(",
        ),
    },
    "objFindByTagId": {
        "scenarioSourceAiGraphExecuteIfChrHasObject": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteIfWeaponThrownOnObject": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteIfGunUnclaimed": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteIfObjectHealthy": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteIfChrActivatedObject": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteObjInteract": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteDestroyObject": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteDropObjectFromChr": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteGiveObjectToChr": (
            "s_aiGraphResolveOptionalRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteObjectMoveToPad": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteSetPadPresetToInvestigationTerminal": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteIfObjectDistanceToPadLessThan": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteSetObjFlag": (
            "s_aiGraphResolveOptionalRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteUnsetObjFlag": (
            "s_aiGraphResolveOptionalRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteIfObjHasFlag": (
            "s_aiGraphResolveOptionalRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteOpenDoor": (
            "s_aiGraphRequireRuntimeObjectTagType(",
        ),
        "scenarioSourceAiGraphExecuteCloseDoor": (
            "s_aiGraphRequireRuntimeObjectTagType(",
        ),
        "scenarioSourceAiGraphExecuteIfDoorState": (
            "s_aiGraphRequireRuntimeObjectTagType(",
        ),
        "scenarioSourceAiGraphExecuteIfObjectIsDoor": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteLockDoor": (
            "s_aiGraphRequireRuntimeObjectTagType(",
        ),
        "scenarioSourceAiGraphExecuteUnlockDoor": (
            "s_aiGraphRequireRuntimeObjectTagType(",
        ),
        "scenarioSourceAiGraphExecuteIfDoorLocked": (
            "s_aiGraphRequireRuntimeObjectTagType(",
        ),
        "scenarioSourceAiGraphExecuteIfLiftStationary": (
            "s_aiGraphRequireRuntimeObjectTagType(",
        ),
        "scenarioSourceAiGraphExecuteLiftGoToStop": (
            "s_aiGraphRequireRuntimeObjectTagType(",
        ),
        "scenarioSourceAiGraphExecuteIfLiftAtStop": (
            "s_aiGraphRequireRuntimeObjectTagType(",
        ),
        "scenarioSourceAiGraphExecuteActivateLift": (
            "s_aiGraphRequireRuntimeObjectTagType(",
        ),
        "scenarioSourceAiGraphExecuteSetObjectSoundPlaying": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecutePlayRepeatingSoundFromObject": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecutePlaySoundFromEntity": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecutePlaySoundFromProp": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteSetObjImage": (
            "s_aiGraphRequireRuntimeObjectTagAnyType(",
        ),
        "scenarioSourceAiGraphExecuteObjectDoAnimation": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteSetDoorOpen": (
            "s_aiGraphRequireRuntimeObjectTagType(",
        ),
        "scenarioSourceAiGraphExecuteEnableObj": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteDisableObj": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteIfObjInRoom": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteIfPlayerLookingAtObject": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteChrGrabObject": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteSetAutogunTargetTeam": (
            "s_aiGraphRequireRuntimeObjectTagType(",
        ),
        "scenarioSourceAiGraphExecuteObjSetModelPartVisible": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteIfObjHealthLessThan": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
        "scenarioSourceAiGraphExecuteSetObjHealth": (
            "s_aiGraphRequireRuntimeObjectTag(",
        ),
    },
}

SCENARIO_RUNTIME_LOOKUP_ERROR = "scenario runtime setup/path/AI-list lookups must stay behind source-checked helper boundaries"

SCENARIO_PLAYER_STATE_ACCESS_ERROR = "scenario player state access must stay behind runtime player proof helpers"

SCENARIO_CURRENT_PLAYER_POINTER_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteEnableObj": (
        "struct player *current_player = g_Vars.currentplayer;",
        "s_aiGraphRequireRuntimePlayerPointer(",
        '"enable_obj", current_player',
        "player = current_player;",
    ),
    "scenarioSourceAiGraphExecuteIfPlayerChrPortalDistanceLessThan": (
        "struct player *player = g_Vars.currentplayer;",
        "s_aiGraphRequireRuntimePlayerPointer(",
        '"if_player_chr_portal_distance_less_than", player',
    ),
    "scenarioSourceAiGraphExecuteSetCameraAnimation": (
        "struct player *player = g_Vars.currentplayer;",
        's_aiGraphRequireRuntimePlayerPointer("set_camera_animation"',
        'player, "current"',
    ),
}

SCENARIO_CURRENT_PLAYER_NUMBER_ERROR = "scenario current-player-number reads must stay behind runtime player-slot proof helpers"

SCENARIO_ACTIVE_CHARACTER_POINTER_SOURCE_PROVEN_FUNCTIONS = {
    "s_aiGraphRequireRuntimeCharacterRef": (
        "return s_aiGraphRequireRuntimeCharacterRefFromBase(action,",
        "g_Vars.chrdata, chr_id, out_chr_count, out_chr",
    ),
    "scenarioSourceAiGraphExecuteSetList": (
        's_aiGraphRequireListControlNode("set_list"',
        "s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
        '"set_list", g_Vars.chrdata, target_preset',
        "chr->ailist = ailist",
    ),
    "scenarioSourceAiGraphExecuteSetReturnList": (
        "struct chrdata *active_chr = g_Vars.chrdata;",
        's_aiGraphRequireListControlNode("set_return_list"',
        's_aiGraphRequireOptionalRuntimeCharacterPointer(\n\t\t\t\t\t"set_return_list", active_chr',
        "s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
        "active_chr->aireturnlist = list_id",
    ),
    "scenarioSourceAiGraphExecuteSetShotList": (
        "struct chrdata *chr = g_Vars.chrdata;",
        's_aiGraphRequireListControlNode("set_shot_list"',
        's_aiGraphRequireRuntimeCharacterStatePointer("set_shot_list"',
        "chr->aishotlist = list_id",
    ),
    "scenarioSourceAiGraphExecuteReturnList": (
        "struct chrdata *chr = g_Vars.chrdata;",
        's_aiGraphRequireListControlNode("return_list"',
        's_aiGraphRequireRuntimeCharacterStatePointer("return_list"',
        "list_id = chr->aireturnlist",
    ),
    "scenarioSourceAiGraphExecuteSetPunchDodgeList": (
        's_aiGraphExecuteSetChrListField("set_punch_dodge_list"',
        "g_Vars.chrdata, offsetof(struct chrdata, aipunchdodgelist)",
    ),
    "scenarioSourceAiGraphExecuteSetShootingAtMeList": (
        's_aiGraphExecuteSetChrListField("set_shooting_at_me_list"',
        "g_Vars.chrdata, offsetof(struct chrdata, aishootingatmelist)",
    ),
    "scenarioSourceAiGraphExecuteSetDarkRoomList": (
        's_aiGraphExecuteSetChrListField("set_dark_room_list"',
        "g_Vars.chrdata, offsetof(struct chrdata, aidarkroomlist)",
    ),
    "scenarioSourceAiGraphExecuteSetPlayerDeadList": (
        's_aiGraphExecuteSetChrListField("set_player_dead_list"',
        "g_Vars.chrdata, offsetof(struct chrdata, aiplayerdeadlist)",
    ),
    "scenarioSourceAiGraphExecuteSayQuip": (
        "struct chrdata *active_chr = g_Vars.chrdata;",
        's_aiGraphRequireQuipShuffleNode("say_quip"',
        's_aiGraphRequireRuntimeCharacterStatePointer("say_quip"',
        "active_chr->soundtimer",
    ),
    "scenarioSourceAiGraphExecuteSetCameraAnimation": (
        "struct chrdata *active_chr = g_Vars.chrdata;",
        's_aiPlayerCutsceneGraphReady("set_camera_animation"',
        "s_aiGraphRequireRuntimeCharacterStatePointer(",
        '"set_camera_animation",',
        "active_chr, &chr_count",
        "active_chr->sleep = -1",
    ),
    "scenarioSourceAiGraphExecuteTryEquipWeapon": (
        "struct chrdata *active_chr = g_Vars.chrdata;",
        's_aiSetupSpawnGraphReady("try_equip_weapon"',
        's_aiGraphRequireOptionalLiveRuntimeCharacterPointer("try_equip_weapon"',
    ),
    "scenarioSourceAiGraphExecuteTryEquipHat": (
        "struct chrdata *active_chr = g_Vars.chrdata;",
        's_aiSetupSpawnGraphReady("try_equip_hat"',
        's_aiGraphRequireOptionalLiveRuntimeCharacterPointer("try_equip_hat"',
    ),
    "scenarioSourceAiGraphExecuteObjectDoAnimation": (
        "struct chrdata *active_chr = g_Vars.chrdata;",
        's_aiSetupSpawnGraphReady("object_do_animation"',
        "s_aiGraphRequireRuntimeCharacterStatePointer(",
        '"object_do_animation", active_chr',
    ),
}

SCENARIO_ACTIVE_CHARACTER_POINTER_ERROR = "scenario active-character pointer reads must stay behind source-aware proof helpers"

SCENARIO_VEHICLE_MOTION_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteHovercopterFireRocket",
    "scenarioSourceAiGraphExecuteHovercarBeginPath",
    "scenarioSourceAiGraphExecuteSetVehicleSpeed",
    "scenarioSourceAiGraphExecuteSetRotorSpeed",
}

SCENARIO_VEHICLE_MOTION_GLOBAL_ERROR = "scenario vehicle motion access must stay behind runtime vehicle proof helpers"

SCENARIO_MISC_BRANCH_VEHICLE_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteIfY": (
        "struct chopperobj *hovercar = g_Vars.hovercar;",
        's_aiGraphRequireRuntimeVehicleObjectPointer("if_y",',
        "OBJTYPE_HOVERCAR, OBJTYPE_CHOPPER",
        "chopperFromHovercar(hovercar)",
        "vehicle_rows=%d source_vehicle_type=%d",
    ),
}

SCENARIO_MISC_BRANCH_VEHICLE_ERROR = "scenario misc-branch vehicle access must stay behind runtime vehicle proof helpers"

SCENARIO_PROP_TARGET_VEHICLE_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteSetTarget": (
        "s_aiGraphCountRuntimeSetupChrRowsFromSource(\"set_target\"",
        's_aiGraphRequireRuntimeVehicleObjectPointer(\n\t\t\t\t"set_target", &hovercar->base,',
        "OBJTYPE_CHOPPER, OBJTYPE_HOVERCAR",
        "chopperSetTarget(hovercar, chrnum)",
        "vehicle_rows=%d source_vehicle_type=%d",
        "objects=%s backend=%s",
    ),
}

SCENARIO_PROP_TARGET_VEHICLE_ERROR = "scenario prop-target vehicle access must stay behind runtime vehicle proof helpers"

SCENARIO_TIMER_VEHICLE_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteRestartTimer": (
        '"restart_timer", &hovercar->base',
        "OBJTYPE_CHOPPER, OBJTYPE_HOVERCAR",
        "chopperRestartTimer(hovercar)",
        "vehicle_rows=%d source_vehicle_type=%d",
        "objects=%s backend=%s",
    ),
    "scenarioSourceAiGraphExecuteIfTimerLessThan": (
        '"if_timer_less_than", &hovercar->base',
        "OBJTYPE_CHOPPER, OBJTYPE_HOVERCAR",
        "chopperGetTimer(hovercar)",
        "vehicle_rows=%d source_vehicle_type=%d",
        "objects=%s backend=%s",
    ),
    "scenarioSourceAiGraphExecuteIfTimerGreaterThan": (
        '"if_timer_greater_than", &hovercar->base',
        "OBJTYPE_CHOPPER, OBJTYPE_HOVERCAR",
        "chopperGetTimer(hovercar)",
        "vehicle_rows=%d source_vehicle_type=%d",
        "objects=%s backend=%s",
    ),
}

SCENARIO_TIMER_VEHICLE_ERROR = "scenario timer vehicle access must stay behind runtime vehicle proof helpers"

SCENARIO_HOVERCAR_BRANCH_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteStop": (
        '"stop", &hovercar->base',
        "OBJTYPE_CHOPPER, OBJTYPE_HOVERCAR",
        "chopperStop(hovercar)",
        "vehicle_rows=%d source_vehicle_type=%d",
        "objects=%s backend=%s",
    ),
    "scenarioSourceAiGraphExecuteTryModifyAttack": (
        '"try_modify_attack", &hovercar->base',
        "OBJTYPE_CHOPPER, OBJTYPE_HOVERCAR",
        "chopperAttack(hovercar)",
        "vehicle_rows=%d source_vehicle_type=%d",
        "objects=%s backend=%s",
    ),
    "scenarioSourceAiGraphExecuteIfLosToTarget": (
        '"if_los_to_target", &hovercar->base',
        "OBJTYPE_CHOPPER, OBJTYPE_HOVERCAR",
        "chopperCheckTargetInFov(hovercar, 64)",
        "chopperCheckTargetInSight(hovercar)",
        "vehicle_rows=%d source_vehicle_type=%d",
        "objects=%s backend=%s",
    ),
    "scenarioSourceAiGraphExecuteIfLosToAttackTarget": (
        '"if_los_to_attack_target", &hovercar->base',
        "OBJTYPE_CHOPPER, OBJTYPE_HOVERCAR",
        "chopperCheckTargetInFov(hovercar, 64)",
        "chopperCheckTargetInSight(hovercar)",
        "vehicle_rows=%d source_vehicle_type=%d",
        "objects=%s backend=%s",
    ),
}

SCENARIO_HOVERCAR_BRANCH_ERROR = "scenario hovercar branch access must stay behind runtime vehicle proof helpers"

SCENARIO_PROP_INDEX_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteRemoveReferencesToChr": (
        's_aiGraphRequireOptionalRuntimeCharacterPointer(\n\t\t\t"remove_references_to_chr"',
    ),
}

SCENARIO_PROP_INDEX_REQUIRED_TOKENS = {
    "scenarioSourceAiGraphExecuteRemoveReferencesToChr": (
        "prop_index = (s32)(chr->prop - g_Vars.props);",
        "chrClearReferences(prop_index)",
        "chr_rows=%d source_chr=%d",
    ),
}

SCENARIO_PROP_INDEX_ERROR = "scenario prop-index derivation must stay behind runtime character proof helpers"

SCENARIO_PROP_PRESET_INDEX_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteRemoveObjectAtPropPreset": (
        's_aiGraphRequirePropPresetTargetNode("action",',
        '"remove_object_at_prop_preset"',
        "s_aiGraphRequireRuntimeCharacterPointer(",
    ),
    "scenarioSourceAiGraphExecuteIfPropPresetHeightLessThan": (
        's_aiGraphRequirePropPresetTargetNode("condition",',
        '"if_prop_preset_height_less_than"',
        "s_aiGraphRequireRuntimeCharacterPointer(",
    ),
}

SCENARIO_PROP_PRESET_INDEX_REQUIRED_TOKENS = {
    "scenarioSourceAiGraphExecuteRemoveObjectAtPropPreset": (
        "struct prop *prop = &g_Vars.props[chr->proppreset1];",
        "chr_rows=%d source_chr=%d cleared=%d",
        "objects=%s backend=graph.ai.action.prop_target+ai/ailists.json+objects.json",
    ),
    "scenarioSourceAiGraphExecuteIfPropPresetHeightLessThan": (
        "struct prop *prop = &g_Vars.props[chr->proppreset1];",
        "propGetBbox(prop",
        "chr_rows=%d source_chr=%d value=%.3f height=%.3f",
        "objects=%s backend=graph.ai.condition.prop_target+ai/ailists.json+objects.json",
    ),
}

SCENARIO_PROP_PRESET_INDEX_ERROR = "scenario prop-preset index access must stay behind object-source and character proof helpers"

SCENARIO_ROOM_PROP_SCAN_SOURCE_PROVEN_FUNCTIONS = {
    "s_aiGraphChrSeesSuspiciousItem": (
        "roomGetProps(chr->prop->rooms",
        "struct prop *prop = &g_Vars.props[*ptr];",
        "chrHasLosToProp(chr, prop)",
    ),
}

SCENARIO_ROOM_PROP_SCAN_CALLERS = {
    "scenarioSourceAiGraphExecuteIfSeesSuspiciousItem": (
        "s_aiGraphRequireSpatialPerceptionActor(",
        '"if_sees_suspicious_item"',
        "chr, 0, 1, &chr_count, &source_chr",
        "s_aiGraphChrSeesSuspiciousItem(chr)",
    ),
}

SCENARIO_ROOM_PROP_SCAN_ERROR = "scenario room-prop scans must stay behind spatial object-source and character proof helpers"

SCENARIO_SCENE_ROOM_TABLE_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteSetRoomFlag": {
        "roomnum": (
            's_aiGraphResolveRuntimeSceneRoom("set_room_flag"',
        ),
    },
    "scenarioSourceAiGraphExecuteConfigureEnvironment": {
        "roomnum": (
            's_aiGraphResolveRuntimeSceneRoom("configure_environment"',
        ),
        "i": (
            's_aiGraphRequireRuntimeSceneRooms("configure_environment"',
            "for (i = 1; i < g_Vars.roomcount; i++)",
        ),
    },
}

SCENARIO_SCENE_ROOM_TABLE_ERROR = "scenario room table mutations must stay behind source-built scene room proof helpers"

SCENARIO_SCENE_ROOM_GLOBAL_SOURCE_PROVEN_FUNCTIONS = {
    "s_aiGraphRequireRuntimeSceneRooms": {
        "pre": (),
        "required": (
            "if (!g_Rooms || g_Vars.roomcount <= 1)",
            "missing source-built scene room table",
            "*out_room_count = g_Vars.roomcount;",
        ),
    },
    "s_aiGraphResolveRuntimeSceneRoom": {
        "pre": (
            "s_aiGraphRequireRuntimeSceneRooms(action, out_room_count)",
        ),
        "required": (
            "roomnum > 0 && roomnum < g_Vars.roomcount",
            "*out_room_available = 1;",
        ),
    },
    "scenarioSourceAiGraphExecuteConfigureEnvironment": {
        "pre": (
            's_aiGraphRequireRuntimeSceneRooms("configure_environment"',
        ),
        "required": (
            "for (i = 1; i < g_Vars.roomcount; i++)",
        ),
    },
}

SCENARIO_SCENE_ROOM_GLOBAL_ERROR = "scenario room table globals must stay behind source-built scene room proof helpers"

SCENARIO_PORTAL_TABLE_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteSetPortalFlag": (
        's_aiGraphResolveRuntimePortal("set_portal_flag"',
    ),
}

SCENARIO_PORTAL_TABLE_ERROR = "scenario portal table mutations must stay behind source-built portals.json proof helpers"

SCENARIO_PORTAL_GLOBAL_SOURCE_PROVEN_FUNCTIONS = {
    "s_aiGraphResolveRuntimePortal": (
        "missing portals.json source",
        "if (!g_BgPortals)",
        "g_BgNumPortalCameraCacheItems > 0",
        "portalnum < g_BgNumPortalCameraCacheItems",
    ),
}

SCENARIO_PORTAL_GLOBAL_ERROR = "scenario portal globals must stay behind source-built portals.json proof helpers"

SCENARIO_MISSION_MUSIC_MODE_GLOBAL_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteDuplicateChr": (
        "missing scenario.ai.action.duplicate_chr node",
        "missing pads.json source",
        's_aiGraphRequireRuntimeCharacterRefFromBase("duplicate_chr"',
        's_aiGraphResolveBodyCatalogId("duplicate_chr"',
        "AI action duplicate_chr chr=%d chr_rows=%d source_chr=%d",
    ),
    "scenarioSourceAiGraphExecuteIfMusicEventQueueIsEmpty": (
        "missing scenario.ai.condition.if_music_event_queue_is_empty node",
        "missing ai/ailists.json source",
        "AI condition if_music_event_queue_is_empty queue=%d waited=%d branch=%d label=%d",
    ),
    "scenarioSourceAiGraphExecuteIfCoopMode": (
        "missing scenario.ai.condition.if_coop_mode node",
        "missing ai/ailists.json source",
        "AI condition if_coop_mode coop=%d normmp=%d branch=%d label=%d",
    ),
    "s_aiGraphRuntimeChrIsSourceSpawned": (
        "s_ActiveScenarioGraphs.level_graph_active",
        "s_ActiveScenarioGraphs.ai_lists_path[0]",
        "strcmp(s_ActiveScenarioGraphs.level_global_settings_kind",
    ),
    "s_countSourceBackedMpParticipantChrs": (
        "s_ActiveScenarioGraphs.level_graph_active",
        "s_ActiveScenarioGraphs.ai_lists_path[0]",
        "strcmp(s_ActiveScenarioGraphs.level_global_settings_kind",
    ),
}

SCENARIO_MISSION_MUSIC_MODE_GLOBAL_ERROR = "scenario mission/music mode globals must stay behind graph source proof"

SCENARIO_PLAYER_INVINCIBILITY_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteChrSetInvincible": (
        "missing scenario.ai.action.chr_set_invincible node",
        's_aiGraphResolvePlayerChr("chr_set_invincible"',
        's_aiGraphRequireRuntimePlayerSlot("chr_set_invincible"',
        "g_PlayerInvincible = true",
        "AI action chr_set_invincible chr=%d chr_rows=%d target_chr=%d player_checked=%d",
    ),
    "scenarioSourceAiGraphExecuteIfPlayerIsInvincible": (
        "missing scenario.ai.condition.if_player_is_invincible node",
        's_aiGraphResolvePlayerChr("if_player_is_invincible"',
        's_aiGraphRequireRuntimePlayerSlot("if_player_is_invincible"',
        "pass = g_PlayerInvincible ? 1 : 0",
        "AI condition if_player_is_invincible chr=%d chr_rows=%d target_chr=%d player_checked=%d",
    ),
}

SCENARIO_PLAYER_INVINCIBILITY_ERROR = "scenario player invincibility global access must stay behind player-slot proof"

SCENARIO_ENVIRONMENT_GLOBAL_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteSetWindSpeed": (
        's_aiGraphRequireSkyNode("set_wind_speed"',
        ".level_ai_set_wind_speed_node_count",
        "g_SkyWindSpeed = 0.1f * speed",
        "AI action set_wind_speed speed=%d value=%.3f",
    ),
    "scenarioSourceAiGraphExecuteSetTintedGlassEnabled": (
        's_aiGraphRequireMiscEffectNode("set_tinted_glass_enabled"',
        ".level_ai_set_tinted_glass_enabled_node_count",
        "g_TintedGlassEnabled = enabled",
        "AI action set_tinted_glass_enabled enabled=%d",
    ),
}

SCENARIO_ENVIRONMENT_GLOBAL_HELPER_PROOF_TOKENS = {
    "s_aiGraphRequireSkyNode": (
        "missing scenario.ai.action.%s node",
        "missing ai/ailists.json source",
    ),
    "s_aiGraphRequireMiscEffectNode": (
        "missing scenario.ai action/condition.%s node",
        "missing ai/ailists.json source",
    ),
}

SCENARIO_ENVIRONMENT_GLOBAL_ERROR = "scenario environment globals must stay behind graph source proof"

SCENARIO_CUTSCENE_FRAME_OVERRUN_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteChrDoAnimation": (
        's_aiGraphRequireAnimationNode("chr_do_animation"',
        's_aiGraphResolveAnimationCatalogId("chr_do_animation"',
        '"chr_do_animation", basechr, target_chr',
        "playerCurrentCutsceneInProgress()",
        "g_CutsceneFrameOverrun240 * speed * 0.25f",
        "AI action chr_do_animation chr=%d chr_rows=%d target_chr=%d player_checked=%d anim_id=%s anim_source=%s clip_bytes=%u",
    ),
    "scenarioSourceAiGraphExecuteObjectDoAnimation": (
        's_aiSetupSpawnGraphReady("object_do_animation"',
        "missing objects.json source",
        '"object_do_animation", anim_id, "object"',
        's_aiGraphRequireRuntimeObjectTag("object_do_animation"',
        "playerCurrentCutsceneInProgress()",
        "g_CutsceneFrameOverrun240 * speed *",
        "AI action object_do_animation anim_id=%s anim_source=%s clip_bytes=%u tag=%d resolved_tag=%d object_rows=%d chr_rows=%d",
    ),
}

SCENARIO_CUTSCENE_FRAME_OVERRUN_ERROR = "scenario cutscene frame-overrun timing must stay behind animation source proof"

SCENARIO_TELEPORT_SOUND_PRIORITY_SOURCE_PROVEN_FUNCTIONS = {
    "s_aiGraphPlayTeleportSound": (
        "osGetThreadPri(0)",
        "osGetThreadPri(&g_AudioManager.thread)",
        "osSetThreadPri(0, audiopri + 1)",
        "sndStart(var80095200, soundnum",
        "osSetThreadPri(0, mainpri)",
    ),
}

SCENARIO_TELEPORT_SOUND_PRIORITY_CALLER_PROOF_TOKENS = {
    "scenarioSourceAiGraphExecuteChrBeginOrEndTeleport": (
        "missing scenario.ai.action.chr_begin_or_end_teleport node",
        "s_aiGraphResolveAudioCatalogId(",
        "s_aiGraphPlayTeleportSound(SFX_RELOAD_FARSIGHT)",
        "sound_id=%s",
    ),
    "scenarioSourceAiGraphExecuteIfChrTeleportFullWhite": (
        "missing scenario.ai.condition.if_chr_teleport_full_white node",
        "s_aiGraphResolveAudioCatalogId(",
        "s_aiGraphPlayTeleportSound(SFX_FIRE_SHOTGUN)",
        "sound_id=%s",
    ),
}

SCENARIO_TELEPORT_SOUND_PRIORITY_ERROR = "scenario teleport sound priority must stay behind catalog audio proof"

SCENARIO_LIFT_NUMBER_SOURCE_PROVEN_FUNCTIONS = {
    "s_aiGraphRequireRuntimeLiftNumber": (
        "s_aiGraphRequireRuntimePadTable(action, &pad_count)",
        "liftnum <= 0 || liftnum > ARRAYCOUNT(g_Lifts)",
        "padUnpack(i, PADFIELD_LIFT, &pad)",
        "pad.liftnum == liftnum",
        "missing runtime lift number %d in source pad rows",
    ),
}

SCENARIO_LIFT_NUMBER_CALLER_PROOF_TOKENS = {
    "scenarioSourceAiGraphExecuteActivateLift": (
        's_aiGraphRequireLiftNode("activate_lift"',
        's_aiGraphRequireRuntimeLiftNumber("activate_lift"',
        's_aiGraphRequireRuntimeObjectTagType("activate_lift"',
        "liftActivate(obj->prop, (u8)liftnum)",
        "AI action activate_lift liftnum=%d pad=%d found=1 pad_rows=%d",
    ),
}

SCENARIO_LIFT_NUMBER_ERROR = "scenario lift-number table access must stay behind public pad-source proof"

SCENARIO_AUTOCUT_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteEndLevel": (
        's_aiPlayerCutsceneGraphReady("end_level"',
        "debugAllowEndLevel()",
        "if (g_Vars.autocutplaying)",
        "g_Vars.autocutfinished = true",
        "AI action end_level autocut=%d",
    ),
    "scenarioSourceAiGraphExecuteDisableObj": (
        's_aiEntityLifecycleGraphReady("disable_obj"',
        's_aiGraphRequireRuntimeObjectTag("disable_obj"',
        "obj = objFindByTagId(tag_id)",
        "if (g_Vars.autocutplaying &&",
        "AI action disable_obj tag=%d object_rows=%d applied=%d",
    ),
}

SCENARIO_AUTOCUT_ERROR = "scenario autocut globals must stay behind graph source proof"

SCENARIO_FRAME_STATE_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteSetDrCarollImages": (
        's_aiGraphRequireMiscEffectNode("set_dr_caroll_images"',
        's_aiGraphRequireRuntimeCharacterRefFromBase(\n\t\t\t"set_dr_caroll_images"',
        "g_Vars.lvframenum % 4",
        "AI action set_dr_caroll_images chr=%d chr_rows=%d target_chr=%d",
    ),
    "scenarioSourceAiGraphExecuteSetDoorOpen": (
        's_aiSetupSpawnGraphReady("set_door_open"',
        's_aiGraphRequireRuntimeObjectTagType("set_door_open"',
        "obj = objFindByTagId(tag_id)",
        "door->lastopen60 = g_Vars.lvframe60",
        "AI action set_door_open tag=%d object_rows=%d applied=%d",
    ),
}

SCENARIO_FRAME_STATE_ERROR = "scenario frame-state globals must stay behind graph source proof"

SCENARIO_KILL_COUNT_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteIfKillCountGreaterThan": (
        's_aiGraphRequireMissionGlobalConditionNode(\n\t\t\t"if_kill_count_greater_than"',
        "branch_taken = g_Vars.killcount > kill_count",
        "AI condition if_kill_count_greater_than kill_count=%d current=%d",
    ),
}

SCENARIO_KILL_COUNT_ERROR = "scenario kill-count global reads must stay behind mission/global source proof"

SCENARIO_STAGE_NUM_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteIfChrInRoom": {
        "globals": ("g_Vars.stagenum",),
        "pre": (
            's_aiGraphPrepareRoomObjectWeaponBranch("if_chr_in_room"',
            's_aiGraphRequireRuntimeCharacterRefFromBase("if_chr_in_room"',
            's_aiGraphRequireRuntimePad("if_chr_in_room"',
        ),
        "required": (
            "stageGetIndex(g_Vars.stagenum) == STAGEINDEX_G5BUILDING",
            's_aiGraphRequireRuntimePlayerSlot("if_chr_in_room"',
            "AI condition if_chr_in_room chr=%d room_type=%d pad=%d found=%d",
        ),
    },
    "scenarioSourceAiGraphExecuteDetectEnemyOnSameFloor": {
        "globals": ("g_Vars.stagenum",),
        "pre": (
            "s_aiGraphRequireSafetyDetectionNode(",
            's_aiGraphRuntimeCharacterPointerLooksSourceBacked(chr',
        ),
        "required": (
            "stageGetIndex(g_Vars.stagenum) ==",
            "STAGEINDEX_MAIANSOS",
            "chrFindByLiteralId(*chrnums)",
            "AI condition detect_enemy_on_same_floor chr_rows=%d source_chr=%d checked=%d",
        ),
    },
    "scenarioSourceAiGraphExecuteIfCutsceneButtonPressed": {
        "globals": ("g_Vars.stagenum",),
        "pre": (
            's_aiPlayerCutsceneGraphReady("if_cutscene_button_pressed"',
        ),
        "required": (
            "playerAnyCutsceneInProgress()",
            "playerAnyCutsceneSkipRequested()",
            "g_Vars.stagenum == STAGE_CITRAINING",
            "AI condition if_cutscene_button_pressed label=%d branch=%d",
        ),
    },
    "scenarioSourceAiGraphExecuteTryEquipWeapon": {
        "globals": ("g_Vars.stagenum", "g_Vars.stagenum", "g_Vars.stagenum"),
        "pre": (
            's_aiSetupSpawnGraphReady("try_equip_weapon"',
            's_aiGraphResolveModelCatalogId("try_equip_weapon"',
            's_aiGraphResolveWeaponCatalogId("try_equip_weapon"',
            's_aiGraphRequireOptionalLiveRuntimeCharacterPointer("try_equip_weapon"',
        ),
        "required": (
            "cheatIsActive(CHEAT_MARQUIS)",
            "cheatIsActive(CHEAT_ENEMYROCKETS)",
            "STAGE_INVESTIGATION",
            "STAGE_MBR",
            "chrGiveWeapon(active_chr",
            "AI action try_equip_weapon model_id=%s weapon_id=%s chr_rows=%d",
        ),
    },
}

SCENARIO_STAGE_NUM_ERROR = "scenario stage-number globals must stay behind source-proven special-case handlers"

SCENARIO_PLAYER_AUTOWALK_STATE_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteIfPlayerAutoWalkFinished": (
        "missing scenario.ai.condition.if_player_auto_walk_finished node",
        "missing ai/ailists.json source",
        "s_aiGraphRequireRuntimeCharacterRefFromBase(",
        "s_aiGraphRequireRuntimePlayerSlot(",
        "setCurrentPlayerNum(playernum)",
        "AI condition if_player_auto_walk_finished chr=%d chr_rows=%d target_chr=%d player_checked=%d label=%d walking=%d",
    ),
}

SCENARIO_PLAYER_AUTOWALK_STATE_ERROR = "scenario player autowalk state reads must stay behind player-slot proof"

SCENARIO_ANTI_PLAYER_GLOBAL_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteChrSetTeam": {
        "pre": (
            's_aiEntityLifecycleGraphReady("chr_set_team"',
            "s_aiGraphCountRuntimeSetupChrRowsFromSource(",
        ),
        "required": (
            's_aiGraphRequireRuntimePlayerSlot("chr_set_team"',
            "PLAYER_IS_ANTI(player)",
            "AI action chr_set_team chr=%d chr_rows=%d target_chr=%d checked_players=%d team=%d applied=%d",
        ),
    },
}

SCENARIO_ANTI_PLAYER_GLOBAL_ERROR = "scenario Anti-player global reads must stay behind chr_set_team source proof"

SCENARIO_PLAYER_CONTROL_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteRevokeControl": (
        's_aiGraphResolvePlayerChr("revoke_control"',
        's_aiGraphRequireRuntimePlayerSlot("revoke_control"',
        "g_PlayersWithControl[playernum] = false",
    ),
    "scenarioSourceAiGraphExecuteGrantControl": (
        's_aiGraphResolvePlayerChr("grant_control"',
        's_aiGraphRequireRuntimePlayerSlot("grant_control"',
        "g_PlayersWithControl[playernum] = true",
    ),
}

SCENARIO_PLAYER_CONTROL_ERROR = "scenario player-control table writes must stay behind runtime player-slot proof helpers"

SCENARIO_AUDIO_ALIAS_SOURCE_PROVEN_FUNCTIONS = {
    "s_aiGraphNormalizeAudioRuntimeId": (
        "ref.hasconfig && ref.confignum < (u32)g_NumAudioRussMappings",
        "mapped.packed = g_AudioRussMappings[ref.confignum].soundnum",
        "return (s32)mapped.id",
    ),
    "s_aiGraphResolveMp3FileNum": (
        "ref.hasconfig && ref.confignum < (u32)g_NumAudioRussMappings",
        "mapped.packed = g_AudioRussMappings[ref.confignum].soundnum",
    ),
}

SCENARIO_AUDIO_CATALOG_RESOLVER_PROOF_TOKENS = {
    "s_aiGraphResolveAudioCatalogId": (
        "s32 leaf_id = s_aiGraphNormalizeAudioRuntimeId(audio_id);",
        "result = catalogResolveSound(leaf_id);",
        "assetCatalogGetByIndex(result.catalog_id)",
        "return entry->id;",
        "s_aiGraphRuntimeFailure(action, reason);",
    ),
    "s_aiGraphResolveSpeechAudioSourceId": (
        "s_aiGraphResolveMp3FileNum(audio_id, &file_num)",
        "return s_aiGraphResolveAudioCatalogId(action, audio_id, role);",
        "romExtractRelPathForFilenum(file_num",
        "fsFileSize(rel_path) <= 0",
        "romdataFileGetName(file_num)",
        "return out_id;",
    ),
}

SCENARIO_AUDIO_ALIAS_ERROR = "scenario audio alias mappings and counts must stay behind catalog audio resolution proof"

SCENARIO_AUDIO_SOURCE_METADATA_PROOF_TOKENS = {
    "s_aiGraphResolveSpeechAudioSourceId": (
        "s_aiGraphResolveMp3FileNum(audio_id, &file_num)",
        "romExtractRelPathForFilenum(file_num",
        "fsFileSize(rel_path) <= 0",
        "romdataFileGetName(file_num)",
        "return out_id;",
    ),
}

SCENARIO_AUDIO_SOURCE_METADATA_ERROR = "scenario ROM audio metadata helpers must stay behind extracted speech source proof"

SCENARIO_SPECIAL_DEATH_ANIM_SOURCE_PROVEN_FUNCTIONS = {
    "s_aiGraphResolveSpecialDeathAnimationCatalogIds": (
        "specialdie < SPECIALDIE_FALLBACK || specialdie > SPECIALDIE_ONCHAIR",
        "s_aiGraphResolveAnimationCatalogId(action,",
        "g_SpecialDieAnims[SPECIALDIE_ONCHAIR].animnum",
        "g_SpecialDieAnims[SPECIALDIE_ONCHAIR + 1].animnum",
        "g_SpecialDieAnims[SPECIALDIE_ONCHAIR - 1].animnum",
        "g_SpecialDieAnims[specialdie - 1].animnum",
    ),
}

SCENARIO_SPECIAL_DEATH_ANIM_ASSIGNMENT_PROOF_TOKENS = {
    "scenarioSourceAiGraphExecuteSetChrSpecialDeathAnimation": (
        "s_aiGraphRequireRuntimeCharacterRefFromBase(",
        "s_aiGraphResolveSpecialDeathAnimationCatalogIds(",
        "chr->specialdie = animation;",
        "anim_id=%s alt_anim_id=%s chair_fallback_anim_id=%s",
    ),
}

SCENARIO_SPECIAL_DEATH_ANIM_ERROR = "scenario special-death animation table reads must stay behind animation catalog proof"

SCENARIO_QUIP_ASSET_TABLE_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteSayQuip": (
        's_aiGraphRequireQuipShuffleNode("say_quip"',
        's_aiGraphRequireRuntimeCharacterStatePointer("say_quip"',
        "s_aiGraphResolveAudioCatalogId(",
        "s_aiGraphResolveLangTextCatalogId(",
        "audio_id=%s text_id=%s",
    ),
    "scenarioSourceAiGraphExecuteSayCiStaffQuip": (
        's_aiGraphRequireQuipShuffleNode("say_ci_staff_quip"',
        's_aiGraphRequireRuntimeCharacterPointer("say_ci_staff_quip"',
        "s_aiGraphResolveAudioCatalogId(",
        "audio_id=%s",
    ),
}

SCENARIO_QUIP_ASSET_TABLE_ERROR = "scenario quip asset tables and bank pointers must stay behind character and catalog proof"

SCENARIO_CHR_FIND_SOURCE_PROVEN_FUNCTIONS = {
    "s_aiGraphRequireRuntimeCharacterRefFromBase": (
        "chr = chrFindById(basechr, chr_id);",
        "s_aiGraphRuntimeChrIsSourceSpawned(chr)",
        "s_aiGraphCountRuntimeSetupChrRowsFromSource(action",
        "s_aiGraphSetupSourceContainsChrnum(chr->chrnum)",
    ),
    "s_aiGraphResolveRuntimeCharacterRefOrSelector": (
        "chr = chrFindById(basechr, chr_id);",
        "s_aiGraphChrIdIsRuntimeSelector(chr_id)",
        "s_aiGraphRuntimeChrIsSourceSpawned(chr)",
        "s_aiGraphCountRuntimeSetupChrRowsFromSource(action",
        "s_aiGraphSetupSourceContainsChrnum(chr->chrnum)",
    ),
    "s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector": (
        "chr = chrFindById(basechr, chr_id);",
        "s_aiGraphChrIdIsRuntimeSelector(chr_id)",
        "s_aiGraphRuntimeChrIsSourceSpawned(chr)",
        "s_aiGraphCountRuntimeSetupChrRowsFromSource(action",
        "s_aiGraphSetupSourceContainsChrnum(chr->chrnum)",
    ),
    "s_aiGraphRuntimeCharacterRefLooksSourceBacked": (
        "chr = chrFindById(basechr, chr_id);",
        "s_aiGraphRuntimeChrIsSourceSpawned(chr)",
        "s_aiGraphTryCountRuntimeSetupChrRowsFromSource(&chr_count)",
        "s_aiGraphSetupSourceContainsChrnum(chr->chrnum)",
    ),
}

SCENARIO_CHR_FIND_ERROR = "scenario chrFindById access must stay inside source-aware character-reference helpers"

SCENARIO_LITERAL_CHR_FIND_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteSetTeamOrders": (
        '"set_team_orders", target',
        "checked_chrs++",
        "AI action set_team_orders chr_rows=%d source_chr=%d action=%d chrs=%d checked=%d",
    ),
    "scenarioSourceAiGraphExecuteSetChrPresetToUnalertedTeammate": (
        '"set_chr_preset_to_unalerted_teammate"',
        "checked_chrs++",
        "AI action set_chr_preset_to_unalerted_teammate chr_rows=%d source_chr=%d checked=%d candidate=%d",
    ),
    "scenarioSourceAiGraphExecuteIfChrNotTalking": (
        '"if_chr_not_talking"',
        "target_chrnum",
        "AI condition if_chr_not_talking chr=%d chr_rows=%d target_chr=%d",
    ),
    "scenarioSourceAiGraphExecuteIfChrInSquadronDoingAction": (
        '"if_chr_in_squadron_doing_action"',
        "checked_chrs++",
        "AI condition if_chr_in_squadron_doing_action chr_rows=%d source_chr=%d checked=%d",
    ),
    "s_aiGraphSafety2Score": (
        '"if_safety2_less_than", nearby',
        "checked_chrs++",
        "out_checked_chrs",
    ),
    "scenarioSourceAiGraphExecuteDetectEnemyOnSameFloor": (
        '"detect_enemy_on_same_floor"',
        "s_aiGraphRuntimeCharacterPointerLooksSourceBacked(",
        "checked_chrs++",
        "AI condition detect_enemy_on_same_floor chr_rows=%d source_chr=%d checked=%d",
    ),
    "scenarioSourceAiGraphExecuteDetectEnemy": (
        "s_aiGraphRuntimeCharacterPointerLooksSourceBacked(",
        "checked_chrs++",
        "AI condition detect_enemy chr_rows=%d source_chr=%d checked=%d",
    ),
    "scenarioSourceAiGraphExecuteIfSafetyLessThan": (
        '"if_safety_less_than", nearby',
        "checked_chrs++",
        "AI condition if_safety_less_than chr_rows=%d source_chr=%d checked=%d",
    ),
    "scenarioSourceAiGraphExecuteIfSquadronIsDead": (
        '"if_squadron_is_dead"',
        "checked_chrs++",
        "AI condition if_squadron_is_dead chr_rows=%d checked=%d",
    ),
    "scenarioSourceAiGraphExecuteIfNumChrsInSquadronGreaterThan": (
        '"if_num_chrs_in_squadron_greater_than"',
        "checked_chrs++",
        "AI condition if_num_chrs_in_squadron_greater_than chr_rows=%d checked=%d",
    ),
    "scenarioSourceAiGraphExecuteSayQuip": (
        '"say_quip", loopchr',
        "checked_chrs++",
        "AI action say_quip chr=%d chr_rows=%d target_chr=%d source_chr=%d checked=%d",
    ),
    "scenarioSourceAiGraphExecuteIncreaseSquadronAlertness": (
        '"increase_squadron_alertness"',
        "checked_chrs++",
        "AI action increase_squadron_alertness chr_rows=%d source_chr=%d checked=%d",
    ),
}

SCENARIO_LITERAL_CHR_FIND_ERROR = "scenario chrFindByLiteralId access must stay behind runtime character proof helpers"

SCENARIO_PROP_TARGET_INDEX_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteSetTarget": (
        's_aiGraphRequireOptionalRuntimeCharacterStatePointer("set_target"',
        "s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
        "if (!target_chr)",
    ),
    "scenarioSourceAiGraphExecuteIfPresetsTargetIsNotMyTarget": (
        's_aiGraphRequireRuntimeCharacterPointer(\n\t\t\t"if_presets_target_is_not_my_target"',
        "chr->chrpreset1 != -1",
    ),
    "scenarioSourceAiGraphExecuteDetectEnemyOnSameFloor": (
        "s_aiGraphRuntimeCharacterPointerLooksSourceBacked(chr",
        "s_aiGraphRuntimeCharacterPointerLooksSourceBacked(",
    ),
    "scenarioSourceAiGraphExecuteDetectEnemy": (
        "s_aiGraphRuntimeCharacterPointerLooksSourceBacked(chr",
        "s_aiGraphRuntimeCharacterPointerLooksSourceBacked(",
    ),
    "scenarioSourceAiGraphExecuteSetTargetToEyespyIfInSight": (
        's_aiGraphRequireOptionalRuntimeCharacterPointer(\n\t\t\t"set_target_to_eyespy_if_in_sight", chr',
        '"set_target_to_eyespy_if_in_sight",\n\t\t\t\t\ttargetchr',
    ),
}

SCENARIO_PROP_TARGET_INDEX_REQUIRED_TOKENS = {
    "scenarioSourceAiGraphExecuteSetTarget": (
        "target_chr_rows=%d target_chr=%d resolved_target_chr=%d",
    ),
    "scenarioSourceAiGraphExecuteIfPresetsTargetIsNotMyTarget": (
        "preset_target=%d target=%d",
    ),
    "scenarioSourceAiGraphExecuteDetectEnemyOnSameFloor": (
        "checked=%d scan=%.3f target=%d",
    ),
    "scenarioSourceAiGraphExecuteDetectEnemy": (
        "checked=%d maxdist=%.3f target=%d",
    ),
    "scenarioSourceAiGraphExecuteSetTargetToEyespyIfInSight": (
        "target_chr_rows=%d source_target_chr=%d",
    ),
}

SCENARIO_PROP_TARGET_INDEX_ERROR = "scenario prop-target index derivation must stay behind runtime character proof helpers"

SCENARIO_LIST_CONTROL_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteSetReturnList",
    "scenarioSourceAiGraphExecuteSetShotList",
    "scenarioSourceAiGraphExecuteReturnList",
}

SCENARIO_LIST_CONTROL_GLOBAL_ERROR = "scenario list-control access must stay behind runtime actor proof helpers"

SCENARIO_QUIP_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteSayQuip",
}

SCENARIO_QUIP_GLOBAL_ERROR = "scenario quip access must stay behind runtime character proof helpers"

SCENARIO_CHARACTER_STATE_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteSetCameraAnimation",
}

SCENARIO_CHARACTER_STATE_GLOBAL_ERROR = "scenario character-state access must stay behind runtime character proof helpers"

SCENARIO_SETUP_EQUIPMENT_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteObjectDoAnimation",
    "scenarioSourceAiGraphExecuteTryEquipHat",
    "scenarioSourceAiGraphExecuteTryEquipWeapon",
}

SCENARIO_SETUP_EQUIPMENT_GLOBAL_ERROR = "scenario setup/equipment access must stay behind runtime character proof helpers"

SCENARIO_CUTSCENE_VISIBILITY_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteShowCutsceneChrs",
}

SCENARIO_CUTSCENE_VISIBILITY_SLOT_ERROR = "scenario cutscene visibility writes must use source-proven character slot pointers"

SCENARIO_CUTSCENE_VISIBILITY_SLOT_SCAN_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteShowCutsceneChrs": (
        's_aiGraphCountRuntimeSetupChrRowsFromSource(\n\t\t\t"show_cutscene_chrs"',
        "slot_count = chrsGetNumSlots();",
        "struct chrdata *checked_chr_slots[slot_count];",
        "chr = &g_ChrSlots[i];",
        's_aiGraphRequireRuntimeCharacterPointer(\n\t\t\t\t\t\t\t"show_cutscene_chrs", chr',
        "checked_chr_slots[checked_chrs++] = chr;",
    ),
}

SCENARIO_CUTSCENE_VISIBILITY_SLOT_SCAN_ERROR = "scenario cutscene visibility slot scans must preflight source-proven character slots"

SCENARIO_MISSION_GLOBAL_PLAYER_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteKillBond",
}

SCENARIO_MISSION_GLOBAL_PLAYER_ERROR = "scenario mission-global player writes must use source-proven local player pointers"

SCENARIO_PLAYER_GLOBAL_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteIfChrActivatedObject",
    "scenarioSourceAiGraphExecuteKillBond",
    "scenarioSourceAiGraphExecuteToggleP1P2",
}

SCENARIO_PLAYER_GLOBAL_POINTER_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteIfChrActivatedObject": (
        ("g_Vars.bond", "g_Vars.coop"),
        (
            "struct player *bond = g_Vars.bond;",
            "struct player *coop = g_Vars.coop;",
            '"if_chr_activated_object", bond',
            '"if_chr_activated_object", coop',
            "chr->prop == bond->prop",
            "chr->prop == coop->prop",
        ),
    ),
    "scenarioSourceAiGraphExecuteKillBond": (
        ("g_Vars.bond",),
        (
            "struct player *bond = g_Vars.bond;",
            's_aiGraphRequireRuntimePlayerPointer("kill_bond", bond',
            "bond->isdead = true;",
        ),
    ),
    "scenarioSourceAiGraphExecuteToggleP1P2": (
        ("g_Vars.bond", "g_Vars.coop"),
        (
            "struct player *bond = g_Vars.bond;",
            "struct player *coop = g_Vars.coop;",
            's_aiGraphRequireRuntimePlayerPointer("toggle_p1p2"',
            '"toggle_p1p2", coop',
            "coop->isdead",
            "bond->isdead",
        ),
    ),
}

SCENARIO_PLAYER_IDENTITY_GLOBAL_SOURCE_PROVEN_FUNCTIONS = {
    "scenarioSourceAiGraphExecuteIfChrActivatedObject": {
        "globals": ("g_Vars.coopplayernum", "g_Vars.coopplayernum"),
        "pre": (
            's_aiGraphRequireObjectInteractionNode("if_chr_activated_object"',
            's_aiGraphRequireRuntimeObjectTag("if_chr_activated_object"',
            "s_aiGraphRequireRuntimeCharacterRef(",
            '"if_chr_activated_object", bond',
        ),
        "required": (
            '"if_chr_activated_object", coop',
            "chr->prop == bond->prop",
            "chr->prop == coop->prop",
            "AI condition if_chr_activated_object chr=%d chr_rows=%d target_chr=%d player_checked=%d",
        ),
    },
    "scenarioSourceAiGraphExecuteSayQuip": {
        "globals": (
            "g_Vars.coopplayernum",
            "g_Vars.bondplayernum",
            "g_Vars.coopplayernum",
            "g_Vars.bondplayernum",
        ),
        "pre": (
            's_aiGraphRequireQuipShuffleNode("say_quip"',
            's_aiGraphRequireRuntimeCharacterRefFromBase("say_quip"',
            's_aiGraphRequireRuntimeCharacterStatePointer("say_quip"',
            's_aiGraphRequireRuntimePlayerSlot("say_quip"',
        ),
        "required": (
            "alternate_playernum = playernum == g_Vars.bondplayernum",
            's_aiGraphRequireRuntimePlayerSlot("say_quip"',
            "AI action say_quip chr=%d chr_rows=%d target_chr=%d source_chr=%d",
        ),
    },
    "scenarioSourceAiGraphExecuteClearInventory": {
        "globals": ("g_Vars.bondplayernum", "g_Vars.coopplayernum"),
        "pre": (
            "missing scenario.ai.action.clear_inventory node",
            "missing ai/ailists.json source",
            's_aiGraphRequireRuntimePlayerSlot("clear_inventory"',
            "setCurrentPlayerNum(playernum)",
        ),
        "required": (
            "invClear()",
            "bgunEquipWeapon(WEAPON_UNARMED)",
            "AI action clear_inventory checked_players=%d players=%d",
        ),
    },
    "scenarioSourceAiGraphExecuteToggleP1P2": {
        "globals": (
            "g_Vars.coopplayernum",
            "g_Vars.bondplayernum",
            "g_Vars.coopplayernum",
            "g_Vars.bondplayernum",
        ),
        "pre": (
            "missing scenario.ai.action.toggle_p1p2 node",
            "missing ai/ailists.json source",
        ),
        "required": (
            's_aiGraphRequireRuntimePlayerPointer("toggle_p1p2"',
            '"toggle_p1p2", coop',
            's_aiGraphRequireRuntimeCharacterRefFromBase(\n\t\t\t\t"toggle_p1p2"',
            "coop->isdead",
            "bond->isdead",
            "AI action toggle_p1p2 chr=%d chr_rows=%d target_chr=%d player_checked=%d",
        ),
    },
    "scenarioSourceAiGraphExecuteChrSetP1P2": {
        "globals": (
            "g_Vars.coopplayernum",
            "g_Vars.coopplayernum",
            "g_Vars.coopplayernum",
            "g_Vars.bondplayernum",
        ),
        "pre": (
            "missing scenario.ai.action.chr_set_p1p2 node",
            "missing ai/ailists.json source",
        ),
        "required": (
            "s_aiGraphRequireRuntimeCharacterRefFromBase(",
            's_aiGraphRequireRuntimePlayerSlot("chr_set_p1p2"',
            "player->isdead",
            "AI action chr_set_p1p2 chr=%d chr_rows=%d source_chr=%d target_chr=%d",
        ),
    },
}

SCENARIO_PLAYER_IDENTITY_GLOBAL_ERROR = "scenario Bond/Co-op player-number globals must stay behind source/player proof"

SCENARIO_PLAYER_GLOBAL_ERROR = "scenario player-global access must use source-proven local player pointers"

SCENARIO_AI_INTERPRETER_GLOBALS = {
    "g_Vars.aioffset",
    "g_Vars.ailist",
}

SCENARIO_RUNTIME_GLOBAL_MAX_COUNTS = {
    "g_Vars.currentplayernum": 27,
    "g_Vars.chrdata": 14,
    "g_Vars.coopplayernum": 10,
    "g_Vars.stagenum": 6,
    "g_Vars.hovercar": 6,
    "g_Vars.bondplayernum": 6,
    "g_Vars.roomcount": 4,
    "g_Vars.props": 4,
    "g_Vars.truck": 4,
    "g_Vars.bond": 3,
    "g_Vars.normmplayerisrunning": 5,
    "g_Vars.autocutplaying": 3,
    "g_Vars.currentplayer": 3,
    "g_Vars.heli": 3,
    "g_Vars.coop": 2,
    "g_Vars.killcount": 2,
    "g_Vars.lvframenum": 2,
    "g_Vars.tickmode": 1,
    "g_Vars.lvframe60": 1,
    "g_Vars.numaibuddies": 1,
    "g_Vars.players": 1,
    "g_Vars.autocutfinished": 1,
    "g_Vars.antiplayernum": 1,
}

SCENARIO_RUNTIME_GLOBAL_INVENTORY_ERROR = "scenario runtime globals must be inventoried by source-boundary guards or interpreter exceptions"

SCENARIO_RUNTIME_EXTERNAL_GLOBAL_MAX_COUNTS = {
    "g_StageSetup": 15,
    "g_Rooms": 10,
    "g_PadsFile": 6,
    "g_SourceWidePadOffsetCount": 5,
    "g_SourceWidePadOffsets": 5,
    "g_SpecialDieAnims": 5,
    "g_GuardQuipBank": 4,
    "g_SourceWidePadFile": 4,
    "g_BgNumPortalCameraCacheItems": 3,
    # B-943: scenarioSourceLoadRoomLightsForStage produces the flat struct
    # light[] backing for g_BgLightsFileData (assigned in bg.c); the references
    # here are explanatory comments documenting that source boundary.
    "g_BgLightsFileData": 2,
    "g_MissionConfig": 3,
    "g_MpAllChrPtrs": 2,
    "g_MpNumChrs": 4,
    "g_SpecialQuipBank": 3,
    "g_BgPortals": 2,
    "g_ChrSlots": 2,
    "g_CiAnnoyedQuips": 2,
    "g_CiAnnoyedQuipsCount": 2,
    "g_CiGreetingQuips": 2,
    "g_CiGreetingQuipsCount": 2,
    "g_CiMainQuips": 2,
    "g_CiMainQuipsCount": 2,
    "g_CiThanksQuips": 2,
    "g_CiThanksQuipsCount": 2,
    "g_CutsceneFrameOverrun240": 2,
    "g_MaianQuipBank": 2,
    "g_MusicEventQueueLength": 2,
    "g_PlayerInvincible": 2,
    "g_PlayersWithControl": 2,
    "g_QuipTexts": 4,
    "g_SkedarQuipBank": 2,
    "g_SkyWindSpeed": 2,
    "g_AudioManager": 1,
    "g_AudioRussMappings": 2,
    "g_Lifts": 1,
    "g_NumAudioRussMappings": 2,
    "g_TagsLinkedList": 1,
    "g_TintedGlassEnabled": 1,
}

SCENARIO_RUNTIME_EXTERNAL_GLOBAL_INVENTORY_ERROR = "scenario runtime external globals must be inventoried by source-boundary guards"


def repo_root() -> Path:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            check=True,
            capture_output=True,
            text=True,
        )
        raw_root = result.stdout.strip()
        if len(raw_root) >= 3 and raw_root[0] == "/" and raw_root[2] == "/":
            raw_root = raw_root[1].upper() + ":" + raw_root[2:]
        root = Path(raw_root)
        if root.exists():
            return root
    except (FileNotFoundError, subprocess.CalledProcessError):
        pass
    root = Path.cwd()
    while root != root.parent:
        if all((root / marker).exists() for marker in ROOT_MARKERS):
            return root
        root = root.parent
    raise SystemExit("asset-native-source guard: could not find repo root")


def relpath(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def read_text(root: Path, rel: str) -> str:
    path = root / rel
    if not path.exists():
        raise AssertionError(f"missing required contract file: {rel}")
    return path.read_text(encoding="utf-8", errors="replace")


def require_sentinals(root: Path) -> list[str]:
    errors: list[str] = []
    for rel, needles in CONTRACT_SENTINELS.items():
        try:
            text = read_text(root, rel)
        except AssertionError as exc:
            errors.append(str(exc))
            continue
        for needle in needles:
            if needle not in text:
                errors.append(f"{rel} missing asset contract sentinel: {needle}")
    return errors


def require_kanban(root: Path) -> list[str]:
    errors: list[str] = []
    try:
        data = json.loads(read_text(root, "tools/kanban/state.json"))
    except (json.JSONDecodeError, AssertionError) as exc:
        return [f"tools/kanban/state.json invalid or missing: {exc}"]

    cards = data.get("cards", [])
    card = next((c for c in cards if c.get("id") == "c3842"), None)
    if not card:
        return ["tools/kanban/state.json missing c3842 card"]
    column = card.get("column")
    if column not in {"active", "done"}:
        errors.append("c3842 must stay active or done after the native-source audit closes")
    if card.get("priority") != 1:
        errors.append("c3842 must stay priority 1")
    subtasks = card.get("subtasks", [])
    status_by_id = {
        str(s.get("id", "")): s.get("status")
        for s in subtasks
        if str(s.get("id", "")).startswith("c3842-s")
    }
    definitive_ids = ("c3842-s7", "c3842-s8", "c3842-s9")
    for subtask_id in definitive_ids:
        if subtask_id not in status_by_id:
            errors.append(f"{subtask_id} definitive optional-slot subtask is missing")

    if column == "active":
        definitive_statuses = [status_by_id.get(s) for s in definitive_ids]
        if not all(status in {"active", "todo", "done"} for status in definitive_statuses):
            errors.append("c3842 definitive optional-slot subtasks have invalid status")
        if (not all(status == "done" for status in definitive_statuses) and
                "active" not in definitive_statuses):
            errors.append(
                "one of c3842-s7 through c3842-s9 must be active while c3842 is active"
            )
    else:
        incomplete = [
            subtask_id
            for subtask_id, status in status_by_id.items()
            if status != "done"
        ]
        if incomplete:
            errors.append(
                "c3842 is done but has incomplete native-source subtasks: "
                + ", ".join(incomplete)
            )
    rom_card = next((c for c in cards if c.get("id") == "c3844"), None)
    if not rom_card:
        errors.append("tools/kanban/state.json missing c3844 card")
    else:
        if rom_card.get("priority") not in (1, "critical"):
            errors.append("c3844 must stay priority 1 or critical")
        if rom_card.get("column") not in {"active", "done"}:
            errors.append("c3844 must stay active or done until runtime ROM fallback removal closes")
        # The ROM-fallback-as-asset-chain-failure framing lives in
        # context/tasks.md (checked by require_sentinals). The c3844 card itself
        # must still track runtime ROM/RomProvider fallback removal in its text
        # or its subtasks.
        rom_text = " ".join(
            str(rom_card.get(k, ""))
            for k in ("title", "description", "notes")
        )
        rom_text += " " + " ".join(
            str(s.get("title", "")) + " " + str(s.get("notes", ""))
            for s in rom_card.get("subtasks", [])
        )
        if "ROM fallback" not in rom_text and "ROM/RomProvider fallback" not in rom_text:
            errors.append("c3844 must explicitly track ROM fallback removal")
        if not any(str(s.get("id", "")).startswith("c3844-s") for s in rom_card.get("subtasks", [])):
            errors.append("c3844 must carry ordered ROM fallback removal subtasks")
    return errors


def public_source_entries(names: list[str], descriptor: str) -> list[str]:
    public = []
    for name in names:
        normalized = name.replace("\\", "/")
        if normalized.endswith("/"):
            continue
        if normalized == descriptor:
            continue
        if normalized.startswith("_meta/"):
            continue
        if normalized.endswith(".sha256"):
            continue
        public.append(normalized)
    return public


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


BONDGUN_MODEL_SOURCE_ONLY_ERROR = (
    "bondgun queued model streaming must enforce source-only public model "
    "handles before direct asset loads"
)

MODEL_HANDLE_SOURCE_ONLY_ERROR = (
    "runtime model handle size queries must enforce source-only public model "
    "handles before direct asset loads"
)

MODELCATALOG_SOURCE_ONLY_ERROR = (
    "modelcatalog body/head validation must enforce source-only public handles "
    "before provider size checks"
)

CATALOG_METADATA_PRELOAD_SOURCE_ONLY_ERROR = (
    "catalog bundled metadata preloads must enforce source-only public sources "
    "before runtime activation"
)

FORGE_RUNTIME_SOURCE_ONLY_ERROR = (
    "Forge runtime model spawns must enforce source-only public family handles "
    "before modeldef loads"
)

SETUP_MODELDEF_SOURCE_ONLY_ERROR = (
    "setup modeldef loads must enforce source-only public model handles "
    "before modeldef loads"
)

CHARACTER_MANAGER_MODELDEF_SOURCE_ONLY_ERROR = (
    "body/head manager modeldef fallbacks must enforce source-only public "
    "family handles before modeldef loads"
)

MUSIC_SEQUENCE_SOURCE_ONLY_ERROR = (
    "music sequencer fallback must enforce source-only audio before legacy "
    "sequence or ROM/static fallback"
)

SOUND_FILE_SOURCE_ONLY_ERROR = (
    "file-source sound playback must refuse ROM fallback in source-only audio"
)

MP3_AUDIO_SOURCE_ONLY_ERROR = (
    "MP3 playback must enforce source-only audio before ROM file playback"
)

MP3_DURATION_SOURCE_ONLY_ERROR = (
    "MP3 duration must enforce source-only audio before ROM file-size fallback"
)


def scan_bondgun_model_source_only_guards(root: Path) -> list[str]:
    try:
        text = read_text(root, "src/game/bondgun.c")
    except AssertionError as exc:
        return [str(exc)]

    required = [
        '#include "asset_source_debug.h"',
        "static bool bgunQueuedLoadPassesSourceOnlyCheck",
        "catalogIdBySourceHandle(ASSET_MODEL, player->gunctrl.loadhandle)",
        "assetSourceDebugFatalHandleFallback(ASSET_MODEL",
        "assetSourceDebugHandleRequiresPublicFileSource(ASSET_MODEL",
        (
            'if (bgunQueuedLoadPassesSourceOnlyCheck(player, '
            '"weapon model inflated-size")) {\n'
            "\t\treturn assetLoadGetInflatedSize"
        ),
        (
            'if (bgunQueuedLoadPassesSourceOnlyCheck(player, '
            '"weapon model loaded-size")) {\n'
            "\t\treturn assetLoadGetLoadedSize"
        ),
        (
            'if (bgunQueuedLoadPassesSourceOnlyCheck(player, '
            '"weapon model load-to-addr")) {\n'
            "\t\treturn assetLoadToAddr"
        ),
    ]

    missing = [needle for needle in required if needle not in text]
    if missing:
        return [BONDGUN_MODEL_SOURCE_ONLY_ERROR + ": missing " + ", ".join(missing)]
    return []


def scan_model_handle_source_only_guards(root: Path) -> list[str]:
    try:
        menu = read_text(root, "src/game/menu.c")
        mainmenu = read_text(root, "src/game/mainmenu.c")
        mplayer_setup = read_text(root, "src/game/mplayer/setup.c")
        player = read_text(root, "src/game/player.c")
        title = read_text(root, "src/game/title.c")
    except AssertionError as exc:
        return [str(exc)]

    checks = {
        "src/game/menu.c": (
            menu,
            [
                '#include "asset_source_debug.h"',
                "static bool menuModelHandlePassesSourceOnlyCheck",
                "assetSourceDebugFatalHandleFallback(type, context, asset_id, handle)",
                "assetSourceDebugHandleRequiresPublicFileSource(type, handle)",
                "menu body preview modeldef",
                "menu head preview modeldef",
                "menu raw model preview",
                "menuModelHandlePassesSourceOnlyCheck(ASSET_BODY",
                "menuModelHandlePassesSourceOnlyCheck(ASSET_HEAD",
                "menuModelHandlePassesSourceOnlyCheck(ASSET_MODEL",
                "catalogIdBySourceHandle(ASSET_MODEL, modelhandle)",
            ],
        ),
        "src/game/mainmenu.c": (
            mainmenu,
            [
                "weapon menu preview weaponnum=%d has no provider-backed catalog entry",
                "menuUnsetModel(&g_Menus[g_MpPlayerNum].menumodel)",
            ],
        ),
        "src/game/mplayer/setup.c": (
            mplayer_setup,
            [
                "MP head preview headnum=%d has no provider-backed catalog entry",
                "MP perfect-head preview headnum=%d has no provider-backed catalog entry",
                "menuUnsetModel(&g_Menus[g_MpPlayerNum].menumodel)",
            ],
        ),
        "src/game/player.c": (
            player,
            [
                "player chrbody weapon modeldef",
                "assetSourceDebugFatalHandleFallback(ASSET_MODEL",
                "assetSourceDebugHandleRequiresPublicFileSource(ASSET_MODEL, weapon_handle)",
                "assetLoadGetInflatedSize(weapon_handle, LOADTYPE_MODEL)",
            ],
        ),
        "src/game/title.c": (
            title,
            [
                '#include "asset_source_debug.h"',
                "title model size",
                "assetSourceDebugFatalHandleFallback(ASSET_MODEL",
                "assetSourceDebugHandleRequiresPublicFileSource(ASSET_MODEL, modelresult.handle)",
                "assetLoadGetLoadedSize(modelresult.handle)",
            ],
        ),
    }

    errors: list[str] = []
    for path, (text, required) in checks.items():
        missing = [needle for needle in required if needle not in text]
        if missing:
            errors.append(
                MODEL_HANDLE_SOURCE_ONLY_ERROR
                + f": {path} missing "
                + ", ".join(missing)
            )

    forbidden = [
        (
            "src/game/mainmenu.c",
            mainmenu,
            "MENUMODELPARAMS_SET_FILENUM(weaponGetFileNum(weaponnum))",
        ),
        (
            "src/game/mplayer/setup.c",
            mplayer_setup,
            "MENUMODELPARAMS_SET_FILENUM(catalogGetHeadFilenumByIndex(headnum))",
        ),
    ]
    for path, text, needle in forbidden:
        if needle in text:
            errors.append(
                MODEL_HANDLE_SOURCE_ONLY_ERROR
                + f": {path} still submits raw legacy preview filenum {needle}"
            )

    order_checks = [
        (
            "src/game/player.c",
            player,
            "player chrbody weapon modeldef",
            "assetLoadGetInflatedSize(weapon_handle, LOADTYPE_MODEL)",
        ),
        (
            "src/game/title.c",
            title,
            "title model size",
            "assetLoadGetLoadedSize(modelresult.handle)",
        ),
        (
            "src/game/menu.c",
            menu,
            "menu body preview modeldef",
            "assetLoadGetInflatedSize(bodyresult.handle, LOADTYPE_MODEL)",
        ),
        (
            "src/game/menu.c",
            menu,
            "menu head preview modeldef",
            "assetLoadGetInflatedSize(headresult.handle, LOADTYPE_MODEL)",
        ),
        (
            "src/game/menu.c",
            menu,
            "menu raw model preview",
            "assetLoadGetInflatedSize(modelhandle, LOADTYPE_MODEL)",
        ),
    ]
    for path, text, before, after in order_checks:
        before_index = text.find(before)
        after_index = text.find(after)
        if before_index == -1 or after_index == -1 or before_index > after_index:
            errors.append(
                MODEL_HANDLE_SOURCE_ONLY_ERROR
                + f": {path} must check {before} before {after}"
            )

    return errors


def scan_modelcatalog_source_only_guards(root: Path) -> list[str]:
    try:
        text = read_text(root, "port/src/modelcatalog.c")
    except AssertionError as exc:
        return [str(exc)]

    required = [
        '#include "asset_source_debug.h"',
        "static s32 catalogValidatePassesSourceOnlyCheck",
        "catalogValidateAssetTypeForCategory",
        "modelcatalog body validation modeldef",
        "modelcatalog head validation modeldef",
        "catalogIdBySourceHandle(type, handle)",
        "assetSourceDebugFatalHandleFallback(type, context, id, handle)",
        "assetSourceDebugHandleRequiresPublicFileSource(type, handle)",
        "source-only %s validation refused non-public model handle",
        "catalogValidatePassesSourceOnlyCheck(index, ce->category, filenum, handle)",
        "catalogValidateSourceMissing(handle, filenum)",
    ]

    missing = [needle for needle in required if needle not in text]
    errors: list[str] = []
    if missing:
        errors.append(
            MODELCATALOG_SOURCE_ONLY_ERROR
            + ": missing "
            + ", ".join(missing)
        )

    before_index = text.find(
        "catalogValidatePassesSourceOnlyCheck(index, ce->category, filenum, handle)"
    )
    after_index = text.find("catalogValidateSourceMissing(handle, filenum)")
    if before_index == -1 or after_index == -1 or before_index > after_index:
        errors.append(
            MODELCATALOG_SOURCE_ONLY_ERROR
            + ": source-only check must precede catalogValidateSourceMissing"
        )

    return errors


def scan_catalog_metadata_preload_source_only_guard(root: Path) -> list[str]:
    try:
        text = read_text(root, "port/src/assetcatalog_load.c")
    except AssertionError as exc:
        return [str(exc)]

    required = [
        "static s32 s_catalogPreloadBundledMetadataPayload(asset_entry_t *entry)",
        "assetSourceDebugEntryRequiresPublicFileSource(entry)",
        "bundled metadata preload",
        "refusing activation",
        "assetSourceDebugTypeLabel(entry->type)",
        "return s_catalogLoadEntryMetadataPayload(entry)",
        "s_catalogPreloadBundledMetadataPayload(mutable_entry)",
    ]

    missing = [needle for needle in required if needle not in text]
    errors: list[str] = []
    if missing:
        errors.append(
            CATALOG_METADATA_PRELOAD_SOURCE_ONLY_ERROR
            + ": missing "
            + ", ".join(missing)
        )

    guard_index = text.find("assetSourceDebugEntryRequiresPublicFileSource(entry)")
    activate_index = text.find("return s_catalogLoadEntryMetadataPayload(entry)")
    if guard_index == -1 or activate_index == -1 or guard_index > activate_index:
        errors.append(
            CATALOG_METADATA_PRELOAD_SOURCE_ONLY_ERROR
            + ": source-only check must precede metadata payload activation"
        )

    call_index = text.find("s_catalogPreloadBundledMetadataPayload(mutable_entry)")
    direct_init_index = text.find("(void)s_catalogLoadEntryMetadataPayload(mutable_entry)")
    if call_index == -1 or direct_init_index != -1:
        errors.append(
            CATALOG_METADATA_PRELOAD_SOURCE_ONLY_ERROR
            + ": catalog init must use the guarded preload helper"
        )

    return errors


def scan_forge_runtime_source_only_guards(root: Path) -> list[str]:
    try:
        text = read_text(root, "port/src/forge/forge_runtime.c")
    except AssertionError as exc:
        return [str(exc)]

    required = [
        '#include "asset_source_debug.h"',
        "static s32 s_forgeModelHandlePassesSourceOnlyCheck",
        "assetSourceDebugFatalHandleFallback(type, context, catalog_id, handle)",
        "assetSourceDebugHandleRequiresPublicFileSource(type, handle)",
        "source-only %s '%s' refused non-public model handle",
        "assetSourceDebugTypeLabel(type)",
        "forge door modeldef",
        "forge weapon pad modeldef",
        "forge prop modeldef",
    ]

    errors: list[str] = []
    missing = [needle for needle in required if needle not in text]
    if missing:
        errors.append(
            FORGE_RUNTIME_SOURCE_ONLY_ERROR
            + ": missing "
            + ", ".join(missing)
        )

    checks = [
        (
            "s_spawn_door",
            "s_forgeModelHandlePassesSourceOnlyCheck(ASSET_PROP, o->catalog_id",
            "modeldefLoadToNewFromHandle(pr.handle, pr.filenum)",
        ),
        (
            "s_spawn_weapon_pad",
            "s_forgeModelHandlePassesSourceOnlyCheck(ASSET_WEAPON",
            "modeldefLoadToNewFromHandle(wr.handle, wr.filenum)",
        ),
        (
            "s_spawn_prop",
            "s_forgeModelHandlePassesSourceOnlyCheck(ASSET_PROP, o->catalog_id",
            "modeldefLoadToNewFromHandle(pr.handle, pr.filenum)",
        ),
    ]

    blocks = {name: text[start:end]
              for name, start, end in iter_c_function_blocks(text)}
    for function_name, before, after in checks:
        block = blocks.get(function_name, "")
        before_index = block.find(before)
        after_index = block.find(after)
        if before_index == -1 or after_index == -1 or before_index > after_index:
            errors.append(
                FORGE_RUNTIME_SOURCE_ONLY_ERROR
                + f": {function_name} must check source-only before {after}"
            )

    return errors


def scan_setup_modeldef_source_only_guard(root: Path) -> list[str]:
    try:
        text = read_text(root, "src/game/setuputils.c")
    except AssertionError as exc:
        return [str(exc)]

    required = [
        '#include "asset_source_debug.h"',
        "static s32 setupModelHandlePassesSourceOnlyCheck",
        'assetSourceDebugFatalHandleFallback(ASSET_MODEL,\n\t\t\t"setup modeldef"',
        "assetSourceDebugHandleRequiresPublicFileSource(ASSET_MODEL, handle)",
        "source-only model '%s' modelnum=%d refused non-public model handle",
        "setupModelHandlePassesSourceOnlyCheck(modelnum, model_id, model_handle)",
        "modeldefLoadToNewFromHandle(model_handle, source_filenum)",
    ]

    errors: list[str] = []
    missing = [needle for needle in required if needle not in text]
    if missing:
        errors.append(
            SETUP_MODELDEF_SOURCE_ONLY_ERROR
            + ": missing "
            + ", ".join(missing)
        )

    blocks = {name: text[start:end]
              for name, start, end in iter_c_function_blocks(text)}
    block = blocks.get("setupLoadModeldef", "")
    before_index = block.find(
        "setupModelHandlePassesSourceOnlyCheck(modelnum, model_id, model_handle)"
    )
    after_index = block.find(
        "modeldefLoadToNewFromHandle(model_handle, source_filenum)"
    )
    if before_index == -1 or after_index == -1 or before_index > after_index:
        errors.append(
            SETUP_MODELDEF_SOURCE_ONLY_ERROR
            + ": setupLoadModeldef must check source-only before modeldefLoadToNewFromHandle"
        )

    return errors


def scan_character_manager_modeldef_source_only_guards(root: Path) -> list[str]:
    try:
        body = read_text(root, "port/src/catalog_mgr_bodies.c")
        head = read_text(root, "port/src/catalog_mgr_heads.c")
    except AssertionError as exc:
        return [str(exc)]

    checks = [
        (
            "port/src/catalog_mgr_bodies.c",
            body,
            "catalogManagerBodyModeldefPassesSourceOnlyCheck",
            "assetSourceDebugFatalHandleFallback(ASSET_BODY",
            "assetSourceDebugHandleRequiresPublicFileSource(ASSET_BODY, handle)",
            "body manager modeldef fallback",
            "catalogManagerBodyModeldefPassesSourceOnlyCheck(bodynum, id,\n\t\t\t\t\thandle)",
            "modeldefLoadToNewFromHandle(handle",
        ),
        (
            "port/src/catalog_mgr_heads.c",
            head,
            "catalogManagerHeadModeldefPassesSourceOnlyCheck",
            "assetSourceDebugFatalHandleFallback(ASSET_HEAD",
            "assetSourceDebugHandleRequiresPublicFileSource(ASSET_HEAD, handle)",
            "head manager modeldef fallback",
            "catalogManagerHeadModeldefPassesSourceOnlyCheck(headnum, id,\n\t\t\t\t\thandle)",
            "modeldefLoadToNewFromHandle(handle",
        ),
    ]

    errors: list[str] = []
    for path, text, helper, fatal, requires, context, before, after in checks:
        required = [
            '#include "asset_source_debug.h"',
            helper,
            fatal,
            requires,
            context,
            before,
            after,
        ]
        missing = [needle for needle in required if needle not in text]
        if missing:
            errors.append(
                CHARACTER_MANAGER_MODELDEF_SOURCE_ONLY_ERROR
                + f": {path} missing "
                + ", ".join(missing)
            )

        before_index = text.find(before)
        after_index = text.find(after)
        if before_index == -1 or after_index == -1 or before_index > after_index:
            errors.append(
                CHARACTER_MANAGER_MODELDEF_SOURCE_ONLY_ERROR
                + f": {path} must check source-only before {after}"
            )

    return errors


def scan_music_sequence_source_only_guard(root: Path) -> list[str]:
    try:
        load_h = read_text(root, "port/include/assetcatalog_load.h")
        load = read_text(root, "port/src/assetcatalog_load.c")
        mod = read_text(root, "port/src/mod.c")
        snd = read_text(root, "src/lib/snd.c")
    except AssertionError as exc:
        return [str(exc)]

    required = {
        "port/include/assetcatalog_load.h": (
            load_h,
                [
                    "CatalogResolveResult catalogResolveMusicSequence(s32 tracknum)",
                    "Public .pdsong track audio is routed through the streaming music path",
                    "Public sequence.mid, sequence.json, and music.ini sources are",
                    "public-source compilation",
                ],
            ),
        "port/src/assetcatalog_load.c": (
            load,
            [
                "CatalogResolveResult catalogResolveMusicSequence(s32 tracknum)",
                "e->ext.audio.category != AUDIO_CAT_MUSIC",
                "e->ext.audio.sound_id != tracknum",
                "r.path = entryGetFilePath(entry)",
                "r.is_mod_override = 1",
                "s_catalogApplySourceOnlyDebug(&r, entry)",
            ],
        ),
        "port/src/mod.c": (
            mod,
            [
                "#include \"asset_source_debug.h\"",
                "catalogResolveMusicSequence((s32)num)",
                "modSequencePlayAudioSource(u16 num)",
                    "modSequencePathHasAudioExtension(r.path)",
                    "modMusicPlay(r.path)",
                    "modMusicIsPlaying()",
                    "modSequenceCompilePublicSource",
                    "modSequenceSiblingPath(r->path, \"sequence.mid\"",
                    "modSequenceSiblingPath(r->path, \"sequence.json\"",
                    "modSequenceSiblingPath(r->path, \"music.ini\"",
                    "fsFileSize(mid_path) <= 0",
                    "modSequenceLoadEventsJson",
                    "fsFileLoad(path, &size)",
                    "modSequenceBuildAlcBuffer",
                    "modSequencePutBe32(data + 64, division)",
                    "AL_CMIDI_LOOPSTART_CODE",
                    "AL_CMIDI_LOOPEND_CODE",
                    "r.source_only_blocked",
                    "streaming playback failed",
                    "ASSET.SOURCE_ONLY: music sequence",
                    "sequencer-native ",
                    "public source compile failed",
                    "but has no public FileProvider source",
                    "assetSourceDebugIsEnabledFor(ASSET_AUDIO)",
                    "refusing ROM/static fallback",
                ],
            ),
        "src/lib/snd.c": (
            snd,
            [
                "modSequencePlayAudioSource(seq->tracknum)",
                "modSequenceLoad(seq->tracknum, &extlen)",
                "seq->tracknum >= g_SeqTable->count",
                "g_SeqRomAddrs[seq->tracknum] < 0x10000",
                "dmaExec(zipstart, g_SeqRomAddrs[seq->tracknum], ziplen)",
            ],
        ),
    }

    errors: list[str] = []
    for path, (text, needles) in required.items():
        missing = [needle for needle in needles if needle not in text]
        if missing:
            errors.append(
                MUSIC_SEQUENCE_SOURCE_ONLY_ERROR
                + f": {path} missing "
                + ", ".join(missing)
            )

    mod_blocks = {name: mod[start:end]
                  for name, start, end in iter_c_function_blocks(mod)}
    mod_sequence = mod_blocks.get("modSequenceLoad", "")
    mod_sequence_audio = mod_blocks.get("modSequencePlayAudioSource", "")
    snd_blocks = {name: snd[start:end]
                  for name, start, end in iter_c_function_blocks(snd)}
    snd_seq_play = snd_blocks.get("seqPlay", "")
    order_checks = [
        (
              "port/src/mod.c",
              mod_sequence,
              "catalogResolveMusicSequence((s32)num)",
              "modSequenceCompilePublicSource(&r, num, outSize)",
          ),
          (
              "port/src/mod.c",
              mod,
              "modSequenceSiblingPath(r->path, \"sequence.mid\"",
              "fsFileSize(mid_path) <= 0",
          ),
          (
              "port/src/mod.c",
              mod,
              "fsFileSize(mid_path) <= 0",
              "modSequenceLoadEventsJson(json_path, tracks, &event_count)",
          ),
          (
              "port/src/mod.c",
              mod_sequence,
              "modSequenceCompilePublicSource(&r, num, outSize)",
              "assetSourceDebugIsEnabledFor(ASSET_AUDIO)",
          ),
          (
              "port/src/mod.c",
              mod_sequence,
              "assetSourceDebugIsEnabledFor(ASSET_AUDIO)",
              "r.source_only_blocked",
          ),
          (
              "port/src/mod.c",
              mod_sequence,
              "modSequenceCompilePublicSource(&r, num, outSize)",
              "fsFileSize(MOD_SEQUENCES_DIR \"/\")",
          ),
        (
            "port/src/mod.c",
            mod_sequence,
            "r.source_only_blocked",
            "snprintf(path, sizeof(path), MOD_SEQUENCES_DIR \"/%04x.bin\", num)",
        ),
        (
            "port/src/mod.c",
            mod_sequence,
            "r.source_only_blocked",
            "return NULL",
        ),
        (
            "port/src/mod.c",
            mod_sequence_audio,
            "modMusicPlay(r.path)",
            "modMusicIsPlaying()",
        ),
        (
            "port/src/mod.c",
            mod_sequence_audio,
            "assetSourceDebugIsEnabledFor(ASSET_AUDIO)",
            "falling back to legacy sequence",
        ),
        (
            "src/lib/snd.c",
            snd_seq_play,
            "modSequencePlayAudioSource(seq->tracknum)",
            "modSequenceLoad(seq->tracknum, &extlen)",
        ),
        (
            "src/lib/snd.c",
            snd_seq_play,
            "modSequenceLoad(seq->tracknum, &extlen)",
            "g_SeqRomAddrs[seq->tracknum] < 0x10000",
        ),
        (
            "src/lib/snd.c",
            snd_seq_play,
            "modSequencePlayAudioSource(seq->tracknum)",
            "dmaExec(zipstart, g_SeqRomAddrs[seq->tracknum], ziplen)",
        ),
    ]
    for path, text, before, after in order_checks:
        before_index = text.find(before)
        after_index = text.find(after, before_index + len(before)) if before_index >= 0 else -1
        if before_index == -1 or after_index == -1 or before_index > after_index:
            errors.append(
                MUSIC_SEQUENCE_SOURCE_ONLY_ERROR
                + f": {path} must check {before} before {after}"
            )

    return errors


def scan_sound_file_source_only_guard(root: Path) -> list[str]:
    try:
        text = read_text(root, "src/lib/snd.c")
    except AssertionError as exc:
        return [str(exc)]

    required = [
        "audioStartFileSound(r.path, volume, pan",
        "f32 filebasepitch = 1.0f",
        "u8 file_sample_pan = AL_PAN_CENTER",
        "u8 file_sample_volume = 127",
        "u8 file_key_volume_index = 0",
        "file_sample_pan = entry->ext.audio.sample_pan",
        "file_sample_volume = entry->ext.audio.sample_volume",
        "file_key_volume_index = (u8)(entry->ext.audio.key_min & 0x1f)",
        "filebasepitch = alCents2Ratio(cents)",
        "pitch,\n\t\t\t\t\tfilebasepitch",
        "file_sample_pan,\n\t\t\t\t\tfile_sample_volume",
        "file_key_volume_index",
        "entry ? entry->ext.audio.has_loop : 0",
        "entry ? entry->ext.audio.loop_start_samples : 0",
        "entry ? entry->ext.audio.loop_end_samples : 0",
        "entry ? entry->ext.audio.loop_count : 0",
        "entry ? entry->ext.audio.has_envelope : 0",
        "entry ? entry->ext.audio.attack_time_us : 0",
        "entry ? entry->ext.audio.release_time_us : 0",
        "assetSourceDebugIsEnabledFor(ASSET_AUDIO)",
        "ASSET.SOURCE_ONLY: sound %d maps to public file source",
        "but file playback failed; refusing ROM/static fallback",
        "MOD: sound %d catalog override failed (%s), falling back to ROM",
    ]
    missing = [needle for needle in required if needle not in text]
    errors: list[str] = []
    if missing:
        errors.append(
            SOUND_FILE_SOURCE_ONLY_ERROR + ": missing " + ", ".join(missing)
        )

    blocks = {
        name: text[start:end]
        for name, start, end in iter_c_function_blocks(text)
    }
    block = blocks.get("sndStart", "")
    play = "audioStartFileSound(r.path, volume, pan"
    before = "assetSourceDebugIsEnabledFor(ASSET_AUDIO)"
    after = "MOD: sound %d catalog override failed (%s), falling back to ROM"
    play_index = block.find(play)
    before_index = block.find(before, play_index if play_index >= 0 else 0)
    after_index = block.find(after)
    if (
        play_index == -1
        or before_index == -1
        or after_index == -1
        or not (play_index < before_index < after_index)
    ):
        errors.append(
            SOUND_FILE_SOURCE_ONLY_ERROR
            + ": sndStart must refuse source-only fallback after file playback failure"
        )

    return errors


def scan_mp3_audio_source_only_guard(root: Path) -> list[str]:
    try:
        snd_text = read_text(root, "src/lib/snd.c")
        propsnd_text = read_text(root, "src/game/propsnd.c")
    except AssertionError as exc:
        return [str(exc)]

    snd_required = [
        '#include "asset_source_debug.h"',
        '#include "fs.h"',
        '#include "romextract.h"',
        '#include "assetcatalog_load.h"',
        "static void *g_SndMp3SourceBytes = NULL",
        "static void sndMp3FreeSourceBuffer(void)",
        "static s32 sndMp3LoadPublicSourceFile(s32 filenum",
        "static s32 sndMp3ResolveSourceOrFallback(s32 filenum",
        "catalogResolveFile(filenum)",
        "fsFileLoad(source.path, &size)",
        "romExtractRelPathForFilenum(filenum, relpath",
        "fsFileLoad(relpath, &size)",
        "g_SndMp3SourceBytes = bytes",
        "sndMp3FreeSourceBuffer()",
        "assetSourceDebugIsEnabledFor(ASSET_AUDIO)",
        "ASSET.SOURCE_ONLY: MP3 file",
        "refusing loose extracted file or ROM/static playback fallback",
        "sndMp3ResolveSourceOrFallback((s32)sp20.id",
        "fileGetRomAddress(filenum)",
        "fileGetRomSize(filenum)",
        "mp3PlayFile(g_SndCurMp3.romaddr, g_SndCurMp3.romsize)",
    ]

    propsnd_required = [
        '#include "asset_source_debug.h"',
        '#include "fs.h"',
        '#include "romextract.h"',
        '#include "assetcatalog_load.h"',
        "static s32 psMp3DurationGetSourceOrFallbackSize(s32 filenum)",
        "catalogResolveFile(filenum)",
        "fsFileSize(source.path)",
        "romExtractRelPathForFilenum(filenum, relpath",
        "fsFileSize(relpath)",
        "assetSourceDebugIsEnabledFor(ASSET_AUDIO)",
        "ASSET.SOURCE_ONLY: MP3 file",
        "refusing loose extracted file or ROM/static",
        "psMp3DurationGetSourceOrFallbackSize((s32)soundnum.id)",
        "fileGetRomSize(filenum)",
    ]

    missing = [needle for needle in snd_required if needle not in snd_text]
    errors: list[str] = []
    if missing:
        errors.append(
            MP3_AUDIO_SOURCE_ONLY_ERROR + ": missing " + ", ".join(missing)
        )

    missing = [
        needle for needle in propsnd_required if needle not in propsnd_text
    ]
    if missing:
        errors.append(
            MP3_DURATION_SOURCE_ONLY_ERROR + ": missing " + ", ".join(missing)
        )

    blocks = {name: snd_text[start:end]
              for name, start, end in iter_c_function_blocks(snd_text)}
    start_block = blocks.get("sndStartMp3", "")
    resolve_block = blocks.get("sndMp3ResolveSourceOrFallback", "")
    load_block = blocks.get("sndMp3LoadPublicSourceFile", "")

    if "g_AudioConfigs[sp24.confignum]" in start_block:
        errors.append(
            MP3_AUDIO_SOURCE_ONLY_ERROR
            + ": sndStartMp3 indexes g_AudioConfigs with the configured speech alias row"
        )

    mp3_config_required = [
        "g_AudioRussMappings[sp24.confignum].audioconfig_index",
        "config->volpercentage",
        "config->pan",
        "config->flags & AUDIOCONFIGFLAG_OFFENSIVE",
        "config && (config->flags & AUDIOCONFIGFLAG_RESPONDHELLO)",
    ]
    missing = [needle for needle in mp3_config_required
               if needle not in start_block]
    if missing:
        errors.append(
            MP3_AUDIO_SOURCE_ONLY_ERROR
            + ": sndStartMp3 missing configured speech config parity "
            + ", ".join(missing)
        )

    order_checks = [
        (
            "sndStartMp3",
            start_block,
            "sndMp3ResolveSourceOrFallback((s32)sp20.id",
            "mp3PlayFile(g_SndCurMp3.romaddr, g_SndCurMp3.romsize)",
        ),
        (
            "sndMp3ResolveSourceOrFallback",
            resolve_block,
            "sndMp3LoadPublicSourceFile(filenum, outaddr, outsize)",
            "fileGetRomAddress(filenum)",
        ),
        (
            "sndMp3LoadPublicSourceFile",
            load_block,
            "catalogResolveFile(filenum)",
            "romExtractRelPathForFilenum(filenum, relpath",
        ),
        (
            "sndMp3LoadPublicSourceFile",
            load_block,
            "assetSourceDebugIsEnabledFor(ASSET_AUDIO)",
            "romExtractRelPathForFilenum(filenum, relpath",
        ),
        (
            "sndMp3LoadPublicSourceFile",
            load_block,
            "fsFileLoad(source.path, &size)",
            "g_SndMp3SourceBytes = bytes",
        ),
    ]
    for block_name, block, before, after in order_checks:
        before_index = block.find(before)
        after_index = block.find(after)
        if before_index == -1 or after_index == -1 or before_index > after_index:
            errors.append(
                MP3_AUDIO_SOURCE_ONLY_ERROR
                + f": {block_name} must check {before} before {after}"
            )

    blocks = {
        name: propsnd_text[start:end]
        for name, start, end in iter_c_function_blocks(propsnd_text)
    }
    duration_block = blocks.get("psMp3DurationGetSourceOrFallbackSize", "")
    ps_block = blocks.get("psGetDuration60", "")
    duration_checks = [
        (
            "psMp3DurationGetSourceOrFallbackSize",
            duration_block,
            "catalogResolveFile(filenum)",
            "romExtractRelPathForFilenum(filenum, relpath",
        ),
        (
            "psMp3DurationGetSourceOrFallbackSize",
            duration_block,
            "assetSourceDebugIsEnabledFor(ASSET_AUDIO)",
            "romExtractRelPathForFilenum(filenum, relpath",
        ),
        (
            "psMp3DurationGetSourceOrFallbackSize",
            duration_block,
            "assetSourceDebugIsEnabledFor(ASSET_AUDIO)",
            "fileGetRomSize(filenum)",
        ),
        (
            "psGetDuration60",
            ps_block,
            "psMp3DurationGetSourceOrFallbackSize((s32)soundnum.id)",
            "* 60 / (1024 * 24 / 8)",
        ),
    ]
    for block_name, block, before, after in duration_checks:
        before_index = block.find(before)
        after_index = block.find(after)
        if before_index == -1 or after_index == -1 or before_index > after_index:
            errors.append(
                MP3_DURATION_SOURCE_ONLY_ERROR
                + f": {block_name} must check {before} before {after}"
            )

    if "fileGetRomSize(soundnum.id)" in ps_block:
        errors.append(
            MP3_DURATION_SOURCE_ONLY_ERROR
            + ": psGetDuration60 must not read ROM size directly"
        )

    return errors


def scan_example_archives(root: Path) -> list[str]:
    example_root = root / "examples/modding/typed-pdxxx-basic"
    result = validate_archive_conformance(
        example_root,
        require_all_families=True,
        recurse=True,
    )
    return result.errors


def scan_generated_scenario_texture_contracts(root: Path) -> list[str]:
    """Fail existing user-facing generated scene.glb archives if stale.

    The extractor is the source of truth, but artists often open the generated
    archives from Build/data directly. If that local tree exists, keep it under
    the same DCC-visible UV contract as the fresh smoke output.
    """
    errors: list[str] = []
    for rel_root in GENERATED_SCENARIO_ROOTS:
        scenario_root = root / rel_root
        if not scenario_root.exists():
            continue
        for archive_path in sorted(scenario_root.glob("*.pdscenario")):
            label = relpath(archive_path, root)
            try:
                with zipfile.ZipFile(archive_path) as archive:
                    data = archive.read("scene.glb")
            except (OSError, KeyError, zipfile.BadZipFile) as exc:
                errors.append(f"{label}: cannot read scene.glb for texture-scale guard: {exc}")
                continue
            validate_scene_glb_texture_contract(label, data, errors)
        for archive_path in sorted(scenario_root.glob("*.pdarena")):
            label = relpath(archive_path, root)
            try:
                with zipfile.ZipFile(archive_path) as archive:
                    nested = [
                        name for name in sorted(archive.namelist())
                        if name.lower().endswith(".pdscenario")
                    ]
                    for name in nested:
                        try:
                            with zipfile.ZipFile(io.BytesIO(archive.read(name))) as nested_archive:
                                data = nested_archive.read("scene.glb")
                        except (OSError, KeyError, zipfile.BadZipFile) as exc:
                            errors.append(
                                f"{label}::{name}: cannot read scene.glb "
                                f"for texture-scale guard: {exc}"
                            )
                            continue
                        validate_scene_glb_texture_contract(
                            f"{label}::{name}", data, errors
                        )
            except (OSError, zipfile.BadZipFile) as exc:
                errors.append(f"{label}: cannot scan arena for nested scenario GLBs: {exc}")
    return errors


def scan_ui_chrome_source_contract(root: Path) -> list[str]:
    errors: list[str] = []
    theme_path = root / "port/fast3d/pdgui_theme.cpp"
    try:
        theme = theme_path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        return [f"could not read {theme_path}: {exc}"]

    forbidden = (
        "mods/base-game/ui-chrome",
        "base-game chrome missing",
        "ui_chrome_frame.tga",
    )
    for needle in forbidden:
        if needle in theme:
            errors.append(
                "base UI chrome must load from ui_chrome_frame.pdui, "
                f"not runtime loose source path/string {needle!r}"
            )

    required = (
        '"base:ui_chrome_frame"',
        '"ui_chrome_frame"',
        "s_generateChromeFrameBgra(bgra)",
        "s_loadPduiTexture(chrome, &w, &h)",
        "UI.CHROME: loaded base chrome from ui_chrome_frame.pdui",
    )
    for needle in required:
        if needle not in theme:
            errors.append(
                "base UI chrome .pdui source contract missing source marker "
                f"{needle!r}"
            )

    return errors


def scan_ai_command_graph_coverage(root: Path) -> list[str]:
    """Reject gameplay AI commands that bypass public scenario graph source."""
    path = root / "src/game/chraicommands.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [f"cannot read {relpath(path, root)} for AI graph coverage: {exc}"]

    pattern = re.compile(
        r"(?m)^(?:bool|s32|void|u32|u8)\s+(ai\w+)\s*\([^)]*\)"
    )
    matches = list(pattern.finditer(text))
    untracked: list[str] = []

    for index, match in enumerate(matches):
        name = match.group(1)
        start = match.start()
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        body = text[start:end]
        if "scenarioSourceAiGraphExecute" in body:
            continue
        if name in AI_INTERPRETER_ONLY_FUNCTIONS:
            continue
        if name in AI_GRAPH_PENDING_FUNCTIONS:
            continue
        line = text.count("\n", 0, start) + 1
        untracked.append(f"{name}:{line}")

    if not untracked:
        return []

    return [
        "scenario AI command graph coverage has untracked debt; add graph "
        "runtime coverage or explicitly classify the command in "
        "AI_GRAPH_PENDING_FUNCTIONS. Missing: "
        + ", ".join(untracked)
    ]


def _parse_c_numeric_defines(text: str) -> dict[str, int]:
    defines: dict[str, int] = {}
    for match in re.finditer(r"(?m)^#define\s+([A-Za-z_]\w*)\s+(0x[0-9a-fA-F]+|\d+)\b", text):
        defines[match.group(1)] = int(match.group(2), 0)
    return defines


def scan_ai_opcode_name_coverage(root: Path) -> list[str]:
    """Reject Scenario AI extraction names that flatten known commands."""
    header_path = root / "src/include/game/chraicommands.h"
    constants_path = root / "src/include/constants.h"
    extractor_path = root / "port/src/romextract_pdarena.c"
    try:
        header = header_path.read_text(encoding="utf-8")
        constants = constants_path.read_text(encoding="utf-8")
        extractor = extractor_path.read_text(encoding="utf-8")
    except OSError as exc:
        return [f"cannot read Scenario AI opcode source contract inputs: {exc}"]

    declared: dict[int, str] = {}
    for match in re.finditer(
        r"/\*0x([0-9a-fA-F]{4})\*/\s+bool\s+(ai\w+)\s*\(",
        header,
    ):
        declared[int(match.group(1), 16)] = match.group(2)

    if not declared:
        return [
            "scenario AI opcode name coverage cannot find declared commands in "
            f"{relpath(header_path, root)}"
        ]

    function_match = re.search(
        r"static\s+const\s+char\s+\*s_aiOpcodeName\s*\([^)]*\)\s*\{(?P<body>.*?)\n\}",
        extractor,
        re.DOTALL,
    )
    if not function_match:
        return [
            "scenario AI opcode name coverage cannot find s_aiOpcodeName in "
            f"{relpath(extractor_path, root)}"
        ]

    defines = _parse_c_numeric_defines(constants)
    named: dict[int, str] = {}
    unknown_cases: list[str] = []
    for match in re.finditer(
        r"case\s+([^:]+):\s*return\s+\"([^\"]+)\";",
        function_match.group("body"),
    ):
        expr = match.group(1).strip()
        if expr in defines:
            opcode = defines[expr]
        else:
            try:
                opcode = int(expr, 0)
            except ValueError:
                unknown_cases.append(expr)
                continue
        named[opcode] = match.group(2)

    flattened = [
        f"0x{opcode:04x}:{declared[opcode]}"
        for opcode, name in sorted(named.items())
        if opcode in declared and name == AI_OPCODE_NAME_DEFAULT
    ]
    missing = [
        f"0x{opcode:04x}:{name}"
        for opcode, name in sorted(declared.items())
        if opcode not in named
    ]

    errors: list[str] = []
    if unknown_cases:
        errors.append(
            "scenario AI opcode name coverage has unparseable case labels: "
            + ", ".join(unknown_cases)
        )
    if missing:
        errors.append(
            "scenario AI opcode names must cover every declared command; "
            "missing: "
            + ", ".join(missing)
        )
    if flattened:
        errors.append(
            "scenario AI opcode names must not flatten declared commands to "
            f"{AI_OPCODE_NAME_DEFAULT!r}: "
            + ", ".join(flattened)
        )
    return errors


def iter_c_function_blocks(text: str) -> list[tuple[str, int, int]]:
    pattern = re.compile(
        r"^(?:static\s+)?(?:[A-Za-z_]\w*\s+)+\*+\s*"
        r"([A-Za-z_]\w*)\s*\([^;]*?\)\s*\{|"
        r"^(?:static\s+)?(?:[A-Za-z_]\w*\s+)+"
        r"([A-Za-z_]\w*)\s*\([^;]*?\)\s*\{",
        re.MULTILINE,
    )
    matches = list(pattern.finditer(text))
    blocks: list[tuple[str, int, int]] = []
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        blocks.append(((match.group(1) or match.group(2)), match.start(), end))
    return blocks


def scan_scenario_runtime_count_guards(root: Path) -> list[str]:
    """Reject Scenario runtime row counts outside source-checked helpers."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario runtime count guards: {exc}"
        ]

    count_pattern = re.compile(
        r"\b("
        + "|".join(re.escape(name) for name in SCENARIO_RUNTIME_COUNT_RULES)
        + r")\s*\("
    )
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in count_pattern.finditer(body):
            count_name = match.group(1)
            if function_name == count_name:
                continue

            allowed_functions = SCENARIO_RUNTIME_COUNT_RULES[count_name]
            guard_tokens = allowed_functions.get(function_name)
            line = text.count("\n", 0, start + match.start()) + 1
            if guard_tokens is None:
                errors.append(
                    f"{function_name}:{line} calls {count_name} outside a "
                    "source-checked Scenario runtime helper"
                )
                continue

            for token in guard_tokens:
                token_pos = body.find(token)
                if token_pos == -1 or token_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} calls {count_name} before "
                        f"source proof token {token!r}"
                    )

    if not errors:
        return []
    return [SCENARIO_RUNTIME_COUNT_ERROR + ": " + ", ".join(errors)]


SCENARIO_NORMAL_PLAY_FALLBACK_ERROR = (
    "scenario normal-play source failures must not fall back to legacy ROM payloads"
)


def scan_scenario_normal_play_fallback_guards(root: Path) -> list[str]:
    """Keep normal stage loading fail-closed after public Scenario source fails."""
    required_files = {
        "setup": root / "src/game/setup.c",
        "tiles": root / "src/game/tilesreset.c",
        "bg": root / "src/game/bg.c",
        "runtime": root / "port/src/scenario_source_runtime.c",
        "runtime_h": root / "port/include/scenario_source_runtime.h",
    }
    texts: dict[str, str] = {}
    errors: list[str] = []

    for name, path in required_files.items():
        try:
            texts[name] = path.read_text(encoding="utf-8")
        except OSError as exc:
            errors.append(f"cannot read {relpath(path, root)}: {exc}")

    if errors:
        return [SCENARIO_NORMAL_PLAY_FALLBACK_ERROR + ": " + ", ".join(errors)]

    forbidden = {
        "setup": (
            "assetLoadToNew(setup_handle",
            "assetLoadToNew(stage.pads_handle",
            "setup source -> ROM handle",
            "pads source -> ROM handle",
        ),
        "tiles": (
            "assetLoadToNew(stage.tile_handle",
            "tiles source -> ROM handle",
        ),
        "bg": (
            "bg source -> ROM bg cache",
        ),
    }

    for name, patterns in forbidden.items():
        for pattern in patterns:
            if pattern in texts[name]:
                errors.append(f"{name} still contains legacy fallback '{pattern}'")

    required = {
        "setup": (
            "scenarioSourceFatalRuntimeFallbackForStage(&stage",
            "level graph activation failed",
            "setup source compile failed",
            "pads source compile failed",
        ),
        "tiles": (
            "scenarioSourceFatalRuntimeFallbackForStage(&stage",
            "tile source compile failed",
        ),
        "bg": (
            "scenarioSourceFatalRuntimeFallbackForStage(&stage",
            "background source renderer activation failed",
        ),
        "runtime": (
            "scenarioSourceFatalRuntimeFallbackForStage(",
            "ASSET.FALLBACK: Scenario stage",
            "runtime ROM/RomProvider fallback after extraction is an asset-chain failure",
        ),
        "runtime_h": (
            "scenarioSourceFatalRuntimeFallbackForStage(",
        ),
    }

    for name, patterns in required.items():
        for pattern in patterns:
            if pattern not in texts[name]:
                errors.append(f"{name} is missing guard '{pattern}'")

    if not errors:
        return []
    return [SCENARIO_NORMAL_PLAY_FALLBACK_ERROR + ": " + ", ".join(errors)]


def scan_scenario_padfile_global_guards(root: Path) -> list[str]:
    """Reject direct padfile globals outside source-gated count helpers."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario padfile global guards: {exc}"
        ]

    padfile_global = re.compile(r"\bg_PadsFile\s*->")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in padfile_global.finditer(body):
            proof_tokens = SCENARIO_PADFILE_GLOBAL_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} reads g_PadsFile fields outside "
                    "a source-gated runtime count helper"
                )
                continue
            for token in proof_tokens:
                if token not in body:
                    errors.append(
                        f"{function_name}:{line} reads g_PadsFile without "
                        f"count-helper proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_PADFILE_GLOBAL_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing padfile count proof token {token!r}"
                )
        if function_name not in SCENARIO_RUNTIME_COUNT_RULES:
            errors.append(
                f"{function_name}:padfile helper is missing from runtime count rules"
            )

    if not errors:
        return []
    return [SCENARIO_PADFILE_GLOBAL_ERROR + ": " + ", ".join(errors)]


def scan_scenario_stage_setup_global_guards(root: Path) -> list[str]:
    """Reject direct stage setup table globals outside source-gated helpers."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario g_StageSetup guards: {exc}"
        ]

    stage_setup_global = re.compile(r"\bg_StageSetup\s*\.\s*[A-Za-z_][A-Za-z0-9_]*")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in stage_setup_global.finditer(body):
            proof_tokens = SCENARIO_STAGE_SETUP_GLOBAL_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} reads {match.group(0)} outside "
                    "a source-gated runtime table helper"
                )
                continue
            for token in proof_tokens:
                if token not in body:
                    errors.append(
                        f"{function_name}:{line} reads {match.group(0)} "
                        f"without runtime table proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_STAGE_SETUP_GLOBAL_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing g_StageSetup proof token {token!r}"
                )

    for function_name in (
        "s_countRuntimePaths",
        "s_countRuntimePads",
        "s_countRuntimeCovers",
        "s_countRuntimeSetupObjectRows",
        "s_countRuntimeSetupChrRows",
        "s_countRuntimeWaypoints",
        "s_countRuntimeWaygroups",
    ):
        if function_name not in SCENARIO_RUNTIME_COUNT_RULES:
            errors.append(
                f"{function_name}:stage setup helper is missing from runtime count rules"
            )
    if "s_aiGraphRequireRuntimePathPointer" not in SCENARIO_RUNTIME_COUNT_RULES[
        "s_countRuntimePaths"
    ]:
        errors.append(
            "s_aiGraphRequireRuntimePathPointer:missing source proof entry "
            "under s_countRuntimePaths"
        )

    if not errors:
        return []
    return [SCENARIO_STAGE_SETUP_GLOBAL_ERROR + ": " + ", ".join(errors)]


def scan_scenario_setup_tag_global_guards(root: Path) -> list[str]:
    """Reject direct setup tag globals outside source-gated tag helpers."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario setup tag global guards: {exc}"
        ]

    setup_tag_global = re.compile(r"\bg_TagsLinkedList\b")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in setup_tag_global.finditer(body):
            proof_tokens = SCENARIO_SETUP_TAG_GLOBAL_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} reads g_TagsLinkedList outside "
                    "a source-gated runtime setup tag helper"
                )
                continue
            for token in proof_tokens:
                if token not in body:
                    errors.append(
                        f"{function_name}:{line} reads g_TagsLinkedList "
                        f"without setup tag proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_SETUP_TAG_GLOBAL_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing setup tag proof token {token!r}"
                )
        if function_name not in SCENARIO_RUNTIME_COUNT_RULES:
            errors.append(
                f"{function_name}:setup tag helper is missing from runtime count rules"
            )

    if not errors:
        return []
    return [SCENARIO_SETUP_TAG_GLOBAL_ERROR + ": " + ", ".join(errors)]


def scan_scenario_setup_behavior_link_source_guards(root: Path) -> list[str]:
    """Keep setup behavior-link source refs validated before live registration."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario setup behavior-link guards: "
            f"{exc}"
        ]

    blocks = {
        function_name: text[start:end]
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []
    required_tokens = (
        "s_setupFindRecordByOrder",
        "s_setupValidateBehaviorLinkTarget",
        "s_setupValidateBehaviorLinkSourceTargets",
        "is not a public setup.fields.json row",
        "expected source type",
        "linked_guns.weapon_1",
        "linked_guns.weapon_2",
        "lift_door_link.door",
        "lift_door_link.lift",
        "safe_item.safe",
        "safe_item.door",
        "padlocked_door.door",
        "conditional_scenery.trigger",
        "conditional_scenery.unexploded",
        "conditional_scenery.exploded",
        "blocked_path.blocker",
        "OBJTYPE_WEAPON",
        "OBJTYPE_DOOR",
        "OBJTYPE_LIFT",
        "OBJTYPE_SAFE",
    )
    for token in required_tokens:
        if token not in text:
            errors.append(f"missing setup behavior-link source token {token!r}")

    collect = blocks.get("s_setupCollectBehaviorLinkSource")
    if collect is None:
        errors.append("s_setupCollectBehaviorLinkSource:missing function body")
    else:
        fill_pos = collect.find("s_setupFillBehaviorLink(")
        validate_pos = collect.find("s_setupValidateBehaviorLinkSourceTargets(")
        count_pos = collect.find("s_ActiveScenarioGraphs.setup_link_count = count")
        if fill_pos < 0:
            errors.append("s_setupCollectBehaviorLinkSource:missing link fill")
        if validate_pos < 0:
            errors.append(
                "s_setupCollectBehaviorLinkSource:missing source target validation"
            )
        if count_pos < 0:
            errors.append("s_setupCollectBehaviorLinkSource:missing count commit")
        if fill_pos >= 0 and validate_pos >= 0 and fill_pos > validate_pos:
            errors.append(
                "s_setupCollectBehaviorLinkSource:validates source targets "
                "before filling link targets"
            )
        if validate_pos >= 0 and count_pos >= 0 and validate_pos > count_pos:
            errors.append(
                "s_setupCollectBehaviorLinkSource:commits behavior-link table "
                "before source target validation"
            )

    if not errors:
        return []
    return [
        SCENARIO_SETUP_BEHAVIOR_LINK_SOURCE_ERROR
        + ": "
        + ", ".join(errors)
    ]


def scan_scenario_source_wide_pad_cache_guards(root: Path) -> list[str]:
    """Reject direct source wide-pad cache globals outside padfile helpers."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario source wide-pad guards: {exc}"
        ]

    wide_pad_global = re.compile(
        r"\bg_SourceWidePad(?:File|Offsets|OffsetCount)\b"
    )
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in wide_pad_global.finditer(body):
            line_start = text.rfind("\n", 0, start + match.start()) + 1
            line_end = text.find("\n", start + match.start())
            if line_end == -1:
                line_end = len(text)
            line_text = text[line_start:line_end].strip()
            if line_text.startswith("static "):
                continue
            proof_tokens = (
                SCENARIO_SOURCE_WIDE_PAD_CACHE_SOURCE_PROVEN_FUNCTIONS.get(
                    function_name
                )
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} reads {match.group(0)} outside "
                    "a source padfile helper"
                )
                continue
            for token in proof_tokens:
                if token not in body:
                    errors.append(
                        f"{function_name}:{line} reads {match.group(0)} "
                        f"without source padfile proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_SOURCE_WIDE_PAD_CACHE_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing source wide-pad proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_SOURCE_WIDE_PAD_CACHE_ERROR + ": " + ", ".join(errors)]


def scan_scenario_runtime_lookup_guards(root: Path) -> list[str]:
    """Reject Scenario runtime lookups outside source-checked helpers."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario runtime lookup guards: {exc}"
        ]

    lookup_pattern = re.compile(
        r"\b("
        + "|".join(re.escape(name) for name in SCENARIO_RUNTIME_LOOKUP_RULES)
        + r")\s*\("
    )
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in lookup_pattern.finditer(body):
            lookup_name = match.group(1)
            allowed_functions = SCENARIO_RUNTIME_LOOKUP_RULES[lookup_name]
            proof_tokens = allowed_functions.get(function_name)
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} calls {lookup_name} outside a "
                    "source-checked Scenario runtime helper"
                )
                continue

            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1 or token_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} calls {lookup_name} before "
                        f"source proof token {token!r}"
                    )

    if not errors:
        return []
    return [SCENARIO_RUNTIME_LOOKUP_ERROR + ": " + ", ".join(errors)]


def scan_scenario_player_state_access_guards(root: Path) -> list[str]:
    """Reject direct Scenario player-state dereferences outside proof helpers."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario player-state guards: {exc}"
        ]

    current_player_deref = re.compile(r"\bg_Vars\.currentplayer\s*->")
    current_player_pointer = re.compile(r"\bg_Vars\.currentplayer\b(?!\s*->)")
    player_table_access = re.compile(r"\bg_Vars\.players\s*\[")
    player_slot_helper = "s_aiGraphRequireRuntimePlayerSlot"
    slot_bounds_token = "playernum < 0 || playernum >= MAX_PLAYERS"
    pointer_proof_token = "s_aiGraphRequireRuntimePlayerPointer(action, player"
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in current_player_deref.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            errors.append(
                f"{function_name}:{line} dereferences g_Vars.currentplayer "
                "without a runtime player proof helper"
            )
        pointer_matches = list(current_player_pointer.finditer(body))
        if pointer_matches:
            proof_tokens = (
                SCENARIO_CURRENT_PLAYER_POINTER_SOURCE_PROVEN_FUNCTIONS.get(
                    function_name
                )
            )
            if proof_tokens is None:
                for match in pointer_matches:
                    line = text.count("\n", 0, start + match.start()) + 1
                    errors.append(
                        f"{function_name}:{line} snapshots "
                        "g_Vars.currentplayer outside a source-proven "
                        "current-player handler"
                    )
            elif len(pointer_matches) != 1:
                line = text.count("\n", 0, start + pointer_matches[1].start()) + 1
                errors.append(
                    f"{function_name}:{line} snapshots g_Vars.currentplayer "
                    "more than once; carry the proven local player pointer"
                )
            else:
                for token in proof_tokens:
                    if token not in body:
                        line = text.count(
                            "\n", 0, start + pointer_matches[0].start()
                        ) + 1
                        errors.append(
                            f"{function_name}:{line} snapshots "
                            "g_Vars.currentplayer without source proof token "
                            f"{token!r}"
                        )
        for match in player_table_access.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            if function_name != player_slot_helper:
                errors.append(
                    f"{function_name}:{line} indexes g_Vars.players outside "
                    f"{player_slot_helper}"
                )
                continue

            bounds_pos = body.find(slot_bounds_token)
            if bounds_pos == -1 or bounds_pos > match.start():
                errors.append(
                    f"{function_name}:{line} indexes g_Vars.players before "
                    "player-slot bounds proof"
                )
            pointer_pos = body.find(pointer_proof_token, match.start())
            if pointer_pos == -1:
                errors.append(
                    f"{function_name}:{line} indexes g_Vars.players without "
                    "runtime player pointer proof"
                )

    if not errors:
        return []
    return [SCENARIO_PLAYER_STATE_ACCESS_ERROR + ": " + ", ".join(errors)]


def scan_scenario_current_player_number_guards(root: Path) -> list[str]:
    """Reject raw current-player-number reads except proven switch helpers."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario current-player-number guards: {exc}"
        ]

    current_player_number = re.compile(r"\bg_Vars\.currentplayernum\b")
    prev_snapshot = re.compile(
        r"\b(?:s32|u32)?\s*prevplayernum\s*=\s*g_Vars\.currentplayernum\s*;"
    )
    hud_fallback = re.compile(r"\bplayernum\s*=\s*g_Vars\.currentplayernum\s*;")
    source_tokens = (
        "s_aiGraphRequire",
        "s_aiGraphResolveRuntimeCharacterRefOrSelector",
        "s_aiAudioGraphReady",
        "s_aiPlayerWeaponStateGraphReady",
        "s_aiPlayerCutsceneGraphReady",
        "s_ActiveScenarioGraphs.level_graph_active",
    )
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in current_player_number.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            line_start = body.rfind("\n", 0, match.start()) + 1
            line_end = body.find("\n", match.start())
            if line_end == -1:
                line_end = len(body)
            statement = body[line_start:line_end]

            if prev_snapshot.search(statement):
                if not function_name.startswith("scenarioSourceAiGraphExecute"):
                    errors.append(
                        f"{function_name}:{line} snapshots g_Vars.currentplayernum "
                        "outside a Scenario graph executor"
                    )
                    continue

                source_pos = min(
                    (
                        pos
                        for pos in (body.find(token) for token in source_tokens)
                        if pos != -1
                    ),
                    default=-1,
                )
                if source_pos == -1 or source_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} snapshots g_Vars.currentplayernum "
                        "before graph/source proof"
                    )

                proof_pos = min(
                    (
                        pos
                        for pos in (
                            body.find("s_aiGraphRequireRuntimePlayerSlot"),
                            body.find("s_aiGraphHudPlayerForChr("),
                        )
                        if pos != -1
                    ),
                    default=-1,
                )
                switch_pos = body.find("setCurrentPlayerNum(playernum)", match.start())
                if proof_pos == -1 or switch_pos == -1 or proof_pos > switch_pos:
                    errors.append(
                        f"{function_name}:{line} switches current player without "
                        "runtime player-slot proof first"
                    )

                restore_pos = body.find("setCurrentPlayerNum(prevplayernum)", switch_pos)
                if switch_pos == -1 or restore_pos == -1:
                    errors.append(
                        f"{function_name}:{line} snapshots g_Vars.currentplayernum "
                        "without restoring it after the player switch"
                    )
                continue

            if (
                function_name == "s_aiGraphHudPlayerForChr"
                and hud_fallback.search(statement)
            ):
                char_pos = body.find("s_aiGraphRequireRuntimeCharacterRefFromBase")
                slot_pos = body.find(
                    "s_aiGraphRequireRuntimePlayerSlot(action, playernum",
                    match.start(),
                )
                if char_pos == -1 or char_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} reads g_Vars.currentplayernum "
                        "before source character proof"
                    )
                if slot_pos == -1:
                    errors.append(
                        f"{function_name}:{line} reads g_Vars.currentplayernum "
                        "without runtime player-slot proof"
                    )
                continue

            errors.append(
                f"{function_name}:{line} reads g_Vars.currentplayernum outside "
                "a proven player switch or HUD fallback"
            )

    if not errors:
        return []
    return [SCENARIO_CURRENT_PLAYER_NUMBER_ERROR + ": " + ", ".join(errors)]


def scan_scenario_active_character_pointer_guards(root: Path) -> list[str]:
    """Reject raw active-character pointer reads outside source-proof helpers."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario active-character pointer guards: {exc}"
        ]

    active_chr_pointer = re.compile(r"\bg_Vars\.chrdata\b(?!\s*->)")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        pointer_matches = list(active_chr_pointer.finditer(body))
        if not pointer_matches:
            continue

        proof_tokens = SCENARIO_ACTIVE_CHARACTER_POINTER_SOURCE_PROVEN_FUNCTIONS.get(
            function_name
        )
        if proof_tokens is None:
            for match in pointer_matches:
                line = text.count("\n", 0, start + match.start()) + 1
                errors.append(
                    f"{function_name}:{line} reads g_Vars.chrdata outside "
                    "an active-character source-proof helper"
                )
            continue

        found_globals = tuple(match.group(0) for match in pointer_matches)
        expected_globals = tuple(
            "g_Vars.chrdata"
            for token in proof_tokens
            if "g_Vars.chrdata" in token
        )
        if found_globals != expected_globals:
            line = text.count("\n", 0, start + pointer_matches[0].start()) + 1
            errors.append(
                f"{function_name}:{line} unexpected active-character global "
                f"shape {found_globals!r}"
            )
        for token in proof_tokens:
            if token not in body:
                line = text.count("\n", 0, start + pointer_matches[0].start()) + 1
                errors.append(
                    f"{function_name}:{line} reads g_Vars.chrdata without "
                    f"source proof token {token!r}"
                )

    for function_name, proof_tokens in sorted(
        SCENARIO_ACTIVE_CHARACTER_POINTER_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing active-character function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        if not active_chr_pointer.search(body):
            errors.append(f"{function_name}:missing expected g_Vars.chrdata read")
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing active-character proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_ACTIVE_CHARACTER_POINTER_ERROR + ": " + ", ".join(errors)]


def scan_scenario_vehicle_motion_global_guards(root: Path) -> list[str]:
    """Reject raw vehicle global dereferences in source-proven motion actions."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario vehicle-motion guards: {exc}"
        ]

    global_deref = re.compile(r"\bg_Vars\.(?:truck|heli|hovercar)\s*->")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name in sorted(SCENARIO_VEHICLE_MOTION_SOURCE_PROVEN_FUNCTIONS):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for match in global_deref.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            errors.append(
                f"{function_name}:{line} dereferences {match.group(0)} "
                "instead of a source-proven local vehicle pointer"
            )

    if not errors:
        return []
    return [SCENARIO_VEHICLE_MOTION_GLOBAL_ERROR + ": " + ", ".join(errors)]


def scan_scenario_misc_branch_vehicle_guards(root: Path) -> list[str]:
    """Reject unproven hovercar target reads in source-proven misc branches."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario misc-branch vehicle guards: {exc}"
        ]

    direct_hovercar_call = re.compile(
        r"\bchopperFromHovercar\s*\(\s*g_Vars\.hovercar\s*\)"
    )
    direct_hovercar_field = re.compile(r"\bg_Vars\.hovercar\s*->")
    hovercar_pointer = re.compile(r"\bg_Vars\.hovercar\b(?!\s*->)")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in direct_hovercar_call.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            errors.append(
                f"{function_name}:{line} calls {match.group(0)} "
                "without a source-proven local hovercar pointer"
            )

    for function_name, proof_tokens in sorted(
        SCENARIO_MISC_BRANCH_VEHICLE_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for match in direct_hovercar_field.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            errors.append(
                f"{function_name}:{line} dereferences {match.group(0)} "
                "instead of a source-proven local hovercar pointer"
            )
        pointer_matches = list(hovercar_pointer.finditer(body))
        if len(pointer_matches) != 1:
            line = (
                text.count("\n", 0, start + pointer_matches[0].start()) + 1
                if pointer_matches
                else text.count("\n", 0, start) + 1
            )
            errors.append(
                f"{function_name}:{line} must snapshot g_Vars.hovercar once "
                "and carry the proven local pointer"
            )
        for token in proof_tokens:
            if token not in body:
                line = (
                    text.count("\n", 0, start + pointer_matches[0].start()) + 1
                    if pointer_matches
                    else text.count("\n", 0, start) + 1
                )
                errors.append(
                    f"{function_name}:{line} reads g_Vars.hovercar without "
                    f"source proof token {token!r}"
                )
        proof_pos = body.find("s_aiGraphRequireRuntimeVehicleObjectPointer(")
        use_pos = body.find("chopperFromHovercar(hovercar)")
        if proof_pos == -1 or use_pos == -1 or proof_pos > use_pos:
            line = (
                text.count("\n", 0, start + use_pos) + 1
                if use_pos >= 0
                else text.count("\n", 0, start) + 1
            )
            errors.append(
                f"{function_name}:{line} uses hovercar target state before "
                "runtime vehicle source proof"
            )

    if not errors:
        return []
    return [SCENARIO_MISC_BRANCH_VEHICLE_ERROR + ": " + ", ".join(errors)]


def scan_scenario_prop_target_vehicle_guards(root: Path) -> list[str]:
    """Reject unproven hovercar target mutations in source-proven target actions."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario prop-target vehicle guards: {exc}"
        ]

    direct_hovercar_set = re.compile(
        r"\bchopperSetTarget\s*\(\s*g_Vars\.hovercar\s*,"
    )
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in direct_hovercar_set.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            errors.append(
                f"{function_name}:{line} calls {match.group(0)} "
                "without a source-proven local hovercar pointer"
            )

    for function_name, proof_tokens in sorted(
        SCENARIO_PROP_TARGET_VEHICLE_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                line = text.count("\n", 0, start) + 1
                errors.append(
                    f"{function_name}:{line} mutates hovercar target without "
                    f"source proof token {token!r}"
                )
        proof_pos = body.find("s_aiGraphRequireRuntimeVehicleObjectPointer(")
        use_pos = body.find("chopperSetTarget(hovercar, chrnum)")
        if proof_pos == -1 or use_pos == -1 or proof_pos > use_pos:
            line = (
                text.count("\n", 0, start + use_pos) + 1
                if use_pos >= 0
                else text.count("\n", 0, start) + 1
            )
            errors.append(
                f"{function_name}:{line} mutates hovercar target before "
                "runtime vehicle source proof"
            )

    if not errors:
        return []
    return [SCENARIO_PROP_TARGET_VEHICLE_ERROR + ": " + ", ".join(errors)]


def scan_scenario_timer_vehicle_guards(root: Path) -> list[str]:
    """Reject unproven hovercar/chopper timer access in source-proven timer actions."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario timer vehicle guards: {exc}"
        ]

    timer_call = re.compile(
        r"\bchopper(?:RestartTimer|GetTimer)\s*\(\s*hovercar\s*\)"
    )
    direct_global_timer = re.compile(
        r"\bchopper(?:RestartTimer|GetTimer)\s*\(\s*g_Vars\.hovercar\s*\)"
    )
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []
    source_proven = set(SCENARIO_TIMER_VEHICLE_SOURCE_PROVEN_FUNCTIONS)

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in direct_global_timer.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            errors.append(
                f"{function_name}:{line} calls {match.group(0)} "
                "without a source-proven local hovercar pointer"
            )
        if function_name not in source_proven:
            for match in timer_call.finditer(body):
                line = text.count("\n", 0, start + match.start()) + 1
                errors.append(
                    f"{function_name}:{line} calls {match.group(0)} outside "
                    "a source-proven timer vehicle action"
                )

    for function_name, proof_tokens in sorted(
        SCENARIO_TIMER_VEHICLE_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                line = text.count("\n", 0, start) + 1
                errors.append(
                    f"{function_name}:{line} uses hovercar timer state without "
                    f"source proof token {token!r}"
                )
        proof_pos = body.find("s_aiGraphRequireRuntimeVehicleObjectPointer(")
        use_tokens = [
            "chopperRestartTimer(hovercar)",
            "chopperGetTimer(hovercar)",
        ]
        use_positions = [body.find(token) for token in use_tokens if token in body]
        if proof_pos == -1 or not use_positions or proof_pos > min(use_positions):
            line = (
                text.count("\n", 0, start + min(use_positions)) + 1
                if use_positions
                else text.count("\n", 0, start) + 1
            )
            errors.append(
                f"{function_name}:{line} uses hovercar timer state before "
                "runtime vehicle source proof"
            )

    if not errors:
        return []
    return [SCENARIO_TIMER_VEHICLE_ERROR + ": " + ", ".join(errors)]


def scan_scenario_hovercar_branch_guards(root: Path) -> list[str]:
    """Reject unproven hovercar helper calls in graph branch actions."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario hovercar branch guards: {exc}"
        ]

    hovercar_call = re.compile(
        r"\bchopper(?:Stop|Attack|CheckTargetInFov|CheckTargetInSight)"
        r"\s*\(\s*hovercar\b"
    )
    direct_global_call = re.compile(
        r"\bchopper(?:Stop|Attack|CheckTargetInFov|CheckTargetInSight)"
        r"\s*\(\s*g_Vars\.hovercar\b"
    )
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []
    source_proven = set(SCENARIO_HOVERCAR_BRANCH_SOURCE_PROVEN_FUNCTIONS)

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in direct_global_call.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            errors.append(
                f"{function_name}:{line} calls {match.group(0)} "
                "without a source-proven local hovercar pointer"
            )
        if function_name not in source_proven:
            for match in hovercar_call.finditer(body):
                line = text.count("\n", 0, start + match.start()) + 1
                errors.append(
                    f"{function_name}:{line} calls {match.group(0)} outside "
                    "a source-proven hovercar branch"
                )

    for function_name, proof_tokens in sorted(
        SCENARIO_HOVERCAR_BRANCH_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                line = text.count("\n", 0, start) + 1
                errors.append(
                    f"{function_name}:{line} uses hovercar branch state without "
                    f"source proof token {token!r}"
                )
        proof_pos = body.find("s_aiGraphRequireRuntimeVehicleObjectPointer(")
        use_positions = [match.start() for match in hovercar_call.finditer(body)]
        if proof_pos == -1 or not use_positions or proof_pos > min(use_positions):
            line = (
                text.count("\n", 0, start + min(use_positions)) + 1
                if use_positions
                else text.count("\n", 0, start) + 1
            )
            errors.append(
                f"{function_name}:{line} uses hovercar branch state before "
                "runtime vehicle source proof"
            )

    if not errors:
        return []
    return [SCENARIO_HOVERCAR_BRANCH_ERROR + ": " + ", ".join(errors)]


def scan_scenario_prop_index_guards(root: Path) -> list[str]:
    """Reject live prop-index derivation unless the owning character is proven."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario prop-index guards: {exc}"
        ]

    prop_index = re.compile(r"\bchr->prop\s*-\s*g_Vars\.props\b")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in prop_index.finditer(body):
            proof_tokens = SCENARIO_PROP_INDEX_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} derives a runtime prop index "
                    "without a source-proven character pointer"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1 or token_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} derives a runtime prop index "
                        f"before source proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_PROP_INDEX_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing prop-index proof token {token!r}"
                )
        for token in SCENARIO_PROP_INDEX_REQUIRED_TOKENS.get(function_name, ()):
            if token not in body:
                errors.append(
                    f"{function_name}:missing prop-index evidence token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_PROP_INDEX_ERROR + ": " + ", ".join(errors)]


def scan_scenario_prop_preset_index_guards(root: Path) -> list[str]:
    """Reject live prop-preset table access unless source/object proofs precede it."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario prop-preset index guards: {exc}"
        ]

    prop_preset_index = re.compile(r"\bg_Vars\.props\s*\[\s*chr->proppreset1\s*\]")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in prop_preset_index.finditer(body):
            proof_tokens = SCENARIO_PROP_PRESET_INDEX_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} indexes a runtime prop preset "
                    "outside a source-proven prop-target handler"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1 or token_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} indexes a runtime prop preset "
                        f"before source proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_PROP_PRESET_INDEX_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing prop-preset index proof token {token!r}"
                )
        for token in SCENARIO_PROP_PRESET_INDEX_REQUIRED_TOKENS.get(
            function_name, ()
        ):
            if token not in body:
                errors.append(
                    f"{function_name}:missing prop-preset index evidence token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_PROP_PRESET_INDEX_ERROR + ": " + ", ".join(errors)]


def scan_scenario_room_prop_scan_guards(root: Path) -> list[str]:
    """Reject direct room prop-table scans unless the source-proven caller owns them."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario room-prop scan guards: {exc}"
        ]

    room_prop_index = re.compile(r"\bg_Vars\.props\s*\[\s*\*ptr\s*\]")
    helper_call = re.compile(r"\bs_aiGraphChrSeesSuspiciousItem\s*\(")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in room_prop_index.finditer(body):
            proof_tokens = SCENARIO_ROOM_PROP_SCAN_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} scans runtime room props "
                    "outside a source-proven helper"
                )
                continue
            for token in proof_tokens:
                if token not in body:
                    errors.append(
                        f"{function_name}:{line} missing room-prop scan token {token!r}"
                    )

        if function_name == "s_aiGraphChrSeesSuspiciousItem":
            continue
        for match in helper_call.finditer(body):
            proof_tokens = SCENARIO_ROOM_PROP_SCAN_CALLERS.get(function_name)
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} calls suspicious-item room-prop scan "
                    "outside a source-proven spatial handler"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1 or token_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} calls suspicious-item room-prop scan "
                        f"before source proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_ROOM_PROP_SCAN_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing room-prop scan evidence token {token!r}"
                )

    for function_name, proof_tokens in sorted(
        SCENARIO_ROOM_PROP_SCAN_CALLERS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing caller function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing room-prop caller proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_ROOM_PROP_SCAN_ERROR + ": " + ", ".join(errors)]


def scan_scenario_scene_room_table_guards(root: Path) -> list[str]:
    """Reject direct Scenario room-table mutations without scene source proof."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario scene-room table guards: {exc}"
        ]

    room_access = re.compile(
        r"\bg_Rooms\s*\[\s*([A-Za-z_][A-Za-z0-9_]*)\s*\]"
    )
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in room_access.finditer(body):
            index_name = match.group(1)
            function_rules = SCENARIO_SCENE_ROOM_TABLE_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            proof_tokens = (
                function_rules.get(index_name)
                if function_rules is not None
                else None
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} touches g_Rooms[{index_name}] "
                    "outside a source-proven scene-room handler"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1 or token_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} touches g_Rooms[{index_name}] "
                        f"before source proof token {token!r}"
                    )

    for function_name, index_rules in sorted(
        SCENARIO_SCENE_ROOM_TABLE_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for index_name, proof_tokens in sorted(index_rules.items()):
            if f"g_Rooms[{index_name}]" not in body:
                errors.append(
                    f"{function_name}:missing expected g_Rooms[{index_name}] access"
                )
            for token in proof_tokens:
                if token not in body:
                    errors.append(
                        f"{function_name}:missing scene-room proof token {token!r}"
                    )

    if not errors:
        return []
    return [SCENARIO_SCENE_ROOM_TABLE_ERROR + ": " + ", ".join(errors)]


def scan_scenario_scene_room_global_guards(root: Path) -> list[str]:
    """Reject direct Scenario room table pointer/count reads outside helpers."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario scene-room global guards: {exc}"
        ]

    room_global_access = re.compile(
        r"\bg_Rooms\b(?!\s*\[)|\bg_Vars\s*\.\s*roomcount\b"
    )
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in room_global_access.finditer(body):
            proof_shape = SCENARIO_SCENE_ROOM_GLOBAL_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_shape is None:
                errors.append(
                    f"{function_name}:{line} reads {match.group(0)} outside "
                    "a source-proven scene-room table helper"
                )
                continue
            for token in proof_shape["pre"]:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} reads {match.group(0)} "
                        f"without scene-room proof token {token!r}"
                    )
                elif token_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} reads {match.group(0)} "
                        f"before scene-room proof token {token!r}"
                    )
            for token in proof_shape["required"]:
                if token not in body:
                    errors.append(
                        f"{function_name}:{line} reads {match.group(0)} "
                        f"without required scene-room token {token!r}"
                    )

    for function_name, proof_shape in sorted(
        SCENARIO_SCENE_ROOM_GLOBAL_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        if not room_global_access.search(body):
            errors.append(f"{function_name}:missing expected scene-room global access")
        for token in proof_shape["pre"] + proof_shape["required"]:
            if token not in body:
                errors.append(
                    f"{function_name}:missing scene-room global proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_SCENE_ROOM_GLOBAL_ERROR + ": " + ", ".join(errors)]


def scan_scenario_portal_table_guards(root: Path) -> list[str]:
    """Reject direct Scenario portal-table mutations without portals.json proof."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario portal-table guards: {exc}"
        ]

    portal_access = re.compile(r"\bg_BgPortals\s*\[")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in portal_access.finditer(body):
            proof_tokens = SCENARIO_PORTAL_TABLE_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} touches g_BgPortals outside a "
                    "source-proven portal handler"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1 or token_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} touches g_BgPortals before "
                        f"source proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_PORTAL_TABLE_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        if "g_BgPortals[portalnum]" not in body:
            errors.append(f"{function_name}:missing expected portal table access")
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing portal-table proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_PORTAL_TABLE_ERROR + ": " + ", ".join(errors)]


def scan_scenario_portal_global_guards(root: Path) -> list[str]:
    """Reject direct Scenario portal globals outside source-proof helpers."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario portal global guards: {exc}"
        ]

    portal_global = re.compile(r"\bg_Bg(?:Portals|NumPortalCameraCacheItems)\b")
    portal_table_mutation = re.compile(r"\bg_BgPortals\s*\[")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in portal_global.finditer(body):
            if portal_table_mutation.match(body, match.start()):
                continue
            proof_tokens = SCENARIO_PORTAL_GLOBAL_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} reads {match.group(0)} outside "
                    "a source-proven portal helper"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} reads {match.group(0)} without "
                        f"source proof token {token!r}"
                    )
                elif token == "missing portals.json source" and token_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} reads {match.group(0)} before "
                        f"source proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_PORTAL_GLOBAL_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing portal-global proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_PORTAL_GLOBAL_ERROR + ": " + ", ".join(errors)]


def scan_scenario_mission_music_mode_global_guards(root: Path) -> list[str]:
    """Reject direct mission/music/runtime mode globals outside graph-source handlers."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario mission/music mode global guards: {exc}"
        ]

    mission_music_mode_global = re.compile(
        r"\b(?:g_MusicEventQueueLength|g_MissionConfig\s*\.\s*iscoop|"
        r"g_Vars\s*\.\s*(?:normmplayerisrunning|numaibuddies))\b"
    )
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in mission_music_mode_global.finditer(body):
            proof_tokens = (
                SCENARIO_MISSION_MUSIC_MODE_GLOBAL_SOURCE_PROVEN_FUNCTIONS.get(
                    function_name
                )
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} reads {match.group(0)} outside "
                    "a graph-source mission/music mode handler"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} reads {match.group(0)} "
                        f"without graph source proof token {token!r}"
                    )
                elif (
                    not token.startswith(("AI action ", "AI condition "))
                    and token_pos > match.start()
                ):
                    errors.append(
                        f"{function_name}:{line} reads {match.group(0)} before "
                        f"graph source proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_MISSION_MUSIC_MODE_GLOBAL_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing mission/music mode proof token {token!r}"
                )

    if not errors:
        return []
    return [
        SCENARIO_MISSION_MUSIC_MODE_GLOBAL_ERROR + ": " + ", ".join(errors)
    ]


def scan_scenario_player_invincibility_guards(root: Path) -> list[str]:
    """Reject direct player invincibility access outside player-slot proof."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario player invincibility guards: {exc}"
        ]

    invincibility_global = re.compile(r"\bg_PlayerInvincible\b")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in invincibility_global.finditer(body):
            proof_tokens = (
                SCENARIO_PLAYER_INVINCIBILITY_SOURCE_PROVEN_FUNCTIONS.get(
                    function_name
                )
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} accesses g_PlayerInvincible "
                    "outside a source-proven player invincibility handler"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} accesses g_PlayerInvincible "
                        f"without player proof token {token!r}"
                    )
                elif (
                    not token.startswith(("AI action ", "AI condition "))
                    and token_pos > match.start()
                ):
                    errors.append(
                        f"{function_name}:{line} accesses g_PlayerInvincible "
                        f"before player proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_PLAYER_INVINCIBILITY_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        if "g_PlayerInvincible" not in body:
            errors.append(
                f"{function_name}:missing expected g_PlayerInvincible access"
            )
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing player invincibility proof token {token!r}"
                )

    if not errors:
        return []
    return [
        SCENARIO_PLAYER_INVINCIBILITY_ERROR + ": " + ", ".join(errors)
    ]


def scan_scenario_environment_global_guards(root: Path) -> list[str]:
    """Reject direct environment globals outside graph-source handlers."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario environment global guards: {exc}"
        ]

    environment_global = re.compile(r"\b(?:g_SkyWindSpeed|g_TintedGlassEnabled)\b")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in environment_global.finditer(body):
            proof_tokens = (
                SCENARIO_ENVIRONMENT_GLOBAL_SOURCE_PROVEN_FUNCTIONS.get(
                    function_name
                )
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} accesses {match.group(0)} outside "
                    "a graph-source environment handler"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} accesses {match.group(0)} "
                        f"without graph source proof token {token!r}"
                    )
                elif (
                    not token.startswith(("AI action ", "g_"))
                    and token_pos > match.start()
                ):
                    errors.append(
                        f"{function_name}:{line} accesses {match.group(0)} "
                        f"before graph source proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_ENVIRONMENT_GLOBAL_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing environment proof token {token!r}"
                )

    for helper_name, proof_tokens in sorted(
        SCENARIO_ENVIRONMENT_GLOBAL_HELPER_PROOF_TOKENS.items()
    ):
        if helper_name not in blocks:
            errors.append(f"{helper_name}:missing helper body")
            continue
        start, end = blocks[helper_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{helper_name}:missing environment helper proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_ENVIRONMENT_GLOBAL_ERROR + ": " + ", ".join(errors)]


def scan_scenario_cutscene_frame_overrun_guards(root: Path) -> list[str]:
    """Reject cutscene timing globals outside animation-source proof."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario cutscene frame-overrun guards: {exc}"
        ]

    cutscene_overrun = re.compile(r"\bg_CutsceneFrameOverrun240\b")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in cutscene_overrun.finditer(body):
            proof_tokens = (
                SCENARIO_CUTSCENE_FRAME_OVERRUN_SOURCE_PROVEN_FUNCTIONS.get(
                    function_name
                )
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} reads g_CutsceneFrameOverrun240 "
                    "outside a source-proven animation handler"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} reads g_CutsceneFrameOverrun240 "
                        f"without animation proof token {token!r}"
                    )
                elif token.startswith("AI action ") and token_pos < match.start():
                    errors.append(
                        f"{function_name}:{line} reads g_CutsceneFrameOverrun240 "
                        f"after proof log token {token!r}"
                    )
                elif (
                    not token.startswith(("AI action ", "g_CutsceneFrameOverrun240"))
                    and token_pos > match.start()
                ):
                    errors.append(
                        f"{function_name}:{line} reads g_CutsceneFrameOverrun240 "
                        f"before animation proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_CUTSCENE_FRAME_OVERRUN_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        if "g_CutsceneFrameOverrun240" not in body:
            errors.append(
                f"{function_name}:missing expected cutscene frame-overrun access"
            )
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing cutscene frame-overrun proof token {token!r}"
                )

    if not errors:
        return []
    return [
        SCENARIO_CUTSCENE_FRAME_OVERRUN_ERROR + ": " + ", ".join(errors)
    ]


def scan_scenario_teleport_sound_priority_guards(root: Path) -> list[str]:
    """Reject teleport sound priority changes outside catalog-proven audio graph paths."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario teleport sound priority guards: {exc}"
        ]

    audio_manager_access = re.compile(r"\bg_AudioManager\b")
    teleport_sound_call = re.compile(r"\bs_aiGraphPlayTeleportSound\s*\(")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in audio_manager_access.finditer(body):
            proof_tokens = (
                SCENARIO_TELEPORT_SOUND_PRIORITY_SOURCE_PROVEN_FUNCTIONS.get(
                    function_name
                )
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} touches g_AudioManager outside "
                    "the teleport sound priority helper"
                )
                continue
            for token in proof_tokens:
                if token not in body:
                    errors.append(
                        f"{function_name}:{line} touches g_AudioManager "
                        f"without teleport sound priority proof token {token!r}"
                    )

        if function_name == "s_aiGraphPlayTeleportSound":
            continue

        for match in teleport_sound_call.finditer(body):
            proof_tokens = (
                SCENARIO_TELEPORT_SOUND_PRIORITY_CALLER_PROOF_TOKENS.get(
                    function_name
                )
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} calls s_aiGraphPlayTeleportSound "
                    "outside a catalog-proven teleport graph handler"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} calls s_aiGraphPlayTeleportSound "
                        f"without audio proof token {token!r}"
                    )
                elif (
                    token.startswith(("missing ", "s_aiGraphResolveAudioCatalogId"))
                    and token_pos > match.start()
                ):
                    errors.append(
                        f"{function_name}:{line} calls s_aiGraphPlayTeleportSound "
                        f"before audio proof token {token!r}"
                    )
                elif token.startswith("sound_id=") and token_pos < match.start():
                    errors.append(
                        f"{function_name}:{line} calls s_aiGraphPlayTeleportSound "
                        f"after proof log token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_TELEPORT_SOUND_PRIORITY_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        if "g_AudioManager" not in body:
            errors.append(
                f"{function_name}:missing expected teleport audio priority access"
            )
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing teleport sound priority token {token!r}"
                )

    for function_name, proof_tokens in sorted(
        SCENARIO_TELEPORT_SOUND_PRIORITY_CALLER_PROOF_TOKENS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing teleport sound caller proof token {token!r}"
                )

    if not errors:
        return []
    return [
        SCENARIO_TELEPORT_SOUND_PRIORITY_ERROR + ": " + ", ".join(errors)
    ]


def scan_scenario_lift_number_guards(root: Path) -> list[str]:
    """Reject lift-number table checks outside public pad-source proof."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario lift-number guards: {exc}"
        ]

    lift_table_access = re.compile(r"\bg_Lifts\b")
    lift_number_call = re.compile(r"\bs_aiGraphRequireRuntimeLiftNumber\s*\(")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in lift_table_access.finditer(body):
            proof_tokens = SCENARIO_LIFT_NUMBER_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} touches g_Lifts outside the "
                    "source-proven lift-number helper"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} touches g_Lifts without "
                        f"lift-number proof token {token!r}"
                    )
                elif token.startswith("s_aiGraphRequireRuntimePadTable") and token_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} touches g_Lifts before "
                        f"public pad-source proof token {token!r}"
                    )

        if function_name == "s_aiGraphRequireRuntimeLiftNumber":
            continue

        for match in lift_number_call.finditer(body):
            proof_tokens = SCENARIO_LIFT_NUMBER_CALLER_PROOF_TOKENS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} calls s_aiGraphRequireRuntimeLiftNumber "
                    "outside a source-proven lift action"
                )
                continue
            lift_activate_pos = body.find("liftActivate(obj->prop, (u8)liftnum)")
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} calls s_aiGraphRequireRuntimeLiftNumber "
                        f"without lift proof token {token!r}"
                    )
                elif token.startswith("s_aiGraphRequireLiftNode") and token_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} calls s_aiGraphRequireRuntimeLiftNumber "
                        f"before graph proof token {token!r}"
                    )
                elif (
                    token.startswith("s_aiGraphRequireRuntimeObjectTagType")
                    and lift_activate_pos != -1
                    and token_pos > lift_activate_pos
                ):
                    errors.append(
                        f"{function_name}:{line} calls liftActivate before "
                        f"object-source proof token {token!r}"
                    )
                elif token.startswith("AI action ") and token_pos < match.start():
                    errors.append(
                        f"{function_name}:{line} logs lift-number proof before "
                        f"source validation token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_LIFT_NUMBER_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        if "g_Lifts" not in body:
            errors.append(f"{function_name}:missing expected g_Lifts access")
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing lift-number proof token {token!r}"
                )

    for function_name, proof_tokens in sorted(
        SCENARIO_LIFT_NUMBER_CALLER_PROOF_TOKENS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing lift-number caller proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_LIFT_NUMBER_ERROR + ": " + ", ".join(errors)]


def scan_scenario_autocut_global_guards(root: Path) -> list[str]:
    """Reject autocut state access outside graph-source player cutscene paths."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario autocut global guards: {exc}"
        ]

    autocut_access = re.compile(
        r"\bg_Vars\s*\.\s*(?:autocutplaying|autocutfinished)\b"
    )
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in autocut_access.finditer(body):
            proof_tokens = SCENARIO_AUTOCUT_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} touches autocut globals outside "
                    "a source-proven cutscene/object graph path"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} touches autocut globals "
                        f"without source proof token {token!r}"
                    )
                elif (
                    token.startswith(
                        (
                            "s_aiPlayerCutsceneGraphReady",
                            "s_aiEntityLifecycleGraphReady",
                            "s_aiGraphRequireRuntimeObjectTag",
                            "obj = objFindByTagId",
                            "debugAllowEndLevel",
                        )
                    )
                    and token_pos > match.start()
                ):
                    errors.append(
                        f"{function_name}:{line} touches autocut globals "
                        f"before source proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_AUTOCUT_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        if "g_Vars.autocut" not in body:
            errors.append(
                f"{function_name}:missing expected autocut global access"
            )
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing autocut proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_AUTOCUT_ERROR + ": " + ", ".join(errors)]


def scan_scenario_frame_state_guards(root: Path) -> list[str]:
    """Reject frame-state globals outside source-proven graph paths."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario frame-state guards: {exc}"
        ]

    frame_state_access = re.compile(
        r"\bg_Vars\s*\.\s*(?:lvframenum|lvframe60)\b"
    )
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in frame_state_access.finditer(body):
            proof_tokens = SCENARIO_FRAME_STATE_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} touches frame-state globals "
                    "outside a source-proven graph path"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} touches frame-state globals "
                        f"without source proof token {token!r}"
                    )
                elif (
                    token.startswith(
                        (
                            "s_aiGraphRequireMiscEffectNode",
                            "s_aiGraphRequireRuntimeCharacterRefFromBase",
                            "s_aiSetupSpawnGraphReady",
                            "s_aiGraphRequireRuntimeObjectTagType",
                            "obj = objFindByTagId",
                        )
                    )
                    and token_pos > match.start()
                ):
                    errors.append(
                        f"{function_name}:{line} touches frame-state globals "
                        f"before source proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_FRAME_STATE_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        if "g_Vars.lvframe" not in body:
            errors.append(
                f"{function_name}:missing expected frame-state global access"
            )
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing frame-state proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_FRAME_STATE_ERROR + ": " + ", ".join(errors)]


def scan_scenario_kill_count_guards(root: Path) -> list[str]:
    """Reject kill-count reads outside mission/global source proof."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario kill-count guards: {exc}"
        ]

    kill_count_access = re.compile(r"\bg_Vars\s*\.\s*killcount\b")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in kill_count_access.finditer(body):
            proof_tokens = SCENARIO_KILL_COUNT_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} reads g_Vars.killcount outside "
                    "a mission/global source-proven condition"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} reads g_Vars.killcount "
                        f"without mission/global proof token {token!r}"
                    )
                elif (
                    token.startswith("s_aiGraphRequireMissionGlobalConditionNode")
                    and token_pos > match.start()
                ):
                    errors.append(
                        f"{function_name}:{line} reads g_Vars.killcount before "
                        f"mission/global proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_KILL_COUNT_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        if "g_Vars.killcount" not in body:
            errors.append(f"{function_name}:missing expected kill-count read")
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing kill-count proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_KILL_COUNT_ERROR + ": " + ", ".join(errors)]


def scan_scenario_stage_number_guards(root: Path) -> list[str]:
    """Reject direct stage-number globals outside source-proven exceptions."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario stage-number guards: {exc}"
        ]

    stage_number_global = re.compile(r"\bg_Vars\s*\.\s*stagenum\b")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        matches = list(stage_number_global.finditer(body))
        if not matches:
            continue
        proof_shape = SCENARIO_STAGE_NUM_SOURCE_PROVEN_FUNCTIONS.get(
            function_name
        )
        if proof_shape is None:
            for match in matches:
                line = text.count("\n", 0, start + match.start()) + 1
                errors.append(
                    f"{function_name}:{line} reads {match.group(0)} outside "
                    "a source-proven stage-number special-case handler"
                )
            continue

        expected_globals = proof_shape["globals"]
        found_globals = tuple(match.group(0) for match in matches)
        if found_globals != expected_globals:
            line = text.count("\n", 0, start + matches[0].start()) + 1
            errors.append(
                f"{function_name}:{line} reads stage-number globals as "
                f"{found_globals!r}; expected {expected_globals!r}"
            )
        for token in proof_shape["pre"]:
            token_pos = body.find(token)
            line = text.count("\n", 0, start + matches[0].start()) + 1
            if token_pos == -1:
                errors.append(
                    f"{function_name}:{line} reads stage-number globals "
                    f"without source proof token {token!r}"
                )
            elif token_pos > matches[0].start():
                errors.append(
                    f"{function_name}:{line} reads stage-number globals before "
                    f"source proof token {token!r}"
                )
        for token in proof_shape["required"]:
            if token not in body:
                line = text.count("\n", 0, start + matches[0].start()) + 1
                errors.append(
                    f"{function_name}:{line} reads stage-number globals "
                    f"without required special-case token {token!r}"
                )

    for function_name, proof_shape in sorted(
        SCENARIO_STAGE_NUM_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        found_globals = tuple(
            match.group(0) for match in stage_number_global.finditer(body)
        )
        if found_globals != proof_shape["globals"]:
            errors.append(
                f"{function_name}:missing expected stage-number globals "
                f"{proof_shape['globals']!r}; found {found_globals!r}"
            )
        for token in proof_shape["pre"] + proof_shape["required"]:
            if token not in body:
                errors.append(
                    f"{function_name}:missing stage-number proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_STAGE_NUM_ERROR + ": " + ", ".join(errors)]


def scan_scenario_player_autowalk_state_guards(root: Path) -> list[str]:
    """Reject direct player autowalk state reads outside player-slot proof."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario player autowalk state guards: {exc}"
        ]

    player_autowalk_state = re.compile(r"\bg_Vars\s*\.\s*tickmode\b")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in player_autowalk_state.finditer(body):
            proof_tokens = (
                SCENARIO_PLAYER_AUTOWALK_STATE_SOURCE_PROVEN_FUNCTIONS.get(
                    function_name
                )
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} reads {match.group(0)} outside "
                    "a player-slot-proven autowalk condition"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} reads {match.group(0)} "
                        f"without player autowalk proof token {token!r}"
                    )
                elif not token.startswith("AI condition ") and token_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} reads {match.group(0)} before "
                        f"player autowalk proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_PLAYER_AUTOWALK_STATE_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        if "g_Vars.tickmode" not in body:
            errors.append(f"{function_name}:missing expected player autowalk state read")
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing player autowalk proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_PLAYER_AUTOWALK_STATE_ERROR + ": " + ", ".join(errors)]


def scan_scenario_anti_player_global_guards(root: Path) -> list[str]:
    """Reject direct Anti-player globals outside source-proven team updates."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario Anti-player global guards: {exc}"
        ]

    anti_player_global = re.compile(r"\bg_Vars\s*\.\s*antiplayernum\b")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in anti_player_global.finditer(body):
            proof_shape = SCENARIO_ANTI_PLAYER_GLOBAL_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_shape is None:
                errors.append(
                    f"{function_name}:{line} reads {match.group(0)} outside "
                    "a source-proven chr_set_team Anti-player branch"
                )
                continue
            for token in proof_shape["pre"]:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} reads {match.group(0)} "
                        f"without Anti-player source proof token {token!r}"
                    )
                elif token_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} reads {match.group(0)} before "
                        f"Anti-player source proof token {token!r}"
                    )
            for token in proof_shape["required"]:
                if token not in body:
                    errors.append(
                        f"{function_name}:{line} reads {match.group(0)} "
                        f"without Anti-player branch token {token!r}"
                    )

    for function_name, proof_shape in sorted(
        SCENARIO_ANTI_PLAYER_GLOBAL_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        if "g_Vars.antiplayernum" not in body:
            errors.append(f"{function_name}:missing expected Anti-player global read")
        for token in proof_shape["pre"] + proof_shape["required"]:
            if token not in body:
                errors.append(
                    f"{function_name}:missing Anti-player proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_ANTI_PLAYER_GLOBAL_ERROR + ": " + ", ".join(errors)]


def scan_scenario_player_control_guards(root: Path) -> list[str]:
    """Reject direct player-control table writes without player-slot proof."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario player-control guards: {exc}"
        ]

    control_access = re.compile(r"\bg_PlayersWithControl\s*\[")
    direct_current_player = re.compile(
        r"\bg_PlayersWithControl\s*\[\s*g_Vars\.currentplayernum\s*\]"
    )
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in direct_current_player.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            errors.append(
                f"{function_name}:{line} writes player control through "
                "g_Vars.currentplayernum instead of a proven player slot"
            )
        for match in control_access.finditer(body):
            proof_tokens = SCENARIO_PLAYER_CONTROL_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} touches g_PlayersWithControl "
                    "outside a source-proven player-control handler"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} touches g_PlayersWithControl "
                        f"without proof token {token!r}"
                    )
                elif token.startswith("g_PlayersWithControl") and token_pos != match.start():
                    errors.append(
                        f"{function_name}:{line} unexpected player-control "
                        f"write shape; expected {token!r}"
                    )
                elif not token.startswith("g_PlayersWithControl") and token_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} touches g_PlayersWithControl "
                        f"before proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_PLAYER_CONTROL_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing player-control proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_PLAYER_CONTROL_ERROR + ": " + ", ".join(errors)]


def scan_scenario_audio_alias_guards(root: Path) -> list[str]:
    """Reject direct Scenario audio alias table/count reads outside catalog resolution."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario audio alias guards: {exc}"
        ]

    alias_access = re.compile(
        r"\b(?:g_AudioRussMappings\s*\[|g_NumAudioRussMappings\b)"
    )
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in alias_access.finditer(body):
            proof_tokens = SCENARIO_AUDIO_ALIAS_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} touches Scenario audio alias mapping/count "
                    "outside the source-proven audio catalog normalizer"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} touches Scenario audio alias mapping/count "
                        f"without audio proof token {token!r}"
                    )
                elif (
                    "g_AudioRussMappings" not in token
                    and "g_NumAudioRussMappings" not in token
                    and "return (s32)mapped.id" not in token
                    and token_pos > match.start()
                ):
                    errors.append(
                        f"{function_name}:{line} touches Scenario audio alias mapping/count "
                        f"before audio proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_AUDIO_ALIAS_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing audio alias proof token {token!r}"
                )

    for function_name, proof_tokens in sorted(
        SCENARIO_AUDIO_CATALOG_RESOLVER_PROOF_TOKENS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        previous_pos = -1
        for token in proof_tokens:
            token_pos = body.find(token)
            if token_pos == -1:
                errors.append(
                    f"{function_name}:missing audio catalog proof token {token!r}"
                )
                continue
            if token_pos < previous_pos:
                errors.append(
                    f"{function_name}:audio catalog proof token {token!r} "
                    "appears out of order"
                )
            previous_pos = token_pos

    if not errors:
        return []
    return [SCENARIO_AUDIO_ALIAS_ERROR + ": " + ", ".join(errors)]


def scan_scenario_audio_source_metadata_guards(root: Path) -> list[str]:
    """Reject Scenario ROM audio metadata helpers outside extracted source proof."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario audio source metadata guards: {exc}"
        ]

    rom_metadata_call = re.compile(
        r"\b(?:romExtractRelPathForFilenum|romdataFileGetName)\s*\("
    )
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in rom_metadata_call.finditer(body):
            proof_tokens = SCENARIO_AUDIO_SOURCE_METADATA_PROOF_TOKENS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} calls {match.group(0)} outside "
                    "the extracted speech source helper"
                )
                continue
            previous_pos = -1
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} calls {match.group(0)} "
                        f"without speech source proof token {token!r}"
                    )
                    continue
                if token_pos < previous_pos:
                    errors.append(
                        f"{function_name}:{line} speech source proof token "
                        f"{token!r} appears out of order"
                    )
                previous_pos = token_pos

    for function_name, proof_tokens in sorted(
        SCENARIO_AUDIO_SOURCE_METADATA_PROOF_TOKENS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing speech source proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_AUDIO_SOURCE_METADATA_ERROR + ": " + ", ".join(errors)]


def scan_scenario_special_death_animation_guards(root: Path) -> list[str]:
    """Reject direct special-death animation rows outside catalog resolution."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario special-death animation guards: {exc}"
        ]

    anim_table_access = re.compile(r"\bg_SpecialDieAnims\s*\[")
    specialdie_assignment = re.compile(r"\bchr\s*->\s*specialdie\s*=\s*animation\s*;")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in anim_table_access.finditer(body):
            proof_tokens = SCENARIO_SPECIAL_DEATH_ANIM_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} touches g_SpecialDieAnims "
                    "outside the source-proven special-death animation resolver"
                )
                continue
            for token in proof_tokens:
                if token not in body:
                    errors.append(
                        f"{function_name}:{line} touches g_SpecialDieAnims "
                        f"without animation proof token {token!r}"
                    )

        for match in specialdie_assignment.finditer(body):
            proof_tokens = (
                SCENARIO_SPECIAL_DEATH_ANIM_ASSIGNMENT_PROOF_TOKENS.get(
                    function_name
                )
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} assigns chr->specialdie "
                    "outside the source-proven special-death graph action"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} assigns chr->specialdie "
                        f"without special-death proof token {token!r}"
                    )
                elif (
                    token != "chr->specialdie = animation;"
                    and "anim_id=%s" not in token
                    and token_pos > match.start()
                ):
                    errors.append(
                        f"{function_name}:{line} assigns chr->specialdie "
                        f"before special-death proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_SPECIAL_DEATH_ANIM_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing special-death animation proof token {token!r}"
                )

    for function_name, proof_tokens in sorted(
        SCENARIO_SPECIAL_DEATH_ANIM_ASSIGNMENT_PROOF_TOKENS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing special-death assignment proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_SPECIAL_DEATH_ANIM_ERROR + ": " + ", ".join(errors)]


def scan_scenario_quip_asset_table_guards(root: Path) -> list[str]:
    """Reject direct quip table/pointer reads outside character-proven catalog paths."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario quip asset table guards: {exc}"
        ]

    quip_table_access = re.compile(
        r"\bg_(?:Guard|Special|Skedar|Maian)QuipBank\b|"
        r"\bg_QuipTexts\s*\[|"
        r"\bg_Ci(?:Greeting|Main|Annoyed|Thanks)Quips\s*\["
    )
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in quip_table_access.finditer(body):
            proof_tokens = SCENARIO_QUIP_ASSET_TABLE_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} touches quip asset tables outside "
                    "a source-proven quip graph action"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1:
                    errors.append(
                        f"{function_name}:{line} touches quip asset tables "
                        f"without quip proof token {token!r}"
                    )
                elif (
                    token.startswith("s_aiGraphRequire")
                    and token_pos > match.start()
                ):
                    errors.append(
                        f"{function_name}:{line} touches quip asset tables "
                        f"before character/source proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_QUIP_ASSET_TABLE_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        if not quip_table_access.search(body):
            errors.append(f"{function_name}:missing expected quip asset table access")
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing quip asset table proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_QUIP_ASSET_TABLE_ERROR + ": " + ", ".join(errors)]


def scan_scenario_chr_find_guards(root: Path) -> list[str]:
    """Reject direct character lookup outside source-aware character helpers."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario chrFindById guards: {exc}"
        ]

    chr_find = re.compile(r"\bchrFindById\s*\(")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in chr_find.finditer(body):
            proof_tokens = SCENARIO_CHR_FIND_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} calls chrFindById outside "
                    "a source-aware character-reference helper"
                )
                continue
            for token in proof_tokens:
                if token not in body:
                    errors.append(
                        f"{function_name}:{line} missing chrFindById proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_CHR_FIND_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing chrFindById evidence token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_CHR_FIND_ERROR + ": " + ", ".join(errors)]


def scan_scenario_literal_chr_find_guards(root: Path) -> list[str]:
    """Reject literal-id character scans unless each live hit is source-proven."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario chrFindByLiteralId guards: {exc}"
        ]

    literal_find = re.compile(r"\bchrFindByLiteralId\s*\(")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        matches = list(literal_find.finditer(body))
        for index, match in enumerate(matches):
            proof_tokens = SCENARIO_LITERAL_CHR_FIND_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} calls chrFindByLiteralId outside "
                    "a source-proven squad/team scan handler"
                )
                continue

            next_call = matches[index + 1].start() if index + 1 < len(matches) else len(body)
            proof_window = body[match.end():next_call]
            has_pointer_proof = (
                "s_aiGraphRequireRuntimeCharacterPointer(" in proof_window
                or "s_aiGraphRequireRuntimeCharacterStatePointer(" in proof_window
                or "s_aiGraphRuntimeCharacterPointerLooksSourceBacked(" in proof_window
            )
            if not has_pointer_proof:
                errors.append(
                    f"{function_name}:{line} calls chrFindByLiteralId without "
                    "runtime character/state proof before the next literal-id scan"
                )

            for token in proof_tokens:
                if token not in body:
                    errors.append(
                        f"{function_name}:{line} missing literal-id scan proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_LITERAL_CHR_FIND_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        if not literal_find.search(body):
            errors.append(f"{function_name}:missing literal-id character scan")
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing literal-id scan evidence token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_LITERAL_CHR_FIND_ERROR + ": " + ", ".join(errors)]


def scan_scenario_prop_target_index_guards(root: Path) -> list[str]:
    """Reject propGetIndexByChrId calls unless source proofs precede them."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario prop-target index guards: {exc}"
        ]

    prop_target_index = re.compile(r"\bpropGetIndexByChrId\s*\(")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in prop_target_index.finditer(body):
            proof_tokens = SCENARIO_PROP_TARGET_INDEX_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                errors.append(
                    f"{function_name}:{line} derives a prop target index "
                    "outside a source-proven character/target handler"
                )
                continue
            for token in proof_tokens:
                token_pos = body.find(token)
                if token_pos == -1 or token_pos > match.start():
                    errors.append(
                        f"{function_name}:{line} derives a prop target index "
                        f"before source proof token {token!r}"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_PROP_TARGET_INDEX_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                errors.append(
                    f"{function_name}:missing prop-target index proof token {token!r}"
                )
        for token in SCENARIO_PROP_TARGET_INDEX_REQUIRED_TOKENS.get(
            function_name, ()
        ):
            if token not in body:
                errors.append(
                    f"{function_name}:missing prop-target index evidence token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_PROP_TARGET_INDEX_ERROR + ": " + ", ".join(errors)]


def scan_scenario_list_control_global_guards(root: Path) -> list[str]:
    """Reject raw actor global dereferences in source-proven list-control actions."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario list-control guards: {exc}"
        ]

    global_deref = re.compile(
        r"\bg_Vars\.(?:chrdata|truck|heli|hovercar)\s*->"
    )
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name in sorted(SCENARIO_LIST_CONTROL_SOURCE_PROVEN_FUNCTIONS):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for match in global_deref.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            errors.append(
                f"{function_name}:{line} dereferences {match.group(0)} "
                "instead of a source-proven local actor pointer"
            )

    if not errors:
        return []
    return [SCENARIO_LIST_CONTROL_GLOBAL_ERROR + ": " + ", ".join(errors)]


def scan_scenario_quip_global_guards(root: Path) -> list[str]:
    """Reject raw active-character global dereferences in source-proven quip actions."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario quip guards: {exc}"
        ]

    global_deref = re.compile(r"\bg_Vars\.chrdata\s*->")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name in sorted(SCENARIO_QUIP_SOURCE_PROVEN_FUNCTIONS):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for match in global_deref.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            errors.append(
                f"{function_name}:{line} dereferences {match.group(0)} "
                "instead of a source-proven local character pointer"
            )

    if not errors:
        return []
    return [SCENARIO_QUIP_GLOBAL_ERROR + ": " + ", ".join(errors)]


def scan_scenario_character_state_global_guards(root: Path) -> list[str]:
    """Reject raw character-state global dereferences after source proof."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario character-state guards: {exc}"
        ]

    global_deref = re.compile(r"\bg_Vars\.chrdata\s*->")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name in sorted(SCENARIO_CHARACTER_STATE_SOURCE_PROVEN_FUNCTIONS):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for match in global_deref.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            errors.append(
                f"{function_name}:{line} dereferences {match.group(0)} "
                "instead of a source-proven local character-state pointer"
            )

    if not errors:
        return []
    return [SCENARIO_CHARACTER_STATE_GLOBAL_ERROR + ": " + ", ".join(errors)]


def scan_scenario_setup_equipment_global_guards(root: Path) -> list[str]:
    """Reject raw active-character globals in source-proven setup/equipment actions."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario setup/equipment guards: {exc}"
        ]

    global_deref = re.compile(r"\bg_Vars\.chrdata\s*->")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name in sorted(SCENARIO_SETUP_EQUIPMENT_SOURCE_PROVEN_FUNCTIONS):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for match in global_deref.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            errors.append(
                f"{function_name}:{line} dereferences {match.group(0)} "
                "instead of a source-proven local character pointer"
            )

    if not errors:
        return []
    return [SCENARIO_SETUP_EQUIPMENT_GLOBAL_ERROR + ": " + ", ".join(errors)]


def scan_scenario_cutscene_visibility_slot_guards(root: Path) -> list[str]:
    """Reject raw slot visibility writes in source-proven cutscene actions."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario cutscene visibility guards: {exc}"
        ]

    direct_slot_write = re.compile(
        r"\bg_ChrSlots\s*\[[^\]]+\]\s*\.\s*(?:hidden2|chrflags)\s*[|&^]?="
    )
    direct_slot_access = re.compile(r"\bg_ChrSlots\s*\[")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []
    scan_errors: list[str] = []

    for function_name in sorted(
        SCENARIO_CUTSCENE_VISIBILITY_SOURCE_PROVEN_FUNCTIONS
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for match in direct_slot_write.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            errors.append(
                f"{function_name}:{line} writes {match.group(0)} "
                "instead of a source-proven local character slot pointer"
            )

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        matches = list(direct_slot_access.finditer(body))
        if not matches:
            continue

        proof_tokens = (
            SCENARIO_CUTSCENE_VISIBILITY_SLOT_SCAN_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
        )
        for index, match in enumerate(matches):
            line = text.count("\n", 0, start + match.start()) + 1
            if proof_tokens is None:
                scan_errors.append(
                    f"{function_name}:{line} scans g_ChrSlots outside "
                    "the source-proven cutscene visibility preflight"
                )
                continue
            for token in proof_tokens[:3]:
                token_pos = body.find(token)
                if token_pos == -1 or token_pos > match.start():
                    scan_errors.append(
                        f"{function_name}:{line} scans g_ChrSlots before "
                        f"source preflight token {token!r}"
                    )
            assignment = "chr = &g_ChrSlots[i];"
            assignment_pos = body.find(assignment, max(0, match.start() - 16))
            if assignment_pos == -1 or assignment_pos > match.start():
                scan_errors.append(
                    f"{function_name}:{line} scans g_ChrSlots without "
                    f"expected local assignment {assignment!r}"
                )
            next_start = (
                matches[index + 1].start()
                if index + 1 < len(matches)
                else len(body)
            )
            proof_window = body[match.end():next_start]
            for token in proof_tokens[4:]:
                if token not in proof_window:
                    scan_errors.append(
                        f"{function_name}:{line} scans g_ChrSlots without "
                        f"slot preflight token {token!r} before the next slot scan"
                    )

    for function_name, proof_tokens in sorted(
        SCENARIO_CUTSCENE_VISIBILITY_SLOT_SCAN_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            scan_errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for token in proof_tokens:
            if token not in body:
                scan_errors.append(
                    f"{function_name}:missing cutscene slot scan proof token {token!r}"
                )

    results: list[str] = []
    if errors:
        results.append(
            SCENARIO_CUTSCENE_VISIBILITY_SLOT_ERROR + ": " + ", ".join(errors)
        )
    if scan_errors:
        results.append(
            SCENARIO_CUTSCENE_VISIBILITY_SLOT_SCAN_ERROR
            + ": "
            + ", ".join(scan_errors)
        )
    return results


def scan_scenario_mission_global_player_guards(root: Path) -> list[str]:
    """Reject raw mission-global player writes after source proof."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario mission-global player guards: {exc}"
        ]

    direct_player_write = re.compile(r"\bg_Vars\.bond\s*->\s*isdead\s*=")
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name in sorted(
        SCENARIO_MISSION_GLOBAL_PLAYER_SOURCE_PROVEN_FUNCTIONS
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        for match in direct_player_write.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            errors.append(
                f"{function_name}:{line} writes {match.group(0)} "
                "instead of a source-proven local player pointer"
            )

    if not errors:
        return []
    return [
        SCENARIO_MISSION_GLOBAL_PLAYER_ERROR + ": " + ", ".join(errors)
    ]


def scan_scenario_player_identity_global_guards(root: Path) -> list[str]:
    """Reject raw Bond/Co-op player-number access outside source/player proof."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario player identity guards: {exc}"
        ]

    player_identity_global = re.compile(
        r"\bg_Vars\s*\.\s*(?:bondplayernum|coopplayernum)\b"
    )
    blocks = {
        function_name: (start, end)
        for function_name, start, end in iter_c_function_blocks(text)
    }
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        matches = list(player_identity_global.finditer(body))
        if not matches:
            continue
        proof_shape = (
            SCENARIO_PLAYER_IDENTITY_GLOBAL_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
        )
        if proof_shape is None:
            for match in matches:
                line = text.count("\n", 0, start + match.start()) + 1
                errors.append(
                    f"{function_name}:{line} reads {match.group(0)} outside "
                    "a source/player-proven Bond/Co-op identity handler"
                )
            continue

        expected_globals = proof_shape["globals"]
        found_globals = tuple(match.group(0) for match in matches)
        if found_globals != expected_globals:
            line = text.count("\n", 0, start + matches[0].start()) + 1
            errors.append(
                f"{function_name}:{line} reads Bond/Co-op player-number globals "
                f"as {found_globals!r}; expected {expected_globals!r}"
            )
        for token in proof_shape["pre"]:
            token_pos = body.find(token)
            line = text.count("\n", 0, start + matches[0].start()) + 1
            if token_pos == -1:
                errors.append(
                    f"{function_name}:{line} reads Bond/Co-op player-number "
                    f"globals without source/player proof token {token!r}"
                )
            elif token_pos > matches[0].start():
                errors.append(
                    f"{function_name}:{line} reads Bond/Co-op player-number "
                    f"globals before source/player proof token {token!r}"
                )
        for token in proof_shape["required"]:
            if token not in body:
                line = text.count("\n", 0, start + matches[0].start()) + 1
                errors.append(
                    f"{function_name}:{line} reads Bond/Co-op player-number "
                    f"globals without required proof token {token!r}"
                )

    for function_name, proof_shape in sorted(
        SCENARIO_PLAYER_IDENTITY_GLOBAL_SOURCE_PROVEN_FUNCTIONS.items()
    ):
        if function_name not in blocks:
            errors.append(f"{function_name}:missing function body")
            continue
        start, end = blocks[function_name]
        body = text[start:end]
        found_globals = tuple(
            match.group(0) for match in player_identity_global.finditer(body)
        )
        if found_globals != proof_shape["globals"]:
            errors.append(
                f"{function_name}:missing expected Bond/Co-op player-number "
                f"globals {proof_shape['globals']!r}; found {found_globals!r}"
            )
        for token in proof_shape["pre"] + proof_shape["required"]:
            if token not in body:
                errors.append(
                    f"{function_name}:missing player identity proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_PLAYER_IDENTITY_GLOBAL_ERROR + ": " + ", ".join(errors)]


def scan_scenario_player_global_guards(root: Path) -> list[str]:
    """Reject raw Bond/Co-op field access after source/player proof."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario player-global guards: {exc}"
        ]

    direct_player_field = re.compile(
        r"\bg_Vars\.(?:bond|coop)\s*->\s*[A-Za-z_][A-Za-z0-9_]*"
    )
    direct_player_pointer = re.compile(r"\bg_Vars\.(?:bond|coop)\b(?!\s*->)")
    errors: list[str] = []

    for function_name, start, end in iter_c_function_blocks(text):
        body = text[start:end]
        for match in direct_player_field.finditer(body):
            line = text.count("\n", 0, start + match.start()) + 1
            errors.append(
                f"{function_name}:{line} reads {match.group(0)} "
                "instead of a source-proven local player pointer"
            )
        pointer_matches = list(direct_player_pointer.finditer(body))
        if not pointer_matches:
            continue

        proof_shape = (
            SCENARIO_PLAYER_GLOBAL_POINTER_SOURCE_PROVEN_FUNCTIONS.get(
                function_name
            )
        )
        if proof_shape is None:
            for match in pointer_matches:
                line = text.count("\n", 0, start + match.start()) + 1
                errors.append(
                    f"{function_name}:{line} reads {match.group(0)} "
                    "outside a source-proven Bond/Co-op player handler"
                )
            continue

        expected_globals, proof_tokens = proof_shape
        found_globals = tuple(match.group(0) for match in pointer_matches)
        if found_globals != expected_globals:
            line = text.count("\n", 0, start + pointer_matches[0].start()) + 1
            errors.append(
                f"{function_name}:{line} reads Bond/Co-op globals as "
                f"{found_globals!r}; expected {expected_globals!r}"
            )
        for token in proof_tokens:
            if token not in body:
                line = text.count("\n", 0, start + pointer_matches[0].start()) + 1
                errors.append(
                    f"{function_name}:{line} reads Bond/Co-op globals "
                    f"without source proof token {token!r}"
                )

    if not errors:
        return []
    return [SCENARIO_PLAYER_GLOBAL_ERROR + ": " + ", ".join(errors)]


def scan_scenario_runtime_global_inventory(root: Path) -> list[str]:
    """Reject new non-interpreter Scenario globals until they are audited."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario runtime-global inventory: "
            f"{exc}"
        ]

    counts = Counter(re.findall(r"g_Vars\.[A-Za-z_][A-Za-z0-9_]*", text))
    errors: list[str] = []

    for global_name, count in sorted(counts.items()):
        if global_name in SCENARIO_AI_INTERPRETER_GLOBALS:
            continue

        max_count = SCENARIO_RUNTIME_GLOBAL_MAX_COUNTS.get(global_name)

        if max_count is None:
            errors.append(f"{global_name} is not in the guarded inventory")
        elif count > max_count:
            errors.append(
                f"{global_name} count {count} exceeds guarded inventory "
                f"{max_count}"
            )

    if not errors:
        return []
    return [SCENARIO_RUNTIME_GLOBAL_INVENTORY_ERROR + ": " + ", ".join(errors)]


def scan_scenario_runtime_external_global_inventory(root: Path) -> list[str]:
    """Reject new Scenario g_* globals until they are explicitly audited."""
    path = root / "port/src/scenario_source_runtime.c"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [
            "cannot read "
            f"{relpath(path, root)} for Scenario external-global inventory: "
            f"{exc}"
        ]

    counts = Counter(re.findall(r"\bg_[A-Za-z_][A-Za-z0-9_]*\b", text))
    counts.pop("g_Vars", None)
    errors: list[str] = []

    for global_name, count in sorted(counts.items()):
        max_count = SCENARIO_RUNTIME_EXTERNAL_GLOBAL_MAX_COUNTS.get(global_name)

        if max_count is None:
            errors.append(f"{global_name} is not in the guarded inventory")
        elif count > max_count:
            errors.append(
                f"{global_name} count {count} exceeds guarded inventory "
                f"{max_count}"
            )

    if not errors:
        return []
    return [
        SCENARIO_RUNTIME_EXTERNAL_GLOBAL_INVENTORY_ERROR
        + ": "
        + ", ".join(errors)
    ]


def staged_files(root: Path) -> list[str]:
    try:
        result = subprocess.run(
            ["git", "diff", "--cached", "--name-only"],
            cwd=root,
            check=True,
            capture_output=True,
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return []
    return [line.strip().replace("\\", "/") for line in result.stdout.splitlines()
            if line.strip()]


def path_has_prefix(path: str, prefixes: tuple[str, ...]) -> bool:
    return any(path == prefix.rstrip("/") or path.startswith(prefix)
               for prefix in prefixes)


def require_staged_evidence(root: Path) -> list[str]:
    staged = staged_files(root)
    if not staged:
        return []
    touches_asset_pipeline = any(path_has_prefix(p, ASSET_SENSITIVE_PREFIXES)
                                 for p in staged)
    if not touches_asset_pipeline:
        return []
    has_evidence = any(path_has_prefix(p, ASSET_EVIDENCE_PREFIXES)
                       for p in staged)
    if has_evidence:
        return []
    return [
        "asset-pipeline files are staged without c3842 evidence. Stage tests or "
        "context showing how public editable source remains the native client source."
    ]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--staged",
        action="store_true",
        help="also require c3842 evidence when asset-pipeline files are staged",
    )
    args = parser.parse_args(argv)

    root = repo_root()
    errors: list[str] = []
    errors.extend(require_sentinals(root))
    errors.extend(require_kanban(root))
    errors.extend(scan_bondgun_model_source_only_guards(root))
    errors.extend(scan_model_handle_source_only_guards(root))
    errors.extend(scan_modelcatalog_source_only_guards(root))
    errors.extend(scan_catalog_metadata_preload_source_only_guard(root))
    errors.extend(scan_forge_runtime_source_only_guards(root))
    errors.extend(scan_setup_modeldef_source_only_guard(root))
    errors.extend(scan_character_manager_modeldef_source_only_guards(root))
    errors.extend(scan_music_sequence_source_only_guard(root))
    errors.extend(scan_sound_file_source_only_guard(root))
    errors.extend(scan_mp3_audio_source_only_guard(root))
    errors.extend(scan_example_archives(root))
    errors.extend(scan_generated_scenario_texture_contracts(root))
    errors.extend(scan_ui_chrome_source_contract(root))
    errors.extend(scan_ai_command_graph_coverage(root))
    errors.extend(scan_ai_opcode_name_coverage(root))
    errors.extend(scan_scenario_normal_play_fallback_guards(root))
    errors.extend(scan_scenario_runtime_count_guards(root))
    errors.extend(scan_scenario_padfile_global_guards(root))
    errors.extend(scan_scenario_stage_setup_global_guards(root))
    errors.extend(scan_scenario_setup_tag_global_guards(root))
    errors.extend(scan_scenario_setup_behavior_link_source_guards(root))
    errors.extend(scan_scenario_source_wide_pad_cache_guards(root))
    errors.extend(scan_scenario_runtime_lookup_guards(root))
    errors.extend(scan_scenario_player_state_access_guards(root))
    errors.extend(scan_scenario_current_player_number_guards(root))
    errors.extend(scan_scenario_active_character_pointer_guards(root))
    errors.extend(scan_scenario_vehicle_motion_global_guards(root))
    errors.extend(scan_scenario_misc_branch_vehicle_guards(root))
    errors.extend(scan_scenario_prop_target_vehicle_guards(root))
    errors.extend(scan_scenario_timer_vehicle_guards(root))
    errors.extend(scan_scenario_hovercar_branch_guards(root))
    errors.extend(scan_scenario_prop_index_guards(root))
    errors.extend(scan_scenario_prop_preset_index_guards(root))
    errors.extend(scan_scenario_room_prop_scan_guards(root))
    errors.extend(scan_scenario_scene_room_table_guards(root))
    errors.extend(scan_scenario_scene_room_global_guards(root))
    errors.extend(scan_scenario_portal_table_guards(root))
    errors.extend(scan_scenario_portal_global_guards(root))
    errors.extend(scan_scenario_mission_music_mode_global_guards(root))
    errors.extend(scan_scenario_player_invincibility_guards(root))
    errors.extend(scan_scenario_environment_global_guards(root))
    errors.extend(scan_scenario_cutscene_frame_overrun_guards(root))
    errors.extend(scan_scenario_teleport_sound_priority_guards(root))
    errors.extend(scan_scenario_lift_number_guards(root))
    errors.extend(scan_scenario_autocut_global_guards(root))
    errors.extend(scan_scenario_frame_state_guards(root))
    errors.extend(scan_scenario_kill_count_guards(root))
    errors.extend(scan_scenario_stage_number_guards(root))
    errors.extend(scan_scenario_player_autowalk_state_guards(root))
    errors.extend(scan_scenario_anti_player_global_guards(root))
    errors.extend(scan_scenario_player_control_guards(root))
    errors.extend(scan_scenario_audio_alias_guards(root))
    errors.extend(scan_scenario_audio_source_metadata_guards(root))
    errors.extend(scan_scenario_special_death_animation_guards(root))
    errors.extend(scan_scenario_quip_asset_table_guards(root))
    errors.extend(scan_scenario_chr_find_guards(root))
    errors.extend(scan_scenario_literal_chr_find_guards(root))
    errors.extend(scan_scenario_prop_target_index_guards(root))
    errors.extend(scan_scenario_list_control_global_guards(root))
    errors.extend(scan_scenario_quip_global_guards(root))
    errors.extend(scan_scenario_character_state_global_guards(root))
    errors.extend(scan_scenario_setup_equipment_global_guards(root))
    errors.extend(scan_scenario_cutscene_visibility_slot_guards(root))
    errors.extend(scan_scenario_mission_global_player_guards(root))
    errors.extend(scan_scenario_player_identity_global_guards(root))
    errors.extend(scan_scenario_player_global_guards(root))
    errors.extend(scan_scenario_runtime_global_inventory(root))
    errors.extend(scan_scenario_runtime_external_global_inventory(root))
    if args.staged:
        errors.extend(require_staged_evidence(root))

    if errors:
        print("asset-native-source guard failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        print(
            "\nContract: public typed-archive source files are the game-facing "
            "source; generated products are cache only.",
            file=sys.stderr,
        )
        return 1

    print("asset-native-source guard ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
