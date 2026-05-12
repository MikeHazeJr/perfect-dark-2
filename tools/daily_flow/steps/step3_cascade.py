"""
Step 3 - Condition cascade.

For each kanban card that flipped to done in this run, invoke the parked-evaluator
cascade subcommand. Then run a global pass for update-ready and update-stale.

Captures cascade events (parent card -> dependents unblocked) to a per-day JSON.
"""

from __future__ import annotations

import datetime as dt
import pathlib
import subprocess
from typing import Any

from ..lib import fsutil, timefmt

LOG_PREFIX = "DAILY-FLOW.CASCADE"


def _evaluator() -> pathlib.Path:
    return fsutil.repo_root() / "tools" / "parked_evaluator.py"


def _run_evaluator(args: list[str]) -> tuple[int, str, str]:
    cmd = ["python", str(_evaluator()), *args]
    proc = subprocess.run(
        cmd,
        cwd=fsutil.repo_root(),
        capture_output=True,
        text=True,
        check=False,
        encoding="utf-8",
        errors="replace",
    )
    return proc.returncode, proc.stdout, proc.stderr


def run_cascade(state_sync_result: dict[str, Any], today: dt.date | None = None) -> dict[str, Any]:
    today = today or timefmt.today_et()
    today_iso = today.isoformat()

    events: list[dict[str, Any]] = []
    failures: list[dict[str, Any]] = []

    for flip in state_sync_result.get("cards_flipped", []):
        cid = flip.get("id")
        if not cid:
            continue
        rc, stdout, stderr = _run_evaluator(["cascade", "--card-done", cid])
        events.append({
            "card": cid,
            "exit": rc,
            "stdout_tail": stdout.splitlines()[-5:],
            "stderr_tail": stderr.splitlines()[-5:],
        })
        if rc != 0:
            failures.append({"step": "cascade", "card": cid, "exit": rc, "stderr": stderr})

    rc1, _, err1 = _run_evaluator(["update-ready"])
    if rc1 != 0:
        failures.append({"step": "update-ready", "exit": rc1, "stderr": err1})

    rc2, _, err2 = _run_evaluator(["update-stale"])
    if rc2 != 0:
        failures.append({"step": "update-stale", "exit": rc2, "stderr": err2})

    result = {
        "for_date": today_iso,
        "cascade_events": events,
        "update_ready_exit": rc1,
        "update_stale_exit": rc2,
        "failures": failures,
    }
    fsutil.save_json_atomic(fsutil.state_dir() / f"cascade-{today_iso}.json", result)
    return result
