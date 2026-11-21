#!/usr/bin/env python3
"""Stage the first-party bundled sample mods into an install (T-RELEASE-007).

Every dev-mods/dev-mods.json entry with "release": true is packed, from its
mod.json and its top-level typed archives only (never its build scripts,
notes or authoring sources), into <install>/mods/bundled/<id>.pdmod through
the release verifier (tools/verify_pdxxx_modder_workflow.py
--release-package). The inventory <install>/bundled-mods.sha256 is written
last, one "<sha256> *<id>.pdmod" line per package in dev-mods.json order. On a
fresh profile the game enables each listed package while its bytes still
match (port/include/modmgr_bundled_samples.h); a user's own copy of the same
id always wins and is never enabled for them.

All packages are built and verified before anything in the install changes,
and the old inventory is removed first, so an interrupted run never leaves an
inventory naming packages it did not write.

    python tools/stage_bundled_samples.py --install-root <install>
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[1]
MANIFEST = Path("dev-mods/dev-mods.json")
# The names the game reads (port/include/modmgr_bundled_samples.h).
BUNDLED_DIR = Path("mods/bundled")
INVENTORY_NAME = "bundled-mods.sha256"
PACKAGE_EXT = ".pdmod"
MAX_PACKAGES = 16
MAX_ID_LEN = 63
# The same rules as devtools/release.ps1's release mod check.
ENTRY_ID = re.compile(r"[a-z0-9][a-z0-9_.-]*\Z", re.ASCII)
ENTRY_PATH = re.compile(r"[a-z0-9][a-z0-9_-]*\Z", re.ASCII)


class StageError(ValueError):
    pass


def load_verifier(repo: Path):
    path = repo / "tools" / "verify_pdxxx_modder_workflow.py"
    spec = importlib.util.spec_from_file_location("pdxxx_workflow_for_staging", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise StageError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def read_json(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise StageError(f"{path}: {error}") from error


def release_entries(repo: Path) -> list[dict]:
    """The dev-mods.json entries with "release": true, in file order."""
    data = read_json(repo / MANIFEST)
    if not isinstance(data, dict) or data.get("schema") != "pd2-dev-mods-v1" \
            or not isinstance(data.get("mods"), list):
        raise StageError(f"{MANIFEST}: not a pd2-dev-mods-v1 manifest")
    entries = []
    seen = set()
    for entry in data["mods"]:
        if not isinstance(entry, dict):
            raise StageError(f"{MANIFEST}: every mod entry must be an object")
        if entry.get("release") is not True:
            continue
        mod_id, path = entry.get("id"), entry.get("path")
        if not isinstance(mod_id, str) or not ENTRY_ID.fullmatch(mod_id) \
                or len(mod_id) > MAX_ID_LEN or PureWindowsPath(mod_id).is_reserved():
            raise StageError(f"release mod id {mod_id!r} is not a single safe name")
        if not isinstance(path, str) or not ENTRY_PATH.fullmatch(path) \
                or PureWindowsPath(path).is_reserved():
            raise StageError(f"release mod {mod_id} path {path!r} is not a single safe name")
        if mod_id.lower() in seen:
            raise StageError(f"release mod id {mod_id} appears twice")
        seen.add(mod_id.lower())
        entries.append({"id": mod_id, "path": path})
    if len(entries) > MAX_PACKAGES:
        raise StageError(f"{len(entries)} release mods; the game reads at most {MAX_PACKAGES}")
    return entries


def package_members(source: Path, typed_suffixes) -> list[Path]:
    """mod.json and the top-level typed archives: what a package holds."""
    if not source.is_dir() or source.is_symlink():
        raise StageError(f"{source}: not a folder")
    members = [source / "mod.json"]
    if not members[0].is_file():
        raise StageError(f"{source}: no mod.json")
    archives = []
    for path in sorted(source.rglob("*")):
        if path.is_file() and path.suffix.lower() in typed_suffixes:
            if path.parent != source:
                # A package keeps its archives at the top. A typed archive
                # deeper down is authoring input (a template, a placeholder)
                # and the folder is not a package.
                raise StageError(f"{path}: a typed archive below the top of a release mod folder")
            if path.is_symlink():
                raise StageError(f"{path}: a link, not a file")
            archives.append(path)
    if not archives:
        raise StageError(f"{source}: no typed archives")
    return members + archives


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def build_package(entry: dict, repo: Path, scratch: Path, verifier_path: Path,
                  python: str, typed_suffixes) -> Path:
    source = repo / "dev-mods" / entry["path"]
    manifest = read_json(source / "mod.json")
    if not isinstance(manifest, dict) or manifest.get("id") != entry["id"]:
        raise StageError(f"{source / 'mod.json'}: its id is not {entry['id']!r}")
    staged = scratch / entry["id"]
    staged.mkdir()
    for member in package_members(source, typed_suffixes):
        shutil.copyfile(member, staged / member.name)
    package = scratch / f"{entry['id']}{PACKAGE_EXT}"
    result = subprocess.run(
        [python, "-B", str(verifier_path), "--source-root", str(staged),
         "--release-package", str(package)],
        capture_output=True, text=True, check=False)
    if result.returncode != 0 or not package.is_file():
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise StageError(f"release mod {entry['id']} could not be packaged")
    return package


def stage(repo: Path, install_root: Path, python: str = sys.executable,
          only: set[str] | None = None) -> list[dict]:
    """Stage the release entries (only those named in `only`, when given)."""
    if not install_root.is_dir() or install_root.is_symlink():
        raise StageError(f"{install_root}: the install folder must exist")
    entries = release_entries(repo)
    if only is not None:
        entries = [entry for entry in entries if entry["id"] in only]
    verifier = load_verifier(repo)
    typed_suffixes = set(verifier.TYPED_DESCRIPTORS)
    verifier_path = repo / "tools" / "verify_pdxxx_modder_workflow.py"
    bundled = install_root / BUNDLED_DIR
    inventory = install_root / INVENTORY_NAME

    receipt = []
    # The scratch folder sits in the install (same volume, so publishing is a
    # rename) under a hidden name the game's scanner skips.
    with tempfile.TemporaryDirectory(prefix=".bundled-stage-", dir=install_root) as tmp:
        scratch = Path(tmp)
        packages = [build_package(entry, repo, scratch, verifier_path, python, typed_suffixes)
                    for entry in entries]

        # Nothing in the install has changed yet. From here: drop the old
        # inventory, publish the packages, then write the new inventory.
        if inventory.exists():
            inventory.unlink()
        if packages:
            bundled.mkdir(parents=True, exist_ok=True)
        if bundled.is_dir():
            keep = {package.name.lower() for package in packages}
            for stale in bundled.glob(f"*{PACKAGE_EXT}"):
                if stale.name.lower() not in keep:
                    stale.unlink()
        lines = []
        for entry, package in zip(entries, packages):
            digest = file_sha256(package)
            size = package.stat().st_size
            os.replace(package, bundled / package.name)
            lines.append(f"{digest} *{package.name}\n")
            receipt.append({"id": entry["id"], "path": (BUNDLED_DIR / package.name).as_posix(),
                            "sha256": digest, "bytes": size})
        if lines:
            pending = scratch / INVENTORY_NAME
            pending.write_bytes("".join(lines).encode("ascii"))
            os.replace(pending, inventory)
    return receipt


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, default=REPO)
    parser.add_argument("--install-root", type=Path, required=True)
    parser.add_argument("--python", default=sys.executable,
                        help="interpreter for the release verifier (default: this one)")
    parser.add_argument("--only", default=None,
                        help="comma-separated ids: stage only these release entries; an empty "
                             "value stages none and removes the old inventory and packages")
    args = parser.parse_args()
    only = None
    if args.only is not None:
        only = {item.strip() for item in args.only.split(",") if item.strip()}
    try:
        receipt = stage(args.repo.resolve(), args.install_root.resolve(), args.python, only)
    except (OSError, StageError, subprocess.SubprocessError) as error:
        print(f"Bundled sample staging failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(receipt, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
