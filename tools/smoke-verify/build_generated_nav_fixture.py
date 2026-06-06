#!/usr/bin/env python3
"""Build a Scenario fixture that proves generated nav can use paths.tsv."""

from __future__ import annotations

import argparse
import hashlib
import json
import zipfile
from pathlib import Path


PAD_HEADER = (
    "pad_id\troom_ref\tliftnum\tflags\tpos_x\tpos_y\tpos_z\t"
    "up_x\tup_y\tup_z\tlook_x\tlook_y\tlook_z\t"
    "bbox_xmin\tbbox_xmax\tbbox_ymin\tbbox_ymax\tbbox_zmin\tbbox_zmax\n"
)
PADS_TSV = (
    PAD_HEADER
    + "pad_0000\troom_0001\t0\t0x00000000\t0.000\t0.000\t0.000\t"
    "0.000\t1.000\t0.000\t0.000\t0.000\t1.000\t"
    "-16.000\t16.000\t-16.000\t16.000\t-16.000\t16.000\n"
    + "pad_0001\troom_0001\t0\t0x00000000\t64.000\t0.000\t0.000\t"
    "0.000\t1.000\t0.000\t1.000\t0.000\t0.000\t"
    "-16.000\t16.000\t-16.000\t16.000\t-16.000\t16.000\n"
    + "pad_0002\troom_0001\t0\t0x00000000\t128.000\t0.000\t0.000\t"
    "0.000\t1.000\t0.000\t1.000\t0.000\t0.000\t"
    "-16.000\t16.000\t-16.000\t16.000\t-16.000\t16.000\n"
    + "pad_0003\troom_0001\t0\t0x00000000\t64.000\t0.000\t64.000\t"
    "0.000\t1.000\t0.000\t0.000\t0.000\t1.000\t"
    "-16.000\t16.000\t-16.000\t16.000\t-16.000\t16.000\n"
    + "pad_0004\troom_0001\t0\t0x00000000\t192.000\t0.000\t64.000\t"
    "0.000\t1.000\t0.000\t0.000\t0.000\t1.000\t"
    "-16.000\t16.000\t-16.000\t16.000\t-16.000\t16.000\n"
)
PATHS_TSV = (
    "path_ref\tflags\tpads\n"
    "path_0000\t0x01\tpad_0000,pad_0001,pad_0002\n"
    "path_0001\t0x02\tpad_0001|outward,pad_0003\n"
    "path_0002\t0x00\tpad_0004\n"
)
OBJECTS_TSV = (
    "record_id\tkind\tpad_ref\tmodel_catalog_id\tweapon_catalog_id\tsecondary_weapon_catalog_id\t"
    "body_catalog_id\thead_catalog_id\tailist_ref\tflags\tflags2\tflags3\n"
    "setup_0000\tcharacter_spawn\tpad_0000\t\t\t\tbase:a51airman\tbase:head_a51faceplate\t"
    "ailist_1025\t0x00000000\t0x00000000\t\n"
    "setup_0001\thover_car\tpad_0001\tbase:model_hovcop_eu\t\t\t\t\tailist_1026\t"
    "0x00000000\t0x00000000\t0x00000000\n"
)
AI_LISTS_TSV = (
    "ailist_ref\tlist_id\tgraph_node\tcommand_index\toffset\topcode\topcode_name\toperands\t"
    "model_catalog_id\tweapon_catalog_id\tbody_catalog_id\thead_catalog_id\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0000\t0\t0\t0x0076\t"
    "set_pad_preset_to_target_quadrant\t0x01,0x01\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0001\t1\t4\t0x0002\tlabel\t0x01\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0002\t2\t7\t0x0075\t"
    "if_waypoint_within_quadrant\t0x01,0x02\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0003\t3\t11\t0x0002\tlabel\t0x02\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0004\t4\t14\t0x0042\t"
    "set_pad_preset_to_pad_on_route_to_target\t0x03\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0005\t5\t17\t0x0002\tlabel\t0x03\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0006\t6\t20\t0x0121\t"
    "find_cover\t0x80,0x85,0x04\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0007\t7\t25\t0x0002\tlabel\t0x04\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0008\t8\t28\t0x0122\t"
    "find_cover_within_dist\t0x80,0x85,0x00,0x00,0x00,0x00,0x05\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0009\t9\t37\t0x0002\tlabel\t0x05\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0010\t10\t40\t0x0123\t"
    "find_cover_outside_dist\t0x80,0x85,0x00,0x00,0x00,0x00,0x06\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0011\t11\t49\t0x0002\tlabel\t0x06\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0012\t12\t52\t0x0124\t"
    "go_to_cover\t0x11\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0013\t13\t55\t0x0125\t"
    "check_cover_out_of_sight\t0x07\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0014\t14\t58\t0x0002\tlabel\t0x07\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0015\t15\t61\t0x013c\t"
    "face_cover\t0x08\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0016\t16\t64\t0x0002\tlabel\t0x08\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0017\t17\t67\t0x013e\t"
    "danger_cover\t\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0018\t18\t69\t0x012f\t"
    "release_cover\t\t\t\t\t\n"
    "ailist_1025\t0x0401\tscenario.ai.ailist_1025.command.0019\t19\t71\t0x0004\tend\t\t\t\t\t\n"
    "ailist_1026\t0x0402\tscenario.ai.ailist_1026.command.0000\t0\t0\t0x00d5\t"
    "hovercar_begin_path\t0x00\t\t\t\t\n"
    "ailist_1026\t0x0402\tscenario.ai.ailist_1026.command.0001\t1\t3\t0x00d6\t"
    "set_vehicle_speed\t0x0f,0x00,0x00,0x3c\t\t\t\t\n"
    "ailist_1026\t0x0402\tscenario.ai.ailist_1026.command.0002\t2\t9\t0x0004\tend\t\t\t\t\t\n"
)
SETUP_FIELDS_TSV = (
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
WAYPOINTS_TSV = "waypoint_id\tpad_ref\tgroup_ref\tstep\tneighbours\n"
WAYGROUPS_TSV = "waygroup_id\tstep\twaypoints\tneighbours\n"
COVERS_TSV = "cover_id\tflags\tpos_x\tpos_y\tpos_z\tlook_x\tlook_y\tlook_z\n"
NAVIGATION_INI = """[navigation]
source = scene.glb
collision_source = collision.obj
generator = deterministic.surface_graph.v1
supports_walk = true
supports_jump = true
supports_drop = true
supports_wall = true
supports_ceiling = true
portals_file = portals.tsv
pads_file = pads.tsv
spawns_file = spawns.tsv
volumes_file = volumes.tsv
waypoints_file = navigation/waypoints.tsv
waygroups_file = navigation/waygroups.tsv
covers_file = navigation/covers.tsv
paths_file = navigation/paths.tsv
generated_cache = _meta/generated-navmesh.json
"""

SOURCE_NAMES = [
    "scene.glb",
    "collision.obj",
    "navigation.ini",
    "portals.tsv",
    "pads.tsv",
    "spawns.tsv",
    "volumes.tsv",
    "navigation/waypoints.tsv",
    "navigation/waygroups.tsv",
    "navigation/covers.tsv",
    "navigation/paths.tsv",
]


def count_tsv_rows(data: bytes) -> int:
    lines = data.decode("utf-8", "replace").splitlines()
    return sum(1 for index, line in enumerate(lines) if index > 0 and line.strip())


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def text_bytes(text: str) -> bytes:
    return text.encode("utf-8")


def build_navmesh_json(entries: dict[str, bytes]) -> bytes:
    counts = {
        "pads": count_tsv_rows(entries["pads.tsv"]),
        "volumes": count_tsv_rows(entries["volumes.tsv"]),
        "waypoints": count_tsv_rows(entries["navigation/waypoints.tsv"]),
        "waygroups": count_tsv_rows(entries["navigation/waygroups.tsv"]),
        "covers": count_tsv_rows(entries["navigation/covers.tsv"]),
        "paths": count_tsv_rows(entries["navigation/paths.tsv"]),
    }
    hashes = {name: sha256_hex(entries[name]) for name in SOURCE_NAMES}
    text = (
        "{\n"
        '  "schema": "pd2.generated.navmesh.v1",\n'
        '  "scenario": "base:scenario_test_ash",\n'
        '  "derived_from": "scene.glb",\n'
        '  "inputs": ["scene.glb", "collision.obj", "navigation.ini", "portals.tsv", "pads.tsv", "spawns.tsv", "volumes.tsv", "navigation/waypoints.tsv", "navigation/waygroups.tsv", "navigation/covers.tsv", "navigation/paths.tsv"],\n'
        '  "generator": "deterministic.surface_graph.v1",\n'
        '  "capabilities": ["walk", "jump", "drop", "wall", "ceiling"],\n'
        f'  "source_counts": {{ "pads": {counts["pads"]}, "volumes": {counts["volumes"]}, "waypoints": {counts["waypoints"]}, "waygroups": {counts["waygroups"]}, "covers": {counts["covers"]}, "paths": {counts["paths"]} }},\n'
        f'  "source_hashes": {{ "scene.glb": "{hashes["scene.glb"]}", "collision.obj": "{hashes["collision.obj"]}", "navigation.ini": "{hashes["navigation.ini"]}", "portals.tsv": "{hashes["portals.tsv"]}", "pads.tsv": "{hashes["pads.tsv"]}", "spawns.tsv": "{hashes["spawns.tsv"]}", "volumes.tsv": "{hashes["volumes.tsv"]}", "navigation/waypoints.tsv": "{hashes["navigation/waypoints.tsv"]}", "navigation/waygroups.tsv": "{hashes["navigation/waygroups.tsv"]}", "navigation/covers.tsv": "{hashes["navigation/covers.tsv"]}", "navigation/paths.tsv": "{hashes["navigation/paths.tsv"]}" }},\n'
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
        "pads.tsv": text_bytes(PADS_TSV),
        "navigation/paths.tsv": text_bytes(PATHS_TSV),
        "navigation/waypoints.tsv": text_bytes(WAYPOINTS_TSV),
        "navigation/waygroups.tsv": text_bytes(WAYGROUPS_TSV),
        "navigation/covers.tsv": text_bytes(COVERS_TSV),
        "navigation.ini": text_bytes(NAVIGATION_INI),
        "objects.tsv": text_bytes(OBJECTS_TSV),
        "setup.fields.tsv": text_bytes(SETUP_FIELDS_TSV),
        "ai/ailists.tsv": text_bytes(AI_LISTS_TSV),
    }
    entries: dict[str, bytes] = {}
    with zipfile.ZipFile(source, "r") as zin:
        for name in zin.namelist():
            entries[name] = zin.read(name)

    entries.update(replacements)
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
