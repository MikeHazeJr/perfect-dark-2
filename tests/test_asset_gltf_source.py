import copy
import io
import json
from pathlib import Path
import struct
import sys
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from asset_gltf_source import (
    buffer_declaration, declared_archive_buffers, load_buffer,
    parse_document, resolve_buffer_member,
    scene_plan, transform_position,
)
from asset_archive_conformance import (
    pdmesh_gltf_json_and_bin, validate_archive_bytes,
    validate_pdmesh_gltf_integer_native_boundary,
)


class GltfSourceTests(unittest.TestCase):
    def setUp(self):
        self.data = struct.pack("<9f", 0, 0, 0, 8, 0, 0, 0, 8, 0)
        self.doc = {
            "asset": {"version": "2.0"},
            "buffers": [{"uri": "data/mesh%20source.bin", "byteLength": 36}],
            "bufferViews": [{"buffer": 0, "byteLength": 36}],
            "accessors": [{"bufferView": 0, "componentType": 5126,
                           "count": 3, "type": "VEC3"}],
            "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
        }

    def archive(self, doc=None, payload=None, extra=None):
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w") as archive:
            archive.writestr("model.gltf", json.dumps(doc or self.doc))
            archive.writestr("data/mesh source.bin", self.data if payload is None else payload)
            archive.writestr("mesh.ini", "[mesh]\nid=mod_test:mesh_triangle\nmodel_file=model.gltf\n")
            archive.writestr("_meta/manifest.json", "{}")
            for name, data in (extra or {}).items():
                archive.writestr(name, data)
        stream.seek(0)
        return stream

    def test_standard_local_buffer_reaches_real_mesh_boundary(self):
        with zipfile.ZipFile(self.archive()) as archive:
            errors = []
            self.assertEqual(declared_archive_buffers(archive, errors, "fixture"),
                             {"data/mesh source.bin"})
            self.assertEqual(errors, [])
            document, binary = pdmesh_gltf_json_and_bin(
                archive.read("model.gltf"), archive, "model.gltf")
            self.assertEqual(binary, self.data)
            validate_pdmesh_gltf_integer_native_boundary(
                "fixture", archive, set(archive.namelist()), errors)
            self.assertEqual(errors, [])

    def test_only_declared_buffers_receive_archive_exception(self):
        result = validate_archive_bytes(self.archive(extra={"raw.bin": b"dump"}).getvalue(),
                                        "fixture.pdmesh", ".pdmesh")
        self.assertTrue(any("raw.bin" in e and "forbidden" in e for e in result.errors))
        self.assertFalse(any("data/mesh source.bin" in e for e in result.errors), result.errors)

    def test_nonbuffer_binary_references_cannot_reuse_buffer_exception(self):
        for references in (
            {"images": [{"uri": "data/mesh%20source.bin"}]},
            {"images": [{"uri": "data/mesh%20source%2ebin"}]},
            {"extras": {"model_file": "data/mesh%20source.bin"}},
            {"extras": [{"model_FILE": "data/mesh%20source.bin"}]},
            {"extras": {"payload_uri": "data/mesh%20source.bin"}},
            {"images": [{"uri": "raw.bin.png"}]},
        ):
            doc = copy.deepcopy(self.doc)
            doc.update(references)
            with self.subTest(references=references):
                result = validate_archive_bytes(self.archive(doc).getvalue(),
                                                "fixture.pdmesh", ".pdmesh")
                self.assertTrue(any("forbidden non-buffer glTF" in e for e in result.errors),
                                result.errors)
                self.assertTrue(any("data/mesh source.bin" in e and "forbidden" in e
                                    for e in result.errors), result.errors)

    def test_decoded_nonbuffer_references_reject_invalid_and_missing_sources(self):
        for reference in ("missing.png", "../outside.png", "https://example.com/image.png",
                          "", None, 12, []):
            doc = copy.deepcopy(self.doc)
            doc["images"] = [{"uri": reference}]
            with self.subTest(reference=reference), zipfile.ZipFile(self.archive(doc)) as archive:
                errors = []
                self.assertEqual(declared_archive_buffers(archive, errors, "fixture"), set())
                self.assertTrue(errors)

        # Escaped JSON keys are classified after decoding, including bufferless extras.
        source = b'{"asset":{"version":"2.0"},"extras":{"model_\\u0066ile":"raw.bin"}}'
        with zipfile.ZipFile(self.archive(extra={"animation.gltf": source})) as archive:
            errors = []
            declared_archive_buffers(archive, errors, "fixture")
            self.assertTrue(any("animation.gltf" in e and "forbidden non-buffer" in e
                                for e in errors), errors)

    def test_valid_images_and_nonfile_metadata_preserve_buffer_exception(self):
        doc = copy.deepcopy(self.doc)
        doc["images"] = [{"uri": "images/base%20color.png"},
                         {"uri": "data:image/png;base64,aW1hZ2U="}]
        doc["extras"] = {
            "model_file": "model.gltf", "schema": "pd2.mesh.v1",
            "name": "artist.bin", "catalog_id": "example:asset.bin",
            "source_sha256": "metadata.bin", "documentation_uri": "https://example.com/docs",
        }
        with zipfile.ZipFile(self.archive(doc, extra={"images/base color.png": b"image"})) as archive:
            errors = []
            self.assertEqual(declared_archive_buffers(archive, errors, "fixture"),
                             {"data/mesh source.bin"})
            self.assertEqual(errors, [])

    def test_missing_and_mismatched_buffer_reject_before_admission(self):
        for payload in (b"", self.data[:-1], self.data + b"x"):
            with self.subTest(size=len(payload)), zipfile.ZipFile(self.archive(payload=payload)) as archive:
                errors = []
                self.assertEqual(declared_archive_buffers(archive, errors, "fixture"), set())
                self.assertTrue(errors)
                with self.assertRaises(ValueError):
                    load_buffer(self.doc, "model.gltf", archive)
        doc = copy.deepcopy(self.doc)
        doc["buffers"][0]["uri"] = "missing.bin"
        with zipfile.ZipFile(self.archive(doc)) as archive:
            with self.assertRaises(KeyError):
                load_buffer(doc, "model.gltf", archive)

    def test_declaration_rejects_multiple_wrong_and_out_of_bounds_buffers(self):
        for change in (
            lambda d: d["buffers"].append(d["buffers"][0]),
            lambda d: d["buffers"][0].update(byteLength=True),
            lambda d: d["bufferViews"][0].update(byteLength=37),
            lambda d: d["bufferViews"][0].update(buffer=1),
            lambda d: d["bufferViews"][0].update(byteStride=5),
            lambda d: d.update(bufferViews=[]),
        ):
            doc = copy.deepcopy(self.doc)
            change(doc)
            with self.assertRaises(ValueError):
                buffer_declaration(doc)

    def test_unsafe_and_aliased_uris_reject(self):
        self.assertEqual(resolve_buffer_member("models/model.gltf", "data/mesh%20source.bin"),
                         "models/data/mesh source.bin")
        for uri in ("../source.bin", "/source.bin", "https://x/a.bin", "C:/x.bin",
                    "a%2fb.bin", "%2e%2e/a.bin", "a%252fb.bin", "a.bin?x", "a//b.bin",
                    "CON.bin", "aux/x.bin", "COM1.bin"):
            with self.subTest(uri=uri), self.assertRaises(ValueError):
                resolve_buffer_member("models/model.gltf", uri)

    def test_json_duplicates_trailing_and_invalid_unicode_reject(self):
        for data in (b'{"asset":{},"asset":{}}', b'{}true', b'{"x":NaN}',
                     b'{"x":1e999}', b'{"x":"\\ud800"}', b'{"x":"\\u0000"}',
                     b'{"x":' + b'[' * 1100 + b'0' + b']' * 1100 + b'}'):
            with self.subTest(data=data), self.assertRaises((ValueError, UnicodeError)):
                parse_document(data)

    def test_scene_selection_instancing_and_geometry_library_defaults(self):
        doc = copy.deepcopy(self.doc)
        self.assertEqual(scene_plan(doc).instances[0].node_index, -1)
        doc["meshes"].append(copy.deepcopy(doc["meshes"][0]))
        doc["nodes"] = [{"mesh": 0, "translation": [10, 0, 0]},
                        {"mesh": 0, "translation": [20, 0, 0]}, {"mesh": 1}]
        doc["scenes"] = [{"nodes": [2]}, {"nodes": [1, 0]}]
        self.assertEqual(scene_plan(doc).selected_scene, 0)
        self.assertEqual(scene_plan(doc).instances[0].mesh_index, 1)
        doc["scene"] = 1
        plan = scene_plan(doc)
        self.assertEqual([instance.node_index for instance in plan.instances], [1, 0])
        self.assertEqual([instance.mesh_index for instance in plan.instances], [0, 0])
        self.assertEqual(transform_position(plan.instances[0], (0, 0, 0)), (20, 0, 0))
        self.assertEqual(transform_position(plan.instances[1], (0, 0, 0)), (10, 0, 0))
        # Sharing roots between scenes is legal, duplicating one selected root is not.
        doc["scenes"][0]["nodes"] = [0]
        self.assertEqual(len(scene_plan(doc).instances), 2)
        doc["scenes"][1]["nodes"] = [0, 0]
        with self.assertRaises(ValueError):
            scene_plan(doc)

    def test_scene_parent_transform_matrix_mirror_and_zero_scale(self):
        doc = copy.deepcopy(self.doc)
        doc["nodes"] = [
            {"rotation": [0, 0, 0.7071067811865476, 0.7071067811865476],
             "scale": [2, 3, 1], "children": [1]},
            {"mesh": 0, "translation": [4, 5, 6]},
        ]
        transformed = transform_position(scene_plan(doc).instances[0], (0, 0, 0))
        for actual, expected in zip(transformed, (-15, 8, 6)):
            self.assertAlmostEqual(actual, expected, places=6)
        doc["nodes"] = [{"mesh": 0, "matrix": [0, 2, 0, 0, -3, 0, 0, 0,
                                                0, 0, 4, 0, 5, 0, 0, 1]}]
        self.assertEqual(transform_position(scene_plan(doc).instances[0], (1, 1, 1)),
                         (2, 2, 4))
        doc["nodes"] = [{"mesh": 0, "scale": [-2, 3, 4], "children": [1]},
                        {"mesh": 0, "scale": [-1, 1, 1]},
                        {"mesh": 0, "scale": [0, 0, 0]}]
        plan = scene_plan(doc)
        self.assertEqual([instance.mirrored for instance in plan.instances], [True, False, False])
        self.assertEqual(transform_position(plan.instances[2], (1, 1, 1)), (0, 0, 0))

    def test_native_boundary_uses_transformed_float32_coordinates(self):
        for translation, rejected in ((32759, False), (32760, True),
                                      (32759.4991, True)):
            doc = copy.deepcopy(self.doc)
            doc["nodes"] = [{"mesh": 0, "translation": [translation, 0, 0]}]
            with self.subTest(translation=translation), zipfile.ZipFile(self.archive(doc)) as archive:
                errors = []
                validate_pdmesh_gltf_integer_native_boundary(
                    "fixture", archive, set(archive.namelist()), errors)
                self.assertEqual(bool(errors), rejected, errors)
        self.assertEqual(transform_position(scene_plan(doc).instances[0], (8, 0, 0))[0],
                         32767.5)

    def test_native_boundary_excludes_inactive_invalid_coordinates(self):
        doc = copy.deepcopy(self.doc)
        payload = self.data + struct.pack("<9f", float("inf"), 0, 0, 8, 0, 0, 0, 8, 0)
        doc["buffers"][0]["byteLength"] = len(payload)
        doc["bufferViews"].append({"buffer": 0, "byteOffset": 36, "byteLength": 36})
        doc["accessors"].append({"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"})
        doc["meshes"].append({"primitives": [{"attributes": {"POSITION": 1}}]})
        doc["nodes"] = [{"mesh": 0}, {"mesh": 1}]
        doc["scenes"] = [{"nodes": [0]}, {"nodes": [1]}]
        for selected in (0, 1):
            doc["scene"] = selected
            with self.subTest(scene=selected), zipfile.ZipFile(self.archive(doc, payload)) as archive:
                errors = []
                validate_pdmesh_gltf_integer_native_boundary(
                    "fixture", archive, set(archive.namelist()), errors)
                self.assertEqual(bool(errors), selected == 1, errors)

    def test_scene_cycles_bad_transforms_and_references_reject(self):
        for nodes in (
            [{"children": [0]}],
            [{"children": [1, 1]}, {"mesh": 0}],
            [{"children": [2]}, {"children": [2]}, {"mesh": 0}],
            [{"mesh": 0}, {"children": [2]}, {"children": [1]}],
            [{"mesh": 0, "rotation": [0, 0, 0, 2]}],
            [{"mesh": 0, "translation": [0, True, 0]}],
            [{"mesh": False}], [{"mesh": 0.5}], [{"mesh": 9}],
        ):
            doc = copy.deepcopy(self.doc)
            doc["nodes"] = nodes
            with self.subTest(nodes=nodes), self.assertRaises(ValueError):
                scene_plan(doc)
            with zipfile.ZipFile(self.archive(doc)) as archive:
                errors = []
                validate_pdmesh_gltf_integer_native_boundary(
                    "fixture", archive, set(archive.namelist()), errors)
                self.assertTrue(errors)

    def test_deep_scene_hierarchy_and_inactive_transform_exclusion(self):
        doc = copy.deepcopy(self.doc)
        count = 4096
        doc["nodes"] = [{"translation": [1, 0, 0], "children": [i + 1]}
                        for i in range(count - 1)]
        doc["nodes"].append({"translation": [1, 0, 0], "mesh": 0})
        plan = scene_plan(parse_document(json.dumps(doc).encode()))
        self.assertEqual(plan.instances[0].node_index, count - 1)
        self.assertEqual(transform_position(plan.instances[0], (0, 0, 0)), (count, 0, 0))
        doc["nodes"] = [{"mesh": 0}, {"scale": [1e308, 1, 1], "children": [2]},
                        {"mesh": 0, "scale": [1e308, 1, 1]}]
        doc["scenes"] = [{"nodes": [0]}, {"nodes": [1]}]
        self.assertEqual(len(scene_plan(doc).instances), 1)
        doc["scene"] = 1
        with self.assertRaises(ValueError):
            scene_plan(doc)

    def test_glb_uses_strict_document_and_exact_binary_bounds(self):
        def glb(json_bytes, binary=self.data, extra=b""):
            json_bytes += b" " * (-len(json_bytes) % 4)
            binary += b"\x00" * (-len(binary) % 4)
            chunks = struct.pack("<II", len(json_bytes), 0x4e4f534a) + json_bytes
            chunks += struct.pack("<II", len(binary), 0x004e4942) + binary + extra
            return struct.pack("<4sII", b"glTF", 2, len(chunks) + 12) + chunks

        doc = copy.deepcopy(self.doc)
        del doc["buffers"][0]["uri"]
        encoded = json.dumps(doc).encode()
        self.assertEqual(pdmesh_gltf_json_and_bin(glb(encoded))[1], self.data)
        for source in (
            glb(encoded, self.data[:-4]),
            glb(encoded, extra=struct.pack("<II", 4, 0x004e4942) + b"xxxx"),
            glb(encoded.replace(b'"asset":', b'"asset":{},"asset":', 1)),
            glb(encoded) + b"tail",
        ):
            with self.assertRaises(ValueError):
                pdmesh_gltf_json_and_bin(source)


if __name__ == "__main__":
    unittest.main()
