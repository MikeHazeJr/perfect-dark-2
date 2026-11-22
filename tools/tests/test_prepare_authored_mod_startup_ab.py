"""Check the new startup comparison controls without launching any client."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke-verify"))

from prepare_authored_mod_startup_ab import MOD_IDS, RELEASE_SHA256, load_packages, prepare


class StartupFixtureTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory(prefix="pd2-startup-ab-test-")
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)
        self.repo = self.root / "repo"
        runner = self.repo / "tools/smoke-verify/run.ps1"
        runner.parent.mkdir(parents=True)
        runner.touch()
        self.inventory = self.root / "inventory.json"
        self.output = self.root / "fixture"

    def write_inventory(self, *, root_audio=None, nested_audio=None):
        rows = []
        for mod_id in sorted(MOD_IDS):
            version = "0.2.0" if mod_id == "crystal_pistol" else "0.1.0"
            manifest = {"id": mod_id, "name": mod_id, "version": version}
            data = json.dumps(manifest).encode("utf-8")
            member = root_audio[1] if root_audio and mod_id == root_audio[0] else None
            if nested_audio and mod_id == nested_audio[0]:
                member = nested_audio[1]
            if mod_id == "grid_two_rooms":
                source = self.root / mod_id
                source.mkdir()
                (source / "mod.json").write_bytes(data)
                entries = [["mod.json", "file", hashlib.sha256(data).hexdigest()]]
                if member:
                    audio = source / member
                    audio.parent.mkdir(exist_ok=True)
                    audio.write_bytes(b"sample fixture only")
                    if audio.parent != source:
                        entries.append([audio.parent.name, "directory"])
                    entries.append([member, "file", hashlib.sha256(audio.read_bytes()).hexdigest()])
                inventory_bytes = json.dumps(sorted(entries), separators=(",", ":")).encode("utf-8")
                form = "loose"
            else:
                source = self.root / f"{mod_id}.pdmod"
                with zipfile.ZipFile(source, "w") as archive:
                    archive.writestr("mod.json", data)
                    if member:
                        archive.writestr(member, "sample fixture only")
                inventory_bytes = source.read_bytes()
                form = "archive"
            rows.append({**manifest, "source": str(source), "installed_path": source.name,
                         "form": form, "sha256": hashlib.sha256(inventory_bytes).hexdigest(),
                         "evidence": "synthetic unit-test input"})
        self.inventory.write_text(json.dumps(rows), encoding="utf-8")

    def test_approximate_pair_shares_inputs_and_carries_no_exact_incident_claim(self):
        self.write_inventory()
        definitions = prepare(self.repo, self.inventory, self.output, inventory_kind="approximate")
        receipt = json.loads((self.output / "receipt.json").read_text(encoding="utf-8"))
        self.assertEqual(receipt["inventory_kind"], "approximate")
        self.assertFalse(receipt["native_execution"])
        self.assertEqual(receipt["expected_client_sha256"], RELEASE_SHA256)
        self.assertEqual({entry["id"] for entry in receipt["packages"]}, MOD_IDS)
        self.assertTrue(all(not entry["audio_ini_paths"] for entry in receipt["packages"]))
        empty, detected = (json.loads((definitions / f"{name}.json").read_text(encoding="utf-8"))
                           for name in ("startup_empty_mods", "startup_detected_disabled_mods"))
        self.assertEqual(empty["boot_args"], detected["boot_args"])
        self.assertNotIn("--moddir", empty["boot_args"])
        self.assertEqual(empty["fixtures"][0], detected["fixtures"][0])
        self.assertEqual(json.loads((self.output / "payload/mods-enabled.json").read_text()), [])
        self.assertIn("modmgr: scan complete -- 0 mods found", empty["assertions"]["required_lines"])
        self.assertIn("modmgr: scan complete -- 5 mods found", detected["assertions"]["required_lines"])
        self.assertIn(r"modmgr: parsed audio\.ini for", detected["assertions"]["forbidden_patterns"])

    def test_archive_root_audio_manifest_refuses_disabled_only_pair(self):
        self.write_inventory(root_audio=("needler", "AuDio.INI"))
        with self.assertRaisesRegex(ValueError, "Root audio.ini.*needler"):
            prepare(self.repo, self.inventory, self.output, inventory_kind="approximate")
        self.assertFalse(self.output.exists())

    def test_loose_root_audio_manifest_refuses_disabled_only_pair(self):
        self.write_inventory(root_audio=("grid_two_rooms", "audio.ini"))
        with self.assertRaisesRegex(ValueError, "Root audio.ini.*grid_two_rooms"):
            load_packages(self.inventory)

    def test_nested_audio_name_is_recognized_separately_from_root(self):
        self.write_inventory(nested_audio=("grid_two_rooms", "docs/audio.ini"))
        packages, _ = load_packages(self.inventory)
        verified = next(record for row, _, _, record in packages if row["id"] == "grid_two_rooms")
        self.assertEqual(verified["audio_ini_paths"], ["docs/audio.ini"])


if __name__ == "__main__":
    unittest.main()
