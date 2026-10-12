"""Runner output plumbing using the existing storage lock and budget domains."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import sys
import uuid

from storage import Storage, StorageError, plain, process_identity, row_retained, walk

SCHEMA = "pd2.smoke-runner-retention.v1"


class RunnerRetention(Storage):
    def __init__(self, project, policy=None):
        super().__init__(project, policy, {"retain_housekeeping": True})
        self.run_id = None
        self.last_budget_error = None

    def context(self, request):
        run_id = request["run_id"]
        if not isinstance(run_id, str) or not re.fullmatch(r"runner-[0-9a-f]{32}", run_id):
            raise StorageError("Invalid retained runner identity")
        self.run_id = run_id
        directory = plain(self.root / "receipts" / run_id)
        record = json.loads(plain(directory / "run-owner.json").read_text(encoding="utf-8"))
        if record.get("schema") != SCHEMA or record.get("run_id") != run_id or record.get("retain_housekeeping") is not True:
            raise StorageError("Retained runner owner marker mismatch")
        if record.get("owner") != process_identity(request["owner_pid"]):
            raise StorageError("Retained runner PID/creation identity mismatch")
        return directory, record

    def publish(self, destination, value, profile=None):
        destination = plain(destination)
        if destination.exists():
            raise StorageError("Retained runner output collision: " + str(destination))
        payload = json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.archive_used = self.usage()
        self.preflight(len(payload), 0 if profile else len(payload))
        if profile:
            self.profile_budget(profile, len(payload))
        candidate = destination.with_name(destination.name + "." + self.operation_id + "." + uuid.uuid4().hex + ".tmp")
        succeeded = False
        try:
            with open(candidate, "xb") as stream:
                stream.write(payload)
                stream.flush()
            self.archive_used = self.usage()
            if profile: self.profile_budget(profile)
            self.preflight()
            # Native Windows rename refuses a raced destination. No replacement.
            os.rename(candidate, destination)
            succeeded = True
        finally:
            self.archive_used = self.usage()
            try:
                if profile: self.profile_budget(profile)
                self.preflight()
            except (OSError, StorageError) as error:
                self.last_budget_error = str(error)
                if succeeded: raise
        return {"Path": str(destination), "Bytes": len(payload), "ArchiveBytes": self.archive_used,
                "AccountingDomain": "profile-reservation" if profile else "archive",
                "Retention": self.retention_metadata(), "RunId": self.run_id}

    def profile_budget(self, profile, pending=0):
        directory, record = profile
        reserved = record.get("reserved_bytes")
        if type(reserved) is not int or reserved < 0:
            raise StorageError("Invalid existing reuse profile reservation")
        used = sum(info.st_size for _, info in walk(directory))
        if used + pending > reserved:
            raise StorageError("Retained result exceeds existing reuse profile reservation")
        return used

    def new_run(self, request):
        owner = process_identity(request["owner_pid"])
        self.run_id = "runner-" + uuid.uuid4().hex
        directory = plain(self.root / "receipts" / self.run_id)
        record = {"schema": SCHEMA, "run_id": self.run_id, "owner": owner, "retain_housekeeping": True}
        length = len(json.dumps(record, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))
        self.preflight(length, length)
        directory.parent.mkdir(parents=True, exist_ok=True)
        directory.mkdir()  # Exclusive native mkdir; never adopt an existing root.
        published = self.publish(directory / "run-owner.json", record)
        return {"RunId": self.run_id, "RunRoot": str(directory), "ProjectRoot": str(self.project),
                "Owner": owner, "Retention": published["Retention"]}

    def write_run(self, request):
        directory, _ = self.context(request)
        name = request["name"]
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9._-]+\.json", name) or name == "run-owner.json":
            raise StorageError("Invalid retained runner output name")
        return self.publish(directory / name, request["value"])

    def write_profile(self, request):
        _, owner_record = self.context(request)
        identifier = request["reuse_id"]
        if not isinstance(identifier, str) or not re.fullmatch(r"reuse-[0-9a-f]{32}", identifier):
            raise StorageError("Invalid retained reuse identity")
        record = self.state.get("reuse_runs", {})[identifier]
        if record.get("status") != "active" or record.get("owner") != owner_record["owner"] or not row_retained(record):
            raise StorageError("Retained result requires this owner's active reuse lease")
        directory = plain(self.root / "profiles" / identifier)
        if record.get("path") != directory.relative_to(self.root).as_posix():
            raise StorageError("Registered reuse profile escaped its exact path")
        marker = json.loads(plain(directory / ".pd-reuse-owner.json").read_text(encoding="utf-8"))
        if marker != {"schema": 1, "id": identifier, "base_id": record["base_id"], "owner": record["owner"]}:
            raise StorageError("Reuse profile owner marker mismatch")
        destination = plain(directory / "profile" / "result.json")
        result = self.publish(destination, request["value"], (directory, record))
        result["ProfileBytes"] = self.profile_budget((directory, record))
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["new-run", "write-run-json", "write-profile-json"])
    args = parser.parse_args()
    request = json.loads(sys.stdin.buffer.read().decode("utf-8-sig"))
    # The public wire action belongs to its launching PowerShell caller.
    if type(request.get("owner_pid")) is not int or request["owner_pid"] != os.getppid():
        raise StorageError("Runner output helper requires its actual parent owner")
    storage = RunnerRetention(request["project"], request.get("policy"))
    kernel = ctypes.WinDLL("kernel32")
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    kernel.SetPriorityClass(kernel.GetCurrentProcess(), 0x4000)
    try:
        with storage.locked():
            if args.action == "new-run": result = storage.new_run(request)
            elif args.action == "write-run-json": result = storage.write_run(request)
            else: result = storage.write_profile(request)
    except (OSError, ValueError, KeyError, StorageError) as error:
        raise StorageError(str(error) + "; runner=" + str(storage.run_id) +
                           "; retention=" + json.dumps(storage.retention_metadata()) +
                           "; budget_error=" + str(storage.last_budget_error)) from error
    print(json.dumps(result, ensure_ascii=True))


if __name__ == "__main__":
    try: main()
    except (OSError, ValueError, KeyError, StorageError) as error:
        print("Retained runner output refused: " + str(error), file=sys.stderr)
        sys.exit(2)
