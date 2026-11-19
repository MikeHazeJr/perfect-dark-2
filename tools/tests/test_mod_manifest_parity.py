"""Hold the offline mod.json validator to the shared verdict table.

tests/fixtures/modmanifest/cases.json lists one verdict per payload for the
offline validator (validate_manifest in tools/verify_pdxxx_modder_workflow.py)
and one for the in-game validator (modManifestValidateBytes in
port/src/mod_manifest_validate.cpp). This test owns the offline column;
tests/test_mod_manifest_validate.cpp owns the game column. Together they keep
"valid manifest" meaning the same thing in the tools and in the game.
"""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


def load_verifier(path):
    spec = importlib.util.spec_from_file_location("pdxxx_workflow_parity_under_test", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


REPO_ROOT = Path(__file__).resolve().parents[2]
VERIFIER = load_verifier(REPO_ROOT / "tools" / "verify_pdxxx_modder_workflow.py")
CASES_PATH = REPO_ROOT / "tests" / "fixtures" / "modmanifest" / "cases.json"
FIELDS = ("id", "name", "version")
VERDICT_KINDS = ("ok", "unreadable", "not_object", "field")


def payload_of(case):
    if "text" in case:
        return case["text"].encode("utf-8")
    return bytes.fromhex(case["hex"])


def offline_verdict(payload):
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / "mod.json").write_bytes(payload)
        errors = []
        VERIFIER.validate_manifest(Path(tmp), errors)
    if not errors:
        return "ok"
    if any("unreadable JSON" in error for error in errors):
        return "unreadable"
    if any("expected a JSON object" in error for error in errors):
        return "not_object"
    failing = [field for field in FIELDS
               if any(error.endswith("missing non-empty string " + field) for error in errors)]
    if not failing or len(failing) != len(errors):
        return "unrecognised:" + " | ".join(errors)
    return "field:" + ",".join(failing)


class ManifestParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.document = json.loads(CASES_PATH.read_text(encoding="utf-8"))
        cls.cases = cls.document["cases"]

    def test_table_is_well_formed(self):
        self.assertEqual("pd2.test.mod_manifest_cases.v1", self.document["schema"])
        self.assertGreaterEqual(len(self.cases), 90)
        names = [case["name"] for case in self.cases]
        self.assertEqual(len(names), len(set(names)))
        for case in self.cases:
            with self.subTest(case=case["name"]):
                self.assertEqual(1, ("text" in case) + ("hex" in case))
                for column in ("offline", "game"):
                    self.assertIn(case[column].split(":")[0], VERDICT_KINDS)

    def test_offline_validator_matches_its_column(self):
        for case in self.cases:
            with self.subTest(case=case["name"]):
                self.assertEqual(case["offline"], offline_verdict(payload_of(case)))

    def test_game_never_accepts_what_offline_refuses(self):
        for case in self.cases:
            with self.subTest(case=case["name"]):
                if case["game"] == "ok":
                    self.assertEqual("ok", case["offline"])

    def test_every_difference_is_a_documented_game_strictness(self):
        differing = [case for case in self.cases if case["game"] != case["offline"]]
        self.assertGreaterEqual(len(differing), 12)
        for case in self.cases:
            with self.subTest(case=case["name"]):
                if case["game"] != case["offline"]:
                    self.assertNotEqual("ok", case["game"])
                    self.assertTrue(case.get("stricter", "").strip())
                else:
                    self.assertNotIn("stricter", case)

    def test_table_covers_the_october_manifest_cases(self):
        by_name = {case["name"]: case for case in self.cases}
        for field in FIELDS:
            for label in ("null", "false", "true", "zero", "float", "array", "object",
                          "empty", "space", "tab_newline", "missing"):
                self.assertEqual("field:" + field,
                                 by_name["field_%s_%s" % (field, label)]["offline"])
        for label in ("null", "false", "true", "zero", "integer", "float", "string",
                      "empty_array", "array_of_manifest"):
            self.assertEqual("not_object", by_name["not_object_" + label]["offline"])
        for name in ("unreadable_open_brace", "unreadable_unterminated_string",
                     "unreadable_missing_value", "unreadable_invalid_utf8_byte"):
            self.assertEqual("unreadable", by_name[name]["offline"])
        self.assertEqual("ok", by_name["valid_padded_text_is_not_trimmed"]["offline"])


if __name__ == "__main__":
    unittest.main()
