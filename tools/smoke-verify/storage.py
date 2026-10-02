"""Prospective smoke storage. Never imports/prunes historical installs.

New workspaces contain private ordinary files, never hardlinks/junctions to
source assets. The archive stores each content once and keeps complete recipes.
Only completed, unchanged, unpinned, new managed workspaces may be pruned.
"""
import argparse
import contextlib
import ctypes
import datetime as dt
import gzip
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import sys
import time
import uuid

CHECKOUT = Path(__file__).absolute().parents[2]
REPARSE = 0x400
spec = importlib.util.spec_from_file_location("pd_storage_io", CHECKOUT / "devtools/smoke-storage-manifest.py")
io = importlib.util.module_from_spec(spec)
spec.loader.exec_module(io)

class StorageError(RuntimeError):
    pass

def utc():
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="microseconds")

def plain(path):
    path = Path(os.path.abspath(path))
    for ancestor in [*reversed(path.parents), path]:
        if ancestor.exists() or ancestor.is_symlink():
            s = os.lstat(ancestor)
            if stat.S_ISLNK(s.st_mode) or getattr(s, "st_file_attributes", 0) & REPARSE:
                raise StorageError("Reparse/alias path rejected: " + str(ancestor))
    return path

def relative(name):
    p = PurePosixPath(name)
    if not name or p.is_absolute() or any(x in {"", ".", ".."} or ":" in x or "\\" in x for x in name.split("/")):
        raise StorageError("Invalid recipe path: " + name)
    return p

def walk(root):
    stack = [plain(root)]
    while stack:
        parent = stack.pop()
        with os.scandir(parent) as entries:
            for e in sorted(entries, key=lambda x: x.name):
                s = e.stat(follow_symlinks=False)
                if stat.S_ISLNK(s.st_mode) or getattr(s, "st_file_attributes", 0) & REPARSE:
                    raise StorageError("Reparse entry rejected: " + e.path)
                if stat.S_ISDIR(s.st_mode):
                    stack.append(Path(e.path))
                elif stat.S_ISREG(s.st_mode):
                    if s.st_nlink > 1:
                        raise StorageError("Hardlinked file rejected: " + e.path)
                    yield Path(e.path), s
                else:
                    raise StorageError("Non-regular entry rejected: " + e.path)

def signature(s):
    return [s.st_size, s.st_mtime_ns, s.st_dev, s.st_ino, s.st_nlink]

def json_gzip(path, value):
    plain(path.parent).mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    with gzip.open(temporary, "wt", encoding="utf-8", compresslevel=1) as f:
        json.dump(value, f, separators=(",", ":"), sort_keys=True)
    os.replace(temporary, path)

def read_gzip(path):
    with gzip.open(plain(path), "rt", encoding="utf-8") as f:
        return json.load(f)

def policy_from(value):
    defaults = json.loads((Path(__file__).parent / "storage-policy.json").read_text())
    if set(value or {}) - set(defaults) or (value or {}).get("schema", 1) != 1:
        raise StorageError("Unknown storage policy fields/schema")
    defaults.update(value or {})
    count = defaults["max_full_installs"]
    if isinstance(count, bool) or not isinstance(count, int) or count < 1:
        raise StorageError("max_full_installs must be a positive integer")
    for k in ["min_free_gib", "max_full_install_gib", "max_archive_gib", "peak_run_gib", "extra_growth_gib", "peak_build_gib"]:
        x = defaults[k]
        if isinstance(x, bool) or not isinstance(x, (int, float)) or not math.isfinite(x) or x < 0:
            raise StorageError("Invalid storage budget: " + k)
        defaults[k + "_bytes"] = int(x * 2**30)
    if not defaults["max_full_install_gib_bytes"] or not defaults["max_archive_gib_bytes"]:
        raise StorageError("Archive/full-install byte budgets must be positive")
    return defaults

def exclusive_file(path, delete=False, expected=None, metadata=None):
    """Refuse in-use files; deletion operates on a verified Windows handle."""
    path = plain(path)
    if os.name != "nt":
        if expected and hashlib.sha256(path.read_bytes()).hexdigest() != expected["sha256"]:
            raise StorageError("Changed file refused during retention")
        if delete: path.unlink()
        return
    from ctypes import wintypes as w
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    create = kernel.CreateFileW
    create.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, ctypes.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
    create.restype = w.HANDLE
    close = kernel.CloseHandle; close.argtypes = [w.HANDLE]
    final = kernel.GetFinalPathNameByHandleW
    final.argtypes = [w.HANDLE, w.LPWSTR, w.DWORD, w.DWORD]
    handle = create(str(path), 0x80000000 | (0x10000 if delete else 0), 0, None, 3, 0x00200000, None)
    if handle == ctypes.c_void_p(-1).value: raise ctypes.WinError(ctypes.get_last_error())
    try:
        buffer = ctypes.create_unicode_buffer(32768)
        length = final(handle, buffer, len(buffer), 0)
        resolved = buffer.value.removeprefix("\\\\?\\")
        if not length or length >= len(buffer) or os.path.normcase(resolved) != os.path.normcase(str(path.absolute())):
            raise StorageError("Opened deletion path differs from prospective workspace")
        if metadata and signature(os.stat(path, follow_symlinks=False)) != metadata:
            raise StorageError("File identity changed during retention")
        if expected:
            read = kernel.ReadFile
            read.argtypes = [w.HANDLE, ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD), ctypes.c_void_p]
            block = ctypes.create_string_buffer(262144); count = w.DWORD(); digest = hashlib.sha256()
            while True:
                if not read(handle, block, len(block), ctypes.byref(count), None):
                    raise ctypes.WinError(ctypes.get_last_error())
                if not count.value: break
                digest.update(block.raw[:count.value])
            if digest.hexdigest() != expected["sha256"]:
                raise StorageError("Changed file refused during retention")
        if delete:
            disposition = w.BOOL(1)
            set_info = kernel.SetFileInformationByHandle
            set_info.argtypes = [w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD]
            if not set_info(handle, 4, ctypes.byref(disposition), ctypes.sizeof(disposition)):
                raise ctypes.WinError(ctypes.get_last_error())
    finally:
        close(handle)

class Storage:
    def __init__(self, project, policy=None):
        self.project = plain(project)
        allowed = plain(CHECKOUT / ".claude")
        if self.project != CHECKOUT and not self.project.is_relative_to(allowed):
            raise StorageError("Isolated test project must be inside canonical .claude")
        self.root = plain(self.project / ".claude/smoke-storage")
        self.policy = policy_from(policy)
        self.state_path = plain(self.root / "state.json")
        self.state = {"schema": 1, "installs": {}, "blobs": {}}
        self.validated = set()
        self.new_blob_bytes = 0
        self.archive_used = None

    @contextlib.contextmanager
    def locked(self):
        self.root.mkdir(parents=True, exist_ok=True)
        path = plain(self.root / "storage.lock")
        with open(path, "a+b") as lock:
            lock.seek(0, 2)
            if not lock.tell(): lock.write(b"0"); lock.flush()
            deadline = time.monotonic() + 60
            if os.name == "nt":
                import msvcrt
                while True:
                    try:
                        lock.seek(0); msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
                        break
                    except OSError:
                        if time.monotonic() >= deadline: raise StorageError("Another storage operation owns the lock")
                        time.sleep(.1)
            else:
                import fcntl
                fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
            try:
                if self.state_path.exists(): self.state = json.loads(self.state_path.read_text(encoding="utf-8"))
                if self.state.get("schema") != 1: raise StorageError("Unknown prospective storage registry")
                self.archive_used = self.usage()
                yield self
            finally:
                if os.name == "nt":
                    lock.seek(0); msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)

    def save(self):
        temporary = self.state_path.with_suffix(".tmp")
        temporary.write_text(json.dumps(self.state, separators=(",", ":")), encoding="utf-8")
        os.replace(temporary, self.state_path)

    def usage(self):
        total = 0
        for name in ["blobs", "seeds", "receipts"]:
            base = self.root / name
            if base.exists(): total += sum(s.st_size for _, s in walk(base))
        return total

    def preflight(self, pending=0, archive_pending=0):
        if not isinstance(pending, int) or not isinstance(archive_pending, int) or pending < 0 or archive_pending < 0:
            raise StorageError("Pending budgets must be nonnegative integer bytes")
        floor = self.policy["min_free_gib_bytes"]
        growth = self.policy["extra_growth_gib_bytes"]
        free = shutil.disk_usage(self.root).free
        if free - pending - growth < floor:
            raise StorageError(f"Disk preflight refused: free={free}, pending={pending}, reserve={growth}, floor={floor}; no evidence evicted")
        if self.archive_used is None: self.archive_used = self.usage()
        used = self.archive_used
        if used + archive_pending > self.policy["max_archive_gib_bytes"]:
            raise StorageError(f"Archive budget refused: used={used}, pending={archive_pending}; existing evidence preserved")
        return {"free_bytes": free, "archive_bytes": used, "pending_bytes": pending, "floor_bytes": floor}

    def blob_path(self, digest):
        if len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise StorageError("Invalid content digest")
        return plain(self.root / "blobs" / digest[:2] / (digest + ".gz"))

    def digest(self, path):
        path = plain(path)
        before = os.stat(path, follow_symlinks=False)
        if before.st_nlink > 1: raise StorageError("Hardlinked input rejected: " + str(path))
        with io.sequential_open(path) as f:
            io.verify_opened_path(f, Path(path))
            opened = os.fstat(f.fileno())
            digest = hashlib.file_digest(f, "sha256").hexdigest()
            if signature(opened) != signature(os.fstat(f.fileno())):
                raise StorageError("Input changed while reading: " + str(path))
        after = os.stat(path, follow_symlinks=False)
        if signature(before) != signature(after): raise StorageError("Input replaced while reading: " + str(path))
        return digest, after

    def verify_blob(self, digest):
        if digest in self.validated: return
        h = hashlib.sha256()
        with gzip.open(self.blob_path(digest), "rb") as f:
            while block := f.read(262144): h.update(block)
        if h.hexdigest() != digest: raise StorageError("Corrupt immutable archive blob: " + digest)
        self.validated.add(digest)

    def put(self, path):
        path = plain(path)
        digest, s = self.digest(path)
        destination = self.blob_path(digest)
        if destination.exists():
            self.verify_blob(digest)
            return {"sha256": digest, "bytes": s.st_size, "mtime_ns": s.st_mtime_ns}
        self.preflight(s.st_size * 2 + 1024, s.st_size + 1024)
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_suffix("." + uuid.uuid4().hex + ".tmp")
        try:
            with io.sequential_open(path) as source, gzip.open(temporary, "wb", compresslevel=1) as out:
                io.verify_opened_path(source, Path(path))
                copied = hashlib.sha256()
                while block := source.read(262144): copied.update(block); out.write(block)
            if copied.hexdigest() != digest or signature(s) != signature(os.stat(path, follow_symlinks=False)):
                raise StorageError("Input changed before archiving: " + str(path))
            # The global storage lock prevents another writer. Existing blobs never overwritten.
            os.rename(temporary, destination)
            stored = destination.stat().st_size
            self.new_blob_bytes += stored
            self.archive_used += stored
            self.state["blobs"][digest] = {"bytes": s.st_size, "stored_bytes": stored}
            self.validated.add(digest)
        finally:
            if temporary.exists(): temporary.unlink()  # Only our newly created prospective temp.
        return {"sha256": digest, "bytes": s.st_size, "mtime_ns": s.st_mtime_ns}

    def checkout(self, entry, destination):
        destination = plain(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        digest = entry["sha256"]
        h = hashlib.sha256()
        with gzip.open(self.blob_path(digest), "rb") as source, open(destination, "xb") as out:
            while block := source.read(262144): h.update(block); out.write(block)
        if h.hexdigest() != digest or destination.stat().st_size != entry["bytes"]:
            raise StorageError("Reconstruction hash mismatch: " + str(destination))
        os.utime(destination, ns=(entry["mtime_ns"], entry["mtime_ns"]))

    def capture(self, directory):
        answer = {}
        for path, _ in walk(directory):
            name = path.relative_to(directory).as_posix()
            if name == ".pd-storage-owner.json": continue
            answer[name] = self.put(path)
        return answer

    def read_seed(self, seed_id):
        if not isinstance(seed_id, str) or len(seed_id) != 64 or any(c not in "0123456789abcdef" for c in seed_id):
            raise StorageError("Invalid immutable seed identity")
        files = read_gzip(plain(self.root / "seeds" / (seed_id + ".json.gz")))
        if not isinstance(files, dict) or not files:
            raise StorageError("Invalid immutable seed manifest")
        actual = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        if actual != seed_id:
            raise StorageError("Immutable seed manifest identity mismatch")
        for name, entry in files.items():
            relative(name)
            if not isinstance(entry, dict) or set(entry) != {"sha256", "bytes", "mtime_ns"}:
                raise StorageError("Invalid immutable seed file entry")
            digest = entry["sha256"]
            if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
                raise StorageError("Invalid immutable seed blob identity")
            if any(type(entry[k]) is not int or entry[k] < 0 for k in ("bytes", "mtime_ns")):
                raise StorageError("Invalid immutable seed byte/mtime entry")
            if self.state["blobs"].get(digest, {}).get("bytes") != entry["bytes"]:
                raise StorageError("Immutable seed blob size mismatch")
            self.verify_blob(digest)
        return files

    def inspect(self, directory):
        content, metadata = {}, {}
        for path, s in walk(directory):
            name = path.relative_to(directory).as_posix()
            if name == ".pd-storage-owner.json": continue
            digest, actual = self.digest(path)
            content[name] = {"sha256": digest, "bytes": actual.st_size, "mtime_ns": actual.st_mtime_ns}
            metadata[name] = signature(actual)
        return content, metadata

    def recipe(self, install):
        receipt = read_gzip(self.root / "receipts" / install["id"] / "recipe.json.gz")
        if receipt.get("complete") is not True: raise StorageError("Incomplete reconstruction receipt")
        seed = read_gzip(self.root / "seeds" / (receipt["seed"] + ".json.gz"))
        files = dict(seed)
        for name in receipt["deleted"]: files.pop(name, None)
        files.update(receipt["changed"])
        for name, entry in files.items(): relative(name); self.verify_blob(entry["sha256"])
        self.verify_blob(receipt["definition"]["sha256"])
        if receipt.get("provenance", {}).get("source_patch"):
            self.verify_blob(receipt["provenance"]["source_patch"]["sha256"])
        return files, receipt

    def install_path(self, install):
        name = str(relative(install["path"]))
        directory = plain(self.root / name)
        if not any(directory.is_relative_to(self.root / x) for x in ["installs", "shared", "restored"]):
            raise StorageError("Registry path outside prospective workspace areas")
        return directory

    def eligible(self, install, allow_pin=False):
        if install["status"] != "completed" or (install.get("pin") and not allow_pin): return False
        directory = self.install_path(install)
        marker = json.loads(plain(directory / ".pd-storage-owner.json").read_text())
        if marker.get("id") != install["id"]: raise StorageError("Managed workspace owner marker mismatch")
        expected, receipt = self.recipe(install)
        actual, metadata = self.inspect(directory)
        if actual != expected or metadata != receipt["metadata"]:
            raise StorageError("Completed workspace changed; preserve and review: " + str(directory))
        return True

    def remove(self, install):
        if not self.eligible(install): raise StorageError("Workspace is active or pinned; no removal")
        directory = self.install_path(install)
        if not directory.is_relative_to(self.root / "installs"):
            raise StorageError("Pruning restricted to newly registered private installs")
        expected, receipt = self.recipe(install)
        paths = [p for p, _ in walk(directory)]
        if {p.relative_to(directory).as_posix() for p in paths} != set(expected) | {".pd-storage-owner.json"}:
            raise StorageError("Workspace file set changed during retention")
        # Probe every file first. Recheck the opened canonical path when deleting;
        # a racing lock fails closed with the complete recipe still retained.
        for path in paths: exclusive_file(path)
        try:
            for path in paths:
                name = path.relative_to(directory).as_posix()
                exclusive_file(path, delete=True, expected=expected.get(name), metadata=receipt["metadata"].get(name))
        except (OSError, StorageError):
            install.update(status="retention_blocked", pin=True,
                           full_bytes=sum(s.st_size for _, s in walk(directory)))
            self.save()
            raise
        for parent, dirs, _ in os.walk(directory, topdown=False, followlinks=False):
            for name in dirs: plain(Path(parent) / name).rmdir()
        directory.rmdir()
        install["status"] = "compacted"
        install["full_bytes"] = 0

    def make_room(self, required):
        limit_count = self.policy["max_full_installs"]
        limit_bytes = self.policy["max_full_install_gib_bytes"]
        while True:
            full = [x for x in self.state["installs"].values() if x["status"] != "compacted"]
            if len(full) + 1 <= limit_count and sum(x["full_bytes"] for x in full) + required <= limit_bytes:
                return
            # Prefer retaining the newest successful copy; older successes and
            # then oldest failures yield slots first. Receipts never pruned.
            successes = [x for x in full if x["status"] == "completed" and x.get("passed")]
            latest_success = max(successes, key=lambda x: x["created"])["id"] if successes else None
            victims = sorted((x for x in full if x["status"] == "completed" and not x.get("pin")
                              and self.install_path(x).is_relative_to(self.root / "installs")),
                             key=lambda x: (2 if x["id"] == latest_success else 0 if x.get("passed") else 1, x["created"]))
            if not victims: raise StorageError("Full-install budget blocked by active/pinned evidence; no new copy")
            self.remove(victims[0]); self.save()

    def seed(self, request):
        started = time.perf_counter()
        phases = {}
        self.preflight(self.policy["peak_run_gib_bytes"])
        files = {}
        if request.get("seed_id"):
            if any(request.get(k) for k in ("template", "sources", "data_source", "rom_id")):
                raise StorageError("Immutable seed cannot be combined with mutable source inputs")
            files = self.read_seed(request["seed_id"])
            binary = "PerfectDarkServer.exe" if request.get("target") == "pd-server" else "PerfectDark.exe"
            if binary not in files:
                raise StorageError("Immutable seed does not contain the requested target binary")
        elif request.get("template"):
            template = plain(request["template"])
            if template.is_relative_to(self.root) and not any(template.is_relative_to(self.root / x) for x in ["installs", "shared", "restored"]):
                raise StorageError("The archive itself cannot be an input template")
            for path, _ in walk(template):
                name = path.relative_to(template).as_posix()
                if name == ".pd-storage-owner.json" or name.startswith("logs/") or name.endswith(".log"): continue
                files[name] = self.put(path)
        else:
            for source, destination in request["sources"]:
                files[str(relative(destination))] = self.put(plain(source))
            if request.get("data_source"):
                data = plain(request["data_source"])
                for path, _ in walk(data):
                    name = "data/" + request["rom_id"] + "/" + path.relative_to(data).as_posix()
                    files[str(relative(name))] = self.put(path)
        seed_digest = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        seed_path = self.root / "seeds" / (seed_digest + ".json.gz")
        if not seed_path.exists():
            length = len(json.dumps(files).encode())
            self.preflight(length + 1024, length + 1024)
            json_gzip(seed_path, files)
            self.archive_used += seed_path.stat().st_size
            self.preflight()
        actual_bytes = sum(x["bytes"] for x in files.values())
        phases["source_snapshot_seconds"] = time.perf_counter() - started
        transition_started = time.perf_counter()
        reserve = max(actual_bytes, self.policy["peak_run_gib_bytes"])
        self.preflight(reserve)
        shared = bool(request.get("shared"))
        target = "server" if request.get("target") == "pd-server" else "client"
        directory = self.root / "shared" / target if shared else None
        previous = next((x for x in self.state["installs"].values()
                         if directory and x["status"] != "compacted" and self.install_path(x) == directory), None)
        reusable = None
        if directory and directory.exists():
            if previous is None: raise StorageError("Unregistered shared workspace; preserve existing files")
            if not self.eligible(previous, allow_pin=True): raise StorageError("Shared workspace is active or incomplete")
            if previous.get("passed") and not previous.get("pin"):
                for path, _ in walk(directory): exclusive_file(path)
                reusable, _ = self.recipe(previous)
                previous["status"] = "compacted"; previous["full_bytes"] = 0
            else:
                for path, _ in walk(directory): exclusive_file(path)
                retained = self.root / "installs" / previous["id"]
                retained.parent.mkdir(parents=True, exist_ok=True)
                os.rename(directory, retained)
                previous["path"] = retained.relative_to(self.root).as_posix()
        self.make_room(reserve)
        identifier = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ-") + re_safe(request["name"]) + "-" + uuid.uuid4().hex[:8]
        if directory is None: directory = self.root / "installs" / identifier
        directory = plain(directory)
        directory.mkdir(parents=True, exist_ok=True)
        copied = reused = 0
        record = {"id": identifier, "path": directory.relative_to(self.root).as_posix(), "seed": seed_digest,
                  "created": utc(), "status": "active", "owner_pid": request.get("owner_pid"),
                  "full_bytes": reserve, "pin": False, "shared": shared}
        self.state["installs"][identifier] = record
        self.save()  # Reserve before any copy; interrupted work remains bounded and protected.
        phases["workspace_transition_seconds"] = time.perf_counter() - transition_started
        checkout_started = time.perf_counter()
        if reusable is not None:
            for name, entry in reusable.items():
                if files.get(name) != entry: exclusive_file(plain(directory / name), delete=True)
            marker = directory / ".pd-storage-owner.json"
            if marker.exists(): exclusive_file(marker, delete=True)
            for parent, dirs, _ in os.walk(directory, topdown=False, followlinks=False):
                for name in dirs:
                    child = plain(Path(parent) / name)
                    if not any(child.iterdir()): child.rmdir()
        for name, entry in files.items():
            destination = directory / str(relative(name))
            if reusable is not None and reusable.get(name) == entry:
                reused += entry["bytes"]
            else:
                self.checkout(entry, destination); copied += entry["bytes"]
        marker = {"schema": 1, "id": identifier, "prospective": True}
        (directory / ".pd-storage-owner.json").write_text(json.dumps(marker), encoding="utf-8")
        (self.root / "receipts" / identifier / "artifacts").mkdir(parents=True, exist_ok=True)
        self.state["installs"][identifier] = record; self.save()
        phases["checkout_seconds"] = time.perf_counter() - checkout_started
        phases["total_seconds"] = time.perf_counter() - started
        return {"InstallDir": str(directory), "StorageId": identifier,
                "StorageReceiptDir": str(self.root / "receipts" / identifier), "StorageSeed": seed_digest,
                "StorageCopiedBytes": copied, "StorageReusedBytes": reused,
                "StorageNewArchiveBytes": self.new_blob_bytes, "StorageSeedPhases": phases}

    def complete(self, request):
        install = self.state["installs"][request["id"]]
        if install["status"] != "active": raise StorageError("Only the owning active run may complete")
        if install.get("owner_pid") != request.get("owner_pid"): raise StorageError("Completion owner mismatch")
        directory = self.install_path(install)
        marker = json.loads(plain(directory / ".pd-storage-owner.json").read_text())
        if marker.get("id") != install["id"]: raise StorageError("Completion owner marker mismatch")
        files = self.capture(directory)
        actual, metadata = self.inspect(directory)
        if actual != files: raise StorageError("Workspace changed during evidence capture; leave active/preserved")
        seed = read_gzip(self.root / "seeds" / (install["seed"] + ".json.gz"))
        receipt_dir = self.root / "receipts" / install["id"]
        definition = plain(request["definition"])
        definition_record = self.put(definition)
        patch = subprocess.run(["git", "diff", "--binary", "HEAD", "--", "src", "port", "include", "tests", "CMakeLists.txt", "tools/smoke-verify"],
                               cwd=self.project, capture_output=True)
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=self.project, capture_output=True, text=True)
        provenance = {"git_head": head.stdout.strip() if head.returncode == 0 else None,
                      "git_dirty_patch_available": patch.returncode == 0,
                      "binary_reproduction": "Exact binary bytes are in the recipe; HEAD/patch is diagnostic provenance, not a claim about the binary build"}
        if patch.returncode == 0:
            self.preflight(len(patch.stdout) + 1024)
            patch_path = plain(receipt_dir / ("source-diff-" + uuid.uuid4().hex + ".tmp"))
            try:
                patch_path.write_bytes(patch.stdout)
                provenance["source_patch"] = self.put(patch_path)
            finally:
                if patch_path.exists(): exclusive_file(patch_path, delete=True)
        outcome = request.get("outcome", {})
        recipe = {"schema": 1, "complete": True, "id": install["id"], "seed": install["seed"],
                  "changed": {n: e for n, e in files.items() if seed.get(n) != e},
                  "deleted": sorted(seed.keys() - files.keys()), "metadata": metadata,
                  "definition": definition_record, "outcome": outcome, "provenance": provenance, "completed": utc()}
        serialized = json.dumps(recipe, separators=(",", ":")).encode()
        self.preflight(len(serialized) + 1024, len(serialized) + 1024)
        json_gzip(receipt_dir / "recipe.json.gz", recipe)
        self.archive_used += (receipt_dir / "recipe.json.gz").stat().st_size
        log_files = []
        for name, entry in files.items():
            if name.endswith(".log"):
                destination = receipt_dir / "logs" / str(relative(name))
                self.preflight(entry["bytes"], entry["bytes"])
                if not destination.exists(): self.checkout(entry, destination)
                self.archive_used += entry["bytes"]
                log_files.append(str(destination))
        outcome_length = len(json.dumps(outcome).encode()) + 1024
        self.preflight(outcome_length + definition_record["bytes"], outcome_length + definition_record["bytes"])
        json_gzip(receipt_dir / "outcome.json.gz", outcome)
        definition_path = receipt_dir / "definition.json"
        self.checkout(definition_record, definition_path)
        self.archive_used = self.usage()
        self.preflight()
        install.update(status="completed", passed=bool(request["passed"]), pin=bool(request.get("keep")),
                       full_bytes=sum(e["bytes"] for e in files.values()), completed=utc())
        self.save()
        return {"Receipt": str(receipt_dir / "recipe.json.gz"), "LogFiles": log_files,
                "FullInstallRetained": True, "ArchiveBytes": self.archive_used, "NewArchiveBytes": self.new_blob_bytes}

    def restore(self, identifier, destination):
        install = self.state["installs"][identifier]
        files, _ = self.recipe(install)
        destination = plain(destination)
        if not destination.is_relative_to(self.root / "restored"):
            raise StorageError("Restore destination must be a new directory under prospective storage/restored")
        if destination.exists(): raise StorageError("Restore never overwrites an existing destination")
        self.preflight(sum(x["bytes"] for x in files.values()))
        full_bytes = sum(x["bytes"] for x in files.values())
        self.make_room(full_bytes)
        destination.mkdir(parents=True)
        restored_id = "restore-" + uuid.uuid4().hex
        self.state["installs"][restored_id] = {"id": restored_id, "created": utc(), "status": "restoring",
            "pin": True, "full_bytes": full_bytes, "seed": install["seed"], "source_receipt": identifier,
            "path": destination.relative_to(self.root).as_posix()}
        self.save()
        for name, entry in files.items(): self.checkout(entry, destination / str(relative(name)))
        self.state["installs"][restored_id]["status"] = "restored"
        self.save()
        return {"Restored": str(destination), "Files": len(files)}

def re_safe(name):
    import re
    return re.sub(r"[^A-Za-z0-9._-]+", "-", name)[:100]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["seed", "complete", "preflight", "status", "restore", "dry-run"])
    args = parser.parse_args()
    request = json.load(sys.stdin)
    if os.name == "nt":
        kernel = ctypes.WinDLL("kernel32")
        kernel.GetCurrentProcess.restype = ctypes.c_void_p
        kernel.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        kernel.SetPriorityClass(kernel.GetCurrentProcess(), 0x4000)
    storage = Storage(request["project"], request.get("policy"))
    readonly = args.action in {"status", "dry-run"}
    if readonly and storage.state_path.exists(): storage.state = json.loads(storage.state_path.read_text(encoding="utf-8"))
    with (contextlib.nullcontext(storage) if readonly else storage.locked()):
        if args.action == "seed": result = storage.seed(request)
        elif args.action == "complete": result = storage.complete(request)
        elif args.action == "restore": result = storage.restore(request["id"], Path(request["destination"]))
        elif args.action == "preflight": result = storage.preflight(request.get("pending_bytes", 0), request.get("archive_pending_bytes", 0))
        else:
            rows = list(storage.state["installs"].values())
            result = {"ArchiveBytes": storage.usage(), "Installs": rows, "Policy": storage.policy,
                      "HistoricalPruning": False, "DeletionPerformed": False}
            if args.action == "dry-run":
                result["CandidateVerification"] = "Inventory only; retention re-verifies complete recipes, every file, and exclusive handles before removal"
                result["Candidates"] = [{"id": x["id"], "path": str(storage.root / x["path"]), "bytes": x["full_bytes"]}
                                         for x in rows if x["status"] == "completed" and not x.get("pin")]
        print(json.dumps(result, ensure_ascii=True))

if __name__ == "__main__":
    try: main()
    except (OSError, ValueError, KeyError, StorageError) as error:
        print("Smoke storage refused: " + str(error), file=sys.stderr)
        sys.exit(2)
