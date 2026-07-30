#!/usr/bin/env python3
"""Perfect Dark 2 Workbench-aware commit-message validator.

Required subject:
    <Area> - <WorkbenchItemID>: <summary>

Required trailer:
    Refs: <WorkbenchItemID> [, B-NNN]
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

SUBJECT_MAX_LEN = 96
EXCEPTION_PATTERNS = [
    re.compile(r"^Merge\b"),
    re.compile(r'^Revert\s+"'),
    re.compile(r"^fixup!"),
    re.compile(r"^squash!"),
    re.compile(r"^amend!"),
]
ANTI_PATTERN_SUBJECT = re.compile(
    r"^(wip|fix|update|test|chore|stuff|misc|tmp|temp|todo)\s*[:\-]?\s*$",
    re.IGNORECASE,
)
SUBJECT_RE = re.compile(
    r"^(?P<area>[A-Za-z][A-Za-z0-9 _\-]*?)\s+-\s+"
    r"(?P<item>[A-Za-z][A-Za-z0-9_-]*(?:-[A-Za-z0-9_-]+)*)\s*:\s+"
    r"(?P<summary>\S.*)$"
)
REFS_RE = re.compile(
    r"^Refs\s*:\s*(?P<list>[^\r\n]+?)\s*$",
    re.IGNORECASE | re.MULTILINE,
)


def fail(message: str) -> None:
    print("\ncommit-msg hook rejected this commit:\n", file=sys.stderr)
    for line in message.splitlines():
        print("  " + line, file=sys.stderr)
    print(
        "\nSee context/designs/commit-message-standard.md for the format.",
        file=sys.stderr,
    )
    print(
        "Bypass with `git commit --no-verify` only when explicitly authorized.",
        file=sys.stderr,
    )
    sys.exit(1)


def normalize_area(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", value.lower())


def find_repo_root() -> Path:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as exc:
        fail(f"could not locate git repo root via git rev-parse: {exc}")
    raw_root = result.stdout.strip()
    if len(raw_root) >= 3 and raw_root[0] == "/" and raw_root[2] == "/":
        raw_root = raw_root[1].upper() + ":" + raw_root[2:]
    root = Path(raw_root)
    if root.exists():
        return root

    root = Path.cwd()
    while root != root.parent:
        if (
            (root / "AGENTS.md").exists()
            and (root / "Tools/Workbench/data/roadmap.json").exists()
        ):
            return root
        root = root.parent
    fail(f"could not resolve git repo root path: {result.stdout.strip()}")


def load_items(root: Path) -> list[dict[str, object]]:
    state_file = root / "Tools" / "Workbench" / "data" / "roadmap.json"
    if not state_file.exists():
        fail(f"Workbench roadmap missing: {state_file}")
    try:
        data = json.loads(state_file.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        fail(f"Workbench roadmap is not valid JSON: {exc}")
    items = data.get("items")
    if not isinstance(items, list):
        fail("Workbench roadmap has no items array.")
    return items


def read_message(path: Path) -> list[str]:
    if not path.exists():
        fail(f"commit message file missing: {path}")
    raw = path.read_text(encoding="utf-8", errors="replace")
    lines = [line for line in raw.splitlines() if not line.startswith("#")]
    while lines and not lines[-1].strip():
        lines.pop()
    return lines


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        fail("called without a commit message path (are you outside git?).")
    lines = read_message(Path(argv[1]))
    if not lines:
        fail("commit message is empty.")
    subject = lines[0].rstrip()

    if any(pattern.match(subject) for pattern in EXCEPTION_PATTERNS):
        return 0
    if ANTI_PATTERN_SUBJECT.match(subject):
        fail(
            "subject is a bare placeholder.\n"
            "Use: <Area> - <WorkbenchItemID>: <summary>"
        )
    if len(subject) > SUBJECT_MAX_LEN:
        fail(f"subject is {len(subject)} chars, limit is {SUBJECT_MAX_LEN}.")

    match = SUBJECT_RE.match(subject)
    if match is None:
        fail(
            "subject does not match required format.\n"
            "Required: <Area> - <WorkbenchItemID>: <summary>\n"
            "Example: Tooling - T-TOOLING-001: Replace Kanban with Workbench"
        )

    area_token = match.group("area").strip()
    item_id = match.group("item").upper()
    items = load_items(find_repo_root())
    item = next(
        (entry for entry in items if str(entry.get("id", "")).upper() == item_id),
        None,
    )
    if item is None:
        fail(
            f"item {item_id!r} does not exist in the Workbench roadmap.\n"
            "Create the item through the Workbench API before committing."
        )

    item_area = str(item.get("area", ""))
    if normalize_area(area_token) != normalize_area(item_area):
        fail(
            f"area mismatch: subject says {area_token!r}, but {item_id} "
            f"belongs to {item_area!r}."
        )

    body = "\n".join(lines[1:])
    if not body.strip():
        fail("commit body is empty. Explain what changed and why, then add Refs:.")
    refs_matches = list(REFS_RE.finditer(body))
    if not refs_matches:
        fail(f"body has no Refs: trailer. Add: Refs: {item_id}")
    refs_text = refs_matches[-1].group("list")
    if not re.search(
        rf"(?<![A-Za-z0-9_-]){re.escape(item_id)}(?![A-Za-z0-9_-])",
        refs_text,
        re.IGNORECASE,
    ):
        fail(f"Refs line does not include {item_id!r}.\n  Refs: {refs_text}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
