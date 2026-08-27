#!/usr/bin/env python3
"""Focused tests for the repo-local Codex Workbench lifecycle hook."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
HOOK_PATH = ROOT / ".codex" / "hooks" / "workbench_session.py"
SPEC = importlib.util.spec_from_file_location("pd2_workbench_session_hook", HOOK_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Could not load hook module from {HOOK_PATH}")
HOOK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HOOK)


def stamp(moment: datetime) -> str:
    return moment.isoformat(timespec="milliseconds").replace("+00:00", "Z")


class WorkbenchSessionHookTests(unittest.TestCase):
    def test_hook_config_declares_the_enforcement_lifecycle(self) -> None:
        config = json.loads((ROOT / ".codex" / "hooks.json").read_text(encoding="utf-8"))
        hooks = config["hooks"]
        self.assertEqual(
            {
                "SessionStart",
                "SubagentStart",
                "UserPromptSubmit",
                "PreToolUse",
                "PostToolUse",
                "Stop",
                "SessionEnd",
            },
            set(hooks),
        )
        for groups in hooks.values():
            for group in groups:
                for handler in group["hooks"]:
                    self.assertEqual("command", handler["type"])
                    self.assertIn("commandWindows", handler)
                    self.assertIn("workbench_session.py", handler["command"])

    def test_coordination_id_is_stable_and_session_specific(self) -> None:
        first = HOOK.coordination_id("thr_example_123")
        self.assertEqual(first, HOOK.coordination_id("thr_example_123"))
        self.assertNotEqual(first, HOOK.coordination_id("thr_example_124"))
        self.assertRegex(first, r"^codex-[a-z0-9]+-[a-f0-9]{8}$")

    def test_meta_requires_canonical_nonisolated_data_root(self) -> None:
        root = str(ROOT)
        good = {
            "canonical": True,
            "isolated": False,
            "projectRoot": root,
            "dataDir": str(ROOT / "Tools" / "Workbench" / "data"),
            "duplicateIds": [],
        }
        self.assertEqual([], HOOK.validate_meta(good))
        for patch in (
            {"canonical": False},
            {"isolated": True},
            {"dataDir": str(ROOT / "wrong")},
            {"duplicateIds": ["T-001"]},
        ):
            bad = dict(good)
            bad.update(patch)
            self.assertTrue(HOOK.validate_meta(bad), patch)

    def test_owned_items_and_new_notes_are_exactly_scoped(self) -> None:
        items = [
            {"id": "T-1", "owner": "session", "status": "partial"},
            {"id": "T-2", "owner": "session", "status": "validated"},
            {"id": "T-3", "owner": "other", "status": "partial"},
        ]
        self.assertEqual(["T-1"], [item["id"] for item in HOOK.owned_items(items, "session")])
        notes = [
            {"id": "N-1", "target": "T-1", "state": "new"},
            {"id": "N-2", "target": "T-3", "state": "new"},
            {"id": "N-3", "target": "GENERAL", "state": "new"},
            {"id": "N-4", "target": "T-1", "state": "acknowledged"},
        ]
        self.assertEqual(
            ["N-1", "N-3"],
            [note["id"] for note in HOOK.relevant_new_notes(notes, {"T-1"})],
        )

    def test_pre_edit_denies_without_owned_item(self) -> None:
        event = {"tool_name": "apply_patch"}
        receipt = {"coordination_id": "session"}
        with mock.patch.object(HOOK, "load_workbench", return_value=({}, [], [])), mock.patch.object(
            HOOK, "coordination_session_exists", return_value=True
        ):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                HOOK.handle_pre_tool(event, ROOT, receipt)
        payload = json.loads(output.getvalue())
        self.assertEqual("deny", payload["hookSpecificOutput"]["permissionDecision"])
        self.assertIn("owns no active Workbench item", payload["hookSpecificOutput"]["permissionDecisionReason"])

    def test_pre_edit_denies_targeted_new_note_and_allows_clean_owner(self) -> None:
        event = {"tool_name": "apply_patch"}
        receipt = {"coordination_id": "session"}
        items = [{"id": "T-1", "owner": "session", "status": "partial"}]
        notes = [{"id": "N-1", "target": "T-1", "state": "new"}]
        with mock.patch.object(HOOK, "load_workbench", return_value=({}, items, notes)), mock.patch.object(
            HOOK, "coordination_session_exists", return_value=True
        ):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                HOOK.handle_pre_tool(event, ROOT, receipt)
        self.assertIn("N-1->T-1", output.getvalue())

        with mock.patch.object(HOOK, "load_workbench", return_value=({}, items, [])), mock.patch.object(
            HOOK, "coordination_session_exists", return_value=True
        ):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                HOOK.handle_pre_tool(event, ROOT, receipt)
        self.assertEqual("", output.getvalue())

    def test_sync_requires_item_update_or_owned_note_after_edit(self) -> None:
        now = datetime.now(timezone.utc)
        items = [
            {
                "id": "T-1",
                "owner": "session",
                "status": "partial",
                "updated": stamp(now - timedelta(seconds=1)),
            }
        ]
        self.assertFalse(HOOK.item_or_note_synced_after(items, [], "session", now))
        items[0]["updated"] = stamp(now + timedelta(seconds=1))
        self.assertTrue(HOOK.item_or_note_synced_after(items, [], "session", now))
        items[0]["updated"] = stamp(now - timedelta(seconds=1))
        notes = [
            {
                "target": "T-1",
                "author": "session",
                "state": "new",
                "ts": stamp(now + timedelta(seconds=1)),
            }
        ]
        self.assertTrue(HOOK.item_or_note_synced_after(items, notes, "session", now))

    def test_stop_continues_once_until_workbench_is_current(self) -> None:
        now = datetime.now(timezone.utc)
        receipt = {
            "session_id": "thr_stop_test",
            "coordination_id": "session",
            "last_edit_at": stamp(now),
            "dirty": True,
        }
        stale_items = [
            {
                "id": "T-1",
                "owner": "session",
                "status": "partial",
                "updated": stamp(now - timedelta(seconds=1)),
            }
        ]
        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.object(
            HOOK, "load_workbench", return_value=({}, stale_items, [])
        ):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                HOOK.handle_stop({"stop_hook_active": False}, Path(temp_dir), dict(receipt))
            payload = json.loads(output.getvalue())
            self.assertEqual("block", payload["decision"])

        current_items = [dict(stale_items[0], updated=stamp(now + timedelta(seconds=1)))]
        current_receipt = dict(receipt)
        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.object(
            HOOK, "load_workbench", return_value=({}, current_items, [])
        ), mock.patch.object(HOOK, "run_coordination"):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                HOOK.handle_stop({"stop_hook_active": False}, Path(temp_dir), current_receipt)
            self.assertEqual("", output.getvalue())
            self.assertFalse(current_receipt["dirty"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
