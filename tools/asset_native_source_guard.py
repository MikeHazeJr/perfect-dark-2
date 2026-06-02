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
}

PDSCENARIO_REQUIRED_PUBLIC_ENTRY_NAMES = {
    "scene.glb",
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
    ".tsv",
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
        "Asset Pipeline native-source correction",
        "Runtime ROM fallback is an asset-chain failure",
        "c3842",
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
    ".claude/smoke-verify-install/data/ntsc-final/scenarios",
    ".claude/smoke-verify-install/data/ntsc-final/arenas",
)

AI_INTERPRETER_ONLY_FUNCTIONS = {
    "aiEndList",
    "aiGoToNext",
    "aiGoToFirst",
    "aiLabel",
    "aiYield",
}

AI_GRAPH_PENDING_FUNCTIONS = set()


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
        if rom_card.get("priority") != 1:
            errors.append("c3844 must stay priority 1")
        if rom_card.get("column") not in {"active", "done"}:
            errors.append("c3844 must stay active or done until runtime ROM fallback removal closes")
        rom_text = " ".join(
            str(rom_card.get(k, ""))
            for k in ("title", "description", "notes")
        )
        if "ROM fallback" not in rom_text or "asset-chain failure" not in rom_text:
            errors.append("c3844 must explicitly track ROM fallback as an asset-chain failure")
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
    errors.extend(scan_example_archives(root))
    errors.extend(scan_generated_scenario_texture_contracts(root))
    errors.extend(scan_ui_chrome_source_contract(root))
    errors.extend(scan_ai_command_graph_coverage(root))
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
