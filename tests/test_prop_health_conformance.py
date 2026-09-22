import io
import json
from pathlib import Path
import struct
import sys
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from asset_archive_conformance import (
    is_native_prop_health, validate_prop_source_contract,
)


def float_from_bits(bits):
    return struct.unpack("<f", struct.pack("<I", bits))[0]


class PropHealthConformanceTests(unittest.TestCase):
    def check_archive(self, health, graph_health):
        prop = {
            "schema": "pd2.prop.v2", "catalog_id": "test:health",
            "prop_key": "object", "display_name": "Health boundary",
            "health": health, "flags": 0,
        }
        graph = {
            "schema": "pd.prop_behavior.v1", "asset_id": "test:health",
            "graph_id": "runtime",
            "nodes": [
                {"id": "spawn", "kind": "event.spawn", "params": {}},
                {"id": "health", "kind": "action.set_health",
                 "params": {"value": graph_health}},
            ],
            "edges": [{"from": "spawn", "to": "health"}],
        }
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w") as archive:
            archive.writestr("prop.ini", "[prop]\ncatalog_id=test:health\n"
                             "prop_file=prop.json\nmodel_file=model.gltf\n"
                             "behavior_graph=behavior.graph.json\n")
            archive.writestr("prop.json", json.dumps(prop))
            archive.writestr("behavior.graph.json", json.dumps(graph))
            archive.writestr("model.gltf", '{"asset":{"version":"2.0"}}')
        stream.seek(0)
        with zipfile.ZipFile(stream) as archive:
            errors = []
            validate_prop_source_contract("health fixture", archive,
                                          set(archive.namelist()), errors)
        return errors

    def test_native_float_boundary_is_used_by_both_source_contracts(self):
        bits = struct.unpack("<I", struct.pack("<f", 3276.7))[0]
        values = [
            (0, True), (-0.0, True), (125.55, True), (3276, True),
            (float_from_bits(bits - 1), True),
            (float_from_bits(bits), True), (3276.7, True),
            (float_from_bits(bits + 1), False), (3277, False),
            (1e20, False), (1e100, False), (10 ** 400, False), (-1, False),
        ]
        for value, valid in values:
            with self.subTest(value=value):
                self.assertEqual(is_native_prop_health(value), valid)
                for source_health, graph_health, diagnostic in (
                    (value, 125, "prop.json health"),
                    (125, value, "action.set_health"),
                ):
                    errors = self.check_archive(source_health, graph_health)
                    if valid:
                        self.assertEqual(errors, [])
                    else:
                        self.assertEqual(len(errors), 1, errors)
                        self.assertIn(diagnostic, errors[0])
                        self.assertIn("native range 0..3276.7", errors[0])

    def test_nonnumeric_and_nonfinite_values_reject_in_both_source_contracts(self):
        for value in (True, False, None, "125", [], {},
                      float("inf"), float("-inf"), float("nan")):
            with self.subTest(value=value):
                self.assertFalse(is_native_prop_health(value))
                errors = self.check_archive(value, value)
                self.assertEqual(len(errors), 2, errors)
                self.assertTrue(any("prop.json health" in e for e in errors), errors)
                self.assertTrue(any("action.set_health" in e for e in errors), errors)


if __name__ == "__main__":
    unittest.main()
