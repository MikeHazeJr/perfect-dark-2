#!/usr/bin/env python3
"""Validate public .pdanim source payloads without launching the game."""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import zipfile
from collections import Counter
from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path


CATALOG_ID_RE = re.compile(r"^[a-z0-9_]+:[a-z0-9_./-]+$")
GLB_MAGIC = 0x46546C67
GLB_JSON_CHUNK = 0x4E4F534A
PDANIM_CHR_GENERATOR = "Perfect Dark 2 pdanim_chr semantic extractor v4"


@dataclass
class PdanimStats:
    archives: int = 0
    character: int = 0
    weapon: int = 0
    gltf: int = 0
    glb: int = 0
    commands: int = 0
    animations: int = 0
    channels: int = 0
    samplers: int = 0
    nodes: int = 0
    command_rows: int = 0
    target_paths: Counter[str] = field(default_factory=Counter)
    stale_generators: Counter[str] = field(default_factory=Counter)
    stale_generator_examples: dict[str, list[str]] = field(default_factory=dict)


def record_stale_generator(stats: PdanimStats, label: str, generator: str) -> None:
    stats.stale_generators[generator] += 1
    examples = stats.stale_generator_examples.setdefault(generator, [])
    if len(examples) < 5:
        examples.append(label)


def parse_ini(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("[") or line.startswith("#"):
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def gltf_json_from_bytes(data: bytes) -> dict[str, object]:
    if len(data) >= 12:
        magic, version, total_length = struct.unpack_from("<III", data, 0)
        if magic == GLB_MAGIC:
            if version != 2 or total_length > len(data):
                raise ValueError("invalid GLB header")
            offset = 12
            while offset + 8 <= total_length:
                chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
                offset += 8
                if offset + chunk_length > total_length:
                    raise ValueError("invalid GLB chunk length")
                chunk = data[offset:offset + chunk_length]
                offset += (chunk_length + 3) & ~3
                if chunk_type == GLB_JSON_CHUNK:
                    return json.loads(chunk.decode("utf-8"))
            raise ValueError("GLB has no JSON chunk")
    return json.loads(data.decode("utf-8"))


def iter_pdanim_inputs(paths: Iterable[Path]) -> Iterable[Path]:
    for path in paths:
        if path.is_dir():
            yield from sorted(path.rglob("*.pdanim"))
        else:
            yield path


def require_catalog_id(errors: list[str], label: str, value: object) -> None:
    if not isinstance(value, str) or not CATALOG_ID_RE.match(value):
        errors.append(f"{label} must be a catalog ID string")


def accessor_at(accessors: list[object], index: object) -> dict[str, object] | None:
    if not isinstance(index, int) or index < 0 or index >= len(accessors):
        return None
    accessor = accessors[index]
    return accessor if isinstance(accessor, dict) else None


def validate_commands(
    label: str,
    archive: zipfile.ZipFile,
    ini: dict[str, str],
    stats: PdanimStats,
    errors: list[str],
) -> None:
    if "commands.json" not in archive.namelist():
        errors.append(f"{label} weapon animation is missing commands.json")
        return

    try:
        source = json.loads(archive.read("commands.json").decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        errors.append(f"{label} commands.json is invalid JSON: {exc}")
        return

    commands = source.get("commands")
    if not isinstance(commands, list) or not commands:
        errors.append(f"{label} commands.json must contain non-empty commands")
        return

    declared = ini.get("command_count")
    if declared is not None:
        try:
            declared_count = int(declared)
        except ValueError:
            declared_count = -1
        if declared_count != len(commands):
            errors.append(
                f"{label} command_count {declared!r} does not match "
                f"commands.json count {len(commands)}"
            )

    for index, command in enumerate(commands):
        if not isinstance(command, dict):
            errors.append(f"{label} command {index} must be an object")
            continue
        name = command.get("command")
        if not isinstance(name, str) or not name:
            errors.append(f"{label} command {index} is missing command name")
        if "animation" in command:
            require_catalog_id(errors, f"{label} command {index} animation", command["animation"])
        if "sound" in command:
            require_catalog_id(errors, f"{label} command {index} sound", command["sound"])

    stats.commands += 1
    stats.command_rows += len(commands)


def validate_gltf_animation(
    label: str,
    data: bytes,
    stats: PdanimStats,
    errors: list[str],
) -> None:
    try:
        gltf = gltf_json_from_bytes(data)
    except (ValueError, json.JSONDecodeError, UnicodeDecodeError) as exc:
        errors.append(f"{label} animation source is invalid glTF/GLB: {exc}")
        return

    asset = gltf.get("asset", {})
    generator = asset.get("generator", "") if isinstance(asset, dict) else ""
    if isinstance(generator, str) and generator.startswith("Perfect Dark 2 pdanim_chr"):
        if generator != PDANIM_CHR_GENERATOR:
            record_stale_generator(stats, label, generator)

    animations = gltf.get("animations")
    nodes = gltf.get("nodes", [])
    accessors = gltf.get("accessors", [])
    if not isinstance(animations, list) or not animations:
        errors.append(f"{label} must contain at least one glTF animation")
        return
    if not isinstance(nodes, list) or not nodes:
        errors.append(f"{label} must contain animation target nodes")
    if not isinstance(accessors, list):
        errors.append(f"{label} accessors must be a list")
        accessors = []

    stats.animations += len(animations)
    stats.nodes += len(nodes) if isinstance(nodes, list) else 0

    for anim_index, animation in enumerate(animations):
        if not isinstance(animation, dict):
            errors.append(f"{label} animation {anim_index} must be an object")
            continue
        channels = animation.get("channels")
        samplers = animation.get("samplers")
        if not isinstance(channels, list):
            errors.append(f"{label} animation {anim_index} channels must be a list")
            continue
        if not isinstance(samplers, list):
            errors.append(f"{label} animation {anim_index} samplers must be a list")
            continue
        if not channels:
            extras = gltf.get("extras", {})
            if not isinstance(extras, dict) or extras.get("pd_zero_frame_placeholder") is not True:
                errors.append(f"{label} animation {anim_index} has no channels")
            continue
        if not samplers:
            errors.append(f"{label} animation {anim_index} has no samplers")
            continue

        stats.channels += len(channels)
        stats.samplers += len(samplers)

        for sampler_index, sampler in enumerate(samplers):
            if not isinstance(sampler, dict):
                errors.append(f"{label} sampler {sampler_index} must be an object")
                continue
            interpolation = sampler.get("interpolation", "LINEAR")
            if interpolation not in {"LINEAR", "STEP"}:
                errors.append(
                    f"{label} sampler {sampler_index} interpolation "
                    "must be LINEAR or STEP"
                )
            for key in ("input", "output"):
                accessor_index = sampler.get(key)
                if not isinstance(accessor_index, int):
                    errors.append(f"{label} sampler {sampler_index} missing {key} accessor")
                    continue
                if accessor_index < 0 or accessor_index >= len(accessors):
                    errors.append(f"{label} sampler {sampler_index} {key} accessor is out of range")
                    continue
                accessor = accessors[accessor_index]
                if isinstance(accessor, dict) and accessor.get("count", 0) <= 0:
                    errors.append(f"{label} sampler {sampler_index} {key} accessor has no values")

        for channel_index, channel in enumerate(channels):
            if not isinstance(channel, dict):
                errors.append(f"{label} channel {channel_index} must be an object")
                continue
            sampler_index = channel.get("sampler")
            if not isinstance(sampler_index, int) or sampler_index < 0 or sampler_index >= len(samplers):
                errors.append(f"{label} channel {channel_index} sampler is out of range")
            target = channel.get("target")
            if not isinstance(target, dict):
                errors.append(f"{label} channel {channel_index} target must be an object")
                continue
            node = target.get("node")
            path = target.get("path")
            if not isinstance(node, int) or node < 0 or node >= len(nodes):
                errors.append(f"{label} channel {channel_index} target node is out of range")
            if path not in {"translation", "rotation", "scale"}:
                errors.append(f"{label} channel {channel_index} target path {path!r} is unsupported")
                continue
            elif isinstance(path, str):
                stats.target_paths[path] += 1
            if not isinstance(sampler_index, int) or sampler_index < 0 or sampler_index >= len(samplers):
                continue
            sampler = samplers[sampler_index]
            if not isinstance(sampler, dict):
                continue
            input_accessor = accessor_at(accessors, sampler.get("input"))
            output_accessor = accessor_at(accessors, sampler.get("output"))
            if input_accessor:
                if input_accessor.get("componentType") != 5126 or input_accessor.get("type") != "SCALAR":
                    errors.append(
                        f"{label} channel {channel_index} input accessor "
                        "must be float SCALAR time values"
                    )
            if output_accessor:
                expected_type = "VEC4" if path == "rotation" else "VEC3"
                if output_accessor.get("componentType") != 5126 or output_accessor.get("type") != expected_type:
                    errors.append(
                        f"{label} channel {channel_index} output accessor "
                        f"must be float {expected_type} values for {path}"
                    )


def validate_archive(path: Path, stats: PdanimStats, errors: list[str]) -> None:
    label = path.as_posix()
    stats.archives += 1
    try:
        with zipfile.ZipFile(path) as archive:
            names = set(archive.namelist())
            if "animation.ini" not in names:
                errors.append(f"{label} is missing animation.ini")
                return
            ini = parse_ini(archive.read("animation.ini").decode("utf-8"))
            require_catalog_id(errors, f"{label} catalog_id", ini.get("catalog_id"))
            category = ini.get("category", "")
            source_member = ini.get("animation_file", "")
            if category == "weapon_animation":
                stats.weapon += 1
                if source_member:
                    if source_member not in names:
                        errors.append(f"{label} weapon animation is missing {source_member}")
                        return
                    if source_member.endswith(".gltf"):
                        stats.gltf += 1
                    elif source_member.endswith(".glb"):
                        stats.glb += 1
                    else:
                        errors.append(f"{label} animation_file must be animation.gltf or animation.glb")
                        return
                    validate_gltf_animation(
                        f"{label}::{source_member}",
                        archive.read(source_member),
                        stats,
                        errors,
                    )
                else:
                    validate_commands(label, archive, ini, stats, errors)
            elif category == "character_animation":
                stats.character += 1
                source_member = source_member or "animation.gltf"
                if source_member not in names:
                    errors.append(f"{label} character animation is missing {source_member}")
                    return
                if source_member.endswith(".gltf"):
                    stats.gltf += 1
                elif source_member.endswith(".glb"):
                    stats.glb += 1
                else:
                    errors.append(f"{label} animation_file must be animation.gltf or animation.glb")
                    return
                validate_gltf_animation(
                    f"{label}::{source_member}",
                    archive.read(source_member),
                    stats,
                    errors,
                )
            else:
                errors.append(f"{label} has unknown category {category!r}")
    except (OSError, zipfile.BadZipFile, KeyError, UnicodeDecodeError) as exc:
        errors.append(f"{label}: {exc}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "paths",
        nargs="+",
        type=Path,
        help=".pdanim archives or directories containing them.",
    )
    parser.add_argument(
        "--require-both-categories",
        action="store_true",
        help="Fail unless both character and weapon animation archives are present.",
    )
    parser.add_argument(
        "--max-errors",
        type=int,
        default=80,
        help="Maximum detailed failures to print before summarizing the rest.",
    )
    args = parser.parse_args(argv)

    stats = PdanimStats()
    errors: list[str] = []
    for path in iter_pdanim_inputs(args.paths):
        validate_archive(path, stats, errors)

    if stats.archives == 0:
        errors.append("no .pdanim archives were found")
    if args.require_both_categories and (stats.character == 0 or stats.weapon == 0):
        errors.append("both character_animation and weapon_animation archives are required")
    summary_errors: list[str] = []
    for generator, count in sorted(stats.stale_generators.items()):
        examples = stats.stale_generator_examples.get(generator, [])
        example_text = ", ".join(examples)
        if count > len(examples):
            example_text += f", ... {count - len(examples)} more"
        summary_errors.append(
            f"{count} archive(s) use stale pdanim generator {generator!r}"
            + (f" (examples: {example_text})" if example_text else "")
        )
    errors = summary_errors + errors

    if errors:
        print("pdanim source contract failed:", file=sys.stderr)
        max_errors = max(args.max_errors, 1)
        for error in errors[:max_errors]:
            print(f"  - {error}", file=sys.stderr)
        if len(errors) > max_errors:
            print(f"  - ... {len(errors) - max_errors} more error(s)", file=sys.stderr)
        return 1

    targets = ",".join(
        f"{name}={stats.target_paths[name]}"
        for name in sorted(stats.target_paths)
    )
    print(
        "pdanim source contract ok: "
        f"archives={stats.archives} "
        f"character={stats.character} "
        f"weapon={stats.weapon} "
        f"gltf={stats.gltf} "
        f"glb={stats.glb} "
        f"commands={stats.commands} "
        f"command_rows={stats.command_rows} "
        f"animations={stats.animations} "
        f"channels={stats.channels} "
        f"samplers={stats.samplers} "
        f"nodes={stats.nodes} "
        f"target_paths={targets}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
