"""Small isolated storage gate; never launches the game or touches historical output."""
import contextlib
import ctypes
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import unittest
import uuid

spec = importlib.util.spec_from_file_location('smoke_storage', Path(__file__).with_name('storage.py'))
storage = importlib.util.module_from_spec(spec); spec.loader.exec_module(storage)
BASE = storage.CHECKOUT / '.claude/smoke-storage-selftests' / uuid.uuid4().hex
MEASUREMENTS = {}
POLICY = dict(schema=1, min_free_gib=0, max_full_installs=3,
              max_full_install_gib=65536 / 2**30, max_archive_gib=4 / 1024,
              peak_run_gib=0, extra_growth_gib=0, peak_build_gib=0)

class StorageTests(unittest.TestCase):
    def setUp(self):
        self.project = BASE / self._testMethodName
        self.source = self.project / 'Build'; self.source.mkdir(parents=True)
        (self.source / 'PerfectDark.exe').write_bytes(b'fake-not-executable\0' * 128)
        (self.source / 'pd.unit.z64').write_bytes(b'fake-rom\0' * 128)
        self.data = self.project / '.claude/smoke-verify-cache/unit'; self.data.mkdir(parents=True)
        (self.data / 'editable.pdmesh').write_bytes(b'native-source\0' * 512)
        self.definition = self.project / 'definition.json'; self.definition.write_text('{"name":"storage-unit"}')
        self.historical = self.project / '.claude/smoke-verify-runs/historical/logs/old.log'
        self.historical.parent.mkdir(parents=True); self.historical.write_bytes(b'retain historical evidence')
        self.policy = dict(POLICY)

    @contextlib.contextmanager
    def operation(self):
        with storage.Storage(self.project, self.policy).locked() as s: yield s

    def seed(self, shared=False, owner_pid=None):
        with self.operation() as s:
            return s.seed(dict(name='tiny-test', target='pd', sources=[
                [str(self.source / 'PerfectDark.exe'), 'PerfectDark.exe'],
                [str(self.source / 'pd.unit.z64'), 'pd.unit.z64']],
                data_source=str(self.data), rom_id='unit', shared=shared,
                owner_pid=os.getpid() if owner_pid is None else owner_pid))

    def release(self, run, reviewer='unit-reviewer', reason='interrupted unit run'):
        with self.operation() as s:
            return s.release_interrupted(dict(id=run['StorageId'], reviewer=reviewer, reason=reason))

    @staticmethod
    def exited_pid():
        child = subprocess.Popen([os.sys.executable, '-c', 'pass'])
        child.wait()
        return child.pid

    def complete(self, run, passed=True, keep=False):
        with self.operation() as s:
            return s.complete(dict(id=run['StorageId'], owner_pid=os.getpid(),
                                   definition=str(self.definition), passed=passed, keep=keep,
                                   outcome={'passed':passed,'assertions_met':1}))

    def test_mutation_isolation_and_exact_restore(self):
        a, b = self.seed(), self.seed()
        original = (self.data / 'editable.pdmesh').read_bytes()
        path = Path(a['InstallDir']) / 'data/unit/editable.pdmesh'; before = path.stat()
        modified = b'X' + original[1:]; path.write_bytes(modified)
        os.utime(path, ns=(before.st_atime_ns, before.st_mtime_ns))
        (Path(a['InstallDir']) / 'logs').mkdir()
        (Path(a['InstallDir']) / 'logs/pd-client.log').write_text('unique failure\n')
        (Path(a['InstallDir']) / 'save.sav').write_bytes(b'unique save')
        self.assertEqual((Path(b['InstallDir']) / 'data/unit/editable.pdmesh').read_bytes(), original)
        self.assertEqual((self.data / 'editable.pdmesh').read_bytes(), original)
        self.assertEqual(path.stat().st_nlink, 1)
        receipt = self.complete(a, passed=False)
        with self.operation() as s:
            destination = s.root / 'restored/exact'
            s.restore(a['StorageId'], destination)
        self.assertEqual((destination / 'data/unit/editable.pdmesh').read_bytes(), modified)
        self.assertEqual((destination / 'data/unit/editable.pdmesh').stat().st_mtime_ns, before.st_mtime_ns)
        self.assertEqual((destination / 'save.sav').read_bytes(), b'unique save')
        self.assertEqual(Path(receipt['LogFiles'][0]).read_text(), 'unique failure\n')
        with gzip.open(receipt['Receipt'], 'rt') as f: recipe=json.load(f)
        self.assertIn('data/unit/editable.pdmesh', recipe['changed'])
        MEASUREMENTS['modified_receipt_bytes'] = Path(receipt['Receipt']).stat().st_size
        MEASUREMENTS['private_seed_bytes'] = a['StorageCopiedBytes']

    def test_shared_reset_and_reuse(self):
        a = self.seed(shared=True); directory = Path(a['InstallDir'])
        (directory / 'data/unit/editable.pdmesh').write_bytes(b'mutated')
        (directory / 'saves/new').mkdir(parents=True); (directory / 'saves/new/unique.sav').write_bytes(b'test-only')
        self.complete(a)
        b = self.seed(shared=True)
        self.assertEqual(a['InstallDir'], b['InstallDir'])
        self.assertEqual((directory / 'data/unit/editable.pdmesh').read_bytes(), (self.data / 'editable.pdmesh').read_bytes())
        self.assertFalse((directory / 'saves').exists())
        self.assertGreater(b['StorageReusedBytes'], 0)
        self.assertLess(b['StorageCopiedBytes'], a['StorageCopiedBytes'])
        MEASUREMENTS['shared_reset_copied_bytes'] = b['StorageCopiedBytes']
        MEASUREMENTS['shared_reset_reused_bytes'] = b['StorageReusedBytes']

    def test_shared_explicit_binary_bytes_refresh_despite_older_timestamp(self):
        first = self.seed(shared=True)
        source = self.source / 'PerfectDark.exe'
        original = source.read_bytes()
        original_time = source.stat().st_mtime_ns
        self.complete(first)
        replacement = b'X' + original[1:]
        source.write_bytes(replacement)
        older_time = original_time - 10_000_000_000
        os.utime(source, ns=(older_time, older_time))
        second = self.seed(shared=True)
        self.assertEqual(second['InstallDir'], first['InstallDir'])
        self.assertEqual((Path(second['InstallDir']) / 'PerfectDark.exe').read_bytes(), replacement)
        self.assertNotEqual(second['StorageSeed'], first['StorageSeed'])
        self.assertEqual(source.read_bytes(), replacement)
        self.complete(second)
        frozen = self.frozen(first['StorageSeed'])
        self.assertEqual((Path(frozen['InstallDir']) / 'PerfectDark.exe').read_bytes(), original)

    def frozen(self, seed_id, shared=False, **extra):
        with self.operation() as s:
            return s.seed(dict(name='frozen-test', target='pd', seed_id=seed_id,
                               shared=shared, owner_pid=os.getpid(), **extra))

    def test_frozen_seed_preserves_exact_snapshot_while_default_sees_edits(self):
        a = self.seed()
        original = (self.data / 'editable.pdmesh').read_bytes()
        original_binary = (self.source / 'PerfectDark.exe').read_bytes()
        (self.data / 'editable.pdmesh').write_bytes(b'edited public source')
        (self.source / 'PerfectDark.exe').write_bytes(b'new binary')
        b = self.frozen(a['StorageSeed'])
        self.assertEqual((Path(b['InstallDir']) / 'data/unit/editable.pdmesh').read_bytes(), original)
        self.assertEqual((Path(b['InstallDir']) / 'PerfectDark.exe').read_bytes(), original_binary)
        self.assertEqual(b['StorageSeed'], a['StorageSeed'])
        self.assertEqual(b['StorageNewArchiveBytes'], 0)
        self.assertEqual((Path(b['InstallDir']) / 'PerfectDark.exe').stat().st_nlink, 1)
        c = self.seed()
        self.assertEqual((Path(c['InstallDir']) / 'data/unit/editable.pdmesh').read_bytes(), b'edited public source')
        self.assertNotEqual(c['StorageSeed'], a['StorageSeed'])
        self.assertGreaterEqual(b['StorageSeedPhases']['total_seconds'],
                                b['StorageSeedPhases']['source_snapshot_seconds'])
        MEASUREMENTS['frozen_seed_phases'] = b['StorageSeedPhases']

    def test_frozen_seed_reuses_success_but_preserves_failed_full_workspace(self):
        a = self.seed(shared=True); self.complete(a)
        b = self.frozen(a['StorageSeed'], shared=True)
        self.assertEqual(b['StorageCopiedBytes'], 0)
        self.assertGreater(b['StorageReusedBytes'], 0)
        (Path(b['InstallDir']) / 'failure-proof.log').write_text('retain exact failure')
        self.complete(b, passed=False, keep=True)
        c = self.frozen(a['StorageSeed'], shared=True)
        self.assertEqual(c['StorageReusedBytes'], 0)
        with self.operation() as s:
            previous = s.state['installs'][b['StorageId']]
            self.assertTrue(previous['pin'])
            self.assertEqual((s.install_path(previous) / 'failure-proof.log').read_text(), 'retain exact failure')

    def test_frozen_seed_tampered_manifest_and_ambiguous_inputs_fail_closed(self):
        a = self.seed()
        with self.assertRaises(storage.StorageError): self.frozen('../escape')
        with self.assertRaises(storage.StorageError): self.frozen(a['StorageSeed'], sources=[['x', 'x']])
        with self.operation() as s:
            path = s.root / 'seeds' / (a['StorageSeed'] + '.json.gz')
            files = storage.read_gzip(path)
            files['PerfectDark.exe']['bytes'] += 1
            with gzip.open(path, 'wt') as f: json.dump(files, f)
            before = len(s.state['installs'])
            with self.assertRaisesRegex(storage.StorageError, 'identity mismatch'):
                s.seed(dict(name='bad', target='pd', seed_id=a['StorageSeed']))
            self.assertEqual(len(s.state['installs']), before)

    def test_frozen_seed_corrupt_blob_is_rejected_before_workspace_creation(self):
        a = self.seed()
        with self.operation() as s:
            files = storage.read_gzip(s.root / 'seeds' / (a['StorageSeed'] + '.json.gz'))
            digest = files['PerfectDark.exe']['sha256']
            with gzip.open(s.blob_path(digest), 'wb') as f: f.write(b'corrupt')
        with self.operation() as s:
            before = len(s.state['installs'])
            with self.assertRaisesRegex(storage.StorageError, 'Corrupt immutable archive blob'):
                s.seed(dict(name='bad', target='pd', seed_id=a['StorageSeed']))
            self.assertEqual(len(s.state['installs']), before)

    def test_frozen_seed_valid_hash_does_not_authorize_bad_paths_sizes_or_target(self):
        a = self.seed()
        with self.operation() as s:
            original = storage.read_gzip(s.root / 'seeds' / (a['StorageSeed'] + '.json.gz'))
            for defect in ('path', 'bytes', 'mtime', 'target'):
                with self.subTest(defect=defect):
                    files = json.loads(json.dumps(original))
                    if defect == 'path': files['../escape'] = files.pop('PerfectDark.exe')
                    elif defect == 'bytes': files['PerfectDark.exe']['bytes'] += 1
                    elif defect == 'mtime': files['PerfectDark.exe']['mtime_ns'] = False
                    else: files['other.exe'] = files.pop('PerfectDark.exe')
                    identity = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
                    storage.json_gzip(s.root / 'seeds' / (identity + '.json.gz'), files)
                    before = len(s.state['installs'])
                    with self.assertRaises(storage.StorageError):
                        s.seed(dict(name='bad', target='pd', seed_id=identity))
                    self.assertEqual(len(s.state['installs']), before)

    def test_failed_and_pinned_shared_evidence_retained(self):
        a=self.seed(shared=True); directory=Path(a['InstallDir'])
        (directory / 'unique.bin').write_bytes(b'unique binary')
        self.complete(a, passed=False, keep=True)
        b=self.seed(shared=True)
        with self.operation() as s:
            row=s.state['installs'][a['StorageId']]
            self.assertTrue(row['pin']); self.assertEqual((s.install_path(row)/'unique.bin').read_bytes(), b'unique binary')
        self.assertFalse((Path(b['InstallDir'])/'unique.bin').exists())

    def test_bounded_retention_and_compacted_restore(self):
        runs=[]
        for i in range(5):
            run=self.seed(); self.complete(run, passed=i%2==0); runs.append(run)
        with self.operation() as s:
            full=[x for x in s.state['installs'].values() if x['status']!='compacted']
            self.assertEqual(len(full),3)
            compact=next(x for x in s.state['installs'].values() if x['status']=='compacted')
            s.restore(compact['id'],s.root/'restored/compacted')
            self.assertEqual((s.root/'restored/compacted/PerfectDark.exe').read_bytes(),(self.source/'PerfectDark.exe').read_bytes())
            MEASUREMENTS['retained_full_install_count']=len(full)
            MEASUREMENTS['archive_bytes_after_five_runs']=s.usage()
            MEASUREMENTS['distinct_asset_blobs']=len(s.state['blobs'])
        self.assertEqual(self.historical.read_bytes(),b'retain historical evidence')

    def test_active_and_pinned_limit_refuses_new_copy(self):
        self.policy['max_full_installs']=1
        a=self.seed()
        with self.assertRaises(storage.StorageError): self.seed()
        self.complete(a, keep=True)
        with self.assertRaises(storage.StorageError): self.seed()
        self.assertTrue(Path(a['InstallDir']).exists())

    def test_byte_budget_below_the_run_reserve_says_so(self):
        # B-1173: CORE's C: location set max_full_install_gib 1.5 with the
        # checked-in peak_run_gib 2, so every seed was refused as "blocked by
        # active/pinned evidence" in an empty storage. The refusal now names
        # the budget, before anything is written.
        self.policy['peak_run_gib'] = 2 / 1024
        self.policy['max_full_install_gib'] = 1 / 1024
        with self.assertRaises(storage.StorageError) as refused:
            self.seed()
        self.assertIn('cannot fit one install', str(refused.exception))
        with self.operation() as s:
            self.assertEqual(s.state['installs'], {})
        self.policy['max_full_install_gib'] = 3 / 1024
        self.assertTrue(Path(self.seed()['InstallDir']).exists())

    def test_discard_completed_deletes_each_install_and_keeps_its_logs(self):
        # Mike, 2026-11-15, test installs on C:: "delete each one ... as soon
        # as it's no longer needed". With discard_completed the full copy goes
        # as soon as the run is recorded; its receipts and logs stay, and the
        # install can still be rebuilt from them. A kept run stays.
        self.policy['discard_completed'] = True
        run = self.seed(); directory = Path(run['InstallDir'])
        (directory / 'logs').mkdir(); (directory / 'logs/pd-client.log').write_text('run log\n')
        receipt = self.complete(run)
        self.assertFalse(directory.exists())
        self.assertFalse(receipt['FullInstallRetained'])
        self.assertGreater(receipt['DiscardedBytes'], 0)
        self.assertEqual(Path(receipt['LogFiles'][0]).read_text(), 'run log\n')
        with self.operation() as s:
            row = s.state['installs'][run['StorageId']]
            self.assertEqual(row['status'], 'compacted'); self.assertEqual(row['full_bytes'], 0)
            s.restore(run['StorageId'], s.root / 'restored/discarded')
            self.assertEqual((s.root / 'restored/discarded/PerfectDark.exe').read_bytes(),
                             (self.source / 'PerfectDark.exe').read_bytes())
        kept = self.seed(); self.complete(kept, keep=True)
        self.assertTrue(Path(kept['InstallDir']).exists())
        with self.assertRaises(storage.StorageError):
            storage.policy_from(dict(POLICY, discard_completed='yes'))

    def test_compact_deletes_named_completed_installs_now(self):
        a, b = self.seed(), self.seed()
        self.complete(a); self.complete(b, keep=True)
        active = self.seed()
        with self.operation() as s:
            with self.assertRaises(storage.StorageError): s.compact({'ids': [active['StorageId']]})
            with self.assertRaises(storage.StorageError): s.compact({'ids': [b['StorageId']]})
            with self.assertRaises(storage.StorageError): s.compact({'ids': []})
            result = s.compact({'ids': [a['StorageId']]})
        self.assertEqual(result['Compacted'], [a['StorageId']])
        self.assertGreater(result['FreedBytes'], 0)
        self.assertFalse(Path(a['InstallDir']).exists())
        self.assertTrue(Path(b['InstallDir']).exists())
        with self.operation() as s:
            result = s.compact({'ids': [b['StorageId'], a['StorageId']], 'unpin': True})
        self.assertEqual(result['Compacted'], [b['StorageId']])
        self.assertFalse(Path(b['InstallDir']).exists())
        self.assertTrue(Path(active['InstallDir']).exists())

    def test_reviewed_release_frees_an_interrupted_workspace(self):
        # Decision 7A: an interrupted run's workspace is released after review,
        # recorded, and then reclaimed by ordinary retention; the release
        # itself deletes nothing.
        self.policy['max_full_installs'] = 1
        a = self.seed(owner_pid=self.exited_pid())
        with self.assertRaises(storage.StorageError): self.seed()
        with self.assertRaises(storage.StorageError): self.release(a, reviewer=' ')
        with self.assertRaises(storage.StorageError): self.release(a, reason='')
        result = self.release(a)
        self.assertFalse(result['DeletionPerformed'])
        self.assertTrue(Path(a['InstallDir']).exists())
        self.assertTrue((Path(a['StorageReceiptDir']) / 'release.json.gz').exists())
        with self.operation() as s:
            row = s.state['installs'][a['StorageId']]
            self.assertEqual((row['status'], row['passed'], row['pin']), ('completed', False, False))
            self.assertEqual(row['released']['by'], 'unit-reviewer')
            self.assertIn(row['released']['owner']['state'], ('exited', 'no process has this id'))
        with self.assertRaises(storage.StorageError): self.release(a)
        b = self.seed()
        self.assertFalse(Path(a['InstallDir']).exists())
        self.complete(b)

    def test_reviewed_release_refuses_a_live_owner_or_produced_files(self):
        live = self.seed()
        with self.assertRaises(storage.StorageError): self.release(live)
        produced = self.seed(owner_pid=self.exited_pid())
        logs = Path(produced['InstallDir']) / 'logs'; logs.mkdir()
        (logs / 'run.log').write_text('written by a run')
        with self.assertRaises(storage.StorageError): self.release(produced)
        with self.operation() as s:
            self.assertEqual(s.state['installs'][produced['StorageId']]['status'], 'active')
            self.assertEqual(s.state['installs'][live['StorageId']]['status'], 'active')
        self.assertTrue((logs / 'run.log').exists())
        self.complete(live)

    def test_storage_root_follows_the_location_file(self):
        # Decision 6B: a checkout may keep its smoke storage on another drive.
        default = storage.storage_root(self.project)
        self.assertEqual(default, storage.plain(self.project / '.claude/smoke-storage'))
        location = self.project / '.claude/smoke-storage-location.json'
        moved = self.project / 'ssd-root' / 'core'
        location.write_text(json.dumps({'schema': 1, 'root': str(moved)}), encoding='utf-8')
        self.assertEqual(storage.storage_root(self.project), storage.plain(moved))
        run = self.seed()
        self.assertTrue(Path(run['InstallDir']).is_relative_to(moved / 'installs'))
        self.assertTrue((moved / 'state.json').exists())
        self.assertFalse((self.project / '.claude/smoke-storage/state.json').exists())
        self.complete(run)
        for bad in ({'schema': 2, 'root': str(moved)}, {'schema': 1, 'root': 'relative/root'},
                    {'schema': 1, 'root': Path(moved).anchor}, {'schema': 1},
                    {'schema': 1, 'root': str(moved), 'extra': 1},
                    {'schema': 1, 'root': str(moved), 'policy': {'peak_run_gib': 1}},
                    {'schema': 1, 'root': str(moved), 'policy': 'small'}):
            location.write_text(json.dumps(bad), encoding='utf-8')
            with self.assertRaises(storage.StorageError): storage.storage_root(self.project)

    def test_location_limits_apply_to_its_root_and_builds_measure_the_checkout(self):
        # Mike, 2026-11-15: installs on C: "only one or two at a time". The
        # location file's limits govern its root whatever policy the caller
        # sends (the scripts always send the whole checked-in file: B-1171);
        # a limit it leaves unset follows the caller; a build is measured on
        # the checkout's own drive and floor.
        location = self.project / '.claude/smoke-storage-location.json'
        moved = self.project / 'ssd-root' / 'core'
        location.write_text(json.dumps({'schema': 1, 'root': str(moved),
            'policy': {'max_full_installs': 2, 'min_free_gib': 200}}), encoding='utf-8')
        plain = storage.Storage(self.project, {'schema': 1})
        self.assertEqual(plain.policy['max_full_installs'], 2)
        self.assertEqual(plain.policy['min_free_gib'], 200)
        defaults = json.loads((Path(storage.__file__).parent / 'storage-policy.json').read_text())
        self.assertEqual(plain.project_policy['min_free_gib'], defaults['min_free_gib'])
        self.assertEqual(plain.project_policy['max_full_installs'], defaults['max_full_installs'])
        sent = storage.Storage(self.project, dict(defaults, max_full_installs=3, max_archive_gib=3))
        self.assertEqual(sent.policy['max_full_installs'], 2)
        self.assertEqual(sent.policy['min_free_gib'], 200)
        self.assertEqual(sent.policy['max_archive_gib'], 3)
        self.assertEqual(sent.project_policy['max_full_installs'], 3)

        real = storage.shutil.disk_usage
        gib = 2**30
        def fake(path):
            free = 0 if Path(path).resolve().is_relative_to(moved.resolve()) else 100 * gib
            return real(self.project)._replace(free=free)
        storage.shutil.disk_usage = fake
        try:
            with self.operation() as s:
                with self.assertRaises(storage.StorageError): s.preflight(gib)
                result = s.preflight(gib, 0, True)
                self.assertEqual(result['free_bytes'], 100 * gib)
        finally:
            storage.shutil.disk_usage = real

    def test_reviewed_release_writes_a_missing_owner_marker(self):
        a = self.seed(owner_pid=self.exited_pid())
        marker = Path(a['InstallDir']) / '.pd-storage-owner.json'
        marker.unlink()
        self.release(a)
        self.assertEqual(json.loads(marker.read_text())['id'], a['StorageId'])
        with self.operation() as s:
            row = s.state['installs'][a['StorageId']]
            self.assertTrue(row['released']['owner_marker'].startswith('written by the release'))
            self.assertTrue(s.eligible(row))

    def test_archive_budget_and_disk_floor(self):
        self.policy['max_archive_gib']=1/2**30
        with self.assertRaises(storage.StorageError): self.seed()
        self.assertFalse((self.project/'.claude/smoke-storage/installs').exists())
        self.policy=dict(POLICY); self.policy['min_free_gib']=2**30
        with self.assertRaises(storage.StorageError): self.seed()

    def test_byte_budget_refuses_large_install(self):
        self.policy['max_full_install_gib']=100/2**30
        with self.assertRaises(storage.StorageError): self.seed()
        self.assertFalse((self.project/'.claude/smoke-storage/installs').exists())

    def test_changed_completed_fixture_blocks_pruning(self):
        self.policy['max_full_installs']=1
        run=self.seed(); self.complete(run)
        p=Path(run['InstallDir'])/'PerfectDark.exe'; original=p.stat()
        p.write_bytes(b'Y'+p.read_bytes()[1:]); os.utime(p,ns=(original.st_atime_ns,original.st_mtime_ns))
        with self.assertRaises(storage.StorageError): self.seed()
        self.assertEqual(p.read_bytes()[0],ord('Y'))

    def test_locked_workspace_blocks_pruning(self):
        if os.name!='nt': self.skipTest('Windows exclusive-handle gate')
        self.policy['max_full_installs']=1
        run=self.seed(); self.complete(run)
        kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        kernel.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p]
        kernel.CreateFileW.restype=ctypes.c_void_p; kernel.CloseHandle.argtypes=[ctypes.c_void_p]
        handle=kernel.CreateFileW(str(Path(run['InstallDir'])/'PerfectDark.exe'),0x80000000,0,None,3,0,None)
        self.assertNotEqual(handle,ctypes.c_void_p(-1).value)
        try:
            with self.assertRaises(OSError): self.seed()
            self.assertTrue((Path(run['InstallDir'])/'pd.unit.z64').exists())
        finally: kernel.CloseHandle(handle)

    def test_alias_and_corruption_refused(self):
        alias=self.source/'alias.exe'; os.link(self.source/'PerfectDark.exe',alias)
        try:
            with self.operation() as s:
                with self.assertRaises(storage.StorageError): s.put(alias)
        finally: alias.unlink()
        run=self.seed(); self.complete(run)
        with self.operation() as s:
            row=s.state['installs'][run['StorageId']]; files,_=s.recipe(row)
            blob=s.blob_path(files['PerfectDark.exe']['sha256']); blob.write_bytes(b'corrupt')
        with self.operation() as s:
            with self.assertRaises((OSError,storage.StorageError)): s.restore(run['StorageId'],s.root/'restored/corrupt')
        for name in ['../escape','a/../b','a/./b','C:/outside','a\\b','/absolute']:
            with self.assertRaises(storage.StorageError): storage.relative(name)

    def test_policy_validation(self):
        for value in [dict(max_full_installs=0),dict(min_free_gib=float('nan')),dict(max_archive_gib=-1),dict(schema=2),dict(typo=1)]:
            with self.assertRaises(storage.StorageError): storage.policy_from(value)

    def test_canonical_containment_with_dot_segments(self):
        with self.assertRaises(storage.StorageError):
            storage.Storage(storage.CHECKOUT/'.claude/../..',self.policy)
        run=self.seed(); self.complete(run)
        with self.operation() as s:
            with self.assertRaises(storage.StorageError):
                s.restore(run['StorageId'],s.root/'restored/../../escape')
        self.assertFalse((self.project/'.claude/escape').exists())

    def test_junction_traversal_refused(self):
        if os.name!='nt': self.skipTest('Windows junction gate')
        alias=self.project/'junction'
        # MinGW Python uses forward-slash paths; cmd treats them as switches.
        # Normalize the same slash form on every Windows Python implementation.
        alias_native=alias.as_posix().replace('/', '\\')
        source_native=self.source.as_posix().replace('/', '\\')
        result=subprocess.run(['cmd','/c','mklink','/J',alias_native,source_native],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        try:
            with self.assertRaises(storage.StorageError): storage.plain(alias/'PerfectDark.exe')
            with self.assertRaises(storage.StorageError): list(storage.walk(alias))
        finally: os.rmdir(alias)  # Remove our junction entry only, never its target.

    def template_request(self, template, **extra):
        return dict(name='template-test', target='pd', template=str(template), owner_pid=os.getpid(), **extra)

    def test_template_with_explicit_binary_keeps_shared_data_and_swaps_binary(self):
        # One pre-extracted template serves any build under test: explicit inputs
        # replace same-named template members, everything else comes from the template.
        template = self.project / 'template'
        (template / 'data/unit').mkdir(parents=True)
        (template / 'PerfectDark.exe').write_bytes(b'template-binary\0' * 64)
        (template / 'pd.unit.z64').write_bytes(b'fake-rom\0' * 128)
        (template / 'data/unit/editable.pdmesh').write_bytes(b'shared-extraction\0' * 256)
        (template / 'logs').mkdir(); (template / 'logs/old.log').write_text('never copied\n')
        tested = self.project / 'session-build'; tested.mkdir()
        (tested / 'PerfectDark.exe').write_bytes(b'build-under-test\0' * 64)
        (tested / 'runtime.dll').write_bytes(b'fake-dll\0' * 16)
        with self.operation() as s: plain_run = s.seed(self.template_request(template))
        with self.operation() as s:
            swapped = s.seed(self.template_request(template, sources=[
                [str(tested / 'PerfectDark.exe'), 'PerfectDark.exe'],
                [str(tested / 'runtime.dll'), 'runtime.dll']]))
        a, b = Path(plain_run['InstallDir']), Path(swapped['InstallDir'])
        self.assertEqual((a / 'PerfectDark.exe').read_bytes(), (template / 'PerfectDark.exe').read_bytes())
        self.assertFalse((a / 'runtime.dll').exists())
        self.assertEqual((b / 'PerfectDark.exe').read_bytes(), (tested / 'PerfectDark.exe').read_bytes())
        self.assertEqual((b / 'runtime.dll').read_bytes(), (tested / 'runtime.dll').read_bytes())
        for install in (a, b):
            self.assertEqual((install / 'data/unit/editable.pdmesh').read_bytes(),
                             (template / 'data/unit/editable.pdmesh').read_bytes())
            self.assertEqual((install / 'pd.unit.z64').read_bytes(), (template / 'pd.unit.z64').read_bytes())
            self.assertFalse((install / 'logs/old.log').exists())
        self.assertNotEqual(plain_run['StorageSeed'], swapped['StorageSeed'])
        # The template itself is never written.
        self.assertEqual((template / 'PerfectDark.exe').read_bytes(), b'template-binary\0' * 64)
        self.assertEqual((template / 'logs/old.log').read_text(), 'never copied\n')
        # A template is a complete install: it cannot also take a separate data source.
        with self.operation() as s:
            with self.assertRaises(storage.StorageError):
                s.seed(self.template_request(template, data_source=str(self.data), rom_id='unit'))
        # Explicit inputs still go through the same path and alias checks.
        with self.operation() as s:
            with self.assertRaises(storage.StorageError):
                s.seed(self.template_request(template, sources=[[str(tested / 'PerfectDark.exe'), '../escape.exe']]))

    def test_dry_run_is_non_mutating(self):
        request=json.dumps({'project':str(self.project),'policy':POLICY})
        result=subprocess.run([os.sys.executable,'-B',str(Path(__file__).with_name('storage.py')),'dry-run'],input=request,text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse(json.loads(result.stdout)['DeletionPerformed'])
        self.assertFalse((self.project/'.claude/smoke-storage').exists())

def main():
    began=time.monotonic()
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(StorageTests))
    report={'passed':result.wasSuccessful(),'tests':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),
            'seconds':round(time.monotonic()-began,3),'measurements':MEASUREMENTS,'fixture_root':str(BASE),
            'fixture_bytes':sum(s.st_size for _,s in storage.walk(BASE)), 'historical_output_touched':False}
    target=storage.CHECKOUT/'.claude/smoke-storage-validation'/BASE.name
    target.mkdir(parents=True); (target/'results.json').write_text(json.dumps(report,indent=2))
    # Only this tiny generated fixture tree, validated below canonical .claude.
    storage.plain(BASE)
    assert BASE.is_relative_to(storage.CHECKOUT/'.claude/smoke-storage-selftests')
    for p,_ in storage.walk(BASE): storage.exclusive_file(p,delete=True)
    for parent,dirs,_ in os.walk(BASE,topdown=False,followlinks=False):
        for name in dirs: storage.plain(Path(parent)/name).rmdir()
    BASE.rmdir()
    print(json.dumps(report)); print('Evidence: '+str(target/'results.json'))
    return 0 if result.wasSuccessful() else 1

if __name__=='__main__': raise SystemExit(main())
