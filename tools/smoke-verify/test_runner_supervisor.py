"""Source/component tests. Inert broker identities; no native supervisor launch."""
import argparse
import copy
import ctypes
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import time
import threading
from contextlib import contextmanager

from runner_retention import RunnerRetention
from runner_supervisor import ReceiptOwner, Native, StorageError, operation_id, publish, validate_spec, check_client_identity, CLOSURE, MAX_CONTROLLERS
from storage import process_identity

EVIDENCE = None
ROOT = Path(__file__).absolute().parents[2]


class FakeNative:
    def __init__(self):
        self.self_identity = process_identity(os.getpid())
        self.live = {11: dict(pid=11, creation_ticks=111), 22: dict(pid=22, creation_ticks=222)}
        self.duplicates = 0
        self.partial = False
        self.handles = {}

    def identity(self, pid):
        return self.live.get(pid)

    def duplicate_witnesses(self, controller):
        self.duplicates += 1
        key = (controller['pid'], controller['creation_ticks'])
        record = self.handles.setdefault(key, {'root': 1000 + self.duplicates})
        if self.partial:
            self.partial = False
            raise StorageError('injected partial transfer')
        record.setdefault('job', 2000 + self.duplicates)
        return dict(record)


def spec(native, value=None, ordinal='0'):
    raw = json.dumps(value or {'proof': {'RunId': 'runner-' + 'a' * 32}}).encode()
    name = 'parent-closeout-' + ordinal * 32 + '.json'
    parent = 'runner-' + 'a' * 32
    return dict(schema='pd2.receipt-supervisor.v1', operation=operation_id(parent, name), parent_run=parent,
                parent_owner=native.live[11], name=name, request_sha256=hashlib.sha256(raw).hexdigest(),
                sources={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in CLOSURE}), raw


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.native = FakeNative()
        self.spec, self.raw = spec(self.native)
        self.owner = ReceiptOwner(self.spec, self.native)

    def message(self, action, **extra):
        return dict(self.spec, action=action, **extra)

    def test_source_identity_and_conflicts(self):
        validate_spec(self.spec, ROOT)
        for field in ('operation', 'parent_run', 'name', 'request_sha256', 'sources'):
            bad = self.message('attach'); bad[field] = 'foreign'
            with self.assertRaises(StorageError): self.owner.request(bad, 11)
        bad = copy.deepcopy(self.spec); bad['sources'].pop(CLOSURE[0])
        with self.assertRaises(StorageError): validate_spec(bad, ROOT)
        bad = copy.deepcopy(self.spec); bad['sources'][CLOSURE[0]] = '0' * 64
        with self.assertRaises(StorageError): validate_spec(bad, ROOT)

    def test_live_original_refuses_takeover(self):
        with self.assertRaisesRegex(StorageError, 'still live'): self.owner.request(self.message('attach'), 22)
        self.owner.request(self.message('attach'), 11)
        with self.assertRaisesRegex(StorageError, 'unverified'): self.owner.request(self.message('shutdown', handles=self.owner.handles), 11)
        self.assertFalse(self.owner.shutdown.is_set())

    def test_late_result_and_explicit_recovery(self):
        original = self.owner.request(self.message('attach'), 11)
        del self.native.live[11]
        recovered = self.owner.request(self.message('attach'), 22)
        self.assertNotEqual(original['handles'], recovered['handles'])
        self.assertFalse(self.owner.shutdown.is_set())
        self.owner.terminal = {'state': 'published', 'path': 'inert', 'sha256': '0' * 64}
        self.owner.request(self.message('status'), 22)
        self.assertFalse(self.owner.shutdown.is_set())
        reply = self.owner.request(self.message('shutdown', handles=recovered['handles']), 22)
        self.assertTrue(reply['shutdown_ready'])
        self.assertFalse(self.owner.shutdown.is_set())  # failed reply cannot exit
        self.owner.acknowledge(reply)
        self.assertTrue(self.owner.shutdown.is_set())

    def test_same_controller_failed_reply_reuses_witnesses(self):
        first = self.owner.request(self.message('attach'), 11)
        second = self.owner.request(self.message('attach'), 11)
        self.assertEqual(first['handles'], second['handles'])
        self.assertEqual(self.native.duplicates, 1)
        self.assertEqual(self.owner.attachments, 1)

    def test_partial_transfer_retains_bound_controller(self):
        self.native.partial = True
        with self.assertRaises(StorageError): self.owner.request(self.message('attach'), 11)
        self.assertEqual(self.owner.controller, self.native.live[11])
        held = self.native.handles[(11, 111)]['root']
        recovered = self.owner.request(self.message('attach'), 11)
        self.assertEqual(recovered['handles']['root'], held)
        self.assertEqual(self.owner.attachments, 1)

    def test_reused_pid_does_not_equal_original_generation(self):
        self.owner.request(self.message('attach'), 11)
        self.native.live[11] = dict(pid=11, creation_ticks=999)
        self.owner.request(self.message('attach'), 22)
        with self.assertRaisesRegex(StorageError, 'still live'): self.owner.request(self.message('attach'), 11)

    def test_unknown_identity_is_not_exit(self):
        self.native.identity = lambda pid: (_ for _ in ()).throw(StorageError('identity unavailable'))
        with self.assertRaises(StorageError): self.owner.request(self.message('attach'), 22)
        self.assertEqual(self.owner.attachments, 0)

    def test_pipe_claim_matches_held_generation(self):
        held = {'pid': 11, 'creation_ticks': 111}
        check_client_identity({'controller_identity': held}, held)
        for bad in ({'pid': 11, 'creation_ticks': 999}, {'pid': 22, 'creation_ticks': 111}, {'pid': True, 'creation_ticks': 111}, None):
            with self.assertRaises(StorageError): check_client_identity({'controller_identity': bad}, held)

    def test_attachment_capacity_no_eviction(self):
        del self.native.live[11]
        for n in range(MAX_CONTROLLERS):
            self.native.live = {22 + n: dict(pid=22 + n, creation_ticks=222 + n)}
            self.owner.request(self.message('attach'), 22 + n)
        previous = copy.deepcopy(self.owner.controller)
        self.native.live = {99: dict(pid=99, creation_ticks=999)}
        with self.assertRaisesRegex(StorageError, 'capacity'): self.owner.request(self.message('attach'), 99)
        self.assertEqual(self.owner.controller, previous)

    def test_metadata_does_not_transfer_or_shutdown(self):
        reply = self.owner.request(self.message('describe'), 22)
        self.assertTrue(reply['metadata_only'])
        self.assertIsNone(self.owner.controller)
        self.assertEqual(self.native.duplicates, 0)


class InertKernel:
    """Production Native constructor/loop over inert calls, no OS resources."""
    def __init__(self):
        self.last_error = 0
        self.handles = {}
        self.next_handle = 100
        self.connect_errors = deque()
        self.frames = deque()
        self.writes = []
        self.disconnects = 0
        self.close_failures = 0
        self.closed = []
        self.client_pid = 11
        self.mutex_wait = 0
        self.mutex_releases = []
        self.mutex_release_failures = 0
        self.on_ack = None

    def GetCurrentProcess(self): return 99
    def CreateMutexW(self, *args): return 88
    def WaitForSingleObject(self, handle, timeout): return self.mutex_wait
    def ReleaseMutex(self, handle):
        if self.mutex_release_failures:
            self.mutex_release_failures -= 1; self.last_error = 5; return False
        self.mutex_releases.append(handle); return True
    def IsProcessInJob(self, process, job, inside): inside._obj.value = True; return True
    def OpenProcess(self, access, inherit, pid):
        self.next_handle += 1
        self.handles[self.next_handle] = pid
        return self.next_handle
    def GetProcessTimes(self, handle, created, exited, kernel, user):
        pid = self.handles.get(handle, os.getpid())
        created._obj.dwLowDateTime = {11: 111, 22: 222}.get(pid, 999)
        return True
    def CloseHandle(self, handle):
        if self.close_failures:
            self.close_failures -= 1; self.last_error = 5; return False
        self.closed.append(handle); return True
    def CreateNamedPipeW(self, *args): return 77
    def ConnectNamedPipe(self, pipe, overlapped):
        error = self.connect_errors.popleft() if self.connect_errors else 0
        self.last_error = error; return error == 0
    def DisconnectNamedPipe(self, pipe): self.disconnects += 1; return True
    def GetNamedPipeClientProcessId(self, pipe, pid): pid._obj.value = self.client_pid; return True
    def DuplicateHandle(self, source, handle, target, output, *args):
        output._obj.value = handle + 10000; return True
    def WriteFile(self, pipe, raw, size, count, overlap):
        self.writes.append(bytes(raw)); count._obj.value = size; return True
    def ReadFile(self, pipe, buffer, size, count, overlap):
        item = self.frames.popleft() if self.frames else 'lost'
        if item is None:
            self.last_error = 232; return False
        if item == 'lost':
            self.last_error = 109; return False
        if item in ('ack', 'badack'):
            if self.on_ack is not None: self.on_ack()
            item = json.dumps({'ack_sha256': hashlib.sha256(self.writes[-1]).hexdigest() if item == 'ack' else '0' * 64}).encode()
        ctypes.memmove(buffer, item, len(item)); count._obj.value = len(item); return True


class NativeAdapterTests(unittest.TestCase):
    def setUp(self):
        self.kernel = InertKernel()
        self.native = Native(55, 'f' * 32, kernel=self.kernel)
        self.spec, self.raw = spec(FakeNative())
        self.owner = ReceiptOwner(self.spec, self.native)

    def wire(self, action, ack='ack'):
        message = dict(self.spec, action=action, controller_identity={'pid': 11, 'creation_ticks': 111})
        if self.owner.handles is not None: message['handles'] = self.owner.handles
        self.kernel.frames.extend([json.dumps(message).encode(), ack])
        self.native.serve_once(self.owner)

    def test_production_constructor_initializes_identity_state(self):
        self.assertEqual(self.native.self_identity, {'pid': os.getpid(), 'creation_ticks': 999})
        self.assertIsNone(self.native.client_handle)

    def test_unread_reply_never_authorizes_shutdown(self):
        self.wire('attach')
        self.owner.terminal = {'state': 'published', 'path': 'inert', 'sha256': '0' * 64}
        self.wire('shutdown', 'lost')
        self.assertFalse(self.owner.shutdown.is_set())
        previous = dict(self.owner.handles)
        self.wire('shutdown')
        self.assertTrue(self.owner.shutdown.is_set())
        self.assertEqual(self.owner.handles, previous)

    def test_delayed_client_read_and_exact_acknowledgement(self):
        self.wire('attach')
        self.owner.terminal = {'state': 'published', 'path': 'inert', 'sha256': '0' * 64}
        message = dict(self.spec, action='shutdown', handles=self.owner.handles,
                       controller_identity={'pid': 11, 'creation_ticks': 111})
        self.kernel.frames.extend([json.dumps(message).encode(), None, 'ack'])
        self.native.serve_once(self.owner)
        self.assertTrue(self.owner.shutdown.is_set())

    def test_stale_ack_nonce_and_lost_original_do_not_exit(self):
        self.wire('attach')
        self.owner.terminal = {'state': 'published', 'path': 'inert', 'sha256': '0' * 64}
        self.wire('shutdown', 'badack')
        self.assertFalse(self.owner.shutdown.is_set())
        self.assertIn('acknowledgement', self.native.first_failure['message'])

    def test_disconnected_before_accept_resets_connection(self):
        self.kernel.connect_errors.append(232)
        self.native.serve_once(self.owner)
        self.assertEqual(self.kernel.disconnects, 1)
        self.wire('attach')
        self.assertIsNotNone(self.owner.handles)

    def test_failed_client_close_retains_witness_and_broker(self):
        self.kernel.close_failures = 1
        self.wire('attach')
        retained = self.native.client_handle
        self.assertIsNotNone(retained)
        self.assertEqual(self.native.first_failure['native_error'], 5)
        self.assertFalse(self.owner.shutdown.is_set())
        self.wire('status')
        self.assertIn(retained, self.kernel.closed)
        self.assertIsNone(self.native.client_handle)
        self.assertIsNotNone(self.owner.handles)

    def test_failed_query_close_blocks_new_queries_until_retry(self):
        self.kernel.close_failures = 1
        with self.assertRaises(OSError): self.native.identity(22)
        pending = list(self.native.pending_closes)
        self.kernel.close_failures = 1
        with self.assertRaises(OSError): self.native.identity(22)
        self.assertEqual(self.native.pending_closes, pending)
        self.assertEqual(self.native.identity(22), {'pid': 22, 'creation_ticks': 222})
        self.assertEqual(self.native.pending_closes, [])

    def admission_fixture(self):
        directory = EVIDENCE / ('admission-' + str(len(list(EVIDENCE.glob('admission-*')))))
        (directory / '.codex-coordination').mkdir(parents=True)
        path = directory / '.codex-coordination/state.json'
        path.write_bytes(b'{"unit":1}')
        self.spec.update(project=str(directory), qualification_required=True)
        return path, dict(deadline_utc_ticks=time.time_ns() // 100 + 621355968000000000 + 100000000,
                         queue_path=str(path).replace('\\', '/'), queue_sha256=hashlib.sha256(path.read_bytes()).hexdigest())

    def test_qualification_missing_wire_admission_refuses(self):
        self.admission_fixture()
        with self.assertRaisesRegex(StorageError, 'wire admission'):
            with self.native.admission({}, self.spec): self.fail('unadmitted boundary executed')
        self.assertEqual(self.kernel.mutex_releases, [])

    def test_qualification_abandoned_mutex_releases_and_refuses(self):
        _, record = self.admission_fixture(); self.kernel.mutex_wait = 0x80
        with self.assertRaisesRegex(StorageError, 'Abandoned'):
            with self.native.admission({'admission':record}, self.spec): self.fail('abandoned boundary executed')
        self.assertEqual(self.kernel.mutex_releases, [88])
        self.assertIsNone(self.native.admission_mutex)

    def test_qualification_expired_before_rebind_refuses(self):
        _, record = self.admission_fixture(); record['deadline_utc_ticks'] = 1
        with self.assertRaisesRegex(StorageError, 'expired'):
            with self.native.admission({'admission':record}, self.spec): self.owner.request(self.message_for_guard(), 11)
        self.assertIsNone(self.owner.controller)
        self.assertEqual(self.kernel.mutex_releases, [88])

    def message_for_guard(self):
        return dict(self.spec, action='attach', controller_identity={'pid':11,'creation_ticks':111})

    def test_actual_broker_ack_after_fifo_change_does_not_shutdown(self):
        path, record = self.admission_fixture()
        message = self.message_for_guard(); message['admission'] = record
        self.kernel.frames.extend([json.dumps(message).encode(), 'ack'])
        self.native.serve_once(self.owner)
        self.assertIsNotNone(self.owner.handles)
        self.owner.terminal = {'state':'published','path':'inert','sha256':'0'*64}
        message.update(action='shutdown',handles=self.owner.handles)
        self.kernel.on_ack = lambda: path.write_bytes(b'{"unit":2}')
        self.kernel.frames.extend([json.dumps(message).encode(), 'ack'])
        self.native.serve_once(self.owner)
        self.assertFalse(self.owner.shutdown.is_set())
        self.assertIn('FIFO changed',self.native.first_failure['message'])

    def test_actual_broker_ack_after_expiry_does_not_shutdown(self):
        _, record = self.admission_fixture()
        message = self.message_for_guard(); message['admission'] = record
        self.kernel.frames.extend([json.dumps(message).encode(), 'ack'])
        self.native.serve_once(self.owner)
        self.owner.terminal = {'state':'published','path':'inert','sha256':'0'*64}
        message.update(action='shutdown',handles=self.owner.handles)
        self.kernel.frames.extend([json.dumps(message).encode(), 'ack'])
        # Advance actual guard clock after reply read, without changing request.
        after = (record['deadline_utc_ticks'] - 621355968000000000) * 100
        with patch('runner_supervisor.time.time_ns', side_effect=[after-100,after]):
            self.native.serve_once(self.owner)
        self.assertFalse(self.owner.shutdown.is_set())
        self.assertIn('expired',self.native.first_failure['message'])

    def test_qualification_failed_mutex_release_retains_exact_witness(self):
        _, record = self.admission_fixture(); self.kernel.mutex_release_failures = 1
        with self.assertRaises(OSError):
            with self.native.admission({'admission':record},self.spec): pass
        self.assertEqual(self.native.admission_mutex,88)
        self.assertTrue(self.native.admission_taken)
        self.assertNotIn(88,self.kernel.closed)
        self.native.cleanup_admission()
        self.assertIsNone(self.native.admission_mutex)
        self.assertFalse(self.native.admission_taken)
        self.assertIn(88,self.kernel.closed)


class PublicationTests(unittest.TestCase):
    sequence = 0
    def setUp(self):
        type(self).sequence += 1
        self.project = EVIDENCE / ('case-' + str(self.sequence))
        self.project.mkdir()
        self.native = FakeNative()
        self.spec, self.raw = spec(self.native, ordinal=str(self.sequence))
        self.spec.update(project=str(self.project), policy={'min_free_gib': 0})
        self.owner = ReceiptOwner(self.spec, self.native)

    def result(self):
        return json.loads(Path(self.owner.terminal['path']).read_text())

    def test_accounting_entry_precedes_real_storage_construction(self):
        observed = []
        owner = self.owner
        class EntryStorage(RunnerRetention):
            def __init__(self, *args, **kwargs):
                with owner.lock:
                    observed.append(owner.accounting_started)
                super().__init__(*args, **kwargs)
        before = owner.request(dict(self.spec, action='attach'), 11)
        self.assertIs(before['accounting_started'], False)
        publish(self.spec, self.raw, self.native, owner, EntryStorage)
        self.assertEqual(observed, [True])
        after = owner.request(dict(self.spec, action='status'), 11)
        self.assertIs(after['accounting_started'], True)
        self.assertEqual(owner.terminal['state'], 'published')
        self.assertFalse(owner.shutdown.is_set())

    def test_accounting_entry_is_observable_while_completion_pending(self):
        entered, release = threading.Event(), threading.Event()
        owner = self.owner
        class WaitingStorage(RunnerRetention):
            @contextmanager
            def locked(self):
                entered.set()
                if not release.wait(5):
                    raise StorageError('source fixture release timed out')
                with super().locked():
                    yield self
        owner.request(dict(self.spec, action='attach'), 11)
        worker = threading.Thread(target=publish,
            args=(self.spec, self.raw, self.native, owner, WaitingStorage))
        worker.start()
        try:
            self.assertTrue(entered.wait(2))
            reply = owner.request(dict(self.spec, action='status'), 11)
            self.assertIs(reply['accounting_started'], True)
            self.assertIsNone(reply['terminal'])
            self.assertTrue(worker.is_alive())
            with self.assertRaisesRegex(StorageError, 'unverified'):
                owner.request(dict(self.spec, action='shutdown', handles=owner.handles), 11)
            self.assertFalse(owner.shutdown.is_set())
        finally:
            release.set()
            worker.join(5)
        self.assertFalse(worker.is_alive())
        self.assertEqual(owner.terminal['state'], 'published')
        self.assertIs(owner.accounting_started, True)

    def test_existing_accounting_publish_and_collision(self):
        publish(self.spec, self.raw, self.native, self.owner)
        self.assertEqual(self.owner.terminal['state'], 'published')
        result = self.result()
        self.assertEqual(result['context']['Owner'], self.native.self_identity)
        self.assertNotEqual(result['context']['Owner'], self.spec['parent_owner'])
        receipt = Path(result['receipt']['Path'])
        before = receipt.read_bytes()
        duplicate = ReceiptOwner(self.spec, self.native)
        publish(self.spec, self.raw, self.native, duplicate)
        self.assertIsNone(duplicate.terminal)
        self.assertIsNotNone(duplicate.failure)
        self.assertEqual(receipt.read_bytes(), before)

    def test_original_owner_loss_does_not_change_storage_owner(self):
        del self.native.live[11]
        publish(self.spec, self.raw, self.native, self.owner)
        self.assertEqual(self.owner.terminal['state'], 'published')
        self.assertEqual(self.result()['spec']['parent_owner']['pid'], 11)
        self.assertFalse(self.owner.shutdown.is_set())

    def test_partial_input_failure_is_accounted_and_retained(self):
        publish(self.spec, self.raw[:-1], self.native, self.owner)
        self.assertEqual(self.owner.terminal['state'], 'failed')
        self.assertIn('request-hash', self.result()['failure']['message'])

    def test_context_failure_is_unknown_not_terminal(self):
        class FailContext(RunnerRetention):
            def new_run(self, *args, **kwargs): raise StorageError('injected context admission failure')
        publish(self.spec, self.raw, self.native, self.owner, FailContext)
        self.assertIsNone(self.owner.terminal)
        self.assertIn('context admission', self.owner.failure['message'])
        self.owner.request(dict(self.spec, action='attach'), 11)
        with self.assertRaises(StorageError): self.owner.request(dict(self.spec, action='shutdown', handles=self.owner.handles), 11)

    def test_terminal_accounting_failure_keeps_original_receipt(self):
        class FailResult(RunnerRetention):
            def write_run(self, request):
                if request['name'] == 'operation-result.json': raise StorageError('injected result accounting failure')
                return super().write_run(request)
        publish(self.spec, self.raw, self.native, self.owner, FailResult)
        self.assertIsNone(self.owner.terminal)
        self.assertIn('accounting_error', self.owner.failure)
        context = self.project / '.claude/smoke-storage/receipts' / ('runner-' + self.spec['operation'])
        self.assertTrue((context / self.spec['name']).is_file())

    def test_internal_id_does_not_change_ordinary_wire_admission(self):
        storage = RunnerRetention(str(self.project), {'min_free_gib': 0})
        with storage.locked():
            with self.assertRaisesRegex(StorageError, 'internal'): storage.new_run({'owner_pid': os.getpid()}, internal_run_id='foreign')
            ctx = storage.new_run({'owner_pid': os.getpid(), 'internal_run_id': 'foreign'})
        self.assertRegex(ctx['RunId'], '^runner-[0-9a-f]{32}$')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--evidence', required=True)
    args = parser.parse_args()
    EVIDENCE = Path(args.evidence).absolute()
    if not EVIDENCE.is_relative_to(ROOT / '.claude') or EVIDENCE.exists(): raise SystemExit('Fresh owned .claude evidence required')
    EVIDENCE.mkdir(parents=True)
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    record = dict(passed=result.wasSuccessful(), tests=result.testsRun, no_supervisor_or_native_capture=True,
                  inert_native_protocol=True, existing_storage_accounting=True, retained_fixtures=True,
                  sources={p:hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in CLOSURE},
                  test_source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    with (EVIDENCE / 'result.json').open('x', encoding='utf-8') as out: json.dump(record, out, indent=2)
    sys.exit(0 if result.wasSuccessful() else 1)
