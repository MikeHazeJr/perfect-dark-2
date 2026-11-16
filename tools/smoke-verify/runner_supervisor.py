"""One surviving receipt owner. No subprocess, cleanup, queue release or kills.

The caller enrolls this process in a non-killing job at creation and explicitly
inherits its job handle. Native tests/admission are separate from source tests.
"""
import argparse
import base64
import ctypes
import contextlib
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import sys
import threading
import time

from runner_retention import RunnerRetention
from storage import StorageError, plain

MAX_BODY = 32 * 1024 * 1024
MAX_FRAME = 16384
MAX_CONTROLLERS = 8
SCHEMA = "pd2.receipt-supervisor.v1"
CLOSURE = (
    "tools/smoke-verify/runner_supervisor.py",
    "tools/smoke-verify/runner_retention.py",
    "tools/smoke-verify/storage.py",
    "devtools/smoke-storage-manifest.py",
    "tools/smoke-verify/lib/Storage-Harness.ps1",
    "tools/smoke-verify/lib/Runner-Supervisor.ps1",
    "tools/smoke-verify/lib/Runner-Retention.ps1",
    "devtools/Build-Retention.ps1",
    "devtools/build-session.ps1",
)


def hash_bytes(value):
    return hashlib.sha256(value).hexdigest()


def operation_id(parent_run, name):
    return hash_bytes((parent_run + "\n" + name).encode("utf-8"))[:32]


def check_client_identity(message, held):
    claimed = message.get("controller_identity")
    if not isinstance(claimed, dict) or type(claimed.get("pid")) is not int or type(claimed.get("creation_ticks")) is not int or claimed != held:
        raise StorageError("Local pipe client generation mismatch")


def validate_spec(spec, source_root):
    if type(spec.get('qualification_required', False)) is not bool:
        raise StorageError('Supervisor qualification authority must be boolean')
    if spec.get("schema") != SCHEMA or not re.fullmatch(r"runner-[0-9a-f]{32}", spec.get("parent_run", "")):
        raise StorageError("Invalid supervisor parent/schema")
    if not re.fullmatch(r"parent-closeout-[0-9a-f]{32}(?:-completion)?\.json", spec.get("name", "")):
        raise StorageError("Invalid supervisor receipt name")
    if spec.get("operation") != operation_id(spec["parent_run"], spec["name"]):
        raise StorageError("Supervisor operation mismatch")
    if not re.fullmatch(r"[0-9a-f]{64}", spec.get("request_sha256", "")):
        raise StorageError("Invalid supervisor request hash")
    owner = spec.get("parent_owner", {})
    if type(owner.get("pid")) is not int or owner["pid"] <= 0 or type(owner.get("creation_ticks")) is not int or owner["creation_ticks"] <= 0:
        raise StorageError("Invalid supervisor original owner")
    if set(spec.get("sources", {})) != set(CLOSURE):
        raise StorageError("Incomplete supervisor source closure")
    for name, expected in spec["sources"].items():
        if hash_bytes(plain(source_root / name).read_bytes()) != expected:
            raise StorageError("Supervisor source changed: " + name)


class ReceiptOwner:
    """Pure controller protocol; native operations are explicit injected seams."""
    def __init__(self, spec, native):
        self.spec, self.native = spec, native
        self.original = spec["parent_owner"]
        self.controller = None
        self.handles = None
        self.attachments = 0
        self.terminal = None
        self.failure = None
        self.accounting_started = False
        self.shutdown = threading.Event()
        self.lock = threading.RLock()

    def matches(self, message):
        for key in ("operation", "parent_run", "name", "request_sha256", "sources"):
            if message.get(key) != self.spec[key]:
                raise StorageError("Supervisor immutable operation mismatch: " + key)

    def original_live(self):
        identity = self.native.identity(self.original["pid"])
        return identity == self.original

    def request(self, message, client_pid):
        with self.lock:
            if message.get("action") == "describe":
                for key in ("operation", "parent_run", "name", "sources"):
                    if message.get(key) != self.spec[key]:
                        raise StorageError("Supervisor describe identity mismatch")
                if self.native.identity(client_pid) is None:
                    raise StorageError("Supervisor describe client unavailable")
                return dict(schema=SCHEMA, operation=self.spec["operation"],
                            identity=self.native.self_identity, spec=self.spec,
                            output_mode="nul", metadata_only=True)
            self.matches(message)
            client = self.native.identity(client_pid)
            if client is None:
                raise StorageError("Supervisor client generation unavailable")
            if client != self.original and self.original_live():
                raise StorageError("Original supervisor controller is still live")
            if self.controller is not None and client != self.controller:
                if self.native.identity(self.controller["pid"]) == self.controller:
                    raise StorageError("Another supervisor controller is still live")
            action = message.get("action")
            if action == "attach":
                if client != self.controller:
                    if self.attachments >= MAX_CONTROLLERS:
                        raise StorageError("Supervisor attachment capacity exhausted")
                    # Bind before duplication/reply. Partial native duplication
                    # stays on the native adapter and retries only this client.
                    self.controller = client
                    self.handles = None
                    self.attachments += 1
                if self.handles is None:
                    self.handles = self.native.duplicate_witnesses(client)
            elif action in ("status", "shutdown"):
                if client != self.controller or self.handles is None:
                    raise StorageError("Supervisor client has not acquired witnesses")
                if action == "shutdown":
                    if self.terminal is None or message.get("handles") != self.handles:
                        raise StorageError("Supervisor terminal handoff unverified")
                    # The reply must be delivered before the broker authorizes
                    # exit. A failed reply keeps the same witnesses and owner.
            else:
                raise StorageError("Unknown supervisor action")
            return dict(schema=SCHEMA, operation=self.spec["operation"],
                        identity=self.native.self_identity, controller=self.controller,
                        handles=self.handles, terminal=self.terminal, failure=self.failure,
                        accounting_started=self.accounting_started,
                        output_mode="nul", shutdown_ready=action == "shutdown")

    def acknowledge(self, reply):
        # Called only after a same-connection acknowledgement of the exact
        # nonce-bearing reply bytes. Buffered WriteFile is not delivery.
        with self.lock:
            if reply.get("shutdown_ready"):
                if (reply.get("controller") != self.controller or
                        reply.get("handles") != self.handles or
                        reply.get("terminal") != self.terminal or self.terminal is None):
                    raise StorageError("Stale supervisor terminal acknowledgement")
                self.shutdown.set()


def publish(spec, raw, native, owner, storage_factory=RunnerRetention):
    """Existing publisher/accounting only, under this supervisor's live identity."""
    # This reports entry into this real accounting function. It confers no
    # lock acquisition, completion, admission or shutdown authority.
    with owner.lock:
        owner.accounting_started = True
    storage = storage_factory(spec["project"], spec.get("policy"))
    context = None
    try:
        with storage.locked():
            context = storage.new_run({"owner_pid": native.self_identity["pid"]},
                                      internal_run_id="runner-" + spec["operation"])
            marker = dict(schema=SCHEMA, spec=spec, supervisor=native.self_identity,
                          output_mode="nul", job_name="Local\\PD2Receipt-" + spec["operation"])
            registration = storage.write_run(dict(owner_pid=native.self_identity["pid"],
                run_id=context["RunId"], name="operation.json", value=marker))
        if len(raw) > MAX_BODY or hash_bytes(raw) != spec["request_sha256"]:
            raise StorageError("Supervisor partial/oversized/request-hash mismatch")
        value = json.loads(raw.decode("utf-8"), parse_constant=lambda x: (_ for _ in ()).throw(StorageError("Nonfinite receipt JSON")))
        if value.get("proof", {}).get("RunId") != spec["parent_run"]:
            raise StorageError("Supervisor parent proof mismatch")
        with storage.locked():
            receipt = storage.write_run(dict(owner_pid=native.self_identity["pid"],
                run_id=context["RunId"], name=spec["name"], value=value))
            receipt["ReceiptSha256"] = hash_bytes(plain(receipt["Path"]).read_bytes())
            terminal = dict(schema=SCHEMA, spec=spec, supervisor=native.self_identity,
                            state="published", context=context, receipt=receipt,
                            registration=registration, output_mode="nul")
            result = storage.write_run(dict(owner_pid=native.self_identity["pid"],
                run_id=context["RunId"], name="operation-result.json", value=terminal))
        with owner.lock:
            owner.terminal = dict(state="published", path=result["Path"], sha256=hash_bytes(plain(result["Path"]).read_bytes()))
    except Exception as error:
        reason = dict(type=type(error).__name__, message=str(error)[:2048])
        with owner.lock:
            owner.failure = reason
        if context is not None:
            try:
                with storage.locked():
                    terminal = dict(schema=SCHEMA, spec=spec, supervisor=native.self_identity,
                                    state="failed", context=context, failure=reason, output_mode="nul")
                    result = storage.write_run(dict(owner_pid=native.self_identity["pid"],
                        run_id=context["RunId"], name="operation-result.json", value=terminal))
                with owner.lock:
                    owner.terminal = dict(state="failed", path=result["Path"], sha256=hash_bytes(plain(result["Path"]).read_bytes()))
            except Exception as accounting_error:
                # No fake durable failure when budget/identity/publication failed.
                # The broker and process stay owned; shutdown remains refused.
                with owner.lock:
                    owner.failure["accounting_error"] = str(accounting_error)[:2048]


class Native:
    def __init__(self, job, operation, *, kernel=None):
        if (os.name != "nt" and kernel is None) or job <= 0:
            raise StorageError("Supervisor requires inherited native Windows job")
        from ctypes import wintypes as w
        self.w = w
        self.k = kernel if kernel is not None else ctypes.WinDLL("kernel32", use_last_error=True)
        self.injected_kernel = kernel is not None
        self.client_handle = None
        self.client_pid = None
        self.pending_closes = []
        self.first_failure = None
        self.last_failure = None
        self.disconnect_pending = False
        self.admission_mutex = None
        self.admission_taken = False
        signatures = {
            "CreateMutexW": ([ctypes.c_void_p, w.BOOL, w.LPCWSTR], w.HANDLE),
            "WaitForSingleObject": ([w.HANDLE, w.DWORD], w.DWORD),
            "ReleaseMutex": ([w.HANDLE], w.BOOL),
            "GetCurrentProcess": ([], w.HANDLE),
            "OpenProcess": ([w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
            "GetProcessTimes": ([w.HANDLE] + [ctypes.POINTER(w.FILETIME)] * 4, w.BOOL),
            "CloseHandle": ([w.HANDLE], w.BOOL),
            "IsProcessInJob": ([w.HANDLE, w.HANDLE, ctypes.POINTER(w.BOOL)], w.BOOL),
            "DuplicateHandle": ([w.HANDLE, w.HANDLE, w.HANDLE, ctypes.POINTER(w.HANDLE), w.DWORD, w.BOOL, w.DWORD], w.BOOL),
            "CreateNamedPipeW": ([w.LPCWSTR, w.DWORD, w.DWORD, w.DWORD, w.DWORD, w.DWORD, w.DWORD, ctypes.c_void_p], w.HANDLE),
            "ConnectNamedPipe": ([w.HANDLE, ctypes.c_void_p], w.BOOL),
            "DisconnectNamedPipe": ([w.HANDLE], w.BOOL),
            "GetNamedPipeClientProcessId": ([w.HANDLE, ctypes.POINTER(w.ULONG)], w.BOOL),
            "ReadFile": ([w.HANDLE, ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD), ctypes.c_void_p], w.BOOL),
            "WriteFile": ([w.HANDLE, ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD), ctypes.c_void_p], w.BOOL),
        }
        if not self.injected_kernel:
            for name, (args, result) in signatures.items():
                function = getattr(self.k, name); function.argtypes = args; function.restype = result
        self.job = job
        inside = w.BOOL()
        self.require(self.k.IsProcessInJob(self.k.GetCurrentProcess(), job, ctypes.byref(inside)) and inside.value, "Verify inherited supervisor job")
        self.self_identity = self.identity(os.getpid())
        self.transfers = {}
        self.pipe = self.k.CreateNamedPipeW("\\\\.\\pipe\\PD2Receipt-" + operation,
            3 | 0x80000, 4 | 2 | 1 | 8, 1, MAX_FRAME, MAX_FRAME, 0, None)
        self.require(self.pipe != ctypes.c_void_p(-1).value, "Create exclusive local receipt broker")

    def require(self, ok, action):
        if not ok:
            raise ctypes.WinError(self.last_error(), action)

    @contextlib.contextmanager
    def admission(self, message, spec):
        # Production descriptors and qualification descriptors cannot switch
        # authority. A qualifying controller cannot omit the finite wire gate.
        if not spec.get('qualification_required', False):
            yield
            return
        record = message.get('admission')
        fields = {'deadline_utc_ticks', 'queue_path', 'queue_sha256'}
        expected = str(Path(spec['project']) / '.codex-coordination' / 'state.json').replace('\\', '/')
        if (not isinstance(record, dict) or set(record) != fields or
                type(record['deadline_utc_ticks']) is not int or record['deadline_utc_ticks'] <= 0 or
                record['queue_path'] != expected or
                not isinstance(record['queue_sha256'], str) or
                not re.fullmatch(r'[0-9a-f]{64}', record['queue_sha256'])):
            raise StorageError('Supervisor finite wire admission differs')
        self.cleanup_admission()
        mutex = self.k.CreateMutexW(None, False, 'PerfectDark2CodexCoordination')
        self.require(mutex, 'Open exact qualification coordination mutex')
        self.admission_mutex = mutex
        try:
            waited = self.k.WaitForSingleObject(mutex, 500)
            if waited == 0x80:
                self.admission_taken = True
                raise StorageError('Abandoned coordination mutex; finite admission refused')
            if waited != 0:
                raise StorageError('Qualification coordination mutex unavailable')
            self.admission_taken = True
            path = plain(Path(record['queue_path']))
            with path.open('rb') as stream:
                raw = stream.read(4194305)
            if len(raw) > 4194304 or hash_bytes(raw) != record['queue_sha256']:
                raise StorageError('Qualification FIFO changed before ownership boundary')
            if time.time_ns() // 100 + 621355968000000000 >= record['deadline_utc_ticks']:
                raise StorageError('Qualification expired before ownership boundary')
            yield
        finally:
            self.cleanup_admission()

    def cleanup_admission(self):
        # This broker's serve thread retries exact retained mutex ownership;
        # failed release/close never drops the witness or creates a replacement.
        if self.admission_mutex is None:
            return
        if self.admission_taken:
            self.require(self.k.ReleaseMutex(self.admission_mutex), 'Release acquired qualification mutex')
            self.admission_taken = False
        self.close(self.admission_mutex)
        self.admission_mutex = None

    def last_error(self):
        return self.k.last_error if self.injected_kernel else ctypes.get_last_error()

    def record_failure(self, error):
        failure = dict(type=type(error).__name__, message=str(error)[:2048],
                       native_error=getattr(error, "winerror", 0))
        if self.first_failure is None:
            self.first_failure = failure
        self.last_failure = failure

    def close(self, handle):
        self.require(self.k.CloseHandle(handle), "Close exact supervisor handle")

    def identity(self, pid):
        if self.client_handle is not None and pid == self.client_pid:
            return self.held_identity(self.client_handle, pid)
        self.retry_pending_closes()
        handle = self.k.OpenProcess(0x1000, False, pid)
        if not handle:
            if self.last_error() in (87, 1168):
                return None
            self.require(False, "Query exact controller generation")
        try:
            return self.held_identity(handle, pid)
        finally:
            try:
                self.close(handle)
            except OSError as error:
                self.pending_closes.append(handle)
                self.record_failure(error)
                raise

    def retry_pending_closes(self):
        # Admission stops at a retained failed close, so this collection cannot
        # grow on repeated queries. No handle is overwritten or globally swept.
        while self.pending_closes:
            self.close(self.pending_closes[0])
            self.pending_closes.pop(0)

    def held_identity(self, handle, pid):
        created, exited, kernel, user = (self.w.FILETIME() for _ in range(4))
        self.require(self.k.GetProcessTimes(handle, *map(ctypes.byref, (created, exited, kernel, user))), "Read held controller identity")
        if exited.dwHighDateTime or exited.dwLowDateTime:
            return None
        return dict(pid=pid, creation_ticks=created.dwHighDateTime << 32 | created.dwLowDateTime)

    def duplicate_witnesses(self, controller):
        key = (controller["pid"], controller["creation_ticks"])
        record = self.transfers.setdefault(key, {})
        target = self.client_handle
        if target is None or self.client_pid != controller["pid"] or self.held_identity(target, controller["pid"]) != controller:
            raise StorageError("Controller changed during witness transfer")
        for name, handle in (("root", self.k.GetCurrentProcess()), ("job", self.job)):
            if name not in record:
                value = self.w.HANDLE()
                self.require(self.k.DuplicateHandle(self.k.GetCurrentProcess(), handle, target,
                    ctypes.byref(value), 0, False, 2), "Duplicate exact " + name + " witness")
                record[name] = value.value
        return dict(record)

    def read_frame(self):
        buffer = ctypes.create_string_buffer(MAX_FRAME)
        count = self.w.DWORD()
        deadline = time.monotonic() + 5
        while not self.k.ReadFile(self.pipe, buffer, MAX_FRAME, ctypes.byref(count), None):
            if self.last_error() != 232 or time.monotonic() >= deadline:
                raise StorageError("Receipt broker frame unavailable/oversized")
            time.sleep(.005)
        if count.value == 0:
            raise StorageError("Empty receipt broker frame")
        return buffer.raw[:count.value]

    def cleanup_client(self):
        self.cleanup_admission()
        if self.disconnect_pending:
            disconnected = self.k.DisconnectNamedPipe(self.pipe)
            # Already-disconnected is a checked, idempotent cleanup result.
            if not disconnected and self.last_error() != 233:
                self.require(False, "Disconnect exact broker connection")
            self.disconnect_pending = False
        if self.client_handle is not None:
            self.close(self.client_handle)
            self.client_handle = None
            self.client_pid = None
        self.retry_pending_closes()

    def serve_once(self, owner):
        try:
            self.cleanup_client()
            if owner.shutdown.is_set():
                return
            connected = self.k.ConnectNamedPipe(self.pipe, None)
            error = 0 if connected else self.last_error()
            if not connected and error not in (535,):
                if error == 536:
                    return
                if error == 232:
                    self.disconnect_pending = True
                    self.cleanup_client()
                    return
                self.require(False, "Connect receipt broker")
            self.disconnect_pending = True
            try:
                pid = self.w.ULONG()
                self.require(self.k.GetNamedPipeClientProcessId(self.pipe, ctypes.byref(pid)), "Verify local broker client")
                # Pin the kernel-reported process before reading its request.
                # Claimed creation ticks must match this held handle; PID reuse
                # cannot redirect a saved request into a new process's table.
                self.client_handle = self.k.OpenProcess(0x40 | 0x1000, False, pid.value)
                self.require(self.client_handle, "Hold local pipe client generation")
                self.client_pid = pid.value
                count = self.w.DWORD()
                message = json.loads(self.read_frame().decode("utf-8"))
                try:
                    with self.admission(message, owner.spec):
                        check_client_identity(message, self.held_identity(self.client_handle, pid.value))
                        reply = owner.request(message, pid.value)
                except Exception as error:
                    reply = dict(error=str(error)[:2048])
                reply["handoff_nonce"] = secrets.token_hex(16)
                reply["broker_first_failure"] = self.first_failure
                reply["broker_last_failure"] = self.last_failure
                raw = json.dumps(reply, ensure_ascii=True, separators=(",", ":")).encode("utf-8")
                if len(raw) > MAX_FRAME:
                    raise StorageError("Receipt broker reply exceeded bound")
                self.require(self.k.WriteFile(self.pipe, raw, len(raw), ctypes.byref(count), None) and count.value == len(raw), "Buffer receipt handoff")
                ack = json.loads(self.read_frame().decode("utf-8"))
                if ack != {"ack_sha256": hash_bytes(raw)}:
                    raise StorageError("Receipt handoff acknowledgement mismatch")
                with self.admission(message, owner.spec):
                    check_client_identity(message, self.held_identity(self.client_handle, pid.value))
                    if not reply.get("error"):
                        owner.acknowledge(reply)
            finally:
                self.cleanup_client()
        except (OSError, ValueError, StorageError) as error:
            # Cleanup failure retains its exact handle/connection and diagnostic.
            # The next iteration retries cleanup before accepting another client.
            self.record_failure(error)

    def serve(self, owner):
        while True:
            self.serve_once(owner)
            if owner.shutdown.is_set() and not self.disconnect_pending and self.client_handle is None and not self.pending_closes:
                try:
                    self.close(self.pipe)
                    self.pipe = None
                    return
                except OSError as error:
                    self.record_failure(error)
            time.sleep(.005)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", required=True)
    parser.add_argument("--job", required=True, type=int)
    arguments = parser.parse_args()
    if len(arguments.spec) > 12288:
        raise StorageError("Supervisor metadata exceeds bound")
    spec = json.loads(base64.b64decode(arguments.spec, validate=True))
    source_root = Path(__file__).absolute().parents[2]
    validate_spec(spec, source_root)
    native = Native(arguments.job, spec["operation"])
    if os.getppid() != spec["parent_owner"]["pid"] or native.identity(os.getppid()) != spec["parent_owner"]:
        raise StorageError("Supervisor actual original parent mismatch")
    owner = ReceiptOwner(spec, native)
    def work():
        raw = sys.stdin.buffer.read(MAX_BODY + 1)
        publish(spec, raw, native, owner)
    thread = threading.Thread(target=work, name="accounted-receipt", daemon=False)
    thread.start()
    native.serve(owner)
    thread.join()
    # Existing job/native process resources stay live until explicit witnessed
    # handoff. No job termination or implicit cleanup on original owner loss.
    return 0 if owner.terminal["state"] == "published" else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:
        # A pre-broker failure has no active accounting worker. The retained
        # launcher owns its exact native exit and startup-failure evidence.
        sys.exit(2)
