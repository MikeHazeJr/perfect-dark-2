"""Native Windows, tiny retained fixtures only. Never deletes its evidence tree."""
import argparse
import contextlib
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock
import uuid

spec = importlib.util.spec_from_file_location("retained_storage", Path(__file__).with_name("storage.py"))
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)
EVIDENCE = None
POLICY = dict(min_free_gib=0, max_full_installs=10, max_full_install_gib=0.01,
              max_archive_gib=0.01, peak_run_gib=0, extra_growth_gib=0, peak_build_gib=0)


def bytes_tree(root):
    # Windows' held byte-range lock intentionally forbids reading storage.lock.
    # It is protocol state, not captured install/receipt evidence.
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob("*") if p.is_file() and p != root / "storage.lock"}


class RetainedStorageTests(unittest.TestCase):
    def setUp(self):
        self.project = EVIDENCE / (self._testMethodName + "-" + uuid.uuid4().hex)
        self.project.mkdir()
        self.source = self.project / "inert-input.txt"
        self.source.write_bytes(b"inert storage fixture; never executed\n")
        self.definition = self.project / "definition-input.json"
        self.definition.write_text('{"fixture":"retention-only"}', encoding="utf-8")

    def storage(self, retained=True, policy=None):
        return s.Storage(self.project, policy or POLICY, {"retain_housekeeping": retained})

    def seed(self, st):
        return st.seed(dict(name="retention-only", owner_pid=os.getpid(),
                            sources=[[str(self.source), "PerfectDark.exe"]]))

    def request(self, install):
        return dict(id=install["StorageId"], owner_pid=os.getpid(),
                    definition=str(self.definition), passed=False, keep=False)

    def complete(self, st, install):
        def git(args, **kwargs):
            # Legacy setup never enters its temporary-patch deletion path.
            if not st.retain_housekeeping and args[1] == "diff":
                return subprocess.CompletedProcess(args, 1, b"", b"fixture diagnostic unavailable")
            return subprocess.CompletedProcess(args, 0,
                b"diagnostic source diff\n" if args[1] == "diff" else "fixture-head\n", b"")
        with mock.patch.object(s.subprocess, "run", side_effect=git):
            return st.complete(self.request(install))

    def test_invalid_options_create_no_storage(self):
        for value in [True, [], {"other": True}, {"retain_housekeeping": 1}, {"retain_housekeeping": "true"}]:
            with self.assertRaises(s.StorageError): s.Storage(self.project, POLICY, value)
        self.assertFalse((self.project / ".claude").exists())

    def test_persisted_null_options_cannot_silently_downgrade(self):
        st = self.storage(False)
        st.root.mkdir(parents=True)
        st.state["installs"]["corrupt"] = {"retention": None}
        st.state_path.write_text(json.dumps(st.state), encoding="utf-8")
        before = st.state_path.read_bytes()
        with self.assertRaisesRegex(s.StorageError, "Persisted retention options"):
            with st.locked(): self.fail("Invalid row admitted an operation")
        self.assertEqual(before, st.state_path.read_bytes())

    def test_count_refusal_precedes_capture_and_preserves_state(self):
        st = self.storage(policy={**POLICY, "max_full_installs": 1})
        with st.locked():
            self.seed(st)
            before = bytes_tree(st.root)
            with mock.patch.object(st, "put", side_effect=AssertionError("capture must not start")):
                with self.assertRaisesRegex(s.StorageError, "refuses eviction"):
                    self.seed(st)
            self.assertEqual(before, bytes_tree(st.root))

    def test_full_byte_floor_and_archive_refusals(self):
        st = self.storage()
        with st.locked():
            with self.assertRaisesRegex(s.StorageError, "refuses eviction"):
                st.make_room(st.policy["max_full_install_gib_bytes"] + 1)
            with mock.patch.object(s.shutil, "disk_usage", return_value=mock.Mock(free=0)):
                with self.assertRaisesRegex(s.StorageError, "Disk preflight"):
                    st.preflight(1)
            with self.assertRaisesRegex(s.StorageError, "Archive budget"):
                st.preflight(0, st.policy["max_archive_gib_bytes"] + 1)
            self.assertEqual({}, st.state["installs"])
            self.assertFalse(st.state_path.exists())

    def test_shared_request_refuses_before_capture(self):
        st = self.storage()
        with st.locked(), mock.patch.object(st, "put", side_effect=AssertionError("no capture")):
            with self.assertRaisesRegex(s.StorageError, "shared workspace reset"):
                st.seed(dict(name="refuse", shared=True, sources=[[str(self.source), "PerfectDark.exe"]]))
            self.assertFalse((st.root / "blobs").exists())

    def test_default_shared_reset_and_remove_protect_sticky_row(self):
        st = self.storage(False)
        with st.locked():
            row = dict(id="sticky", status="completed", pin=False, full_bytes=1,
                       path="shared/client", retention={"retain_housekeeping": True})
            st.state["installs"]["sticky"] = row
            with mock.patch.object(st, "eligible", side_effect=AssertionError("no deletion probe")):
                with self.assertRaisesRegex(s.StorageError, "install removal"): st.remove(row)
            with mock.patch.object(st, "put", side_effect=AssertionError("no capture")):
                with self.assertRaisesRegex(s.StorageError, "shared workspace reset"):
                    st.seed(dict(name="refuse", shared=True, sources=[]))

    def test_default_victim_selection_excludes_sticky_row(self):
        st = self.storage(False, {**POLICY, "max_full_installs": 1})
        with st.locked():
            st.state["installs"]["sticky"] = dict(id="sticky", status="completed", pin=False,
                full_bytes=1, created="1", path="installs/sticky", retention={"retain_housekeeping": True})
            with mock.patch.object(st, "remove", side_effect=AssertionError("no removal")):
                with self.assertRaisesRegex(s.StorageError, "active/pinned evidence"): st.make_room(1)

    def test_failed_blob_candidate_survives_and_is_charged(self):
        st = self.storage()
        real_open = s.gzip.open
        @contextlib.contextmanager
        def failing_open(path, mode, **kwargs):
            with real_open(path, mode, **kwargs) as stream:
                def fail(data):
                    stream.write(data[:3])
                    raise OSError("injected gzip failure")
                yield mock.Mock(write=fail)
        with st.locked(), mock.patch.object(s.gzip, "open", side_effect=failing_open):
            with self.assertRaisesRegex(OSError, "injected gzip failure"): st.put(self.source)
            candidates = list((st.root / "blobs").rglob("*.tmp"))
            self.assertEqual(1, len(candidates))
            self.assertIn(st.operation_id, candidates[0].name)
            self.assertGreater(candidates[0].stat().st_size, 0)
            self.assertEqual({}, st.state["blobs"])
            self.assertEqual(st.usage(), st.archive_used)

    def test_failed_receipt_candidate_survives_and_is_charged(self):
        st = self.storage()
        path = st.root / "receipts" / "fixture" / "receipt.json.gz"
        def fail(value, stream, **kwargs):
            stream.write("partial")
            raise OSError("injected receipt failure")
        with st.locked(), mock.patch.object(s.json, "dump", side_effect=fail):
            with self.assertRaisesRegex(OSError, "injected receipt failure"): st.write_receipt(path, {})
            self.assertFalse(path.exists())
            self.assertEqual(1, len(list(path.parent.glob("*.tmp"))))
            self.assertEqual(st.usage(), st.archive_used)

    def test_completion_collision_precedes_capture(self):
        st = self.storage()
        with st.locked():
            install = self.seed(st)
            path = Path(install["StorageReceiptDir"]) / "recipe.json.gz"
            path.write_bytes(b"prior failed receipt must survive")
            before = bytes_tree(st.root)
            with mock.patch.object(st, "capture", side_effect=AssertionError("no capture")):
                with self.assertRaisesRegex(s.StorageError, "receipt collision"):
                    self.complete(st, install)
            self.assertEqual(before, bytes_tree(st.root))

    def test_raw_diff_survives_completion_failure_and_is_charged(self):
        st = self.storage()
        with st.locked():
            install = self.seed(st)
            with mock.patch.object(st, "write_receipt", side_effect=OSError("injected completion failure")):
                with self.assertRaisesRegex(OSError, "injected completion failure"):
                    self.complete(st, install)
            patches = list(Path(install["StorageReceiptDir"]).glob("source-diff-*.patch"))
            self.assertEqual(1, len(patches))
            raw = patches[0].read_bytes()
            digest = hashlib.sha256(raw).hexdigest()
            with gzip.open(st.blob_path(digest), "rb") as f: self.assertEqual(raw, f.read())
            self.assertEqual(st.usage(), st.archive_used)
            self.assertEqual("active", st.state["installs"][install["StorageId"]]["status"])

    def test_default_completion_inherits_and_restore_inherits(self):
        st = self.storage()
        with st.locked(): install = self.seed(st)
        st = self.storage(False)
        with st.locked():
            result = self.complete(st, install)
            self.assertTrue(st.retain_housekeeping)
            recipe = s.read_gzip(result["Receipt"])
            self.assertTrue(recipe["retention"]["retain_housekeeping"])
            self.assertFalse(st.state["installs"][install["StorageId"]]["passed"])
            self.assertEqual(st.usage(), st.archive_used)
        st = self.storage(False)
        with st.locked():
            destination = st.root / "restored" / "retained-copy"
            st.restore(install["StorageId"], destination)
            row = next(x for x in st.state["installs"].values() if x.get("source_receipt"))
            self.assertTrue(row["retention"]["retain_housekeeping"])
            self.assertEqual(self.source.read_bytes(), (destination / "PerfectDark.exe").read_bytes())
            self.assertEqual(st.usage(), st.archive_used)

    def test_restore_requested_retention_is_or_not_downgraded(self):
        st = self.storage(False)
        with st.locked():
            install = self.seed(st)
            self.complete(st, install)
        st = self.storage(True)
        with st.locked():
            st.restore(install["StorageId"], st.root / "restored" / "requested-retained")
            row = next(x for x in st.state["installs"].values() if x.get("source_receipt"))
            self.assertTrue(row["retention"]["retain_housekeeping"])

    def test_late_completion_promotion_refused_before_capture(self):
        st = self.storage(False)
        with st.locked(): install = self.seed(st)
        st = self.storage(True)
        with st.locked(), mock.patch.object(st, "capture", side_effect=AssertionError("no capture")):
            before = bytes_tree(st.root)
            with self.assertRaisesRegex(s.StorageError, "Late retention promotion"):
                self.complete(st, install)
            self.assertEqual(before, bytes_tree(st.root))

    def test_prior_registry_bytes_preserved_and_legacy_candidate_refused(self):
        st = self.storage()
        with st.locked():
            self.seed(st)
            before = st.state_path.read_bytes()
            st.save()
            snapshots = list((st.root / "protocol-artifacts").rglob("state-before-*.json"))
            self.assertTrue(any(p.read_bytes() == before for p in snapshots))
            self.assertEqual(st.usage(), st.archive_used)
            legacy = st.state_path.with_suffix(".tmp")
            legacy.write_bytes(b"abandoned exact candidate")
            tree = bytes_tree(st.root)
            with self.assertRaisesRegex(s.StorageError, "Legacy registry candidate"): st.save()
            self.assertEqual(tree, bytes_tree(st.root))

    def test_registry_failed_candidate_is_retained_and_charged(self):
        st = self.storage()
        with st.locked():
            self.seed(st)
            before = st.state_path.read_bytes()
            with mock.patch.object(s.os, "replace", side_effect=OSError("injected atomic replacement failure")):
                with self.assertRaisesRegex(OSError, "atomic replacement failure"): st.save()
            self.assertEqual(before, st.state_path.read_bytes())
            self.assertEqual(1, len(list((st.root / "protocol-artifacts").rglob("*.tmp"))))
            self.assertEqual(st.usage(), st.archive_used)

    def test_receipt_publish_race_cannot_overwrite(self):
        st = self.storage()
        path = st.root / "receipts" / "fixture" / "receipt.json.gz"
        real_rename = s.os.rename
        def race(source, destination):
            Path(destination).write_bytes(b"racing evidence")
            return real_rename(source, destination)
        with st.locked(), mock.patch.object(s.os, "rename", side_effect=race):
            with self.assertRaises(OSError): st.write_receipt(path, {})
            self.assertEqual(b"racing evidence", path.read_bytes())
            self.assertEqual(1, len(list(path.parent.glob("*.tmp"))))
            self.assertEqual(st.usage(), st.archive_used)

    def test_actual_compressed_budget_refuses_promotion(self):
        st = self.storage(policy={**POLICY, "max_archive_gib": 10 / 2**30})
        with st.locked(), mock.patch.object(s, "gzip_bound", return_value=1):
            with self.assertRaisesRegex(s.StorageError, "Archive budget"): st.put(self.source)
            self.assertEqual({}, st.state["blobs"])
            self.assertEqual(1, len(list((st.root / "blobs").rglob("*.tmp"))))
            self.assertGreater(st.archive_used, 10)
            self.assertEqual(st.usage(), st.archive_used)

    def test_partial_checkout_is_preserved_and_charged(self):
        st = self.storage()
        with st.locked():
            entry = st.put(self.source)
            destination = st.root / "receipts" / "fixture" / "partial.log"
            real_open = s.gzip.open
            @contextlib.contextmanager
            def failing_read(path, mode):
                with real_open(path, mode) as stream:
                    yield mock.Mock(read=mock.Mock(side_effect=[stream.read(3), OSError("injected decode failure")]))
            with mock.patch.object(s.gzip, "open", side_effect=failing_read):
                with self.assertRaisesRegex(OSError, "decode failure"): st.checkout(entry, destination)
            self.assertEqual(self.source.read_bytes()[:3], destination.read_bytes())
            self.assertEqual(st.usage(), st.archive_used)

    def test_cli_refusal_has_effective_identity_and_no_storage_mutation(self):
        request = dict(project=str(self.project), policy=POLICY,
                       retention={"retain_housekeeping": True}, shared=True)
        result = subprocess.run([sys.executable, "-B", str(Path(__file__).with_name("storage.py")), "seed"],
            input=json.dumps(request).encode("utf-8"), capture_output=True)
        self.assertEqual(2, result.returncode)
        error = result.stderr.decode("utf-8")
        self.assertIn('"retain_housekeeping": true', error)
        self.assertIn('"operation_id":', error)
        self.assertFalse((self.project / ".claude").exists())

    def test_private_seed_directory_collision_preserves_prior_owner(self):
        st = self.storage()
        fixed = s.dt.datetime(2026, 1, 1, tzinfo=s.dt.timezone.utc)
        directory = st.root / "installs" / "20260101T000000000000Z-collision-aaaaaaaa"
        directory.mkdir(parents=True)
        marker = directory / ".pd-storage-owner.json"
        marker.write_bytes(b"prior exact owner marker")
        (directory / "prior.txt").write_bytes(b"prior evidence")
        before = bytes_tree(directory)
        with st.locked(), mock.patch.object(s.dt, "datetime", wraps=s.dt.datetime) as clock, \
                mock.patch.object(s.uuid, "uuid4", return_value=mock.Mock(hex="a" * 32)):
            clock.now.return_value = fixed
            with self.assertRaises(FileExistsError):
                st.seed(dict(name="collision", sources=[[str(self.source), "PerfectDark.exe"]]))
            self.assertEqual(before, bytes_tree(directory))
            self.assertEqual({}, st.state["installs"])
            self.assertFalse(st.state_path.exists())

    def test_late_log_collision_preserves_raced_file_and_failure(self):
        st = self.storage()
        with st.locked():
            install = self.seed(st)
            (Path(install["InstallDir"]) / "run.log").write_bytes(b"actual run log")
            destination = Path(install["StorageReceiptDir"]) / "logs" / "run.log"
            real_capture = st.capture
            def race(directory):
                files = real_capture(directory)
                destination.parent.mkdir(parents=True)
                destination.write_bytes(b"raced evidence must survive")
                return files
            with mock.patch.object(st, "capture", side_effect=race):
                with self.assertRaises(FileExistsError): self.complete(st, install)
            self.assertEqual(b"raced evidence must survive", destination.read_bytes())
            self.assertEqual("active", st.state["installs"][install["StorageId"]]["status"])
            self.assertEqual(st.usage(), st.archive_used)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence-directory", required=True)
    args = parser.parse_args()
    if os.name != "nt": parser.error("Native Windows Python required")
    EVIDENCE = s.plain(args.evidence_directory)
    if not EVIDENCE.is_relative_to(s.CHECKOUT / ".claude"):
        parser.error("Evidence must be inside canonical .claude")
    EVIDENCE.mkdir()  # Fresh destination only; all fixtures survive pass/failure.
    unittest.main(argv=[__file__], verbosity=2)
