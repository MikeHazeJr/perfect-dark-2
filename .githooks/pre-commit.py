#!/usr/bin/env python3
"""Perfect Dark 2 pre-commit validator."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def fail(message: str) -> int:
    print(f"pre-commit hook rejected this commit: {message}", file=sys.stderr)
    print("Bypass only when explicitly authorized.", file=sys.stderr)
    return 1


def repo_root() -> Path:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError) as exc:
        raise RuntimeError(f"could not locate repo root: {exc}") from exc
    raw_root = result.stdout.strip()
    if len(raw_root) >= 3 and raw_root[0] == "/" and raw_root[2] == "/":
        raw_root = raw_root[1].upper() + ":" + raw_root[2:]
    root = Path(raw_root)
    if root.exists():
        return root

    root = Path.cwd()
    while root != root.parent:
        if (root / "AGENTS.md").exists() and (root / "tools/kanban/state.json").exists():
            return root
        root = root.parent
    raise RuntimeError("could not locate repo root from current directory")


def repo_relative_arg(root: Path, target: Path) -> str:
    try:
        return target.relative_to(root).as_posix()
    except ValueError:
        return str(target)


def main() -> int:
    try:
        root = repo_root()
    except RuntimeError as exc:
        return fail(str(exc))

    guard = root / "tools" / "asset_native_source_guard.py"
    if not guard.exists():
        return fail(f"missing {guard}")

    guard_arg = repo_relative_arg(root, guard)
    result = subprocess.run(
        [sys.executable, guard_arg, "--staged"],
        cwd=root,
        text=True,
    )
    if result.returncode != 0:
        return fail("c3842 Asset Pipeline native-source guard failed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
