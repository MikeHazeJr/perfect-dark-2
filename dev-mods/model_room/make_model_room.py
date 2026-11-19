#!/usr/bin/env python3
"""The Model Room: an original level made from a model, end to end.

Forge step F5 (context/designs/modding/forge-mode-scope.md) makes a level
from a model a modelling program exports. This script stands in for the
modelling program: it writes model_room.glb in glTF's own terms (metres, +Y
up), the way Blender's exporter would, and then hands it to
tools/build_level_from_model.py, which makes model_room.pdarena and mod.json
here. Everything in it is drawn here; nothing comes from the original game.

The room: 24 by 24 metres and 5 high; a platform 1.2 metres up at each end,
reached by a ramp (the west one is the east one under a mirroring node, which
the builder must wind the right way round); four pillars; two low cover
blocks; a ceiling (drawn, never collided with); a strip along the north wall
named "nocollide_strip" (drawn, not collided with); and eight starts on a
ring, two of them on the platforms, each turned to face the middle.

Usage:
    python dev-mods/model_room/make_model_room.py            write and build
    python dev-mods/model_room/make_model_room.py --check    check the built
                                                             files are current
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "tools"))
import build_level_from_model as blm  # noqa: E402
import level_archive as la  # noqa: E402
from simple_glb import GlbWriter, box, png, quad  # noqa: E402
sys.path.pop(0)

MODEL = HERE / "model_room.glb"
LEVEL_ID = "model_room"
NAME = "Model Room"

H = 12.0        # half the floor's side
TALL = 5.0      # floor to ceiling
LIFT = 1.2      # the platforms' height
RING = 8.5      # the starts' ring


def tile_texture() -> bytes:
    """A pale floor tile with a darker joint, 32 pixels a side."""
    size, pixels = 32, []
    for y in range(size):
        for x in range(size):
            edge = min(x, y, size - 1 - x, size - 1 - y)
            shade = 120 if edge < 1 else (182 if edge < 3 else 214)
            shade += ((x * 7 + y * 13) % 5) - 2
            pixels.append((shade, shade, shade + 4))
    return png(pixels, size, size)


def panel_texture() -> bytes:
    """A blue-grey wall panel with two darker seams across it."""
    size, pixels = 32, []
    for y in range(size):
        for x in range(size):
            seam = y in (0, 1, 16) or x == 0
            base = (96, 112, 138) if seam else (150, 166, 190)
            pixels.append(base)
    return png(pixels, size, size)


def stripe_texture() -> bytes:
    """Amber and dark bands on a slant, for the pillars and the cover."""
    size, pixels = 32, []
    for y in range(size):
        for x in range(size):
            band = ((x + y) // 8) % 2
            pixels.append((214, 160, 48) if band else (52, 48, 44))
    return png(pixels, size, size)


def model() -> bytes:
    w = GlbWriter("Perfect Dark 2 Model Room maker")
    floor_mat = w.material("Floor", (1.0, 1.0, 1.0, 1.0), tile_texture())
    wall_mat = w.material("Walls", (1.0, 1.0, 1.0, 1.0), panel_texture())
    stripe_mat = w.material("Stripes", (1.0, 1.0, 1.0, 1.0), stripe_texture())
    trim_mat = w.material("Trim", (0.36, 0.40, 0.48, 1.0), None)

    floor = quad((-H, 0, H), (H, 0, H), (H, 0, -H), (-H, 0, -H))
    walls = (quad((-H, 0, -H), (H, 0, -H), (H, TALL, -H), (-H, TALL, -H)) +      # north, faces +z
             quad((H, 0, H), (-H, 0, H), (-H, TALL, H), (H, TALL, H)) +          # south, faces -z
             quad((-H, 0, H), (-H, 0, -H), (-H, TALL, -H), (-H, TALL, H)) +      # west, faces +x
             quad((H, 0, -H), (H, 0, H), (H, TALL, H), (H, TALL, -H)))           # east, faces -x
    ceiling = quad((-H, TALL, -H), (H, TALL, -H), (H, TALL, H), (-H, TALL, H))
    w.node("floor", w.mesh("floor", floor, floor_mat, 0.5))
    w.node("walls", w.mesh("walls", walls, wall_mat, 0.25))
    w.node("ceiling", w.mesh("ceiling", ceiling, trim_mat))

    # The east platform and its ramp, and the same again mirrored to the west.
    platform = box(5.0, 0.0, -2.5, 9.0, LIFT, 2.5, top=True)
    ramp = quad((1.4, 0.0, 2.5), (5.0, LIFT, 2.5), (5.0, LIFT, -2.5), (1.4, 0.0, -2.5))
    ramp_sides = [((1.4, 0.0, 2.5), (5.0, 0.0, 2.5), (5.0, LIFT, 2.5)),       # +z side, faces +z
                  ((5.0, 0.0, -2.5), (1.4, 0.0, -2.5), (5.0, LIFT, -2.5))]    # -z side, faces -z
    east = w.mesh("platform", platform + ramp + ramp_sides, floor_mat, 0.5)
    w.node("platform_east", east)
    w.node("platform_west", east, scale=[-1.0, 1.0, 1.0])

    pillars = []
    for cx in (-3.5, 3.5):
        for cz in (-7.0, 7.0):
            pillars += box(cx - 0.5, 0.0, cz - 0.5, cx + 0.5, TALL, cz + 0.5, top=False)
    cover = box(-1.5, 0.0, 4.2, 1.5, 1.1, 4.8) + box(-1.5, 0.0, -4.8, 1.5, 1.1, -4.2)
    w.node("pillars", w.mesh("pillars", pillars, stripe_mat, 1.0))
    w.node("cover", w.mesh("cover", cover, stripe_mat, 1.0))
    strip = quad((-8.0, 3.2, -H + 0.05), (8.0, 3.2, -H + 0.05), (8.0, 3.6, -H + 0.05), (-8.0, 3.6, -H + 0.05))
    w.node("nocollide_strip", w.mesh("strip", strip, stripe_mat, 1.0))

    for k in range(8):
        angle = 2.0 * math.pi * k / 8
        x, z = RING * math.cos(angle), RING * math.sin(angle)
        on_platform = k in (0, 4)          # due east and due west
        y = (LIFT if on_platform else 0.0) + 0.1
        phi = math.atan2(-math.cos(angle), -math.sin(angle))
        w.node("start_%d" % (k + 1), translation=[round(x, 4), y, round(z, 4)],
               rotation=[0.0, math.sin(phi / 2.0), 0.0, math.cos(phi / 2.0)])
    return w.bytes()


def main() -> int:
    check_only = "--check" in sys.argv[1:]
    data = model()
    if check_only:
        if not MODEL.is_file() or MODEL.read_bytes() != data:
            print("MODEL ROOM: STALE: model_room.glb; run the maker")
            return 1
    else:
        MODEL.write_bytes(data)
    try:
        arena, mod, _members, level = blm.build(MODEL, LEVEL_ID, NAME, 100.0, False)
    except la.BuildError as error:
        print("MODEL ROOM: not built: %s" % error)
        return 1
    errors = la.conformance(arena, LEVEL_ID + ".pdarena")
    for error in errors:
        print("conformance: " + error)
    if errors:
        print("MODEL ROOM: the archive validator refuses it (%d)" % len(errors))
        return 1
    out_arena, out_mod = HERE / (LEVEL_ID + ".pdarena"), HERE / "mod.json"
    if check_only:
        current = out_arena.is_file() and la.unpacked(out_arena.read_bytes()) == la.unpacked(arena) and \
            out_mod.is_file() and out_mod.read_text(encoding="utf-8") == mod
        print("MODEL ROOM: %s" % ("up to date" if current else "STALE: run the maker"))
        return 0 if current else 1
    out_arena.write_bytes(arena)
    out_mod.write_text(mod, encoding="utf-8", newline="\n")
    floors = sum(1 for _t, kind in level.collision if kind == "floor")
    print("MODEL ROOM: built %s (%d bytes) from %s (%d bytes): %d collision triangles (%d floors), %d starts at "
          "heights %s; sha256 %s" % (out_arena.name, len(arena), MODEL.name, len(data), len(level.collision), floors,
                                     len(level.places), sorted({p[1] for p, _l in level.places}), la.sha(arena)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
