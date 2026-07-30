# Daily Workbench Briefing Prompt

This is the entry prompt for the scheduled Perfect Dark 2 morning session.
The repo-local Workbench is the only durable project-truth store. Do not read,
write, reconstruct, or revive the retired Kanban data.

## Run

From the repository root:

```powershell
python -m tools.daily_flow.orchestrator
```

The command is intentionally read-only with respect to project truth. It folds:

- `Tools/Workbench/data/roadmap.json`
- `Tools/Workbench/data/notes.jsonl`
- `Tools/Workbench/data/changelog.jsonl`

and atomically writes:

- `Tools/Workbench/exports/daily-briefing.json`
- `tools/daily_flow/state/last-run.json`

The briefing contains current non-complete items, unresolved decisions, notes
that still need action, truth-status counts, and recent Workbench activity.

## Required session behavior

1. Read `AGENTS.md`, `Tools/Workbench/README.md`, and
   `Tools/Workbench/SCHEMAS.md`.
2. Check `Tools/CodexCoordination` and register the session.
3. Process every `new` Workbench note affecting the session's lane before
   beginning work.
4. Surface unresolved decision items. Never answer a user-choice question by
   inventing a preference.
5. Use the Workbench server API for every durable roadmap or note mutation.
   Do not edit `roadmap.json` directly while the server is available.
6. Use the coordination FIFO for builds, tests, game runs, captures, editors,
   deployments, and other exclusive resources.
7. Treat status as verified truth. `implemented` requires connection to the
   production path; `validated` requires durable passing evidence.
8. Record evidence and handoff state in the owning Workbench item before the
   session ends.

## Boundaries

- Do not auto-merge, auto-push, stash, or mutate branches from the scheduled
  briefing job.
- Do not auto-resolve merge conflicts.
- Do not create a second task, note, decision, validation, or performance
  store in daily-flow state.
- Do not modify `context/session-log.md`; implementation sessions own their
  own closeout records.
- Do not invoke builds or game runs from the briefing job.
- If Workbench data is missing or invalid, fail loudly and report the exact
  file and error. Never fall back to a retired tracker.

When complete, report the briefing path, current open-note/decision counts,
and any parse failure that requires attention.
