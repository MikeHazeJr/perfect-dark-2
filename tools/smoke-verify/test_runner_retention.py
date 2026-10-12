"""Tiny native-Windows runner output fixtures. All evidence is retained."""
import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock
import uuid

import runner_retention as r
from storage import CHECKOUT, StorageError, plain, walk

EVIDENCE = None
POLICY = dict(min_free_gib=0, max_full_installs=5, max_full_install_gib=.01,
              max_archive_gib=.01, peak_run_gib=0, extra_growth_gib=0, peak_build_gib=0)


class RunnerOutputTests(unittest.TestCase):
    def setUp(self):
        self.project = EVIDENCE / (self._testMethodName + "-" + uuid.uuid4().hex)
        self.project.mkdir()
        self.store = r.RunnerRetention(self.project, POLICY)
        with self.store.locked(): self.context = self.store.new_run({"owner_pid": os.getpid()})
        self.request = dict(owner_pid=os.getpid(), run_id=self.context["RunId"], name="result.json", value={"passed": False})

    def profile(self, reserved=4096):
        identifier = "reuse-" + uuid.uuid4().hex
        directory = self.store.root / "profiles" / identifier
        (directory / "profile").mkdir(parents=True)
        record = dict(id=identifier, status="active", path=directory.relative_to(self.store.root).as_posix(),
                      base_id="inert-base", owner=self.context["Owner"], reserved_bytes=reserved,
                      retention={"retain_housekeeping": True})
        marker = {k: record[k] for k in ("id", "base_id", "owner")}
        marker["schema"] = 1
        (directory / ".pd-reuse-owner.json").write_text(json.dumps(marker), encoding="utf-8")
        self.store.state.setdefault("reuse_runs", {})[identifier] = record
        self.store.save()
        return identifier, directory, record

    def test_context_is_fresh_and_registry_unchanged(self):
        with self.store.locked():
            other = self.store.new_run({"owner_pid": os.getpid()})
            self.assertNotEqual(self.context["RunRoot"], other["RunRoot"])
            self.assertTrue(Path(self.context["RunRoot"]).is_relative_to(self.store.root / "receipts"))
            self.assertFalse(self.store.state_path.exists())
            self.assertEqual(self.store.usage(), self.store.archive_used)

    def test_directory_collision_cannot_adopt_prior_root(self):
        collision = "runner-" + "a" * 32
        directory = self.store.root / "receipts" / collision
        directory.mkdir()
        marker = directory / "run-owner.json"
        marker.write_bytes(b"prior evidence")
        with self.store.locked(), mock.patch.object(r.uuid, "uuid4", return_value=mock.Mock(hex="a" * 32)):
            with self.assertRaises(FileExistsError): self.store.new_run({"owner_pid": os.getpid()})
            self.assertEqual(b"prior evidence", marker.read_bytes())

    def test_run_write_accounts_exact_utf8_without_double_count(self):
        self.request["value"] = {"passed": False, "label": "猫 café"}
        with self.store.locked():
            before = self.store.usage()
            result = self.store.write_run(self.request)
            path = Path(result["Path"])
            self.assertEqual("archive", result["AccountingDomain"])
            self.assertEqual(before + path.stat().st_size, result["ArchiveBytes"])
            self.assertEqual(self.request["value"], json.loads(path.read_text(encoding="utf-8")))
            self.assertFalse(list(path.parent.glob("result.json.*.tmp")))

    def test_existing_output_is_preserved(self):
        path = Path(self.context["RunRoot"]) / "result.json"
        path.write_bytes(b"prior failed result")
        with self.store.locked():
            with self.assertRaisesRegex(StorageError, "output collision"): self.store.write_run(self.request)
            self.assertEqual(b"prior failed result", path.read_bytes())

    def test_pid_creation_mismatch_refuses(self):
        marker = Path(self.context["RunRoot"]) / "run-owner.json"
        value = json.loads(marker.read_text())
        value["owner"]["creation_ticks"] += 1
        marker.write_text(json.dumps(value), encoding="utf-8")
        with self.store.locked():
            with self.assertRaisesRegex(StorageError, "PID/creation"): self.store.write_run(self.request)
            self.assertFalse((marker.parent / "result.json").exists())

    def test_invalid_identity_and_escape_names_refuse(self):
        with self.store.locked():
            for identifier in ["../outside", "runner-../outside", None]:
                with self.assertRaises(StorageError): self.store.write_run({**self.request, "run_id": identifier})
            for name in ["../escape.json", "a/b.json", "a\\b.json", "C:escape.json", "run-owner.json"]:
                with self.assertRaises(StorageError): self.store.write_run({**self.request, "name": name})
            self.assertFalse((self.store.root / "escape.json").exists())

    def test_initial_budget_refusal_creates_no_candidate(self):
        with self.store.locked():
            self.store.policy["max_archive_gib_bytes"] = self.store.usage()
            with self.assertRaisesRegex(StorageError, "Archive budget"): self.store.write_run(self.request)
            self.assertFalse(list(Path(self.context["RunRoot"]).glob("result*")))

    def test_actual_budget_refusal_retains_candidate(self):
        calls = 0
        def budget(*args):
            nonlocal calls
            calls += 1
            if calls > 1: raise StorageError("injected actual budget refusal")
        with self.store.locked(), mock.patch.object(self.store, "preflight", side_effect=budget):
            with self.assertRaisesRegex(StorageError, "actual budget refusal"): self.store.write_run(self.request)
            self.assertEqual(1, len(list(Path(self.context["RunRoot"]).glob("result.json.*.tmp"))))
            self.assertFalse((Path(self.context["RunRoot"]) / "result.json").exists())
            self.assertEqual(self.store.usage(), self.store.archive_used)
            self.assertIn("actual budget refusal", self.store.last_budget_error)

    def test_raced_publication_preserves_both_files(self):
        rename = r.os.rename
        def race(source, destination):
            Path(destination).write_bytes(b"raced evidence")
            rename(source, destination)
        with self.store.locked(), mock.patch.object(r.os, "rename", side_effect=race):
            with self.assertRaises(FileExistsError): self.store.write_run(self.request)
            directory = Path(self.context["RunRoot"])
            self.assertEqual(b"raced evidence", (directory / "result.json").read_bytes())
            self.assertEqual(1, len(list(directory.glob("result.json.*.tmp"))))
            self.assertEqual(self.store.usage(), self.store.archive_used)

    def test_partial_output_survives_and_is_charged(self):
        original_open = open
        @contextlib.contextmanager
        def partial_open(path, mode):
            with original_open(path, mode) as stream:
                def partial(value):
                    stream.write(value[:3])
                    raise OSError("injected partial output failure")
                yield mock.Mock(write=partial, flush=stream.flush)
        with self.store.locked(), mock.patch("runner_retention.open", side_effect=partial_open, create=True):
            # Context-marker reading uses Path.read_text, independently of this writer.
            with self.assertRaisesRegex(OSError, "partial output failure"): self.store.write_run(self.request)
            files = list(Path(self.context["RunRoot"]).glob("result.json.*.tmp"))
            self.assertEqual(1, len(files))
            self.assertEqual(3, files[0].stat().st_size)
            self.assertEqual(self.store.usage(), self.store.archive_used)

    def test_profile_uses_existing_reservation_and_preserves_registry(self):
        with self.store.locked():
            identifier, directory, _ = self.profile()
            before = self.store.state_path.read_bytes()
            archive = self.store.usage()
            result = self.store.write_profile({**self.request, "reuse_id": identifier})
            self.assertEqual("profile-reservation", result["AccountingDomain"])
            self.assertEqual(archive, result["ArchiveBytes"])
            self.assertEqual(sum(info.st_size for _, info in walk(directory)), result["ProfileBytes"])
            self.assertEqual(before, self.store.state_path.read_bytes())

    def test_profile_existing_temporary_consumes_reservation(self):
        with self.store.locked():
            identifier, directory, record = self.profile()
            (directory / "old-retained.tmp").write_bytes(b"previous candidate")
            record["reserved_bytes"] = sum(info.st_size for _, info in walk(directory))
            before = self.store.state_path.read_bytes()
            with self.assertRaisesRegex(StorageError, "profile reservation"):
                self.store.write_profile({**self.request, "reuse_id": identifier})
            self.assertFalse((directory / "profile/result.json").exists())
            self.assertEqual(before, self.store.state_path.read_bytes())

    def test_profile_wrong_owner_completed_and_legacy_refuse(self):
        with self.store.locked():
            identifier, directory, record = self.profile()
            for change in [dict(owner={"pid": 1}), dict(status="completed"), dict(retention={"retain_housekeeping": False})]:
                original = dict(record)
                record.update(change)
                with self.assertRaisesRegex(StorageError, "active reuse lease"):
                    self.store.write_profile({**self.request, "reuse_id": identifier})
                record.clear(); record.update(original)
            self.assertFalse((directory / "profile/result.json").exists())

    def test_profile_path_escape_refuses_before_writing(self):
        with self.store.locked():
            identifier, directory, record = self.profile()
            record["path"] = "profiles/../receipts"
            with self.assertRaisesRegex(StorageError, "exact path"):
                self.store.write_profile({**self.request, "reuse_id": identifier})
            self.assertFalse((directory / "profile/result.json").exists())

    def test_wire_binds_actual_parent_and_reports_identity(self):
        request = dict(project=str(self.project), policy=POLICY, owner_pid=os.getpid())
        command = [sys.executable, "-B", str(Path(r.__file__)), "new-run"]
        result = subprocess.run(command, input=json.dumps(request).encode("utf-8"), capture_output=True)
        self.assertEqual(0, result.returncode, result.stderr)
        context = json.loads(result.stdout)
        self.assertTrue(context["Retention"]["retain_housekeeping"])
        self.assertEqual(os.getpid(), context["Owner"]["pid"])
        result = subprocess.run(command, input=json.dumps({**request, "owner_pid": 1}).encode("utf-8"), capture_output=True)
        self.assertEqual(2, result.returncode)
        self.assertIn(b"actual parent owner", result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence-directory", required=True)
    args = parser.parse_args()
    if os.name != "nt": parser.error("Native Windows Python required")
    EVIDENCE = plain(args.evidence_directory)
    if not EVIDENCE.is_relative_to(CHECKOUT / ".claude"): parser.error("Canonical .claude evidence required")
    EVIDENCE.mkdir()
    unittest.main(argv=[__file__], verbosity=2)
