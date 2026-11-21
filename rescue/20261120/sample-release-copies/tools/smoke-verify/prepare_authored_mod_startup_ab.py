"""Prepare a private v0.1.105 mod-discovery smoke pair with pinned inputs."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import sys
import zipfile


RELEASE_COMMIT = "8556dc8de559ab9a1633af04476257d979f6c68b"
RELEASE_SHA256 = "4efa7f4f87d19de65364ae60038763d7d444bf58f6ad4282204f357b89ac5114"
MOD_IDS = {"blank_room", "crystal_pistol", "grid_two_rooms", "model_room", "needler"}
ARCHIVE_TYPES = {".pdmod", ".zip"}
WINDOWS_DEVICE_NAMES = {"CON", "PRN", "AUX", "NUL"} | {
    f"{prefix}{number}" for prefix in ("COM", "LPT") for number in range(1, 10)
}


def sha256_file(path):
    with path.open("rb") as stream:
        digest = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
        return digest.hexdigest()


def plain_stat(path):
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or (
        getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
    ):
        raise ValueError(f"Alias or reparse point: {path}")
    return info


def absolute_path(value):
    path = Path(value)
    if not path.is_absolute():
        raise ValueError(f"Expected an absolute path: {path}")
    return Path(os.path.abspath(path))


def check_ancestors(path):
    for parent in reversed(path.parents):
        if not stat.S_ISDIR(plain_stat(parent).st_mode):
            raise ValueError(f"Expected an ordinary directory: {parent}")


def read_json(data):
    def unique_pairs(pairs):
        document = {}
        for key, value in pairs:
            if key in document:
                raise ValueError(f"Duplicate JSON key: {key}")
            document[key] = value
        return document

    return json.loads(data.decode("utf-8-sig"), object_pairs_hook=unique_pairs)


def package_path(value):
    path = PurePosixPath(value)
    if (
        len(path.parts) not in (1, 2)
        or path.as_posix() != value
        or any(
            part.startswith(".")
            or part.endswith((" ", "."))
            or re.search(r'[\\<>:"|?*\x00-\x1f]', part)
            or part.lower().endswith(".legacy_backup")
            or part.split(".")[0].upper() in WINDOWS_DEVICE_NAMES
            for part in path.parts
        )
        or path.parts[0].lower() in {"shared", "inbox", "untrusted"}
    ):
        raise ValueError(f"Unsupported scan path: {value!r}")
    return path


def loose_entries(path):
    entries = []
    pending = [path]
    while pending:
        for entry in pending.pop().iterdir():
            rel = entry.relative_to(path).as_posix()
            mode = plain_stat(entry).st_mode
            if stat.S_ISDIR(mode):
                entries.append([rel, "directory"])
                pending.append(entry)
            elif stat.S_ISREG(mode):
                entries.append([rel, "file", sha256_file(entry)])
            else:
                raise ValueError(f"Unsupported loose mod member: {entry}")
    return sorted(entries)


def inspect_package(source, form):
    check_ancestors(source)
    source_mode = plain_stat(source).st_mode
    entries = None
    if form == "archive" and stat.S_ISREG(source_mode) and source.suffix.lower() in ARCHIVE_TYPES:
        digest = sha256_file(source)
        with zipfile.ZipFile(source) as archive:
            manifests = [
                entry for entry in archive.infolist() if entry.filename.lower() == "mod.json"
            ]
            if len(manifests) != 1 or manifests[0].filename != "mod.json":
                raise ValueError(f"Expected one root mod.json: {source}")
            if manifests[0].file_size > 1024 * 1024:
                raise ValueError(f"Oversize package manifest: {source}")
            manifest = archive.read(manifests[0])
            members = [entry.filename.replace("\\", "/") for entry in archive.infolist()
                       if not entry.is_dir()]
    elif form == "loose" and stat.S_ISDIR(source_mode):
        entries = loose_entries(source)
        tree = json.dumps(entries, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        digest = hashlib.sha256(tree).hexdigest()
        manifest_path = source / "mod.json"
        info = plain_stat(manifest_path)
        if not stat.S_ISREG(info.st_mode) or info.st_size > 1024 * 1024:
            raise ValueError(f"Expected an ordinary root mod.json: {source}")
        manifest = manifest_path.read_bytes()
        members = [entry[0] for entry in entries if entry[1] == "file"]
    else:
        raise ValueError(f"Package form does not match {source}: {form}")
    audio_ini_paths = sorted(name for name in members
                             if PurePosixPath(name).name.casefold() == "audio.ini")
    return digest, entries, read_json(manifest), hashlib.sha256(manifest).hexdigest(), audio_ini_paths


def verify_package(row, source):
    digest, entries, manifest, manifest_digest, audio_ini_paths = inspect_package(source, row["form"])
    if digest != row["sha256"].lower():
        raise ValueError(f"Approved SHA-256 does not match {source}")
    if not isinstance(manifest, dict) or any(
        manifest.get(field) != row[field] for field in ("id", "name", "version")
    ):
        raise ValueError(f"Approved id, name or version does not match {source}")
    return {
        "sha256": digest,
        "manifest_sha256": manifest_digest,
        "loose_tree_entries": entries,
        "audio_ini_paths": audio_ini_paths,
    }


def load_packages(inventory):
    check_ancestors(inventory)
    if not stat.S_ISREG(plain_stat(inventory).st_mode):
        raise ValueError(f"Expected an ordinary inventory file: {inventory}")
    raw = inventory.read_bytes()
    rows = read_json(raw)
    if not isinstance(rows, list) or len(rows) != len(MOD_IDS):
        raise ValueError("Supply one evidenced input for each of the five incident packages")
    paths = set()
    ids = set()
    packages = []
    for row in rows:
        required = ("id", "name", "version", "source", "installed_path", "form", "sha256", "evidence")
        if not isinstance(row, dict) or any(
            not isinstance(row.get(field), str) or not row[field].strip() for field in required
        ):
            raise ValueError(f"Each package needs these fields: {', '.join(required)}")
        mod_id = row["id"]
        if mod_id not in MOD_IDS or mod_id in ids:
            raise ValueError(f"Unexpected or duplicate package ID: {mod_id}")
        ids.add(mod_id)
        if mod_id == "crystal_pistol" and row["version"] != "0.2.0":
            raise ValueError("The incident Crystal Pistol version is 0.2.0")
        installed = package_path(row["installed_path"])
        folded = installed.as_posix().casefold()
        if any(
            folded == seen or folded.startswith(seen + "/") or seen.startswith(folded + "/")
            for seen in paths
        ):
            raise ValueError(f"Colliding inventory paths: {installed}")
        paths.add(folded)
        if row["form"] not in ("loose", "archive"):
            raise ValueError(f"Unsupported package form: {row['form']}")
        if row["form"] == "archive" and installed.suffix.lower() not in ARCHIVE_TYPES:
            raise ValueError(f"Unsupported installed archive extension: {installed}")
        if not re.fullmatch(r"[0-9a-fA-F]{64}", row["sha256"]):
            raise ValueError(f"Missing a complete approved SHA-256: {mod_id}")
        source = absolute_path(row["source"])
        verified = verify_package(row, source)
        # The disabled-only pair asserts no mount or audio-only admission.
        # Record nested members without treating them as scan-root manifests.
        if any(PurePosixPath(name).as_posix().casefold() == "audio.ini"
               for name in verified["audio_ini_paths"]):
            raise ValueError(f"Root audio.ini requires a separate comparison: {mod_id}")
        packages.append((row, source, installed, verified))
    return packages, hashlib.sha256(raw).hexdigest()


def fixture(repo, source, destination):
    return {"src": os.path.relpath(source, repo).replace(os.sep, "/"), "dst": destination}


def definition(name, packages, repo, payload, inventory_kind):
    count = len(packages)
    return {
        "scenario_name": name,
        "description": f"v0.1.105 startup with a matched empty selection, {inventory_kind} incident inventory.",
        "tags": ["boot", "modding"],
        "log_channel_mask": "all",
        "verbose": 0,
        "timeout_seconds": 360,
        "remove_paths": ["mods", "user-home"],
        "fixtures": [
            fixture(repo, payload / "mods-enabled.json", "user-home/mods-enabled.json"),
            fixture(repo, payload / name, "user-home/mods"),
        ],
        "boot_args": ["--no-update-check", "--no-net", "--portable", "--savedir", "./user-home"],
        "input_sequence": [
            {"at_ms": 0, "type": "wait_until", "condition": "title_sequence_ready",
             "timeout_ms": 240000, "stable_ms": 500},
            {"at_ms": 60000, "type": "exit"},
        ],
        "screenshots": [
            {"at_ms": 1000, "name": "title-ready"},
            {"at_ms": 30000, "name": "title-sequence"},
        ],
        "assertions": {
            "required_lines": [
                f"SMOKE: scenario={name} ",
                r"modmgr: scanning '.*user-home[/\\]mods' for mods",
                f"modmgr: scan complete -- {count} mods found",
                "modmgr: loaded mods-enabled.json in saved order",
                f"modmgr: initialized .* {count} mods, 0 enabled",
                "assetcatalog_scanner: scan complete, 0 mod components registered",
                r"SMOKE\.WAIT: satisfied condition=title_sequence_ready ",
                "SMOKE: result=scripted_exit",
                *[
                    rf"modmgr: discovered mod \[\d+\] '{re.escape(row['id'])}' "
                    for row, _, _, _ in packages
                ],
            ],
            "required_counts": [
                {"pattern": r"modmgr: discovered mod \[\d+\]", "min": count, "max": count}
            ],
            "forbidden_patterns": [
                "modmgr: loading mod '",
                r"modmgr: parsed audio\.ini for",
                "MIGRATE: scanning .* for legacy folder mods",
                "assetcatalog_scanner: scan complete, [1-9][0-9]* mod components registered",
                "assetcatalog_scanner: archive .* component descriptors registered",
                "EXCEPTION_ACCESS_VIOLATION",
                r"CRASH\.DIAG",
                "FATAL: ",
                "SMOKE: result=(?:timeout|unclean_exit)",
            ],
        },
    }


def write_json(path, value):
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def prepare(repo, inventory, output, *, inventory_kind):
    check_ancestors(repo)
    check_ancestors(output)
    runner = repo / "tools/smoke-verify/run.ps1"
    if not runner.is_file():
        raise ValueError(f"Expected the canonical checkout with its smoke runner: {repo}")
    if output.is_relative_to(repo) or output.drive.casefold() != repo.drive.casefold():
        raise ValueError("Use a fresh scratch directory outside the checkout on the same drive")
    packages, input_digest = load_packages(inventory)
    if any(output.is_relative_to(source) for _, source, _, _ in packages):
        raise ValueError("Scratch output must be outside every package input")
    output.mkdir()
    test_dir = output / "tests"
    payload = output / "payload"
    test_dir.mkdir()
    payload.mkdir()
    selection = payload / "mods-enabled.json"
    write_json(selection, [])
    receipt_rows = []
    definitions = []
    for cohort, cohort_packages in (
        ("startup_empty_mods", []),
        ("startup_detected_disabled_mods", packages),
    ):
        root = payload / cohort
        root.mkdir()
        (root / ".pdmod-migration-done").write_bytes(b"# Matched warm discovery control.\n")
        for row, source, installed, verified in cohort_packages:
            destination = root.joinpath(*installed.parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            if row["form"] == "loose":
                shutil.copytree(source, destination, symlinks=True)
            else:
                shutil.copy2(source, destination, follow_symlinks=False)
            if verify_package(row, destination) != verified:
                raise ValueError(f"Staged bytes differ: {destination}")
            receipt_rows.append({**row, **verified, "staged": destination.relative_to(output).as_posix()})
        definitions.append(definition(cohort, cohort_packages, repo, payload, inventory_kind))
    for item in definitions:
        write_json(test_dir / (item["scenario_name"] + ".json"), item)
    write_json(output / "receipt.json", {
        "revision": RELEASE_COMMIT,
        "expected_client_sha256": RELEASE_SHA256,
        "inventory_kind": inventory_kind,
        "inventory_sha256": input_digest,
        "verdict": "fixtures-prepared",
        "native_execution": False,
        "incident_provenance": (
            "Caller attests original incident bytes, paths and forms; see evidence entries"
            if inventory_kind == "incident-exact"
            else "Retained project artifacts; no identity claim for the incident profile"
        ),
        "packages": receipt_rows,
        "controls": {
            "enabled_json_sha256": sha256_file(selection),
            "migration_sentinel_sha256": sha256_file(payload / "startup_empty_mods/.pdmod-migration-done"),
            "fixture_sha256": {path.name: sha256_file(path) for path in sorted(test_dir.iterdir())},
        },
    })
    return test_dir


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, help="Absolute canonical checkout used by run.ps1")
    parser.add_argument("--inventory", required=True, help="Absolute reviewed five-package JSON file")
    parser.add_argument("--inventory-kind", choices=("approximate", "incident-exact"), default="approximate",
                        help="Use incident-exact only with evidence of the original bytes, paths and forms")
    parser.add_argument("--output", required=True, help="New scratch directory inside a caller-owned parent")
    args = parser.parse_args()
    try:
        tests = prepare(*(absolute_path(value) for value in (args.repo, args.inventory, args.output)),
                        inventory_kind=args.inventory_kind)
    except (KeyError, OSError, ValueError, RuntimeError, zipfile.BadZipFile) as error:
        print(f"Fixture preparation failed: {error}", file=sys.stderr)
        return 1
    print(f"Prepared fixtures only. TestsDir: {tests}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
