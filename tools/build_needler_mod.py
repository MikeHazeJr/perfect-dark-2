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
        bindings/{material-slots,grip-sockets,presentation}.json
        dependencies/assets/models/weapon.pdmesh          (needle spike)
        dependencies/assets/materials/default.pdmaterial  (pink crystalline)
        dependencies/assets/textures/body.pdtexture       (pink crystal body)
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
    mod_needler:needle                       the needle spike mesh
    mod_needler:needle_crystal               the pink crystalline material
    mod_needler:needle_body                  the pink crystal body texture
    mod_needler:needler__projectile_homing   primary tracking needle
    mod_needler:needler__projectile_burst    secondary non-tracking needle
    mod_needler:pink_burst_effect            small pink contact explosion
    mod_needler:pink_spark                   pink impact spark texture

The active homing of the primary needle depends on a separate runtime
homing-gate adapter that is OUT OF SCOPE for this data-only mod; the
``projectile.homing`` node here is the authored data half (target source,
filter, lost-target behaviour, steering gains) that the adapter consumes.
"""

from __future__ import annotations

import base64
import json
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
# material_manifest() is intentionally NOT imported: it hardcodes the example's
# tri_texture/tri_effect dependency paths, so the needler material manifest is
# built with manifest_with() declaring THIS archive's own member paths instead.
# ---------------------------------------------------------------------------
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_typed_pdxxx_examples import (  # noqa: E402  (after sys.path tweak)
    ZIP_TIME,
    manifest_with,
    effect_manifest,
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

NS = "mod_needler"


def cid(local: str) -> str:
    return f"{NS}:{local}"


# Catalog IDs ---------------------------------------------------------------
WEAPON_ID = cid("needler")
MESH_ID = cid("needle")
MATERIAL_ID = cid("needle_crystal")
BODY_TEXTURE_ID = cid("needle_body")
HOMING_PROJECTILE_ID = cid("needler__projectile_homing")
BURST_PROJECTILE_ID = cid("needler__projectile_burst")
BURST_EFFECT_ID = cid("pink_burst_effect")
SPARK_TEXTURE_ID = cid("pink_spark")


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
        "asset": {"version": "2.0", "generator": "build_needler_mod.py needle spike v1"},
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
         f"catalog_id = {MESH_ID}\n"
         "model_file = model.gltf\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("model.gltf", needle_gltf()),
        ("_meta/manifest.json", manifest_with("mesh", MESH_ID, {
            "geometry": "model.gltf",
            "model_file": "model.gltf",
        })),
    ])


def build_body_texture() -> bytes:
    png = solid_rgba_png(2, 2, (236, 64, 168, 255))  # pink crystal body
    return build_archive_bytes([
        ("texture.ini",
         "; needle_body.pdtexture - pink crystal body texture (self-contained)\n"
         "[texture]\n"
         f"catalog_id = {BODY_TEXTURE_ID}\n"
         "name = Needle Crystal Body\n"
         "\n[source]\n"
         "texture_file = texture.png\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("texture.png", png),
        ("_meta/manifest.json", manifest_with("texture", BODY_TEXTURE_ID, {
            "texture_file": "texture.png",
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


def build_crystal_material(body_texture: bytes) -> bytes:
    material_json = json.dumps({
        "schema": "pd2.material.v1",
        "catalog_id": MATERIAL_ID,
        "name": "Needle Crystal",
        "shading_model": "pd2_unlit",
        "base_color": [1.0, 0.35, 0.75, 1.0],
        "texture_slots": [
            # "archive" is an intra-archive member path, not a catalog-ref key
            # (does not end in _ref/_catalog_id), so the conformance ref scanner
            # leaves it alone -- matching the example tri_material's slot shape.
            {"name": "base_color",
             "archive": "dependencies/assets/texture/body.pdtexture"},
        ],
    }, indent=2) + "\n"
    return build_archive_bytes([
        ("material.ini",
         "; needle_crystal.pdmaterial - pink crystalline needle material\n"
         "[material]\n"
         f"catalog_id = {MATERIAL_ID}\n"
         "name = Needle Crystal\n"
         "shader = pd2_unlit\n"
         "material_file = material.json\n"
         "texture_archive = dependencies/assets/texture/body.pdtexture\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("material.json", material_json),
        ("dependencies/assets/texture/body.pdtexture", body_texture),
        # material_manifest() hardcodes the example texture path; declare the
        # texture_archive/material_file fields explicitly instead so the
        # manifest matches THIS archive's member layout.
        ("_meta/manifest.json", manifest_with("material", MATERIAL_ID, {
            "material_file": "material.json",
            "texture_archive": "dependencies/assets/texture/body.pdtexture",
        })),
    ])


def build_pink_burst_effect(spark_texture: bytes) -> bytes:
    # effect.graph.json -- small pink contact explosion. The pink tint lives in
    # the effect/texture data because the OG explosion path is hard-white with
    # no scalar tint; this custom effect carries the colour.
    # NOTE: effect.explosion / effect.spark are ILLUSTRATIVE node kinds. Unlike
    # the weapon/projectile graphs (whose kinds are pinned to the live
    # weapon_graph_runtime.c module table), there is no in-tree effect-graph
    # runtime to validate these against yet. The conformance checker only
    # requires effect.graph.json to be valid JSON with no forbidden members /
    # bad refs -- it does not enforce an effect-node-kind enum. These kinds and
    # the "tint" params are the authored intent for the pink burst and will need
    # alignment with the real effect-graph runtime when/if it lands.
    effect_graph = dumps_graph({
        "schema": "pd.effect_graph.v1",
        "asset_id": BURST_EFFECT_ID,
        "nodes": [
            {"id": "burst", "kind": "effect.explosion",
             "params": {"explosion_class": "small", "tint": [1.0, 0.4, 0.8, 1.0]}},
            {"id": "spark", "kind": "effect.spark",
             "params": {"tint": [1.0, 0.5, 0.85, 1.0]}},
        ],
        "edges": [{"from": "burst", "to": "spark"}],
    })
    timeline = dumps_graph({
        "schema": "pd2.effect.timeline.v1",
        "tracks": [
            {"time": 0.0, "property": "intensity", "value": 1.0},
            {"time": 0.18, "property": "intensity", "value": 0.0},
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
         "explosion_class = small\n"
         "intensity = 1.0\n"),
        ("effect.graph.json", effect_graph),
        ("timeline.json", timeline),
        ("dependencies/assets/textures/spark.pdtexture", spark_texture),
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
                "model_ref": MESH_ID,
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
                "model_ref": MESH_ID,
                "source_mode": "secondary",
                "source_function_type": "shoot_projectile",
                "scale": 1.0, "damage": 5.0}},
            {"id": "motion", "kind": "projectile.motion", "params": {
                "motion_kind": "powered", "speed": 26.0,
                "powered": True, "timer60": 180}},
            {"id": "impact", "kind": "projectile.impact", "params": {
                "impact_filter": "any",
                "explosion_ref": BURST_EFFECT_ID,
                "spark_ref": SPARK_TEXTURE_ID,
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
def build_weapon(needle_mesh: bytes, crystal_material: bytes, body_texture: bytes,
                 homing_projectile: bytes, burst_projectile: bytes) -> bytes:
    # event.trigger_pressed + spawn.fired_projectile are live weapon node kinds
    # (weapon_graph_runtime.c s_modules lines 63, 76). The spawn node reads
    # "mode", "function_type" (-> INVENTORYFUNCTYPE_SHOOT_PROJECTILE for
    # "shoot_projectile") and "projectile_ref" (heldFunctionFromNode ~2495-2593).
    def weapon_graph(graph_id: str, trigger_id: str, mode: str,
                     projectile_ref: str) -> str:
        return dumps_graph({
            "schema": "pd.weapon_graph.v1",
            "asset_id": WEAPON_ID,
            "graph_id": graph_id,
            "nodes": [
                {"id": trigger_id, "kind": "event.trigger_pressed",
                 "params": {"mode": mode}},
                {"id": "spawn_projectile", "kind": "spawn.fired_projectile",
                 "params": {"mode": mode, "function_type": "shoot_projectile",
                            "projectile_ref": projectile_ref}},
            ],
            "edges": [{"from": trigger_id, "to": "spawn_projectile"}],
            "exports": [{"name": graph_id, "node": "spawn_projectile"}],
        })

    primary_graph = weapon_graph(
        "primary", "trigger_primary", "primary", HOMING_PROJECTILE_ID)
    secondary_graph = weapon_graph(
        "secondary", "trigger_secondary", "secondary", BURST_PROJECTILE_ID)
    return write_archive(MOD_DIR / "needler.pdweapon", [
        ("weapon.ini",
         "; needler.pdweapon - Halo-Needler-style weapon with typed dependency closure\n"
         "[weapon]\n"
         f"catalog_id = {WEAPON_ID}\n"
         "name = Needler\n"
         "dual_wieldable = false\n"
         "model_file = dependencies/assets/models/weapon.pdmesh\n"
         "primary_graph = behavior/primary.graph.json\n"
         "secondary_graph = behavior/secondary.graph.json\n"
         "settings_file = behavior/settings.json\n"
         "variables_file = behavior/variables.json\n"
         "shared_context_file = behavior/shared-context.json\n"
         "material_slots_file = bindings/material-slots.json\n"
         "grip_sockets_file = bindings/grip-sockets.json\n"
         "presentation_file = bindings/presentation.json\n"
         "primary_projectile_archive = dependencies/assets/projectiles/primary.pdprojectile\n"
         "\n[meta]\n"
         "manifest = _meta/manifest.json\n"),
        ("behavior/primary.graph.json", primary_graph),
        ("behavior/secondary.graph.json", secondary_graph),
        ("behavior/settings.json", dumps_graph({
            "schema": "pd.weapon.settings.v1",
            "fire_cadence": {"value": 8, "unit": "centiseconds"},
        })),
        ("behavior/variables.json", dumps_graph({
            "schema": "pd.weapon.variables.v1",
            "ammo_clip": 20,
            "ammo_reserve": 100,
        })),
        ("behavior/shared-context.json", dumps_graph({
            "schema": "pd.weapon.shared_context.v1",
            "contexts": ["owner_player", "owner_team",
                         "weapon_instance", "damage_credit_player"],
        })),
        # "material" here is an intra-archive member path (not a catalog-ref
        # key), matching the example tri_weapon material-slots shape exactly.
        ("bindings/material-slots.json", dumps_graph({
            "schema": "pd.weapon.material_slots.v1",
            "slots": [{"name": "body",
                       "material": "dependencies/assets/materials/default.pdmaterial"}],
        })),
        ("bindings/grip-sockets.json", dumps_graph({
            "schema": "pd.weapon.grip_sockets.v1",
            "sockets": [{"name": "primary_grip", "mesh_socket": "grip"}],
        })),
        ("bindings/presentation.json", dumps_graph({
            "schema": "pd.weapon.presentation.v1",
            "crosshair": "default",
        })),
        ("dependencies/assets/models/weapon.pdmesh", needle_mesh),
        ("dependencies/assets/materials/default.pdmaterial", crystal_material),
        ("dependencies/assets/textures/body.pdtexture", body_texture),
        ("dependencies/assets/projectiles/primary.pdprojectile", homing_projectile),
        ("dependencies/assets/projectiles/secondary.pdprojectile", burst_projectile),
        ("_meta/manifest.json", manifest_with("weapon", WEAPON_ID, {
            "model_file": "dependencies/assets/models/weapon.pdmesh",
            "primary_graph": "behavior/primary.graph.json",
            "secondary_graph": "behavior/secondary.graph.json",
            "settings_file": "behavior/settings.json",
            "variables_file": "behavior/variables.json",
            "shared_context_file": "behavior/shared-context.json",
            "material_slots_file": "bindings/material-slots.json",
            "grip_sockets_file": "bindings/grip-sockets.json",
            "presentation_file": "bindings/presentation.json",
            "primary_projectile_archive": "dependencies/assets/projectiles/primary.pdprojectile",
        })),
    ])


# ---------------------------------------------------------------------------
# mod.json -- rewrite the reserved manifest with the real contents/asset list.
# ---------------------------------------------------------------------------
def write_mod_json() -> None:
    mod = {
        "id": "needler",
        "name": "Needler",
        "version": "0.2.0",
        "author": "PD2",
        "description": (
            "Halo-Needler-style custom weapon. Primary fire: tracking needles "
            "(active homing depends on a separate runtime homing-gate adapter, "
            "out of scope for this data-only mod). Secondary fire: non-tracking "
            "needles that explode on contact with small pink explosions. "
            "Pure data .pdmod: one self-contained needler.pdweapon embedding its "
            "full typed dependency closure (mesh, material, textures, projectiles, "
            "and the pink contact-explosion effect)."
        ),
        "contents": ["weapon"],
        "requires_restart": False,
        "assets": [
            {"catalog_id": WEAPON_ID, "kind": "weapon", "archive": "needler.pdweapon"},
        ],
    }
    (MOD_DIR / "mod.json").write_text(json.dumps(mod, indent=2) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# Driver.
# ---------------------------------------------------------------------------
def build() -> Path:
    MOD_DIR.mkdir(parents=True, exist_ok=True)

    # Leaf dependencies first.
    needle_mesh = build_needle_mesh()
    body_texture = build_body_texture()
    spark_texture = build_spark_texture()
    crystal_material = build_crystal_material(body_texture)
    burst_effect = build_pink_burst_effect(spark_texture)

    # Projectiles embed their own closures.
    homing_projectile = build_homing_projectile(needle_mesh)
    burst_projectile = build_burst_projectile(needle_mesh, burst_effect, spark_texture)

    # The weapon embeds everything.
    build_weapon(
        needle_mesh, crystal_material, body_texture,
        homing_projectile, burst_projectile,
    )

    write_mod_json()
    return MOD_DIR / "needler.pdweapon"


def main() -> int:
    weapon_path = build()
    print(f"wrote {weapon_path.relative_to(REPO_ROOT)}")
    print(f"wrote {(MOD_DIR / 'mod.json').relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
