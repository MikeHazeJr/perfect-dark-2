"""
Filesystem and JSON helpers for the daily-flow orchestrator.

All JSON writes are atomic (tmp file + rename) and use UTF-8 with trailing newline,
matching the convention established by tools/kanban/server.py and tools/parked_evaluator.py.

Schema invariant: every persisted JSON gets schema_version and semantic_version on first
write. Loader uses .get() with defaults so missing fields never raise.
"""

from __future__ import annotations

import datetime as dt
import hashlib
import json
import os
import pathlib
from typing import Any

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
STATE_DIR = REPO_ROOT / "tools" / "daily_flow" / "state"

LOG_PREFIX = "DAILY-FLOW.FSUTIL"


def repo_root() -> pathlib.Path:
    return REPO_ROOT


def state_dir() -> pathlib.Path:
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    return STATE_DIR


def load_json(path: pathlib.Path, default: Any = None) -> Any:
    if not path.exists():
        return default if default is not None else {}
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def save_json_atomic(path: pathlib.Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    serialized = json.dumps(data, indent=2, ensure_ascii=False) + "\n"
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(serialized, encoding="utf-8")
    os.replace(tmp, path)


def audit_hash(payload: Any) -> str:
    """Stable SHA-256 of a JSON-serializable payload. Used for idempotence checks."""
    blob = json.dumps(payload, sort_keys=True, ensure_ascii=False).encode("utf-8")
    return hashlib.sha256(blob).hexdigest()


def read_lines(path: pathlib.Path) -> list[str]:
    if not path.exists():
        return []
    return path.read_text(encoding="utf-8").splitlines()


def write_text_atomic(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    os.replace(tmp, path)


def today_iso(today: dt.date | None = None) -> str:
    return (today or dt.date.today()).isoformat()


def yesterday_iso(today: dt.date | None = None) -> str:
    d = today or dt.date.today()
    return (d - dt.timedelta(days=1)).isoformat()


def has_em_dash(text: str) -> bool:
    """Returns True if text contains a U+2014 EM DASH. The project forbids them
    in all committed files because PowerShell on Windows 1252 truncates at the
    first non-ASCII byte."""
    return "—" in text


def assert_no_em_dash(text: str, where: str) -> None:
    if has_em_dash(text):
        raise ValueError(f"{LOG_PREFIX}: em-dash detected in {where}; project forbids U+2014")


def posix_rel(path: pathlib.Path) -> str:
    """Returns a forward-slash relative path from repo root.

    Use this when emitting paths into JSON or markdown so downstream tools and
    cross-OS readers see consistent paths regardless of the host platform.
    """
    return str(path.relative_to(REPO_ROOT)).replace("\\", "/")
