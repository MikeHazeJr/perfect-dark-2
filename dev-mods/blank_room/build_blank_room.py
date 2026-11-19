#!/usr/bin/env python3
"""Builds the Blank Room: a small level that holds nothing of the original game.

One room, 24 by 24 metres and 4 high, with four pillars for cover, eight
player starts on a ring and no objects. It is the level template Forge starts
an original level from (context/designs/modding/forge-mode-scope.md, step F5):
every level cloned from a base arena embeds that arena's geometry and so can
never be shared, and until this one there was no playable level free of the
original game's data.

This script draws the walls and floor, their texture (a tile drawn here) and
the starts. Everything else in the archive, and the checks that mirror the
game's rules, come from tools/level_archive.py, which a level made from a
model shares (tools/build_level_from_model.py). The only things taken from
elsewhere are structural and come from this repository's own example level,
examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena, which CORE's
builder (tools/build_typed_pdxxx_examples.py) keeps in step with the game:
the graph's AI kind nodes and their fixed links, the pickup and door rule
nodes and edges, and the cache kind an arena.ini names. So when the level
formats move, rebuild the example and then this.

Usage:
    python dev-mods/blank_room/build_blank_room.py            build and check
    python dev-mods/blank_room/build_blank_room.py --check    check the built
                                                              file is current

The rules each member has to keep, with the file and line of the code that
enforces them, are in forge-mode-scope.md ("F5: the smallest level the game
accepts"). The checks mirror them, and the archive is then given to
tools/asset_archive_conformance.py.
"""

from __future__ import annotations

import json
import math
import struct
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "tools"))
import level_archive as la  # noqa: E402  (the shared level machinery)
sys.path.pop(0)

OUT_ARENA = HERE / "blank_room.pdarena"
OUT_MOD = HERE / "mod.json"

MOD_ID = "blank_room"
ARENA_ID = "mod_blank_room:arena_blank_room"
SCENARIO_ID = "mod_blank_room:scenario_blank_room"
SCENARIO_MEMBER = "dependencies/assets/scenarios/scenario_blank_room.pdscenario"
DISPLAY_NAME = "Blank Room"

# The room, in world units (1 unit is 1 cm; a player stands 172 tall).
HALF = 1200.0          # the floor runs from -HALF to +HALF on x and z
HEIGHT = 400.0         # floor to ceiling
CELL = 600.0           # floor, wall and ceiling quads are this wide
PILLAR_AT = 350.0      # pillar centres are at (+-PILLAR_AT, +-PILLAR_AT)
PILLAR_HALF = 80.0
RING = 850.0           # player starts stand on a ring of this radius
STARTS = 8
TILE = 200.0           # the texture repeats every TILE units

BuildError = la.BuildError
require = la.require
sub, cross, dot = la.sub, la.cross, la.dot


# ---------------------------------------------------------------------------
# Geometry
# ---------------------------------------------------------------------------

def quad(a, b, c, d, facing):
    """Two triangles for the quad a-b-c-d (corners in order round its edge),
    wound so that (v1 - v0) x (v2 - v0) points along `facing`. The game's
    floor test needs that for floors; walls face into the room by the same
    rule."""
    first = (a, b, c)
    second = (a, c, d)
    if dot(cross(sub(b, a), sub(c, a)), facing) < 0:
        first = (a, c, b)
        second = (a, d, c)
    return [first, second]


def steps(low, high, size):
    count = int(round((high - low) / size))
    return [low + size * i for i in range(count + 1)]


def build_surfaces():
    """Returns (collision, visual): lists of (triangle, kind) with kind one of
    floor, wall, pillar, ceiling. The ceiling is drawn and not collided with,
    as in the base arenas, which keep almost no downward faces in collision."""
    floor, walls, pillars, ceiling = [], [], [], []
    marks = steps(-HALF, HALF, CELL)
    for i in range(len(marks) - 1):
        for j in range(len(marks) - 1):
            x0, x1, z0, z1 = marks[i], marks[i + 1], marks[j], marks[j + 1]
            floor += quad((x0, 0.0, z0), (x1, 0.0, z0), (x1, 0.0, z1), (x0, 0.0, z1), (0.0, 1.0, 0.0))
            ceiling += quad((x0, HEIGHT, z0), (x1, HEIGHT, z0), (x1, HEIGHT, z1), (x0, HEIGHT, z1),
                            (0.0, -1.0, 0.0))
    for i in range(len(marks) - 1):
        a, b = marks[i], marks[i + 1]
        # West and east walls run along z; north and south along x. Each
        # faces the middle of the room.
        walls += quad((-HALF, 0.0, a), (-HALF, 0.0, b), (-HALF, HEIGHT, b), (-HALF, HEIGHT, a), (1.0, 0.0, 0.0))
        walls += quad((HALF, 0.0, a), (HALF, 0.0, b), (HALF, HEIGHT, b), (HALF, HEIGHT, a), (-1.0, 0.0, 0.0))
        walls += quad((a, 0.0, -HALF), (b, 0.0, -HALF), (b, HEIGHT, -HALF), (a, HEIGHT, -HALF), (0.0, 0.0, 1.0))
        walls += quad((a, 0.0, HALF), (b, 0.0, HALF), (b, HEIGHT, HALF), (a, HEIGHT, HALF), (0.0, 0.0, -1.0))
    for cx in (-PILLAR_AT, PILLAR_AT):
        for cz in (-PILLAR_AT, PILLAR_AT):
            x0, x1, z0, z1 = cx - PILLAR_HALF, cx + PILLAR_HALF, cz - PILLAR_HALF, cz + PILLAR_HALF
            # Four sides, each facing away from the pillar's middle.
            pillars += quad((x0, 0.0, z0), (x0, 0.0, z1), (x0, HEIGHT, z1), (x0, HEIGHT, z0), (-1.0, 0.0, 0.0))
            pillars += quad((x1, 0.0, z0), (x1, 0.0, z1), (x1, HEIGHT, z1), (x1, HEIGHT, z0), (1.0, 0.0, 0.0))
            pillars += quad((x0, 0.0, z0), (x1, 0.0, z0), (x1, HEIGHT, z0), (x0, HEIGHT, z0), (0.0, 0.0, -1.0))
            pillars += quad((x0, 0.0, z1), (x1, 0.0, z1), (x1, HEIGHT, z1), (x0, HEIGHT, z1), (0.0, 0.0, 1.0))
    collision = [(t, "floor") for t in floor] + [(t, "wall") for t in walls] + [(t, "pillar") for t in pillars]
    visual = collision + [(t, "ceiling") for t in ceiling]
    return collision, visual


# ---------------------------------------------------------------------------
# The texture and the scene
# ---------------------------------------------------------------------------

def tile_png(size: int = 64) -> bytes:
    """A plain floor tile: a pale panel with a darker joint round its edge
    and a little grain, drawn here so that nothing is borrowed."""
    rows = bytearray()
    for y in range(size):
        rows.append(0)  # filter type: none
        for x in range(size):
            edge = min(x, y, size - 1 - x, size - 1 - y)
            grain = ((x * 73856093) ^ (y * 19349663)) % 13 - 6
            shade = 118 if edge < 2 else (176 if edge < 4 else 206)
            shade = max(0, min(255, shade + grain))
            rows += bytes((shade, shade, min(255, shade + 6)))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)

    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)  # 8-bit RGB, no alpha
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) +
            chunk(b"IEND", b""))


TINT = {
    "floor": (0.86, 0.87, 0.90, 1.0),
    "wall": (0.66, 0.72, 0.80, 1.0),
    "pillar": (0.92, 0.80, 0.58, 1.0),
    "ceiling": (0.46, 0.48, 0.54, 1.0),
}


def texcoord(point, kind, triangle):
    if kind in ("floor", "ceiling"):
        return (point[0] / TILE, point[2] / TILE)
    # A wall or a pillar side: along the wall, and up it.
    along = point[2] if triangle[0][0] == triangle[1][0] == triangle[2][0] else point[0]
    return (along / TILE, point[1] / TILE)


def scene_glb(visual) -> bytes:
    corners = []
    for triangle, kind in visual:
        for point in triangle:
            corners.append((point, texcoord(point, kind, triangle), TINT[kind]))
    primitive = la.Primitive(material="blank_room_tile", image_name="blank_room_tile", image_png=tile_png(),
                             corners=corners, room=1)
    return la.scene_glb([primitive], "Perfect Dark 2 Blank Room builder", "blank_room", "blank_room_mesh")


# ---------------------------------------------------------------------------
# Starts
# ---------------------------------------------------------------------------

def start_places():
    """Eight starts on a ring, in order round it, each facing the middle.
    None is near the origin (the spawn pool marks points there down), each is
    at least 150 from a wall or a pillar, and neighbours see each other, so
    the links the game makes between consecutive pads cross no wall."""
    places = []
    for index in range(STARTS):
        angle = 2.0 * math.pi * index / STARTS
        x, z = RING * math.cos(angle), RING * math.sin(angle)
        length = math.hypot(x, z)
        places.append(((round(x, 3), la.PAD_ABOVE_FLOOR, round(z, 3)),
                       (round(-x / length, 6), 0.0, round(-z / length, 6))))
    return places


def room_checks(members: dict, level: la.Level) -> None:
    """The rules of this room's own geometry: upright walls, and starts clear
    of the walls, the pillars and the ceiling."""
    for triangle, kind in level.collision:
        if kind != "floor":
            normal, _length = la.unit_normal(triangle)
            require(abs(normal[1]) < 0.01, "a wall triangle is not upright")
    for index, row in enumerate(json.loads(members["pads.json"])["rows"]):
        x, y, z = row["position"]
        require(max(abs(x), abs(z)) <= HALF - 150, "pad %d: too near a wall" % index)
        require(0 < y < HEIGHT - 30, "pad %d: not between floor and ceiling" % index)
        for cx in (-PILLAR_AT, PILLAR_AT):
            for cz in (-PILLAR_AT, PILLAR_AT):
                require(max(abs(x - cx), abs(z - cz)) >= PILLAR_HALF + 150, "pad %d: too near a pillar" % index)


def build():
    collision, visual = build_surfaces()
    level = la.Level(
        arena_id=ARENA_ID,
        scenario_id=SCENARIO_ID,
        scenario_member=SCENARIO_MEMBER,
        display_name=DISPLAY_NAME,
        category="Templates",
        arena_comment="blank_room.pdarena - the Blank Room, a level template that holds nothing of the original game",
        scenario_comment="scenario_blank_room.pdscenario - the Blank Room, an original one-room level",
        collision_comment="Blank Room collision. Original geometry, made by dev-mods/blank_room/build_blank_room.py.",
        scene=scene_glb(visual),
        collision=collision,
        places=start_places(),
        room_low=(-HALF, 0.0, -HALF),
        room_high=(HALF, HEIGHT, HALF),
        room_origin=(0.0, HEIGHT / 2.0, 0.0),
    )
    arena, members = la.build_level(level, room_checks)
    mod = json.dumps({
        "id": MOD_ID,
        "name": "Blank Room (level template)",
        "version": "0.1.0",
        "author": "PD2",
        "description": "A small original level: one room with four pillars and eight player starts, and "
                       "nothing of the original game in it. Open it in Forge to make a level that can be shared.",
        "tags": ["arena", "pdarena", "template"],
        "contents": ["arena"],
    }, indent=2) + "\n"
    return arena, mod, members


def main() -> int:
    check_only = "--check" in sys.argv[1:]
    try:
        arena, mod, members = build()
    except BuildError as error:
        print("BLANK ROOM: not built: %s" % error)
        return 1
    errors = la.conformance(arena, "blank_room.pdarena")
    for error in errors:
        print("conformance: " + error)
    if errors:
        print("BLANK ROOM: the archive validator refuses it (%d)" % len(errors))
        return 1
    if check_only:
        # Compared member by member, not as zip bytes: the compressed form can
        # differ between zlib versions while the level is the same.
        current = OUT_ARENA.is_file() and la.unpacked(OUT_ARENA.read_bytes()) == la.unpacked(arena) and \
            OUT_MOD.is_file() and OUT_MOD.read_text(encoding="utf-8") == mod
        print("BLANK ROOM: %s" % ("up to date" if current else "STALE: run the builder"))
        return 0 if current else 1
    OUT_ARENA.write_bytes(arena)
    OUT_MOD.write_text(mod, encoding="utf-8", newline="\n")
    graph = members["level.graph.json"]
    print("BLANK ROOM: built %s (%d bytes); scene %d bytes, graph %d bytes, %d pads, %d collision triangles; "
          "sha256 %s" % (OUT_ARENA.name, len(arena), len(members["scene.glb"]), len(graph),
                         len(json.loads(members["pads.json"])["rows"]),
                         len(json.loads(members["collision.flags.json"])["rows"]), la.sha(arena)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
