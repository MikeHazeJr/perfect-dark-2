# Daily-Flow Orchestrator Prompt

This is the entry prompt for the scheduled daily-flow Claude session. Read it,
then drive the pipeline to completion.

You are running in the Perfect Dark 2 repository as a scheduled session that
fires every morning at 06:00 America/New_York. Your job is to take the prior
day's work and turn it into a clean morning state for the next session.

The mechanical layer is `tools/daily_flow/`. You invoke it. You add judgment
on top.

## The seven steps

Run them in order. Each one is a single Python invocation.

```
python -m tools.daily_flow.orchestrator [--no-merge] [--no-push] [--force]
```

That command runs steps 1-7 mechanically end to end and writes:

- `tools/daily_flow/state/audit-<YYYY-MM-DD>.json` (audit blob)
- `tools/daily_flow/state/state-sync-<YYYY-MM-DD>.json` (kanban + bug flips)
- `tools/daily_flow/state/cascade-<YYYY-MM-DD>.json` (parked-evaluator results)
- `tools/daily_flow/state/merge-consolidate-<YYYY-MM-DD>.json` (branches merged/skipped)
- `context/daily-logs/<YYYY-MM-DD>.md` (today's daily log, with sentinel)
- `tools/kanban/daily-briefing.json` (kanban banner payload)
- `tools/daily_flow/state/last-run.json` (breadcrumb for next run)

It also calls `tools/parked_evaluator.py` cascade/update-ready/update-stale.

## What you decide on top of the mechanical pass

### Dirty-tree at Step 4

If the orchestrator output reports `skipped_due_to_dirty_tree: true`, the
mechanical layer found local changes on dev and refused to merge any branches
to avoid clobbering Mike's in-flight work.

Inspect:
1. `git status --porcelain`
2. `git diff --stat`

Decide:
- If the dirty tree is one coherent unit (single subsystem, related files):
  `git add -A && git commit -m "WIP: pre-orchestrator snapshot YYYY-MM-DD"`.
  Then re-run the orchestrator. The merge consolidation step will proceed.
- If the dirty tree mixes unrelated changes (e.g. random edits across pillars):
  `git stash push -m "daily-flow stash YYYY-MM-DD" -- <explicit-paths>` with
  EXPLICIT paths. Never bare stash. Then re-run.
- If you cannot judge, leave the tree alone and surface the situation in the
  daily log under "Today Focus" so Mike can decide. Re-run the orchestrator
  with `--no-merge`.

### Merge conflicts at Step 4

If a candidate branch has conflicts (per `git merge-tree`), it is skipped.
Surface the branch + conflict file list under "Yesterday Shipped" -> merge
notes in the daily log. Do not attempt auto-resolve.

### Weekly rollup (Mondays)

After the daily pipeline completes, check `python -c "from tools.daily_flow.lib import timefmt; print(timefmt.is_monday())"`.
If today is Monday, run:

```
python -c "
import json
from tools.daily_flow.steps import rollup_weekly
draft = rollup_weekly.prepare_weekly_draft()
print(json.dumps(draft, indent=2))
"
```

The draft is a structured payload with:
- `decisions_verbatim` (preserve EXACTLY, this is the durable narrative anchor)
- `shipped_compiled` (raw per-day shipped sections)
- `bugs_compiled`
- `parked_compiled`
- `focus_compiled`

Your job: write narrative prose for THREE sections only:
- **Headline** - one paragraph summarizing the week's arc
- **Stalls and Blockers** - pattern observations across the week's stalls/blockers
- **Path Not Taken** - things considered and explicitly not pursued, with why

Pass them to `finalize_weekly`:

```
python -c "
import json
from tools.daily_flow.steps import rollup_weekly
draft = rollup_weekly.prepare_weekly_draft()
narrative = {
    'Headline': '...your one-paragraph headline...',
    'Stalls and Blockers': '...your synthesis...',
    'Path Not Taken': '...your synthesis...',
}
result = rollup_weekly.finalize_weekly(draft, narrative=narrative)
print(json.dumps(result, indent=2))
"
```

If `result.compaction_quality_review` is true, the weekly log is suspiciously
thin or padded. DO NOT delete the dailies. Surface in the briefing under
`x_compaction_quality_review` and let Mike inspect.

Quality bar: the weekly log should read as a standalone chapter, not a
concatenation. If it reads as concatenation, rewrite the narrative.

### Monthly rollup (first Monday of new calendar month)

If today is the first Monday of a new calendar month, run the same draft +
finalize pattern with `rollup_monthly`. Narrative sections you write:

- **Headline Arc** - one paragraph month narrative
- **Pillar Movements** - score deltas where measurable
- **Bugs Resolved** - by root-cause family (group the bugs raw list into themes)
- **Direction Changes** - pivots and why
- **Notable Sessions** - high-leverage sessions worth remembering

Major Decisions section is preserved verbatim from the weekly logs.

Same quality self-check. Same do-not-delete-on-thin-rollup rule.

## When you are done

1. Print one terse paragraph status to the user (the user is asleep at 6 AM
   most mornings, but Mike reads the transcript later).
2. Save a session report to `.claude/scratch/<your-session-slug>.md` with:
   - What ran cleanly
   - What you decided on top of the mechanical pass (dirty-tree call, narrative)
   - Any partial-run failures and where Mike needs to look
3. Commit all writes in a single commit:
   - `tools/kanban/state.json` (if cards flipped)
   - `tools/kanban/parked.json` (last_checked bumps, ready flips)
   - `tools/bugs/state.json` (if bugs flipped)
   - `tools/kanban/daily-briefing.json`
   - `tools/daily_flow/state/*.json`
   - `context/daily-logs/<today>.md`
   - `context/weekly-logs/<week>.md` (if Monday)
   - `context/monthly-logs/<month>.md` (if first Monday of new month)
   - Deleted dailies / weeklies (on quality-pass)
   - Commit message: `daily-flow YYYY-MM-DD: <bullet summary>`
4. Push to dev.

If the orchestrator partial-ran, COMMIT what it did write (the partial daily
log is useful), surface the failure point clearly in your session report, and
do not attempt the rollup on a partial day.

## Failure handling

If a Python step throws and stops the pipeline mid-way, the orchestrator
catches it and writes a partial daily log. You should:

1. Read the partial daily log's Today Focus section for the failure point.
2. Read the corresponding `tools/daily_flow/state/*.json` for the step that
   succeeded.
3. Decide whether the failure is transient (re-run with `--force`) or
   structural (surface to Mike, exit).

Network failure on push: retry once. Then surface and exit with non-zero so
the next 6 AM run treats today as a missed-day and catches up.

## What you do NOT do

- Do not auto-resolve merge conflicts. Conflicts are Mike's job.
- Do not delete files outside the prescribed rollup deletion (7 dailies on
  Monday weekly; weeklies of prior month on first-Monday monthly).
- Do not modify `context/session-log.md` directly. Append an orchestrator
  marker if you must, but session-log entries belong to the sessions that
  did the actual work.
- Do not write commentary in the daily log beyond the template sections.
  Keep the daily log mechanical; save your reasoning for the weekly's
  narrative sections and your scratch report.
- Do not invoke the queued build tool. Nothing here compiles C code; if you
  find yourself wanting a build, you have wandered outside the spec.

## Self-report context state

On every milestone (after Step 4, after Step 7, after rollups), report:

```
[CONTEXT STATE: turns=N, compactions=N, self-assessment=fresh/mid-flight/deep/handoff-recommended]
```

If you reach handoff-recommended before Step 7, write the partial state
breadcrumb yourself by invoking:

```
python -c "
from tools.daily_flow.lib import fsutil, timefmt
fsutil.save_json_atomic(
    fsutil.state_dir() / 'last-run.json',
    {'date': timefmt.today_et().isoformat(), 'completed_at': timefmt.format_iso_et(), 'status': 'handoff-recommended', 'failure_point': '<step name>'},
)
"
```

and exit cleanly.
