#!/usr/bin/env python3
"""Build the Grid's original two-room portal and navigation fixture."""

from argparse import ArgumentParser
import json
from pathlib import Path
import sys

MOD_DIR = Path(__file__).resolve().parent
REPO = MOD_DIR.parents[1]
sys.path.insert(0, str(REPO / "tools"))
sys.path.insert(0, str(MOD_DIR.parent / "blank_room"))

import level_archive as la  # noqa: E402
from build_blank_room import quad, texcoord, tile_png  # noqa: E402

X = 1550
Z = 820
HEIGHT = 520
DOOR_HALF = 220
DOOR_HEIGHT = 340
MOD_ID = "grid_two_rooms"
ARENA = f"mod_{MOD_ID}:arena_{MOD_ID}"
SCENARIO = f"mod_{MOD_ID}:scenario_{MOD_ID}"
SCENARIO_MEMBER = "dependencies/assets/scenarios/scenario_grid_two_rooms.pdscenario"


def room_surfaces(low, high, right_room):
    surfaces = []

    def add(points, normal, kind):
        surfaces.extend((triangle, kind) for triangle in quad(*points, normal))

    def wall_x(x, start_z, end_z, bottom, inward):
        add(((x, bottom, start_z), (x, bottom, end_z),
             (x, HEIGHT, end_z), (x, HEIGHT, start_z)),
            (inward, 0, 0), "wall")

    for height, inward, kind in [(0, 1, "floor"), (HEIGHT, -1, "ceiling")]:
        add(((low, height, -Z), (high, height, -Z),
             (high, height, Z), (low, height, Z)),
            (0, inward, 0), kind)
    for z, inward in [(-Z, 1), (Z, -1)]:
        add(((low, 0, z), (high, 0, z), (high, HEIGHT, z), (low, HEIGHT, z)),
            (0, 0, inward), "wall")

    wall_x(high if right_room else low, -Z, Z, 0, -1 if right_room else 1)
    partition_normal = 1 if right_room else -1
    wall_x(0, -Z, -DOOR_HALF, 0, partition_normal)
    wall_x(0, DOOR_HALF, Z, 0, partition_normal)
    wall_x(0, -DOOR_HALF, DOOR_HALF, DOOR_HEIGHT, partition_normal)
    return surfaces


def build():
    port = {
        "portal_ref": "portal_0000", "room_a": "room_1", "room_b": "room_2", "flags": 0,
        "vertices": [(0, 0, DOOR_HALF), (0, 0, -DOOR_HALF),
                     (0, DOOR_HEIGHT, -DOOR_HALF), (0, DOOR_HEIGHT, DOOR_HALF)],
    }
    primitives, collision, collision_rooms = [], [], []
    image = tile_png()
    bounds = [(-X, 0), (0, X)]
    tints = [(0.55, 0.72, 0.88, 1.0), (0.84, 0.62, 0.42, 1.0)]
    for room, ((low, high), color) in enumerate(zip(bounds, tints), 1):
        corners = []
        for triangle, kind in room_surfaces(low, high, room == 2):
            corners.extend((point, texcoord(point, kind, triangle), color) for point in triangle)
            if kind != "ceiling":
                collision.append((triangle, kind))
                collision_rooms.append(room)
        primitives.append(la.Primitive("grid_room_%d" % room, "grid_tile_%d" % room, image, corners, room))

    # Consecutive pads cross the aperture in both directions.
    route = [(-360, 120), (-880, 400), (-1150, 0), (-800, -400),
             (-360, -120), (360, -120), (960, -160), (360, 120)]
    places = [((x, la.PAD_ABOVE_FLOOR, z), (1 if x < 0 else -1, 0, 0)) for x, z in route]
    level = la.Level(
        arena_id=ARENA, scenario_id=SCENARIO, scenario_member=SCENARIO_MEMBER,
        display_name="Grid Test Rooms", category="Templates",
        arena_comment="Original two-room Grid test fixture",
        scenario_comment="Two chambers joined by a door-sized portal",
        collision_comment="Original Grid fixture collision",
        scene=la.scene_glb(primitives, "Perfect Dark 2", "grid_test_rooms", "grid_test_rooms"),
        collision=collision, places=places,
        room_low=(-X, 0, -Z), room_high=(0, HEIGHT, Z), room_origin=(-X / 2, HEIGHT / 2, 0),
        extra_rooms=(((X / 2, HEIGHT / 2, 0), (0, 0, -Z), (X, HEIGHT, Z)),),
        portals=(port,), collision_rooms=tuple(collision_rooms),
    )
    return la.build_level(level)[0]


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check the existing archive's source members")
    args = parser.parse_args()
    manifest = json.loads((MOD_DIR / "mod.json").read_text(encoding="utf-8"))
    enabled = json.loads((MOD_DIR / "enabled.json").read_text(encoding="utf-8"))
    if manifest.get("id") != MOD_ID or enabled != [MOD_ID]:
        parser.exit(1, "Grid test rooms: fixture mod ID does not match the arena namespace\n")
    archive = build()
    path = MOD_DIR / "grid_two_rooms.pdarena"
    errors = la.conformance(archive, path.name)
    if errors:
        parser.exit(1, "\n".join(errors) + "\n")
    if args.check:
        if not path.is_file() or la.unpacked(archive) != la.unpacked(path.read_bytes()):
            parser.exit(1, "Grid test rooms are missing or stale\n")
    else:
        path.write_bytes(archive)
    print("Grid test rooms: two rooms, one portal, eight connected navigation pads")


if __name__ == "__main__":
    main()
