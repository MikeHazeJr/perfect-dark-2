#!/usr/bin/env python3
"""Verify authoritative geometry-state semantics in public .pdmesh sources."""

from __future__ import annotations

import argparse
import json
import sys
import zipfile
from pathlib import Path

from pdmesh_render_stream import (
    RENDER_NODE_TYPES,
    RenderStreamError,
    SCHEMA_CURRENT,
    validate_render_stream,
)


def parse_expectation(value: str) -> tuple[Path, str]:
    path_text, separator, expected = value.rpartition("=")
    if (not separator or
            expected not in {"on", "off", "inherited", "baseline", "mixed"} or
            not path_text):
        raise argparse.ArgumentTypeError(
            "expected PATH=on, PATH=off, PATH=inherited, PATH=baseline, "
            "or PATH=mixed"
        )
    return Path(path_text), expected


def parse_state_count(value: str) -> tuple[Path, str, int]:
    path_state, separator, count_text = value.rpartition("=")
    path_text, state_separator, state = path_state.rpartition(":")
    if (not separator or not state_separator or not path_text or
            state not in {"on", "off", "inherited", "baseline"}):
        raise argparse.ArgumentTypeError(
            "expected PATH:STATE=COUNT with STATE on, off, inherited, or "
            "baseline"
        )
    try:
        count = int(count_text, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("COUNT must be an integer") from exc
    if count < 0:
        raise argparse.ArgumentTypeError("COUNT must be non-negative")
    return Path(path_text), state, count


def load_json_member(archive: zipfile.ZipFile, member: str) -> dict:
    try:
        value = json.loads(archive.read(member).decode("utf-8"))
    except (KeyError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"{member}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{member}: root must be an object")
    return value


def inspect_archive(path: Path) -> dict:
    with zipfile.ZipFile(path) as archive:
        render = load_json_member(archive, "model.render.json")
        nodes = load_json_member(archive, "model.nodes.json")
        faces = load_json_member(archive, "model.faces.json")

    node_rows = nodes.get("nodes")
    face_rows = faces.get("faces")
    if not isinstance(node_rows, list) or not node_rows:
        raise ValueError("model.nodes.json: non-empty nodes required")
    if not isinstance(face_rows, list):
        raise ValueError("model.faces.json: faces array required")

    render_nodes: dict[str, int] = {}
    for index, node in enumerate(node_rows):
        if not isinstance(node, dict):
            continue
        node_type = node.get("type")
        if (isinstance(node_type, bool) or
                not isinstance(node_type, int) or
                node_type not in RENDER_NODE_TYPES):
            continue
        group = node.get("group")
        mcount = node.get("mcount")
        if (not isinstance(group, str) or not group or
                isinstance(mcount, bool) or not isinstance(mcount, int)):
            raise ValueError(
                f"model.nodes.json: render node {index} has invalid "
                "group/mcount"
            )
        if group in render_nodes:
            raise ValueError(
                f"model.nodes.json: duplicate render group {group!r}"
            )
        render_nodes[group] = mcount

    try:
        render_report = validate_render_stream(
            render, len(face_rows), render_nodes
        )
    except RenderStreamError as exc:
        raise ValueError(str(exc)) from exc
    if render_report.schema_version != SCHEMA_CURRENT:
        raise ValueError("model.render.json: schema v3 required")

    group_state: dict[str, dict] = {}
    for group, states in render_report.triangle_lighting.items():
        if group not in render_nodes:
            continue
        distinct = set(states)
        resolved = next(iter(distinct)) if len(distinct) == 1 else "mixed"
        group_state[group] = {
            "mcount": render_nodes[group],
            "triangles": len(states) // 3,
            "geometry_commands": render_report.geometry_command_counts.get(
                group, 0
            ),
            "vertex_loads": render_report.vertex_load_counts.get(group, 0),
            "vertex_scopes": render_report.vertex_scope_counts.get(group, 0),
            "vertex_cache_resets": (
                render_report.vertex_cache_reset_counts.get(group, 0)
            ),
            "retained_vertex_corners": (
                render_report.retained_vertex_corner_counts.get(group, 0)
            ),
            "vertex_lighting": resolved,
            "vertex_lighting_counts": {
                "on": states.count("on"),
                "off": states.count("off"),
                "inherited": states.count("inherited"),
                "baseline": states.count("baseline"),
            },
        }

    if not group_state:
        raise ValueError("no tri-bearing render groups found")
    return {
        "archive": str(path),
        "schema": render["pd_schema_version"],
        "groups": group_state,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--expect-lighting",
        action="append",
        default=[],
        type=parse_expectation,
        metavar="PATH=on|off|inherited|baseline|mixed",
        help="Require every tri-bearing render group to resolve to this state.",
    )
    parser.add_argument(
        "--expect-state-count",
        action="append",
        default=[],
        type=parse_state_count,
        metavar="PATH:STATE=COUNT",
        help="Require an exact aggregate corner count for one lighting state.",
    )
    args = parser.parse_args()
    if not args.expect_lighting:
        parser.error("at least one --expect-lighting is required")

    reports: list[dict] = []
    errors: list[str] = []
    for path, expected in args.expect_lighting:
        try:
            report = inspect_archive(path)
            reports.append(report)
            for group, state in report["groups"].items():
                if state["vertex_lighting"] != expected:
                    errors.append(
                        f"{path}:{group}: expected lighting {expected}, "
                        f"got {state['vertex_lighting']}"
                    )
                if (expected not in {"inherited", "baseline"} and
                        state["geometry_commands"] <= 0):
                    errors.append(
                        f"{path}:{group}: no authoritative geometry commands"
                    )
        except (OSError, ValueError, zipfile.BadZipFile) as exc:
            errors.append(f"{path}: {exc}")

    reports_by_path = {Path(report["archive"]): report for report in reports}
    for path, state_name, expected_count in args.expect_state_count:
        report = reports_by_path.get(path)
        if report is None:
            errors.append(f"{path}: no report available for state-count check")
            continue
        actual_count = sum(
            group["vertex_lighting_counts"][state_name]
            for group in report["groups"].values()
        )
        if actual_count != expected_count:
            errors.append(
                f"{path}: expected {state_name} corner count {expected_count}, "
                f"got {actual_count}"
            )

    print(json.dumps({"reports": reports, "errors": errors}, indent=2))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
