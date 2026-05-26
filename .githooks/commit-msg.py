#!/usr/bin/env python3
"""Perfect Dark 2 commit-msg validator.

Enforces the commit message standard documented in
context/designs/commit-message-standard.md.

Required subject format:
    <Pillar> - <CardID>: <summary>

Required body trailer:
    Refs: cNNN [, B-NNN] [, pt-NNN]

Pillar must resolve to an entry in tools/kanban/state.json (by id or name,
case-insensitive). CardID must exist and its pillar must match the subject.

Bypass with `git commit --no-verify` only when Mike explicitly authorizes it.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

SUBJECT_MAX_LEN = 72

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
    r"^(?P<pillar>[A-Za-z][A-Za-z0-9 _\-]*?)\s+-\s+(?P<card>[Cc]\d+)\s*:\s+(?P<summary>\S.*)$"
)

REFS_RE = re.compile(
    r"^Refs\s*:\s*(?P<list>[^\r\n]+?)\s*$",
    re.IGNORECASE | re.MULTILINE,
)

CARD_ID_IN_REFS_RE = re.compile(r"\bc\d+\b", re.IGNORECASE)


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
        if (root / "AGENTS.md").exists() and (root / "tools/kanban/state.json").exists():
            return root
        root = root.parent
    fail(f"could not resolve git repo root path: {result.stdout.strip()}")


def load_kanban(root: Path):
    state_file = root / "tools" / "kanban" / "state.json"
    if not state_file.exists():
        fail(f"kanban state file missing: {state_file}")
    try:
        data = json.loads(state_file.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        fail(f"tools/kanban/state.json is not valid JSON: {exc}")
    pillars = data.get("pillars", [])
    cards = data.get("cards", [])
    if not pillars:
        fail("tools/kanban/state.json has no pillars array.")
    if not cards:
        fail("tools/kanban/state.json has no cards array.")
    return pillars, cards


def find_pillar(pillars, token: str):
    needle = token.strip().lower()
    for entry in pillars:
        if entry.get("id", "").lower() == needle:
            return entry
        if entry.get("name", "").lower() == needle:
            return entry
    return None


def find_card(cards, card_id: str):
    needle = card_id.lower()
    for entry in cards:
        if entry.get("id", "").lower() == needle:
            return entry
    return None


def read_message(path: Path) -> list[str]:
    if not path.exists():
        fail(f"commit message file missing: {path}")
    raw = path.read_text(encoding="utf-8", errors="replace")
    lines = [ln for ln in raw.splitlines() if not ln.startswith("#")]
    while lines and lines[-1].strip() == "":
        lines.pop()
    return lines


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        fail("called without a commit message path (are you outside git?).")

    msg_path = Path(argv[1])
    lines = read_message(msg_path)
    if not lines:
        fail("commit message is empty.")

    subject = lines[0].rstrip()

    for pat in EXCEPTION_PATTERNS:
        if pat.match(subject):
            return 0

    if ANTI_PATTERN_SUBJECT.match(subject):
        fail(
            "subject is a bare placeholder.\n"
            f"  Got: {subject!r}\n"
            "  Use: <Pillar> - <CardID>: <summary>\n"
            "  Example: 'Tooling - c120: Add commit-msg hook enforcing pillar prefix'"
        )

    if len(subject) > SUBJECT_MAX_LEN:
        fail(
            f"subject is {len(subject)} chars, limit is {SUBJECT_MAX_LEN}.\n"
            "Tighten the summary or move detail to the body.\n"
            f"  Subject: {subject}"
        )

    match = SUBJECT_RE.match(subject)
    if match is None:
        fail(
            "subject does not match required format.\n"
            "  Required: <Pillar> - <CardID>: <summary>\n"
            "  Example:  Tooling - c120: Add commit-msg hook enforcing pillar prefix\n"
            f"  Got:      {subject}"
        )

    pillar_token = match.group("pillar").strip()
    card_id = match.group("card").lower()
    summary = match.group("summary").strip()

    if not summary:
        fail("subject summary is empty after the colon.")

    root = find_repo_root()
    pillars, cards = load_kanban(root)

    pillar = find_pillar(pillars, pillar_token)
    if pillar is None:
        valid = ", ".join(sorted(p.get("name", "") for p in pillars))
        fail(
            f"pillar {pillar_token!r} is not registered in tools/kanban/state.json.\n"
            f"Valid pillars: {valid}"
        )

    card = find_card(cards, card_id)
    if card is None:
        fail(
            f"card {card_id!r} does not exist in tools/kanban/state.json.\n"
            "Create the card first, or amend to reference an existing card."
        )

    card_pillar_id = card.get("pillar", "").lower()
    pillar_id = pillar.get("id", "").lower()
    if pillar_id != card_pillar_id:
        fail(
            f"pillar mismatch: subject says {pillar.get('name')!r} but card "
            f"{card_id} is in pillar {card_pillar_id!r}.\n"
            "Fix the subject's pillar prefix, or move the card to the right pillar."
        )

    body_lines = lines[1:]
    if all(not ln.strip() for ln in body_lines):
        fail(
            "commit body is empty. Add 2-4 sentences explaining what changed and why,\n"
            "and a 'Refs:' line."
        )

    body = "\n".join(body_lines)
    refs_matches = list(REFS_RE.finditer(body))
    if not refs_matches:
        fail(
            "body has no 'Refs:' line.\n"
            "Add a trailer like:  Refs: " + card_id + "   (and any B-NNN / pt-NNN)"
        )

    refs_text = refs_matches[-1].group("list")
    referenced_cards = {m.group(0).lower() for m in CARD_ID_IN_REFS_RE.finditer(refs_text)}
    if card_id not in referenced_cards:
        fail(
            f"Refs line does not include the subject's CardID {card_id!r}.\n"
            f"  Refs: {refs_text}"
        )

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
