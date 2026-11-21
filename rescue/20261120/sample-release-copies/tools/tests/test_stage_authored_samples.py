"""Exercise the reviewed inputs and no-overwrite install staging with real .pdmod files."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile


COPY_ROOT = Path(__file__).resolve().parents[2]
SOURCE_REPO = Path(os.environ.get("PD2_SAMPLE_TEST_SOURCE_REPO", str(COPY_ROOT)))
sys.path.insert(0, str(COPY_ROOT / "tools"))

from stage_authored_samples import BUNDLED_MODS, INVENTORY, stage_samples


class AuthoredSampleStagingTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory(prefix="pd2-sample-staging-test-")
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)
        self.inventory = COPY_ROOT / INVENTORY

    def make_install(self, name):
        install = self.root / name
        install.mkdir()
        return install

    def stage(self, install, *, repo=SOURCE_REPO, inventory=None, fresh=False):
        return stage_samples(repo, install, inventory or self.inventory, fresh=fresh)

    def test_equal_package_inventory_from_all_three_pristine_staging_calls(self):
        calls = [("local-dev", False), ("packaged-dev", True), ("packaged-stable", True)]
        receipts = [self.stage(self.make_install(name), fresh=fresh) for name, fresh in calls]
        self.assertEqual(receipts[0], receipts[1])
        self.assertEqual(receipts[1], receipts[2])
        self.assertEqual([entry["id"] for entry in receipts[0]], ["blank_room", "model_room"])
        for name, _ in calls:
            install = self.root / name
            self.assertEqual(set((install / BUNDLED_MODS).iterdir()),
                             {install / entry["path"] for entry in receipts[0]})
            for entry in receipts[0]:
                packaged_file = install / entry["path"]
                self.assertEqual(hashlib.sha256(packaged_file.read_bytes()).hexdigest(),
                                 entry["installed_sha256"])
                with zipfile.ZipFile(packaged_file) as package:
                    self.assertEqual(set(package.namelist()), set(entry["members"]))
                    for member, digest in entry["members"].items():
                        self.assertEqual(hashlib.sha256(package.read(member)).hexdigest(), digest)
                    metadata = json.loads(package.read("mod.json"))
                    self.assertEqual((metadata["id"], metadata["version"]),
                                     (entry["id"], entry["version"]))

    def test_local_restage_preserves_modified_package_override_and_saved_order(self):
        install = self.make_install("existing-dev")
        first = self.stage(install)
        edited = install / first[0]["path"]
        edited.write_bytes(b"player changed an installed package")
        override = install / "mods" / "blank_room"
        override.mkdir()
        override_bytes = b'{"id": "blank_room", "name": "Edited Blank Room", "version": "0.1.1"}'
        (override / "mod.json").write_bytes(override_bytes)
        user_home = install / "user-home"
        user_home.mkdir()
        enabled = user_home / "mods-enabled.json"
        enabled.write_bytes(b'["model_room", "my_extra_mod"]')
        restaged = self.stage(install)
        self.assertEqual([entry["outcome"] for entry in restaged],
                         ["preserved-existing", "already-exact"])
        self.assertIsNone(restaged[0]["installed_sha256"])
        self.assertEqual(restaged[0]["candidate_sha256"], first[0]["installed_sha256"])
        self.assertEqual(edited.read_bytes(), b"player changed an installed package")
        self.assertEqual((override / "mod.json").read_bytes(), override_bytes)
        self.assertEqual(enabled.read_bytes(), b'["model_room", "my_extra_mod"]')

    def test_fresh_staging_refuses_any_preexisting_install_mod(self):
        install = self.make_install("dirty-release")
        mods = install / "mods"
        mods.mkdir()
        preexisting = mods / "third-party.pdmod"
        preexisting.write_bytes(b"untouched")
        with self.assertRaisesRegex(ValueError, "absent install mods directory"):
            self.stage(install, fresh=True)
        self.assertEqual(list(mods.iterdir()), [preexisting])
        self.assertEqual(preexisting.read_bytes(), b"untouched")

    def test_fresh_publication_failure_leaves_no_partial_install(self):
        install = self.make_install("refused-publication")
        rename = Path.rename

        def refuse_publication(source, destination):
            if destination == install / "mods":
                raise PermissionError("simulated publication refusal")
            return rename(source, destination)

        with patch.object(Path, "rename", refuse_publication):
            with self.assertRaisesRegex(PermissionError, "publication refusal"):
                self.stage(install, fresh=True)
        self.assertEqual(list(install.iterdir()), [])
        self.assertEqual(len(self.stage(install, fresh=True)), 2)

    def test_changed_reviewed_member_fails_before_installing_other_samples(self):
        install = self.make_install("tampered-source-install")
        copied_repo = self.root / "tampered-source"
        tools = copied_repo / "tools"
        tools.mkdir(parents=True)
        shutil.copyfile(SOURCE_REPO / "tools/verify_pdxxx_modder_workflow.py",
                        tools / "verify_pdxxx_modder_workflow.py")
        samples = json.loads(self.inventory.read_text(encoding="utf-8"))["samples"]
        for sample in samples:
            folder = copied_repo / "dev-mods" / sample["id"]
            folder.mkdir(parents=True)
            for member in sample["members"]:
                shutil.copyfile(SOURCE_REPO / "dev-mods" / sample["id"] / member,
                                folder / member)
        (copied_repo / "dev-mods/model_room/model_room.pdarena").write_bytes(b"unreviewed change")
        with self.assertRaisesRegex(ValueError, "Reviewed member digest changed"):
            self.stage(install, repo=copied_repo, fresh=True)
        self.assertEqual(list(install.iterdir()), [])

    def test_rejects_duplicate_ids_and_unsafe_inventory_member(self):
        install = self.make_install("invalid-inventory-install")
        staged_inventory = self.root / "bad-inventory.json"
        original = self.inventory.read_text(encoding="utf-8")
        for text, error in [(original.replace('"model_room"', '"blank_room"'), "duplicate"),
                            (original.replace('"blank_room.pdarena"', '"../escape.pdarena"'), "Invalid member")]:
            with self.subTest(error=error):
                staged_inventory.write_text(text, encoding="utf-8")
                with self.assertRaisesRegex(ValueError, error):
                    self.stage(install, inventory=staged_inventory, fresh=True)
                self.assertEqual(list(install.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
