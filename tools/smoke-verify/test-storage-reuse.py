"""Tiny retained fixtures for exclusive read-only profiles; no game or deletion."""
import concurrent.futures
import contextlib
import ctypes
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
import unittest
import uuid

spec = importlib.util.spec_from_file_location("storage", Path(__file__).with_name("storage.py"))
storage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(storage)
BASE = storage.CHECKOUT / ".claude/smoke-storage-selftests" / ("reuse-" + uuid.uuid4().hex)
POLICY = dict(schema=1, min_free_gib=0, max_full_installs=1, max_full_install_gib=65536 / 2**30,
              max_archive_gib=4 / 1024, peak_run_gib=1024 / 2**30, extra_growth_gib=0, peak_build_gib=0)


def fixture(project):
    """Write a few-byte complete failed receipt directly, without old pruning/reset paths."""
    s = storage.Storage(project, POLICY)
    base = s.root / "shared/client"
    base.mkdir(parents=True)
    for name, data in {"PerfectDark.exe": b"not executable", "pd.unit.z64": b"tiny fake rom",
                       "data/unit/native.pdmesh": b"editable source", "data/unit/.pdextract-cache": b"prior stamp",
                       "pd.ini": b"unique prior settings", "logs/prior.log": b"original failure"}.items():
        path = base / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    definition = b'{"name":"small-reuse-fixture"}'
    files, metadata = s.inspect(base)
    definition_entry = {"sha256": hashlib.sha256(definition).hexdigest(), "bytes": len(definition), "mtime_ns": 0}
    for name, entry in {**files, "definition": definition_entry}.items():
        blob = s.blob_path(entry["sha256"])
        blob.parent.mkdir(parents=True, exist_ok=True)
        with gzip.open(blob, "wb") as out: out.write(definition if name == "definition" else (base / name).read_bytes())
        s.state["blobs"][entry["sha256"]] = {"bytes": entry["bytes"], "stored_bytes": blob.stat().st_size}
    seed = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    storage.json_gzip(s.root / "seeds" / (seed + ".json.gz"), files)
    identifier = "completed-failure"
    (base / ".pd-storage-owner.json").write_text(json.dumps({"schema": 1, "id": identifier, "prospective": True}))
    record = dict(id=identifier, seed=seed, status="completed", pin=False, passed=False,
                  path="shared/client", created=storage.utc(), full_bytes=sum(x["bytes"] for x in files.values()))
    s.state["installs"][identifier] = record
    receipt = dict(schema=1, complete=True, id=identifier, seed=seed, changed={}, deleted=[], metadata=metadata,
                   definition=definition_entry, outcome={"passed": False, "assertions_met": 11, "assertions_total": 29})
    storage.json_gzip(s.root / "receipts" / identifier / "recipe.json.gz", receipt)
    (s.root / "receipts" / identifier / "prior-result.json").write_text(json.dumps(receipt["outcome"]))
    s.save()
    return dict(project=str(project), id=identifier, seed_id=seed, binary_sha256=files["PerfectDark.exe"]["sha256"], owner_pid=os.getpid())


class ReuseTests(unittest.TestCase):
    def setUp(self):
        self.project = BASE / self._testMethodName
        self.request = fixture(self.project)
        self.policy = dict(POLICY)
        with self.operation() as s:
            self.before = s.reuse_snapshot(s.state["installs"][self.request["id"]])

    @contextlib.contextmanager
    def operation(self):
        with storage.Storage(self.project, self.policy).locked() as s: yield s

    def acquire(self, **patch):
        with self.operation() as s: return s.acquire_reuse({**self.request, **patch})

    def close(self, run, **patch):
        with self.operation() as s:
            return s.release_reuse(dict(id=run["ReuseId"], owner_pid=os.getpid(), **patch))

    def test_failed_receipt_and_base_preserved_without_copy_or_delete(self):
        run = self.acquire()
        for name in ("ProfileDir", "LogDir", "CaptureDir"):
            path = Path(run[name])
            self.assertFalse(path.is_relative_to(Path(run["InstallDir"])))
            self.assertEqual(list(path.iterdir()), [])
        (Path(run["ProfileDir"]) / "pd.ini").write_text("fresh settings")
        (Path(run["LogDir"]) / "client.log").write_text("new evidence")
        (Path(run["CaptureDir"]) / "screen.png").write_bytes(b"tiny capture fixture")
        self.assertEqual(run["StorageCopiedBytes"], 0)
        self.assertFalse(run["PriorPassed"])
        result = self.close(run, outcome={"passed": True, "consumer": "read-only fixture"})
        self.assertFalse(result["DeletionPerformed"])
        self.assertTrue(Path(result["ProfileRetained"]).is_dir())
        with self.operation() as s:
            self.assertEqual(s.reuse_snapshot(s.state["installs"][self.request["id"]]), self.before)
            self.assertEqual(len(s.state["installs"]), 1)  # Full-copy cap stays exactly one.
            self.assertFalse(s.state["installs"][self.request["id"]]["passed"])
            self.assertFalse(s.reuse_held(self.request["id"]))

    def test_concurrent_acquisition_has_exactly_one_owner(self):
        barrier = threading.Barrier(2)
        def attempt():
            barrier.wait()
            try: return self.acquire()
            except storage.StorageError as error: return str(error)
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            result = list(pool.map(lambda _: attempt(), range(2)))
        runs = [x for x in result if isinstance(x, dict)]
        self.assertEqual(len(runs), 1)
        self.assertIn("exclusive reuse lease", next(x for x in result if isinstance(x, str)))
        self.close(runs[0])

    def test_storage_reset_and_retention_refuse_leased_base(self):
        run = self.acquire()
        with self.operation() as s:
            install = s.state["installs"][self.request["id"]]
            self.assertFalse(s.eligible(install, allow_pin=True))
            with self.assertRaises(storage.StorageError): s.remove(install)
            with self.assertRaises(storage.StorageError): s.seed(dict(name="other", target="pd", seed_id=install["seed"], shared=True))
        self.close(run)

    def test_changed_base_leaves_lease_held_and_all_evidence_retained(self):
        run = self.acquire()
        (Path(run["InstallDir"]) / "data/unit/.pdextract-cache").write_bytes(b"unexpected changed stamp")
        with self.assertRaises(storage.StorageError): self.close(run)
        with self.operation() as s:
            self.assertTrue(s.reuse_held(self.request["id"]))
            self.assertEqual(s.state["reuse_runs"][run["ReuseId"]]["status"], "integrity_failed")
            self.assertFalse(s.state["installs"][self.request["id"]]["passed"])
        self.assertTrue(Path(run["ProfileDir"]).exists())

    def test_empty_directory_mutation_is_detected(self):
        run = self.acquire()
        (Path(run["InstallDir"]) / "unexpected-empty").mkdir()
        with self.assertRaisesRegex(storage.StorageError, "changed"): self.close(run)

    def test_prior_failure_receipt_mutation_is_detected(self):
        run = self.acquire()
        result = self.project / ".claude/smoke-storage/receipts/completed-failure/prior-result.json"
        result.write_text('{"passed":true}')
        with self.assertRaisesRegex(storage.StorageError, "changed"): self.close(run)
        self.assertTrue(result.exists())

    def test_hash_seed_pin_and_incomplete_refusals_create_no_profile(self):
        for patch in ({"binary_sha256": "0" * 64}, {"seed_id": "0" * 64}):
            with self.assertRaises(storage.StorageError): self.acquire(**patch)
        with self.operation() as s:
            row = s.state["installs"][self.request["id"]]
            row["pin"] = True; s.save()
        with self.assertRaises(storage.StorageError): self.acquire()
        with self.operation() as s:
            row = s.state["installs"][self.request["id"]]
            row.update(pin=False, status="active"); s.save()
        with self.assertRaises(storage.StorageError): self.acquire()
        self.assertFalse((self.project / ".claude/smoke-storage/profiles").exists())

    def test_corrupt_seed_blob_and_owner_marker_refuse(self):
        with self.operation() as s:
            files = storage.read_gzip(s.root / "seeds" / (self.request["seed_id"] + ".json.gz"))
            blob = s.blob_path(files["PerfectDark.exe"]["sha256"])
            with gzip.open(blob, "wb") as out: out.write(b"bad blob")
        with self.assertRaisesRegex(storage.StorageError, "Corrupt"): self.acquire()
        marker = self.project / ".claude/smoke-storage/shared/client/.pd-storage-owner.json"
        marker.write_text('{"schema":1,"id":"another","prospective":true}')
        with self.assertRaisesRegex(storage.StorageError, "marker"): self.acquire()

    def test_owner_creation_identity_mismatch_cannot_close(self):
        run = self.acquire()
        with self.operation() as s:
            s.state["reuse_runs"][run["ReuseId"]]["owner"]["creation_ticks"] += 1; s.save()
        with self.assertRaisesRegex(storage.StorageError, "identity mismatch"): self.close(run)
        with self.operation() as s: self.assertTrue(s.reuse_held(self.request["id"]))

    def test_profile_path_escape_refuses_without_touching_outside(self):
        run = self.acquire()
        outside = self.project / "outside"
        outside.mkdir(); (outside / "unique.txt").write_text("retain me")
        with self.operation() as s:
            s.state["reuse_runs"][run["ReuseId"]]["path"] = "../../outside"; s.save()
        with self.assertRaises(storage.StorageError): self.close(run)
        self.assertEqual((outside / "unique.txt").read_text(), "retain me")

    def test_hardlink_base_is_rejected(self):
        source = self.project / ".claude/smoke-storage/shared/client/PerfectDark.exe"
        os.link(source, self.project / "retained-hardlink")
        with self.assertRaisesRegex(storage.StorageError, "Hardlinked"): self.acquire()

    def test_in_use_base_refuses_without_a_profile(self):
        from ctypes import wintypes as w
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, ctypes.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
        kernel.CreateFileW.restype = w.HANDLE
        kernel.CloseHandle.argtypes = [w.HANDLE]
        path = self.project / ".claude/smoke-storage/shared/client/logs/prior.log"
        handle = kernel.CreateFileW(str(path), 0x80000000, 0, None, 3, 0, None)
        self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
        try:
            with self.assertRaises(OSError): self.acquire()
            self.assertFalse((self.project / ".claude/smoke-storage/profiles").exists())
        finally:
            kernel.CloseHandle(handle)

    def test_native_utf8_json_transport_accepts_bom_and_unicode(self):
        unicode_request = fixture(self.project / "Unicode \u03a9 path with spaces")
        for request in (self.request, unicode_request):
            payload = json.dumps({**request, "policy": self.policy}, ensure_ascii=False).encode("utf-8")
            for prefix in (b"", b"\xef\xbb\xbf"):
                with self.subTest(project=request["project"], bom=bool(prefix)):
                    result = subprocess.run([sys.executable, "-B", str(Path(__file__).with_name("storage.py")), "reuse-plan"],
                                            input=prefix + payload, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", errors="replace"))
                    self.assertFalse(json.loads(result.stdout)["RuntimeAdmitted"])

    def test_manifest_tamper_and_timestamp_only_mutation_refuse(self):
        source = self.project / ".claude/smoke-storage/shared/client/PerfectDark.exe"
        info = source.stat()
        os.utime(source, ns=(info.st_atime_ns, info.st_mtime_ns + 1000000000))
        with self.assertRaisesRegex(storage.StorageError, "byte/identity"): self.acquire()
        os.utime(source, ns=(info.st_atime_ns, info.st_mtime_ns))
        with self.operation() as s:
            path = s.root / "seeds" / (self.request["seed_id"] + ".json.gz")
            manifest = storage.read_gzip(path)
            manifest["PerfectDark.exe"]["bytes"] += 1
            storage.json_gzip(path, manifest)
        with self.assertRaisesRegex(storage.StorageError, "identity mismatch"): self.acquire()

    def test_profile_marker_and_admission_tamper_keep_lease_held(self):
        run = self.acquire()
        marker_path = Path(run["ProfileDir"]).parent / ".pd-reuse-owner.json"
        marker = marker_path.read_bytes()
        marker_path.write_text('{"schema":1,"id":"another"}')
        with self.assertRaisesRegex(storage.StorageError, "marker mismatch"): self.close(run)
        marker_path.write_bytes(marker)
        admission_path = Path(run["ReuseReceiptDir"]) / "admission.json.gz"
        admission = storage.read_gzip(admission_path)
        admission["snapshot"]["record"]["passed"] = True
        storage.json_gzip(admission_path, admission)
        with self.assertRaisesRegex(storage.StorageError, "snapshot hash mismatch"): self.close(run)
        with self.operation() as s: self.assertTrue(s.reuse_held(self.request["id"]))

    def test_closed_profile_bytes_remain_budgeted(self):
        run = self.acquire()
        (Path(run["CaptureDir"]) / "fixture.bin").write_bytes(b"x" * 4096)
        self.close(run)
        with self.operation() as s:
            self.assertGreater(s.profile_bytes(), 4096)
            full_bytes = s.state["installs"][self.request["id"]]["full_bytes"]
        self.policy["max_full_install_gib"] = (full_bytes + 4096 + 1024) / 2**30
        with self.assertRaisesRegex(storage.StorageError, "byte budget"): self.acquire()

    def test_existing_count_byte_and_disk_budgets_never_relaxed(self):
        self.policy["max_full_install_gib"] = 1 / 2**30
        with self.assertRaisesRegex(storage.StorageError, "byte budget"): self.acquire()
        self.policy = {**POLICY, "min_free_gib": 2**40}
        with self.assertRaisesRegex(storage.StorageError, "Disk preflight"): self.acquire()
        self.policy = dict(POLICY)
        with self.operation() as s:
            s.state["installs"]["other-retained"] = {**s.state["installs"][self.request["id"]], "id": "other-retained", "path": "installs/other"}; s.save()
        with self.assertRaisesRegex(storage.StorageError, "count exceeds"): self.acquire()


if __name__ == "__main__":
    import sys
    if "--fixture" in sys.argv:
        print(json.dumps(fixture(BASE / "PowerShell \u03a9 path with spaces")))
    else:
        began = time.monotonic()
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ReuseTests))
        report = dict(passed=result.wasSuccessful(), tests=result.testsRun, failures=len(result.failures), errors=len(result.errors),
                      seconds=round(time.monotonic() - began, 3), fixture_root=str(BASE),
                      game_launched=False, full_game_copied=False, deletion_performed=False,
                      fixture_bytes=sum(p.stat().st_size for p in BASE.rglob("*") if p.is_file()))
        directory = storage.CHECKOUT / ".claude/smoke-storage-validation" / BASE.name
        directory.mkdir(parents=True)
        (directory / "results.json").write_text(json.dumps(report, indent=2))
        print(json.dumps(report))
        sys.exit(0 if result.wasSuccessful() else 1)
