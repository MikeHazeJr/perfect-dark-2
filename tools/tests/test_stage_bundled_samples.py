"""Tests for tools/stage_bundled_samples.py (T-RELEASE-007).

The first case stages the real dev-mods.json release set into a scratch
install. The others use a small fake repository whose verifier is a stub, to
prove the refusals and that a failed run leaves the install as it was.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import textwrap
import unittest
from unittest import mock
import zipfile

REPO = Path(__file__).resolve().parents[2]


def load_stager():
    spec = importlib.util.spec_from_file_location(
        "stage_bundled_samples_under_test", REPO / "tools" / "stage_bundled_samples.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


STAGER = load_stager()

STUB_VERIFIER = textwrap.dedent('''\
    """Stand-in release verifier: zips the staged folder, or fails on request."""
    import argparse, os, sys, zipfile
    from pathlib import Path
    TYPED_DESCRIPTORS = {".pdarena": "arena.ini", ".pdvehicle": "vehicle.ini"}
    if __name__ == "__main__":
        ap = argparse.ArgumentParser()
        ap.add_argument("--source-root", type=Path)
        ap.add_argument("--release-package", type=Path)
        args = ap.parse_args()
        if args.source_root.name in os.environ.get("STUB_VERIFY_FAIL", "").split(","):
            sys.exit(1)
        with zipfile.ZipFile(args.release_package, "w") as zf:
            for path in sorted(args.source_root.iterdir()):
                zf.writestr(zipfile.ZipInfo(path.name, (1980, 1, 1, 0, 0, 0)), path.read_bytes())
    ''')


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class FakeRepo:
    """A repository with a stub verifier and the given dev-mods entries."""

    def __init__(self, root: Path, mods: list[dict]):
        self.root = root
        (root / "tools").mkdir(parents=True)
        (root / "tools" / "verify_pdxxx_modder_workflow.py").write_text(STUB_VERIFIER, encoding="utf-8")
        (root / "dev-mods").mkdir()
        manifest = {"schema": "pd2-dev-mods-v1", "description": "test", "mods": []}
        for mod in mods:
            manifest["mods"].append({key: mod[key] for key in ("id", "path", "dev", "release")})
            folder = root / "dev-mods" / mod["path"]
            folder.mkdir()
            (folder / "mod.json").write_text(json.dumps({"id": mod.get("manifest_id", mod["id"]),
                                                         "name": mod["id"], "version": "1.0"}),
                                             encoding="utf-8")
            for name, data in mod.get("files", {}).items():
                target = folder / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
        (root / "dev-mods" / "dev-mods.json").write_text(json.dumps(manifest), encoding="utf-8")


def arena(mod_id: str, **extra) -> dict:
    entry = {"id": mod_id, "path": mod_id, "dev": True, "release": True,
             "files": {f"{mod_id}.pdarena": b"arena " + mod_id.encode()}}
    entry.update(extra)
    return entry


class RealReleaseSetTest(unittest.TestCase):
    def test_stages_the_release_set_in_manifest_order(self):
        manifest = json.loads((REPO / "dev-mods" / "dev-mods.json").read_text(encoding="utf-8"))
        expected = [m["id"] for m in manifest["mods"] if m.get("release") is True]
        self.assertIn("crystal_pistol", expected)
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            receipt = STAGER.stage(REPO, Path(first))
            again = STAGER.stage(REPO, Path(second))
            self.assertEqual([r["id"] for r in receipt], expected)
            inventory = (Path(first) / "bundled-mods.sha256").read_bytes().decode("ascii")
            lines = inventory.splitlines(keepends=True)
            self.assertEqual(len(lines), len(expected))
            for entry, line in zip(receipt, lines):
                package = Path(first) / entry["path"]
                self.assertEqual(line, f"{sha256(package)} *{entry['id']}.pdmod\n")
                source = REPO / "dev-mods" / next(m["path"] for m in manifest["mods"]
                                                  if m["id"] == entry["id"])
                with zipfile.ZipFile(package) as zf:
                    names = sorted(zf.namelist())
                allowed = sorted(["mod.json"] + [p.name for p in source.iterdir()
                                                 if p.suffix in STAGER.load_verifier(REPO).TYPED_DESCRIPTORS])
                self.assertEqual(names, allowed, entry["id"])
            # The same sources give the same packages and the same inventory.
            self.assertEqual([r["sha256"] for r in again], [r["sha256"] for r in receipt])
            self.assertEqual((Path(second) / "bundled-mods.sha256").read_bytes(), inventory.encode("ascii"))
            leftovers = [p.name for p in Path(first).iterdir() if p.name.startswith(".bundled-stage-")]
            self.assertEqual(leftovers, [])


class FakeRepoTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.install = self.base / "install"
        self.install.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def repo(self, mods):
        return FakeRepo(self.base / "repo", mods).root

    def test_packs_only_the_manifest_and_top_level_archives(self):
        repo = self.repo([arena("one", files={"one.pdarena": b"a", "README.md": b"notes",
                                              "build_one.py": b"print()", "model.glb": b"glb"}),
                          {"id": "dev_only", "path": "dev_only", "dev": True, "release": False}])
        receipt = STAGER.stage(repo, self.install)
        self.assertEqual([r["id"] for r in receipt], ["one"])
        with zipfile.ZipFile(self.install / "mods" / "bundled" / "one.pdmod") as zf:
            self.assertEqual(sorted(zf.namelist()), ["mod.json", "one.pdarena"])

    def test_refuses_a_typed_archive_below_the_top(self):
        repo = self.repo([arena("one", files={"one.pdarena": b"a",
                                              "template/tri.pdvehicle": b"template"})])
        with self.assertRaisesRegex(STAGER.StageError, "below the top"):
            STAGER.stage(repo, self.install)

    def test_refuses_a_manifest_with_another_id(self):
        repo = self.repo([arena("one", manifest_id="two")])
        with self.assertRaisesRegex(STAGER.StageError, "its id is not"):
            STAGER.stage(repo, self.install)

    def test_refuses_unsafe_or_duplicate_entries(self):
        for mods in ([arena("one", path="../one")],
                     [arena("../one", path="one")],
                     [arena("con", path="con_folder", files={"x.pdarena": b"a"})],
                     [arena("one"), arena("ONE".lower(), path="one_again")]):
            with self.subTest(mods=[m["id"] + "@" + m["path"] for m in mods]):
                shutil.rmtree(self.base / "repo", ignore_errors=True)
                for mod in mods:
                    mod.setdefault("path", mod["id"])
                try:
                    repo = self.repo(mods)
                except (OSError, ValueError):
                    continue  # the fixture itself cannot be made on this filesystem
                with self.assertRaises(STAGER.StageError):
                    STAGER.stage(repo, self.install)

    def test_a_failed_package_leaves_the_install_as_it_was(self):
        bundled = self.install / "mods" / "bundled"
        bundled.mkdir(parents=True)
        (bundled / "old.pdmod").write_bytes(b"old package")
        (self.install / "bundled-mods.sha256").write_bytes(b"old inventory\n")
        repo = self.repo([arena("one"), arena("two")])
        with mock.patch.dict(os.environ, {"STUB_VERIFY_FAIL": "two"}):
            with self.assertRaisesRegex(STAGER.StageError, "could not be packaged"):
                STAGER.stage(repo, self.install)
        self.assertEqual((bundled / "old.pdmod").read_bytes(), b"old package")
        self.assertEqual((self.install / "bundled-mods.sha256").read_bytes(), b"old inventory\n")
        self.assertFalse((bundled / "one.pdmod").exists())
        self.assertEqual([p.name for p in self.install.iterdir() if p.name.startswith(".")], [])

    def test_replaces_stale_packages_and_keeps_other_files(self):
        bundled = self.install / "mods" / "bundled"
        bundled.mkdir(parents=True)
        (bundled / "removed_sample.pdmod").write_bytes(b"stale")
        (bundled / "notes.txt").write_bytes(b"keep")
        repo = self.repo([arena("one")])
        STAGER.stage(repo, self.install)
        self.assertFalse((bundled / "removed_sample.pdmod").exists())
        self.assertEqual((bundled / "notes.txt").read_bytes(), b"keep")
        self.assertTrue((bundled / "one.pdmod").is_file())

    def test_only_stages_the_named_entries_and_an_empty_set_clears(self):
        repo = self.repo([arena("one"), arena("two")])
        receipt = STAGER.stage(repo, self.install, only={"two", "not_a_release_entry"})
        self.assertEqual([r["id"] for r in receipt], ["two"])
        bundled = self.install / "mods" / "bundled"
        self.assertTrue((bundled / "two.pdmod").is_file())
        self.assertFalse((bundled / "one.pdmod").exists())
        self.assertEqual(STAGER.stage(repo, self.install, only=set()), [])
        self.assertFalse((self.install / "bundled-mods.sha256").exists())
        self.assertFalse((bundled / "two.pdmod").exists())

    def test_no_release_entries_removes_the_old_inventory(self):
        (self.install / "bundled-mods.sha256").write_bytes(b"old inventory\n")
        repo = self.repo([{"id": "dev_only", "path": "dev_only", "dev": True, "release": False}])
        self.assertEqual(STAGER.stage(repo, self.install), [])
        self.assertFalse((self.install / "bundled-mods.sha256").exists())


if __name__ == "__main__":
    unittest.main()
