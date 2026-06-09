#!/usr/bin/env python3
"""Repair stale generated metadata/weapon manifests from public descriptors.

This is an offline currentness repair for retained generated installs. It does
not weaken validation and it does not reconstruct data from private binaries:
the source values must already be present in the public descriptor. Weapon
archives may also have the old descriptor key names rewritten to the current
public names.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import shutil
import tempfile
import zipfile
from pathlib import Path
from typing import Iterable


ARCHIVE_DESCRIPTORS = {
    ".pdbotprofile": "botprofile.ini",
    ".pdgamemode": "gamemode.ini",
    ".pdmission": "mission.ini",
    ".pdprojectile": "projectile.ini",
    ".pdentity": "entity.ini",
    ".pdweapon": "weapon.ini",
}

WEAPON_KEY_RENAMES = {
    "shared_context": "shared_context_file",
    "settings": "settings_file",
    "variables": "variables_file",
}

WEAPON_REQUIRED_MEMBERS = {
    "primary_graph": "behavior/primary.graph.json",
    "secondary_graph": "behavior/secondary.graph.json",
    "shared_context_file": "behavior/shared-context.json",
    "settings_file": "behavior/settings.json",
    "variables_file": "behavior/variables.json",
}

WEAPON_OPTIONAL_MEMBERS = {
    "material_slots_file": "bindings/material-slots.json",
    "grip_sockets_file": "bindings/grip-sockets.json",
    "presentation_file": "bindings/presentation.json",
    "primary_projectile_archive": "dependencies/assets/projectiles/primary.pdprojectile",
    "deployed_entity_archive": "dependencies/assets/entities/deployed.pdentity",
    "fire_sound_archive": "dependencies/assets/audio/fire.pdsfx",
    "idle_animation_archive": "dependencies/assets/animations/idle.pdanim",
    "reticle_archive": "dependencies/assets/ui/reticle.pdui",
}


def iter_inputs(paths: Iterable[Path]) -> Iterable[Path]:
    suffixes = set(ARCHIVE_DESCRIPTORS)
    for path in paths:
        if path.is_dir():
            for suffix in sorted(suffixes):
                yield from sorted(path.rglob(f"*{suffix}"))
        elif path.suffix.lower() in suffixes:
            yield path


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_bytes(obj: object) -> bytes:
    return (json.dumps(obj, indent=2) + "\n").encode("utf-8")


def parse_ini(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith(";"):
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def rewrite_weapon_ini(text: str) -> tuple[str, bool]:
    changed = False
    lines: list[str] = []
    seen: set[str] = set()
    for raw in text.splitlines():
        if "=" not in raw:
            lines.append(raw)
            continue
        key, value = raw.split("=", 1)
        stripped_key = key.strip()
        new_key = WEAPON_KEY_RENAMES.get(stripped_key, stripped_key)
        if new_key != stripped_key:
            changed = True
        seen.add(new_key)
        lines.append(f"{new_key} = {value.strip()}")

    values = parse_ini("\n".join(lines))
    for key, member in WEAPON_REQUIRED_MEMBERS.items():
        if values.get(key) != member:
            if key not in seen:
                lines.append(f"{key} = {member}")
            else:
                lines = [
                    f"{key} = {member}" if line.startswith(f"{key} = ") else line
                    for line in lines
                ]
            changed = True
    return "\n".join(lines) + "\n", changed


def remove_ini_key(text: str, key: str) -> tuple[str, bool]:
    changed = False
    lines: list[str] = []
    for raw in text.splitlines():
        if "=" in raw and raw.split("=", 1)[0].strip() == key:
            changed = True
            continue
        lines.append(raw)
    return "\n".join(lines) + "\n", changed


def to_int(value: str) -> int:
    return int(value, 0)


def copy_string(manifest: dict, values: dict[str, str], key: str) -> bool:
    value = values.get(key)
    if not value:
        return False
    if manifest.get(key) == value:
        return False
    manifest[key] = value
    return True


def copy_int(manifest: dict, values: dict[str, str], key: str) -> bool:
    value = values.get(key)
    if value is None or value == "":
        return False
    parsed = to_int(value)
    if manifest.get(key) == parsed:
        return False
    manifest[key] = parsed
    return True


def update_hash_entry(doc: dict, path: str, size: int, digest: str) -> None:
    entries = doc.get("entries")
    if not isinstance(entries, list):
        return
    for entry in entries:
        if isinstance(entry, dict) and entry.get("path") == path:
            entry["size"] = size
            entry["sha256"] = digest


def update_manifest(ext: str, manifest: dict, values: dict[str, str],
                    names: set[str]) -> bool:
    changed = False
    if ext == ".pdbotprofile":
        for key in ("type_key", "difficulty_key", "target_body", "profile_file"):
            changed |= copy_string(manifest, values, key)
        return changed

    if ext == ".pdgamemode":
        for key in ("name", "description", "mode_key", "rules_file"):
            changed |= copy_string(manifest, values, key)
        for key in ("min_players", "max_players", "team_based", "requirefeature"):
            changed |= copy_int(manifest, values, key)
        return changed

    if ext == ".pdmission":
        for key in (
            "scenario",
            "scenario_archive",
            "scenario_graph_cache",
            "mission_graph_file",
            "objectives_file",
            "briefing_file",
            "category",
        ):
            changed |= copy_string(manifest, values, key)
        if "id" in manifest and "catalog_id" not in manifest:
            manifest["catalog_id"] = manifest["id"]
            changed = True
        return changed

    if ext == ".pdprojectile":
        for key in ("behavior_graph", "model_archive", "model_catalog_id", "entity_ref"):
            changed |= copy_string(manifest, values, key)
        return changed

    if ext == ".pdentity":
        for key in (
            "bindings_file",
            "behavior_graph",
            "composition_file",
            "model_archive",
            "model_catalog_id",
            "archetype",
        ):
            changed |= copy_string(manifest, values, key)
        return changed

    if ext == ".pdweapon":
        changed |= copy_string(manifest, values, "model_file")
        for key in WEAPON_REQUIRED_MEMBERS:
            changed |= copy_string(manifest, values, key)
        for key, member in WEAPON_OPTIONAL_MEMBERS.items():
            if values.get(key) or member in names or manifest.get(key):
                if manifest.get(key) != member:
                    manifest[key] = member
                    changed = True
        return changed

    return False


def upgrade_zip_bytes(data: bytes, ext: str, dry_run: bool) -> tuple[str, bytes | None]:
    descriptor = ARCHIVE_DESCRIPTORS.get(ext)
    if not descriptor:
        return "skip:unsupported", None

    with zipfile.ZipFile(io.BytesIO(data), "r") as archive:
        names = archive.namelist()
        name_set = set(names)
        if descriptor not in name_set or "_meta/manifest.json" not in name_set:
            return "skip:missing-source", None

        descriptor_text = archive.read(descriptor).decode("utf-8")
        descriptor_changed = False
        if ext == ".pdweapon":
            descriptor_text, descriptor_changed = rewrite_weapon_ini(descriptor_text)
            values_for_model = parse_ini(descriptor_text)
            model_file = values_for_model.get("model_file")
            if model_file and model_file not in name_set:
                descriptor_text, removed = remove_ini_key(descriptor_text, "model_file")
                descriptor_changed |= removed
        values = parse_ini(descriptor_text)
        manifest = json.loads(archive.read("_meta/manifest.json").decode("utf-8"))
        manifest_changed = False
        if ext == ".pdweapon" and manifest.get("model_file") not in (None, values.get("model_file")):
            manifest.pop("model_file", None)
            manifest_changed = True
        if ext == ".pdweapon" and not values.get("model_file"):
            if "model_file" in manifest:
                manifest.pop("model_file", None)
                manifest_changed = True
        manifest_changed |= update_manifest(ext, manifest, values, name_set)

        replacements: dict[str, bytes] = {}
        nested_changed = 0
        if ext == ".pdweapon":
            for name in names:
                nested_ext = Path(name).suffix.lower()
                if nested_ext not in {".pdprojectile", ".pdentity"}:
                    continue
                result, nested_bytes = upgrade_zip_bytes(
                    archive.read(name), nested_ext, dry_run
                )
                if result in {"manifest", "descriptor+manifest"}:
                    nested_changed += 1
                    if nested_bytes is not None:
                        replacements[name] = nested_bytes

        if not descriptor_changed and not manifest_changed and nested_changed == 0:
            return "skip:current", None

        descriptor_data = descriptor_text.encode("utf-8")
        descriptor_hash = sha256_hex(descriptor_data)
        manifest_data = json_bytes(manifest)
        replacements["_meta/manifest.json"] = manifest_data
        if descriptor_changed:
            replacements[descriptor] = descriptor_data
            replacements[f"_meta/{descriptor}.sha256"] = (
                descriptor_hash + "\n"
            ).encode("ascii")
            for meta_name in ("_meta/inventory.json", "_meta/hashes.json"):
                if meta_name in name_set:
                    doc = json.loads(archive.read(meta_name).decode("utf-8"))
                    update_hash_entry(doc, descriptor, len(descriptor_data), descriptor_hash)
                    replacements[meta_name] = json_bytes(doc)

        if dry_run:
            if descriptor_changed:
                return "descriptor+manifest", None
            return "manifest", None

        out_io = io.BytesIO()
        with zipfile.ZipFile(out_io, "w", compression=zipfile.ZIP_DEFLATED) as out:
            for name in names:
                out.writestr(name, replacements.get(name, archive.read(name)))
        if descriptor_changed:
            return "descriptor+manifest", out_io.getvalue()
        return "manifest", out_io.getvalue()


def upgrade_archive(path: Path, dry_run: bool) -> str:
    ext = path.suffix.lower()
    result, data = upgrade_zip_bytes(path.read_bytes(), ext, dry_run)
    if dry_run or data is None:
        return result

    fd, tmp_name = tempfile.mkstemp(
        prefix=path.name + ".", suffix=".tmp", dir=str(path.parent)
    )
    os.close(fd)
    tmp_path = Path(tmp_name)
    try:
        tmp_path.write_bytes(data)
        shutil.move(str(tmp_path), path)
    finally:
        if tmp_path.exists():
            tmp_path.unlink()
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--max-errors", type=int, default=20)
    args = parser.parse_args()

    scanned = 0
    manifest_only = 0
    descriptor_and_manifest = 0
    errors: list[str] = []

    for path in iter_inputs(args.paths):
        scanned += 1
        try:
            result = upgrade_archive(path, args.dry_run)
        except Exception as exc:  # noqa: BLE001 - CLI should report all archive failures.
            if len(errors) < args.max_errors:
                errors.append(f"{path}: {exc}")
            continue
        if result == "manifest":
            manifest_only += 1
        elif result == "descriptor+manifest":
            descriptor_and_manifest += 1

    if errors:
        print("generated manifest upgrade failed:", file=os.sys.stderr)
        for error in errors:
            print(f"  - {error}", file=os.sys.stderr)
        return 1

    mode = "dry-run " if args.dry_run else ""
    print(
        f"generated manifest upgrade {mode}ok: scanned={scanned} "
        f"manifest={manifest_only} descriptor_manifest={descriptor_and_manifest}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
