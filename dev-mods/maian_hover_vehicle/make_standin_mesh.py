"""Stand-in model for the Maian hover vehicle (Maian vehicle plan, step V2).

SUPERSEDED 2026-11-16 by build_vehicle_mesh.py (the final model, from the
cleaned-up Tripo model), whose output the recipe now uses from meshes/vehicle/.
This script now writes to meshes/standin/ by default, so the two cannot
overwrite each other; the recipe does not read meshes/standin/.

Turns the geometry of Mike's October vehicle study (builder_v4.py in
.claude/pd-maian-models-20261013/vehicle/, run in a headless Blender by
blender_dump_vehicle.py, which writes a JSON dump) into the member files of
the vehicle's embedded `.pdmesh`: mesh.ini, model.gltf, model.mtl and the four
sidecars. The recipe puts them into the copy of the example mesh it adds to
the vehicle.

Regenerate:
    blender -b --factory-startup --python blender_dump_vehicle.py -- \
        <models>/vehicle/builder_v4.py <dump.json>
    python make_standin_mesh.py <dump.json> [out dir]

Why V4 and not V5: V5 has never passed its own rider-clearance check
("Short-mast source sampled head separation fails"); V4 builds cleanly.

This is a stand-in so the vehicle can be seen and driven. It is not the final
model: no named turret, seat or crystal parts yet (V5, V4 in the plan), the
turret is the study's pistol-sized one with six crystals rather than 18, and
there is no gunner cowl (V7).

Conventions:
  - Model units are the base hoverbike's: the vehicle is drawn at the
    hoverbike's model-slot scale (409/4096, about 0.1), so 1000 units are
    about 1 m in the game. The study is in metres.
  - Native model axes, as for held models: +Z forward, +Y up, +X left. The
    study is forward -X, up Z (so +Y is right).
  - The origin sits at the middle of the hull's length and 0.54 m above its
    floor, as the hoverbike's floor sits about 0.56 m below its origin, so the
    hover physics hold it at a hoverbike's height.
  - The rider study figures (objects named *_occupied_*) and the hidden
    clearance envelopes are left out: riders are players and characters.

What the in-game mesh compiler takes from glTF: positions, indices and each
primitive's baseColorFactor as a vertex colour; no normals, no lighting. So
shading is baked by face direction into a few steps per colour (at most 64
colours in all), and every triangle is written with both windings.
"""
import base64
import json
import math
import struct
import sys
from pathlib import Path

UNITS_PER_METRE = 1000.0
ORIGIN_X = -0.05     # study x of the hull's middle (it runs -1.60 to 1.50)
ORIGIN_Z = 0.54      # study z of the origin (the floor is at about -0.02)
SHADE_STEPS = 6
LIGHT = (-0.35, 0.80, 0.49)   # model space: from the left, above, ahead

LEFT_OUT = ("_occupied_",)


def to_model(p):
    bx, by, bz = p
    return (-by * UNITS_PER_METRE,
            (bz - ORIGIN_Z) * UNITS_PER_METRE,
            -(bx - ORIGIN_X) * UNITS_PER_METRE)


def collect(dump):
    """(material name, three model-space points) for every kept triangle."""
    palette = {}
    tris = []
    skipped = []
    for obj in dump["objects"]:
        if obj["hidden"] or any(mark in obj["name"] for mark in LEFT_OUT):
            skipped.append(obj["name"])
            continue
        palette.update(obj["palette"])
        for tri in obj["triangles"]:
            tris.append((tri["m"], tuple(to_model(v) for v in tri["v"])))
    return tris, palette, skipped


def shaded(tris):
    light_len = math.sqrt(sum(v * v for v in LIGHT))
    light = tuple(v / light_len for v in LIGHT)
    out = []
    for name, (a, b, c) in tris:
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


def build(dump_path, out_dir):
    dump = json.loads(Path(dump_path).read_text(encoding="utf-8"))
    tris, palette, skipped = collect(dump)
    groups = {}
    for name, step, tri in shaded(tris):
        groups.setdefault((name, step), []).append(tri)

    buffer = bytearray()
    views, accessors, materials, primitives = [], [], [], []
    mtl = ["# stand-in material sidecar (Maian hover vehicle)"]
    total = 0
    lo = [1e9] * 3
    hi = [-1e9] * 3
    radius = 0.0

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
            radius = max(radius, math.sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]))
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
        colour = shade(palette.get(name, [0.5, 0.5, 0.5]), step)
        material = "MaianHover_%s_%d" % (name, step)
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
        "asset": {"version": "2.0",
                  "generator": "make_standin_mesh.py from the October vehicle study builder_v4"},
        "buffers": [{"uri": "data:application/octet-stream;base64," +
                     base64.b64encode(bytes(buffer)).decode("ascii"),
                     "byteLength": len(buffer)}],
        "bufferViews": views,
        "accessors": accessors,
        "materials": materials,
        "meshes": [{"name": "maian_hover_standin", "primitives": primitives}],
        "nodes": [{"name": "maian_hover_standin", "mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    bounds = {"xmin": round(lo[0], 3), "xmax": round(hi[0], 3),
              "ymin": round(lo[1], 3), "ymax": round(hi[1], 3),
              "zmin": round(lo[2], 3), "zmax": round(hi[2], 3)}

    def node(node_id, parent, node_type, partnum, hitpart):
        return {"id": node_id, "parent": parent, "type": node_type, "partnum": partnum,
                "part": partnum, "mtx0": 0, "mtx1": -1, "mtx2": -1,
                "position": {"x": 0.0, "y": 0.0, "z": 0.0}, "drawdist": 0.0, "target": -1,
                "group": "-", "render_mtx": 0, "mcount": 1, "hitpart": hitpart,
                "bounds": bounds, "distance": {"near": 0.0, "far": 0.0},
                "reorder": {"pivot": {"x": 0.0, "y": 0.0, "z": 0.0},
                            "axis": {"x": 0.0, "y": 1.0, "z": 0.0},
                            "target_a": -1, "target_b": -1, "side": 0}}

    # The base hoverbike's layout: position root, bounding box, display list.
    # The hoverbike code needs the bounding box (objFindBboxRodata).
    nodes = [node(0, -1, 2, 0, 0), node(1, 0, 10, -1, 1), node(2, 1, 24, -1, 0)]
    mesh_ini = "\n".join([
        "; maian_hover.pdmesh - stand-in model for the Maian hover vehicle,",
        "; generated by make_standin_mesh.py from the October vehicle study (builder_v4).",
        "[model]",
        "catalog_id = example:tri_mesh",
        "kind = mesh",
        "model_file = model.gltf",
        "material_file = model.mtl",
        "hierarchy_file = model.nodes.json",
        "parts_file = model.parts.json",
        "faces_file = model.faces.json",
        "render_stream_file = model.render.json",
        "; Culling radius in model units: the farthest vertex from the origin.",
        "model_scale = %.3f" % radius,
        "",
    ])
    files = {
        "mesh.ini": mesh_ini,
        "model.gltf": json.dumps(gltf, indent=1) + "\n",
        "model.mtl": "\n".join(mtl) + "\n",
        "model.nodes.json": json.dumps({"schema": "pd2.mesh.nodes.v1", "nodes": nodes},
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
    print("left out:", len(skipped), "objects (riders and clearance envelopes)")
    print("triangles written (both windings):", total, "primitives:", len(primitives))
    print("bounds:", bounds)
    print("size (m): %.2f wide, %.2f tall, %.2f long" % (
        (hi[0] - lo[0]) / UNITS_PER_METRE, (hi[1] - lo[1]) / UNITS_PER_METRE,
        (hi[2] - lo[2]) / UNITS_PER_METRE))
    print("culling radius (model units): %.1f" % radius)
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    target = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).parent / "meshes" / "standin"
    sys.exit(build(sys.argv[1], target))
