#!/usr/bin/env python3
"""Shared validation for editable ``model.render.json`` command streams."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass


SCHEMA_LEGACY = 1
SCHEMA_GEOMETRY = 2
SCHEMA_CURRENT = 3
G_LIGHTING = 0x00020000
G_TEXTURE_GEN = 0x00040000
G_TEXTURE_GEN_LINEAR = 0x00080000
VERTEX_LOAD_GEOMETRY_MASK = 0x000F0000
VERTEX_CACHE_SLOTS = 64
MAX_MATRIX_INDEX = 32766
TYPE3_COMPILER_BASELINE_MODE = G_LIGHTING
TYPE3_COMPILER_BASELINE_KNOWN = (
    G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR
)
RENDER_NODE_TYPES = {0x04, 0x16, 0x18}

_BASE_COMMANDS = {"mtx", "pop", "material", "tri"}
_GEOMETRY_COMMANDS = {"geometry_set", "geometry_clear"}
_VERTEX_CACHE_COMMANDS = {
    "vertex_load", "vertex_scope", "vertex_cache_reset",
}


class RenderStreamError(ValueError):
    """A public render stream violates its typed schema contract."""


@dataclass(frozen=True)
class RenderStreamReport:
    schema_version: int
    command_count: int
    tri_faces: tuple[int, ...]
    triangle_groups: tuple[str, ...]
    matrix_command_count: int
    geometry_command_counts: dict[str, int]
    vertex_load_counts: dict[str, int]
    vertex_scope_counts: dict[str, int]
    vertex_cache_reset_counts: dict[str, int]
    retained_vertex_corner_counts: dict[str, int]
    triangle_lighting: dict[str, tuple[str, ...]]


def _is_u32(value: object) -> bool:
    return (isinstance(value, int) and not isinstance(value, bool) and
            0 <= value <= 0xFFFFFFFF)


def _u32_array3(command: dict, field: str, index: int) -> list[int]:
    values = command.get(field)
    if (not isinstance(values, list) or len(values) != 3 or
            any(not _is_u32(value) for value in values)):
        raise RenderStreamError(
            f"model.render.json tri command {index} has invalid "
            f"{field} {values!r}"
        )
    return values


def validate_render_stream(
        root: object,
        face_count: int,
        group_render_modes: Mapping[str, int] | None = None,
) -> RenderStreamReport:
    """Validate one render stream and return its normalized semantic report."""

    if not isinstance(root, dict):
        raise RenderStreamError("model.render.json root must be an object")
    if root.get("pd_kind") != "mesh_render_commands":
        raise RenderStreamError(
            "model.render.json must declare pd_kind mesh_render_commands"
        )

    schema_marker = object()
    schema = root.get("pd_schema_version", schema_marker)
    if schema is schema_marker:
        legacy_schema = root.get("schema")
        if legacy_schema == "pd2.mesh.render.v1":
            schema = SCHEMA_LEGACY
        else:
            raise RenderStreamError(
                "model.render.json has unsupported schema identity "
                f"{legacy_schema!r}"
            )
    if (isinstance(schema, bool) or not isinstance(schema, int) or
            schema not in {SCHEMA_LEGACY, SCHEMA_GEOMETRY, SCHEMA_CURRENT}):
        raise RenderStreamError(
            "model.render.json has unsupported pd_schema_version "
            f"{schema!r}"
        )
    if (isinstance(face_count, bool) or not isinstance(face_count, int) or
            face_count < 0):
        raise ValueError("face_count must be a non-negative integer")

    commands = root.get("commands")
    if not isinstance(commands, list) or not commands:
        raise RenderStreamError(
            "model.render.json must contain a non-empty commands array"
        )

    allowed_commands = set(_BASE_COMMANDS)
    if schema >= SCHEMA_GEOMETRY:
        allowed_commands.update(_GEOMETRY_COMMANDS)
    if schema >= SCHEMA_CURRENT:
        allowed_commands.update(_VERTEX_CACHE_COMMANDS)

    tri_faces: list[int] = []
    triangle_groups: list[str] = []
    matrix_commands = 0
    geometry_mode_by_group: dict[str, int] = {}
    geometry_known_by_group: dict[str, int] = {}
    geometry_counts: dict[str, int] = {}
    vertex_load_counts: dict[str, int] = {}
    vertex_scope_counts: dict[str, int] = {}
    vertex_cache_reset_counts: dict[str, int] = {}
    retained_vertex_corner_counts: dict[str, int] = {}
    cache_generation_by_group: dict[str, int] = {}
    vertex_slots_by_group: dict[
        str, list[tuple[int, int, int] | None]
    ] = {}
    triangle_lighting: dict[str, list[str]] = {}

    for index, command in enumerate(commands):
        if not isinstance(command, dict):
            raise RenderStreamError(
                f"model.render.json command {index} is not an object"
            )

        name = command.get("command")
        if name not in allowed_commands:
            raise RenderStreamError(
                f"model.render.json command {index} has invalid command {name!r}"
            )
        group = command.get("group")
        if not isinstance(group, str) or not group.strip():
            raise RenderStreamError(
                f"model.render.json command {index} has invalid group {group!r}"
            )
        if group_render_modes is not None and group not in group_render_modes:
            raise RenderStreamError(
                f"model.render.json command {index} group {group!r} "
                "does not have one unique render owner"
            )

        if "matrix_flags" in command:
            flags = command.get("matrix_flags")
            if (isinstance(flags, bool) or not isinstance(flags, int) or
                    flags < 0 or flags > 255):
                raise RenderStreamError(
                    f"model.render.json command {index} has invalid "
                    f"matrix_flags {flags!r}"
                )
        if name == "tri" and "matrix_index" not in command:
            raise RenderStreamError(
                f"model.render.json tri command {index} is missing "
                "matrix_index"
            )
        if name in {"mtx", "tri"} and "matrix_index" in command:
            matrix_index = command.get("matrix_index")
            if (isinstance(matrix_index, bool) or
                    not isinstance(matrix_index, int) or
                    matrix_index < (-1 if name == "mtx" else 0) or
                    matrix_index > MAX_MATRIX_INDEX):
                raise RenderStreamError(
                    f"model.render.json command {index} has invalid "
                    f"matrix_index {matrix_index!r}"
                )

        if name == "mtx":
            matrix_commands += 1
            continue

        if name in _GEOMETRY_COMMANDS:
            geometry_mask = command.get("geometry_mask")
            if not _is_u32(geometry_mask):
                raise RenderStreamError(
                    f"model.render.json command {index} has invalid "
                    f"geometry_mask {geometry_mask!r}"
                )
            geometry_counts[group] = geometry_counts.get(group, 0) + 1
            prior_mode = geometry_mode_by_group.get(group, 0)
            geometry_mode_by_group[group] = (
                prior_mode | geometry_mask
                if name == "geometry_set"
                else prior_mode & ~geometry_mask
            )
            geometry_known_by_group[group] = (
                geometry_known_by_group.get(group, 0) | geometry_mask
            )
            continue

        if name == "vertex_load":
            slot_first = command.get("slot_first")
            slot_count = command.get("slot_count")
            if (isinstance(slot_first, bool) or
                    not isinstance(slot_first, int) or slot_first < 0 or
                    isinstance(slot_count, bool) or
                    not isinstance(slot_count, int) or slot_count <= 0 or
                    slot_first >= VERTEX_CACHE_SLOTS or
                    slot_count > VERTEX_CACHE_SLOTS - slot_first):
                raise RenderStreamError(
                    f"model.render.json vertex_load command {index} has "
                    f"invalid slot range {slot_first!r}+{slot_count!r}"
                )
            slots = vertex_slots_by_group.setdefault(
                group, [None] * VERTEX_CACHE_SLOTS
            )
            snapshot = (
                geometry_mode_by_group.get(group, 0),
                geometry_known_by_group.get(group, 0),
                cache_generation_by_group.get(group, 0),
            )
            for slot in range(slot_first, slot_first + slot_count):
                slots[slot] = snapshot
            vertex_load_counts[group] = vertex_load_counts.get(group, 0) + 1
            continue

        if name == "vertex_scope":
            render_mode = (group_render_modes or {}).get(group)
            if (isinstance(render_mode, bool) or
                    not isinstance(render_mode, int) or render_mode != 3):
                raise RenderStreamError(
                    f"model.render.json vertex_scope command {index} group "
                    f"{group!r} must have exactly one Type-3 render owner"
                )
            if vertex_scope_counts.get(group, 0):
                raise RenderStreamError(
                    f"model.render.json vertex_scope command {index} group "
                    f"{group!r} repeats the single Type-3 list boundary"
                )
            geometry_mode_by_group[group] = 0
            geometry_known_by_group[group] = 0
            cache_generation_by_group[group] = (
                cache_generation_by_group.get(group, 0) + 1
            )
            vertex_scope_counts[group] = vertex_scope_counts.get(group, 0) + 1
            vertex_slots_by_group.setdefault(
                group, [None] * VERTEX_CACHE_SLOTS
            )
            continue

        if name == "vertex_cache_reset":
            render_mode = (group_render_modes or {}).get(group)
            if (isinstance(render_mode, bool) or
                    not isinstance(render_mode, int) or render_mode == 3):
                raise RenderStreamError(
                    f"model.render.json vertex_cache_reset command {index} "
                    f"group {group!r} requires one non-Type-3 render owner"
                )
            if vertex_cache_reset_counts.get(group, 0):
                raise RenderStreamError(
                    f"model.render.json vertex_cache_reset command {index} "
                    f"group {group!r} repeats the single list boundary"
                )
            geometry_mode_by_group[group] = 0
            geometry_known_by_group[group] = 0
            cache_generation_by_group[group] = (
                cache_generation_by_group.get(group, 0) + 1
            )
            vertex_slots_by_group[group] = [None] * VERTEX_CACHE_SLOTS
            vertex_cache_reset_counts[group] = (
                vertex_cache_reset_counts.get(group, 0) + 1
            )
            continue

        if name != "tri":
            continue

        face_index = command.get("face_index")
        if (isinstance(face_index, bool) or not isinstance(face_index, int) or
                face_index < 0 or face_index >= face_count):
            raise RenderStreamError(
                f"model.render.json tri command {index} has invalid "
                f"face_index {face_index!r}"
            )
        tri_faces.append(face_index)
        triangle_groups.append(group)

        if schema < SCHEMA_GEOMETRY:
            continue

        vertex_mode = _u32_array3(command, "vertex_geometry_mode", index)
        vertex_known = _u32_array3(command, "vertex_geometry_known", index)
        cache_slots = (
            _u32_array3(command, "vertex_cache_slots", index)
            if schema >= SCHEMA_CURRENT else None
        )
        states = triangle_lighting.setdefault(group, [])
        for corner in range(3):
            if vertex_mode[corner] & ~vertex_known[corner]:
                raise RenderStreamError(
                    f"model.render.json tri command {index} corner {corner} "
                    "mode exceeds its known mask"
                )
            draw_known = geometry_known_by_group.get(group, 0)
            expected_mode = geometry_mode_by_group.get(group, 0)
            expected_known = draw_known
            if cache_slots is not None:
                slot = cache_slots[corner]
                slots = vertex_slots_by_group.setdefault(
                    group, [None] * VERTEX_CACHE_SLOTS
                )
                if slot >= VERTEX_CACHE_SLOTS or slots[slot] is None:
                    raise RenderStreamError(
                        f"model.render.json tri command {index} corner "
                        f"{corner} references unloaded vertex slot {slot}"
                    )
                expected_mode, expected_known, load_generation = slots[slot]
                if load_generation < cache_generation_by_group.get(group, 0):
                    retained_vertex_corner_counts[group] = (
                        retained_vertex_corner_counts.get(group, 0) + 1
                    )
            corner_known = vertex_known[corner] & VERTEX_LOAD_GEOMETRY_MASK
            expected_known &= VERTEX_LOAD_GEOMETRY_MASK
            if (corner_known != expected_known or
                    ((vertex_mode[corner] ^ expected_mode) &
                     corner_known & VERTEX_LOAD_GEOMETRY_MASK)):
                raise RenderStreamError(
                    f"model.render.json tri command {index} corner {corner} "
                    "has ambiguous G_VTX geometry state"
                )
            baseline_known = (
                TYPE3_COMPILER_BASELINE_KNOWN
                if (group_render_modes or {}).get(group) == 3 else 0
            )
            load_effective_known = (
                corner_known | baseline_known
            ) & VERTEX_LOAD_GEOMETRY_MASK
            draw_effective_known = (
                draw_known | baseline_known
            ) & VERTEX_LOAD_GEOMETRY_MASK
            if load_effective_known != draw_effective_known:
                raise RenderStreamError(
                    f"model.render.json tri command {index} corner {corner} "
                    "has unrelocatable G_VTX geometry state"
                )
            if not (corner_known & G_LIGHTING):
                if (schema == SCHEMA_CURRENT and
                        (group_render_modes or {}).get(group) == 3):
                    states.append("baseline")
                else:
                    states.append("inherited")
            elif vertex_mode[corner] & G_LIGHTING:
                states.append("on")
            else:
                states.append("off")

    if len(tri_faces) != face_count or len(set(tri_faces)) != face_count:
        raise RenderStreamError(
            "model.render.json must reference every model.faces.json face "
            "exactly once"
        )

    return RenderStreamReport(
        schema_version=schema,
        command_count=len(commands),
        tri_faces=tuple(tri_faces),
        triangle_groups=tuple(triangle_groups),
        matrix_command_count=matrix_commands,
        geometry_command_counts=geometry_counts,
        vertex_load_counts=vertex_load_counts,
        vertex_scope_counts=vertex_scope_counts,
        vertex_cache_reset_counts=vertex_cache_reset_counts,
        retained_vertex_corner_counts=retained_vertex_corner_counts,
        triangle_lighting={
            group: tuple(states) for group, states in triangle_lighting.items()
        },
    )
