#!/usr/bin/env python3
"""Tests for the archive validator's reflective material check (B-1268).

The room display lists of the game light and texture-generate their
reflective surfaces (G_LIGHTING | G_TEXTURE_GEN). Such a vertex's colour
entry is a normal, and fast3d generates its texture coordinates from that
normal and the camera's reflection vectors. A base scene.glb names those
materials (pd2_material.texgen true) with the G_TEXTURE scale the UVs are
generated with (texgen_scale) and the runtime UV scale they are converted
with (uv_scale); the scene renderer lights and maps them per view. Such a
material's own UVs are never used, so it is never also animated.

Run: python -B tools/tests/test_scene_texgen_conformance.py -v
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

STAMP = ("Perfect Dark 2 PDSCENARIO scene.glb exporter "
         "bg_visual_scene_glb_v12_dccuv_rsptexscale_texshift_samplerwrap_untextured_"
         "uvbound_color0_alphamask_materialextras_dualtex_alphablend_cipalfix_b943_"
         "iafix_b945_dyntex_allrooms_b1266_cull_b1267_texgen_b1268")


def material(**pd2_extra):
    pd2 = {
        "texture_command": "lod_texture", "primary_image": "", "secondary_image": "",
        "wrap_s": "repeat", "wrap_t": "repeat", "offset": 2, "shift_s": 0, "shift_t": 0,
        "min_lod": 0, "tile_flag": 0, "cull": "back",
    }
    pd2.update(pd2_extra)
    return {"name": "mat", "alphaMode": "OPAQUE", "extras": {"pd2_material": pd2}}


def glb(materials):
    # One triangle with TEXCOORD_0: the validator looks at materials only in
    # a scene that has textured geometry.
    body = struct.pack("<6f", 0.0, 0.0, 1.0, 0.0, 0.0, 1.0)
    doc = {
        "asset": {"version": "2.0", "generator": STAMP},
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


def texgen_errors(materials):
    errors: list[str] = []
    conf.validate_scene_glb_texture_contract("test", glb(materials), errors)
    return [e for e in errors if "texgen" in e]


GOOD = dict(texgen=True, texgen_scale=[2816, 2816], uv_scale=[0.03125, 0.03125])


class SceneTexgenConformance(unittest.TestCase):
    def test_the_exporter_stamp_in_the_source_carries_the_mark(self):
        source = (TOOLS.parent / "port" / "src" / "romextract_pdarena.c").read_text(encoding="utf-8")
        line = next(l for l in source.splitlines()
                    if l.startswith("#define PDSCENARIO_BG_VISUAL_EXPORT_VERSION \""))
        self.assertIn("_texgen_b1268", line)
        # The cull mark stays: a scene with reflective materials records cull modes too.
        self.assertIn(conf.SCENE_CULL_EXPORT_MARK, line)

    def test_a_reflective_material_with_its_scales_passes(self):
        self.assertEqual(texgen_errors([material(**GOOD)]), [])
        self.assertEqual(texgen_errors([material(**dict(GOOD, texgen_scale=[2048, 4096]))]), [])

    def test_a_material_without_the_field_is_not_held_to_it(self):
        self.assertEqual(texgen_errors([material()]), [])

    def test_the_field_is_true_when_present(self):
        for value in (False, 1, "yes", None):
            with self.subTest(texgen=value):
                bad = texgen_errors([material(**dict(GOOD, texgen=value))])
                self.assertEqual(len(bad), 1, bad)
                self.assertIn("pd2_material.texgen must be true when present", bad[0])

    def test_the_scale_is_two_integers_from_1_to_65535(self):
        for scale in ([0, 2816], [70000, 1], [2816], "2816", [True, 2816], [2816.5, 2816], None):
            with self.subTest(texgen_scale=scale):
                pd2 = dict(GOOD, texgen_scale=scale)
                if scale is None:
                    del pd2["texgen_scale"]
                bad = texgen_errors([material(**pd2)])
                self.assertEqual(len(bad), 1, bad)
                self.assertIn("texgen_scale must be two integers from 1 to 65535", bad[0])

    def test_the_uv_scale_is_two_positive_numbers(self):
        for uv in ([0, 0.03125], [0.03125], ["a", 1], [-1.0, 1.0], None):
            with self.subTest(uv_scale=uv):
                pd2 = dict(GOOD, uv_scale=uv)
                if uv is None:
                    del pd2["uv_scale"]
                bad = texgen_errors([material(**pd2)])
                self.assertEqual(len(bad), 1, bad)
                self.assertIn("uv_scale must be two positive numbers on a texgen material", bad[0])

    def test_a_reflective_material_is_never_animated(self):
        bad = texgen_errors([material(**dict(GOOD, animation="river"))])
        self.assertEqual(len(bad), 1, bad)
        self.assertIn("is texgen and animated", bad[0])


if __name__ == "__main__":
    unittest.main()
