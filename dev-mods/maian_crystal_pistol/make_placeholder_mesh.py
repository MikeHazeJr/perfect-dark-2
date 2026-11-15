"""Stand-in held mesh for the Maian crystal pistol.

Turns the geometry of the October artist source (builder_v6.py, a script that
only needs `math` for its geometry functions) into the member files of the
weapon's `.pdmesh`: model.gltf plus the four sidecars and model.mtl. The
recipe puts them into the template's mesh archive as overrides.

This is a stand-in so the weapon can be seen in the game. It is not the final
model: no named socket parts, no per-crystal states, uncalibrated size. The
real model replaces these files when it arrives (handoff note, section 7).

Usage:
    python make_placeholder_mesh.py <path to builder_v6.py> [out dir]

The builder is read as text and executed in memory; nothing is written next
to it. The default out dir is meshes/weapon beside this script.

What the in-game mesh compiler takes from glTF: positions, indices and each
primitive's baseColorFactor (as a vertex colour). No normals, no lighting. So
shading is baked: every palette colour is split into a few brightness steps
by face direction, one primitive per (colour, step), at most 64 colours in
all. Each triangle is written with both windings, so nothing depends on
which way a face was wound.
"""
import base64
import json
import math
import struct
import sys
from pathlib import Path

SCALE = 470.0        # artist units to model units: about 280 long, the Phoenix's size
SHADE_STEPS = 6
LIGHT = (-0.35, 0.80, -0.49)  # in model space: from the gun's right, above, behind


def load_builder(path):
    namespace = {"__name__": "builder_v6_in_memory"}
    exec(compile(Path(path).read_text(encoding="utf-8"), "builder_v6", "exec"), namespace)
    return namespace


def about_y(points, axis):
    """Rotate so that +Z maps to `axis` (unit, y == 0)."""
    s, c = axis[0], axis[2]
    return [(x * c + z * s, y, -x * s + z * c) for x, y, z in points]


def moved(points, by):
    return [(x + by[0], y + by[1], z + by[2]) for x, y, z in points]


def to_model(p):
    """Artist space (muzzle toward -X, up +Z) to native held-model space.

    Native guns are authored muzzle toward +Z, up +Y, +X the gun's left: the
    Phoenix has its muzzle part at z +161 and its hold part at z -90, and
    bondgun.c turns every held model 180 degrees about Y. Mod-held models
    get the same pose (crystal pistol asset README, "Scale and frame").
    At this scale the grip lands near z -90 and the emitter near z +161.
    """
    return (-p[1] * SCALE, p[2] * SCALE, -p[0] * SCALE)


def collect(b):
    parts = []   # (points, faces, material index per face, material names)

    def add(geometry, names, place=None):
        points, faces, indices = geometry[0], geometry[1], geometry[2]
        if place:
            points = place(points)
        parts.append((points, faces, indices, names))

    add(b["casing_geometry"](), ["teal", "navy"])
    for slot in b["slots"]():
        add(b["crystal_geometry"](slot["length"], slot["radius_bound"]),
            ["mint", "bright", "shade"],
            lambda pts, s=slot: moved(about_y(pts, s["axis"]), s["base_pivot"]))
    add(b["crystal_geometry"](.046, .034, 4), ["mint", "bright", "shade"],
        lambda pts: moved(about_y(pts, (-1.0, 0.0, 0.0)), (-.297, 0.0, .110)))
    for side in (-1, 1):
        add(b["lens_geometry"](side), ["blue"])
    add(b["grip_geometry"](), ["teal", "navy"])
    return parts


def triangles(parts, palette):
    """(colour name, shade step, three model-space points) for every triangle."""
    out = []
    light_len = math.sqrt(sum(v * v for v in LIGHT))
    light = tuple(v / light_len for v in LIGHT)
    for points, faces, indices, names in parts:
        model = [to_model(p) for p in points]
        for face, material in zip(faces, indices):
            name = names[material]
            assert name in palette, name
            for k in range(1, len(face) - 1):
                a, b, c = model[face[0]], model[face[k]], model[face[k + 1]]
                ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
                vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
                nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
                length = math.sqrt(nx * nx + ny * ny + nz * nz)
                if length < 1e-9:
                    continue
                facing = abs((nx * light[0] + ny * light[1] + nz * light[2]) / length)
                step = min(SHADE_STEPS - 1, int(facing * SHADE_STEPS))
                out.append((name, step, (a, b, c)))
    return out


def shade(rgb, step):
    k = 0.45 + 0.55 * (step + 0.5) / SHADE_STEPS
    return [round(min(1.0, channel * k * 1.6), 4) for channel in rgb]


def build(builder_path, out_dir):
    b = load_builder(builder_path)
    palette = {name: value[0] for name, value in b["PALETTE"].items()}
    tris = triangles(collect(b), palette)
    groups = {}
    for name, step, tri in tris:
        groups.setdefault((name, step), []).append(tri)

    buffer = bytearray()
    views, accessors, materials, primitives, mtl = [], [], [], [], ["# stand-in material sidecar"]
    total = 0
    lo = [1e9] * 3
    hi = [-1e9] * 3

    def view(data, target):
        while len(buffer) % 4:
            buffer.append(0)
        views.append({"buffer": 0, "byteOffset": len(buffer), "byteLength": len(data),
                      "target": target})
        buffer.extend(data)
        return len(views) - 1

    for (name, step), group in sorted(groups.items()):
        points = [p for tri in group for p in tri]
        for p in points:
            for i in range(3):
                lo[i] = min(lo[i], p[i])
                hi[i] = max(hi[i], p[i])
        indices = []
        for i in range(len(group)):
            indices += [3 * i, 3 * i + 1, 3 * i + 2, 3 * i, 3 * i + 2, 3 * i + 1]
        assert len(points) < 65536
        pos = view(b"".join(struct.pack("<3f", *p) for p in points), 34962)
        uv = view(b"".join(struct.pack("<2f", 0.0, 0.0) for _ in points), 34962)
        idx = view(b"".join(struct.pack("<H", i) for i in indices), 34963)
        base = len(accessors)
        accessors.append({"bufferView": pos, "componentType": 5126, "count": len(points),
                          "type": "VEC3",
                          "min": [min(p[i] for p in points) for i in range(3)],
                          "max": [max(p[i] for p in points) for i in range(3)]})
        accessors.append({"bufferView": uv, "componentType": 5126, "count": len(points),
                          "type": "VEC2"})
        accessors.append({"bufferView": idx, "componentType": 5123, "count": len(indices),
                          "type": "SCALAR"})
        colour = shade(palette[name], step)
        material = "CrystalPistol_%s_%d" % (name, step)
        materials.append({"name": material, "doubleSided": True,
                          "pbrMetallicRoughness": {"baseColorFactor": colour + [1.0],
                                                   "metallicFactor": 0.1,
                                                   "roughnessFactor": 0.5}})
        mtl += ["newmtl " + material, "Kd %.4f %.4f %.4f" % tuple(colour), "d 1.0"]
        primitives.append({"attributes": {"POSITION": base, "TEXCOORD_0": base + 1},
                           "indices": base + 2, "material": len(materials) - 1, "mode": 4})
        total += len(indices) // 3

    assert len(materials) <= 64, len(materials)
    assert total * 3 <= 32767, total
    gltf = {
        "asset": {"version": "2.0", "generator": "make_placeholder_mesh.py from builder_v6"},
        "buffers": [{"uri": "data:application/octet-stream;base64," +
                     base64.b64encode(bytes(buffer)).decode("ascii"),
                     "byteLength": len(buffer)}],
        "bufferViews": views,
        "accessors": accessors,
        "materials": materials,
        "meshes": [{"name": "maian_crystal_pistol_standin", "primitives": primitives}],
        "nodes": [{"name": "maian_crystal_pistol_standin", "mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    bounds = {"xmin": round(lo[0], 3), "xmax": round(hi[0], 3),
              "ymin": round(lo[1], 3), "ymax": round(hi[1], 3),
              "zmin": round(lo[2], 3), "zmax": round(hi[2], 3)}

    def node(node_id, parent, node_type, partnum):
        return {"id": node_id, "parent": parent, "type": node_type, "partnum": partnum,
                "part": partnum, "mtx0": 0, "mtx1": -1, "mtx2": -1,
                "position": {"x": 0.0, "y": 0.0, "z": 0.0}, "drawdist": 0.0, "target": -1,
                "group": "-", "render_mtx": 0, "mcount": 1, "hitpart": 0, "bounds": bounds,
                "distance": {"near": 0.0, "far": 0.0},
                "reorder": {"pivot": {"x": 0.0, "y": 0.0, "z": 0.0},
                            "axis": {"x": 0.0, "y": 1.0, "z": 0.0},
                            "target_a": -1, "target_b": -1, "side": 0}}

    files = {
        "model.gltf": json.dumps(gltf, indent=1) + "\n",
        "model.mtl": "\n".join(mtl) + "\n",
        "model.nodes.json": json.dumps({"schema": "pd2.mesh.nodes.v1",
                                        "nodes": [node(0, -1, 2, 0), node(1, 0, 24, -1)]},
                                       indent=2) + "\n",
        "model.parts.json": json.dumps({"schema": "pd2.mesh.parts.v1",
                                        "parts": [{"partnum": 0, "node": 0}]}, indent=2) + "\n",
        "model.faces.json": json.dumps({"schema": "pd2.mesh.faces.v1",
                                        "faces": [{"face_index": i, "matrix_index": 0}
                                                  for i in range(total)]}) + "\n",
        "model.render.json": json.dumps({"schema": "pd2.mesh.render.v1",
                                         "pd_kind": "mesh_render_commands",
                                         "pd_schema_version": 1,
                                         "commands": [{"group": "-", "command": "tri",
                                                       "face_index": i, "matrix_index": 0}
                                                      for i in range(total)]}) + "\n",
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, text in files.items():
        (out_dir / name).write_bytes(text.encode("ascii"))
    print("triangles written (both windings):", total, "primitives:", len(primitives))
    print("bounds:", bounds)
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    target = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).parent / "meshes" / "weapon"
    sys.exit(build(sys.argv[1], target))
