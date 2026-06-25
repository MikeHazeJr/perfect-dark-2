#!/usr/bin/env python3
"""Validate scenario scene.glb DCC texture-scale contract.

This is intentionally narrower than full typed-archive conformance: it checks
the exact GLB a user opens in Blender/3DS Max, either as a raw .glb file or as
the scene.glb member inside a .pdscenario archive.
"""

from __future__ import annotations

import argparse
import io
import sys
import zipfile
from dataclasses import dataclass
from collections.abc import Iterable
from pathlib import Path

from asset_archive_conformance import (
    _glb_json_and_bin,
    validate_scene_glb_texture_contract,
)


CURRENT_SCENARIO_GLB_STAMP = (
    "bg_visual_scene_glb_v12_dccuv_rsptexscale_texshift_samplerwrap_"
    "untextured_uvbound_color0_alphamask_materialextras_dualtex_alphablend"
)


@dataclass
class SceneMaterialStats:
    checked: int = 0
    current_v11: int = 0
    materials: int = 0
    materials_with_pd2: int = 0
    complete_pd2_materials: int = 0
    secondary_textures: int = 0


def update_scene_material_stats(
    label: str,
    data: bytes,
    stats: SceneMaterialStats,
    errors: list[str],
) -> None:
    try:
        gltf_json, _bin_chunk = _glb_json_and_bin(data)
    except Exception as exc:  # validated below, only avoid losing summary counts
        errors.append(f"{label} scene.glb could not be counted: {exc}")
        return

    asset = gltf_json.get("asset")
    generator = asset.get("generator", "") if isinstance(asset, dict) else ""
    if isinstance(generator, str) and CURRENT_SCENARIO_GLB_STAMP in generator:
        stats.current_v11 += 1

    materials = gltf_json.get("materials", [])
    if not isinstance(materials, list):
        return

    for material in materials:
        if not isinstance(material, dict):
            continue
        stats.materials += 1
        extras = material.get("extras", {})
        pd2_material = (
            extras.get("pd2_material") if isinstance(extras, dict) else None
        )
        if not isinstance(pd2_material, dict):
            continue
        stats.materials_with_pd2 += 1
        complete_keys = {
            "texture_command",
            "primary_image",
            "secondary_image",
            "wrap_s",
            "wrap_t",
            "offset",
            "shift_s",
            "shift_t",
            "min_lod",
            "tile_flag",
        }
        if all(key in pd2_material for key in complete_keys):
            stats.complete_pd2_materials += 1
        if isinstance(pd2_material.get("secondaryTexture"), dict):
            stats.secondary_textures += 1


def read_scene_glbs_from_archive(path: Path, label: str) -> list[tuple[str, bytes]]:
    results: list[tuple[str, bytes]] = []
    suffix = path.suffix.lower()
    with zipfile.ZipFile(path) as archive:
        names = sorted(archive.namelist())
        if suffix == ".pdscenario":
            results.append((f"{label}::scene.glb", archive.read("scene.glb")))
        for name in names:
            if Path(name).suffix.lower() != ".pdscenario":
                continue
            nested = archive.read(name)
            with zipfile.ZipFile(io.BytesIO(nested)) as nested_archive:
                results.append((
                    f"{label}::{name}::scene.glb",
                    nested_archive.read("scene.glb"),
                ))
    return results


def read_scene_glbs(path: Path) -> list[tuple[str, bytes]]:
    suffix = path.suffix.lower()
    if suffix == ".glb":
        return [(path.as_posix(), path.read_bytes())]
    if suffix in {".pdscenario", ".pdarena"}:
        return read_scene_glbs_from_archive(path, path.as_posix())
    raise ValueError(f"{path} is not a .glb, .pdscenario, or .pdarena file")


def iter_scene_inputs(paths: Iterable[Path]) -> Iterable[Path]:
    for path in paths:
        if path.is_dir():
            for child in sorted(path.rglob("*")):
                suffix = child.suffix.lower()
                if child.is_file() and (
                    child.name == "scene.glb"
                    or suffix in {".pdscenario", ".pdarena"}
                ):
                    yield child
            continue
        yield path


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "paths",
        nargs="+",
        type=Path,
        help=(
            "scene.glb files, .pdscenario/.pdarena archives, or directories "
            "containing them."
        ),
    )
    parser.add_argument(
        "--require-secondary",
        action="store_true",
        help="Fail if no material contains a pd2_material.secondaryTexture binding.",
    )
    args = parser.parse_args(argv)

    errors: list[str] = []
    stats = SceneMaterialStats()

    for path in iter_scene_inputs(args.paths):
        try:
            glbs = read_scene_glbs(path)
        except (OSError, KeyError, zipfile.BadZipFile, ValueError) as exc:
            errors.append(f"{path}: {exc}")
            continue

        for label, data in glbs:
            validate_scene_glb_texture_contract(label, data, errors)
            update_scene_material_stats(label, data, stats, errors)
            stats.checked += 1

    if not stats.checked and not errors:
        errors.append("no scene.glb files or .pdscenario archives were found")
    if args.require_secondary and stats.secondary_textures == 0:
        errors.append("no pd2_material.secondaryTexture bindings were found")

    if errors:
        print("scene.glb texture contract failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print(
        "scene.glb texture contract ok: "
        f"checked={stats.checked} "
        f"current_v11={stats.current_v11} "
        f"materials={stats.materials} "
        f"pd2_materials={stats.materials_with_pd2} "
        f"complete_pd2_materials={stats.complete_pd2_materials} "
        f"secondary_textures={stats.secondary_textures}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
