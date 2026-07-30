#!/usr/bin/env python3
"""Scheduled Workbench briefing exporter.

This keeps the established ``python -m tools.daily_flow.orchestrator`` entrypoint
while making the Workbench the only live project-truth source. It never mutates
roadmap or note state; durable changes remain server-API-only.
"""

from __future__ import annotations

import argparse
import atexit
import json
import signal
import sys
from typing import Any

from . import workbench_mode
from .lib import fsutil, gitutil, timefmt

LOG_PREFIX = "DAILY-FLOW.ORCHESTRATOR"


def _last_run_path():
    return fsutil.state_dir() / "last-run.json"


def _record_last_run(status: str, failure_point: str | None = None) -> None:
    payload = {
        "schema_version": 2,
        "date": timefmt.today_et().isoformat(),
        "completed_at": timefmt.format_iso_et(),
        "status": status,
        "failure_point": failure_point,
        "truth_source": "Tools/Workbench/data/roadmap.json",
    }
    fsutil.save_json_atomic(_last_run_path(), payload)


def _print(prefix: str, payload: Any) -> None:
    print(f"{prefix} {json.dumps(payload, ensure_ascii=False)}", flush=True)


def _run_lock_cleanup(reason: str) -> dict[str, object]:
    """Best-effort stale Git lock cleanup retained for scheduled-run safety."""
    try:
        status = gitutil.cleanup_stale_locks()
    except Exception as exc:
        _print(f"{LOG_PREFIX}.LOCK_CLEANUP.ERROR", {"reason": reason, "error": str(exc)})
        return {"reason": reason, "error": str(exc)}
    if status.get("cleared") or status.get("deferred_fresh"):
        _print(f"{LOG_PREFIX}.LOCK_CLEANUP", {"reason": reason, **status})
    return {"reason": reason, **status}


def _signal_cleanup(signum: int, _frame: Any) -> None:
    _run_lock_cleanup(reason=f"signal_{signum}")
    sys.exit(128 + signum)


def _install_cleanup_hooks() -> None:
    atexit.register(_run_lock_cleanup, reason="atexit")
    try:
        signal.signal(signal.SIGTERM, _signal_cleanup)
    except (ValueError, OSError):
        pass
    sigbreak = getattr(signal, "SIGBREAK", None)
    if sigbreak is not None:
        try:
            signal.signal(sigbreak, _signal_cleanup)
        except (ValueError, OSError):
            pass


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Export a read-only daily briefing from the project Workbench."
    )
    # Keep historical scheduler flags as explicit no-ops so existing scheduled
    # task definitions continue to launch without gaining mutation privileges.
    parser.add_argument("--force", action="store_true", help="Compatibility no-op.")
    parser.add_argument("--no-merge", action="store_true", help="Compatibility no-op.")
    parser.add_argument("--no-push", action="store_true", help="Compatibility no-op.")
    parser.add_argument("--catchup-only", action="store_true", help="Compatibility no-op.")
    parser.add_argument("--for-date", default=None, help="Compatibility no-op.")
    parser.add_argument(
        "--skip-on-fresh-lock",
        action="store_true",
        help="Exit successfully when a live Git operation owns a fresh lock.",
    )
    args = parser.parse_args(argv)

    _install_cleanup_hooks()
    preflight = _run_lock_cleanup(reason="preflight")
    if preflight.get("deferred_fresh") and args.skip_on_fresh_lock:
        _print(
            f"{LOG_PREFIX}.LOCK_FRESH.SKIP",
            {
                "deferred_fresh": preflight.get("deferred_fresh"),
                "live_git_processes": preflight.get("live_git_processes"),
            },
        )
        return 0

    if not workbench_mode.available():
        _record_last_run("failed", "workbench_missing")
        _print(
            f"{LOG_PREFIX}.WORKBENCH_MISSING",
            {
                "error": "Tools/Workbench/data/roadmap.json is required",
                "policy": "refusing any retired tracker fallback",
            },
        )
        return 2

    try:
        result = workbench_mode.run()
    except Exception as exc:
        _record_last_run("failed", "workbench_export")
        _print(f"{LOG_PREFIX}.WORKBENCH.FAIL", {"error": str(exc)})
        return 2

    _record_last_run("ok")
    _print(f"{LOG_PREFIX}.WORKBENCH", result)
    return 0


if __name__ == "__main__":
    sys.exit(main())
