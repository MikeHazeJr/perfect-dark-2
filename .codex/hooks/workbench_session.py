#!/usr/bin/env python3
"""Codex lifecycle guard for the Perfect Dark 2 Workbench workflow.

The hook uses the Codex session id as the stable key for live coordination. It
does not guess which durable item a prompt belongs to. A session must claim or
create an active Workbench item through the canonical API before an edit tool
can run. After an edit, the session must update an owned item or add a durable
note before the turn can stop cleanly.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


WORKBENCH_URL = os.environ.get("PD2_WORKBENCH_URL", "http://127.0.0.1:8378").rstrip("/")
ACTIVE_ITEM_STATUSES = {"missing", "stub", "partial", "implemented"}
EDIT_TOOL_NAMES = {"apply_patch", "Edit", "Write"}
MAX_PROMPT_TASK = 180


class HookFailure(RuntimeError):
    """An actionable repository-policy failure."""


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def iso_now() -> str:
    return utc_now().isoformat(timespec="milliseconds").replace("+00:00", "Z")


def parse_stamp(value: object) -> datetime | None:
    if not isinstance(value, str) or not value.strip():
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None


def repository_root() -> Path:
    script_root = Path(__file__).resolve().parents[2]
    if (script_root / "AGENTS.md").is_file() and (
        script_root / "Tools" / "Workbench" / "data" / "roadmap.json"
    ).is_file():
        return script_root

    current = Path.cwd().resolve()
    for candidate in (current, *current.parents):
        if (candidate / "AGENTS.md").is_file() and (
            candidate / "Tools" / "Workbench" / "data" / "roadmap.json"
        ).is_file():
            return candidate
    raise HookFailure("could not locate the Perfect Dark 2 repository root")


def coordination_id(session_id: str) -> str:
    readable = re.sub(r"[^A-Za-z0-9]+", "", session_id)[-8:].lower() or "session"
    digest = hashlib.sha256(session_id.encode("utf-8")).hexdigest()[:8]
    return f"codex-{readable}-{digest}"


def normalize_path(value: object) -> str:
    if not isinstance(value, str):
        return ""
    return os.path.normcase(os.path.normpath(value))


def validate_meta(meta: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if meta.get("canonical") is not True:
        errors.append("canonical must be true")
    if meta.get("isolated") is not False:
        errors.append("isolated must be false")
    project_root = normalize_path(meta.get("projectRoot"))
    data_dir = normalize_path(meta.get("dataDir"))
    if not project_root:
        errors.append("projectRoot is missing")
    expected_data = normalize_path(
        os.path.join(str(meta.get("projectRoot", "")), "Tools", "Workbench", "data")
    )
    if not data_dir or data_dir != expected_data:
        errors.append("dataDir is not the canonical Tools/Workbench/data directory")
    if meta.get("duplicateIds"):
        errors.append("duplicate Workbench IDs are present")
    return errors


def api_request(path: str, *, method: str = "GET", body: dict[str, Any] | None = None,
                timeout: float = 2.0) -> Any:
    data = None
    headers: dict[str, str] = {}
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(
        f"{WORKBENCH_URL}{path}", data=data, headers=headers, method=method
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
        raise HookFailure(f"Workbench API {path} is unavailable: {exc}") from exc


def load_workbench() -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    meta = api_request("/api/meta")
    if not isinstance(meta, dict):
        raise HookFailure("Workbench /api/meta did not return an object")
    errors = validate_meta(meta)
    if errors:
        raise HookFailure("canonical Workbench verification failed: " + "; ".join(errors))

    roadmap = api_request("/api/roadmap")
    if not isinstance(roadmap, dict) or not isinstance(roadmap.get("items"), list):
        raise HookFailure("Workbench /api/roadmap has no items array")
    notes = api_request("/api/notes")
    if not isinstance(notes, list):
        raise HookFailure("Workbench /api/notes did not return a list")
    return meta, roadmap["items"], notes


def owned_items(items: list[dict[str, Any]], owner: str) -> list[dict[str, Any]]:
    return [
        item
        for item in items
        if item.get("owner") == owner and item.get("status") in ACTIVE_ITEM_STATUSES
    ]


def relevant_new_notes(notes: list[dict[str, Any]], item_ids: set[str]) -> list[dict[str, Any]]:
    return [
        note
        for note in notes
        if note.get("state") == "new"
        and (note.get("target") == "GENERAL" or note.get("target") in item_ids)
    ]


def open_decisions(items: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [item for item in items if item.get("type") == "decision" and item.get("status") == "open"]


def state_path(root: Path, session_id: str) -> Path:
    digest = hashlib.sha256(session_id.encode("utf-8")).hexdigest()[:24]
    return root / ".codex-coordination" / "hook-sessions" / f"{digest}.json"


def load_receipt(root: Path, session_id: str) -> dict[str, Any]:
    path = state_path(root, session_id)
    if not path.is_file():
        return {
            "version": 1,
            "session_id": session_id,
            "coordination_id": coordination_id(session_id),
            "started_at": iso_now(),
            "last_edit_at": None,
            "dirty": False,
            "announced_items": [],
        }
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        data = {}
    data.setdefault("version", 1)
    data.setdefault("session_id", session_id)
    data.setdefault("coordination_id", coordination_id(session_id))
    data.setdefault("started_at", iso_now())
    data.setdefault("last_edit_at", None)
    data.setdefault("dirty", False)
    data.setdefault("announced_items", [])
    return data


def save_receipt(root: Path, receipt: dict[str, Any]) -> None:
    path = state_path(root, str(receipt["session_id"]))
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(receipt, handle, indent=2, sort_keys=True)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temp_name, path)
    finally:
        if os.path.exists(temp_name):
            os.unlink(temp_name)


def coordination_script(root: Path, meta: dict[str, Any] | None) -> Path:
    candidates: list[Path] = []
    if meta and isinstance(meta.get("projectRoot"), str):
        candidates.append(Path(meta["projectRoot"]))
    candidates.append(root)
    for candidate in candidates:
        script = candidate / "Tools" / "CodexCoordination" / "CodexCoordination.ps1"
        if script.is_file():
            return script
    raise HookFailure("Tools/CodexCoordination/CodexCoordination.ps1 is missing")


def powershell_executable() -> str:
    return "powershell.exe" if os.name == "nt" else "pwsh"


def run_coordination(root: Path, meta: dict[str, Any] | None, command: str,
                     arguments: list[str], timeout: float = 5.0) -> None:
    script = coordination_script(root, meta)
    process = subprocess.run(
        [
            powershell_executable(),
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(script),
            command,
            *arguments,
        ],
        cwd=script.parents[2],
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )
    if process.returncode != 0:
        detail = (process.stderr or process.stdout or "unknown error").strip()
        raise HookFailure(f"coordination {command} failed: {detail[:500]}")


def coordination_session_exists(root: Path, meta: dict[str, Any] | None, owner: str) -> bool:
    candidates: list[Path] = []
    if meta and isinstance(meta.get("projectRoot"), str):
        candidates.append(Path(meta["projectRoot"]))
    candidates.append(root)
    for candidate in candidates:
        path = candidate / ".codex-coordination" / "state.json"
        if not path.is_file():
            continue
        try:
            state = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        return any(session.get("sessionId") == owner for session in state.get("sessions", []))
    return False


def ensure_registered(root: Path, meta: dict[str, Any] | None, owner: str) -> bool:
    if coordination_session_exists(root, meta, owner):
        run_coordination(root, meta, "heartbeat", ["-SessionId", owner, "-Status", "active"])
        return False
    run_coordination(
        root,
        meta,
        "register",
        [
            "-SessionId",
            owner,
            "-Goal",
            "Codex session operating under Workbench governance",
            "-CurrentTask",
            "Awaiting the first user task",
            "-Eta",
            "active",
            "-Plan",
            "1. Read Workbench; 2. Process notes; 3. Claim an item; 4. Coordinate surfaces; 5. Verify and hand off",
        ],
    )
    run_coordination(
        root,
        meta,
        "chat",
        [
            "-SessionId",
            owner,
            "-Message",
            "Session registered by the repository hook. No code surfaces are claimed until Workbench ownership is recorded.",
        ],
    )
    return True


def concise_task(prompt: object) -> str:
    if not isinstance(prompt, str):
        return "Reviewing the current user task"
    compact = re.sub(r"\s+", " ", prompt).strip()
    if not compact:
        return "Reviewing the current user task"
    if len(compact) > MAX_PROMPT_TASK:
        compact = compact[: MAX_PROMPT_TASK - 3].rstrip() + "..."
    return compact


def additional_context(owner: str, meta: dict[str, Any], items: list[dict[str, Any]],
                       notes: list[dict[str, Any]], event_name: str) -> str:
    owned = owned_items(items, owner)
    owned_ids = {str(item.get("id")) for item in owned}
    new_notes = relevant_new_notes(notes, owned_ids)
    decisions = open_decisions(items)
    lines = [
        f"PD2 Workbench hook active for coordination session {owner} ({event_name}).",
        f"Canonical Workbench: {meta.get('projectRoot')} on {meta.get('branch')} at {meta.get('head')}.",
    ]
    if owned:
        lines.append(
            "Owned active items: "
            + "; ".join(f"{item.get('id')} [{item.get('status')}] {item.get('title')}" for item in owned[:8])
        )
    else:
        lines.append("Owned active items: none. Claim or create one through the Workbench API before editing.")
    if new_notes:
        lines.append("Blocking new notes: " + ", ".join(str(note.get("id")) for note in new_notes))
    if decisions:
        lines.append(
            "Open decisions: "
            + "; ".join(f"{item.get('id')} {item.get('title')}" for item in decisions[:6])
        )
    lines.extend(
        [
            "Before edits: process targeted new notes, set the Workbench item owner to the coordination session above, and announce shared surfaces in coordination chat.",
            "Before builds, tests, runs, captures, editors, imports, or deployments: use the coordination FIFO and finish it immediately with evidence.",
            "After edits: update an owned Workbench item's status/evidence or add a durable handoff note after the final edit; otherwise the Stop hook continues the turn once.",
        ]
    )
    return "\n".join(lines)


def output_context(event_name: str, context: str, *, system_message: str | None = None) -> None:
    payload: dict[str, Any] = {
        "continue": True,
        "hookSpecificOutput": {
            "hookEventName": event_name,
            "additionalContext": context,
        },
    }
    if system_message:
        payload["systemMessage"] = system_message
    print(json.dumps(payload))


def output_deny(reason: str) -> None:
    print(
        json.dumps(
            {
                "systemMessage": reason,
                "hookSpecificOutput": {
                    "hookEventName": "PreToolUse",
                    "permissionDecision": "deny",
                    "permissionDecisionReason": reason,
                },
            }
        )
    )


def item_or_note_synced_after(items: list[dict[str, Any]], notes: list[dict[str, Any]],
                              owner: str, edit_time: datetime) -> bool:
    owned = owned_items(items, owner)
    owned_ids = {str(item.get("id")) for item in owned}
    for item in owned:
        updated = parse_stamp(item.get("updated"))
        if updated and updated > edit_time:
            return True
    for note in notes:
        stamp = parse_stamp(note.get("ts"))
        if note.get("author") == owner and note.get("target") in owned_ids and stamp and stamp > edit_time:
            return True
    return False


def handle_session_start(event: dict[str, Any], root: Path, receipt: dict[str, Any]) -> None:
    owner = str(receipt["coordination_id"])
    try:
        meta, items, notes = load_workbench()
        ensure_registered(root, meta, owner)
        save_receipt(root, receipt)
        output_context("SessionStart", additional_context(owner, meta, items, notes, "SessionStart"))
    except HookFailure as exc:
        save_receipt(root, receipt)
        output_context(
            "SessionStart",
            "Do not edit project files until the canonical Workbench and coordination preflight succeeds.",
            system_message=f"PD2 Workbench preflight failed: {exc}",
        )


def handle_subagent_start(event: dict[str, Any], root: Path, receipt: dict[str, Any]) -> None:
    owner = str(receipt["coordination_id"])
    try:
        meta, items, notes = load_workbench()
        ensure_registered(root, meta, owner)
        output_context("SubagentStart", additional_context(owner, meta, items, notes, "SubagentStart"))
    except HookFailure as exc:
        output_context(
            "SubagentStart",
            "The parent session has no verified Workbench authority. Keep the subagent read-only.",
            system_message=f"PD2 Workbench subagent preflight failed: {exc}",
        )


def handle_prompt(event: dict[str, Any], root: Path, receipt: dict[str, Any]) -> None:
    owner = str(receipt["coordination_id"])
    try:
        meta, items, notes = load_workbench()
        ensure_registered(root, meta, owner)
        run_coordination(
            root,
            meta,
            "heartbeat",
            ["-SessionId", owner, "-CurrentTask", concise_task(event.get("prompt")), "-Status", "active"],
        )
        owned = owned_items(items, owner)
        announced = set(str(value) for value in receipt.get("announced_items", []))
        current = {str(item.get("id")) for item in owned}
        newly_owned = sorted(current - announced)
        if newly_owned:
            run_coordination(
                root,
                meta,
                "chat",
                [
                    "-SessionId",
                    owner,
                    "-Message",
                    "Workbench ownership active: " + ", ".join(newly_owned) + ". Shared edit surfaces must be announced before mutation.",
                ],
            )
            receipt["announced_items"] = sorted(announced | current)
            save_receipt(root, receipt)
        output_context("UserPromptSubmit", additional_context(owner, meta, items, notes, "UserPromptSubmit"))
    except HookFailure as exc:
        output_context(
            "UserPromptSubmit",
            "Keep this turn read-only until the repository Workbench preflight is restored.",
            system_message=f"PD2 Workbench refresh failed: {exc}",
        )


def handle_pre_tool(event: dict[str, Any], root: Path, receipt: dict[str, Any]) -> None:
    tool_name = str(event.get("tool_name", ""))
    if tool_name not in EDIT_TOOL_NAMES:
        return
    owner = str(receipt["coordination_id"])
    try:
        meta, items, notes = load_workbench()
        if not coordination_session_exists(root, meta, owner):
            raise HookFailure(f"coordination session {owner} is not registered")
        owned = owned_items(items, owner)
        if not owned:
            raise HookFailure(
                f"{owner} owns no active Workbench item. Create or claim an item through the canonical API first."
            )
        item_ids = {str(item.get("id")) for item in owned}
        new_notes = relevant_new_notes(notes, item_ids)
        if new_notes:
            details = ", ".join(f"{note.get('id')}->{note.get('target')}" for note in new_notes)
            raise HookFailure(f"process targeted new Workbench notes before editing: {details}")
    except HookFailure as exc:
        output_deny(f"PD2 Workbench edit blocked: {exc}")


def handle_post_tool(event: dict[str, Any], root: Path, receipt: dict[str, Any]) -> None:
    if str(event.get("tool_name", "")) not in EDIT_TOOL_NAMES:
        return
    receipt["last_edit_at"] = iso_now()
    receipt["dirty"] = True
    save_receipt(root, receipt)
    try:
        meta, _, _ = load_workbench()
        run_coordination(
            root,
            meta,
            "heartbeat",
            [
                "-SessionId",
                str(receipt["coordination_id"]),
                "-CurrentTask",
                "Code changed; Workbench evidence or handoff is required after the final edit",
                "-Status",
                "active",
            ],
        )
    except HookFailure:
        return


def handle_stop(event: dict[str, Any], root: Path, receipt: dict[str, Any]) -> None:
    if not receipt.get("dirty"):
        return
    edit_time = parse_stamp(receipt.get("last_edit_at"))
    if edit_time is None:
        return
    owner = str(receipt["coordination_id"])
    try:
        meta, items, notes = load_workbench()
        if item_or_note_synced_after(items, notes, owner, edit_time):
            receipt["dirty"] = False
            receipt["last_synced_at"] = iso_now()
            save_receipt(root, receipt)
            run_coordination(
                root,
                meta,
                "heartbeat",
                ["-SessionId", owner, "-CurrentTask", "Workbench checkpoint current", "-Status", "active"],
            )
            return
    except HookFailure as exc:
        reason = f"Workbench verification failed after the last edit: {exc}"
    else:
        reason = (
            f"Session {owner} changed code after its last durable Workbench update. "
            "Update an owned item's status/evidence or add a durable Workbench handoff note, "
            "then verify context/tasks.md remains consistent."
        )

    if event.get("stop_hook_active") is True:
        return
    print(json.dumps({"decision": "block", "reason": reason}))


def add_handoff_note(owner: str, target: str, edit_stamp: str) -> None:
    api_request(
        "/api/notes",
        method="POST",
        timeout=0.8,
        body={
            "target": target,
            "author": owner,
            "text": (
                "Automated session-end handoff: this Codex session ended after code edits "
                f"recorded at {edit_stamp} without a later Workbench item update or durable note. "
                "Inspect the working tree and coordination state before continuing or changing truth status."
            ),
        },
    )


def handle_session_end(event: dict[str, Any], root: Path, receipt: dict[str, Any]) -> None:
    owner = str(receipt["coordination_id"])
    try:
        meta, items, notes = load_workbench()
        edit_time = parse_stamp(receipt.get("last_edit_at"))
        owned = owned_items(items, owner)
        if receipt.get("dirty") and edit_time and not item_or_note_synced_after(items, notes, owner, edit_time):
            if owned:
                add_handoff_note(owner, str(owned[0].get("id")), str(receipt.get("last_edit_at")))
            else:
                run_coordination(
                    root,
                    meta,
                    "chat",
                    [
                        "-SessionId",
                        owner,
                        "-Message",
                        "Session ended dirty without an owned Workbench item. Inspect the working tree before continuing.",
                    ],
                    timeout=1.0,
                )
        run_coordination(
            root,
            meta,
            "heartbeat",
            ["-SessionId", owner, "-CurrentTask", "Session ended; inspect any durable handoff", "-Status", "idle"],
            timeout=1.0,
        )
    except (HookFailure, subprocess.TimeoutExpired):
        return


def main() -> int:
    try:
        event = json.load(sys.stdin)
        if not isinstance(event, dict):
            raise HookFailure("hook input must be a JSON object")
        session_id = event.get("session_id")
        event_name = event.get("hook_event_name")
        if not isinstance(session_id, str) or not session_id:
            raise HookFailure("hook input has no session_id")
        if not isinstance(event_name, str) or not event_name:
            raise HookFailure("hook input has no hook_event_name")
        root = repository_root()
        receipt = load_receipt(root, session_id)

        handlers = {
            "SessionStart": handle_session_start,
            "SubagentStart": handle_subagent_start,
            "UserPromptSubmit": handle_prompt,
            "PreToolUse": handle_pre_tool,
            "PostToolUse": handle_post_tool,
            "Stop": handle_stop,
            "SessionEnd": handle_session_end,
        }
        handler = handlers.get(event_name)
        if handler is not None:
            handler(event, root, receipt)
        return 0
    except (HookFailure, json.JSONDecodeError, OSError, subprocess.SubprocessError) as exc:
        print(f"PD2 Workbench hook failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
