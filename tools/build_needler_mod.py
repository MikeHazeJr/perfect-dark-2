#!/usr/bin/env python3
"""Emit the Needler dev mod: a conformant Halo-Needler-style .pdweapon.

This builder is the needler-specific sibling of
``tools/build_typed_pdxxx_examples.py``. It adapts that file's
``update_weapon`` / ``update_projectile_entity`` / ``update_effect`` /
``update_material`` / ``update_texture`` / ``update_mesh`` emitters
member-for-member so the produced archives match the frozen typed-archive
schema validated by ``tools/asset_archive_conformance.py`` exactly (no TSV,
no loose .bin, catalog-ID references only, named JSON members under the
canonical paths).

Output: ``dev-mods/needler/`` as a loose dev-mod component folder ---

    dev-mods/needler/
        mod.json                  (rewritten: contents tags + asset list)
        needler.pdweapon          (the weapon; self-contained dependency closure)

The weapon archive embeds its entire dependency closure under
``dependencies/assets/...`` so the mod is self-contained:

    needler.pdweapon
        weapon.ini
        behavior/{primary,secondary}.graph.json
        behavior/{settings,variables,shared-context}.json
        bindings/presentation.json
        dependencies/assets/models/weapon.pdmesh          (held Needler body)
        dependencies/assets/projectiles/primary.pdprojectile    (homing needle)
        dependencies/assets/projectiles/secondary.pdprojectile  (burst needle)
        _meta/manifest.json

Each embedded projectile in turn embeds its own dependency closure (needle
mesh, and -- for the burst projectile -- the pink contact-explosion effect
plus the pink spark texture). Every cross-asset reference is a catalog-ID
string in the ``mod_needler:`` namespace and resolves to an archive that
actually declares that catalog_id, so the strict conformance checker's
known-catalog-ID gate is satisfied.

Catalog IDs (namespace ``mod_needler`` == mod id ``needler``):
    mod_needler:needler                      the weapon (custom-slot bound on scan)
    mod_needler:needler_model                the held first-person weapon mesh
    mod_needler:needle                       the projectile needle spike mesh
    mod_needler:needler__projectile_homing   primary tracking needle
    mod_needler:needler__projectile_burst    secondary non-tracking needle
    mod_needler:pink_burst_effect            small pink contact explosion
    mod_needler:pink_spark                   pink impact spark texture

The primary needle homing and secondary impact effect are consumed by the
default-on weapon graph runtime; this archive is the public source for both
the gameplay behavior and the first-person presentation.
"""

from __future__ import annotations

import argparse
import base64
import json
import math
import struct
import sys
import zipfile
import zlib
from collections.abc import Iterable
from io import BytesIO
from pathlib import Path

# ---------------------------------------------------------------------------
# Schema-parity helpers imported from the canonical example builder so the
# Needler archives use the exact same manifest/descriptor shape. Importing is
# side-effect free (that module guards its work behind __main__).
#
# Only the helpers whose output shape we reuse verbatim are imported:
#   ZIP_TIME        -- fixed (1980,1,1) timestamp -> deterministic archives
#   manifest_with   -- canonical pd.asset_archive.manifest.v1 builder
#   effect_manifest -- canonical effect manifest (effect_file + timeline_file)
# ---------------------------------------------------------------------------
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_typed_pdxxx_examples import (  # noqa: E402  (after sys.path tweak)
    ZIP_TIME,
    audio_wav_descriptor,
    audio_wav_manifest,
    manifest_with,
    effect_manifest,
    pcm16_mono_wav,
)


def dumps_graph(graph: dict[str, object]) -> str:
    """Serialize a behavior/effect graph as deterministic, valid JSON.

    Built from dicts (not string concatenation) so the emitted JSON can never
    drift into the broken-brace failure mode that hand-escaped f-strings invite.
    The conformance checker parses these with json.loads and scans the values,
    so formatting/key-order is irrelevant -- only validity and content matter.
    """
    return json.dumps(graph, indent=2) + "\n"


REPO_ROOT = Path(__file__).resolve().parents[1]
MOD_DIR = REPO_ROOT / "dev-mods" / "needler"
DEFAULT_BURST_TINT = (1.0, 0.4, 0.8, 1.0)
DEFAULT_SPARK_TINT = (1.0, 0.5, 0.85, 1.0)

NS = "mod_needler"


def cid(local: str) -> str:
    return f"{NS}:{local}"


# Catalog IDs ---------------------------------------------------------------
WEAPON_ID = cid("needler")
HELD_MESH_ID = cid("needler_model")
NEEDLE_MESH_ID = cid("needle")
HOMING_PROJECTILE_ID = cid("needler__projectile_homing")
BURST_PROJECTILE_ID = cid("needler__projectile_burst")
BURST_EFFECT_ID = cid("pink_burst_effect")
BURST_SFX_ID = cid("pink_burst_sfx")
SPARK_TEXTURE_ID = cid("pink_spark")
# The first-person renderer applies another fixed 0.1 model scale.  Keep the
# editable source comfortably inside the view frustum instead of filling the
# screen with the nearest face of the held mesh.
HELD_WEAPON_SCALE = 5.0
HELD_WEAPON_POS_X = 30.0
HELD_WEAPON_POS_Y = -18.0
HELD_WEAPON_POS_Z = -45.0


# ---------------------------------------------------------------------------
# Archive writer -- mirrors build_typed_pdxxx_examples.write_archive but
# targets dev-mods/needler/ and accepts an absolute-ish member layout.
# ---------------------------------------------------------------------------
def write_archive(path: Path, entries: Iterable[tuple[str, bytes | str]]) -> bytes:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for name, data in entries:
            if isinstance(data, str):
                data = data.encode("utf-8")
            info = zipfile.ZipInfo(name, ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, data)
    return path.read_bytes()


def build_archive_bytes(entries: Iterable[tuple[str, bytes | str]]) -> bytes:
    """Build a typed archive entirely in memory (for nested dependencies)."""
    buffer = BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for name, data in entries:
            if isinstance(data, str):
                data = data.encode("utf-8")
            info = zipfile.ZipInfo(name, ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, data)
    return buffer.getvalue()


# ---------------------------------------------------------------------------
# Minimal real PNG (stdlib zlib). Solid-colour RGBA, w x h. Conformance only
# requires a member named texture.png; we emit a genuine, valid PNG anyway.
# ---------------------------------------------------------------------------
def solid_rgba_png(width: int, height: int, rgba: tuple[int, int, int, int]) -> bytes:
    def chunk(tag: bytes, payload: bytes) -> bytes:
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    r, g, b, a = rgba
    raw = bytearray()
    row = bytes([r, g, b, a]) * width
    for _ in range(height):
        raw.append(0)  # filter type 0 (None) per scanline
        raw.extend(row)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)  # 8-bit RGBA
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


# ---------------------------------------------------------------------------
# Needle spike mesh -- thin elongated 4-sided pyramid along +Z, authored as a
# self-contained glTF with an inline base64 data-URI buffer (the same buffer
# style the example tri_mesh uses). All POSITION values are integers, so they
# quantize cleanly to native s16; UVs lie in [0,1]. Triangle mode, uint16
# indices -- matching validate_pdmesh_gltf_integer_native_boundary's contract.
# ---------------------------------------------------------------------------
def needle_gltf() -> str:
    # 4 base corners (z=0, +/-1 square) + 1 tip (z=24). Thin and elongated.
    positions = [
        (-1.0, -1.0, 0.0),
        (1.0, -1.0, 0.0),
        (1.0, 1.0, 0.0),
        (-1.0, 1.0, 0.0),
        (0.0, 0.0, 24.0),
    ]
    texcoords = [
        (0.0, 0.0),
        (1.0, 0.0),
        (1.0, 1.0),
        (0.0, 1.0),
        (0.5, 0.5),
    ]
    # 4 side faces (base corner -> next corner -> tip).
    indices = [
        0, 1, 4,
        1, 2, 4,
        2, 3, 4,
        3, 0, 4,
    ]

    pos_bytes = b"".join(struct.pack("<3f", *p) for p in positions)
    uv_bytes = b"".join(struct.pack("<2f", *t) for t in texcoords)
    idx_bytes = b"".join(struct.pack("<H", i) for i in indices)

    # Single buffer: [positions | texcoords | indices], 4-byte aligned chunks.
    def pad4(data: bytes) -> bytes:
        rem = (-len(data)) % 4
        return data + b"\x00" * rem

    pos_off = 0
    uv_off = pos_off + len(pos_bytes)
    idx_off = uv_off + len(uv_bytes)
    buffer = pad4(pos_bytes) + pad4(uv_bytes) + pad4(idx_bytes)

    gltf = {
        "asset": {"version": "2.0", "generator": "build_needler_mod.py projectile needle v1"},
        "buffers": [{
            "uri": "data:application/octet-stream;base64," + base64.b64encode(buffer).decode("ascii"),
            "byteLength": len(buffer),
        }],
        "bufferViews": [
            {"buffer": 0, "byteOffset": pos_off, "byteLength": len(pos_bytes), "target": 34962},
            {"buffer": 0, "byteOffset": uv_off, "byteLength": len(uv_bytes), "target": 34962},
            {"buffer": 0, "byteOffset": idx_off, "byteLength": len(idx_bytes), "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3",
             "min": [-1.0, -1.0, 0.0], "max": [1.0, 1.0, 24.0]},
            {"bufferView": 1, "componentType": 5126, "count": len(texcoords), "type": "VEC2"},
            {"bufferView": 2, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
        ],
        "materials": [{
            "name": "NeedleCrystal",
            "pbrMetallicRoughness": {
                "baseColorFactor": [1.0, 0.35, 0.75, 1.0],
                "metallicFactor": 0.1,
                "roughnessFactor": 0.25,
            },
        }],
        "meshes": [{
            "name": "needle",
            "primitives": [{
                "attributes": {"POSITION": 0, "TEXCOORD_0": 1},
                "indices": 2,
                "material": 0,
                "mode": 4,
            }],
        }],
        "nodes": [{"name": "needle", "mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    return json.dumps(gltf, indent=2) + "\n"


# ---------------------------------------------------------------------------
# Leaf dependency archives (built in memory, embedded into parents).
# ---------------------------------------------------------------------------
def build_needle_mesh() -> bytes:
    return build_archive_bytes([
        ("mesh.ini",
         "; needle.pdmesh - thin elongated needle spike (self-contained mesh)\n"
         "[mesh]\n"
         f"catalog_id = {NEEDLE_MESH_ID}\n"
         "model_file = model.gltf\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("model.gltf", needle_gltf()),
        ("_meta/manifest.json", manifest_with("mesh", NEEDLE_MESH_ID, {
            "geometry": "model.gltf",
            "model_file": "model.gltf",
        })),
    ])


# ---------------------------------------------------------------------------
# Held Needler mesh -- a compact low-poly crystalline weapon body with a grip,
# muzzle, side pods, and raised needle crystals. This is intentionally distinct
# from the projectile needle so the first-person render proof cannot be
# satisfied by the old stand-in spike.
# ---------------------------------------------------------------------------
def needler_weapon_gltf() -> str:
    # Match native first-person weapon source scale. The renderer applies a
    # 0.1 viewmodel scale, so tiny proving geometry is real but not inspectable.
    positions: list[tuple[float, float, float]] = []
    texcoords: list[tuple[float, float]] = []
    indices: list[int] = []

    def add_vertex(p: tuple[float, float, float], uv: tuple[float, float]) -> int:
        positions.append((p[0] * HELD_WEAPON_SCALE, p[1] * HELD_WEAPON_SCALE,
                          -p[2] * HELD_WEAPON_SCALE))
        texcoords.append(uv)
        return len(positions) - 1

    def add_box(name: str, x0: float, y0: float, z0: float,
                x1: float, y1: float, z1: float) -> None:
        del name
        faces = [
            ((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)),
            ((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)),
            ((x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0)),
            ((x0, y0, z1), (x0, y0, z0), (x1, y0, z0), (x1, y0, z1)),
            ((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)),
            ((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)),
        ]
        for face in faces:
            base = len(positions)
            add_vertex(face[0], (0.0, 0.0))
            add_vertex(face[1], (1.0, 0.0))
            add_vertex(face[2], (1.0, 1.0))
            add_vertex(face[3], (0.0, 1.0))
            indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])

    def add_pyramid(base: list[tuple[float, float, float]],
                    tip: tuple[float, float, float]) -> None:
        base_indices = [add_vertex(p, uv) for p, uv in zip(base, [
            (0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)
        ])]
        tip_index = add_vertex(tip, (0.5, 0.5))
        indices.extend([base_indices[0], base_indices[1], tip_index])
        indices.extend([base_indices[1], base_indices[2], tip_index])
        indices.extend([base_indices[2], base_indices[3], tip_index])
        indices.extend([base_indices[3], base_indices[0], tip_index])

    # Main first-person silhouette: long body on +Z, lowered grip, forward muzzle.
    add_box("body", -7.0, -3.0, -4.0, 7.0, 3.0, 18.0)
    add_box("muzzle", -4.0, -1.5, 16.0, 4.0, 1.5, 26.0)
    add_box("grip", -2.5, -9.0, -1.0, 2.5, -3.0, 8.0)
    add_box("left_pod", -10.0, -1.5, -1.0, -6.0, 1.5, 14.0)
    add_box("right_pod", 6.0, -1.5, -1.0, 10.0, 1.5, 14.0)

    for z in (5.0, 9.0, 13.0, 17.0):
        add_pyramid(
            [(-2.0, 2.0, z - 1.0), (2.0, 2.0, z - 1.0),
             (2.0, 2.0, z + 1.0), (-2.0, 2.0, z + 1.0)],
            (0.0, 8.0, z),
        )

    # Large asymmetric dorsal crystal bank. The first-person weapon renderer
    # uses the same public GLTF source as gameplay, so keep the visibility
    # witness as actual Needler silhouette instead of a debug overlay.
    add_box("dorsal_crystal_near", -8.0, 6.0, -2.0, 8.0, 18.0, 7.0)
    add_box("dorsal_crystal_far", -3.0, 5.0, 6.0, 13.0, 17.0, 17.0)

    pos_bytes = b"".join(struct.pack("<3f", *p) for p in positions)
    uv_bytes = b"".join(struct.pack("<2f", *t) for t in texcoords)
    idx_bytes = b"".join(struct.pack("<H", i) for i in indices)

    def pad4(data: bytes) -> bytes:
        rem = (-len(data)) % 4
        return data + b"\x00" * rem

    pos_off = 0
    uv_off = pos_off + len(pos_bytes)
    idx_off = uv_off + len(uv_bytes)
    buffer = pad4(pos_bytes) + pad4(uv_bytes) + pad4(idx_bytes)

    mins = [min(p[i] for p in positions) for i in range(3)]
    maxs = [max(p[i] for p in positions) for i in range(3)]
    gltf = {
        "asset": {"version": "2.0", "generator": "build_needler_mod.py held weapon v3"},
        "buffers": [{
            "uri": "data:application/octet-stream;base64," + base64.b64encode(buffer).decode("ascii"),
            "byteLength": len(buffer),
        }],
        "bufferViews": [
            {"buffer": 0, "byteOffset": pos_off, "byteLength": len(pos_bytes), "target": 34962},
            {"buffer": 0, "byteOffset": uv_off, "byteLength": len(uv_bytes), "target": 34962},
            {"buffer": 0, "byteOffset": idx_off, "byteLength": len(idx_bytes), "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3",
             "min": mins, "max": maxs},
            {"bufferView": 1, "componentType": 5126, "count": len(texcoords), "type": "VEC2"},
            {"bufferView": 2, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
        ],
        "materials": [{
            "name": "NeedlerBody",
            "doubleSided": True,
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.92, 0.20, 0.62, 1.0],
                "metallicFactor": 0.15,
                "roughnessFactor": 0.35,
            },
        }],
        "meshes": [{
            "name": "needler_weapon",
            "primitives": [{
                "attributes": {"POSITION": 0, "TEXCOORD_0": 1},
                "indices": 2,
                "material": 0,
                "mode": 4,
            }],
        }],
        "nodes": [{"name": "needler_weapon", "mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    return json.dumps(gltf, indent=2) + "\n"


def build_held_weapon_mesh() -> bytes:
    tri_count = 100
    model_mtl = (
        "# held Needler material sidecar\n"
        "newmtl NeedlerBody\n"
        "Kd 0.92 0.20 0.62\n"
        "d 1.0\n"
    )
    bounds = {
        "xmin": -10.0 * HELD_WEAPON_SCALE, "xmax": 13.0 * HELD_WEAPON_SCALE,
        "ymin": -9.0 * HELD_WEAPON_SCALE, "ymax": 18.0 * HELD_WEAPON_SCALE,
        "zmin": -26.0 * HELD_WEAPON_SCALE, "zmax": 4.0 * HELD_WEAPON_SCALE,
    }
    model_nodes = {
        "schema": "pd2.mesh.nodes.v1",
        "nodes": [
            {
                "id": 0,
                "parent": -1,
                "type": 2,
                "partnum": 0,
                "part": 0,
                "mtx0": 0,
                "mtx1": -1,
                "mtx2": -1,
                "position": {"x": 0.0, "y": 0.0, "z": 0.0},
                "drawdist": 0.0,
                "target": -1,
                "group": "-",
                "render_mtx": 0,
                "mcount": 1,
                "hitpart": 0,
                "bounds": bounds,
                "distance": {"near": 0.0, "far": 0.0},
                "reorder": {
                    "pivot": {"x": 0.0, "y": 0.0, "z": 0.0},
                    "axis": {"x": 0.0, "y": 1.0, "z": 0.0},
                    "target_a": -1,
                    "target_b": -1,
                    "side": 0,
                },
            },
            {
                "id": 1,
                "parent": 0,
                "type": 24,
                "partnum": -1,
                "part": -1,
                "mtx0": 0,
                "mtx1": -1,
                "mtx2": -1,
                "position": {"x": 0.0, "y": 0.0, "z": 0.0},
                "drawdist": 0.0,
                "target": -1,
                "group": "-",
                "render_mtx": 0,
                "mcount": 1,
                "hitpart": 0,
                "bounds": bounds,
                "distance": {"near": 0.0, "far": 0.0},
                "reorder": {
                    "pivot": {"x": 0.0, "y": 0.0, "z": 0.0},
                    "axis": {"x": 0.0, "y": 1.0, "z": 0.0},
                    "target_a": -1,
                    "target_b": -1,
                    "side": 0,
                },
            },
        ],
    }
    model_parts = {
        "schema": "pd2.mesh.parts.v1",
        "parts": [{"partnum": 0, "node": 0}],
    }
    model_faces = {
        "schema": "pd2.mesh.faces.v1",
        "faces": [
            {"face_index": i, "matrix_index": 0}
            for i in range(tri_count)
        ],
    }
    model_render = {
        "schema": "pd2.mesh.render.v1",
        "pd_kind": "mesh_render_commands",
        "commands": [
            {"group": "-", "command": "tri",
             "face_index": i, "matrix_index": 0}
            for i in range(tri_count)
        ],
    }
    return build_archive_bytes([
        ("mesh.ini",
         "; weapon.pdmesh - held Needler first-person body (self-contained mesh)\n"
         "[mesh]\n"
         f"catalog_id = {HELD_MESH_ID}\n"
         "model_file = model.gltf\n"
         "material_file = model.mtl\n"
         "hierarchy_file = model.nodes.json\n"
         "parts_file = model.parts.json\n"
         "faces_file = model.faces.json\n"
         "render_stream_file = model.render.json\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("model.gltf", needler_weapon_gltf()),
        ("model.mtl", model_mtl),
        ("model.nodes.json", json.dumps(model_nodes, indent=2) + "\n"),
        ("model.parts.json", json.dumps(model_parts, indent=2) + "\n"),
        ("model.faces.json", json.dumps(model_faces, indent=2) + "\n"),
        ("model.render.json", json.dumps(model_render, indent=2) + "\n"),
        ("_meta/manifest.json", manifest_with("mesh", HELD_MESH_ID, {
            "geometry": "model.gltf",
            "model_file": "model.gltf",
            "material_file": "model.mtl",
            "hierarchy_file": "model.nodes.json",
            "parts_file": "model.parts.json",
            "faces_file": "model.faces.json",
            "render_stream_file": "model.render.json",
        })),
    ])


def build_spark_texture() -> bytes:
    png = solid_rgba_png(2, 2, (255, 128, 210, 255))  # bright pink spark
    return build_archive_bytes([
        ("texture.ini",
         "; pink_spark.pdtexture - pink impact spark texture (self-contained)\n"
         "[texture]\n"
         f"catalog_id = {SPARK_TEXTURE_ID}\n"
         "name = Pink Spark\n"
         "\n[source]\n"
         "texture_file = texture.png\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("texture.png", png),
        ("_meta/manifest.json", manifest_with("texture", SPARK_TEXTURE_ID, {
            "texture_file": "texture.png",
        })),
    ])


def build_pink_burst_sfx() -> bytes:
    sample = pcm16_mono_wav(frame_count=220)
    return build_archive_bytes([
        ("sound.ini", audio_wav_descriptor(
            "sfx", BURST_SFX_ID, "Pink Needle Burst", "sfx", 5,
            len(sample), 220,
        )),
        ("sample.wav", sample),
        ("_meta/manifest.json", audio_wav_manifest(
            "sfx", BURST_SFX_ID, len(sample), 220,
        )),
    ])


def build_pink_burst_effect(
    burst_sfx: bytes,
    burst_tint: tuple[float, float, float, float] = DEFAULT_BURST_TINT,
    spark_tint: tuple[float, float, float, float] = DEFAULT_SPARK_TINT,
) -> bytes:
    # effect.graph.json -- small pink contact explosion. The pink tint lives in
    # the effect/texture data because the OG explosion path is hard-white with
    # no scalar tint; this custom effect carries the colour.
    # effect.explosion / effect.spark are live node kinds compiled by the
    # public effect runtime. explosion_class belongs to the explosion node,
    # not the effect.ini descriptor. Wave 11 projects the authored tint and
    # shader through the shared gameplay/presentation transaction.
    effect_graph = dumps_graph({
        "schema": "pd.effect_graph.v1",
        "asset_id": BURST_EFFECT_ID,
        "nodes": [
            {"id": "burst", "kind": "effect.explosion",
             "params": {"explosion_class": "small",
                        "audio_catalog_id": BURST_SFX_ID,
                        "tint": list(burst_tint)}},
            {"id": "spark", "kind": "effect.spark",
             "params": {"tint": list(spark_tint)}},
        ],
        "edges": [{"from": "burst", "to": "spark"}],
    })
    timeline = dumps_graph({
        "schema": "pd2.effect.timeline.v1",
        "tracks": [
            {"time": 0.0, "property": "intensity", "value": 1.0},
            {"time": 1.5, "property": "intensity", "value": 0.0},
        ],
    })
    return build_archive_bytes([
        ("effect.ini",
         "[effect]\n"
         f"catalog_id = {BURST_EFFECT_ID}\n"
         "name = Pink Needle Burst\n"
         "effect_key = explosion\n"
         "target_key = world\n"
         "effect_file = effect.graph.json\n"
         "timeline_file = timeline.json\n"
         "shader_id = needler_pink_burst\n"
         "intensity = 1.0\n"),
        ("effect.graph.json", effect_graph),
        ("timeline.json", timeline),
        ("dependencies/assets/audio/pink_burst.pdsfx", burst_sfx),
        ("_meta/manifest.json", effect_manifest(BURST_EFFECT_ID)),
    ])


# ---------------------------------------------------------------------------
# Projectile archives.
# ---------------------------------------------------------------------------
def build_homing_projectile(needle_mesh: bytes) -> bytes:
    # spawn_state (model) -> motion (powered) -> homing (tracking).
    # Node kinds pinned to weapon_graph_runtime.c s_modules (lines 86-98).
    # Param keys pinned to projectileRuntimeFromNode():
    #   spawn_state: model_ref, source_mode, source_function_type, scale, damage
    #   motion:      motion_kind, speed, powered, timer60
    #   homing:      target_source, target_filter, lost_target_behavior,
    #                retarget_policy, steering_gain, steering_damping
    graph = dumps_graph({
        "schema": "pd.projectile_graph.v1",
        "asset_id": HOMING_PROJECTILE_ID,
        "graph_id": "projectile",
        "nodes": [
            {"id": "spawn", "kind": "projectile.spawn_state", "params": {
                "model_ref": NEEDLE_MESH_ID,
                "source_mode": "primary",
                "source_function_type": "shoot_projectile",
                "scale": 1.0, "damage": 6.0}},
            {"id": "motion", "kind": "projectile.motion", "params": {
                "motion_kind": "powered", "speed": 22.0,
                "powered": True, "timer60": 240}},
            {"id": "homing", "kind": "projectile.homing", "params": {
                "target_source": "owner_aim_target",
                "target_filter": "enemy",
                "lost_target_behavior": "fly_straight",
                "retarget_policy": "keep_until_lost",
                "steering_gain": 0.18, "steering_damping": 0.85}},
        ],
        "edges": [
            {"from": "spawn", "to": "motion"},
            {"from": "motion", "to": "homing"},
        ],
        "exports": [{"name": "main", "node": "spawn"}],
    })
    return build_archive_bytes([
        ("projectile.ini",
         "; needler__projectile_homing.pdprojectile - primary tracking needle\n"
         "[projectile]\n"
         f"catalog_id = {HOMING_PROJECTILE_ID}\n"
         "name = Needler Homing Needle\n"
         "speed = 22.0\n"
         "behavior_graph = behavior.graph.json\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("behavior.graph.json", graph),
        ("dependencies/assets/models/needle.pdmesh", needle_mesh),
        ("_meta/manifest.json", manifest_with("projectile", HOMING_PROJECTILE_ID, {
            "behavior_graph": "behavior.graph.json",
        })),
    ])


def build_burst_projectile(needle_mesh: bytes, burst_effect: bytes,
                           spark_texture: bytes) -> bytes:
    # spawn_state (model) -> motion (powered, NO homing) -> impact (explode).
    # impact param keys are the JSON keys the runtime parser reads
    # (weapon_graph_runtime.c WEAPON_GRAPH_OP_PROJECTILE_IMPACT, ~2228-2239):
    #   impact_filter, explosion_ref, spark_ref, consume_on_hit, stick_on_hit.
    # NB: explosion_ref / spark_ref are the JSON keys -- the C struct fields are
    # named impact_explosion_ref / impact_spark_ref, but heldParamString() pulls
    # them from the unprefixed JSON keys, so the authored data must use those.
    graph = dumps_graph({
        "schema": "pd.projectile_graph.v1",
        "asset_id": BURST_PROJECTILE_ID,
        "graph_id": "projectile",
        "nodes": [
            {"id": "spawn", "kind": "projectile.spawn_state", "params": {
                "model_ref": NEEDLE_MESH_ID,
                "source_mode": "secondary",
                "source_function_type": "shoot_projectile",
                "scale": 1.0, "damage": 5.0}},
            {"id": "motion", "kind": "projectile.motion", "params": {
                "motion_kind": "powered", "speed": 26.0,
                "powered": True, "timer60": 180}},
            {"id": "impact", "kind": "projectile.impact", "params": {
                "impact_filter": "any",
                "explosion_ref": BURST_EFFECT_ID,
                "spark_ref": BURST_EFFECT_ID,
                "consume_on_hit": True,
                "stick_on_hit": False}},
        ],
        "edges": [
            {"from": "spawn", "to": "motion"},
            {"from": "motion", "to": "impact"},
        ],
        "exports": [{"name": "main", "node": "spawn"}],
    })
    return build_archive_bytes([
        ("projectile.ini",
         "; needler__projectile_burst.pdprojectile - secondary non-tracking needle\n"
         "[projectile]\n"
         f"catalog_id = {BURST_PROJECTILE_ID}\n"
         "name = Needler Burst Needle\n"
         "speed = 26.0\n"
         "behavior_graph = behavior.graph.json\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("behavior.graph.json", graph),
        ("dependencies/assets/models/needle.pdmesh", needle_mesh),
        ("dependencies/assets/effects/pink_burst.pdeffect", burst_effect),
        ("dependencies/assets/textures/spark.pdtexture", spark_texture),
        ("_meta/manifest.json", manifest_with("projectile", BURST_PROJECTILE_ID, {
            "behavior_graph": "behavior.graph.json",
        })),
    ])


# ---------------------------------------------------------------------------
# The weapon -- adapts update_weapon member-for-member.
# ---------------------------------------------------------------------------
def build_weapon(held_weapon_mesh: bytes, homing_projectile: bytes,
                 burst_projectile: bytes, output_dir: Path = MOD_DIR) -> bytes:
    # event.trigger_pressed + spawn.fired_projectile are live weapon node kinds
    # (weapon_graph_runtime.c s_modules lines 63, 76). The spawn node reads
    # "mode", "function_type" (-> INVENTORYFUNCTYPE_SHOOT_PROJECTILE for
    # "shoot_projectile") and "projectile_ref" (heldFunctionFromNode ~2495-2593).
    def weapon_graph(graph_id: str, trigger_id: str, mode: str,
                     projectile_ref: str) -> str:
        spawn_id = f"{graph_id}_spawn_projectile"
        return dumps_graph({
            "schema": "pd.weapon_graph.v1",
            "asset_id": WEAPON_ID,
            "graph_id": graph_id,
            "nodes": [
                {"id": trigger_id, "kind": "event.trigger_pressed",
                 "params": {"mode": mode}},
                {"id": spawn_id, "kind": "spawn.fired_projectile",
                 "params": {"mode": mode, "function_type": "shoot_projectile",
                            "projectile_ref": projectile_ref}},
            ],
            "edges": [{"from": trigger_id, "to": spawn_id}],
            "exports": [{"name": graph_id, "node": spawn_id}],
        })

    primary_graph = weapon_graph(
        "primary", "trigger_primary", "primary", HOMING_PROJECTILE_ID)
    secondary_graph = weapon_graph(
        "secondary", "trigger_secondary", "secondary", BURST_PROJECTILE_ID)
    return write_archive(output_dir / "needler.pdweapon", [
        ("weapon.ini",
         "; needler.pdweapon - Halo-Needler-style weapon with typed dependency closure\n"
         "[weapon]\n"
         f"catalog_id = {WEAPON_ID}\n"
         "name = Needler\n"
         "dual_wieldable = false\n"
         "model_file = dependencies/assets/models/weapon.pdmesh\n"
         "muzzlez = 3.0\n"
         f"posx = {HELD_WEAPON_POS_X:.1f}\n"
         f"posy = {HELD_WEAPON_POS_Y:.1f}\n"
         f"posz = {HELD_WEAPON_POS_Z:.1f}\n"
         "track_type = rocket_launcher\n"
         "primary_graph = behavior/primary.graph.json\n"
         "secondary_graph = behavior/secondary.graph.json\n"
         "settings_file = behavior/settings.json\n"
         "variables_file = behavior/variables.json\n"
         "shared_context_file = behavior/shared-context.json\n"
         "presentation_file = bindings/presentation.json\n"
         "primary_projectile_archive = dependencies/assets/projectiles/primary.pdprojectile\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("behavior/primary.graph.json", primary_graph),
        ("behavior/secondary.graph.json", secondary_graph),
        # c3849 Wave 5f (B6.4): canonical schema spellings converge on the
        # base emitter strings (pd.weapon_settings.v1 / pd.weapon_variables.v1);
        # the old dotted spellings stay accepted at parse with a LOG_WARNING.
        ("behavior/settings.json", dumps_graph({
            "schema": "pd.weapon_settings.v1",
            "fire_cadence": {"value": 8, "unit": "centiseconds"},
        })),
        ("behavior/variables.json", dumps_graph({
            "schema": "pd.weapon_variables.v1",
            "ammo_clip": 20,
            "ammo_reserve": 100,
        })),
        ("behavior/shared-context.json", dumps_graph({
            "schema": "pd.weapon.shared_context.v1",
            "contexts": ["owner_player", "owner_team",
                         "weapon_instance", "damage_credit_player"],
        })),
        ("bindings/presentation.json", dumps_graph({
            "schema": "pd.weapon.presentation.v1",
            "crosshair": "default",
        })),
        ("dependencies/assets/models/weapon.pdmesh", held_weapon_mesh),
        ("dependencies/assets/projectiles/primary.pdprojectile", homing_projectile),
        ("dependencies/assets/projectiles/secondary.pdprojectile", burst_projectile),
        ("_meta/manifest.json", manifest_with("weapon", WEAPON_ID, {
            "model_file": "dependencies/assets/models/weapon.pdmesh",
            "primary_graph": "behavior/primary.graph.json",
            "secondary_graph": "behavior/secondary.graph.json",
            "settings_file": "behavior/settings.json",
            "variables_file": "behavior/variables.json",
            "shared_context_file": "behavior/shared-context.json",
            "presentation_file": "bindings/presentation.json",
            "primary_projectile_archive": "dependencies/assets/projectiles/primary.pdprojectile",
        })),
    ])


# ---------------------------------------------------------------------------
# mod.json -- rewrite the reserved manifest with the real contents/asset list.
# ---------------------------------------------------------------------------
def write_mod_json(output_dir: Path = MOD_DIR) -> None:
    mod = {
        "id": "needler",
        "name": "Needler",
        "version": "0.4.4",
        "author": "PD2",
        "description": (
            "Halo-Needler-style custom weapon. Primary fire launches graph-driven "
            "tracking needles; secondary fire launches graph-driven non-tracking "
            "needles that explode on contact with small pink explosions. One "
            "self-contained needler.pdweapon embeds the held weapon mesh, projectile "
            "mesh, material, textures, projectiles, and pink contact-explosion effect."
        ),
        "contents": ["weapon"],
        "requires_restart": False,
        "assets": [
            {"catalog_id": WEAPON_ID, "kind": "weapon", "archive": "needler.pdweapon"},
        ],
    }
    (output_dir / "mod.json").write_text(json.dumps(mod, indent=2) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# Driver.
# ---------------------------------------------------------------------------
def build(
    output_dir: Path = MOD_DIR,
    burst_tint: tuple[float, float, float, float] = DEFAULT_BURST_TINT,
    spark_tint: tuple[float, float, float, float] = DEFAULT_SPARK_TINT,
) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)

    # Leaf dependencies first.
    held_weapon_mesh = build_held_weapon_mesh()
    needle_mesh = build_needle_mesh()
    spark_texture = build_spark_texture()
    burst_sfx = build_pink_burst_sfx()
    burst_effect = build_pink_burst_effect(burst_sfx, burst_tint, spark_tint)

    # Projectiles embed their own closures.
    homing_projectile = build_homing_projectile(needle_mesh)
    burst_projectile = build_burst_projectile(needle_mesh, burst_effect, spark_texture)

    # The weapon embeds everything.
    build_weapon(
        held_weapon_mesh, homing_projectile, burst_projectile, output_dir,
    )

    write_mod_json(output_dir)
    return output_dir / "needler.pdweapon"


def parse_tint(value: str) -> tuple[float, float, float, float]:
    """Parse a public effect RGBA value for deterministic validation fixtures."""
    try:
        values = tuple(float(part.strip()) for part in value.split(","))
    except ValueError as exc:
        raise argparse.ArgumentTypeError("tint must contain four numbers") from exc
    if len(values) != 4 or any(not math.isfinite(part) or part < 0.0 or part > 1.0
                               for part in values):
        raise argparse.ArgumentTypeError(
            "tint must contain exactly four finite numbers in the range 0..1"
        )
    return values  # type: ignore[return-value]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=MOD_DIR,
                        help="output mod directory (default: dev-mods/needler)")
    parser.add_argument("--burst-tint", type=parse_tint, default=DEFAULT_BURST_TINT,
                        help="editable explosion RGBA as r,g,b,a")
    parser.add_argument("--spark-tint", type=parse_tint, default=DEFAULT_SPARK_TINT,
                        help="editable spark RGBA as r,g,b,a")
    args = parser.parse_args()
    output_dir = args.output_dir.resolve()
    weapon_path = build(output_dir, args.burst_tint, args.spark_tint)
    try:
        weapon_display = weapon_path.relative_to(REPO_ROOT)
        manifest_display = (output_dir / "mod.json").relative_to(REPO_ROOT)
    except ValueError:
        weapon_display = weapon_path
        manifest_display = output_dir / "mod.json"
    print(f"wrote {weapon_display}")
    print(f"wrote {manifest_display}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
