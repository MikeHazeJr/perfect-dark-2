#!/usr/bin/env python3
"""Upgrade stale v3 character .pdanim archives to the current v4 source contract.

This is an offline repair path for retained generated installs. It does not
weaken validation and it does not invent runtime data: it only accepts archives
whose public animation.gltf payload still declares the old v3 character
extractor. Non-zero clips keep their authored GLTF channel data with the v4
generator stamp. Zero-frame clips are rewritten to the v4 no-op placeholder
shape that the current extractor emits.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import tempfile
import zipfile
from pathlib import Path
from typing import Iterable


OLD_GENERATOR = "Perfect Dark 2 pdanim_chr semantic extractor v3"
NEW_GENERATOR = "Perfect Dark 2 pdanim_chr semantic extractor v4"


def iter_pdanim_inputs(paths: Iterable[Path]) -> Iterable[Path]:
    for path in paths:
        if path.is_dir():
            yield from sorted(path.rglob("*.pdanim"))
        elif path.suffix.lower() == ".pdanim":
            yield path


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_bytes(obj: object) -> bytes:
    return (json.dumps(obj, indent=2) + "\n").encode("utf-8")


def replace_ini_value(text: str, key: str, value: str) -> str:
    prefix = f"{key} = "
    lines = []
    replaced = False
    for line in text.splitlines():
        if line.startswith(prefix):
            lines.append(prefix + value)
            replaced = True
        else:
            lines.append(line)
    if not replaced:
        lines.append(prefix + value)
    return "\n".join(lines) + "\n"


def build_zero_frame_placeholder(old_gltf: dict, manifest: dict) -> dict:
    extras = old_gltf.get("extras")
    if not isinstance(extras, dict):
        extras = {}
    part_count = extras.get("pd_part_count")
    if not isinstance(part_count, int) or part_count <= 0:
        nodes = old_gltf.get("nodes")
        part_count = len(nodes) if isinstance(nodes, list) and nodes else 1

    nodes = []
    old_nodes = old_gltf.get("nodes")
    for index in range(part_count):
        name = None
        if isinstance(old_nodes, list) and index < len(old_nodes):
            old_node = old_nodes[index]
            if isinstance(old_node, dict):
                old_name = old_node.get("name")
                if isinstance(old_name, str) and old_name:
                    name = old_name
        nodes.append({"name": name or f"part_{index:03d}"})

    repeat_ranges = extras.get("pd_repeat_ranges")
    if not isinstance(repeat_ranges, list):
        repeat_ranges = []
    cut_skip_frames = extras.get("pd_cut_skip_frames")
    if not isinstance(cut_skip_frames, list):
        cut_skip_frames = []

    return {
        "asset": {"version": "2.0", "generator": NEW_GENERATOR},
        "nodes": nodes,
        "animations": [
            {
                "name": str(manifest.get("id") or "character_animation"),
                "samplers": [],
                "channels": [],
            }
        ],
        "extras": {
            "pd_kind": "animation",
            "pd_category": "character_animation",
            "pd_part_count": part_count,
            "pd_channel_count": 0,
            "pd_anim_flags": int(extras.get("pd_anim_flags") or manifest.get("flags") or 0),
            "pd_zero_frame_placeholder": True,
            "pd_repeat_ranges": repeat_ranges,
            "pd_cut_skip_frames": cut_skip_frames,
        },
    }


def update_hash_entry(doc: dict, path: str, size: int, digest: str) -> None:
    entries = doc.get("entries")
    if not isinstance(entries, list):
        return
    for entry in entries:
        if isinstance(entry, dict) and entry.get("path") == path:
            entry["size"] = size
            entry["sha256"] = digest


def upgrade_archive(path: Path, dry_run: bool) -> str:
    with zipfile.ZipFile(path, "r") as archive:
        names = archive.namelist()
        if "animation.gltf" not in names:
            return "skip:not-gltf"
        gltf = json.loads(archive.read("animation.gltf").decode("utf-8"))
        asset = gltf.get("asset")
        if not isinstance(asset, dict) or asset.get("generator") != OLD_GENERATOR:
            return "skip:not-v3"
        manifest = json.loads(archive.read("_meta/manifest.json").decode("utf-8"))
        ini_text = archive.read("animation.ini").decode("utf-8")

        frame_count = manifest.get("frame_count")
        bytes_per_frame = manifest.get("bytes_per_frame")
        if frame_count == 0 or bytes_per_frame == 0:
            gltf = build_zero_frame_placeholder(gltf, manifest)
            change_kind = "zero-frame"
        else:
            gltf["asset"]["generator"] = NEW_GENERATOR
            change_kind = "restamped"

        gltf_data = json_bytes(gltf)
        gltf_hash = sha256_hex(gltf_data)
        manifest["pd_schema_version"] = 4
        manifest["animation_size"] = len(gltf_data)
        manifest_data = json_bytes(manifest)

        ini_text = replace_ini_value(ini_text, "animation_size", str(len(gltf_data)))
        ini_data = ini_text.encode("utf-8")
        ini_hash = sha256_hex(ini_data)

        inventory = json.loads(archive.read("_meta/inventory.json").decode("utf-8"))
        hashes = json.loads(archive.read("_meta/hashes.json").decode("utf-8"))
        update_hash_entry(inventory, "animation.ini", len(ini_data), ini_hash)
        update_hash_entry(inventory, "animation.gltf", len(gltf_data), gltf_hash)
        update_hash_entry(hashes, "animation.ini", len(ini_data), ini_hash)
        update_hash_entry(hashes, "animation.gltf", len(gltf_data), gltf_hash)
        inventory_data = json_bytes(inventory)
        hashes_data = json_bytes(hashes)

        if dry_run:
            return change_kind

        fd, tmp_name = tempfile.mkstemp(
            prefix=path.name + ".", suffix=".tmp", dir=str(path.parent)
        )
        os.close(fd)
        tmp_path = Path(tmp_name)
        try:
            with zipfile.ZipFile(tmp_path, "w", compression=zipfile.ZIP_DEFLATED) as out:
                for name in names:
                    if name == "animation.ini":
                        data = ini_data
                    elif name == "_meta/manifest.json":
                        data = manifest_data
                    elif name == "animation.gltf":
                        data = gltf_data
                    elif name == "_meta/animation.ini.sha256":
                        data = (ini_hash + "\n").encode("ascii")
                    elif name == "_meta/animation.gltf.sha256":
                        data = (gltf_hash + "\n").encode("ascii")
                    elif name == "_meta/inventory.json":
                        data = inventory_data
                    elif name == "_meta/hashes.json":
                        data = hashes_data
                    else:
                        data = archive.read(name)
                    out.writestr(name, data)
            shutil.move(str(tmp_path), path)
        finally:
            if tmp_path.exists():
                tmp_path.unlink()
        return change_kind


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--max-errors", type=int, default=20)
    args = parser.parse_args()

    scanned = 0
    upgraded = 0
    zero_frame = 0
    errors: list[str] = []

    for path in iter_pdanim_inputs(args.paths):
        scanned += 1
        try:
            result = upgrade_archive(path, args.dry_run)
        except Exception as exc:  # noqa: BLE001 - CLI should report all archive failures.
            if len(errors) < args.max_errors:
                errors.append(f"{path}: {exc}")
            continue
        if result == "zero-frame":
            upgraded += 1
            zero_frame += 1
        elif result == "restamped":
            upgraded += 1

    if errors:
        print("pdanim v3 upgrade failed:", file=os.sys.stderr)
        for error in errors:
            print(f"  - {error}", file=os.sys.stderr)
        return 1

    mode = "dry-run " if args.dry_run else ""
    print(
        f"pdanim v3 upgrade {mode}ok: scanned={scanned} "
        f"upgraded={upgraded} zero_frame={zero_frame}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
