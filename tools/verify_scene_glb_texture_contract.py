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
from collections.abc import Iterable
from pathlib import Path

from asset_archive_conformance import validate_scene_glb_texture_contract


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
    args = parser.parse_args(argv)

    errors: list[str] = []
    checked = 0

    for path in iter_scene_inputs(args.paths):
        try:
            glbs = read_scene_glbs(path)
        except (OSError, KeyError, zipfile.BadZipFile, ValueError) as exc:
            errors.append(f"{path}: {exc}")
            continue

        for label, data in glbs:
            validate_scene_glb_texture_contract(label, data, errors)
            checked += 1

    if not checked and not errors:
        errors.append("no scene.glb files or .pdscenario archives were found")

    if errors:
        print("scene.glb texture contract failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print(f"scene.glb texture contract ok: checked={checked}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
