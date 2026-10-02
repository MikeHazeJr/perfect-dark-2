#!/usr/bin/env python3
"""Prepare an exact historical .claude cleanup manifest. Never deletes files.

Every proposed duplicate is hashed in full, with a retained same-content witness.
Unique source assets/binaries and all evidence remain in place. Compiler products
are separately classified as rebuildable. All targets stay under canonical .claude;
reparse points, tracked files, current output and changed files fail closed.
"""
import argparse
import collections
import ctypes
import datetime as dt
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import time

CANONICAL = Path(r"C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike")
CUTOFF = dt.datetime(2026, 9, 1, tzinfo=dt.timezone.utc).timestamp()
RUN_NAME = re.compile(r"^20260[5-8]\d{2}T\d{6}Z-.+$")
PRODUCT_EXTENSIONS = {".obj", ".gch"}
EVIDENCE_EXTENSIONS = {
    ".log", ".dmp", ".md", ".txt", ".bmp", ".png", ".jpg", ".jpeg", ".gif",
    ".ini", ".cfg", ".conf", ".toml", ".yaml", ".yml", ".sav", ".save", ".pak",
    ".eeprom", ".sra", ".fla", ".state", ".json", ".jsonl", ".ndjson", ".xml",
    ".csv", ".tsv", ".out", ".err", ".trace", ".exe", ".dll", ".pdb",
}
REPARSE = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)

if os.name == "nt":
    import msvcrt
    # Initialize once: loading the API DLL for each small file is very costly.
    KERNEL = ctypes.WinDLL("kernel32", use_last_error=True)
    CREATE_FILE = KERNEL.CreateFileW
    CREATE_FILE.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                           ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
    CREATE_FILE.restype = ctypes.c_void_p
    FINAL_PATH = KERNEL.GetFinalPathNameByHandleW
    FINAL_PATH.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32]
    FINAL_PATH.restype = ctypes.c_uint32
    KERNEL.CloseHandle.argtypes = [ctypes.c_void_p]


def stamp():
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")


def identity(s):
    return (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns)


def verify_opened_path(stream, expected_path):
    if os.name != "nt":
        return
    buffer = ctypes.create_unicode_buffer(32768)
    length = FINAL_PATH(msvcrt.get_osfhandle(stream.fileno()), buffer, len(buffer), 0)
    if not length or length >= len(buffer):
        raise RuntimeError(f"Could not verify opened handle path: {expected_path}")
    actual = buffer.value.removeprefix("\\\\?\\")
    if os.path.normcase(actual) != os.path.normcase(str(expected_path)):
        raise RuntimeError(f"Opened file traversed an alias/reparse point: {expected_path} -> {actual}")


def sequential_open(path):
    """Ordinary OS access checks, with a sequential-read hint and no CRT path probe."""
    if os.name != "nt":
        return open(path, "rb")
    handle = CREATE_FILE(str(path), 0x80000000, 7, None, 3, 0x08000000, None)
    if handle == ctypes.c_void_p(-1).value or handle is None:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        descriptor = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except BaseException:
        KERNEL.CloseHandle(handle)
        raise
    try:
        return os.fdopen(descriptor, "rb")
    except BaseException:
        os.close(descriptor)
        raise


def check_plain(path):
    s = os.lstat(path)
    if stat.S_ISLNK(s.st_mode) or getattr(s, "st_file_attributes", 0) & REPARSE:
        raise RuntimeError(f"Reparse point rejected: {path}")
    return s


def checked_root():
    checkout = Path(__file__).absolute().parent.parent
    if os.path.normcase(str(checkout.resolve())) != os.path.normcase(str(CANONICAL)):
        raise RuntimeError("This preparation is restricted to the canonical checkout")
    for part in [*reversed(CANONICAL.parents), CANONICAL, CANONICAL / ".claude"]:
        check_plain(part)
    return CANONICAL / ".claude"


def contained(path, root):
    absolute = Path(os.path.abspath(path))
    if os.path.commonpath([os.path.normcase(str(root)),
                           os.path.normcase(str(absolute))]) != os.path.normcase(str(root)):
        raise RuntimeError(f"Target outside canonical .claude: {path}")
    return absolute


def walk_files(root):
    """Check each directory once and every leaf using cached no-follow metadata."""
    check_plain(root)
    stack = [Path(root)]
    while stack:
        parent = stack.pop()
        check_plain(parent)
        with os.scandir(parent) as entries:
            for entry in entries:
                s = entry.stat(follow_symlinks=False)
                if stat.S_ISLNK(s.st_mode) or getattr(s, "st_file_attributes", 0) & REPARSE:
                    raise RuntimeError(f"Reparse point rejected: {entry.path}")
                if stat.S_ISDIR(s.st_mode):
                    stack.append(Path(entry.path))
                elif stat.S_ISREG(s.st_mode):
                    yield Path(entry.path), s
                else:
                    raise RuntimeError(f"Nonregular file rejected: {entry.path}")


class Hasher:
    def __init__(self, rate):
        self.start = time.monotonic()
        self.rate = rate * 1024 * 1024
        self.bytes = 0
        self.files = 0
        self.last_progress = 0

    def digest(self, path, expected):
        digest = hashlib.sha256()
        with sequential_open(path) as stream:
            opened = os.fstat(stream.fileno())
            # Windows scandir caches size/time but reports inode/device as zero.
            if (opened.st_size, opened.st_mtime_ns) != (expected.st_size, expected.st_mtime_ns):
                raise RuntimeError(f"File changed before hashing: {path}")
            verify_opened_path(stream, path)
            while block := stream.read(1024 * 1024):
                digest.update(block)
                self.bytes += len(block)
                delay = self.bytes / self.rate - (time.monotonic() - self.start)
                if delay > 0:
                    time.sleep(min(delay, 0.05))
            if identity(os.fstat(stream.fileno())) != identity(opened):
                raise RuntimeError(f"File changed while hashing: {path}")
            if identity(check_plain(path)) != identity(opened):
                raise RuntimeError(f"File replaced while hashing: {path}")
        self.files += 1
        now = time.monotonic()
        if now - self.last_progress > 10:
            print(json.dumps({"event": "hash_progress", "files": self.files,
                              "GiB_read": round(self.bytes / 2**30, 2),
                              "elapsed_seconds": round(now - self.start)}), flush=True)
            self.last_progress = now
        return digest.hexdigest(), opened


def process_snapshot():
    command = ("Get-Process | Where-Object { $_.ProcessName -match "
               "'PerfectDark|pd-tests|ninja|cc1|gcc|ffmpeg' } | "
               "Select-Object Id,ProcessName,Path | ConvertTo-Json -Compress")
    p = subprocess.run(["powershell.exe", "-NoProfile", "-Command", command],
                       capture_output=True, text=True, timeout=20)
    if p.returncode:
        raise RuntimeError(f"Process snapshot failed: {p.stderr.strip()}")
    return json.loads(p.stdout) if p.stdout.strip() else []


def tracked_files():
    p = subprocess.run(["git", "ls-files", "-z", "--", ".claude"],
                       cwd=CANONICAL, capture_output=True, check=True)
    return {os.path.normcase(str(CANONICAL / os.fsdecode(x)))
            for x in p.stdout.split(b"\0") if x}


def write_row(stream, row):
    stream.write(json.dumps(row, separators=(",", ":")) + "\n")


def prepare(args):
    root = checked_root()
    active = process_snapshot()
    if active:
        raise RuntimeError(f"Game/test/build activity blocks preparation: {active}")
    if os.name == "nt":
        # Only our own process: keep the serial audit below engineering CPU work.
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)
    tracked = tracked_files()
    output_parent = root / "cleanup-manifests"
    output_parent.mkdir(exist_ok=True)
    check_plain(output_parent)
    output = output_parent / dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output.mkdir()  # Never overwrite a previous approval artifact.
    hasher = Hasher(args.max_read_mib_per_second)
    counts = collections.defaultdict(lambda: {"files": 0, "bytes": 0})
    preserved = collections.defaultdict(lambda: {"files": 0, "bytes": 0})
    witnesses = {}
    runs = []
    builds = []
    for e in sorted(os.scandir(root / "smoke-verify-runs"), key=lambda x: x.name):
        if RUN_NAME.fullmatch(e.name):
            check_plain(e.path)
            if not e.is_dir(follow_symlinks=False):
                raise RuntimeError(f"Historical run is not a directory: {e.path}")
            if max((s.st_mtime for _, s in walk_files(e.path)), default=0) < CUTOFF:
                runs.append(Path(e.path))
    for e in sorted(os.scandir(root / "session-builds"), key=lambda x: x.name):
        if e.name.startswith(".") or not e.is_dir(follow_symlinks=False):
            continue
        check_plain(e.path)
        if max((s.st_mtime for _, s in walk_files(e.path)), default=0) < CUTOFF:
            builds.append(Path(e.path))
    header = {"schema": "pd2-cleanup-manifest-v1", "created_utc": stamp(),
              "canonical_root": str(root), "cutoff_utc": "2026-09-01T00:00:00Z",
              "approval_required": True, "deletion_performed": False,
              "historical_runs": [p.name for p in runs],
              "historical_builds": [p.name for p in builds],
              "process_snapshot_before": active,
              "open_handle_inspection": "unavailable; process snapshots are not proof of non-use",
              "revalidate_before_deletion": ["canonical containment and no reparse ancestors",
                 "exact manifest SHA-256 approved", "each target content/hash/identity unchanged",
                 "each retained witness exists and matches", "no current owner/game/build use",
                 "delete individual files only; no directory-recursive deletion"]}
    (output / "scope.json").write_text(json.dumps(header, indent=2) + "\n", encoding="utf-8")

    def record_keep(stream, path, s, digest, reason):
        preserved[reason]["files"] += 1
        preserved[reason]["bytes"] += s.st_size
        write_row(stream, {"path": str(path), "bytes": s.st_size,
                          "mtime_ns": s.st_mtime_ns, "sha256": digest, "reason": reason})

    def seed(path, stream):
        s = check_plain(path)
        digest, s = hasher.digest(path, s)
        key = (s.st_size, digest)
        if key not in witnesses:
            witnesses[key] = str(path)
            record_keep(stream, path, s, digest, "retained_baseline")

    with gzip.open(output / "delete-files.jsonl.gz", "wt", encoding="utf-8", compresslevel=1) as delete, \
         gzip.open(output / "retained-witnesses.jsonl.gz", "wt", encoding="utf-8", compresslevel=1) as keep:
        # Preserve these baselines; they are never deletion candidates.
        seed(CANONICAL / "Build" / "pd.ntsc-final.z64", keep)
        for base in [root / "smoke-verify-cache" / "ntsc-final",
                     root / "smoke-verify-install" / "data"]:
            if base.is_dir():
                for path, _ in walk_files(base):
                    seed(path, keep)
        binary_bases = [root / "source-freeze", root / "v009visual-source"]
        for base in binary_bases:
            if base.is_dir():
                for path, _ in walk_files(base):
                    if path.suffix.lower() == ".exe":
                        seed(path, keep)
        for base in (root / "session-builds").iterdir():
            if base.is_dir():
                binary = base / "PerfectDark.exe"
                if binary.is_file():
                    check_plain(base)
                    seed(binary, keep)
        for binary in [root / "smoke-verify-install" / "PerfectDark.exe",
                       *sorted((root / "menu0927").glob("bin*/PerfectDark.exe"))]:
            if binary.is_file():
                seed(binary, keep)

        def candidate(path, s, category, rebuildable=False):
            contained(path, root)
            if os.path.normcase(str(path)) in tracked:
                record_keep(keep, path, s, None, "tracked_file")
                return
            digest, s = hasher.digest(path, s)
            key = (s.st_size, digest)
            witness = witnesses.get(key)
            if not rebuildable and witness is None:
                witnesses[key] = str(path)
                record_keep(keep, path, s, digest, "unique_" + category)
                return
            row = {"path": str(path), "bytes": s.st_size, "mtime_ns": s.st_mtime_ns,
                   "device": s.st_dev, "inode": s.st_ino, "links": s.st_nlink,
                   "sha256": digest, "category": category,
                   "reason": "rebuildable_cmake_product" if rebuildable else "verified_byte_duplicate"}
            if witness:
                row["retained_copy"] = witness
            counts[category]["files"] += 1
            counts[category]["bytes"] += s.st_size
            write_row(delete, row)

        for run in runs:
            print(json.dumps({"event": "verify_run", "run": run.name}), flush=True)
            for category in ["data", "mod-cache"]:
                base = run / category
                if base.is_dir():
                    for path, s in walk_files(base):
                        if path.suffix.lower() in EVIDENCE_EXTENSIONS:
                            record_keep(keep, path, s, None, "evidence_file")
                        else:
                            candidate(path, s, "asset_data" if category == "data" else "runtime_cache")
            for name, category in [("pd.ntsc-final.z64", "rom_copy"),
                                   ("PerfectDark.exe", "binary_copy")]:
                path = run / name
                if path.is_file():
                    candidate(path, check_plain(path), category)
        for build in builds:
            base = build / "CMakeFiles"
            if base.is_dir():
                for path, s in walk_files(base):
                    if path.suffix.lower() in PRODUCT_EXTENSIONS:
                        candidate(path, s, "compiler_product", rebuildable=True)
    manifest_path = output / "delete-files.jsonl.gz"
    with manifest_path.open("rb") as stream:
        manifest_hash = hashlib.file_digest(stream, "sha256").hexdigest()
    after = process_snapshot()
    summary = {**header, "finished_utc": stamp(), "manifest": str(manifest_path),
               "manifest_sha256": manifest_hash, "delete_categories": dict(counts),
               "preserved_categories": dict(preserved),
               "proposed_file_count": sum(v["files"] for v in counts.values()),
               "proposed_logical_bytes": sum(v["bytes"] for v in counts.values()),
               "hashed_file_count": hasher.files, "hashed_bytes": hasher.bytes,
               "process_snapshot_after": after,
               "ready_for_confirmation": not bool(after),
               "preservation_exclusions": ["all September/current output", "unique source/binary/cache content",
                   "all logs/results/screenshots/artifacts/mods/config/saves", "tracked content",
                   "shared install", "extraction baseline", "menu0927/source-freeze", "this manifest"]}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"event": "manifest_complete", "directory": str(output),
                      "files": summary["proposed_file_count"],
                      "GiB": round(summary["proposed_logical_bytes"] / 2**30, 3),
                      "sha256": manifest_hash}), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--max-read-mib-per-second", type=float, default=96,
                        help="One serial hasher; cap aggregate read rate (default 96 MiB/s).")
    args = parser.parse_args()
    if args.max_read_mib_per_second <= 0:
        parser.error("read rate must be positive")
    prepare(args)


if __name__ == "__main__":
    main()
