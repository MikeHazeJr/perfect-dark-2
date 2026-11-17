#!/usr/bin/env python3
"""Builds the Blank Room: a small level that holds nothing of the original game.

One room, 24 by 24 metres and 4 high, with four pillars for cover, eight
player starts on a ring and no objects. It is the level template Forge starts
an original level from (context/designs/modding/forge-mode-scope.md, step F5):
every level cloned from a base arena embeds that arena's geometry and so can
never be shared, and until this one there was no playable level free of the
original game's data.

Everything in the archive is made here: the walls and floor (scene.glb), their
texture (a tile drawn by this script), the collision (collision.obj and its
flags), the pads, starts and bounds, and the level's graph. The only things
taken from elsewhere are structural and come from this repository's own
example level, examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena,
which CORE's builder (tools/build_typed_pdxxx_examples.py) keeps in step with
the game: the graph's 422 AI kind nodes and their fixed links, the pickup and
door rule nodes and edges, and the cache kind an arena.ini names. So when the
level formats move, rebuild the example and then this.

Usage:
    python dev-mods/blank_room/build_blank_room.py            build and check
    python dev-mods/blank_room/build_blank_room.py --check    check the built
                                                              file is current

The rules each member has to keep, with the file and line of the code that
enforces them, are in forge-mode-scope.md ("F5: the smallest level the game
accepts"). The checks at the end of this script mirror them, and the archive
is then given to tools/asset_archive_conformance.py.
"""

from __future__ import annotations

import hashlib
import io
import json
import math
import struct
import sys
import zipfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
EXAMPLE_ARENA = REPO / "examples" / "modding" / "typed-pdxxx-basic" / "arenas" / "tri_arena.pdarena"
OUT_ARENA = HERE / "blank_room.pdarena"
OUT_MOD = HERE / "mod.json"

MOD_ID = "blank_room"
ARENA_ID = "mod_blank_room:arena_blank_room"
SCENARIO_ID = "mod_blank_room:scenario_blank_room"
SCENARIO_MEMBER = "dependencies/assets/scenarios/scenario_blank_room.pdscenario"
DISPLAY_NAME = "Blank Room"
ZIP_TIME = (1980, 1, 1, 0, 0, 0)

# The room, in world units (1 unit is 1 cm; a player stands 172 tall).
HALF = 1200.0          # the floor runs from -HALF to +HALF on x and z
HEIGHT = 400.0         # floor to ceiling
CELL = 600.0           # floor, wall and ceiling quads are this wide
PILLAR_AT = 350.0      # pillar centres are at (+-PILLAR_AT, +-PILLAR_AT)
PILLAR_HALF = 80.0
RING = 850.0           # player starts stand on a ring of this radius
STARTS = 8
PAD_ABOVE_FLOOR = 50.0  # base start pads sit about this far above their floor
TILE = 200.0           # the texture repeats every TILE units

FLOOR_FLAGS = "0x001b"  # FLOOR1 | FLOOR2 | BLOCK_SIGHT | BLOCK_SHOOT, as base floors
WALL_FLAGS = "0x001c"   # WALL | BLOCK_SIGHT | BLOCK_SHOOT, as base walls


class BuildError(Exception):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise BuildError(message)


# ---------------------------------------------------------------------------
# Geometry
# ---------------------------------------------------------------------------

def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


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


def number(value: float) -> str:
    """A coordinate the way the extractor writes one."""
    return "%.6f" % (0.0 if value == 0 else value)


def collision_obj(collision) -> str:
    # One room, named before the first vertex; no other o or g line, no
    # mtllib. Every triangle is in room 1: a triangle in room 0 overruns the
    # game's tile buffer.
    lines = ["# Blank Room collision. Original geometry, made by dev-mods/blank_room/build_blank_room.py.",
             "o room_1"]
    for index, (triangle, _kind) in enumerate(collision):
        for point in triangle:
            lines.append("v %s %s %s" % (number(point[0]), number(point[1]), number(point[2])))
        base = index * 3
        lines.append("f %d %d %d" % (base + 1, base + 2, base + 3))
    return "\n".join(lines) + "\n"


def collision_flags(collision) -> str:
    # One flat row per triangle, in the order of collision.obj. The reader
    # takes every "{" after "rows" as a row, so nothing with a brace follows.
    rows = []
    for _triangle, kind in collision:
        flags = FLOOR_FLAGS if kind == "floor" else WALL_FLAGS
        rows.append('    { "flags": "%s", "floortype": 0, "floorcol": 4095 }' % flags)
    return ('{\n  "schema": "pd2.scenario.collision.flags.v1",\n  "rows": [\n' + ",\n".join(rows) + "\n  ]\n}\n")


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


def pad4(data: bytes, fill: bytes = b"\x00") -> bytes:
    return data + fill * ((4 - len(data) % 4) % 4)


def scene_glb(visual) -> bytes:
    """An unindexed triangle list with POSITION, TEXCOORD_0, COLOR_0 as four
    floats and _PD_ROOM as an unsigned short: the forms both of the game's
    readers of a scene take (the compiler and the renderer)."""
    positions, texcoords, colours, rooms = bytearray(), bytearray(), bytearray(), bytearray()
    low = [math.inf] * 3
    high = [-math.inf] * 3
    count = 0
    for triangle, kind in visual:
        for point in triangle:
            positions += struct.pack("<3f", *point)
            texcoords += struct.pack("<2f", *texcoord(point, kind, triangle))
            colours += struct.pack("<4f", *TINT[kind])
            rooms += struct.pack("<H", 1)
            for axis in range(3):
                low[axis] = min(low[axis], point[axis])
                high[axis] = max(high[axis], point[axis])
            count += 1
    image = tile_png()
    views, blob = [], bytearray()
    for data, target in ((positions, 34962), (texcoords, 34962), (colours, 34962), (rooms, 34962), (image, None)):
        view = {"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        views.append(view)
        blob += pad4(bytes(data))
    document = {
        "asset": {"version": "2.0", "generator": "Perfect Dark 2 Blank Room builder"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "blank_room", "mesh": 0}],
        "meshes": [{"name": "blank_room_mesh", "primitives": [{
            "attributes": {"POSITION": 0, "TEXCOORD_0": 1, "COLOR_0": 2, "_PD_ROOM": 3},
            "material": 0, "mode": 4}]}],
        "materials": [{"name": "blank_room_tile", "alphaMode": "OPAQUE",
                       "pbrMetallicRoughness": {"baseColorTexture": {"index": 0, "texCoord": 0},
                                                "metallicFactor": 0.0, "roughnessFactor": 1.0}}],
        "textures": [{"sampler": 0, "source": 0}],
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "images": [{"name": "blank_room_tile", "mimeType": "image/png", "bufferView": 4}],
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": views,
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": count, "type": "VEC3",
             "min": [float(v) for v in low], "max": [float(v) for v in high]},
            {"bufferView": 1, "componentType": 5126, "count": count, "type": "VEC2"},
            {"bufferView": 2, "componentType": 5126, "count": count, "type": "VEC4"},
            {"bufferView": 3, "componentType": 5123, "count": count, "type": "SCALAR"},
        ],
    }
    text = pad4(json.dumps(document, separators=(",", ":")).encode("utf-8"), b" ")
    body = (struct.pack("<I4s", len(text), b"JSON") + text + struct.pack("<I4s", len(blob), b"BIN\x00") + bytes(blob))
    return struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body


# ---------------------------------------------------------------------------
# Pads, starts, bounds and the small tables
# ---------------------------------------------------------------------------

def vector(values) -> str:
    return "[" + ", ".join(number(v) for v in values) + "]"


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
        places.append(((round(x, 3), PAD_ABOVE_FLOOR, round(z, 3)), (round(-x / length, 6), 0.0, round(-z / length, 6))))
    return places


def rows_table(schema: str, rows: list[str]) -> str:
    if not rows:
        return '{\n  "schema": "%s",\n  "rows": [\n  ]\n}\n' % schema
    return '{\n  "schema": "%s",\n  "rows": [\n%s\n  ]\n}\n' % (schema, ",\n".join(rows))


def pads_json(places) -> str:
    rows = []
    for index, (position, look) in enumerate(places):
        # room_1 is written out, so a start's room does not depend on the room
        # boxes. A pad Forge adds leaves it empty, as base pads do, and the
        # game then finds the room from rooms.json's box. Flags 0: the game
        # strips a base pad's alignment bits anyway.
        rows.append('    { "pad_ref": "pad_%04d", "room_ref": "room_1", "liftnum": 0, "flags": "0x00000", '
                    '"position": %s, "up": [0.000000, 1.000000, 0.000000], "look": %s, '
                    '"bbox": { "min": [-100.000000, -100.000000, -100.000000], '
                    '"max": [100.000000, 100.000000, 100.000000] } }'
                    % (index, vector(position), vector(look)))
    return rows_table("pd2.scenario.pads.v1", rows)


def spawns_json(places) -> str:
    rows = ['    { "spawn_id": "spawn_%04d", "pad_ref": "pad_%04d", "room_ref": "", "team": "any", '
            '"profile": "intro", "position": null, "look": null }' % (index, index) for index in range(len(places))]
    return rows_table("pd2.scenario.spawns.v1", rows)


def volumes_json(places) -> str:
    rows = []
    for index, (position, _look) in enumerate(places):
        rows.append('    { "volume_id": "volume_pad_%04d", "pad_ref": "pad_%04d", "kind": "pad_bounds", '
                    '"room_ref": "", "shape": "aabb", "min": %s, "max": %s }'
                    % (index, index, vector([v - 100.0 for v in position]), vector([v + 100.0 for v in position])))
    return rows_table("pd2.scenario.volumes.v1", rows)


def paths_json(places) -> str:
    # One closed path round the ring (flags 1): with the waypoint tables empty
    # the game makes a waypoint of every pad and links the pads of each path
    # in order, so simulants can walk the ring both ways.
    pads = ", ".join('"pad_%04d"' % index for index in range(len(places)))
    return rows_table("pd2.scenario.paths.v1", ['    { "path_ref": "path_0000", "flags": 1, "pads": [%s] }' % pads])


def rooms_json() -> str:
    # The room's box, written out so that a pad with no room of its own is
    # found inside it whatever the collision triangles are.
    return rows_table("pd2.scenario.rooms.v1", [
        '    { "room": "room_1", "roomnum": 1, "origin": %s, "min": %s, "max": %s, "brightness": [128, 255] }'
        % (vector((0.0, HEIGHT / 2.0, 0.0)), vector((-HALF, 0.0, -HALF)), vector((HALF, HEIGHT, HALF)))])


# The list every base multiplayer arena has: it starts the match's simulants
# and builds the teams, then hands the list's owner to the game's global list
# 0. Ids below 0x401 are the game's own lists; this one is the stage's.
AI_COMMANDS = [
    (0, "0x0185", "mp_init_simulants", "scenario.ai.action.mp_init_simulants", "{}", ""),
    (2, "0x0145", "rebuild_teams", "scenario.ai.action.rebuild_teams", "{}", ""),
    (4, "0x0146", "rebuild_squadrons", "scenario.ai.action.rebuild_squadrons", "{}", ""),
    (6, "0x0005", "set_list", "scenario.ai.action.set_list", '{"chr": 253, "ai_list": 0}', ', "switches_list": true'),
    (11, "0x0004", "end", "scenario.ai.control.end", "{}", ', "ends": true, "reachable": false'),
]


def command_id(index: int) -> str:
    return "scenario.ai.ailist_0000.command.%04d" % index


def ai_lists_json() -> str:
    rows = []
    for index, (offset, opcode, name, _semantic, args, _extra) in enumerate(AI_COMMANDS):
        rows.append('    { "ailist_ref": "ailist_0000", "list_id": "0x1000", "graph_node": "%s", '
                    '"command_index": %d, "offset": %d, "opcode": "%s", "opcode_name": "%s", "args": %s }'
                    % (command_id(index), index, offset, opcode, name, args))
    return rows_table("pd2.scenario.ai.lists.v1", rows)


NAVIGATION_INI = (
    "[navigation]\n"
    "source = scene.glb\n"
    "collision_source = collision.obj\n"
    "generator = deterministic.surface_graph.v1\n"
    "supports_walk = true\n"
    "supports_jump = true\n"
    "supports_drop = true\n"
    "supports_wall = true\n"
    "supports_ceiling = true\n"
    "portals_file = portals.json\n"
    "pads_file = pads.json\n"
    "spawns_file = spawns.json\n"
    "volumes_file = volumes.json\n"
    "waypoints_file = navigation/waypoints.json\n"
    "waygroups_file = navigation/waygroups.json\n"
    "covers_file = navigation/covers.json\n"
    "paths_file = navigation/paths.json\n"
    "generated_cache = _meta/generated-navmesh.json\n"
)

SCENARIO_INI = (
    "; scenario_blank_room.pdscenario - the Blank Room, an original one-room level\n"
    "[scenario]\n"
    "catalog_id = " + SCENARIO_ID + "\n"
    "name = " + DISPLAY_NAME + "\n"
    "mode = mp\n"
    "source_room_count = 2\n"
    "scene_file = scene.glb\n"
    "scene_format = GLB\n"
    "runtime_source_file = scene.glb\n"
    "collision_source = collision.obj\n"
    "collision_fallback = override\n"
    "pads_file = pads.json\n"
    "spawns_file = spawns.json\n"
    "volumes_file = volumes.json\n"
    "waypoints_file = navigation/waypoints.json\n"
    "waygroups_file = navigation/waygroups.json\n"
    "covers_file = navigation/covers.json\n"
    "paths_file = navigation/paths.json\n"
    "portals_file = portals.json\n"
    "objects_file = objects.json\n"
    "setup_fields_file = setup.fields.json\n"
    "ai_lists_file = ai/ailists.json\n"
    "objectives_file = objectives.json\n"
    "navigation_file = navigation.ini\n"
    "level_graph_file = level.graph.json\n"
    "\n[meta]\n"
    "manifest = _meta/manifest.json\n"
)

SCENARIO_MANIFEST = (
    "{\n"
    '  "pd_kind": "scenario",\n'
    '  "pd_schema_version": 1,\n'
    '  "id": "' + SCENARIO_ID + '",\n'
    '  "room_count": 2,\n'
    '  "scene": "scene.glb",\n'
    '  "scene_format": "GLB",\n'
    '  "runtime_source": "scene.glb",\n'
    '  "collision_source": "collision.obj",\n'
    '  "collision_fallback": "override",\n'
    '  "portals": "portals.json",\n'
    '  "portal_count": 0,\n'
    '  "navigation": "navigation.ini",\n'
    '  "level_graph": "level.graph.json",\n'
    '  "pads": "pads.json",\n'
    '  "spawns": "spawns.json",\n'
    '  "volumes": "volumes.json",\n'
    '  "waypoints": "navigation/waypoints.json",\n'
    '  "waygroups": "navigation/waygroups.json",\n'
    '  "covers": "navigation/covers.json",\n'
    '  "paths": "navigation/paths.json",\n'
    '  "objects": "objects.json",\n'
    '  "setup_fields": "setup.fields.json",\n'
    '  "ai_lists": "ai/ailists.json",\n'
    '  "objectives": "objectives.json"\n'
    "}\n"
)


def sha(data) -> str:
    if isinstance(data, str):
        data = data.encode("utf-8")
    return hashlib.sha256(data).hexdigest()


def navmesh_stamp(members: dict[str, bytes], counts: dict[str, int]) -> str:
    """The navigation record. The game compares the counts and hashes lines
    with its own, character for character, so they are written by hand in the
    form the extractor and the archive rebuild write."""
    names = ["scene.glb", "collision.obj", "navigation.ini", "portals.json", "pads.json", "spawns.json",
             "volumes.json", "navigation/waypoints.json", "navigation/waygroups.json", "navigation/covers.json",
             "navigation/paths.json"]
    hashes = ", ".join('"%s": "%s"' % (name, sha(members[name])) for name in names)
    return (
        "{\n"
        '  "schema": "pd2.generated.navmesh.v1",\n'
        '  "scenario": "' + SCENARIO_ID + '",\n'
        '  "derived_from": "scene.glb",\n'
        '  "inputs": [' + ", ".join('"%s"' % name for name in names) + "],\n"
        '  "generator": "deterministic.surface_graph.v1",\n'
        '  "capabilities": ["walk", "jump", "drop", "wall", "ceiling"],\n'
        '  "source_counts": { "pads": %d, "volumes": %d, "waypoints": %d, "waygroups": %d, "covers": %d, "paths": %d },\n'
        % (counts["pads"], counts["volumes"], counts["waypoints"], counts["waygroups"], counts["covers"],
           counts["paths"]) +
        '  "source_hashes": { ' + hashes + " },\n"
        '  "cache_only": true\n'
        "}\n"
    )


# ---------------------------------------------------------------------------
# The level's graph
# ---------------------------------------------------------------------------

def read_example():
    require(EXAMPLE_ARENA.is_file(), "the example level is missing: %s" % EXAMPLE_ARENA)
    with zipfile.ZipFile(EXAMPLE_ARENA) as outer:
        arena_ini = outer.read("arena.ini").decode("utf-8")
        inner_name = next(name for name in outer.namelist() if name.endswith(".pdscenario"))
        with zipfile.ZipFile(io.BytesIO(outer.read(inner_name))) as inner:
            graph = json.loads(inner.read("level.graph.json").decode("utf-8"))
    cache_kind = ""
    for line in arena_ini.splitlines():
        key, _, value = line.partition("=")
        if key.strip() == "scenario_graph_cache":
            cache_kind = value.strip()
    require(cache_kind != "", "the example arena.ini names no scenario_graph_cache")
    return graph, cache_kind


def line_object(fields: dict) -> str:
    """One node or link on a line, the way the extractor lays them out. A
    node's kind is written `"kind": "x"` with one space: the game counts node
    kinds as text in that exact form."""
    return "    { " + ", ".join('"%s": %s' % (key, json.dumps(value)) for key, value in fields.items()) + " }"


def level_graph(example: dict, places, triangle_count: int) -> str:
    pads = len(places)
    own = {
        "scenario.load": {"id": "scenario.load", "kind": "event.scenario.load"},
        "source.scene": {"id": "source.scene", "kind": "scenario.scene.source", "file": "scene.glb"},
        "collision.generate": {"id": "collision.generate", "kind": "scenario.collision.generate",
                               "source": "collision.obj", "cache": "_meta/generated-collision.json"},
        "scenario.portals": {"id": "scenario.portals", "kind": "scenario.portals.source",
                             "source": "portals.json", "portals": 0},
        "navigation.generate": {"id": "navigation.generate", "kind": "scenario.navigation.generate",
                                "source": "navigation.ini", "cache": "_meta/generated-navmesh.json"},
        "scenario.pads": {"id": "scenario.pads", "kind": "scenario.pads.source", "source": "pads.json",
                          "pads": pads},
        "navigation.paths": {"id": "navigation.paths", "kind": "scenario.navigation.paths.source",
                             "source": "navigation/paths.json", "paths": 1},
        "setup.tables": {"id": "setup.tables", "kind": "scenario.setup.tables", "objects": "objects.json",
                         "fields": "setup.fields.json", "objectives": "objectives.json"},
        "scenario.ai.lists": {"id": "scenario.ai.lists", "kind": "scenario.ai.lists.source",
                              "source": "ai/ailists.json", "lists": 1},
        "scenario.global.settings": {"id": "scenario.global.settings", "kind": "scenario.global.settings.source",
                                     "scenario": SCENARIO_ID, "source": "scenario.ini", "scene": "scene.glb",
                                     "collision": "collision.obj", "navigation": "navigation.ini",
                                     "pads": pads, "volumes": pads},
    }
    # The example's nodes in its order, with this level's own version of each
    # structural node in place of the example's, and without the example's
    # trigger volumes and AI commands, which are its level's.
    dropped = ("scenario.trigger.volume.source", "scenario.ai.command")
    nodes, seen = [], set()
    for node in example["nodes"]:
        if node.get("kind") in dropped:
            continue
        node = own.get(node["id"], node)
        require(node["id"] not in seen, "the example graph lists %s twice" % node["id"])
        seen.add(node["id"])
        nodes.append(node)
    # Structural nodes the example lacks (it is not a playable level) go where
    # the extractor puts them.
    for wanted, after in (("collision.generate", "source.scene"), ("navigation.generate", "scenario.portals"),
                          ("setup.tables", "navigation.paths")):
        if wanted not in seen:
            at = next(i for i, node in enumerate(nodes) if node["id"] == after)
            nodes.insert(at + 1, own[wanted])
            seen.add(wanted)
    for wanted in own:
        require(wanted in seen, "the graph has no %s node" % wanted)
    for index, (offset, opcode, name, semantic, args, _extra) in enumerate(AI_COMMANDS):
        node = {"id": command_id(index), "kind": "scenario.ai.command", "source": "ai/ailists.json",
                "ailist_ref": "ailist_0000", "list_id": "0x1000", "command_index": index, "offset": offset,
                "opcode": opcode, "opcode_name": name, "semantic_kind": semantic, "args": json.loads(args)}
        if name == "set_list":
            node["switches_list"] = True
        if name == "end":
            node["ends"] = True
            node["reachable"] = False
        nodes.append(node)
        seen.add(node["id"])
    for index in range(pads):
        nodes.append({"id": "trigger.volume.%04d" % index, "kind": "scenario.trigger.volume.source",
                      "table": "volumes.json", "volume": "volume_pad_%04d" % index, "pad": "pad_%04d" % index})
        seen.add("trigger.volume.%04d" % index)

    # The example's links between nodes this graph still has (the fixed ones
    # the archive validator asks for), then this level's own.
    links = [link for link in example.get("links", []) if link["from"] in seen and link["to"] in seen
             and not link["to"].startswith(("trigger.volume.", "scenario.ai.ailist_"))
             and not link["from"].startswith("scenario.ai.ailist_")]
    for index in range(pads):
        links.append({"from": "scenario.load", "to": "trigger.volume.%04d" % index})
    for index in range(len(AI_COMMANDS)):
        links.append({"from": "scenario.ai.lists", "to": command_id(index)})
    for index in range(3):
        links.append({"from": command_id(index), "to": command_id(index + 1), "kind": "next"})

    edges = example.get("edges", [])
    counts = dict(example["counts"])
    counts.update({"rooms": 2, "triangles": triangle_count, "pads": pads, "volumes": pads, "objects": 0,
                   "objectives": 0, "ai_lists": 1, "ai_commands": len(AI_COMMANDS), "waypoints": 0,
                   "waygroups": 0, "covers": 0, "paths": 1})
    if "portals" in counts:
        counts["portals"] = 0

    tables = example["tables"]
    out = ["{", '  "schema": "pd2.level.graph.v1",', '  "scenario": "%s",' % SCENARIO_ID,
           '  "source": "scene.glb",', '  "kind": "mp",', '  "tables": {']
    out.append(",\n".join('    "%s": %s' % (key, json.dumps(value)) for key, value in tables.items()))
    out.append("  },")
    out.append('  "nodes": [')
    out.append(",\n".join(line_object(node) for node in nodes))
    out.append("  ],")
    out.append('  "links": [')
    out.append(",\n".join(line_object(link) for link in links))
    out.append("  ],")
    if edges:
        out.append('  "edges": [')
        out.append(",\n".join(line_object(edge) for edge in edges))
        out.append("  ],")
    out.append('  "counts": { ' + ", ".join('"%s": %d' % (key, value) for key, value in counts.items()) + " }")
    out.append("}")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# Archives
# ---------------------------------------------------------------------------

def zip_bytes(entries) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in entries:
            if isinstance(data, str):
                data = data.encode("utf-8")
            info = zipfile.ZipInfo(name, ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    return buffer.getvalue()


def build():
    example, cache_kind = read_example()
    collision, visual = build_surfaces()
    places = start_places()

    members: dict[str, bytes] = {}

    def put(name: str, data) -> None:
        members[name] = data.encode("utf-8") if isinstance(data, str) else data

    put("scenario.ini", SCENARIO_INI)
    put("scene.glb", scene_glb(visual))
    put("collision.obj", collision_obj(collision))
    put("collision.flags.json", collision_flags(collision))
    put("rooms.json", rooms_json())
    put("portals.json", rows_table("pd2.scenario.portals.v1", []))
    put("pads.json", pads_json(places))
    put("spawns.json", spawns_json(places))
    put("volumes.json", volumes_json(places))
    put("navigation/waypoints.json", rows_table("pd2.scenario.waypoints.v1", []))
    put("navigation/waygroups.json", rows_table("pd2.scenario.waygroups.v1", []))
    put("navigation/covers.json", rows_table("pd2.scenario.covers.v1", []))
    put("navigation/paths.json", paths_json(places))
    put("objects.json", rows_table("pd2.scenario.objects.v1", []))
    put("setup.fields.json", rows_table("pd2.scenario.setup.fields.v1", []))
    put("ai/ailists.json", ai_lists_json())
    put("objectives.json", rows_table("pd2.scenario.objectives.v1", []))
    put("navigation.ini", NAVIGATION_INI)
    put("level.graph.json", level_graph(example, places, len(collision)))
    put("_meta/generated-navmesh.json", navmesh_stamp(members, {
        "pads": len(places), "volumes": len(places), "waypoints": 0, "waygroups": 0, "covers": 0, "paths": 1}))
    put("_meta/manifest.json", SCENARIO_MANIFEST)

    check_scenario(members, collision, places)
    scenario = zip_bytes(members.items())

    arena_ini = (
        "; blank_room.pdarena - the Blank Room, a level template that holds nothing of the original game\n"
        "[arena]\n"
        "catalog_id = " + ARENA_ID + "\n"
        "display_name = " + DISPLAY_NAME + "\n"
        "category = Templates\n"
        "load_mode = ARENA_LOADMODE_PLAYABLE\n"
        "scenario = " + SCENARIO_ID + "\n"
        "scenario_archive = " + SCENARIO_MEMBER + "\n"
        "scenario_graph_cache = " + cache_kind + "\n"
        "\n[meta]\n"
        "manifest = _meta/manifest.json\n"
    )
    arena_manifest = (
        "{\n"
        '  "schema": "pd.asset_archive.manifest.v1",\n'
        '  "pd_kind": "arena",\n'
        '  "catalog_id": "' + ARENA_ID + '",\n'
        '  "scenario": "' + SCENARIO_ID + '",\n'
        '  "scenario_archive": "' + SCENARIO_MEMBER + '"\n'
        "}\n"
    )
    arena = zip_bytes([("arena.ini", arena_ini), (SCENARIO_MEMBER, scenario), ("_meta/manifest.json", arena_manifest)])
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


# ---------------------------------------------------------------------------
# Checks: each mirrors a rule the game enforces
# ---------------------------------------------------------------------------

def graph_kind_needles() -> list[str]:
    """The node kinds the game counts in a level graph, read from the game's
    own source so the list is the one of this commit."""
    import re
    source = (REPO / "port" / "src" / "scenario_source_runtime.c").read_text(encoding="utf-8", errors="replace")
    found = re.findall(r's_countTextOccurrences\(\s*text\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)', source)
    needles = []
    for item in found:
        text = item.replace('\\"', '"')
        # The level graph's kinds all begin "scenario."; the same file counts
        # a mission graph's kinds ("mission.") in another text.
        if text.startswith('"kind": "scenario.') and text not in needles:
            needles.append(text)
    return needles


def check_scenario(members: dict[str, bytes], collision, places) -> None:
    graph = members["level.graph.json"].decode("utf-8")
    json.loads(graph)  # strict JSON, one object

    # The graph, as the game reads it: as text.
    require("pd2.level.graph.v1" in graph and SCENARIO_ID in graph, "graph: schema or scenario id missing")
    require('"nodes": []' not in graph and '"nodes":[]' not in graph, "graph: empty nodes")
    needles = graph_kind_needles()
    require(len(needles) >= 400, "only %d node kinds were found in the game's source" % len(needles))
    volume_needle = '"kind": "scenario.trigger.volume.source"'
    for needle in needles:
        wanted = len(places) if needle == volume_needle else 1
        require(graph.count(needle) == wanted, "graph: %s stands %d times, the game wants %d"
                % (needle, graph.count(needle), wanted))
    first = graph.index('"scenario"')
    require(graph[first:].startswith('"scenario": "%s"' % SCENARIO_ID), "graph: the first scenario key is not the id")
    nodes_at = graph.index('"nodes"')
    for key in ("portals", "pads", "spawns", "objects", "setup_fields", "ai_lists", "volumes", "objectives",
                "waypoints", "waygroups", "covers", "paths"):
        at = graph.index('"%s"' % key)
        require(at < nodes_at, "graph: the first \"%s\" is not in tables" % key)
        rest = graph[at + len(key) + 2:].lstrip()
        require(rest.startswith(":") and rest[1:].lstrip().startswith('"'), "graph: table %s is not a string" % key)
    for text in ('"id": "navigation.generate"', '"kind": "scenario.navigation.generate"',
                 '"source": "navigation.ini"', '"cache": "_meta/generated-navmesh.json"'):
        require(text in graph, "graph: missing %s" % text)
    for event in ("event.pickup.touched", "event.door.use", "event.door.approach"):
        require('"kind": "%s"' % event in graph, "graph: no %s node (the pickup and door rules)" % event)

    # navigation.ini, by line.
    ini_lines = members["navigation.ini"].decode("utf-8").splitlines()
    for line in ("[navigation]", "source = scene.glb", "collision_source = collision.obj",
                 "generator = deterministic.surface_graph.v1", "supports_walk = true", "supports_jump = true",
                 "supports_drop = true", "supports_wall = true", "supports_ceiling = true",
                 "generated_cache = _meta/generated-navmesh.json"):
        require(line in ini_lines, "navigation.ini: missing line %s" % line)

    # Every file an ini names is a member; nothing the release rules refuse.
    names = set(members)
    for ini in ("scenario.ini", "navigation.ini"):
        for line in members[ini].decode("utf-8").splitlines():
            key, sep, value = line.partition("=")
            key, value = key.strip(), value.strip()
            if not sep or not value or not (key.endswith("_file") or key in ("manifest", "collision_source")):
                continue
            require(value in names, "%s names %s, which is not a member" % (ini, value))
    for name in names:
        require(not name.endswith((".tsv", ".bin", ".sha256")), "member %s would be refused" % name)
    require(b"mtllib" not in members["collision.obj"], "collision.obj must not name a material library")

    # Collision: one room above 0, floors wound upward, a flags row each.
    obj_lines = members["collision.obj"].decode("utf-8").splitlines()
    groups = [line for line in obj_lines if line.startswith(("o ", "g "))]
    require(groups == ["o room_1"], "collision.obj: rooms are %s" % groups)
    first_vertex = next(i for i, line in enumerate(obj_lines) if line.startswith("v "))
    require(obj_lines.index("o room_1") < first_vertex, "collision.obj: the room is named after a vertex")
    for triangle, kind in collision:
        normal = cross(sub(triangle[1], triangle[0]), sub(triangle[2], triangle[0]))
        length = math.sqrt(dot(normal, normal))
        require(length > 1.0, "a collision triangle is degenerate")
        if kind == "floor":
            require(normal[1] / length >= 0.5, "a floor triangle does not face up")
        else:
            require(abs(normal[1]) / length < 0.01, "a wall triangle is not upright")
    flags_text = members["collision.flags.json"].decode("utf-8")
    require(len(json.loads(flags_text)["rows"]) == len(collision), "collision.flags.json: one row per triangle")
    require("{" not in flags_text[flags_text.rindex("]"):], "collision.flags.json: a brace follows the rows")

    # The scene, as both readers take it.
    glb = members["scene.glb"]
    magic, version, length = struct.unpack("<4sII", glb[:12])
    require(magic == b"glTF" and version == 2 and length == len(glb), "scene.glb: bad header")
    json_length, json_kind = struct.unpack("<I4s", glb[12:20])
    require(json_kind == b"JSON" and json_length % 4 == 0, "scene.glb: bad JSON chunk")
    document = json.loads(glb[20:20 + json_length])
    bin_length, bin_kind = struct.unpack("<I4s", glb[20 + json_length:28 + json_length])
    require(bin_kind == b"BIN\x00" and bin_length % 4 == 0, "scene.glb: bad BIN chunk")
    require(28 + json_length + bin_length == len(glb), "scene.glb: chunks do not add up")
    require("extensionsRequired" not in document, "scene.glb: extensionsRequired is refused")
    require(len(document["buffers"]) == 1 and "uri" not in document["buffers"][0], "scene.glb: one embedded buffer")
    require(0 <= bin_length - document["buffers"][0]["byteLength"] <= 3, "scene.glb: buffer length")
    for accessor in document["accessors"]:
        require(all(key in accessor for key in ("bufferView", "componentType", "count", "type")) and
                accessor["count"] > 0, "scene.glb: an accessor lacks a field")
        require(not accessor.get("normalized") and "sparse" not in accessor, "scene.glb: accessor form")
    primitive = document["meshes"][0]["primitives"][0]
    forms = {"POSITION": (5126, "VEC3"), "TEXCOORD_0": (5126, "VEC2"), "COLOR_0": (5126, "VEC4"),
             "_PD_ROOM": (5123, "SCALAR")}
    count = document["accessors"][primitive["attributes"]["POSITION"]]["count"]
    require(count % 3 == 0, "scene.glb: the vertex count is not whole triangles")
    for name, (component, kind) in forms.items():
        accessor = document["accessors"][primitive["attributes"][name]]
        require((accessor["componentType"], accessor["type"], accessor["count"]) == (component, kind, count),
                "scene.glb: %s is not the form the game reads" % name)
    for view in document["bufferViews"]:
        require(view["buffer"] == 0 and view["byteLength"] > 0 and
                view.get("byteOffset", 0) + view["byteLength"] <= document["buffers"][0]["byteLength"],
                "scene.glb: a buffer view is out of range")
    image = document["bufferViews"][document["images"][0]["bufferView"]]
    data = glb[28 + json_length + image["byteOffset"]:28 + json_length + image["byteOffset"] + image["byteLength"]]
    require(data.startswith(b"\x89PNG\r\n\x1a\n"), "scene.glb: the image is not a PNG")

    # Pads and starts.
    pad_rows = json.loads(members["pads.json"])["rows"]
    for index, row in enumerate(pad_rows):
        require(row["pad_ref"] == "pad_%04d" % index, "pads.json: row %d is %s" % (index, row["pad_ref"]))
        x, y, z = row["position"]
        require(max(abs(x), abs(z)) <= HALF - 150 and math.hypot(x, z) > 85, "pad %d: too near a wall or the origin" % index)
        require(0 < y < HEIGHT - 30, "pad %d: not between floor and ceiling" % index)
        for cx in (-PILLAR_AT, PILLAR_AT):
            for cz in (-PILLAR_AT, PILLAR_AT):
                require(max(abs(x - cx), abs(z - cz)) >= PILLAR_HALF + 150, "pad %d: too near a pillar" % index)
    for a in range(len(pad_rows)):
        for b in range(a + 1, len(pad_rows)):
            pa, pb = pad_rows[a]["position"], pad_rows[b]["position"]
            require(math.hypot(pa[0] - pb[0], pa[2] - pb[2]) >= 150, "pads %d and %d are too close" % (a, b))
    require(len(json.loads(members["spawns.json"])["rows"]) == len(places), "spawns.json: one start per pad")
    require(len(json.loads(members["volumes.json"])["rows"]) == len(pad_rows), "volumes.json: one row per pad")
    for name, text in members.items():
        if name.endswith(".json"):
            json.loads(text)
            require(b"\x00" not in text, "%s holds a NUL byte" % name)

    # The navigation record is the one the final bytes give.
    stamp = navmesh_stamp(members, {"pads": len(pad_rows), "volumes": len(pad_rows), "waypoints": 0,
                                    "waygroups": 0, "covers": 0, "paths": 1})
    require(members["_meta/generated-navmesh.json"].decode("utf-8") == stamp, "the navigation record is stale")
    require(ARENA_ID.split(":")[0] == SCENARIO_ID.split(":")[0] and not ARENA_ID.startswith("base:"),
            "the arena and its scenario must share a namespace that is not base")


def conformance(arena: bytes) -> list[str]:
    """The repository's archive validator, on the built arena, with the
    example level's ids known as they are when CORE checks a tree."""
    sys.path.insert(0, str(REPO / "tools"))
    try:
        import asset_archive_conformance as conf
    finally:
        sys.path.pop(0)
    known = set(conf.collect_source_catalog_ids())
    known.update(conf.collect_archive_catalog_ids(arena, ".pdarena", recurse=True))
    result = conf.validate_archive_bytes(arena, "blank_room.pdarena", ".pdarena", recurse=True,
                                         known_catalog_ids=known)
    return list(result.errors)


def unpacked(archive: bytes) -> dict:
    """Every member of an archive by name, with archives inside it unpacked
    the same way."""
    out = {}
    with zipfile.ZipFile(io.BytesIO(archive)) as outer:
        for name in outer.namelist():
            data = outer.read(name)
            out[name] = unpacked(data) if name.endswith(".pdscenario") else data
    return out


def main() -> int:
    check_only = "--check" in sys.argv[1:]
    try:
        arena, mod, members = build()
    except BuildError as error:
        print("BLANK ROOM: not built: %s" % error)
        return 1
    errors = conformance(arena)
    for error in errors:
        print("conformance: " + error)
    if errors:
        print("BLANK ROOM: the archive validator refuses it (%d)" % len(errors))
        return 1
    if check_only:
        # Compared member by member, not as zip bytes: the compressed form can
        # differ between zlib versions while the level is the same.
        current = OUT_ARENA.is_file() and unpacked(OUT_ARENA.read_bytes()) == unpacked(arena) and \
            OUT_MOD.is_file() and OUT_MOD.read_text(encoding="utf-8") == mod
        print("BLANK ROOM: %s" % ("up to date" if current else "STALE: run the builder"))
        return 0 if current else 1
    OUT_ARENA.write_bytes(arena)
    OUT_MOD.write_text(mod, encoding="utf-8", newline="\n")
    graph = members["level.graph.json"]
    print("BLANK ROOM: built %s (%d bytes); scene %d bytes, graph %d bytes, %d pads, %d collision triangles; "
          "sha256 %s" % (OUT_ARENA.name, len(arena), len(members["scene.glb"]), len(graph),
                         len(json.loads(members["pads.json"])["rows"]),
                         len(json.loads(members["collision.flags.json"])["rows"]), sha(arena)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
