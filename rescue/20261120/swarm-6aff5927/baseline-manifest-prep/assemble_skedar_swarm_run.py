"""Assemble N-1055 evidence already collected in a single run directory.

Outputs a schema-1 manifest after the frozen single-run verifier accepts it.
Keep the program and evidence directory offline and quiescent during assembly.
Managed receipts and queue IDs come from the caller's retained run records.
This tool checks local consistency. Provider activation, visual inspection,
and the authenticity of the originating runtime and queue remain operator gates.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
from types import ModuleType, SimpleNamespace


PATCH_SHA256 = "3f25a29dfb89c4710b5d57ad3d492f9af9f31bd6efa8df0f774312b4ef3b9a43"
FROZEN_PATHS = {
    "port/fast3d/swarm_gpu.cpp": "7ebc741e1102d82cad9e4a1d0e85b222a00b14ce0d02e175342fba5052213d6b",
    "port/include/swarm_gpu.h": "a5a78b395747ddc889b08bd468f1229cb90030df1acaa1955a34dccd3525c82e",
    "port/src/swarm_test.c": "ef0f0022bea291b1c4dda201fe33eb5d5b5f8c86d1f45b2aa2a3f4b9a722e2d3",
    "tools/smoke-verify/tests/skedar_swarm_baseline_cpu.json":
        "698a5ff9e3f1a177ba9a2c6c87c2ea1efac0020076ed4a2820c39cabdaa1f85c",
    "tools/smoke-verify/tests/skedar_swarm_baseline_gpu.json":
        "b6d8b4b289b0e7e01ac08174ee7ce4102336d4b20ad0f0623ce8c4997db7df54",
    "tools/smoke-verify/verify_skedar_swarm_pair.py":
        "3f99868e2db930cd2833c1df698bca4373f778c692f4243802684b7870854fd2",
}
VERIFIER_PATH = "tools/smoke-verify/verify_skedar_swarm_pair.py"
ARTIFACTS = ("client", "arena", "scenario", "source_manifest", "fixture", "receipt", "seed", "log")
INSTALLED = ("client", "arena", "scenario")


class CandidateDirectory:
    """Expose only the proposed manifest in memory to the frozen verifier."""

    def __init__(self, root, document):
        self.root = root
        self.document = document

    def resolve(self, *, strict):
        return CandidateDirectory(self.root.resolve(strict=strict), self.document)

    def __fspath__(self):
        return str(self.root)

    def __truediv__(self, name):
        if name == "manifest.json":
            return SimpleNamespace(read_bytes=lambda: self.document)
        return self.root / name


def frozen_bytes(path, expected_sha256):
    data = path.read_bytes()
    normalized = data.replace(b"\r\n", b"\n")
    if hashlib.sha256(normalized).hexdigest() != expected_sha256:
        raise ValueError(f"{path.name} differs from N-1055")
    return data


def load_verifier(path):
    data = frozen_bytes(path, FROZEN_PATHS[VERIFIER_PATH])
    module = ModuleType("frozen_skedar_swarm_pair")
    exec(compile(data, str(path), "exec"), module.__dict__)
    return module


def file_binding(root, supplied, require):
    path = (root / supplied).resolve(strict=True)
    require(path.is_file() and path.is_relative_to(root), f"artifact outside run directory: {supplied}")
    relative = path.relative_to(root)
    require(relative.as_posix().casefold() != "manifest.json", "manifest cannot be an input artifact")
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return path, {"path": relative.as_posix(), "sha256": digest}


def install_name(name, require):
    require(
        all(part not in {"", ".", ".."} and ":" not in part and "\\" not in part for part in name.split("/")),
        f"invalid managed install path: {name}",
    )
    return PurePosixPath(name).as_posix()


def require_entry(entry, path, binding, require):
    require(
        entry and entry["sha256"] == binding["sha256"]
        and type(entry["bytes"]) is int and entry["bytes"] == path.stat().st_size,
        f"managed entry differs from collected {path.name}",
    )


def assemble(args, verifier):
    require = verifier.require
    root = args.directory.resolve(strict=True)
    require(root.is_dir(), "evidence directory must exist")
    suffix = {"CPU": "cpu", "GPU_FULL": "gpu"}[args.mode]
    fixture_name = f"skedar_swarm_baseline_{suffix}"
    fixture_key = f"tools/smoke-verify/tests/{fixture_name}.json"
    require(re.fullmatch(r"q_[0-9]{17}_[0-9a-f]{8}", args.queue_id), "invalid coordination queue ID")
    require(
        re.fullmatch(rf"[0-9]{{8}}T[0-9]{{12}}Z-{fixture_name}-[0-9a-f]{{8}}", args.run_id),
        "managed run ID does not match the selected fixture",
    )

    files = {key: file_binding(root, getattr(args, key), require) for key in ARTIFACTS}
    fixture = verifier.parse(frozen_bytes(files["fixture"][0], FROZEN_PATHS[fixture_key]))
    require(fixture["scenario_name"] == fixture_name, "frozen fixture name differs")
    receipt_path = files["receipt"][0]
    require(receipt_path.name == "recipe.json.gz", "supply the managed recipe.json.gz")
    recipe = verifier.managed_recipe(receipt_path)
    require(
        type(recipe["schema"]) is int and recipe["schema"] == 1 and recipe["complete"] is True
        and recipe["id"] == args.run_id
        and recipe["retention"]["retain_housekeeping"] is True
        and re.fullmatch(r"[0-9a-f]{32}", recipe["retention"]["operation_id"])
        and datetime.fromisoformat(recipe["completed"]).utcoffset() == timezone.utc.utcoffset(None),
        "expected a complete retained managed recipe for this run",
    )
    require_entry(recipe["definition"], *files["fixture"], require)

    source = verifier.parse(files["source_manifest"][0].read_bytes())
    require(
        type(source["schema"]) is int and source["schema"] == 1
        and re.fullmatch(r"[0-9a-f]{40}", source["source_commit"])
        and source["source_commit"] == recipe["provenance"]["git_head"]
        and source["client_sha256"] == files["client"][1]["sha256"]
        and source["frozen_patch_sha256"] == PATCH_SHA256
        and source["frozen_paths_sha256"] == FROZEN_PATHS,
        "source receipt does not bind N-1055, recipe HEAD, and collected client",
    )

    seed = verifier.managed_recipe(files["seed"][0])
    for key in INSTALLED:
        name = install_name(getattr(args, f"{key}_install_path"), require)
        require(name not in recipe["deleted"], f"managed install entry was deleted: {name}")
        require_entry(recipe["changed"].get(name, seed.get(name)), *files[key], require)
        files[key][1]["install_path"] = name
    require(
        len({files[key][1]["install_path"].casefold() for key in INSTALLED}) == len(INSTALLED),
        "installed artifacts must have distinct paths",
    )

    captures = [
        file_binding(root, args.capture_35, require),
        file_binding(root, args.capture_50, require),
    ]
    require(
        len({path for path, _ in (*files.values(), *captures)}) == len(files) + len(captures),
        "evidence artifacts must use distinct paths",
    )
    for (path, binding), event in zip(
        captures, (event for event in fixture["input_sequence"] if event["type"] == "screenshot"), strict=True,
    ):
        require(path.name == event["path"], f"capture filename differs from fixture: {path.name}")
        binding["at_ms"] = event["at_ms"]

    for path, binding in (files["log"], *captures):
        verifier.verify_recipe_member(recipe, path, binding["sha256"])
        entry = next(
            entry for name, entry in recipe["changed"].items()
            if verifier.path_name(name) == path.name and entry["sha256"] == binding["sha256"]
        )
        require_entry(entry, path, binding, require)

    manifest = {
        "schema": 1,
        "mode": args.mode,
        "run_id": args.run_id,
        "queue_id": args.queue_id,
        "source_commit": source["source_commit"],
        **{key: binding for key, (_, binding) in files.items()},
        "captures": [binding for _, binding in captures],
    }
    document = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
    verifier.verify_run(CandidateDirectory(root, document), args.mode)
    return root, document


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="directory containing all collected run artifacts")
    parser.add_argument("--mode", required=True, choices=("CPU", "GPU_FULL"))
    parser.add_argument("--run-id", required=True, help="storage ID recorded by the retained runner")
    parser.add_argument("--queue-id", required=True, help="runtime turn ID recorded by coordination")
    parser.add_argument(
        "--verifier", type=Path, default=Path(__file__).resolve().parent.parent / "verify_skedar_swarm_pair.py",
        help="path to the pinned N-1055 pair verifier",
    )
    for key in ARTIFACTS:
        parser.add_argument(f"--{key.replace('_', '-')}", type=Path, required=True)
    for key in INSTALLED:
        parser.add_argument(f"--{key}-install-path", required=True, help="exact relative path in the managed recipe")
    parser.add_argument("--capture-35", type=Path, required=True)
    parser.add_argument("--capture-50", type=Path, required=True)
    parser.add_argument("--write", action="store_true", help="create manifest.json exclusively instead of printing it")
    args = parser.parse_args()
    try:
        root, document = assemble(args, load_verifier(args.verifier))
        if args.write:
            with (root / "manifest.json").open("xb") as stream:
                stream.write(document)
        else:
            sys.stdout.write(document.decode("utf-8"))
    except (ValueError, TypeError, KeyError, OSError, EOFError, OverflowError, IndexError) as error:
        print(f"Assembly failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
