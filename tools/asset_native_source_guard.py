#!/usr/bin/env python3
"""Guard the c3842 Asset Pipeline native-source contract.

The contract: typed asset archives expose directly editable public source
files, and the game client consumes those same files through catalog/provider
loading. Generated runtime products are source-hashed cache only.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import zipfile
from pathlib import Path


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
    "pads.tsv",
    "spawns.tsv",
    "volumes.tsv",
    "objects.tsv",
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
    "head",
    "head_id",
    "head_ref",
    "headnum",
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
    "music_ref",
    "projectile",
    "projectile_id",
    "projectile_ref",
    "scenario",
    "scenario_id",
    "scenario_ref",
    "sfx",
    "sfx_id",
    "sfx_ref",
    "sound",
    "sound_id",
    "sound_ref",
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
        "tools/asset_native_source_guard.py",
    ],
    ".agents/skills/pd2-large-change-sweep/SKILL.md": [
        "Asset Pipeline c3842 Gate",
        "public editable source",
        "runtime-cache-only",
    ],
    "context/constraints.md": [
        "Public asset source is the game-facing source",
        "source-hashed rebuildable cache",
    ],
    "context/designs/modding/asset-archive-clean-formats.md": [
        "The public authoring files are also the game-facing source of truth",
        "source-hashed cache",
    ],
    "context/tasks.md": [
        "Asset Pipeline native-source correction",
        "c3842",
    ],
    "context/pillars/modding.md": [
        "Public asset source is the native game source",
        "c3842",
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
        "asset_native_source_guard.py",
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
                errors.append(f"{rel} missing c3842 sentinel: {needle}")
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
    if card.get("column") not in {"active", "done"}:
        errors.append("c3842 must stay active or done after the native-source audit closes")
    if card.get("priority") != 1:
        errors.append("c3842 must stay priority 1")
    subtasks = card.get("subtasks", [])
    if card.get("column") == "active":
        if not any(s.get("id") == "c3842-s1" and s.get("status") == "active"
                   for s in subtasks):
            errors.append("c3842-s1 native-source audit subtask must remain active")
    else:
        incomplete = [
            s.get("id", "(missing)")
            for s in subtasks
            if str(s.get("id", "")).startswith("c3842-s")
            and s.get("status") != "done"
        ]
        if incomplete:
            errors.append(
                "c3842 is done but has incomplete native-source subtasks: "
                + ", ".join(incomplete)
            )
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
    errors: list[str] = []
    example_root = root / "examples/modding/typed-pdxxx-basic"
    if not example_root.exists():
        return ["examples/modding/typed-pdxxx-basic is missing"]

    archives = [
        p for p in example_root.rglob("*")
        if p.is_file() and p.suffix.lower() in TYPED_DESCRIPTORS
    ]
    if not archives:
        return ["typed-pdxxx examples contain no typed archives"]

    for archive_path in archives:
        descriptor = TYPED_DESCRIPTORS[archive_path.suffix.lower()]
        label = relpath(archive_path, root)
        try:
            with zipfile.ZipFile(archive_path) as zf:
                names = zf.namelist()
                if descriptor not in names:
                    errors.append(f"{label} missing root descriptor {descriptor}")

                public_entries = public_source_entries(names, descriptor)
                if not public_entries:
                    errors.append(f"{label} has no public editable source payload")

                if archive_path.suffix.lower() == ".pdscenario":
                    normalized_names = {n.replace("\\", "/") for n in names}
                    missing = sorted(PDSCENARIO_REQUIRED_PUBLIC_ENTRY_NAMES -
                                     normalized_names)
                    for name in missing:
                        errors.append(
                            f"{label} missing required source-first scenario file {name}"
                        )
                    if descriptor in normalized_names:
                        descriptor_text = zf.read(descriptor).decode(
                            "utf-8", errors="replace"
                        )
                        if "scene_file = scene.glb" not in descriptor_text:
                            errors.append(
                                f"{label} scenario.ini must declare scene_file = scene.glb"
                            )
                        if "runtime_source_file = scene.glb" not in descriptor_text:
                            errors.append(
                                f"{label} scenario.ini must declare runtime_source_file = scene.glb"
                            )
                        if "setup_file" in descriptor_text:
                            errors.append(
                                f"{label} scenario.ini still declares setup_file; use decoded tables/graphs"
                            )

                for name in names:
                    normalized = name.replace("\\", "/")
                    leaf = normalized.rsplit("/", 1)[-1].lower()
                    is_scenario_archive = archive_path.suffix.lower() == ".pdscenario"
                    is_embedded_scenario_entry = normalized.startswith("scenario/")
                    if ((is_scenario_archive or is_embedded_scenario_entry) and
                            leaf in PDSCENARIO_FORBIDDEN_PUBLIC_ENTRY_NAMES):
                        errors.append(
                            f"{label} contains forbidden raw scenario dump {normalized}; "
                            "decode setup into named tables/graphs with catalog IDs"
                        )
                    if leaf in FORBIDDEN_ARCHIVE_ENTRY_NAMES:
                        errors.append(
                            f"{label} contains forbidden authored runtime payload {normalized}"
                        )
                    if normalized.endswith(".bin"):
                        errors.append(
                            f"{label} contains forbidden authored .bin payload {normalized}"
                        )
                    if (normalized in public_entries and
                            normalized.lower().endswith(PUBLIC_TEXT_ENTRY_SUFFIXES)):
                        try:
                            text = zf.read(name).decode("utf-8", errors="replace")
                        except KeyError:
                            continue
                        errors.extend(scan_text_asset_refs(label, normalized, text))
        except zipfile.BadZipFile:
            errors.append(f"{label} is not zip-openable")
            continue
    return errors


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
