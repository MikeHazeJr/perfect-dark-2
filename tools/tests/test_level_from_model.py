#!/usr/bin/env python3
"""Tests for tools/build_level_from_model.py: a level from a model (.glb).

The model is made here, in memory, in glTF's own terms (metres, +Y up,
counter-clockwise fronts): a 20 by 20 metre floor, four walls 4 metres high
facing in, a ramp up to a platform a metre high, a ceiling, a decoration
named "nocollide_banner", a block named "collision_blocker" that must not be
drawn, a mirrored copy of the ramp (a node with a negative scale, whose
triangles must come out wound the right way), two materials (one with an
embedded PNG, one plain) and eight empties named start_1 to start_8 on a
ring, each turned to face the middle, one of them standing on the platform.

Run: python -B tools/tests/test_level_from_model.py -v
It needs the repository's example level and archive validator, as the Blank
Room's builder does.
"""

from __future__ import annotations

import json
import math
import struct
import sys
import unittest
import zlib
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import build_level_from_model as blm  # noqa: E402
import level_archive as la  # noqa: E402
from simple_glb import GlbWriter, plain_png, quad  # noqa: E402


H = 10.0   # half the floor's side, metres
TALL = 4.0


def arena_model(starts=True) -> bytes:
    w = GlbWriter()
    tiles = w.material("Tiles", (0.9, 0.9, 1.0, 1.0), plain_png())
    plain = w.material("Trim", (0.8, 0.3, 0.2, 1.0), None)
    floor = quad((-H, 0, H), (H, 0, H), (H, 0, -H), (-H, 0, -H))                 # faces up
    walls = (quad((-H, 0, -H), (H, 0, -H), (H, TALL, -H), (-H, TALL, -H)) +       # north, faces +z
             quad((H, 0, H), (-H, 0, H), (-H, TALL, H), (H, TALL, H)) +           # south, faces -z
             quad((-H, 0, H), (-H, 0, -H), (-H, TALL, -H), (-H, TALL, H)) +       # west, faces +x
             quad((H, 0, -H), (H, 0, H), (H, TALL, H), (H, TALL, -H)))            # east, faces -x
    ceiling = quad((-H, TALL, -H), (H, TALL, -H), (H, TALL, H), (-H, TALL, H))    # faces down
    # A platform 1 m high at x 4..8, z -2..2, and a ramp up to it from x 1.
    platform = quad((4, 1, 2), (8, 1, 2), (8, 1, -2), (4, 1, -2))
    ramp = quad((1, 0, 2), (4, 1, 2), (4, 1, -2), (1, 0, -2))
    w.node("arena", w.mesh("arena", floor + walls + platform + ramp, tiles))
    w.node("ceiling", w.mesh("ceiling", ceiling, plain))
    # A mirrored ramp: the same triangles under a node with scale x -1.
    w.node("ramp_mirrored", w.mesh("ramp_mirrored", ramp, tiles), scale=[-1.0, 1.0, 1.0])
    banner = quad((-3, 2, -9.9), (3, 2, -9.9), (3, 3, -9.9), (-3, 3, -9.9))
    w.node("nocollide_banner", w.mesh("banner", banner, plain))
    blocker = quad((-1, 0, 6), (1, 0, 6), (1, 2, 6), (-1, 2, 6))
    w.node("collision_blocker", w.mesh("blocker", blocker, plain))
    if starts:
        for k in range(8):
            angle = 2.0 * math.pi * k / 8
            x, z = 7.0 * math.cos(angle), 7.0 * math.sin(angle)
            y = 1.0 if k == 0 else 0.0          # start_1 (x 7, z 0) stands on the platform
            phi = math.atan2(-math.cos(angle), -math.sin(angle))
            w.node("start_%d" % (k + 1), translation=[x, y + 0.2, z],
                   rotation=[0.0, math.sin(phi / 2.0), 0.0, math.cos(phi / 2.0)])
    return w.bytes()


class LevelFromModel(unittest.TestCase):
    def build(self, model: bytes):
        path = Path(self.id().rsplit(".", 1)[-1] + ".glb")
        tmp = TOOLS / "tests" / ("_tmp_" + path.name)
        tmp.write_bytes(model)
        try:
            return blm.build(tmp, "model_room", "Model Room", 100.0, False)
        finally:
            tmp.unlink()

    def test_a_model_becomes_a_level_the_validator_accepts(self):
        arena, mod, members, level = self.build(arena_model())
        self.assertEqual(la.conformance(arena, "model_room.pdarena"), [])
        self.assertEqual(json.loads(mod)["id"], "model_room")
        ini = la.unpacked(arena)["arena.ini"].decode("utf-8")
        self.assertIn("catalog_id = mod_model_room:arena_model_room", ini)
        self.assertIn("display_name = Model Room", ini)

    def test_collision_takes_floors_and_walls_and_leaves_ceilings_and_decorations(self):
        _arena, _mod, _members, level = self.build(arena_model())
        floors = [t for t, kind in level.collision if kind == "floor"]
        walls = [t for t, kind in level.collision if kind != "floor"]
        # Floor 2, platform 2, ramp 2, mirrored ramp 2 (a ramp rises 1 in 3: it is a floor).
        self.assertEqual(len(floors), 8)
        # Four walls of 2 and the blocker's 2; not the ceiling, not the banner.
        self.assertEqual(len(walls), 10)
        for triangle in floors:
            normal, _length = la.unit_normal(triangle)
            self.assertGreaterEqual(normal[1], 0.5)
        # Metres became centimetres.
        xs = [p[0] for t, _k in level.collision for p in t]
        self.assertAlmostEqual(max(xs), 1000.0, places=3)
        self.assertAlmostEqual(min(xs), -1000.0, places=3)

    def test_the_mirrored_part_is_wound_the_right_way(self):
        _arena, _mod, _members, level = self.build(arena_model())
        mirrored = [t for t, kind in level.collision if kind == "floor" and min(p[0] for p in t) < -50.0
                    and max(p[1] for p in t) > 1.0]
        self.assertEqual(len(mirrored), 2)
        for triangle in mirrored:
            normal, _length = la.unit_normal(triangle)
            self.assertGreater(normal[1], 0.5)

    def test_the_scene_has_one_textured_part_per_material_and_no_collision_only_mesh(self):
        _arena, _mod, members, _level = self.build(arena_model())
        glb = members["scene.glb"]
        length = struct.unpack("<I", glb[12:16])[0]
        document = json.loads(glb[20:20 + length])
        primitives = document["meshes"][0]["primitives"]
        self.assertEqual([m["name"] for m in document["materials"]], ["Tiles", "Trim"])
        counts = [document["accessors"][p["attributes"]["POSITION"]]["count"] for p in primitives]
        # Tiles: floor 2 + walls 8 + platform 2 + ramp 2 + mirrored ramp 2 = 16 triangles.
        # Trim: ceiling 2 + banner 2 = 4 (the blocker is collision only).
        self.assertEqual(counts, [48, 12])

    def test_starts_stand_above_their_floor_facing_the_middle_in_ring_order(self):
        _arena, _mod, members, level = self.build(arena_model())
        self.assertEqual(len(level.places), 8)
        heights = sorted(round(position[1], 3) for position, _look in level.places)
        self.assertEqual(heights, [50.0] * 7 + [150.0])     # one on the platform
        for position, look in level.places:
            towards = (-position[0], -position[2])
            norm = math.hypot(*towards)
            self.assertAlmostEqual(look[0], towards[0] / norm, places=4)
            self.assertAlmostEqual(look[2], towards[1] / norm, places=4)
            self.assertEqual(look[1], 0.0)
        angles = [math.atan2(p[2], p[0]) for p, _l in level.places]
        self.assertEqual(angles, sorted(angles))
        path = json.loads(members["navigation/paths.json"])["rows"][0]
        self.assertEqual(len(path["pads"]), 8)

    def test_a_model_without_starts_is_refused_with_the_reason(self):
        with self.assertRaises(la.BuildError) as caught:
            self.build(arena_model(starts=False))
        self.assertIn("no start", str(caught.exception))

    def test_a_bad_id_is_refused(self):
        with self.assertRaises(la.BuildError):
            blm.build(Path("unused.glb"), "Model Room", "x", 100.0, False)


if __name__ == "__main__":
    unittest.main()
