#!/usr/bin/env python3
"""Verify the all-family typed .pdxxx modder workflow.

This is an offline s110 gate. It proves the checked-in modder example folder can
be used as authored source, wrapped as .pdmod transport, unpacked again without
rewriting content archives, and inspected as editable source.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import tempfile
import zipfile
from dataclasses import dataclass, field
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE_ROOT = REPO_ROOT / "examples" / "modding" / "typed-pdxxx-basic"
ZIP_TIME = (1980, 1, 1, 0, 0, 0)

TYPED_DESCRIPTORS = {
    ".pdanim": "animation.ini",
    ".pdarena": "arena.ini",
    ".pdbody": "body.ini",
    ".pdbotprofile": "botprofile.ini",
    ".pdcharacter": "character.ini",
    ".pdeffect": "effect.ini",
    ".pdentity": "entity.ini",
    ".pdfont": "font.ini",
    ".pdgamemode": "gamemode.ini",
    ".pdhead": "head.ini",
    ".pdhud": "hud.ini",
    ".pdlang": "lang.ini",
    ".pdmaterial": "material.ini",
    ".pdmesh": "mesh.ini",
    ".pdmission": "mission.ini",
    ".pdprojectile": "projectile.ini",
    ".pdprop": "prop.ini",
    ".pdscenario": "scenario.ini",
    ".pdskin": "skin.ini",
    ".pdsfx": "sound.ini",
    ".pdsong": "music.ini",
    ".pdtexture": "texture.ini",
    ".pdtheme": "theme.ini",
    ".pdui": "ui.ini",
    ".pdvehicle": "vehicle.ini",
    ".pdvoice": "voice.ini",
    ".pdweapon": "weapon.ini",
}

STANDARD_SOURCE_SUFFIXES = {
    ".gltf",
    ".glb",
    ".obj",
    ".mtl",
    ".png",
    ".tga",
    ".jpg",
    ".jpeg",
    ".wav",
    ".ogg",
    ".mp3",
    ".mid",
    ".midi",
    ".ttf",
    ".otf",
}

SEMANTIC_SOURCE_SUFFIXES = {".ini", ".json", ".graph"}
FORBIDDEN_PUBLIC_SUFFIXES = {".bin", ".tsv"}


@dataclass
class WorkflowStats:
    archives: int = 0
    nested_archives: int = 0
    public_source_entries: int = 0
    semantic_source_entries: int = 0
    standard_source_entries: int = 0
    families: set[str] = field(default_factory=set)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def relpath(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def typed_archives(root: Path) -> list[Path]:
    archives = [
        p for p in root.rglob("*")
        if p.is_file() and p.suffix.lower() in TYPED_DESCRIPTORS
    ]
    return sorted(archives)


def add_file_to_zip(zf: zipfile.ZipFile, source: Path, arcname: str) -> None:
    info = zipfile.ZipInfo(arcname, ZIP_TIME)
    info.compress_type = zipfile.ZIP_DEFLATED
    zf.writestr(info, source.read_bytes())


def package_pdmod(source_root: Path, output: Path) -> None:
    if output.exists():
        output.unlink()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(p for p in source_root.rglob("*") if p.is_file()):
            add_file_to_zip(zf, path, relpath(path, source_root))


def extract_pdmod(pdmod: Path, output_root: Path) -> None:
    if output_root.exists():
        shutil.rmtree(output_root)
    output_root.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(pdmod, "r") as zf:
        for info in zf.infolist():
            if info.is_dir():
                continue
            target = output_root / info.filename
            target_full = target.resolve()
            root_full = output_root.resolve()
            if root_full not in target_full.parents and target_full != root_full:
                raise ValueError(f"unsafe .pdmod entry path: {info.filename}")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(zf.read(info.filename))


def validate_archive(path: Path, stats: WorkflowStats, errors: list[str],
                     prefix: str = "") -> None:
    ext = path.suffix.lower()
    if ext not in TYPED_DESCRIPTORS:
        errors.append(f"{prefix}{path}: unknown typed archive extension")
        return
    stats.archives += 1
    stats.families.add(ext)

    try:
        with zipfile.ZipFile(path, "r") as zf:
            names = [n for n in zf.namelist() if n and not n.endswith("/")]
            name_set = set(names)
            descriptor = TYPED_DESCRIPTORS[ext]
            if descriptor not in name_set:
                errors.append(f"{prefix}{path}: missing descriptor {descriptor}")
            if "_meta/manifest.json" not in name_set:
                errors.append(f"{prefix}{path}: missing _meta/manifest.json")

            public_sources = []
            for name in names:
                lower = name.lower()
                if lower.startswith("_meta/"):
                    continue
                suffix = Path(lower).suffix
                if suffix in FORBIDDEN_PUBLIC_SUFFIXES:
                    errors.append(f"{prefix}{path}: forbidden public source entry {name}")
                if suffix in STANDARD_SOURCE_SUFFIXES or suffix in SEMANTIC_SOURCE_SUFFIXES:
                    public_sources.append(name)
                    stats.public_source_entries += 1
                    if suffix in STANDARD_SOURCE_SUFFIXES:
                        stats.standard_source_entries += 1
                    if suffix in SEMANTIC_SOURCE_SUFFIXES:
                        stats.semantic_source_entries += 1

                entry_ext = Path(lower).suffix
                if entry_ext in TYPED_DESCRIPTORS:
                    stats.nested_archives += 1
                    nested_tmp = tempfile.NamedTemporaryFile(delete=False,
                                                            suffix=entry_ext)
                    nested_path = Path(nested_tmp.name)
                    try:
                        nested_tmp.write(zf.read(name))
                        nested_tmp.close()
                        validate_archive(nested_path, stats, errors,
                                         prefix=f"{prefix}{path.name}::{name}: ")
                    finally:
                        try:
                            nested_tmp.close()
                        except Exception:
                            pass
                        nested_path.unlink(missing_ok=True)

            if not public_sources:
                errors.append(f"{prefix}{path}: no editable public source entries")
    except zipfile.BadZipFile:
        errors.append(f"{prefix}{path}: not a zip-openable typed archive")


def validate_manifest(source_root: Path, errors: list[str]) -> None:
    manifest_path = source_root / "mod.json"
    if not manifest_path.exists():
        errors.append(f"{source_root}: missing mod.json")
        return
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"{manifest_path}: unreadable JSON: {exc}")
        return
    for field_name in ("id", "name", "version"):
        if not str(manifest.get(field_name, "")).strip():
            errors.append(f"{manifest_path}: missing non-empty {field_name}")


def verify_roundtrip(source_root: Path, work_root: Path,
                     errors: list[str]) -> tuple[Path, Path]:
    pdmod = work_root / (source_root.name + ".pdmod")
    unpacked = work_root / (source_root.name + "-unpacked")
    package_pdmod(source_root, pdmod)
    extract_pdmod(pdmod, unpacked)

    source_hashes = {
        relpath(path, source_root): sha256_file(path)
        for path in typed_archives(source_root)
    }
    roundtrip_hashes = {
        relpath(path, unpacked): sha256_file(path)
        for path in typed_archives(unpacked)
    }
    if source_hashes != roundtrip_hashes:
        source_keys = set(source_hashes)
        roundtrip_keys = set(roundtrip_hashes)
        for missing in sorted(source_keys - roundtrip_keys):
            errors.append(f"roundtrip missing archive {missing}")
        for extra in sorted(roundtrip_keys - source_keys):
            errors.append(f"roundtrip unexpected archive {extra}")
        for key in sorted(source_keys & roundtrip_keys):
            if source_hashes[key] != roundtrip_hashes[key]:
                errors.append(f"roundtrip changed archive bytes for {key}")

    src_manifest = source_root / "mod.json"
    dst_manifest = unpacked / "mod.json"
    if not dst_manifest.exists():
        errors.append("roundtrip missing mod.json")
    elif sha256_file(src_manifest) != sha256_file(dst_manifest):
        errors.append("roundtrip changed mod.json bytes")

    return pdmod, unpacked


def validate_workflow(source_root: Path, keep_output: Path | None = None) -> int:
    source_root = source_root.resolve()
    errors: list[str] = []
    stats = WorkflowStats()

    validate_manifest(source_root, errors)
    archives = typed_archives(source_root)
    if not archives:
        errors.append(f"{source_root}: no typed .pdxxx archives found")

    for archive in archives:
        validate_archive(archive, stats, errors)

    missing_families = set(TYPED_DESCRIPTORS) - stats.families
    if missing_families:
        errors.append("missing typed family examples: " + ", ".join(sorted(missing_families)))

    if stats.standard_source_entries == 0:
        errors.append("no standard DCC/media source entries found")
    if stats.semantic_source_entries == 0:
        errors.append("no semantic INI/JSON source entries found")

    if keep_output:
        keep_output.mkdir(parents=True, exist_ok=True)
        work_root = keep_output.resolve()
        clean_work = False
    else:
        work_root = Path(tempfile.mkdtemp(prefix="pdxxx-workflow-"))
        clean_work = True

    try:
        pdmod, unpacked = verify_roundtrip(source_root, work_root, errors)
        roundtrip_stats = WorkflowStats()
        for archive in typed_archives(unpacked):
            validate_archive(archive, roundtrip_stats, errors, prefix="roundtrip: ")
        if roundtrip_stats.families != stats.families:
            errors.append("roundtrip family set changed")
        if errors:
            for error in errors[:80]:
                print(f"ERROR: {error}", file=sys.stderr)
            if len(errors) > 80:
                print(f"ERROR: ... {len(errors) - 80} more error(s)", file=sys.stderr)
            return 1

        print(
            "pdxxx modder workflow ok: "
            f"families={len(stats.families)} archives={stats.archives} "
            f"nested={stats.nested_archives} public_sources={stats.public_source_entries} "
            f"standard_sources={stats.standard_source_entries} "
            f"semantic_sources={stats.semantic_source_entries} "
            f"pdmod={pdmod.name} roundtrip={unpacked.name}"
        )
        return 0
    finally:
        if clean_work:
            shutil.rmtree(work_root, ignore_errors=True)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=DEFAULT_SOURCE_ROOT,
                        help="modder-authored folder containing mod.json and typed .pdxxx archives")
    parser.add_argument("--keep-output", type=Path, default=None,
                        help="optional directory for the generated .pdmod and unpacked roundtrip")
    args = parser.parse_args(argv)
    return validate_workflow(args.source_root, args.keep_output)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
