#!/usr/bin/env python3
"""Build and validate the Milestone 1 base-content coverage index.

This tool intentionally stops at the evidence boundary.  It derives the
coverage matrix from the current C/C++ sources, partitions that matrix into
stable shards, and accepts observations from the existing smoke-verify
runner.  It does not launch gameplay itself, and static or accelerated
observations can never become ordinary-gameplay proof during aggregation.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Sequence


SCHEMA = "pd2.base-content.coverage"
SCHEMA_VERSION = 1
RECEIPT_SCHEMA = "pd2.base-content.coverage-receipt"
SHARD_SCHEMA = "pd2.base-content.coverage-shard"
WORK_PLAN_SCHEMA = "pd2.base-content.coverage-work-plan"
TOOL_VERSION = "1.0"
SUPPORTED_SMOKE_RUNNER = "tools/smoke-verify/run.ps1"

SOURCE_PATHS = (
    "src/game/mainmenu.c",
    "src/game/stagetable.c",
    "src/include/constants.h",
    "src/game/challenge.c",
    "port/src/arenadata_authored.c",
    "port/fast3d/pdgui_menu_room.cpp",
)

EXPECTED_COUNTS = {
    "solo": 63,
    "combat_simulator": 108,
    "challenges": 120,
    "network": 126,
    "total": 417,
}

STANDARD_ARENA_CATEGORIES = {"Dark", "Classic"}
DIFFICULTIES = (
    ("agent", "DIFF_A", 0),
    ("special-agent", "DIFF_SA", 1),
    ("perfect-agent", "DIFF_PA", 2),
)
PERFECT_DARK = ("perfect-dark", "DIFF_PD", 3)
NETWORK_MODES = (("co-op", "GAMEMODE_COOP"), ("counter-op", "GAMEMODE_ANTI"))
SCENARIOS = (
    ("combat", "MPSCENARIO_COMBAT"),
    ("hold-the-briefcase", "MPSCENARIO_HOLDTHEBRIEFCASE"),
    ("hacker-central", "MPSCENARIO_HACKERCENTRAL"),
    ("pop-a-cap", "MPSCENARIO_POPACAP"),
    ("king-of-the-hill", "MPSCENARIO_KINGOFTHEHILL"),
    ("capture-the-case", "MPSCENARIO_CAPTURETHECASE"),
)


class CoverageError(RuntimeError):
    """A source, matrix, receipt, or overlap contract was violated."""


def _read(root: Path, relative: str) -> str:
    path = root / relative
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        raise CoverageError(f"cannot read source file {relative}: {exc}") from exc


def _parse_int(token: str, context: str) -> int:
    token = token.strip()
    try:
        return int(token, 0)
    except ValueError as exc:
        raise CoverageError(f"expected numeric literal for {context}, got {token!r}") from exc


def parse_numeric_macros(text: str, prefix: str) -> dict[str, int]:
    values: dict[str, int] = {}
    pattern = re.compile(rf"^\s*#define\s+({re.escape(prefix)}[A-Za-z0-9_]*)\s+(0x[0-9A-Fa-f]+|[0-9]+)\b")
    for line in text.splitlines():
        match = pattern.match(line)
        if match:
            values[match.group(1)] = int(match.group(2), 0)
    return values


def parse_solo_stages(text: str) -> list[dict[str, Any]]:
    start_marker = "struct solostage g_SoloStages[NUM_SOLOSTAGES]"
    start = text.find(start_marker)
    if start < 0:
        raise CoverageError("could not locate g_SoloStages source array")
    body_start = text.find("{", start)
    body_end = text.find("};", body_start)
    if body_start < 0 or body_end < 0:
        raise CoverageError("g_SoloStages source array has no complete initializer")

    rows: list[dict[str, Any]] = []
    row_pattern = re.compile(
        r'^\s*\{\s*(STAGE_[A-Za-z0-9_]+)\s*,\s*([^,]+),.*?,\s*"([^"]+)"\s*\}\s*,?\s*$'
    )
    for line in text[body_start + 1 : body_end].splitlines():
        match = row_pattern.match(line)
        if not match:
            continue
        rows.append(
            {
                "solo_stage_index": len(rows),
                "stage_symbol": match.group(1),
                "unknown_value": match.group(2).strip(),
                "catalog_id": match.group(3),
            }
        )
    if not rows:
        raise CoverageError("g_SoloStages has no parseable rows")
    return rows


def parse_stage_table(text: str) -> list[dict[str, Any]]:
    start = text.find("static const struct stagetableentry s_StagesInit[]")
    if start < 0:
        raise CoverageError("could not locate s_StagesInit source array")
    body_start = text.find("{", start)
    body_end = text.find("};", body_start)
    if body_start < 0 or body_end < 0:
        raise CoverageError("s_StagesInit source array has no complete initializer")

    rows: list[dict[str, Any]] = []
    row_pattern = re.compile(r"^\s*/\*(0x[0-9A-Fa-f]+)\*/\s*(STAGE_[A-Za-z0-9_]+)\s*,")
    for line in text[body_start + 1 : body_end].splitlines():
        match = row_pattern.match(line)
        if not match:
            continue
        index = int(match.group(1), 0)
        if index != len(rows):
            raise CoverageError(
                f"stage-table source index is not contiguous at {match.group(1)}; "
                f"expected 0x{len(rows):02x}"
            )
        rows.append({"stage_table_index": index, "stage_symbol": match.group(2)})
    if not rows:
        raise CoverageError("s_StagesInit has no parseable rows")
    return rows


def parse_arenas(text: str) -> list[dict[str, Any]]:
    start = text.find("const arena_authored_record_t g_ArenaData[]")
    if start < 0:
        raise CoverageError("could not locate g_ArenaData source array")
    body_start = text.find("{", start)
    body_end = text.find("};", body_start)
    if body_start < 0 or body_end < 0:
        raise CoverageError("g_ArenaData source array has no complete initializer")

    rows: list[dict[str, Any]] = []
    row_pattern = re.compile(
        r'^\s*\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*'
        r'(STAGE_[A-Za-z0-9_]+)\s*,\s*([^,]+),'
    )
    for line in text[body_start + 1 : body_end].splitlines():
        match = row_pattern.match(line)
        if not match:
            continue
        rows.append(
            {
                "arena_index": len(rows),
                "catalog_id": match.group(1),
                "slug": match.group(2),
                "category": match.group(3),
                "stage_symbol": match.group(4),
                "require_feature": match.group(5).strip(),
            }
        )
    if not rows:
        raise CoverageError("g_ArenaData has no parseable rows")
    return rows


def parse_challenges(text: str, constants: dict[str, int]) -> list[dict[str, Any]]:
    start = text.find("struct challenge g_MpChallenges[]")
    if start < 0:
        raise CoverageError("could not locate g_MpChallenges source array")
    body_start = text.find("{", start)
    body_end = text.find("};", body_start)
    if body_start < 0 or body_end < 0:
        raise CoverageError("g_MpChallenges source array has no complete initializer")

    rows: list[dict[str, Any]] = []
    row_pattern = re.compile(
        r'^\s*\{\s*L_OPTIONS_([0-9]+)\s*,\s*(MPCONFIG_CHALLENGE[0-9]+)\s*\}'
    )
    for line in text[body_start + 1 : body_end].splitlines():
        match = row_pattern.match(line)
        if not match:
            continue
        config_symbol = match.group(2)
        if config_symbol not in constants:
            raise CoverageError(f"challenge config {config_symbol} is missing from constants.h")
        rows.append(
            {
                "challenge_index": len(rows),
                "name_langid": f"L_OPTIONS_{match.group(1)}",
                "config_symbol": config_symbol,
                "config_value": constants[config_symbol],
            }
        )
    if not rows:
        raise CoverageError("g_MpChallenges has no parseable rows")
    return rows


def _source_revision(root: Path) -> str:
    """Return the latest committed revision affecting only matrix inputs.

    The coverage tool and its generated plan are deliberately outside
    SOURCE_PATHS, so committing them does not change this value.  The source
    byte manifest remains the authoritative uncommitted-change detector.
    """
    try:
        result = subprocess.run(
            ["git", "-C", str(root), "log", "-n", "1", "--format=%H", "--", *SOURCE_PATHS],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return "unknown"
    revision = result.stdout.strip()
    return revision or "unknown"


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def file_fingerprint(root: Path, relative: str) -> str:
    path = root / relative
    try:
        return _sha256_bytes(path.read_bytes())
    except OSError as exc:
        raise CoverageError(f"cannot fingerprint {relative}: {exc}") from exc


def source_manifest(root: Path) -> tuple[list[dict[str, str]], str]:
    files = [{"path": path, "sha256": file_fingerprint(root, path)} for path in SOURCE_PATHS]
    encoded = json.dumps(files, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return files, _sha256_bytes(encoded)


def _index_by_symbol(rows: Iterable[dict[str, Any]], field: str) -> dict[str, list[int]]:
    result: dict[str, list[int]] = {}
    for row in rows:
        result.setdefault(row["stage_symbol"], []).append(row[field])
    return result


def _indices(stage_symbol: str, stage_values: dict[str, int], stage_table: dict[str, list[int]], solo: dict[str, int] | None) -> dict[str, Any]:
    if stage_symbol not in stage_values:
        raise CoverageError(f"{stage_symbol} is missing a numeric STAGE_ definition")
    table_indices = stage_table.get(stage_symbol, [])
    if len(table_indices) > 1:
        raise CoverageError(f"{stage_symbol} has multiple stage-table rows: {table_indices}")
    return {
        "stage_table_index": table_indices[0] if table_indices else None,
        "solo_stage_index": solo.get(stage_symbol) if solo is not None else None,
        "stagenum": stage_values[stage_symbol],
    }


def _cell_id(family: str, ordinal: int) -> str:
    return f"{family}.{ordinal:03d}"


def _base_cell(
    family: str,
    ordinal: int,
    indices: dict[str, Any],
    *,
    catalog_id: str | None = None,
    stage_symbol: str | None = None,
    dimensions: dict[str, Any],
    proof_scope: str,
) -> dict[str, Any]:
    row = {
        "cell_id": _cell_id(family, ordinal),
        "ordinal": ordinal,
        "family": family,
        "proof_scope": proof_scope,
        "indices": indices,
        "dimensions": dimensions,
    }
    if catalog_id is not None:
        row["catalog_id"] = catalog_id
    if stage_symbol is not None:
        row["stage_symbol"] = stage_symbol
    return row


def _matrix_digest_payload(matrix: dict[str, Any]) -> dict[str, Any]:
    payload = copy.deepcopy(matrix)
    payload.pop("generated_at_utc", None)
    payload.pop("matrix_fingerprint", None)
    return payload


def matrix_fingerprint(matrix: dict[str, Any]) -> str:
    encoded = json.dumps(_matrix_digest_payload(matrix), sort_keys=True, separators=(",", ":")).encode("utf-8")
    return _sha256_bytes(encoded)


def build_matrix(root: Path, binary: Path | None = None) -> dict[str, Any]:
    constants_text = _read(root, "src/include/constants.h")
    mainmenu_text = _read(root, "src/game/mainmenu.c")
    stage_table_text = _read(root, "src/game/stagetable.c")
    challenge_text = _read(root, "src/game/challenge.c")
    arena_text = _read(root, "port/src/arenadata_authored.c")

    constants = parse_numeric_macros(constants_text, "MPCONFIG_CHALLENGE")
    stage_values = parse_numeric_macros(constants_text, "STAGE_")
    diff_values = parse_numeric_macros(constants_text, "DIFF_")
    scenario_values = parse_numeric_macros(constants_text, "MPSCENARIO_")
    num_solo_stages = parse_numeric_macros(constants_text, "NUM_SOLOSTAGES").get("NUM_SOLOSTAGES")
    if num_solo_stages is None:
        raise CoverageError("NUM_SOLOSTAGES is missing from constants.h")
    if tuple(diff_values.get(symbol) for _, symbol, _ in DIFFICULTIES) != (0, 1, 2):
        raise CoverageError("Agent/Special Agent/Perfect Agent difficulty constants are not 0, 1, 2")
    if diff_values.get(PERFECT_DARK[1]) != PERFECT_DARK[2]:
        raise CoverageError("Perfect Dark difficulty constant is not 3")

    solo_rows = parse_solo_stages(mainmenu_text)
    stage_rows = parse_stage_table(stage_table_text)
    arena_rows = parse_arenas(arena_text)
    challenge_rows = parse_challenges(challenge_text, constants)
    if len(solo_rows) != num_solo_stages:
        raise CoverageError(f"g_SoloStages has {len(solo_rows)} rows, expected NUM_SOLOSTAGES={num_solo_stages}")
    if len(solo_rows) != 21:
        raise CoverageError(f"Milestone 1 expects 21 solo source rows, found {len(solo_rows)}")
    if len(arena_rows) != 47:
        raise CoverageError(f"Milestone 1 expects the full 47-row arena inventory, found {len(arena_rows)}")
    if len(challenge_rows) != 30:
        raise CoverageError(f"Milestone 1 expects 30 challenge source rows, found {len(challenge_rows)}")
    if len(SCENARIOS) != 6:
        raise CoverageError("scenario contract must contain six scenarios")

    stage_table_indices = _index_by_symbol(stage_rows, "stage_table_index")
    solo_index_by_symbol = {row["stage_symbol"]: row["solo_stage_index"] for row in solo_rows}
    if len(solo_index_by_symbol) != len(solo_rows):
        raise CoverageError("g_SoloStages contains duplicate stage symbols")
    if len({row["catalog_id"] for row in solo_rows}) != len(solo_rows):
        raise CoverageError("g_SoloStages contains duplicate catalog IDs")

    standard_arenas = [row for row in arena_rows if row["category"] in STANDARD_ARENA_CATEGORIES]
    if len(standard_arenas) != 18:
        raise CoverageError(f"standard Combat Simulator source count is {len(standard_arenas)}, expected 18")
    if not any(row["category"] == "Bonus" for row in arena_rows):
        raise CoverageError("Bonus arena inventory is absent")
    if not any(row["category"] == "Random" for row in arena_rows):
        raise CoverageError("Random arena inventory is absent")
    if len({row["catalog_id"] for row in arena_rows}) != len(arena_rows):
        raise CoverageError("g_ArenaData contains duplicate catalog IDs")

    stage_indices_by_symbol = stage_table_indices
    solo_cells: list[dict[str, Any]] = []
    network_cells: list[dict[str, Any]] = []
    for solo_row in solo_rows:
        indices = _indices(solo_row["stage_symbol"], stage_values, stage_indices_by_symbol, solo_index_by_symbol)
        for difficulty_name, difficulty_symbol, difficulty_value in DIFFICULTIES:
            solo_cells.append(
                _base_cell(
                    "solo",
                    len(solo_cells),
                    indices,
                    catalog_id=solo_row["catalog_id"],
                    stage_symbol=solo_row["stage_symbol"],
                    dimensions={
                        "solo_stage_index": solo_row["solo_stage_index"],
                        "difficulty": difficulty_name,
                        "difficulty_symbol": difficulty_symbol,
                        "difficulty_value": difficulty_value,
                    },
                    proof_scope="ordinary-client-campaign",
                )
            )
        for mode_name, mode_symbol in NETWORK_MODES:
            for difficulty_name, difficulty_symbol, difficulty_value in DIFFICULTIES:
                network_cells.append(
                    _base_cell(
                        "network",
                        len(network_cells),
                        indices,
                        catalog_id=solo_row["catalog_id"],
                        stage_symbol=solo_row["stage_symbol"],
                        dimensions={
                            "solo_stage_index": solo_row["solo_stage_index"],
                            "mode": mode_name,
                            "mode_symbol": mode_symbol,
                            "difficulty": difficulty_name,
                            "difficulty_symbol": difficulty_symbol,
                            "difficulty_value": difficulty_value,
                        },
                        proof_scope="ordinary-client-real-peer",
                    )
                )

    combat_cells: list[dict[str, Any]] = []
    arena_inventory: list[dict[str, Any]] = []
    for arena in arena_rows:
        indices = _indices(arena["stage_symbol"], stage_values, stage_indices_by_symbol, solo_index_by_symbol)
        is_standard = arena["category"] in STANDARD_ARENA_CATEGORIES
        inventory_row = dict(arena)
        inventory_row["indices"] = indices
        inventory_row["standard_combat_simulator"] = is_standard
        inventory_row["exclusion_reason"] = None if is_standard else f"{arena['category'].lower()}_arena_inventory"
        arena_inventory.append(inventory_row)
        if not is_standard:
            continue
        for scenario_name, scenario_symbol in SCENARIOS:
            if scenario_symbol not in scenario_values:
                raise CoverageError(f"{scenario_symbol} is missing from constants.h")
            combat_cells.append(
                _base_cell(
                    "combat-simulator",
                    len(combat_cells),
                    indices,
                    catalog_id=arena["catalog_id"],
                    stage_symbol=arena["stage_symbol"],
                    dimensions={
                        "arena_index": arena["arena_index"],
                        "arena_category": arena["category"],
                        "scenario": scenario_name,
                        "scenario_symbol": scenario_symbol,
                        "scenario_value": scenario_values[scenario_symbol],
                    },
                    proof_scope="ordinary-client-combat-simulator",
                )
            )

    challenge_cells: list[dict[str, Any]] = []
    for challenge in challenge_rows:
        for player_count in range(1, 5):
            challenge_cells.append(
                _base_cell(
                    "challenge",
                    len(challenge_cells),
                    {"stage_table_index": None, "solo_stage_index": None, "stagenum": None},
                    dimensions={
                        "challenge_index": challenge["challenge_index"],
                        "name_langid": challenge["name_langid"],
                        "config_symbol": challenge["config_symbol"],
                        "config_value": challenge["config_value"],
                        "player_count": player_count,
                    },
                    proof_scope="ordinary-client-challenge",
                )
            )

    if len(solo_cells) != EXPECTED_COUNTS["solo"]:
        raise CoverageError(f"solo matrix count is {len(solo_cells)}, expected {EXPECTED_COUNTS['solo']}")
    if len(combat_cells) != EXPECTED_COUNTS["combat_simulator"]:
        raise CoverageError(f"Combat Simulator matrix count is {len(combat_cells)}, expected {EXPECTED_COUNTS['combat_simulator']}")
    if len(challenge_cells) != EXPECTED_COUNTS["challenges"]:
        raise CoverageError(f"challenge matrix count is {len(challenge_cells)}, expected {EXPECTED_COUNTS['challenges']}")
    if len(network_cells) != EXPECTED_COUNTS["network"]:
        raise CoverageError(f"network matrix count is {len(network_cells)}, expected {EXPECTED_COUNTS['network']}")

    files, source_fp = source_manifest(root)
    source_revision = _source_revision(root)
    if source_revision == "unknown":
        raise CoverageError("cannot create a release matrix without an exact source revision")
    binary_info: dict[str, Any] = {"path": None, "sha256": None}
    if binary is not None:
        binary_info = {"path": binary.name, "sha256": _sha256_bytes(binary.read_bytes())}

    derived_pd = []
    for solo_row in solo_rows:
        base_cell_ids = [
            _cell_id("solo", solo_row["solo_stage_index"] * len(DIFFICULTIES) + offset)
            for offset in range(len(DIFFICULTIES))
        ]
        derived_pd.append(
            {
                "derived_cell_id": f"solo-perfect-dark.{solo_row['solo_stage_index']:03d}",
                "source_cell_ids": base_cell_ids,
                "stage_symbol": solo_row["stage_symbol"],
                "catalog_id": solo_row["catalog_id"],
                "indices": _indices(solo_row["stage_symbol"], stage_values, stage_indices_by_symbol, solo_index_by_symbol),
                "difficulty": PERFECT_DARK[0],
                "difficulty_symbol": PERFECT_DARK[1],
                "difficulty_value": PERFECT_DARK[2],
                "proof_policy": "derived-only; does not satisfy the 63-cell solo ordinary-gameplay gate",
            }
        )

    matrix: dict[str, Any] = {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "tool_version": TOOL_VERSION,
        "matrix_id": "milestone-1-base-content-v1",
        "generated_at_utc": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "source": {
            "revision": source_revision,
            "fingerprint": source_fp,
            "files": files,
            "network_derivation": "g_SoloStages catalog rows; room start path resolves catalogStageIdByStagenum",
        },
        "binary": binary_info,
        "contract": {
            "expected_counts": EXPECTED_COUNTS,
            "solo_difficulties": [name for name, _, _ in DIFFICULTIES],
            "perfect_dark": "separately-derived",
            "combat_simulator_scenarios": [name for name, _ in SCENARIOS],
            "challenge_player_counts": [1, 2, 3, 4],
            "network_modes": [name for name, _ in NETWORK_MODES],
            "arena_inventory_count": len(arena_inventory),
            "standard_arena_count": len(standard_arenas),
        },
        "domains": [
            {"id": "stage-table", "description": "static s_StagesInit row index", "field": "indices.stage_table_index"},
            {"id": "solo-stage", "description": "g_SoloStages[] index", "field": "indices.solo_stage_index"},
            {"id": "stagenum", "description": "numeric STAGE_* value", "field": "indices.stagenum"},
        ],
        "arena_inventory": arena_inventory,
        "derived": {"solo_perfect_dark": derived_pd},
        "cells": solo_cells + combat_cells + challenge_cells + network_cells,
    }
    matrix["matrix_fingerprint"] = matrix_fingerprint(matrix)
    return matrix


def write_json(path: Path | None, value: Any) -> None:
    encoded = json.dumps(value, indent=2, sort_keys=True) + "\n"
    if path is None:
        sys.stdout.write(encoded)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(encoded, encoding="utf-8")


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise CoverageError(f"cannot read JSON {path}: {exc}") from exc


def _require_matrix(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict) or value.get("schema") != SCHEMA or value.get("schema_version") != SCHEMA_VERSION:
        raise CoverageError("input is not a version 1 base-content coverage matrix")
    expected = value.get("matrix_fingerprint")
    if not isinstance(expected, str) or expected != matrix_fingerprint(value):
        raise CoverageError("matrix_fingerprint does not match matrix contents")
    cells = value.get("cells")
    if not isinstance(cells, list) or not cells:
        raise CoverageError("coverage matrix has no cells")
    source = value.get("source")
    if not isinstance(source, dict) or not isinstance(source.get("revision"), str) or source["revision"] == "unknown":
        raise CoverageError("coverage matrix has no exact source revision")
    return value


def _cell_map(matrix: dict[str, Any]) -> dict[str, dict[str, Any]]:
    cells = matrix["cells"]
    result: dict[str, dict[str, Any]] = {}
    for cell in cells:
        cell_id = cell.get("cell_id")
        if not isinstance(cell_id, str) or cell_id in result:
            raise CoverageError(f"matrix has duplicate or invalid cell_id {cell_id!r}")
        result[cell_id] = cell
    return result


def shard_cells(matrix: dict[str, Any], shard_index: int, shard_count: int) -> list[dict[str, Any]]:
    if shard_count < 1 or shard_index < 0 or shard_index >= shard_count:
        raise CoverageError("shard index must be in the range [0, shard_count)")
    cells = matrix["cells"]
    return [cell for ordinal, cell in enumerate(cells) if ordinal % shard_count == shard_index]


def make_shard_plan(matrix: dict[str, Any], matrix_path: Path, shard_index: int, shard_count: int) -> dict[str, Any]:
    cells = shard_cells(matrix, shard_index, shard_count)
    matrix_arg = matrix_path.as_posix() if not matrix_path.is_absolute() else "<matrix-path>"
    planned_cells = []
    receipt_commands = []
    for cell in cells:
        cell_id = cell["cell_id"]
        planned_cell = copy.deepcopy(cell)
        planned_cell["runner_fixture_status"] = "missing"
        planned_cell["execution_status"] = "not_executable_until_fixture_exists"
        planned_cell["runner_fixture"] = None
        planned_cells.append(planned_cell)
        receipt_commands.append(
            {
                "cell_id": cell_id,
                "receipt_role": "convert a retained smoke observation into a durable receipt",
                "runner": SUPPORTED_SMOKE_RUNNER,
                "runner_fixture_status": "missing",
                "gameplay_command": None,
                "receipt_command": [
                    "python",
                    "tools/base_content_coverage.py",
                    "receipt",
                    "--matrix",
                    matrix_arg,
                    "--cell-id",
                    cell_id,
                    "--observation",
                    f"observations/{cell_id}.json",
                    "--artifact-base",
                    "<run-root>",
                    "--binary",
                    "<client-binary>",
                    "--out",
                    f"receipts/{cell_id}.receipt.json",
                ],
            }
        )
    return {
        "schema": WORK_PLAN_SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "plan_kind": "deterministic-coverage-work-plan",
        "status": "not_executable",
        "matrix_fingerprint": matrix["matrix_fingerprint"],
        "source_fingerprint": matrix["source"]["fingerprint"],
        "shard_index": shard_index,
        "shard_count": shard_count,
        "cell_count": len(cells),
        "cells": planned_cells,
        "gameplay_runner": SUPPORTED_SMOKE_RUNNER,
        "gameplay_commands": [],
        "receipt_commands": receipt_commands,
        "execution_policy": {
            "fixture_generation_and_runtime_driver_required": True,
            "ordinary_gameplay_only_for_validation": True,
            "static_and_accelerated_are_recorded_but_never_promoted": True,
            "timeout_or_crash_requires_retry": True,
        },
    }


def _classify_observation(observation: dict[str, Any]) -> tuple[str, bool, str]:
    raw_status = str(observation.get("status", "not_run"))
    execution_mode = str(observation.get("execution_mode", ""))
    proof_class = str(observation.get("proof_class", ""))
    timed_out = observation.get("timed_out")
    crashed = observation.get("crashed")
    exit_code = observation.get("exit_code")
    assertions_total = observation.get("assertions_total")
    assertions_met = observation.get("assertions_met")

    if timed_out is True:
        return "timeout", False, "runner timed out; no gameplay proof is accepted"
    if crashed is True:
        return "crash", False, "runner reported a crash; no gameplay proof is accepted"
    if execution_mode in {"static", "accelerated"} or proof_class in {"static", "accelerated"}:
        label = "static" if execution_mode == "static" or proof_class == "static" else "accelerated"
        return label, False, f"{label} evidence is retained but cannot promote to ordinary gameplay proof"
    if raw_status != "pass":
        return "failed", False, f"runner status is {raw_status!r}"
    if observation.get("runner") != SUPPORTED_SMOKE_RUNNER:
        return "not-ordinary-gameplay", False, "observation runner is not the supported smoke-verify runner"
    if not isinstance(timed_out, bool) or not isinstance(crashed, bool):
        return "not-ordinary-gameplay", False, "timeout and crash fields must be explicit booleans"
    if not isinstance(exit_code, int) or isinstance(exit_code, bool) or exit_code != 0:
        return "failed", False, f"runner exited with code {exit_code}"
    if (
        not isinstance(assertions_total, int)
        or isinstance(assertions_total, bool)
        or assertions_total <= 0
        or not isinstance(assertions_met, int)
        or isinstance(assertions_met, bool)
        or assertions_met != assertions_total
    ):
        return "failed", False, "assertions_total must be positive and assertions_met must match exactly"
    log_path = observation.get("log_path")
    evidence_paths = observation.get("evidence_paths")
    if not isinstance(log_path, str) or not log_path.strip():
        return "not-ordinary-gameplay", False, "ordinary gameplay receipt requires a retained log_path"
    if (
        not isinstance(evidence_paths, list)
        or not evidence_paths
        or any(not isinstance(path, str) or not path.strip() for path in evidence_paths)
    ):
        return "not-ordinary-gameplay", False, "ordinary gameplay receipt requires retained evidence_paths"
    if execution_mode != "ordinary-client" or proof_class != "ordinary-gameplay":
        return "not-ordinary-gameplay", False, "receipt is not explicitly ordinary-client ordinary-gameplay evidence"
    return "ordinary_gameplay_pass", True, "ordinary client completed the smoke/evidence contract"


def _artifact_record(value: Any, artifact_base: Path, kind: str) -> dict[str, Any]:
    if isinstance(value, Path):
        raw_path = value
    elif isinstance(value, str) and value.strip():
        raw_path = Path(value)
    else:
        raise CoverageError(f"{kind} artifact path must be a nonempty string")
    base = artifact_base.resolve()
    candidate = (raw_path if raw_path.is_absolute() else base / raw_path).resolve()
    try:
        relative = candidate.relative_to(base).as_posix()
    except ValueError as exc:
        raise CoverageError(f"{kind} artifact {value!r} is outside artifact base {base}") from exc
    if not candidate.is_file():
        raise CoverageError(f"{kind} artifact is not a readable file: {value!r}")
    try:
        data = candidate.read_bytes()
    except OSError as exc:
        raise CoverageError(f"{kind} artifact is not readable: {value!r}: {exc}") from exc
    return {"kind": kind, "path": relative, "size_bytes": len(data), "sha256": _sha256_bytes(data)}


def _normalized_observation_and_artifacts(
    observation: dict[str, Any], artifact_base: Path
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    log_record = _artifact_record(observation.get("log_path"), artifact_base, "log")
    evidence_paths = observation.get("evidence_paths")
    if not isinstance(evidence_paths, list):
        raise CoverageError("evidence_paths must be a list")
    evidence_records = [_artifact_record(path, artifact_base, "evidence") for path in evidence_paths]
    normalized = copy.deepcopy(observation)
    normalized["log_path"] = log_record["path"]
    normalized["evidence_paths"] = [record["path"] for record in evidence_records]
    return normalized, [log_record, *evidence_records]


def _validate_artifact_record_shape(record: Any) -> None:
    if not isinstance(record, dict):
        raise CoverageError("retained artifact record must be an object")
    if set(record) != {"kind", "path", "size_bytes", "sha256"}:
        raise CoverageError("retained artifact record has a non-canonical shape")
    kind = record.get("kind")
    relative = record.get("path")
    if (
        not isinstance(kind, str)
        or not kind
        or not isinstance(relative, str)
        or not relative
        or not isinstance(record.get("size_bytes"), int)
        or isinstance(record.get("size_bytes"), bool)
        or record["size_bytes"] < 0
        or not isinstance(record.get("sha256"), str)
        or re.fullmatch(r"[0-9A-F]{64}", record["sha256"]) is None
    ):
        raise CoverageError("retained artifact record has no canonical kind/path")


def _verify_artifact_record(record: Any, artifact_base: Path) -> None:
    _validate_artifact_record_shape(record)
    kind = record["kind"]
    relative = record["path"]
    if Path(relative).is_absolute() or Path(relative).as_posix() != relative:
        raise CoverageError(f"retained artifact path is not portable: {relative!r}")
    current = _artifact_record(relative, artifact_base, kind)
    if current != record:
        raise CoverageError(f"retained {kind} artifact drifted: {relative}")


def _validate_retained_artifacts(
    receipt: dict[str, Any], artifact_base: Path | None = None
) -> None:
    records = receipt.get("retained_artifacts")
    if not isinstance(records, list):
        raise CoverageError(f"receipt {receipt.get('cell_id')!r} has no retained_artifacts list")
    observation = receipt.get("observation")
    accepted = receipt.get("accepted_ordinary_gameplay") is True
    if not accepted:
        if records:
            raise CoverageError(f"receipt {receipt.get('cell_id')!r} has artifacts despite rejected gameplay proof")
        return
    if receipt.get("artifact_base") != ".":
        raise CoverageError(f"receipt {receipt.get('cell_id')!r} has a non-portable artifact base")
    if not isinstance(observation, dict) or len(records) < 2:
        raise CoverageError(f"receipt {receipt.get('cell_id')!r} has incomplete retained artifacts")
    expected_kinds = ["observation", "log"] + ["evidence"] * len(observation.get("evidence_paths", []))
    if [record.get("kind") if isinstance(record, dict) else None for record in records] != expected_kinds:
        raise CoverageError(f"receipt {receipt.get('cell_id')!r} has non-canonical retained artifact kinds")
    if records[1].get("path") != observation.get("log_path"):
        raise CoverageError(f"receipt {receipt.get('cell_id')!r} log path does not match retained artifact")
    if [record.get("path") for record in records[2:]] != observation.get("evidence_paths"):
        raise CoverageError(f"receipt {receipt.get('cell_id')!r} evidence paths do not match retained artifacts")
    if artifact_base is None:
        raise CoverageError(
            f"accepted receipt {receipt.get('cell_id')!r} requires artifact_base to verify retained artifacts"
        )
    for record in records:
        _verify_artifact_record(record, artifact_base)
    observation_path = (artifact_base.resolve() / records[0]["path"]).resolve()
    try:
        on_disk = json.loads(observation_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise CoverageError(f"retained observation artifact cannot be re-read: {records[0]['path']}") from exc
    if not isinstance(on_disk, dict):
        raise CoverageError("retained observation artifact must contain an object")
    normalized, child_records = _normalized_observation_and_artifacts(on_disk, artifact_base)
    if normalized != observation or child_records != records[1:]:
        raise CoverageError(f"receipt {receipt.get('cell_id')!r} observation/artifact structure drifted")


def make_receipt(
    matrix: dict[str, Any],
    cell_id: str,
    observation: dict[str, Any],
    binary: Path,
    *,
    artifact_base: Path | None = None,
    observation_path: Path | None = None,
) -> dict[str, Any]:
    source = matrix.get("source")
    if not isinstance(source, dict) or not isinstance(source.get("revision"), str) or source["revision"] == "unknown":
        raise CoverageError("cannot create a receipt without an exact source revision")
    cells = _cell_map(matrix)
    if cell_id not in cells:
        raise CoverageError(f"receipt cell_id {cell_id!r} is not in the matrix")
    try:
        binary_data = binary.read_bytes()
    except OSError as exc:
        raise CoverageError(f"cannot fingerprint receipt binary {binary}: {exc}") from exc
    binary_fp = _sha256_bytes(binary_data)
    classification, accepted, reason = _classify_observation(observation)
    normalized_observation = observation
    retained_artifacts: list[dict[str, Any]] = []
    if accepted:
        if artifact_base is None or observation_path is None:
            raise CoverageError("ordinary gameplay receipt requires observation_path and artifact_base")
        on_disk = read_json(observation_path)
        if on_disk != observation:
            raise CoverageError("observation object differs from observation_path JSON")
        observation_record = _artifact_record(observation_path, artifact_base, "observation")
        normalized_observation, child_records = _normalized_observation_and_artifacts(observation, artifact_base)
        retained_artifacts = [observation_record, *child_records]
    return {
        "schema": RECEIPT_SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "matrix_fingerprint": matrix["matrix_fingerprint"],
        "source_revision": matrix["source"]["revision"],
        "source_fingerprint": matrix["source"]["fingerprint"],
        "binary_fingerprint": {"path": str(binary.resolve()), "size_bytes": len(binary_data), "sha256": binary_fp},
        "cell_id": cell_id,
        "classification": classification,
        "accepted_ordinary_gameplay": accepted,
        "classification_reason": reason,
        "artifact_base": ".",
        "observation": normalized_observation,
        "retained_artifacts": retained_artifacts,
        "cell": cells[cell_id],
        "recorded_at_utc": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
    }


def validate_receipt(matrix: dict[str, Any], receipt: dict[str, Any], artifact_base: Path | None = None) -> None:
    if not isinstance(receipt, dict) or receipt.get("schema") != RECEIPT_SCHEMA or receipt.get("schema_version") != SCHEMA_VERSION:
        raise CoverageError("input is not a version 1 base-content coverage receipt")
    if receipt.get("matrix_fingerprint") != matrix["matrix_fingerprint"]:
        raise CoverageError(f"receipt {receipt.get('cell_id')!r} belongs to a different matrix")
    if receipt.get("source_revision") != matrix["source"]["revision"]:
        raise CoverageError(f"receipt {receipt.get('cell_id')!r} has a different source revision")
    if receipt.get("source_fingerprint") != matrix["source"]["fingerprint"]:
        raise CoverageError(f"receipt {receipt.get('cell_id')!r} has a different source fingerprint")
    cell_id = receipt.get("cell_id")
    cells = _cell_map(matrix)
    if cell_id not in cells:
        raise CoverageError(f"receipt cell_id {cell_id!r} is not in the matrix")
    if receipt.get("cell") != cells[cell_id]:
        raise CoverageError(f"receipt {cell_id!r} embeds a cell different from the matrix")
    binary_info = receipt.get("binary_fingerprint")
    if not isinstance(binary_info, dict) or not binary_info.get("sha256"):
        raise CoverageError(f"receipt {cell_id!r} has no binary fingerprint")
    matrix_binary = matrix.get("binary", {}).get("sha256")
    if matrix_binary and binary_info["sha256"] != matrix_binary:
        raise CoverageError(f"receipt {cell_id!r} does not match the matrix binary fingerprint")
    observation = receipt.get("observation")
    if not isinstance(observation, dict):
        raise CoverageError(f"receipt {cell_id!r} has no observation object")
    _validate_retained_artifacts(receipt, artifact_base)
    classification, accepted, reason = _classify_observation(observation)
    if receipt.get("classification") != classification or receipt.get("accepted_ordinary_gameplay") != accepted:
        raise CoverageError(f"receipt {cell_id!r} classification is not derived from its observation")
    if receipt.get("classification_reason") != reason:
        raise CoverageError(f"receipt {cell_id!r} classification reason is not canonical")


def _receipt_files(inputs: Sequence[Path]) -> list[Path]:
    paths: list[Path] = []
    for input_path in inputs:
        if input_path.is_dir():
            paths.extend(sorted(input_path.rglob("*.receipt.json")))
            paths.extend(sorted(input_path.rglob("*.receipts.jsonl")))
        else:
            paths.append(input_path)
    return sorted(set(paths), key=lambda path: str(path).lower())


def load_receipts(
    matrix: dict[str, Any], inputs: Sequence[Path], artifact_base: Path | None = None
) -> list[dict[str, Any]]:
    receipts: list[dict[str, Any]] = []
    seen: set[str] = set()
    for path in _receipt_files(inputs):
        try:
            raw = path.read_text(encoding="utf-8")
        except OSError as exc:
            raise CoverageError(f"cannot read receipt file {path}: {exc}") from exc
        values: list[Any]
        try:
            if path.name.endswith(".jsonl"):
                values = [json.loads(line) for line in raw.splitlines() if line.strip()]
            else:
                values = [json.loads(raw)]
        except json.JSONDecodeError as exc:
            raise CoverageError(f"malformed receipt JSON in {path}: {exc}") from exc
        for value in values:
            validate_receipt(matrix, value, artifact_base)
            cell_id = value["cell_id"]
            if cell_id in seen:
                raise CoverageError(f"overlap rejected: more than one receipt claims {cell_id}")
            seen.add(cell_id)
            receipts.append(value)
    return receipts


def aggregate(
    matrix: dict[str, Any], receipts: Sequence[dict[str, Any]], artifact_base: Path | None = None
) -> dict[str, Any]:
    cells = _cell_map(matrix)
    by_id: dict[str, dict[str, Any]] = {}
    for receipt in receipts:
        validate_receipt(matrix, receipt, artifact_base)
        cell_id = receipt["cell_id"]
        if cell_id in by_id:
            raise CoverageError(f"overlap rejected: more than one receipt claims {cell_id}")
        by_id[cell_id] = receipt
    binary_fingerprints = {receipt["binary_fingerprint"]["sha256"] for receipt in receipts}
    if len(binary_fingerprints) > 1:
        raise CoverageError("binary overlap rejected: aggregate contains more than one client fingerprint")

    family_summary: dict[str, dict[str, int]] = {}
    for cell in matrix["cells"]:
        family = cell["family"]
        summary = family_summary.setdefault(
            family,
            {"total": 0, "ordinary_gameplay_pass": 0, "receipted": 0, "missing_receipt": 0},
        )
        summary["total"] += 1
        receipt = by_id.get(cell["cell_id"])
        if receipt is None:
            summary["missing_receipt"] += 1
        else:
            summary["receipted"] += 1
            if receipt["accepted_ordinary_gameplay"]:
                summary["ordinary_gameplay_pass"] += 1

    accepted = sum(1 for receipt in receipts if receipt["accepted_ordinary_gameplay"])
    complete = accepted == len(cells) and len(receipts) == len(cells)
    return {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "matrix_fingerprint": matrix["matrix_fingerprint"],
        "source_revision": matrix["source"]["revision"],
        "source_fingerprint": matrix["source"]["fingerprint"],
        "binary_fingerprints": sorted(binary_fingerprints),
        "total_cells": len(cells),
        "receipt_count": len(receipts),
        "ordinary_gameplay_pass_count": accepted,
        "ordinary_gameplay_complete": complete,
        "status": "validated" if complete else ("partial" if receipts else "not_run"),
        "promotion_policy": "only ordinary-client ordinary-gameplay receipts can validate cells; static and accelerated receipts never promote",
        "family_summary": family_summary,
        "receipts": [
            {
                "cell_id": receipt["cell_id"],
                "classification": receipt["classification"],
                "accepted_ordinary_gameplay": receipt["accepted_ordinary_gameplay"],
                "binary_sha256": receipt["binary_fingerprint"]["sha256"],
                "evidence": receipt["observation"].get("evidence_paths", []),
            }
            for receipt in sorted(receipts, key=lambda value: value["cell_id"])
        ],
    }


def pending_plan(
    matrix: dict[str, Any], receipts: Sequence[dict[str, Any]], artifact_base: Path | None = None
) -> dict[str, Any]:
    for receipt in receipts:
        validate_receipt(matrix, receipt, artifact_base)
    accepted = {receipt["cell_id"] for receipt in receipts if receipt["accepted_ordinary_gameplay"]}
    pending = [cell for cell in matrix["cells"] if cell["cell_id"] not in accepted]
    return {
        "schema": SHARD_SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "matrix_fingerprint": matrix["matrix_fingerprint"],
        "source_fingerprint": matrix["source"]["fingerprint"],
        "mode": "resume",
        "completed_cell_ids": sorted(accepted),
        "pending_cell_count": len(pending),
        "pending_cells": pending,
        "policy": "failed, timed-out, crashed, static, accelerated, and non-ordinary receipts remain pending",
    }


def _path(value: str | None) -> Path | None:
    return Path(value) if value else None


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    plan = subparsers.add_parser("plan", help="derive and write the live source matrix")
    plan.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    plan.add_argument("--out", type=Path)
    plan.add_argument("--binary", type=Path)

    shard = subparsers.add_parser("shard", help="write one deterministic coverage work plan")
    shard.add_argument("--matrix", type=Path, required=True)
    shard.add_argument("--shard-index", type=int, required=True)
    shard.add_argument("--shard-count", type=int, required=True)
    shard.add_argument("--out", type=Path)

    resume = subparsers.add_parser("resume", help="write the pending-cell plan from accepted receipts")
    resume.add_argument("--matrix", type=Path, required=True)
    resume.add_argument("--receipts", type=Path, nargs="+", required=True)
    resume.add_argument("--artifact-base", type=Path)
    resume.add_argument("--out", type=Path)

    merge = subparsers.add_parser("merge", help="validate receipts and write an indexed aggregate")
    merge.add_argument("--matrix", type=Path, required=True)
    merge.add_argument("--receipts", type=Path, nargs="+", required=True)
    merge.add_argument("--artifact-base", type=Path)
    merge.add_argument("--out", type=Path)

    receipt = subparsers.add_parser("receipt", help="convert one smoke observation into a durable receipt")
    receipt.add_argument("--matrix", type=Path, required=True)
    receipt.add_argument("--cell-id", required=True)
    receipt.add_argument("--observation", type=Path, required=True)
    receipt.add_argument("--binary", type=Path, required=True)
    receipt.add_argument("--artifact-base", type=Path)
    receipt.add_argument("--out", type=Path, required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "plan":
            write_json(args.out, build_matrix(args.root, args.binary))
        elif args.command == "shard":
            matrix = _require_matrix(read_json(args.matrix))
            write_json(args.out, make_shard_plan(matrix, args.matrix, args.shard_index, args.shard_count))
        elif args.command == "resume":
            matrix = _require_matrix(read_json(args.matrix))
            receipts = load_receipts(matrix, args.receipts, args.artifact_base)
            write_json(args.out, pending_plan(matrix, receipts, args.artifact_base))
        elif args.command == "merge":
            matrix = _require_matrix(read_json(args.matrix))
            receipts = load_receipts(matrix, args.receipts, args.artifact_base)
            write_json(args.out, aggregate(matrix, receipts, args.artifact_base))
        elif args.command == "receipt":
            matrix = _require_matrix(read_json(args.matrix))
            observation = read_json(args.observation)
            if not isinstance(observation, dict):
                raise CoverageError("observation JSON must be an object")
            artifact_base = args.artifact_base or args.observation.parent
            receipt = make_receipt(
                matrix,
                args.cell_id,
                observation,
                args.binary,
                artifact_base=artifact_base,
                observation_path=args.observation,
            )
            write_json(args.out, receipt)
        else:
            raise CoverageError(f"unknown command {args.command!r}")
    except CoverageError as exc:
        print(f"base-content-coverage: error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
