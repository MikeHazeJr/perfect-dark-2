#!/usr/bin/env python3
"""build_level_from_model.py -- an original level from a model (glTF binary).

Forge step F5 (context/designs/modding/forge-mode-scope.md): a level's walls
and floors are made in a modelling program and exported as one .glb; this
turns the file into a level the game loads, which Forge then fills with
starts, weapons and objects. Everything that does not depend on what the
level looks like comes from tools/level_archive.py, which the Blank Room
shares.

How the model is read:

- Every mesh is drawn, and its triangles are collided with, except: a mesh
  whose node (or a parent's) name begins "collision" is collided with and not
  drawn, and one whose name begins "nocollide" is drawn and not collided with.
  Triangles that face down (normal y below -0.5, ceilings) are never collided
  with, as in the base arenas; triangles that face up (normal y 0.5 or more,
  the game's floor test) are floors, the rest walls. Triangles are wound the
  glTF way: counter-clockwise seen from the front.
- Each material's base colour texture (an embedded image; PNG as is, other
  kinds converted when Pillow is installed) and base colour factor become one
  textured, tinted part of the scene. A material with no texture is drawn
  plain in its factor's colour.
- A node with no mesh whose name begins "start" is a player start: the
  node's position, set 50 units above the floor under it (base starts stand
  about that high), facing where the node's +Z axis points in glTF, which is
  where an object's -Y axis points in Blender, made level. A level needs at
  least one; 8 to 16 is usual. They are walked in order round their middle by
  the one path simulants follow.
- Units: --scale (default 100) turns the model's units into the game's
  centimetres, so a model in Blender's metres comes out life size. A player
  is 172 tall and steps up 30.
- One room: every triangle and start is in room 1, and the room's box is the
  model's. A level of several rooms needs portals, which this does not make.

Usage:
    python tools/build_level_from_model.py MODEL.glb --id ID --name "Name" --out DIR [--scale 100]

writes DIR/ID.pdarena and DIR/mod.json (ids mod_ID:arena_ID and
mod_ID:scenario_ID; ID is lower case letters, digits and underscores), checks
the level against the game's rules and hands it to the archive validator.
"""

from __future__ import annotations

import argparse
import io
import json
import math
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import level_archive as la  # noqa: E402
sys.path.pop(0)

require = la.require

COMPONENTS = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
NORMALISE = {5120: 127.0, 5121: 255.0, 5122: 32767.0, 5123: 65535.0}


# ---------------------------------------------------------------------------
# Reading the model
# ---------------------------------------------------------------------------

class Model:
    def __init__(self, data: bytes):
        require(len(data) >= 20, "not a glTF binary: too short")
        magic, version, length = struct.unpack("<4sII", data[:12])
        require(magic == b"glTF" and version == 2, "not a glTF 2.0 binary (.glb)")
        require(length <= len(data), "the file is shorter than its header says")
        json_length, json_kind = struct.unpack("<I4s", data[12:20])
        require(json_kind == b"JSON", "the first chunk is not JSON")
        self.doc = json.loads(data[20:20 + json_length].decode("utf-8"))
        at = 20 + json_length
        self.blob = b""
        if at + 8 <= length:
            bin_length, bin_kind = struct.unpack("<I4s", data[at:at + 8])
            if bin_kind == b"BIN\x00":
                self.blob = data[at + 8:at + 8 + bin_length]
        required = self.doc.get("extensionsRequired", [])
        require(not required, "the model needs extensions this does not read: %s" % ", ".join(required))
        for buffer in self.doc.get("buffers", []):
            require("uri" not in buffer, "the model's data must be inside the .glb, not beside it")

    def view_bytes(self, index: int) -> bytes:
        view = self.doc["bufferViews"][index]
        start = view.get("byteOffset", 0)
        return self.blob[start:start + view["byteLength"]]

    def accessor(self, index: int) -> list:
        """The accessor's elements as tuples of floats (normalised integers
        scaled to 0..1 or -1..1, as glTF says)."""
        acc = self.doc["accessors"][index]
        require("sparse" not in acc, "sparse accessors are not read")
        require("bufferView" in acc, "an accessor with no data")
        fmt, size = COMPONENTS[acc["componentType"]]
        width = WIDTHS[acc["type"]]
        view = self.doc["bufferViews"][acc["bufferView"]]
        stride = view.get("byteStride", 0) or size * width
        base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
        scale = NORMALISE.get(acc["componentType"]) if acc.get("normalized") else None
        out = []
        for i in range(acc["count"]):
            values = struct.unpack_from("<%d%s" % (width, fmt), self.blob, base + i * stride)
            if scale:
                values = tuple(max(-1.0, v / scale) for v in values)
            out.append(tuple(float(v) for v in values))
        return out


def matrix_of(node: dict):
    """The node's local transform as a 4x4 row-major list."""
    if "matrix" in node:
        m = node["matrix"]  # column-major in glTF
        return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
    tx, ty, tz = node.get("translation", (0.0, 0.0, 0.0))
    qx, qy, qz, qw = node.get("rotation", (0.0, 0.0, 0.0, 1.0))
    sx, sy, sz = node.get("scale", (1.0, 1.0, 1.0))
    rot = [
        [1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qz * qw), 2 * (qx * qz + qy * qw)],
        [2 * (qx * qy + qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qx * qw)],
        [2 * (qx * qz - qy * qw), 2 * (qy * qz + qx * qw), 1 - 2 * (qx * qx + qy * qy)],
    ]
    return [[rot[r][0] * sx, rot[r][1] * sy, rot[r][2] * sz, (tx, ty, tz)[r]] for r in range(3)] + [[0, 0, 0, 1]]


def multiply(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def apply(m, p):
    return tuple(m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2] + m[r][3] for r in range(3))


def apply_direction(m, d):
    return tuple(m[r][0] * d[0] + m[r][1] * d[1] + m[r][2] * d[2] for r in range(3))


def determinant3(m):
    return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
            m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))


def walk_nodes(model: Model):
    """(node, world matrix, inherited name roles) for every node of the
    model's scene, parents first."""
    doc = model.doc
    scene = doc.get("scenes", [{}])[doc.get("scene", 0)] if doc.get("scenes") else {"nodes": list(range(len(doc.get("nodes", []))))}
    identity = [[1.0 if r == c else 0.0 for c in range(4)] for r in range(4)]
    stack = [(index, identity, "") for index in reversed(scene.get("nodes", []))]
    while stack:
        index, parent, role = stack.pop()
        node = doc["nodes"][index]
        world = multiply(parent, matrix_of(node))
        name = node.get("name", "").lower()
        if name.startswith("collision"):
            role = "collision"
        elif name.startswith("nocollide"):
            role = "nocollide"
        yield node, world, role
        for child in reversed(node.get("children", [])):
            stack.append((child, world, role))


def image_png(model: Model, image_index: int, pillow_ok: bool) -> bytes:
    image = model.doc["images"][image_index]
    require("bufferView" in image, "image %d is not inside the .glb" % image_index)
    data = model.view_bytes(image["bufferView"])
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        return data
    require(pillow_ok, "image %d is not a PNG; install Pillow to convert it, or export PNG" % image_index)
    from PIL import Image  # noqa: PLC0415
    out = io.BytesIO()
    Image.open(io.BytesIO(data)).convert("RGBA").save(out, format="PNG")
    return out.getvalue()


WHITE_PNG = None


def white_png() -> bytes:
    global WHITE_PNG
    if WHITE_PNG is None:
        import zlib  # noqa: PLC0415
        rows = b"".join(b"\x00" + b"\xff\xff\xff" * 2 for _ in range(2))

        def chunk(kind: bytes, data: bytes) -> bytes:
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)

        WHITE_PNG = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
    return WHITE_PNG


def read_level(data: bytes, scale: float, pillow_ok: bool):
    """Returns (primitives, collision, starts, bounds): the scene's textured
    parts, the collision triangles with their kind, the starts as (name,
    position, facing) before they are set on the floor, and the model's box."""
    model = Model(data)
    doc = model.doc
    materials = doc.get("materials", [])
    by_material: dict = {}
    collision = []
    starts = []
    low = [math.inf] * 3
    high = [-math.inf] * 3
    for node, world, role in walk_nodes(model):
        scaled = [[world[r][c] * (scale if r < 3 else 1.0) for c in range(4)] for r in range(4)]
        if "mesh" not in node:
            if node.get("name", "").lower().startswith("start"):
                position = apply(scaled, (0.0, 0.0, 0.0))
                ahead = apply_direction(world, (0.0, 0.0, 1.0))
                starts.append((node.get("name", ""), position, (ahead[0], 0.0, ahead[2])))
            continue
        mirrored = determinant3(world) < 0
        for primitive in doc["meshes"][node["mesh"]].get("primitives", []):
            require(primitive.get("mode", 4) == 4, "mesh %s draws something other than triangles"
                    % doc["meshes"][node["mesh"]].get("name", node["mesh"]))
            attributes = primitive["attributes"]
            positions = [apply(scaled, p) for p in model.accessor(attributes["POSITION"])]
            material = primitive.get("material", -1)
            if role != "collision":
                info = materials[material] if 0 <= material < len(materials) else {}
                texture = info.get("pbrMetallicRoughness", {}).get("baseColorTexture")
                uv_set = texture.get("texCoord", 0) if texture is not None else 0
                require(type(uv_set) is int and uv_set >= 0, "a base-color texture has an invalid texCoord")
                uv_attribute = f"TEXCOORD_{uv_set}"
                # The generated scene writes the selected coordinates into TEXCOORD_0.
                require(uv_set == 0 or uv_attribute in attributes,
                        f"material {info.get('name', material)} needs {uv_attribute} on each drawn primitive")
                uvs = (model.accessor(attributes[uv_attribute]) if uv_attribute in attributes
                       else [(0.0, 0.0)] * len(positions))
            order = [int(i[0]) for i in model.accessor(primitive["indices"])] if "indices" in primitive \
                else list(range(len(positions)))
            for t in range(0, len(order) - 2, 3):
                a, b, c = order[t], order[t + 1], order[t + 2]
                if mirrored:
                    b, c = c, b
                triangle = (positions[a], positions[b], positions[c])
                normal, length = la.unit_normal(triangle)
                if length <= 1.0:
                    continue  # too small to keep
                for point in triangle:
                    for axis in range(3):
                        low[axis] = min(low[axis], point[axis])
                        high[axis] = max(high[axis], point[axis])
                if role != "collision":
                    by_material.setdefault(material, []).append((triangle, (uvs[a], uvs[b], uvs[c])))
                if role != "nocollide" and normal[1] >= -0.5:
                    collision.append((triangle, "floor" if normal[1] >= 0.5 else "wall"))

    primitives = []
    for material, triangles in sorted(by_material.items()):
        info = materials[material] if 0 <= material < len(materials) else {}
        pbr = info.get("pbrMetallicRoughness", {})
        tint = tuple(float(v) for v in pbr.get("baseColorFactor", (1.0, 1.0, 1.0, 1.0)))
        texture = pbr.get("baseColorTexture")
        png = white_png()
        if texture is not None:
            source = doc["textures"][texture["index"]].get("source")
            require(source is not None, "material %s names a texture with no image" % info.get("name", material))
            png = image_png(model, source, pillow_ok)
        name = re.sub(r"[^A-Za-z0-9_]+", "_", info.get("name", "material_%d" % max(material, 0))) or "material"
        corners = []
        for triangle, uv in triangles:
            for point, coord in zip(triangle, uv):
                corners.append((point, coord, tint))
        primitives.append(la.Primitive(material=name, image_name=name, image_png=png, corners=corners, room=1))
    require(primitives, "the model has nothing to draw")
    require(collision, "the model has nothing to stand on or bump into")
    return primitives, collision, starts, (tuple(low), tuple(high))


def floor_under(collision, x: float, y: float, z: float):
    """The highest floor at or below y + 10 straight under (x, z), or None."""
    best = None
    for triangle, kind in collision:
        if kind != "floor":
            continue
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = triangle
        det = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
        if abs(det) < 1e-9:
            continue
        u = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / det
        v = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / det
        w = 1.0 - u - v
        if min(u, v, w) < -1e-6:
            continue
        height = u * ay + v * by + w * cy
        if height <= y + 10.0 and (best is None or height > best):
            best = height
    return best


def place_starts(starts, collision):
    """Each start set 50 above the floor under it, facing level, then all in
    order round their middle (the path simulants walk)."""
    require(starts, "the model has no start: add empties named start_1, start_2 and so on")
    placed = []
    for name, (x, y, z), facing in starts:
        floor = floor_under(collision, x, y, z)
        require(floor is not None, "%s has no floor under it" % name)
        length = math.hypot(facing[0], facing[2])
        look = (round(facing[0] / length, 6), 0.0, round(facing[2] / length, 6)) if length > 1e-6 else (0.0, 0.0, 1.0)
        placed.append((name, (round(x, 3), round(floor + la.PAD_ABOVE_FLOOR, 3), round(z, 3)), look))
    mx = sum(p[1][0] for p in placed) / len(placed)
    mz = sum(p[1][2] for p in placed) / len(placed)
    placed.sort(key=lambda p: (math.atan2(p[1][2] - mz, p[1][0] - mx), p[0]))
    return [(position, look) for _name, position, look in placed]


def build(model_path: Path, level_id: str, display_name: str, scale: float, pillow_ok: bool):
    require(re.fullmatch(r"[a-z0-9_]{1,40}", level_id) is not None,
            "the id must be 1 to 40 lower case letters, digits and underscores")
    primitives, collision, starts, (low, high) = read_level(model_path.read_bytes(), scale, pillow_ok)
    places = place_starts(starts, collision)
    margin = 50.0
    room_low = tuple(v - margin for v in low)
    room_high = (high[0] + margin, max(high[1], max(p[0][1] for p in places)) + margin, high[2] + margin)
    origin = tuple((room_low[i] + room_high[i]) / 2.0 for i in range(3))
    scene = la.scene_glb(primitives, "Perfect Dark 2 level-from-model builder", level_id, level_id + "_mesh")
    level = la.Level(
        arena_id="mod_%s:arena_%s" % (level_id, level_id),
        scenario_id="mod_%s:scenario_%s" % (level_id, level_id),
        scenario_member="dependencies/assets/scenarios/scenario_%s.pdscenario" % level_id,
        display_name=display_name,
        category="Templates",
        arena_comment="%s.pdarena - %s, a level made from a model by tools/build_level_from_model.py"
                      % (level_id, display_name),
        scenario_comment="scenario_%s.pdscenario - %s, an original one-room level" % (level_id, display_name),
        collision_comment="%s collision, from %s by tools/build_level_from_model.py." % (display_name, model_path.name),
        scene=scene,
        collision=collision,
        places=places,
        room_low=room_low,
        room_high=room_high,
        room_origin=origin,
    )
    arena, members = la.build_level(level)
    mod = json.dumps({
        "id": level_id,
        "name": "%s (level template)" % display_name,
        "version": "0.1.0",
        "author": "PD2",
        "description": "An original level made from a model. Open it in Forge to place starts, weapons and objects.",
        "tags": ["arena", "pdarena", "template"],
        "contents": ["arena"],
    }, indent=2) + "\n"
    return arena, mod, members, level


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="An original level from a model (.glb).")
    parser.add_argument("model", type=Path)
    parser.add_argument("--id", required=True, help="the level's id: lower case letters, digits, underscores")
    parser.add_argument("--name", required=True, help="the name players see")
    parser.add_argument("--out", required=True, type=Path, help="the folder for ID.pdarena and mod.json")
    parser.add_argument("--scale", type=float, default=100.0, help="game units per model unit (default 100)")
    args = parser.parse_args(argv)
    try:
        import PIL  # noqa: F401,PLC0415
        pillow_ok = True
    except ImportError:
        pillow_ok = False
    try:
        arena, mod, members, level = build(args.model, args.id, args.name, args.scale, pillow_ok)
    except la.BuildError as error:
        print("LEVEL FROM MODEL: not built: %s" % error)
        return 1
    errors = la.conformance(arena, "%s.pdarena" % args.id)
    for error in errors:
        print("conformance: " + error)
    if errors:
        print("LEVEL FROM MODEL: the archive validator refuses it (%d)" % len(errors))
        return 1
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / ("%s.pdarena" % args.id)).write_bytes(arena)
    (args.out / "mod.json").write_text(mod, encoding="utf-8", newline="\n")
    floors = sum(1 for _t, kind in level.collision if kind == "floor")
    print("LEVEL FROM MODEL: built %s.pdarena (%d bytes): %d textured parts, %d collision triangles (%d floors), "
          "%d starts, room box %s to %s; sha256 %s" % (
              args.id, len(arena), len(json.loads(members["scene.glb"][20:20 + struct.unpack("<I", members["scene.glb"][12:16])[0]])["meshes"][0]["primitives"]),
              len(level.collision), floors, len(level.places), la.vector(level.room_low), la.vector(level.room_high),
              la.sha(arena)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
