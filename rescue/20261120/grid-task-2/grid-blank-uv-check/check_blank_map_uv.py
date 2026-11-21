#!/usr/bin/env python3
"""Inspect the stock Blank Map source without extracting or changing it."""

from __future__ import annotations

import argparse
import configparser
import hashlib
import io
import json
import math
import struct
import sys
import zipfile
from pathlib import Path


def member(archive, name):
    if archive.namelist().count(name) != 1:
        raise ValueError(f"expected one exact archive member {name!r}")
    return archive.read(name)


def descriptor(archive, name, section):
    ini = configparser.ConfigParser(interpolation=None)
    ini.read_string(member(archive, name).decode("utf-8"), source=name)
    return ini[section]


def ranges(values, width):
    if not values or any(len(row) != width or not all(map(math.isfinite, row)) for row in values):
        raise ValueError(f"expected finite {width}-component accessor data")
    return [[min(axis), max(axis)] for axis in zip(*values)]


def inspect_floor(reader, scene_bytes):
    scene = reader.Model(scene_bytes)
    doc = scene.doc
    wraps = {10497: "repeat", 33071: "clamp_to_edge", 33648: "mirrored_repeat"}
    floors = []
    for node, world, _ in reader.walk_nodes(scene):
        if "mesh" not in node:
            continue
        for index, primitive in enumerate(doc["meshes"][node["mesh"]]["primitives"]):
            if primitive.get("mode", 4) != 4:
                continue
            attrs = primitive["attributes"]
            points = [reader.apply(world, p) for p in scene.accessor(attrs["POSITION"])]
            if "indices" in primitive:
                order = [int(i[0]) for i in scene.accessor(primitive["indices"])]
            else:
                order = range(len(points))
            if len(order) < 3 or len(order) % 3 or min(order) < 0 or max(order) >= len(points):
                raise ValueError("scene indices do not form complete triangles")
            used = set(order)
            x, y, z = ranges([points[i] for i in used], 3)
            if y != [0.0, 0.0] or x[0] >= x[1] or z[0] >= z[1]:
                continue
            material = doc["materials"][primitive["material"]]
            binding = material["pbrMetallicRoughness"]["baseColorTexture"]
            texture = doc["textures"][binding["index"]]
            sampler = doc["samplers"][texture["sampler"]] if "sampler" in texture else {}
            uv0 = scene.accessor(attrs["TEXCOORD_0"])
            uv1 = scene.accessor(attrs["TEXCOORD_1"])
            if len(uv0) != len(points) or len(uv1) != len(points):
                raise ValueError("floor UV channels do not match the vertex count")
            floors.append({
                "mesh": node["mesh"], "primitive": index, "material": material["name"],
                "image": doc["images"][texture["source"]]["name"],
                "vertices": len(used), "triangles": len(order) // 3,
                "world_x": x, "world_y": y, "world_z": z,
                "material_texcoord": binding.get("texCoord", 0),
                "wrap_s": wraps[sampler.get("wrapS", 10497)],
                "wrap_t": wraps[sampler.get("wrapT", 10497)],
                "uv0": ranges([uv0[i] for i in used], 2),
                "uv1": ranges([uv1[i] for i in used], 2),
            })
    if len(floors) != 1:
        raise ValueError(f"expected one flat Blank Map floor group, found {len(floors)}")
    return floors[0]


def inspect(reader, arena_path):
    arena_bytes = arena_path.read_bytes()
    with zipfile.ZipFile(io.BytesIO(arena_bytes)) as arena:
        ini = descriptor(arena, "arena.ini", "arena")
        if (ini["catalog_id"], ini["scenario"]) != ("base:arena_test_dest", "base:scenario_test_dest"):
            raise ValueError("arena is not the stock Blank Map stage")
        scenario_name = ini["scenario_archive"]
        scenario_bytes = member(arena, scenario_name)
    with zipfile.ZipFile(io.BytesIO(scenario_bytes)) as scenario:
        source = descriptor(scenario, "scenario.ini", "scenario")
        if source["catalog_id"] != ini["scenario"] or source["runtime_source_file"] != source["scene_file"]:
            raise ValueError("the embedded scenario does not bind the advertised scene source")
        scene_name = source["runtime_source_file"]
        scene_bytes = member(scenario, scene_name)
    return {
        "arena": str(arena_path), "arena_id": ini["catalog_id"],
        "arena_sha256": hashlib.sha256(arena_bytes).hexdigest(),
        "scenario_member": scenario_name, "scenario_id": source["catalog_id"],
        "scenario_sha256": hashlib.sha256(scenario_bytes).hexdigest(),
        "scene_member": scene_name, "scene_sha256": hashlib.sha256(scene_bytes).hexdigest(),
        "floor": inspect_floor(reader, scene_bytes),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True, help="checkout supplying tools/build_level_from_model.py")
    parser.add_argument("--expect-stretched", action="store_true", help="verify the retained source baseline")
    parser.add_argument("arena", type=Path, help="stock base_arena_test_dest.pdarena")
    args = parser.parse_args()
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(args.repo.resolve() / "tools"))
    import build_level_from_model as reader

    try:
        result = inspect(reader, args.arena)
        print(json.dumps(result, indent=2))
        if args.expect_stretched:
            floor = result["floor"]
            if (floor["material_texcoord"] != 0 or floor["wrap_s"] != "clamp_to_edge"
                    or floor["wrap_t"] != "clamp_to_edge" or floor["uv0"] != [[0.0, 1.0], [0.0, 1.0]]
                    or any(hi - lo < 9.0 for lo, hi in floor["uv1"])):
                raise ValueError("stock floor no longer matches the stretched source baseline")
    except (ValueError, KeyError, IndexError, OSError, struct.error, configparser.Error,
            zipfile.BadZipFile, reader.la.BuildError) as exc:
        print(f"Blank Map UV check: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
