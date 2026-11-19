"""The Maian hover vehicle's in-game model (Maian vehicle plan, step V2, final model).

Turns the parts of the cleaned-up Tripo model (made with the pd-game-model skill; source,
stages and renders in D:\\Game Dev\\PD2-Assets\\maian-hover-vehicle\\, exported by its
scripts/export_pd_parts.py) into the member files of the vehicle's embedded `.pdmesh`:
mesh.ini, model.obj, model.mtl and the four sidecars. The recipe puts them into the copy
of the example mesh it adds to the vehicle and removes the example's model.gltf; the mod
author moves the mesh manifest's model_file and geometry to model.obj with the descriptor
(a recipe cannot replace a manifest).

Regenerate:
    blender -b --factory-startup <asset>/source/stageE_vcol.blend \
        --python <asset>/scripts/export_pd_parts.py -- <parts.json> <crystal piece .glb>
    python build_vehicle_mesh.py <parts.json> [out dir] [--preview <preview.glb>]

Conventions:
  - Model units are the base hoverbike's (1000 per metre; the vehicle is drawn at the
    hoverbike's model-slot scale, 409/4096). The vehicle is 3.10 m long, as the stand-in.
  - Native model axes: +Z forward, +Y up, +X left. The export is Blender's (nose -Y, up
    +Z, left +X), so (x, y, z)_model = s * (x, z - z0, -(y - y0))_Blender.
  - The origin sits mid-length and 0.54 m above the hull's floor (the hoverbike's floor is
    about 0.56 m below its origin, so the hover physics hold it at a hoverbike's height).
  - Mod meshes are drawn unlit (vertex colour, no texture here), so each corner's colour is
    Tripo's colour (baked into the vertices in stage E) x occlusion x a fixed key light,
    quantised to at most 64 colours per display list.

Nodes (the hoverbike's layout first: position root, bounding box, a display list):
  matrix  part   node
  0       0      root; bounding box; display list "hull" (hull, ring and its 18 seats)
  1       1      turret yaw, on the ring's axis: display list "pod" (the gunner's pod)
  2       2      cannon pitch, at the neck joint, child of the yaw: display list "cannon"
  3       50     muzzle, child of the pitch
  4..21   110+j  ring seat j (the crystal's base), children of the root
  22      128    the muzzle crystal's seat, child of the pitch
  toggle 140 ->  display list "crystals": every working crystal (seafoam), each on its
                 seat's matrix, so the engine can scale a seat to regrow its crystal
  toggle 141 ->  display list "crystals_black": the same crystals, black (overheat), at 97 %
                 inside the seafoam ones: they show only when the engine hides toggle 140
  23      160    the driver's eye, child of the root: in the well ahead of the turret deck,
                 looking over the nose
  24      161    the gunner's eye, child of the yaw (it turns with the turret): above the
                 cannon, looking along it
The two eye points carry no geometry. The game reads them for the riders' cameras
(hoverbikeSeatModelPos, src/game/vehiclegraph.c); a vehicle model without part 160 gets the
base hoverbike's driver position.
"""
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

LENGTH_M = 3.10
UNITS_PER_METRE = 1000.0
FLOOR_TO_ORIGIN_M = 0.54
LIGHT = np.array([0.30, 0.85, 0.42])          # model space: above, ahead, from the left
LIGHT = LIGHT / np.linalg.norm(LIGHT)
COLOUR_CAP = 64
SEAFOAM = (47, 212, 126)                      # the crystal pistol's full crystal, #2FD47E
BLACK = (20, 24, 26)
RING_SCALE = (2.8, 2.8, 2.3)                  # the pistol's crystal, larger (Mike's brief)
RING_SINK = 0.30                              # share of a ring crystal's height inside its seat
MUZZLE_SINK = 0.55
BLACK_SCALE = 0.97
PART_YAW, PART_PITCH, PART_MUZZLE = 1, 2, 50
PART_SEAT0, PART_MUZZLE_SEAT = 110, 128
TOGGLE_CRYSTALS, TOGGLE_BLACK = 140, 141
MTX_ROOT, MTX_YAW, MTX_PITCH, MTX_MUZZLE, MTX_SEAT0 = 0, 1, 2, 3, 4
MTX_MUZZLE_SEAT = MTX_SEAT0 + 18
# Where the riders' eyes are, in model units (+X left, +Y up, +Z forward; the hull's deck
# is at y 306, the well ahead of it bottoms near y -45 at z 390 to 580, the nose ahead is
# below y 70, the pod spans y 306 to 511 and the cannon y 511 to 935). The base
# hoverbike's driver position, 80 world units up and 50 back, is inside this vehicle's
# cannon, which is why the driver's view was full of hull before these parts existed.
PART_DRIVER_SEAT, PART_GUNNER_SEAT = 160, 161
MTX_DRIVER_SEAT, MTX_GUNNER_SEAT = MTX_MUZZLE_SEAT + 1, MTX_MUZZLE_SEAT + 2
DRIVER_EYE = (0.0, 420.0, 450.0)
GUNNER_EYE = (0.0, 960.0, -300.0)
POSITION, TOGGLE, BBOX, DL = 2, 18, 10, 24


def to_s16(value):
    return int(math.floor(value + 0.5) if value >= 0.0 else math.ceil(value - 0.5))


def collapsed(a, b, c):
    ab = [b[i] - a[i] for i in range(3)]
    ac = [c[i] - a[i] for i in range(3)]
    cross = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
    return a == b or a == c or b == c or cross == (0, 0, 0)


def quantise(colours, k=COLOUR_CAP, iters=25, seed=7):
    """colours: (N, 3) uint8 -> (N, 3) uint8 with at most k distinct values (weighted k-means)."""
    uniq, inv, counts = np.unique(colours, axis=0, return_inverse=True, return_counts=True)
    inv = inv.reshape(-1)
    if len(uniq) <= k:
        return colours
    x = uniq.astype(float)
    w = counts.astype(float)
    rng = np.random.default_rng(seed)
    centres = [x[rng.choice(len(x), p=w / w.sum())]]
    d2 = ((x - centres[0]) ** 2).sum(1)
    for _ in range(1, k):
        p = w * d2
        centres.append(x[rng.choice(len(x), p=p / p.sum())])
        d2 = np.minimum(d2, ((x - centres[-1]) ** 2).sum(1))
    c = np.array(centres)
    for _ in range(iters):
        lab = ((x[:, None, :] - c[None]) ** 2).sum(2).argmin(1)
        for j in range(k):
            m = lab == j
            if m.any():
                c[j] = (x[m] * w[m, None]).sum(0) / w[m].sum()
    lab = ((x[:, None, :] - c[None]) ** 2).sum(2).argmin(1)
    cq = np.clip(np.round(c), 0, 255).astype(np.uint8)
    return cq[lab][inv]


class Frame:
    """Blender (1 m Tripo model) -> native model units."""

    def __init__(self, hull_tris):
        pts = np.array([c["p"] for t in hull_tris for c in t])
        self.length = pts[:, 1].max() - pts[:, 1].min()
        self.s = LENGTH_M * UNITS_PER_METRE / self.length
        self.y0 = (pts[:, 1].max() + pts[:, 1].min()) / 2.0
        self.z0 = pts[:, 2].min() + FLOOR_TO_ORIGIN_M * UNITS_PER_METRE / self.s

    def p(self, b):
        return np.array([b[0] * self.s, (b[2] - self.z0) * self.s, -(b[1] - self.y0) * self.s])

    @staticmethod
    def n(b):
        return np.array([b[0], b[2], -b[1]])


def shade(rgb, ao, n, lo_ao=0.42, lo_light=0.52, gain=1.18):
    lambert = max(0.0, float(n @ LIGHT))
    k = (lo_ao + (1.0 - lo_ao) * ao) * (lo_light + (1.0 - lo_light) * lambert) * gain
    return [min(255.0, ch * 255.0 * k) for ch in rgb]


def flat_shade(rgb, n, lo, gain):
    lambert = max(0.0, float(n @ LIGHT))
    k = (lo + (1.0 - lo) * lambert) * gain
    return [min(255.0, ch * k) for ch in rgb]


def build(parts_path, out_dir, preview_path=None):
    data = json.loads(Path(parts_path).read_text(encoding="utf-8"))
    fr = Frame(data["parts"]["Hull"])
    yaw = fr.p(data["pivots"]["Turret_Yaw"])
    pitch = fr.p(data["pivots"]["Cannon_Pitch"])
    muzzle = fr.p(data["muzzle"])
    seats = [fr.p(s) for s in data["seats"]]
    mouth = min(data["muzzle_mouth_halfwidth"], data["muzzle_mouth_halfheight"]) * fr.s

    # Crystal piece (pistol model units, base at the origin, growth +Z in Blender).
    piece = data["crystal"]
    piece_w = max(abs(c["p"][0]) for t in piece for c in t) * 2.0
    piece_h = max(c["p"][2] for t in piece for c in t)

    groups = {}       # group -> list of (matrix index, [(abs pos, rel pos, colour float3)] x 3)

    def add(group, mtx, origin, corners):
        groups.setdefault(group, []).append((mtx, [(p, p - origin, col) for p, col in corners]))

    for name, group, mtx, origin in (("Hull", "hull", MTX_ROOT, np.zeros(3)),
                                     ("Pod", "pod", MTX_YAW, yaw),
                                     ("Cannon", "cannon", MTX_PITCH, pitch)):
        for tri in data["parts"][name]:
            add(group, mtx, origin, [(fr.p(c["p"]), shade(c["c"], c["ao"], fr.n(c["n"]))) for c in tri])

    ring_scale = np.array(RING_SCALE)
    muzzle_k = 0.8 * mouth / (piece_w / 2.0)       # the crystal fills most of the bore
    muzzle_scale = np.array([muzzle_k, muzzle_k, muzzle_k * 0.8])
    muzzle_seat = muzzle + np.array([0.0, 0.0, -MUZZLE_SINK * piece_h * muzzle_scale[2]])

    def crystal(group, mtx, seat, scale, up, colour, lo, gain, shrink):
        # up: True = growth along +Y (ring), False = along +Z (muzzle, forward)
        for tri in piece:
            corners = []
            for c in tri:
                q = np.array(c["p"]) * scale * shrink
                m = np.array(c["n"]) / scale
                m = m / np.linalg.norm(m)
                if up:
                    q = np.array([q[0], q[2], -q[1]])
                    m = np.array([m[0], m[2], -m[1]])
                    q[1] -= RING_SINK * piece_h * scale[2]
                corners.append((seat + q, flat_shade(colour, m, lo, gain)))
            add(group, mtx, seat, corners)

    for black, group in ((False, "crystals"), (True, "crystals_black")):
        colour, lo, gain = (BLACK, 0.55, 1.0) if black else (SEAFOAM, 0.70, 1.12)
        shrink = BLACK_SCALE if black else 1.0
        for j, seat in enumerate(seats):
            crystal(group, MTX_SEAT0 + j, seat, ring_scale, True, colour, lo, gain, shrink)
        crystal(group, MTX_MUZZLE_SEAT, muzzle_seat, muzzle_scale, False, colour, lo, gain, shrink)

    # Quantise each display list's colours, then write the OBJ (one vertex per corner).
    v_lines, vt_lines, f_lines, faces = [], [], [], []
    colour_counts = {}
    dropped = 0
    lo_pt, hi_pt = np.full(3, np.inf), np.full(3, -np.inf)
    radius = 0.0
    preview = []
    order = ["hull", "pod", "cannon", "crystals", "crystals_black"]
    for group in order:
        tris = groups[group]
        cols = np.array([[min(255, max(0, round(ch))) for ch in col] for _, cs in tris for _, _, col in cs],
                        dtype=np.uint8)
        q = quantise(cols)
        colour_counts[group] = len(np.unique(q, axis=0))
        f_lines += ["g " + group, "usemtl " + group]
        k = 0
        for mtx, cs in tris:
            rel = [tuple(to_s16(x) for x in r) for _, r, _ in cs]
            rgb = [tuple(int(x) for x in q[k + i]) for i in range(3)]
            k += 3
            if collapsed(*rel):
                dropped += 1
                continue
            idx = []
            for (absp, _, _), r, c in zip(cs, rel, rgb):
                v_lines.append("v %d %d %d %d %d %d 255" % (r[0], r[1], r[2], c[0], c[1], c[2]))
                vt_lines.append("vt 0.0000 1.0000")
                idx.append(len(v_lines))
                if group != "crystals_black":
                    lo_pt, hi_pt = np.minimum(lo_pt, absp), np.maximum(hi_pt, absp)
                    radius = max(radius, float(np.linalg.norm(absp)))
                preview.append((absp, c, group))
            f_lines.append("f " + " ".join("%d/%d" % (i, i) for i in idx))
            faces.append((group, mtx))
    for group, n in colour_counts.items():
        assert n <= COLOUR_CAP, (group, n)
    assert len(v_lines) <= 32767, len(v_lines)

    bounds = {"xmin": math.floor(lo_pt[0]), "xmax": math.ceil(hi_pt[0]), "ymin": math.floor(lo_pt[1]),
              "ymax": math.ceil(hi_pt[1]), "zmin": math.floor(lo_pt[2]), "zmax": math.ceil(hi_pt[2])}
    zero_bounds = {k: 0.0 for k in bounds}

    def node(nid, parent, ntype, partnum=-1, part=-1, mtx0=-1, pos=(0.0, 0.0, 0.0), target=-1,
             group="-", render_mtx=-1, mcount=0, node_bounds=None, hitpart=0):
        zero = {"x": 0.0, "y": 0.0, "z": 0.0}
        return {"id": nid, "parent": parent, "type": ntype, "partnum": partnum, "part": part,
                "mtx0": mtx0, "mtx1": -1, "mtx2": -1,
                "position": {"x": round(float(pos[0]), 3), "y": round(float(pos[1]), 3),
                             "z": round(float(pos[2]), 3)},
                "drawdist": 0.0, "target": target, "group": group, "render_mtx": render_mtx,
                "mcount": mcount, "hitpart": hitpart, "bounds": node_bounds or dict(zero_bounds),
                "distance": {"near": 0.0, "far": 0.0},
                "reorder": {"pivot": dict(zero), "axis": {"x": 0.0, "y": 1.0, "z": 0.0},
                            "target_a": -1, "target_b": -1, "side": 0}}

    nodes, parts = [], []

    def put(n):
        nodes.append(n)
        return n["id"]

    root = put(node(0, -1, POSITION, partnum=0, part=MTX_ROOT, mtx0=MTX_ROOT, node_bounds=bounds))
    parts.append((0, root))
    bbox = put(node(len(nodes), root, BBOX, node_bounds=bounds, hitpart=1))
    put(node(len(nodes), bbox, DL, group="hull", render_mtx=MTX_ROOT, mcount=1, node_bounds=bounds))
    yaw_id = put(node(len(nodes), root, POSITION, partnum=PART_YAW, part=MTX_YAW, mtx0=MTX_YAW, pos=yaw))
    parts.append((PART_YAW, yaw_id))
    put(node(len(nodes), yaw_id, DL, group="pod", render_mtx=MTX_YAW, mcount=1, node_bounds=bounds))
    pitch_id = put(node(len(nodes), yaw_id, POSITION, partnum=PART_PITCH, part=MTX_PITCH, mtx0=MTX_PITCH,
                        pos=pitch - yaw))
    parts.append((PART_PITCH, pitch_id))
    put(node(len(nodes), pitch_id, DL, group="cannon", render_mtx=MTX_PITCH, mcount=1, node_bounds=bounds))
    muz_id = put(node(len(nodes), pitch_id, POSITION, partnum=PART_MUZZLE, part=MTX_MUZZLE, mtx0=MTX_MUZZLE,
                      pos=muzzle - pitch))
    parts.append((PART_MUZZLE, muz_id))
    mseat_id = put(node(len(nodes), pitch_id, POSITION, partnum=PART_MUZZLE_SEAT, part=MTX_MUZZLE_SEAT,
                        mtx0=MTX_MUZZLE_SEAT, pos=muzzle_seat - pitch))
    parts.append((PART_MUZZLE_SEAT, mseat_id))
    for j, seat in enumerate(seats):
        sid = put(node(len(nodes), root, POSITION, partnum=PART_SEAT0 + j, part=MTX_SEAT0 + j,
                       mtx0=MTX_SEAT0 + j, pos=seat))
        parts.append((PART_SEAT0 + j, sid))
    for toggle_part, group in ((TOGGLE_CRYSTALS, "crystals"), (TOGGLE_BLACK, "crystals_black")):
        tog = put(node(len(nodes), root, TOGGLE, partnum=toggle_part, target=len(nodes) + 1))
        put(node(len(nodes), tog, DL, group=group, render_mtx=MTX_SEAT0, mcount=1, node_bounds=bounds))
        parts.append((toggle_part, tog))
    # The riders' eye points, last so that no earlier node moves.
    driver_id = put(node(len(nodes), root, POSITION, partnum=PART_DRIVER_SEAT, part=MTX_DRIVER_SEAT,
                         mtx0=MTX_DRIVER_SEAT, pos=np.array(DRIVER_EYE)))
    parts.append((PART_DRIVER_SEAT, driver_id))
    gunner_id = put(node(len(nodes), yaw_id, POSITION, partnum=PART_GUNNER_SEAT, part=MTX_GUNNER_SEAT,
                         mtx0=MTX_GUNNER_SEAT, pos=np.array(GUNNER_EYE) - yaw))
    parts.append((PART_GUNNER_SEAT, gunner_id))
    for i, n in enumerate(nodes):
        assert n["id"] == i and n["parent"] < i

    header = ["# maian_hover.pdmesh model.obj - Maian hover vehicle, built by build_vehicle_mesh.py",
              "# Integer model units (1000 per metre); +X left, +Y up, +Z forward. One vertex per",
              "# triangle corner: v x y z r g b a (0-255 vertex colour; no texture). Each group is a",
              "# display list in the frame of its node (model.nodes.json); model.faces.json names",
              "# every triangle's matrix."]
    obj_text = "\n".join(header + ["mtllib model.mtl"] + v_lines + vt_lines + f_lines) + "\n"
    mtl = ["# maian_hover.pdmesh model.mtl - vertex-coloured, opaque. pd_render_class 3 resets",
           "# the opaque Z-buffered render mode at the start of each list."]
    for group in order:
        mtl += ["", "newmtl " + group, "Kd 1.000000 1.000000 1.000000", "d 1.000000", "pd_render_class = 3"]
    mesh_ini = "\n".join([
        "; maian_hover.pdmesh - the Maian hover vehicle, built by build_vehicle_mesh.py from the",
        "; cleaned-up Tripo model (D:\\Game Dev\\PD2-Assets\\maian-hover-vehicle).",
        "[model]",
        "catalog_id = example:tri_mesh",
        "kind = mesh",
        "model_file = model.obj",
        "material_file = model.mtl",
        "hierarchy_file = model.nodes.json",
        "parts_file = model.parts.json",
        "faces_file = model.faces.json",
        "render_stream_file = model.render.json",
        "; Culling radius in model units: the farthest vertex from the origin.",
        "model_scale = %.3f" % radius,
        ""])
    files = {
        "mesh.ini": mesh_ini,
        "model.obj": obj_text,
        "model.mtl": "\n".join(mtl) + "\n",
        "model.nodes.json": json.dumps({"schema": "pd2.mesh.nodes.v1", "nodes": nodes}, indent=2) + "\n",
        "model.parts.json": json.dumps({"schema": "pd2.mesh.parts.v1",
                                        "parts": [{"partnum": p, "node": n} for p, n in sorted(parts)]},
                                       indent=2) + "\n",
        "model.faces.json": json.dumps({"schema": "pd2.mesh.faces.v1",
                                        "faces": [{"face_index": i, "matrix_index": m}
                                                  for i, (_, m) in enumerate(faces)]}) + "\n",
        "model.render.json": json.dumps({"schema": "pd2.mesh.render.v1", "pd_kind": "mesh_render_commands",
                                         "pd_schema_version": 1,
                                         "commands": [{"group": g, "command": "tri", "face_index": i,
                                                       "matrix_index": m}
                                                      for i, (g, m) in enumerate(faces)]}) + "\n",
    }
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, text in files.items():
        (out_dir / name).write_bytes(text.encode("ascii"))
    if preview_path:
        write_preview(preview, preview_path)
    size = hi_pt - lo_pt
    print("frame: scale %.1f units per Tripo unit; origin y0 %.4f z0 %.4f" % (fr.s, fr.y0, fr.z0))
    print("triangles:", {g: len(t) for g, t in groups.items()}, "dropped (collapsed):", dropped)
    print("vertices written:", len(v_lines), "colours per list:", colour_counts)
    print("bounds:", bounds)
    print("size (m): %.2f wide, %.2f tall, %.2f long" % tuple(v / UNITS_PER_METRE for v in size))
    print("culling radius (model units): %.1f" % radius)
    print("pivots (model units): yaw %s pitch %s muzzle %s" % (
        np.round(yaw, 1).tolist(), np.round(pitch, 1).tolist(), np.round(muzzle, 1).tolist()))
    print("ring seats:", len(seats), "first", np.round(seats[0], 1).tolist())
    print("eye points (model units): driver %s gunner %s" % (list(DRIVER_EYE), list(GUNNER_EYE)))
    return 0


def write_preview(corners, path):
    """A .glb of the assembled vehicle in metres with the final vertex colours (review only)."""
    vis = [(p, c) for p, c, g in corners if g != "crystals_black"]
    pos = b"".join(struct.pack("<3f", *(p / UNITS_PER_METRE)) for p, _ in vis)

    def linear(byte):
        # glTF vertex colours are linear; the game's are the sRGB bytes as written
        s = byte / 255.0
        return s / 12.92 if s <= 0.04045 else ((s + 0.055) / 1.055) ** 2.4

    col = b"".join(struct.pack("<4f", linear(c[0]), linear(c[1]), linear(c[2]), 1.0) for _, c in vis)
    n = len(vis)
    buf = pos + col
    lo = (np.array([p for p, _ in vis]) / UNITS_PER_METRE).min(0).tolist()
    hi = (np.array([p for p, _ in vis]) / UNITS_PER_METRE).max(0).tolist()
    gltf = {"asset": {"version": "2.0", "generator": "build_vehicle_mesh.py preview"},
            "buffers": [{"byteLength": len(buf)}],
            "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(pos), "target": 34962},
                            {"buffer": 0, "byteOffset": len(pos), "byteLength": len(col), "target": 34962}],
            "accessors": [{"bufferView": 0, "componentType": 5126, "count": n, "type": "VEC3", "min": lo, "max": hi},
                          {"bufferView": 1, "componentType": 5126, "count": n, "type": "VEC4"}],
            "materials": [{"name": "vertex_colour", "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1],
                                                                             "metallicFactor": 0.0}}],
            "meshes": [{"name": "maian_hover_preview",
                        "primitives": [{"attributes": {"POSITION": 0, "COLOR_0": 1}, "material": 0, "mode": 4}]}],
            "nodes": [{"name": "maian_hover_preview", "mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0}
    js = json.dumps(gltf).encode("ascii")
    js += b" " * ((4 - len(js) % 4) % 4)
    buf += b"\0" * ((4 - len(buf) % 4) % 4)
    glb = struct.pack("<3I", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(buf))
    glb += struct.pack("<2I", len(js), 0x4E4F534A) + js + struct.pack("<2I", len(buf), 0x004E4942) + buf
    Path(path).write_bytes(glb)
    print("preview:", path)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    args = sys.argv[1:]
    preview = None
    if "--preview" in args:
        i = args.index("--preview")
        preview = args[i + 1]
        del args[i:i + 2]
    target = Path(args[1]) if len(args) > 1 else Path(__file__).parent / "meshes" / "vehicle"
    sys.exit(build(args[0], target, preview))
