#!/usr/bin/env python3
"""level_archive.py -- builds an original level's archive (.pdarena with its
.pdscenario inside) from geometry and player starts.

What a level looks like is the caller's: the visible triangles (with their
texture, texture coordinates, tint and room), the collision triangles and the
player starts. Everything else a level the game accepts needs is made here,
in the forms the game reads: the pads, starts, bounds and path tables, the AI
list every base arena has, the navigation files and record, the level's graph
and the two archives. The rules each member keeps, with the file and line of
the code that enforces them, are in context/designs/modding/forge-mode-scope.md
("F5: the smallest level the game accepts"); check_scenario mirrors them and
conformance() hands the archive to tools/asset_archive_conformance.py.

Callers:
    dev-mods/blank_room/build_blank_room.py   the Blank Room, drawn in code
    tools/build_level_from_model.py           a level from a model (.glb)

The only thing taken from elsewhere is structural and comes from this
repository's own example level (examples/modding/typed-pdxxx-basic/arenas/
tri_arena.pdarena), which CORE's builder keeps in step with the game: the
graph's AI kind nodes and fixed links, the pickup and door rule nodes and
edges, and the cache kind an arena.ini names.

Every level made here is one room (room 1): `source_room_count` 2, every
collision triangle in room_1, every pad in room_1, one room box. Levels of
several rooms need portals and are not made here yet.
"""

from __future__ import annotations

import hashlib
import io
import json
import math
import re
import struct
import sys
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
EXAMPLE_ARENA = REPO / "examples" / "modding" / "typed-pdxxx-basic" / "arenas" / "tri_arena.pdarena"
ZIP_TIME = (1980, 1, 1, 0, 0, 0)

FLOOR_FLAGS = "0x001b"  # FLOOR1 | FLOOR2 | BLOCK_SIGHT | BLOCK_SHOOT, as base floors
WALL_FLAGS = "0x001c"   # WALL | BLOCK_SIGHT | BLOCK_SHOOT, as base walls
PAD_ABOVE_FLOOR = 50.0  # base start pads sit about this far above their floor


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


def unit_normal(triangle):
    """The triangle's normal as (v1 - v0) x (v2 - v0), of unit length, and that
    cross product's length (twice the area)."""
    normal = cross(sub(triangle[1], triangle[0]), sub(triangle[2], triangle[0]))
    length = math.sqrt(dot(normal, normal))
    if length == 0.0:
        return (0.0, 0.0, 0.0), 0.0
    return (normal[0] / length, normal[1] / length, normal[2] / length), length


def number(value: float) -> str:
    """A coordinate the way the extractor writes one."""
    return "%.6f" % (0.0 if value == 0 else value)


def vector(values) -> str:
    return "[" + ", ".join(number(v) for v in values) + "]"


def pad4(data: bytes, fill: bytes = b"\x00") -> bytes:
    return data + fill * ((4 - len(data) % 4) % 4)


# ---------------------------------------------------------------------------
# Collision
# ---------------------------------------------------------------------------

def collision_obj(collision, comment: str) -> str:
    """`collision` is a list of (triangle, kind), kind "floor" or anything
    else for a wall. One room, named before the first vertex; no other o or g
    line, no mtllib. Every triangle is in room 1: a triangle in room 0
    overruns the game's tile buffer."""
    lines = ["# " + comment, "o room_1"]
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
# The scene
# ---------------------------------------------------------------------------

@dataclass
class Primitive:
    """Triangles drawn with one texture. Each corner is (position, uv, rgba);
    `room` is the room every corner is in."""
    material: str
    image_name: str
    image_png: bytes
    corners: list = field(default_factory=list)   # [(position, uv, rgba)], three per triangle
    room: int = 1


def scene_glb(primitives, generator: str, node_name: str, mesh_name: str) -> bytes:
    """An unindexed triangle list per primitive with POSITION, TEXCOORD_0,
    COLOR_0 as four floats and _PD_ROOM as an unsigned short: the forms both
    of the game's readers of a scene take (the compiler and the renderer).
    One material, texture and embedded PNG image per primitive."""
    views, blob = [], bytearray()
    accessors, gltf_primitives, materials, textures, images = [], [], [], [], []

    def add_view(data: bytes, target) -> int:
        view = {"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        views.append(view)
        blob.extend(pad4(bytes(data)))
        return len(views) - 1

    for index, primitive in enumerate(primitives):
        positions, texcoords, colours, rooms = bytearray(), bytearray(), bytearray(), bytearray()
        low = [math.inf] * 3
        high = [-math.inf] * 3
        for position, uv, rgba in primitive.corners:
            positions += struct.pack("<3f", *position)
            texcoords += struct.pack("<2f", *uv)
            colours += struct.pack("<4f", *rgba)
            rooms += struct.pack("<H", primitive.room)
            for axis in range(3):
                low[axis] = min(low[axis], position[axis])
                high[axis] = max(high[axis], position[axis])
        count = len(primitive.corners)
        base = len(accessors)
        for data in (positions, texcoords, colours, rooms):
            add_view(data, 34962)
        accessors += [
            {"bufferView": base, "componentType": 5126, "count": count, "type": "VEC3",
             "min": [float(v) for v in low], "max": [float(v) for v in high]},
            {"bufferView": base + 1, "componentType": 5126, "count": count, "type": "VEC2"},
            {"bufferView": base + 2, "componentType": 5126, "count": count, "type": "VEC4"},
            {"bufferView": base + 3, "componentType": 5123, "count": count, "type": "SCALAR"},
        ]
        gltf_primitives.append({
            "attributes": {"POSITION": base, "TEXCOORD_0": base + 1, "COLOR_0": base + 2, "_PD_ROOM": base + 3},
            "material": index, "mode": 4})
        materials.append({"name": primitive.material, "alphaMode": "OPAQUE",
                          "pbrMetallicRoughness": {"baseColorTexture": {"index": index, "texCoord": 0},
                                                   "metallicFactor": 0.0, "roughnessFactor": 1.0}})
        textures.append({"sampler": 0, "source": index})
    # The images after every primitive's vertex data, in primitive order.
    for primitive in primitives:
        images.append({"name": primitive.image_name, "mimeType": "image/png",
                       "bufferView": add_view(primitive.image_png, None)})

    document = {
        "asset": {"version": "2.0", "generator": generator},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": node_name, "mesh": 0}],
        "meshes": [{"name": mesh_name, "primitives": gltf_primitives}],
        "materials": materials,
        "textures": textures,
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "images": images,
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": views,
        "accessors": accessors,
    }
    text = pad4(json.dumps(document, separators=(",", ":")).encode("utf-8"), b" ")
    body = (struct.pack("<I4s", len(text), b"JSON") + text + struct.pack("<I4s", len(blob), b"BIN\x00") + bytes(blob))
    return struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body


# ---------------------------------------------------------------------------
# Pads, starts, bounds and the small tables
# ---------------------------------------------------------------------------

def rows_table(schema: str, rows: list[str]) -> str:
    if not rows:
        return '{\n  "schema": "%s",\n  "rows": [\n  ]\n}\n' % schema
    return '{\n  "schema": "%s",\n  "rows": [\n%s\n  ]\n}\n' % (schema, ",\n".join(rows))


def pads_json(places) -> str:
    """`places` is a list of (position, look), one pad and start each."""
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
    # One closed path through every start in order (flags 1): with the
    # waypoint tables empty the game makes a waypoint of every pad and links
    # the pads of each path in order, so simulants can walk it both ways.
    pads = ", ".join('"pad_%04d"' % index for index in range(len(places)))
    return rows_table("pd2.scenario.paths.v1", ['    { "path_ref": "path_0000", "flags": 1, "pads": [%s] }' % pads])


def rooms_json(origin, low, high) -> str:
    # The room's box, written out so that a pad with no room of its own is
    # found inside it whatever the collision triangles are.
    return rows_table("pd2.scenario.rooms.v1", [
        '    { "room": "room_1", "roomnum": 1, "origin": %s, "min": %s, "max": %s, "brightness": [128, 255] }'
        % (vector(origin), vector(low), vector(high))])


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


def scenario_ini(scenario_id: str, display_name: str, comment: str) -> str:
    return (
        "; " + comment + "\n"
        "[scenario]\n"
        "catalog_id = " + scenario_id + "\n"
        "name = " + display_name + "\n"
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


def scenario_manifest(scenario_id: str) -> str:
    return (
        "{\n"
        '  "pd_kind": "scenario",\n'
        '  "pd_schema_version": 1,\n'
        '  "id": "' + scenario_id + '",\n'
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


def navmesh_stamp(members: dict, counts: dict, scenario_id: str) -> str:
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
        '  "scenario": "' + scenario_id + '",\n'
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


def level_graph(example: dict, places, triangle_count: int, scenario_id: str) -> str:
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
                                     "scenario": scenario_id, "source": "scenario.ini", "scene": "scene.glb",
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
    out = ["{", '  "schema": "pd2.level.graph.v1",', '  "scenario": "%s",' % scenario_id,
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


@dataclass
class Level:
    """What a caller gives: the ids, the look and the starts."""
    arena_id: str
    scenario_id: str
    scenario_member: str            # where the .pdscenario sits inside the .pdarena
    display_name: str
    category: str
    arena_comment: str              # the first line of arena.ini, after "; "
    scenario_comment: str           # the first line of scenario.ini, after "; "
    collision_comment: str          # the first line of collision.obj, after "# "
    scene: bytes                    # scene.glb (scene_glb)
    collision: list                 # [(triangle, kind)], kind "floor" or a wall's
    places: list                    # [(position, look)], one start each
    room_low: tuple                 # the room's box
    room_high: tuple
    room_origin: tuple


def build_level(level: Level, geometry_checks=None):
    """Returns (arena archive bytes, scenario members). `geometry_checks`, if
    given, is called with (members, level) after the general checks, for the
    rules of the caller's own geometry."""
    example, cache_kind = read_example()
    places = level.places
    members: dict = {}

    def put(name: str, data) -> None:
        members[name] = data.encode("utf-8") if isinstance(data, str) else data

    put("scenario.ini", scenario_ini(level.scenario_id, level.display_name, level.scenario_comment))
    put("scene.glb", level.scene)
    put("collision.obj", collision_obj(level.collision, level.collision_comment))
    put("collision.flags.json", collision_flags(level.collision))
    put("rooms.json", rooms_json(level.room_origin, level.room_low, level.room_high))
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
    put("level.graph.json", level_graph(example, places, len(level.collision), level.scenario_id))
    put("_meta/generated-navmesh.json", navmesh_stamp(members, {
        "pads": len(places), "volumes": len(places), "waypoints": 0, "waygroups": 0, "covers": 0, "paths": 1},
        level.scenario_id))
    put("_meta/manifest.json", scenario_manifest(level.scenario_id))

    check_scenario(members, level)
    if geometry_checks:
        geometry_checks(members, level)
    scenario = zip_bytes(members.items())

    arena_ini = (
        "; " + level.arena_comment + "\n"
        "[arena]\n"
        "catalog_id = " + level.arena_id + "\n"
        "display_name = " + level.display_name + "\n"
        "category = " + level.category + "\n"
        "load_mode = ARENA_LOADMODE_PLAYABLE\n"
        "scenario = " + level.scenario_id + "\n"
        "scenario_archive = " + level.scenario_member + "\n"
        "scenario_graph_cache = " + cache_kind + "\n"
        "\n[meta]\n"
        "manifest = _meta/manifest.json\n"
    )
    arena_manifest = (
        "{\n"
        '  "schema": "pd.asset_archive.manifest.v1",\n'
        '  "pd_kind": "arena",\n'
        '  "catalog_id": "' + level.arena_id + '",\n'
        '  "scenario": "' + level.scenario_id + '",\n'
        '  "scenario_archive": "' + level.scenario_member + '"\n'
        "}\n"
    )
    arena = zip_bytes([("arena.ini", arena_ini), (level.scenario_member, scenario),
                       ("_meta/manifest.json", arena_manifest)])
    return arena, members


# ---------------------------------------------------------------------------
# Checks: each mirrors a rule the game enforces
# ---------------------------------------------------------------------------

def graph_kind_needles() -> list[str]:
    """The node kinds the game counts in a level graph, read from the game's
    own source so the list is the one of this commit."""
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


def check_scenario(members: dict, level: Level) -> None:
    collision, places, scenario_id = level.collision, level.places, level.scenario_id
    graph = members["level.graph.json"].decode("utf-8")
    json.loads(graph)  # strict JSON, one object

    # The graph, as the game reads it: as text.
    require("pd2.level.graph.v1" in graph and scenario_id in graph, "graph: schema or scenario id missing")
    require('"nodes": []' not in graph and '"nodes":[]' not in graph, "graph: empty nodes")
    needles = graph_kind_needles()
    require(len(needles) >= 400, "only %d node kinds were found in the game's source" % len(needles))
    volume_needle = '"kind": "scenario.trigger.volume.source"'
    for needle in needles:
        wanted = len(places) if needle == volume_needle else 1
        require(graph.count(needle) == wanted, "graph: %s stands %d times, the game wants %d"
                % (needle, graph.count(needle), wanted))
    first = graph.index('"scenario"')
    require(graph[first:].startswith('"scenario": "%s"' % scenario_id), "graph: the first scenario key is not the id")
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
        normal, length = unit_normal(triangle)
        require(length > 1.0, "a collision triangle is degenerate")
        if kind == "floor":
            require(normal[1] >= 0.5, "a floor triangle does not face up")
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
    forms = {"POSITION": (5126, "VEC3"), "TEXCOORD_0": (5126, "VEC2"), "COLOR_0": (5126, "VEC4"),
             "_PD_ROOM": (5123, "SCALAR")}
    for mesh in document["meshes"]:
        for primitive in mesh["primitives"]:
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
    for image in document["images"]:
        view = document["bufferViews"][image["bufferView"]]
        data = glb[28 + json_length + view["byteOffset"]:28 + json_length + view["byteOffset"] + view["byteLength"]]
        require(data.startswith(b"\x89PNG\r\n\x1a\n"), "scene.glb: an image is not a PNG")

    # Pads and starts: inside the room's box, above its floor, apart, and
    # none near the origin (the spawn pool marks points there down).
    pad_rows = json.loads(members["pads.json"])["rows"]
    require(len(pad_rows) >= 1, "pads.json: a level needs at least one start")
    for index, row in enumerate(pad_rows):
        require(row["pad_ref"] == "pad_%04d" % index, "pads.json: row %d is %s" % (index, row["pad_ref"]))
        x, y, z = row["position"]
        require(all(level.room_low[axis] < (x, y, z)[axis] < level.room_high[axis] for axis in range(3)),
                "pad %d: outside the room's box" % index)
        require(math.hypot(x, z) > 85, "pad %d: too near the origin" % index)
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
                                    "waygroups": 0, "covers": 0, "paths": 1}, scenario_id)
    require(members["_meta/generated-navmesh.json"].decode("utf-8") == stamp, "the navigation record is stale")
    require(level.arena_id.split(":")[0] == scenario_id.split(":")[0] and not level.arena_id.startswith("base:"),
            "the arena and its scenario must share a namespace that is not base")


def conformance(arena: bytes, archive_name: str) -> list[str]:
    """The repository's archive validator, on the built arena, with the
    example level's ids known as they are when CORE checks a tree."""
    sys.path.insert(0, str(REPO / "tools"))
    try:
        import asset_archive_conformance as conf
    finally:
        sys.path.pop(0)
    known = set(conf.collect_source_catalog_ids())
    known.update(conf.collect_archive_catalog_ids(arena, ".pdarena", recurse=True))
    result = conf.validate_archive_bytes(arena, archive_name, ".pdarena", recurse=True,
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
