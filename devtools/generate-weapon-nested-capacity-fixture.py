#!/usr/bin/env python3
"""Generate deterministic T-ASSETS-037 production-scanner fixtures.

The output contains only public editable source (INI, JSON, WAV) plus the
standard typed-archive metadata envelope. The game client consumes it through
assetCatalogRegisterWeaponNestedDependencies via the debug runtime harness.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import wave
import zipfile
from io import BytesIO
from pathlib import Path


COUNT = 70
NAMESPACE = "capacity"


def compact(value: object) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def silence_wav() -> bytes:
    output = BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(22050)
        wav.writeframes(struct.pack("<h", 0) * 32)
    return output.getvalue()


def typed_archive(family: str, catalog_id: str, descriptor_name: str,
                  descriptor: bytes, public: dict[str, tuple[bytes, str]],
                  manifest: dict[str, object]) -> bytes:
    entries: dict[str, tuple[bytes, str]] = {
        descriptor_name: (descriptor, "descriptor"),
        **public,
    }
    inventory = [
        {"path": path, "role": role, "size": len(data), "sha256": sha256(data)}
        for path, (data, role) in entries.items()
    ]
    validation = {
        "schema": "pd2.asset.validation.v1",
        "status": "writer_checked",
        "release_contract": True,
        "descriptor": descriptor_name,
        "manifest": "_meta/manifest.json",
        "public_entry_count": len(entries),
        "dependency_archive_count": 0,
        "dependency_schema_fields": [
            "role", "type", "id", "archive", "required", "version",
            "sha256", "fallback.id", "fallback.reason",
        ],
    }
    meta = {
        "_meta/manifest.json": compact(manifest),
        "_meta/inventory.json": compact({
            "schema": "pd2.asset.inventory.v1",
            "id": catalog_id,
            "family": family,
            "dependency_archive_root": "dependencies/assets",
            "entries": inventory,
        }),
        "_meta/hashes.json": compact({
            "schema": "pd2.asset.hashes.v1", "entries": inventory,
        }),
        "_meta/provenance.json": compact({
            "schema": "pd2.asset.provenance.v1",
            "tool": "generate-weapon-nested-capacity-fixture",
            "family": family,
            "id": catalog_id,
            "source_path": "generated/test-source",
            "source_index": -1,
            "source_symbol": catalog_id,
        }),
        "_meta/validation.json": compact(validation),
        "_meta/source-handles.json": compact({
            "schema": "pd2.asset.source-handles.v1", "handles": [],
        }),
    }
    output = BytesIO()
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path, (data, _role) in entries.items():
            archive.writestr(path, data)
            archive.writestr(f"_meta/{path}.sha256", (sha256(data) + "\n").encode())
        for path, data in meta.items():
            archive.writestr(path, data)
    return output.getvalue()


def sfx_archive(index: int) -> bytes:
    catalog_id = f"{NAMESPACE}:sfx_{index:03d}"
    sample = silence_wav()
    playback = {
        "decoded_sample_count": 32,
        "loop_start_samples": 0,
        "loop_end_samples": 32,
        "loop_count": 0,
        "has_loop": False,
        "sample_pan": 64,
        "sample_volume": 127,
        "key_min": 0,
        "key_max": 127,
        "key_base": 60,
        "key_detune": 0,
        "velocity_min": 0,
        "velocity_max": 127,
        "has_envelope": False,
        "attack_time_us": 0,
        "decay_time_us": 0,
        "release_time_us": 0,
        "attack_volume": 127,
        "decay_volume": 127,
    }
    descriptor = (
        "[sfx]\n"
        f"catalog_id = {catalog_id}\n"
        "audio_category = sfx\n"
        "format = WAV_PCM16\n"
        "sample_rate_hz = 22050\n"
        "file_path = sample.wav\n"
        f"data_size = {len(sample)}\n"
        + "".join(
            f"{key} = {str(value).lower() if isinstance(value, bool) else value}\n"
            for key, value in playback.items()
        )
    ).encode()
    return typed_archive("sfx", catalog_id, "sound.ini", descriptor,
                         {"sample.wav": (sample, "sample")}, {
                             "pd_kind": "sfx", "pd_schema_version": 1,
                             "id": catalog_id, "format": "WAV_PCM16",
                             "sample_rate_hz": 22050, "data": "sample.wav",
                             "data_size": len(sample),
                             **playback,
                         })


def effect_archive(suffix: str, start: int, count: int) -> bytes:
    catalog_id = f"{NAMESPACE}:effect_wide_{suffix}"
    nodes = []
    for index in range(start, start + count):
        nodes.append({
            "id": f"audio_{index:03d}",
            "kind": "effect.explosion",
            "params": {"audio_catalog_id": f"{NAMESPACE}:sfx_{index:03d}"},
        })
    graph = compact({
        "schema": "pd.effect_graph.v1",
        "catalog_id": catalog_id,
        "effect": "wide_dependency_proof",
        "target": "scene",
        "nodes": nodes,
        "edges": [],
    })
    descriptor = (
        "[effect]\n"
        f"catalog_id = {catalog_id}\n"
        "name = Wide dependency proof\n"
        "effect_key = wide_dependency_proof\n"
        "target_key = scene\n"
        "effect_file = effect.graph.json\n"
    ).encode()
    return typed_archive("effect", catalog_id, "effect.ini", descriptor,
                         {"effect.graph.json": (graph, "effect-graph")}, {
                             "pd_kind": "effect", "pd_schema_version": 1,
                             "id": catalog_id,
                             "effect_file": "effect.graph.json",
                         })


def outer_archive(path: Path, corrupt: bool) -> None:
    catalog_id = f"{NAMESPACE}:weapon_{'reject' if corrupt else 'wide'}"
    descriptor = (
        "[weapon]\n"
        f"catalog_id = {catalog_id}\n"
        "name = Nested capacity fixture\n"
        "primary_graph = behavior/primary.graph.json\n"
        "secondary_graph = behavior/secondary.graph.json\n"
        "settings_file = behavior/settings.json\n"
        "variables_file = behavior/variables.json\n"
        "shared_context_file = behavior/shared-context.json\n"
    ).encode()
    graph = lambda graph_id: compact({
        "schema": "pd.weapon_graph.v1", "asset_id": catalog_id,
        "graph_id": graph_id,
        "nodes": [{"id": f"{graph_id}_trigger",
                   "kind": "event.trigger_pressed",
                   "params": {"mode": graph_id}}],
        "edges": [],
        "exports": [{"name": graph_id, "node": f"{graph_id}_trigger"}],
    })
    public: dict[str, tuple[bytes, str]] = {
        "behavior/primary.graph.json": (graph("primary"), "behavior"),
        "behavior/secondary.graph.json": (graph("secondary"), "behavior"),
        "behavior/settings.json": (compact({
            "schema": "pd.weapon_settings.v1", "asset_id": catalog_id,
        }), "settings"),
        "behavior/variables.json": (compact({
            "schema": "pd.weapon_variables.v1", "asset_id": catalog_id,
            "variables": [],
        }), "variables"),
        "behavior/shared-context.json": (compact({
            "schema": "pd.weapon_shared_context.v1", "asset_id": catalog_id,
            "contexts": [],
        }), "behavior"),
    }
    for index in range(COUNT):
        public[f"dependencies/assets/audio/sfx_{index:03d}.pdsfx"] = (
            sfx_archive(index), "dependency-archive")
    public["dependencies/assets/effects/wide_a.pdeffect"] = (
        effect_archive("a", 0, COUNT // 2), "dependency-archive")
    if not corrupt:
        public["dependencies/assets/effects/wide_b.pdeffect"] = (
            effect_archive("b", COUNT // 2, COUNT - COUNT // 2),
            "dependency-archive")
    if corrupt:
        public["dependencies/assets/effects/zz_corrupt.pdeffect"] = (
            b"not a zip archive", "dependency-archive")
    archive_bytes = typed_archive("weapon", catalog_id, "weapon.ini",
                                  descriptor, public, {
                                      "pd_kind": "weapon",
                                      "pd_schema_version": 1,
                                      "id": catalog_id,
                                      "primary_graph": "behavior/primary.graph.json",
                                      "secondary_graph": "behavior/secondary.graph.json",
                                      "settings_file": "behavior/settings.json",
                                      "variables_file": "behavior/variables.json",
                                      "shared_context_file": "behavior/shared-context.json",
                                  })
    path.write_bytes(archive_bytes)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    accept = (args.output_dir / "weapon-nested-capacity.pdweapon").resolve()
    reject = (args.output_dir / "weapon-nested-capacity-reject.pdweapon").resolve()
    plan = (args.output_dir / "weapon-nested-capacity.plan").resolve()
    outer_archive(accept, False)
    outer_archive(reject, True)
    plan.write_text(
        f"capacity|{NAMESPACE}:weapon_wide|{accept}|{COUNT + 2}|2|{COUNT}\n"
        f"capacity_reject|{NAMESPACE}:weapon_reject|{reject}|"
        f"{NAMESPACE}:sfx_|{COUNT}|{NAMESPACE}:effect_wide_a\n",
        encoding="utf-8",
    )
    smoke = {
        "scenario_name": "weapon_nested_capacity",
        "description": "Production scanner accepts and fully observes a 72-archive nested weapon closure with 70 effect-owned dependencies across two valid effects, then rejects a corrupt late effect without leaked rows or edges.",
        "tags": ["modding", "pdxxx", "weapon", "nested", "capacity", "t-assets-037"],
        "log_channel_mask": "all",
        "verbose": 0,
        "timeout_seconds": 180,
        "install_state": "clean",
        "boot_args": [
            "--no-update-check", "--no-sound", "--no-net", "--portable",
            "--debug-weapon-nested-harness", str(plan),
        ],
        "input_sequence": [
            {"at_ms": 0, "type": "wait", "comment": "harness exits after catalog boot"},
            {"at_ms": 170000, "type": "exit", "comment": "timeout safety"},
        ],
        "assertions": {
            "required_lines": [
                "SMOKE: scenario=weapon_nested_capacity",
                "PDWEAPON.NESTED.HARNESS: passed=2 cases=2 result=PASS",
                "SMOKE: result=weapon_nested_harness_pass",
            ],
            "forbidden_patterns": [
                "PDWEAPON.NESTED.HARNESS.FAIL",
                "EXCEPTION_ACCESS_VIOLATION",
                "SMOKE: result=timeout",
            ],
            "required_counts": [
                {"pattern": "PDWEAPON.NESTED.REGISTER: owner=capacity:weapon_wide", "min": COUNT + 2, "max": COUNT + 2},
            ],
        },
    }
    (args.output_dir / "weapon_nested_capacity.json").write_bytes(compact(smoke))
    print(plan)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
