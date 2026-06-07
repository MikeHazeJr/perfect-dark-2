#!/usr/bin/env python3
"""Build a Scenario fixture that proves generated nav can use paths.json."""

from __future__ import annotations

import argparse
import hashlib
import json
import zipfile
from pathlib import Path


PADS_JSON = json.dumps({
    "schema": "pd2.scenario.pads.v1",
    "rows": [
        {
            "pad_ref": "pad_0000",
            "room_ref": "room_0001",
            "liftnum": 0,
            "flags": "0x00000000",
            "position": [0, 0, 0],
            "up": [0, 1, 0],
            "look": [0, 0, 1],
            "bbox": {"min": [-16, -16, -16], "max": [16, 16, 16]},
        },
        {
            "pad_ref": "pad_0001",
            "room_ref": "room_0001",
            "liftnum": 0,
            "flags": "0x00000000",
            "position": [64, 0, 0],
            "up": [0, 1, 0],
            "look": [1, 0, 0],
            "bbox": {"min": [-16, -16, -16], "max": [16, 16, 16]},
        },
        {
            "pad_ref": "pad_0002",
            "room_ref": "room_0001",
            "liftnum": 0,
            "flags": "0x00000000",
            "position": [128, 0, 0],
            "up": [0, 1, 0],
            "look": [1, 0, 0],
            "bbox": {"min": [-16, -16, -16], "max": [16, 16, 16]},
        },
        {
            "pad_ref": "pad_0003",
            "room_ref": "room_0001",
            "liftnum": 0,
            "flags": "0x00000000",
            "position": [64, 0, 64],
            "up": [0, 1, 0],
            "look": [0, 0, 1],
            "bbox": {"min": [-16, -16, -16], "max": [16, 16, 16]},
        },
        {
            "pad_ref": "pad_0004",
            "room_ref": "room_0001",
            "liftnum": 0,
            "flags": "0x00000000",
            "position": [192, 0, 64],
            "up": [0, 1, 0],
            "look": [0, 0, 1],
            "bbox": {"min": [-16, -16, -16], "max": [16, 16, 16]},
        },
    ],
}, indent=2) + "\n"
PATHS_JSON = json.dumps({
    "schema": "pd2.scenario.paths.v1",
    "rows": [
        {
            "path_ref": "path_0000",
            "flags": "0x01",
            "pads": ["pad_0000", "pad_0001", "pad_0002"],
        },
        {
            "path_ref": "path_0001",
            "flags": "0x02",
            "pads": ["pad_0001|outward", "pad_0003"],
        },
        {
            "path_ref": "path_0002",
            "flags": "0x00",
            "pads": ["pad_0004"],
        },
    ],
}, indent=2) + "\n"
SPAWNS_JSON = json.dumps({
    "schema": "pd2.scenario.spawns.v1",
    "rows": [
        {
            "spawn_id": "spawn_0000",
            "pad_ref": "pad_0000",
            "room_ref": "room_0001",
            "team": "any",
            "profile": "default",
            "position": [0, 0, 0],
            "look": [0, 0, 1],
        },
    ],
}, indent=2) + "\n"
VOLUMES_JSON = json.dumps({
    "schema": "pd2.scenario.volumes.v1",
    "rows": [],
}, indent=2) + "\n"
PORTALS_JSON = json.dumps({
    "schema": "pd2.scenario.portals.v1",
    "rows": [],
}, indent=2) + "\n"
OBJECTIVES_JSON = json.dumps({
    "schema": "pd2.scenario.objectives.v1",
    "rows": [],
}, indent=2) + "\n"
OBJECTS_JSON = json.dumps({
    "schema": "pd2.scenario.objects.v1",
    "rows": [
        {
            "record_id": "setup_0000",
            "kind": "character_spawn",
            "pad_ref": "pad_0000",
            "model_catalog_id": "",
            "weapon_catalog_id": "",
            "secondary_weapon_catalog_id": "",
            "body_catalog_id": "base:a51airman",
            "head_catalog_id": "base:head_a51faceplate",
            "ailist_ref": "ailist_1025",
            "flags": "0x00000000",
            "flags2": "0x00000000",
            "flags3": "",
        },
        {
            "record_id": "setup_0001",
            "kind": "hover_car",
            "pad_ref": "pad_0001",
            "model_catalog_id": "base:model_hovcop_eu",
            "weapon_catalog_id": "",
            "secondary_weapon_catalog_id": "",
            "body_catalog_id": "",
            "head_catalog_id": "",
            "ailist_ref": "ailist_1026",
            "flags": "0x00000000",
            "flags2": "0x00000000",
            "flags3": "0x00000000",
        },
    ],
}, indent=2) + "\n"
def ai_lists_json(rows: list[tuple[str, str, int, int, str, str, list[str]]]) -> str:
    return json.dumps({
        "schema": "pd2.scenario.ai.lists.v1",
        "rows": [
            {
                "ailist_ref": ailist_ref,
                "list_id": list_id,
                "graph_node": f"scenario.ai.{ailist_ref}.command.{command_index:04d}",
                "command_index": command_index,
                "offset": offset,
                "opcode": opcode,
                "opcode_name": opcode_name,
                "operands": operands,
                "model_catalog_id": "",
                "weapon_catalog_id": "",
                "body_catalog_id": "",
                "head_catalog_id": "",
            }
            for ailist_ref, list_id, command_index, offset, opcode, opcode_name, operands in rows
        ],
    }, indent=2) + "\n"


AI_LISTS_JSON = ai_lists_json([
    ("ailist_1025", "0x0401", 0, 0, "0x0076", "set_pad_preset_to_target_quadrant", ["0x01", "0x01"]),
    ("ailist_1025", "0x0401", 1, 4, "0x0002", "label", ["0x01"]),
    ("ailist_1025", "0x0401", 2, 7, "0x0075", "if_waypoint_within_quadrant", ["0x01", "0x02"]),
    ("ailist_1025", "0x0401", 3, 11, "0x0002", "label", ["0x02"]),
    ("ailist_1025", "0x0401", 4, 14, "0x0042", "set_pad_preset_to_pad_on_route_to_target", ["0x03"]),
    ("ailist_1025", "0x0401", 5, 17, "0x0002", "label", ["0x03"]),
    ("ailist_1025", "0x0401", 6, 20, "0x0121", "find_cover", ["0x80", "0x85", "0x04"]),
    ("ailist_1025", "0x0401", 7, 25, "0x0002", "label", ["0x04"]),
    ("ailist_1025", "0x0401", 8, 28, "0x0122", "find_cover_within_dist", ["0x80", "0x85", "0x00", "0x00", "0x00", "0x00", "0x05"]),
    ("ailist_1025", "0x0401", 9, 37, "0x0002", "label", ["0x05"]),
    ("ailist_1025", "0x0401", 10, 40, "0x0123", "find_cover_outside_dist", ["0x80", "0x85", "0x00", "0x00", "0x00", "0x00", "0x06"]),
    ("ailist_1025", "0x0401", 11, 49, "0x0002", "label", ["0x06"]),
    ("ailist_1025", "0x0401", 12, 52, "0x0124", "go_to_cover", ["0x11"]),
    ("ailist_1025", "0x0401", 13, 55, "0x0125", "check_cover_out_of_sight", ["0x07"]),
    ("ailist_1025", "0x0401", 14, 58, "0x0002", "label", ["0x07"]),
    ("ailist_1025", "0x0401", 15, 61, "0x013c", "face_cover", ["0x08"]),
    ("ailist_1025", "0x0401", 16, 64, "0x0002", "label", ["0x08"]),
    ("ailist_1025", "0x0401", 17, 67, "0x013e", "danger_cover", []),
    ("ailist_1025", "0x0401", 18, 69, "0x012f", "release_cover", []),
    ("ailist_1025", "0x0401", 19, 71, "0x0004", "end", []),
    ("ailist_1026", "0x0402", 0, 0, "0x00d5", "hovercar_begin_path", ["0x00"]),
    ("ailist_1026", "0x0402", 1, 3, "0x00d6", "set_vehicle_speed", ["0x0f", "0x00", "0x00", "0x3c"]),
    ("ailist_1026", "0x0402", 2, 9, "0x0004", "end", []),
])
SETUP_FIELDS_ROWS = (
    "record_id\tkind\tfield\ttype\tvalue\tcatalog_id\tref_record_id\n"
    "setup_0000\tcharacter_spawn\tcharacter.spawn_flags\tu32_hex\t0x00000100\t\t\n"
    "setup_0000\tcharacter_spawn\tcharacter.slot\ts32\t5000\t\t\n"
    "setup_0000\tcharacter_spawn\tcharacter.ai_list\tindexed_ref\tailist_1025\t\t\n"
    "setup_0000\tcharacter_spawn\tcharacter.pad_preset\ts32\t0\t\t\n"
    "setup_0000\tcharacter_spawn\tcharacter.character_preset\ts32\t0\t\t\n"
    "setup_0000\tcharacter_spawn\tcharacter.hearing_scale\ts32\t1000\t\t\n"
    "setup_0000\tcharacter_spawn\tcharacter.view_distance\ts32\t250\t\t\n"
    "setup_0000\tcharacter_spawn\tcharacter.chair\ts32\t-1\t\t\n"
    "setup_0001\thover_car\tcommand.order\ts32\t1\t\t\n"
    "setup_0001\thover_car\tbase.extra_scale\ts32\t76\t\t\n"
    "setup_0001\thover_car\tbase.hidden2\ts32\t0\t\t\n"
    "setup_0001\thover_car\tbase.model\tmodel_catalog_id\t\tbase:model_hovcop_eu\t\n"
    "setup_0001\thover_car\tbase.pad\tpad_ref\tpad_0001\t\t\n"
    "setup_0001\thover_car\tbase.flags\tu32_hex\t0x00000000\t\t\n"
    "setup_0001\thover_car\tbase.flags2\tu32_hex\t0x00000000\t\t\n"
    "setup_0001\thover_car\tbase.flags3\tu32_hex\t0x00000000\t\t\n"
    "setup_0001\thover_car\tbase.floor_color\ts32\t4095\t\t\n"
    "setup_0001\thover_car\tbase.geo_count\ts32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.ai_offset\ts32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.ai_return_list\ts32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.speed\tf32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.speed_aim\tf32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.speed_time60\tf32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.turn_y_speed60\tf32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.turn_x_speed60\tf32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.turn_rot60\tf32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.rot_y\tf32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.rot_x\tf32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.rot_z\tf32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.next_step\ts32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.status\ts32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.dead\ts32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.dead_timer60\ts32\t0\t\t\n"
    "setup_0001\thover_car\tvehicle.sparks_timer60\ts32\t0\t\t\n"
)
waypoints_json = json.dumps({
    "schema": "pd2.scenario.waypoints.v1",
    "rows": [],
}, indent=2) + "\n"
waygroups_json = json.dumps({
    "schema": "pd2.scenario.waygroups.v1",
    "rows": [],
}, indent=2) + "\n"
covers_json = json.dumps({
    "schema": "pd2.scenario.covers.v1",
    "rows": [],
}, indent=2) + "\n"
NAVIGATION_INI = """[navigation]
source = scene.glb
collision_source = collision.obj
generator = deterministic.surface_graph.v1
supports_walk = true
supports_jump = true
supports_drop = true
supports_wall = true
supports_ceiling = true
portals_file = portals.json
pads_file = pads.json
spawns_file = spawns.json
volumes_file = volumes.json
waypoints_file = navigation/waypoints.json
waygroups_file = navigation/waygroups.json
covers_file = navigation/covers.json
paths_file = navigation/paths.json
generated_cache = _meta/generated-navmesh.json
"""

SOURCE_NAMES = [
    "scene.glb",
    "collision.obj",
    "navigation.ini",
    "portals.json",
    "pads.json",
    "spawns.json",
    "volumes.json",
    "navigation/waypoints.json",
    "navigation/waygroups.json",
    "navigation/covers.json",
    "navigation/paths.json",
]


def count_json_rows(data: bytes) -> int:
    parsed = json.loads(data.decode("utf-8", "replace"))
    rows = parsed.get("rows") if isinstance(parsed, dict) else None
    return len(rows) if isinstance(rows, list) else 0


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def text_bytes(text: str) -> bytes:
    return text.encode("utf-8")


def setup_fields_json_from_rows(text: str) -> bytes:
    lines = [line for line in text.splitlines() if line.strip()]
    header = lines[0].split("\t")
    rows = []
    for line in lines[1:]:
        cols = line.split("\t")
        row = {header[i]: cols[i] if i < len(cols) else "" for i in range(len(header))}
        rows.append(row)
    return text_bytes(json.dumps({
        "schema": "pd2.scenario.setup.fields.v1",
        "rows": rows,
    }, indent=2) + "\n")


def build_navmesh_json(entries: dict[str, bytes]) -> bytes:
    counts = {
        "pads": count_json_rows(entries["pads.json"]),
        "volumes": count_json_rows(entries["volumes.json"]),
        "waypoints": count_json_rows(entries["navigation/waypoints.json"]),
        "waygroups": count_json_rows(entries["navigation/waygroups.json"]),
        "covers": count_json_rows(entries["navigation/covers.json"]),
        "paths": count_json_rows(entries["navigation/paths.json"]),
    }
    hashes = {name: sha256_hex(entries[name]) for name in SOURCE_NAMES}
    text = (
        "{\n"
        '  "schema": "pd2.generated.navmesh.v1",\n'
        '  "scenario": "base:scenario_test_ash",\n'
        '  "derived_from": "scene.glb",\n'
        '  "inputs": ["scene.glb", "collision.obj", "navigation.ini", "portals.json", "pads.json", "spawns.json", "volumes.json", "navigation/waypoints.json", "navigation/waygroups.json", "navigation/covers.json", "navigation/paths.json"],\n'
        '  "generator": "deterministic.surface_graph.v1",\n'
        '  "capabilities": ["walk", "jump", "drop", "wall", "ceiling"],\n'
        f'  "source_counts": {{ "pads": {counts["pads"]}, "volumes": {counts["volumes"]}, "waypoints": {counts["waypoints"]}, "waygroups": {counts["waygroups"]}, "covers": {counts["covers"]}, "paths": {counts["paths"]} }},\n'
        f'  "source_hashes": {{ "scene.glb": "{hashes["scene.glb"]}", "collision.obj": "{hashes["collision.obj"]}", "navigation.ini": "{hashes["navigation.ini"]}", "portals.json": "{hashes["portals.json"]}", "pads.json": "{hashes["pads.json"]}", "spawns.json": "{hashes["spawns.json"]}", "volumes.json": "{hashes["volumes.json"]}", "navigation/waypoints.json": "{hashes["navigation/waypoints.json"]}", "navigation/waygroups.json": "{hashes["navigation/waygroups.json"]}", "navigation/covers.json": "{hashes["navigation/covers.json"]}", "navigation/paths.json": "{hashes["navigation/paths.json"]}" }},\n'
        '  "cache_only": true\n'
        "}\n"
    )
    return text_bytes(text)


def update_manifest(data: bytes) -> bytes:
    manifest = json.loads(data.decode("utf-8"))
    manifest["pad_count"] = 5
    manifest["waypoint_count"] = 0
    manifest["waygroup_count"] = 0
    manifest["cover_count"] = 0
    manifest["path_count"] = 3
    manifest["ai_list_count"] = 2
    manifest["ai_command_count"] = 23
    manifest["object_count"] = 2
    return text_bytes(json.dumps(manifest, indent=2) + "\n")


def build_fixture(source: Path, out: Path) -> None:
    replacements = {
        "pads.json": text_bytes(PADS_JSON),
        "spawns.json": text_bytes(SPAWNS_JSON),
        "volumes.json": text_bytes(VOLUMES_JSON),
        "portals.json": text_bytes(PORTALS_JSON),
        "navigation/paths.json": text_bytes(PATHS_JSON),
        "navigation/waypoints.json": text_bytes(waypoints_json),
        "navigation/waygroups.json": text_bytes(waygroups_json),
        "navigation/covers.json": text_bytes(covers_json),
        "navigation.ini": text_bytes(NAVIGATION_INI),
        "objects.json": text_bytes(OBJECTS_JSON),
        "setup.fields.json": setup_fields_json_from_rows(SETUP_FIELDS_ROWS),
        "ai/ailists.json": text_bytes(AI_LISTS_JSON),
        "objectives.json": text_bytes(OBJECTIVES_JSON),
    }
    entries: dict[str, bytes] = {}
    with zipfile.ZipFile(source, "r") as zin:
        for name in zin.namelist():
            entries[name] = zin.read(name)

    entries.update(replacements)
    rewrites = {
        b"portals.tsv": b"portals.json",
        b"objects.tsv": b"objects.json",
        b"setup.fields.tsv": b"setup.fields.json",
        b"ai/ailists.tsv": b"ai/ailists.json",
        b"objectives.tsv": b"objectives.json",
        b"pads.tsv": b"pads.json",
        b"spawns.tsv": b"spawns.json",
        b"volumes.tsv": b"volumes.json",
        b"navigation/waypoints.tsv": b"navigation/waypoints.json",
        b"navigation/waygroups.tsv": b"navigation/waygroups.json",
        b"navigation/covers.tsv": b"navigation/covers.json",
        b"navigation/paths.tsv": b"navigation/paths.json",
    }
    for name in list(entries):
        if name.endswith((".ini", ".json")):
            for old, new in rewrites.items():
                entries[name] = entries[name].replace(old, new)
    for name in ("scenario.ini", "level.graph.json", "_meta/manifest.json"):
        if name in entries:
            for old, new in rewrites.items():
                entries[name] = entries[name].replace(old, new)
    for stale in (
        "pads.tsv",
        "spawns.tsv",
        "volumes.tsv",
        "portals.tsv",
        "objects.tsv",
        "setup.fields.tsv",
        "ai/ailists.tsv",
        "objectives.tsv",
        "navigation/waypoints.tsv",
        "navigation/waygroups.tsv",
        "navigation/covers.tsv",
        "navigation/paths.tsv",
        "_meta/hashes.tsv",
        "_meta/pads.tsv.sha256",
        "_meta/spawns.tsv.sha256",
        "_meta/volumes.tsv.sha256",
        "_meta/portals.tsv.sha256",
        "_meta/objects.tsv.sha256",
        "_meta/setup.fields.tsv.sha256",
        "_meta/ai/ailists.tsv.sha256",
        "_meta/objectives.tsv.sha256",
        "_meta/navigation/waypoints.tsv.sha256",
        "_meta/navigation/waygroups.tsv.sha256",
        "_meta/navigation/covers.tsv.sha256",
        "_meta/navigation/paths.tsv.sha256",
    ):
        entries.pop(stale, None)
    entries["_meta/manifest.json"] = update_manifest(entries["_meta/manifest.json"])
    entries["_meta/generated-navmesh.json"] = build_navmesh_json(entries)

    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED) as zout:
        for name in sorted(entries):
            zout.writestr(name, entries[name])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    if not args.source.exists():
        raise SystemExit(f"source archive not found: {args.source}")
    build_fixture(args.source, args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
