#!/usr/bin/env python3
"""Tests for the archive validator's scene cull mode check (B-1267).

The room display lists of the game set and clear G_CULL_BACK, and a base
scene.glb now says for every material whether the game drew it with back
faces culled (pd2_material.cull "back") or from both sides ("none", with
glTF's own doubleSided true). The scene renderer culls only where a scene
says "back", so a scene written before the field and an authored level keep
drawing every surface from both sides. The validator holds a scene to the
field only when its exporter stamp says the exporter records it.

Run: python -B tools/tests/test_scene_cull_conformance.py -v
"""

from __future__ import annotations

import json
import struct
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import asset_archive_conformance as conf  # noqa: E402

STAMP_BEFORE = ("Perfect Dark 2 PDSCENARIO scene.glb exporter "
                "bg_visual_scene_glb_v12_dccuv_rsptexscale_texshift_samplerwrap_untextured_"
                "uvbound_color0_alphamask_materialextras_dualtex_alphablend_cipalfix_b943_"
                "iafix_b945_dyntex")
STAMP_NOW = STAMP_BEFORE + "_allrooms_b1266_cull_b1267"


def material(cull=None, double_sided=None):
    pd2 = {
        "texture_command": "lod_texture", "primary_image": "", "secondary_image": "",
        "wrap_s": "repeat", "wrap_t": "repeat", "offset": 2, "shift_s": 0, "shift_t": 0,
        "min_lod": 0, "tile_flag": 0,
    }
    if cull is not None:
        pd2["cull"] = cull
    out = {"name": "mat", "alphaMode": "OPAQUE", "extras": {"pd2_material": pd2}}
    if double_sided is not None:
        out["doubleSided"] = double_sided
    return out


def glb(generator, materials):
    # One triangle with TEXCOORD_0: the validator looks at materials only in
    # a scene that has textured geometry.
    body = struct.pack("<6f", 0.0, 0.0, 1.0, 0.0, 0.0, 1.0)
    doc = {
        "asset": {"version": "2.0", "generator": generator},
        "materials": materials,
        "buffers": [{"byteLength": len(body)}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(body)}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC2"}],
        "meshes": [{"primitives": [{"attributes": {"TEXCOORD_0": 0}, "material": 0}]}],
    }
    text = json.dumps(doc).encode("utf-8")
    text += b" " * (-len(text) % 4)
    total = 12 + 8 + len(text) + 8 + len(body)
    return (struct.pack("<4sII", b"glTF", 2, total)
            + struct.pack("<II", len(text), 0x4E4F534A) + text
            + struct.pack("<II", len(body), 0x004E4942) + body)


def cull_errors(generator, materials):
    errors: list[str] = []
    conf.validate_scene_glb_texture_contract("test", glb(generator, materials), errors)
    return [e for e in errors if "pd2_material.cull" in e]


class SceneCullConformance(unittest.TestCase):
    def test_the_exporter_stamp_in_the_source_carries_the_mark(self):
        source = (TOOLS.parent / "port" / "src" / "romextract_pdarena.c").read_text(encoding="utf-8")
        line = next(l for l in source.splitlines()
                    if l.startswith("#define PDSCENARIO_BG_VISUAL_EXPORT_VERSION \""))
        self.assertIn(conf.SCENE_CULL_EXPORT_MARK, line)
        self.assertIn(conf.SCENE_CULL_EXPORT_MARK, STAMP_NOW)
        self.assertNotIn(conf.SCENE_CULL_EXPORT_MARK, STAMP_BEFORE)

    def test_a_current_scene_names_a_cull_mode_on_every_material(self):
        self.assertEqual(cull_errors(STAMP_NOW, [material("back"), material("none", True)]), [])
        missing = cull_errors(STAMP_NOW, [material("back"), material()])
        self.assertEqual(len(missing), 1, missing)
        self.assertIn("material 1 is missing pd2_material.cull", missing[0])

    def test_a_scene_from_before_the_field_is_not_held_to_it(self):
        self.assertEqual(cull_errors(STAMP_BEFORE, [material(), material()]), [])

    def test_the_field_is_back_or_none(self):
        bad = cull_errors(STAMP_NOW, [material("front")])
        self.assertEqual(len(bad), 1, bad)
        self.assertIn("must be back or none", bad[0])
        # Also on a scene that is not held to the field: present means valid.
        self.assertEqual(len(cull_errors(STAMP_BEFORE, [material("both")])), 1)

    def test_double_sided_agrees_with_it(self):
        for cull, double_sided in (("none", None), ("none", False), ("back", True)):
            with self.subTest(cull=cull, double_sided=double_sided):
                bad = cull_errors(STAMP_NOW, [material(cull, double_sided)])
                self.assertEqual(len(bad), 1, bad)
                self.assertIn("and doubleSided disagree", bad[0])
        for cull, double_sided in (("back", None), ("back", False), ("none", True)):
            with self.subTest(cull=cull, double_sided=double_sided):
                self.assertEqual(cull_errors(STAMP_NOW, [material(cull, double_sided)]), [])


if __name__ == "__main__":
    unittest.main()
