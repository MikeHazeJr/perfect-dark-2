#!/usr/bin/env python3
"""Validate and stage the reviewed executable-relative authored sample inventory."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import re
import stat
import subprocess
import sys
import tempfile


SAMPLE_ID = re.compile(r"[a-z0-9][a-z0-9_-]*\Z", re.ASCII)
MEMBER_NAME = re.compile(r"[a-z0-9][a-z0-9_-]*\.pd[a-z0-9]+\Z", re.ASCII)
SHA256 = re.compile(r"[0-9a-f]{64}\Z", re.ASCII)
INVENTORY = Path("dev-mods/authored-samples.json")
BUNDLED_MODS = Path("mods/bundled")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON field: {key}")
        result[key] = value
    return result


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)


def plain_path(path: Path, kind) -> bool:
    try:
        info = path.lstat()
    except FileNotFoundError:
        return False
    reparse = getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
    return not reparse and kind(info.st_mode)


def require_plain_directory(path: Path) -> None:
    if not plain_path(path, stat.S_ISDIR):
        raise ValueError(f"Expected a regular directory: {path}")


def require_plain_tree(path: Path) -> None:
    for directory in (*reversed(path.parents), path):
        require_plain_directory(directory)


def file_sha256(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def load_inventory(path: Path) -> list[dict]:
    data = read_json(path)
    if (not isinstance(data, dict) or set(data) != {"schema", "samples"}
            or data["schema"] != "pd2-authored-samples-v1"
            or not isinstance(data["samples"], list) or not data["samples"]):
        raise ValueError("Invalid or empty authored sample inventory")

    ids = set()
    for sample in data["samples"]:
        if not isinstance(sample, dict) or set(sample) != {"id", "version", "members"}:
            raise ValueError("Invalid authored sample entry")
        sample_id = sample["id"]
        if (not isinstance(sample_id, str) or not SAMPLE_ID.fullmatch(sample_id)
                or PureWindowsPath(sample_id).is_reserved() or sample_id in ids):
            raise ValueError(f"Invalid or duplicate authored sample ID: {sample_id!r}")
        ids.add(sample_id)
        if not isinstance(sample["version"], str) or not sample["version"].strip():
            raise ValueError(f"Missing version for {sample_id}")
        members = sample["members"]
        if not isinstance(members, dict) or "mod.json" not in members or len(members) < 2:
            raise ValueError(f"Expected mod.json and typed archive members for {sample_id}")
        for name, digest in members.items():
            if name != "mod.json" and (not MEMBER_NAME.fullmatch(name)
                                      or PureWindowsPath(name).is_reserved()):
                raise ValueError(f"Invalid member for {sample_id}: {name!r}")
            if not isinstance(digest, str) or not SHA256.fullmatch(digest):
                raise ValueError(f"Invalid member SHA-256 for {sample_id}/{name}")
    return data["samples"]


def build_package(sample: dict, source_root: Path, verifier: Path, scratch: Path) -> Path:
    sample_id = sample["id"]
    source = source_root / sample_id
    require_plain_directory(source)
    package_input = scratch / sample_id
    package_input.mkdir()
    for name, digest in sample["members"].items():
        member = source / name
        if not plain_path(member, stat.S_ISREG):
            raise ValueError(f"Expected a regular reviewed member: {member}")
        payload = member.read_bytes()
        if hashlib.sha256(payload).hexdigest() != digest:
            raise ValueError(f"Reviewed member digest changed: {member}")
        (package_input / name).write_bytes(payload)

    metadata = read_json(package_input / "mod.json")
    if (not isinstance(metadata, dict) or metadata.get("id") != sample_id
            or metadata.get("version") != sample["version"]):
        raise ValueError(f"Inventory ID or version disagrees with {source / 'mod.json'}")

    package = scratch / f"{sample_id}.pdmod"
    subprocess.run(
        [sys.executable, "-B", str(verifier), "--source-root", str(package_input),
         "--release-package", str(package)], check=True, stdout=sys.stderr,
    )
    return package


def publish_package(package: Path, destination: Path, digest: str) -> str:
    try:
        # Windows rename fails if the destination exists. On other hosts, use
        # an exclusive hard-link publication within the same filesystem.
        if os.name == "nt":
            package.rename(destination)
        else:
            os.link(package, destination)
        return "installed"
    except FileExistsError:
        if plain_path(destination, stat.S_ISREG) and file_sha256(destination) == digest:
            return "already-exact"
        return "preserved-existing"


def stage_samples(repo: Path, install_root: Path, inventory: Path, *, fresh: bool) -> list[dict]:
    require_plain_tree(install_root)
    source_root = repo / "dev-mods"
    require_plain_directory(source_root)
    samples = load_inventory(inventory)
    verifier = repo / "tools/verify_pdxxx_modder_workflow.py"
    bundled = install_root / BUNDLED_MODS
    if os.path.lexists(bundled.parent):
        require_plain_directory(bundled.parent)
        if fresh:
            raise ValueError("Fresh sample staging requires an absent install mods directory")
    if os.path.lexists(bundled):
        require_plain_directory(bundled)

    # Validate the entire inventory before adding any package to the install.
    # The private directory is outside the startup-scanned tree and on the
    # same volume as the final packages.
    with tempfile.TemporaryDirectory(prefix=".authored-samples-", dir=install_root) as directory:
        scratch = Path(directory)
        packages = [build_package(sample, source_root, verifier, scratch) for sample in samples]
        prepared_bundled = scratch / BUNDLED_MODS if fresh else bundled
        prepared_bundled.mkdir(parents=True, exist_ok=True)
        require_plain_tree(prepared_bundled)
        result = []
        for sample, package in zip(samples, packages, strict=True):
            digest = file_sha256(package)
            outcome = publish_package(package, prepared_bundled / package.name, digest)
            result.append({
                **sample,
                "path": (BUNDLED_MODS / package.name).as_posix(),
                "candidate_sha256": digest,
                "installed_sha256": None if outcome == "preserved-existing" else digest,
                "outcome": outcome,
            })
        if fresh:
            # Publish one complete tree. Windows rename also refuses a path a
            # concurrent writer created after the original absence check.
            prepared_bundled.parent.rename(bundled.parent)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--install-root", type=Path, required=True)
    parser.add_argument("--fresh", action="store_true", help="require fresh release staging")
    args = parser.parse_args()
    repo = args.repo.resolve(strict=True)
    try:
        receipt = stage_samples(repo, args.install_root.absolute(), repo / INVENTORY, fresh=args.fresh)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Authored sample staging failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(receipt, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
