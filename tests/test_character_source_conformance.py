"""Character source contracts distinguish an unbound head from a broken binding."""
import io
import json
from pathlib import Path
import sys
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from asset_archive_conformance import validate_character_source_contract


class CharacterSourceConformanceTests(unittest.TestCase):
    def check(self, head="", manifest_head=None, members=(), ini_extra="", manifest_extra=None,
              policy="integrated"):
        ini = ("[character]\ncatalog_id = test:character\nbody_asset = test:body\n"
               f"head_asset = {head}\nbodyfile = body.pdbody\n{ini_extra}")
        if policy is not None:
            ini += f"head_policy = {policy}\n"
        manifest = {"body": "test:body", "body_archive": "body.pdbody",
                    "head": manifest_head, "head_archive": None}
        manifest.update(manifest_extra or {})
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w") as zf:
            zf.writestr("character.ini", ini)
            zf.writestr("_meta/manifest.json", json.dumps(manifest))
            zf.writestr("body.pdbody", b"nested archive checked by recursive contract")
            for name in members:
                zf.writestr(name, b"nested archive checked by recursive contract")
        stream.seek(0)
        errors = []
        with zipfile.ZipFile(stream) as zf:
            validate_character_source_contract("fixture", zf, set(zf.namelist()), errors)
        return errors

    def test_explicit_policy_allows_unbound_head(self):
        for policy in ("integrated", "random_gender"):
            self.assertEqual(self.check(policy=policy), [])
        self.assertTrue(self.check(policy=None))

    def test_bound_head_requires_matching_catalog_identity_and_archive(self):
        self.assertEqual(self.check("test:head", "test:head", ("head.pdhead",),
                                   "headfile = head.pdhead\n", {"head_archive": "head.pdhead"},
                                   policy="fixed"), [])
        self.assertEqual(self.check("test:head", "test:head", ("head.pdhead",),
                                   "headfile = head.pdhead\n", {"head_archive": "head.pdhead"},
                                   policy=None), [])
        self.assertTrue(self.check("test:head", "test:different"))
        self.assertTrue(self.check("test:head", "test:head", ("head.pdhead",)))

    def test_unbound_head_cannot_hide_a_dependency(self):
        self.assertTrue(self.check(members=("head.pdhead",)))
        self.assertTrue(self.check(ini_extra="headfile = missing.pdhead\n"))
        self.assertTrue(self.check(manifest_extra={"head_archive": "head.pdhead"}))

    def test_malformed_and_mismatched_references_reject(self):
        for value in ("123", "", 1, False, [], {}):
            with self.subTest(value=value):
                self.assertTrue(self.check(manifest_head=value))
        self.assertTrue(self.check(head="12"))
        self.assertTrue(self.check(head="test:head"))
        self.assertTrue(self.check(manifest_head="test:head"))
        self.assertTrue(self.check(manifest_extra={"body": "test:different"}))

    def test_invalid_or_conflicting_head_policy_rejects(self):
        for policy in ("", "random", "FIXED", "fixed"):
            self.assertTrue(self.check(policy=policy))
        self.assertTrue(self.check(manifest_extra={"head_policy": "fixed"}))
        self.assertTrue(self.check("test:head", "test:head", ("head.pdhead",),
                                  "headfile = head.pdhead\n", {"head_archive": "head.pdhead"}))

    def test_ambiguous_source_keys_reject(self):
        self.assertTrue(self.check(ini_extra="head_policy = fixed\n"))
        self.assertTrue(self.check(ini_extra="body_id = test:different\n"))
        self.assertTrue(self.check(ini_extra="body_archive = missing.pdbody\n"))
        self.assertEqual(self.check(ini_extra="body_id = test:body\n"), [])


if __name__ == "__main__":
    unittest.main()
