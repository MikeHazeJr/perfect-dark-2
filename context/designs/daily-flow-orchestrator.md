# Daily-Flow Orchestrator

> Pillar: Tooling. Status: SPEC v0.5 IMPLEMENTING (2026-05-11). Design owner: Mike. Author: Claude (vigilant-stonebraker).

The Daily-Flow Orchestrator is a scheduled Claude session that runs every morning, reconciles the prior day's work into structured logs, advances state across kanban + parked + bugs + smoke-verify, consolidates pending worktree merges, and writes a focused briefing for the next session.

The aim is to turn the data layer (`tools/kanban/state.json`, `tools/kanban/parked.json`, `tools/bugs/state.json`, `.claude/smoke-verify-runs/`) plus the build-time smoke gate into a reliable, hands-off morning ritual. No human is in the loop on most mornings. When it cannot finish, it leaves a clear partial-state breadcrumb.

---

## Architecture

Two cooperating layers:

1. **Mechanical layer (Python)**. Idempotent, pure-data scripts under `tools/daily_flow/`. They read JSON state, run git commands, parse the session log, write daily-log markdown, and emit the briefing payload. Nothing in the mechanical layer makes judgment calls.

2. **Reasoning layer (Claude)**. The scheduled session runs the `tools/daily_flow/orchestrator-prompt.md` instructions. It invokes the Python steps in order, judges the few decisions the mechanics cannot (coherent-vs-incoherent dirty tree, merge conflict handling, rollup quality), and writes the result.

Layer boundary rules:

- Anything that can be deterministic is deterministic.
- The Python layer never asks for input. If it cannot proceed, it writes a partial-state JSON breadcrumb and exits non-zero.
- The Claude layer never re-implements what Python already does; it calls the script and consumes the JSON output.

---

## Schedule

Daily at **06:00 America/New_York** (Eastern Time, DST-aware). Registered via the `anthropic-skills:schedule` skill (cron expression in ET).

The scheduled task spawns a fresh Claude Code session in the PD2 working tree and invokes `tools/daily_flow/orchestrator-prompt.md` as the entry prompt. The session runs to completion and exits.

**Catch-up handling.** The first thing the orchestrator does is consult `tools/daily_flow/state/last-run.json` (a single-line breadcrumb `{ "date": "YYYY-MM-DD", "completed_at": "..." }`). If the previous calendar day has no entry and a daily log is missing for it, the orchestrator runs a catch-up pass for that day before starting today. Catch-up uses the same pipeline but reads git history with `--since` clamped to that day's window.

Missed-multi-day handling: catch up the oldest missing day first, write its log, then advance the cursor. Stop after the most recent complete day; do not write a partial daily log for today before 6 AM.

---

## Scheduling Surface

The Claude desktop app maintains TWO independent scheduler namespaces, each with its own MCP server endpoint and its own SKILL.md storage path. Tasks created from one namespace are invisible to the other.

| Namespace | SKILL.md storage | Registry JSON | Visible from |
|-----------|------------------|---------------|--------------|
| Code-mode | `~/.claude/scheduled-tasks/<id>/SKILL.md` | `AppData/Roaming/Claude/claude-code-sessions/.../scheduled-tasks.json` | Claude Code sessions (`mcp__scheduled-tasks__list_scheduled_tasks` from a code session) |
| Local-agent-mode | `OneDrive/Documents/Claude/Scheduled/<id>/SKILL.md` | `AppData/Roaming/Claude/local-agent-mode-sessions/.../scheduled-tasks.json` | Cowork/dispatch sessions AND the consumer desktop app's Capabilities Memory/Schedule view |

Both namespaces fire from the same desktop-app scheduler service: when the cron matches, the app launches a new claude-desktop process with the SKILL.md prompt as entry. Both namespaces honor cron in local time (Eastern for our setup) with a small deterministic jitter (~3 to 5 minutes) for load balancing.

**Where pd2-daily-flow lives (as of 2026-05-12):** BOTH namespaces. The original registration via `anthropic-skills:schedule` from the orchestrator session landed in the code-mode namespace only. c122 duplicated the entry into the local-agent-mode namespace so Mike can see and manage it from the consumer desktop app.

**Why both, not just one:** the dedup guard in `tools/daily_flow/orchestrator.py` makes concurrent fire safe (see Idempotence). Leaving both registrations active is belt-and-suspenders: if the consumer-app registry write gets clobbered by the desktop app at some future point, the code-mode fire still runs the morning pipeline. Conversely, if the code-mode namespace ever moves or breaks, the local-agent-mode fire keeps the pipeline alive.

**Dedup guard.** `orchestrator.py main()` checks `state/last-run.json` immediately after resolving `today`. If `last.date == today.isoformat()` and `last.status == "ok"`, it emits `DAILY-FLOW.ORCHESTRATOR.DEDUP` and exits 0 without running catch-up or the pipeline. The `--force` flag bypasses the guard for manual reruns. Concurrent fires from both schedulers are made safe because the first to complete writes `last-run.json` with status ok; the second's guard catches it and exits in well under a second. The two SKILL.md files are byte-identical copies, so neither variant of the prompt drifts ahead of the other.

**If the registry edit gets clobbered.** The local-agent-mode registry is held in memory by the running desktop-app process. If the app rewrites the JSON without merging external file edits, the pd2-daily-flow entry can disappear after an app shutdown or scheduler heartbeat. Recovery: open a Cowork or dispatch session and call `mcp__scheduled-tasks__create_scheduled_task` with `taskId: "pd2-daily-flow"`, `cronExpression: "0 6 * * *"`, and the prompt body from the existing OneDrive SKILL.md. The SKILL.md file itself is durable on disk and survives registry churn, so the dispatch-side create call only needs to add the registry row.

**To remove the duplicate registration** (if Mike decides one namespace is sufficient): call `mcp__scheduled-tasks__update_scheduled_task` with `taskId: "pd2-daily-flow"` and `enabled: false` from the namespace you want to silence. The dedup guard will still protect against any future reactivation drift.

---

## Pipeline (seven steps)

### Step 1 - Previous-day audit

Inputs: `git log --since="24 hours ago" dev`, `context/session-log.md`, kanban state snapshot from yesterday (cached at `tools/daily_flow/state/kanban-snapshot-YYYY-MM-DD.json`), `.claude/smoke-verify-runs/results-*.json` from the window, `context/daily-logs/<yesterday>.md` if it exists.

Output: in-memory audit blob with keys `commits`, `sessions`, `kanban_transitions`, `smoke_results`, `prev_log`. Persisted to `tools/daily_flow/state/audit-YYYY-MM-DD.json` so downstream steps and re-runs share a single audit pass.

Idempotence: if the audit file exists for today, load it instead of recomputing.

### Step 2 - State sync

Card flip rule: a card flips to `done` if AND only if all of these hold:
- The card has at least one commit on a `claude/*` branch or on `dev` in the audit window.
- A scratch file `.claude/scratch/<slug>.md` exists referencing the same slug as the commit author branch.
- The card's `column` is currently `active` or `blocked`.

Bug flip rule: a bug flips to `fixed` if AND only if all of these hold:
- The bug status is currently `fix-pending-verification`.
- The bug has a non-empty `linked_test`.
- The latest smoke-verify result for that test (by mtime in `.claude/smoke-verify-runs/results-*.json`) has `Passed = true` AND was produced at or after the bug's `fix_commit` reference.

`last_checked` is bumped to today for every parked entry. Idempotent.

Idempotence: each flip writes back the same state if invoked twice with the same audit. The script computes a deterministic set of (card_id, new_column) and (bug_id, new_status) tuples and applies them in a single atomic write.

### Step 3 - Condition cascade

For each card flipped to `done` in this run, invoke:

```
python tools/parked_evaluator.py cascade --card-done <id>
```

Then globally:

```
python tools/parked_evaluator.py update-ready
python tools/parked_evaluator.py update-stale
```

Cascade events (parent card -> which parked threads got unblocked) are captured to `tools/daily_flow/state/cascade-YYYY-MM-DD.json` for the summary step.

### Step 4 - Pending-merge consolidation

Enumerate `claude/*` branches ahead of dev:

```
git for-each-ref --format='%(refname:short) %(committerdate:iso8601-strict)' refs/heads/claude/
```

For each, in oldest-committer-date-first order:

1. Skip if the branch's worktree appears in `git worktree list` as locked (an active session).
2. Dry-run with `git merge-tree HEAD <branch>` to check for conflict markers. If conflicts, surface the branch + conflicting file list to the summary and skip.
3. Check dev working tree via `git status --porcelain`. If dirty:
   - Claude judges coherence from `git diff --stat`. A coherent dirty tree (single subsystem, related files) gets committed as WIP with a clear message ("WIP: pre-merge snapshot before <branch>"). An incoherent dirty tree gets `git stash push -- <explicit-files>` with the files Claude lists (never bare stash; per project memory).
   - Worst case (Claude cannot judge): skip the merge for this branch, surface to summary.
4. Pre-merge snapshot: HEAD SHA + `git diff --stat` line counts.
5. Merge with `--no-ff -m "Merge <branch>: <short title>"`.
6. Post-merge line-count verify: any unexpected shrink halts and reports.
7. Push.
8. `git branch -D <branch>` and `git worktree remove .claude/worktrees/<slug>` if present.
9. Loop.

Surface to summary: branches merged, branches skipped (with reason), conflict file lists.

### Step 5 - Priority sort

Kanban cards (active + backlog) ordered by:
1. `flag` priority desc (`"P0" > "P1" > "P2" > null`).
2. Pillar weight (Engine + Catalog + Connectivity highest; pillar weights table embedded in `tools/daily_flow/lib/priority.py`).
3. `created` ascending (older first).

Bugs ordered by:
1. Severity (`blocker > critical > major > minor`; matched against bugs.json values).
2. `filed_date` ascending.

Parked-ready threads ordered by `parked_date` ascending (oldest first).

The sort yields a focus shortlist (top 3-5) for the summary.

### Step 6 - Summary generation

Writes `context/daily-logs/YYYY-MM-DD.md` with sections:

```
# Daily Summary YYYY-MM-DD

> Orchestrator run [mm-dd-yyyy - hh:mm]. Window: <start> to <end>.

## Yesterday Shipped
- <commit/session/card bullets>

## Bugs
- Filed: <list or "none">
- Resolved: <list with fix commit or "none">
- Regressions caught: <list or "none">

## Decisions
<empty if none, headers retained>

### <decision-title>
- Decision id: dec-NNN
- Decided: <what>
- Why: <rationale>
- Alternatives considered: <list>
- What we did NOT do: <list with reasons>
- Implications: <forward-looking>

## Parked Threads
- Newly parked: <list>
- Newly unparked: <list>
- Cascade events: <list>
- Stale items needing triage: <list>

## Today Focus
1. <item> (rationale)
2. <item> (rationale)
...

---
SENTINEL: daily-flow YYYY-MM-DD complete.
```

Empty sections collapse to a single `(none)` line but headers remain. The sentinel at the end is a truncation guard per project procedures.

### Step 7 - Day-prep briefing payload

Writes `tools/kanban/daily-briefing.json` with:

```json
{
  "schema_version": 1,
  "semantic_version": "0.1.0",
  "x_extensibility_rule": "Additive-only. Reserved namespace: x_*.",
  "generated_at": "YYYY-MM-DDTHH:MM:SS-04:00",
  "for_date": "YYYY-MM-DD",
  "partial": false,
  "headlines": ["...", "...", "..."],
  "ready_to_resume": { "count": N, "items": ["pt-NNN", "..."] },
  "blocker_bugs": { "count": N, "items": ["B-NNN", "..."] },
  "focus": [
    { "kind": "card", "id": "cNNN", "title": "...", "rationale": "..." }
  ],
  "x_failure_point": null
}
```

The kanban browser reads it on load (existing `server.py` gains a `/api/briefing` endpoint) and surfaces a top banner that dismisses to a small icon.

---

## Weekly rollup

Triggered on Mondays after the daily run. Reads the seven `context/daily-logs/YYYY-MM-DD.md` files for the prior Mon-Sun, distills them into `context/weekly-logs/YYYY-WNN.md`. Sections:

```
# Week YYYY-WNN

> Generated [mm-dd-yyyy - hh:mm]. Window: <Mon> to <Sun>.

## Headline
<one paragraph>

## Shipped
<grouped by pillar>

## Bugs
<totals + notable families>

## Decisions This Week
<dec-NNN entries lifted verbatim, fully preserved>

## Parked Activity
<net park/unpark, cascade summary>

## Stalls and Blockers
<pattern observations>

## Path Not Taken
<things considered and explicitly not pursued, with why>

---
SENTINEL: weekly-flow YYYY-WNN complete.
```

After write + commit, the 7 daily files are deleted in the same commit. The weekly log is the durable narrative; dailies are scaffolding.

**Decisions are the durable narrative anchor.** They lift verbatim from daily logs and survive into the weekly log unchanged.

**Quality self-check:** if the weekly file is under 80 lines or over 600 lines, the orchestrator flags it as `compaction_quality_review: true` in the briefing payload and the dailies are NOT deleted that morning.

---

## Monthly rollup

Triggered on the first Monday of a new calendar month after the weekly. Reads the prior month's `context/weekly-logs/YYYY-WNN.md` files, distills into `context/monthly-logs/YYYY-MM.md`. Sections:

```
# Month YYYY-MM

> Generated [mm-dd-yyyy - hh:mm].

## Headline Arc
<one paragraph month narrative>

## Pillar Movements
<score deltas where measurable; stability, online, modding, etc.>

## Major Decisions
<dec-NNN entries with full rationale and rejected alternatives>

## Bugs Resolved
<by root-cause family>

## Direction Changes
<pivots and why>

## Notable Sessions
<high-leverage code sessions worth remembering>

---
SENTINEL: monthly-flow YYYY-MM complete.
```

After write + commit, the prior month's weekly files are deleted in the same commit.

**Quality self-check:** under 120 lines or over 1200 lines flags `compaction_quality_review: true` and the weeklies are NOT deleted.

---

## Decision-ID registry

Decisions get a monotonically-increasing ID at write time. The registry is `tools/daily_flow/decisions.json` (note: outside the per-day `state/` subdir, because it is durable across all daily runs and IS committed to git, unlike state/ which is gitignored operational data):

```json
{
  "schema_version": 1,
  "semantic_version": "0.1.0",
  "next_id": 42,
  "decisions": [
    {
      "id": "dec-001",
      "date": "2026-05-11",
      "title": "...",
      "daily_log": "context/daily-logs/2026-05-11.md",
      "weekly_log": null,
      "monthly_log": null,
      "x_meta": {}
    }
  ]
}
```

When a daily log lifts into a weekly, the registry's `weekly_log` field gets updated. Same on monthly rollup. The ID never changes; it threads through all three log tiers.

Sessions can append decisions to their own scratch files using the format `### dec-PROPOSED: <title>` and the orchestrator allocates the next ID at write time.

---

## Idempotence and failure handling

- **Re-run on same day**: each Python step computes an `audit_hash` over its inputs (git HEAD SHA, kanban state mtime, etc.) and skips when the result file's `audit_hash` matches. Forced re-run via `--force` flag.
- **Step failure**: the orchestrator writes a partial daily log with the failure noted at the section that failed, sets `partial: true` in the briefing, and includes `x_failure_point` in the briefing payload. Subsequent steps run with degraded input where possible.
- **Network failure on push**: retry once with 30 s backoff, then surface to summary.
- **State file corruption**: the orchestrator refuses to overwrite a malformed JSON and surfaces a diff against the most recent good copy.
- **Compaction quality self-check**: rollup output line counts are checked against thresholds (80 / 600 weekly, 120 / 1200 monthly). Off-threshold flags `compaction_quality_review` and the prior-tier files are NOT deleted that morning.

---

## Pipeline diagram

```
06:00 ET (cron)
        |
        v
    Claude session spawned in PD2 worktree
        |
        v
    orchestrator-prompt.md (Claude reads)
        |
        v
    Step 1 audit -> state/audit-YYYY-MM-DD.json
        |
        v
    Step 2 state sync (write kanban + bugs + parked.last_checked)
        |
        v
    Step 3 cascade (parked_evaluator.py x3)
        |
        v
    Step 4 merge consolidation (claude/* branches -> dev)
        |
        v
    Step 5 priority sort (in-memory)
        |
        v
    Step 6 daily log write -> context/daily-logs/YYYY-MM-DD.md
        |
        v
    Step 7 briefing -> tools/kanban/daily-briefing.json
        |
        v
    [Monday only] weekly rollup -> context/weekly-logs/YYYY-WNN.md
        |
        v
    [First Monday of new month] monthly rollup -> context/monthly-logs/YYYY-MM.md
        |
        v
    state/last-run.json updated
        |
        v
    Commit + push (single commit covering all writes)
```

---

## Integration points

- **Kanban**: reads/writes `tools/kanban/state.json`. Calls `server.py /api/cards/:id PATCH` when the server is up (port 7531 probe); falls back to atomic JSON rewrite when down.
- **Parked**: reads `tools/kanban/parked.json`. Invokes `tools/parked_evaluator.py` for cascade + update-ready + update-stale.
- **Bugs**: reads/writes `tools/bugs/state.json`. Bug status transitions go through `parked_evaluator.py set_bug_status` (library import) to keep semantics consistent.
- **Smoke-verify**: reads `.claude/smoke-verify-runs/results-*.json` (latest by mtime per test name).
- **Session log**: appends a single line entry under today's date noting the orchestrator run; never overwrites existing entries.
- **Dev Window**: `devtools/dev-window-v2/dev-window-v2.ps1` gains a "Briefing" banner that reads `tools/kanban/daily-briefing.json` when the kanban Open button is clicked (banner surfaces in the new tab the browser opens; banner area added to `tools/kanban/index.html` via `/api/briefing`).

---

## Failure modes (full enumeration)

| Mode | Detection | Response |
|------|-----------|----------|
| Missed 6 AM (machine asleep) | `state/last-run.json` is older than 24h | Catch-up oldest-first |
| Git push failed | Push command exit != 0 | One retry with 30 s backoff, then surface |
| Merge conflict | `git merge-tree` non-empty conflict markers | Skip branch, surface in summary |
| Dirty dev tree, uncoherent | Claude judgment | Stash explicit paths or skip merge |
| State file malformed JSON | json.load raises | Refuse overwrite, surface diff |
| Rollup too thin / padded | Line-count threshold | Set `compaction_quality_review` flag, do NOT delete prior-tier files |
| `parked_evaluator.py` non-zero exit | subprocess returncode | Surface in summary, continue with remaining steps |
| Daily log already written today | `audit_hash` match | No-op |
| Decision-ID file corrupted | json.load raises | Refuse overwrite, surface diff |

---

## File layout

```
tools/daily_flow/
  orchestrator.py                 # entrypoint, runs all steps
  orchestrator-prompt.md          # the Claude prompt the scheduled task invokes
  decisions.json                  # monotonic decision-id registry (committed)
  steps/
    step1_audit.py
    step2_state_sync.py
    step3_cascade.py
    step4_merge_consolidate.py
    step5_priority_sort.py
    step6_daily_log.py
    step7_briefing.py
    rollup_weekly.py
    rollup_monthly.py
  lib/
    fsutil.py                     # atomic JSON read/write, snapshot helpers
    gitutil.py                    # git log/diff/merge subprocess wrappers
    priority.py                   # pillar weights, sort comparators
    templates.py                  # daily/weekly/monthly markdown templates
    timefmt.py                    # ET tz helpers + [mm-dd-yyyy - hh:mm] formatter
    decisions.py                  # decision registry helpers
  state/                          # gitignored; per-day operational cache
    last-run.json
    audit-YYYY-MM-DD.json         # per-day audit cache
    cascade-YYYY-MM-DD.json       # per-day cascade events
    state-sync-YYYY-MM-DD.json    # per-day card + bug flip log
    merge-consolidate-YYYY-MM-DD.json  # per-day branch merge results
    kanban-snapshot-YYYY-MM-DD.json    # nightly kanban snapshot for diff

context/daily-logs/<YYYY-MM-DD>.md
context/weekly-logs/<YYYY-WNN>.md
context/monthly-logs/<YYYY-MM>.md

tools/kanban/daily-briefing.json  # surfaced by dev-window kanban open
```

---

## What this design does NOT do

- It does NOT auto-resolve merge conflicts. Conflicts surface and wait for Mike.
- It does NOT block on a build. Build verification is the responsibility of the session that wrote the code; the orchestrator only consolidates already-built branches.
- It does NOT trigger smoke-verify runs; it consumes results that the build pipeline already produced.
- It does NOT decide what is in scope for a new day's work. The focus shortlist is a suggestion, not an assignment.
- It does NOT delete daily logs that fail the quality self-check.
- It does NOT survive across project relocation; paths are absolute to the worktree root via `git rev-parse --show-toplevel`.

---

## Phase 2 enhancements (not in v0.5 ship)

These are scoped here so a follow-up session can pick them up without re-discovery. Each is additive; none requires breaking the v0.5 contract.

### Phase 2A. Commit-hygiene audit

**Trigger**: Mike's directive 2026-05-11. Commit messages must follow a standard format tied to the kanban card driving the work. A separate session is building the prevention layer (pre-commit hook). The orchestrator gains the detection layer in Step 1.

**Standard** (per `feedback_commit_message_standard.md` in Mike's auto-memory, expected to be authored by the parallel session that filed this requirement):

- First line: `<Pillar> - <CardID>: <summary>` (e.g. `Catalog - c027: F11 retirement of g_Weapons[]`).
- Body: 2-4 sentences explaining the change.
- Trailer: `Refs: cNNN` line citing the kanban card. Multiple references allowed: `Refs: c118, c050`.

Note: this is a documentation pointer, not a duplicate of the standard. The memory file is authoritative; this doc points to it.

**Audit (Step 1 extension)**:

The audit step gains a `commit_hygiene` subsection in its output JSON. For each commit in the 24 h window:

```json
{
  "sha": "...",
  "subject": "...",
  "compliant": true | false,
  "format_violations": [
    "missing_pillar_prefix",
    "missing_card_id",
    "missing_refs_trailer",
    "body_too_short",
    "body_too_long"
  ],
  "referenced_cards": ["c118", "c050"]
}
```

Compliance check is regex-based. Pillar names enumerate from `kanban/state.json::pillars[].id`. Card IDs match `c\d{3,}`. Allow merge commits (subject starts with `Merge `) and bot commits (author matches a configurable list) to pass without enforcement.

**New daily log section: `## Commit Hygiene`**

Inserts after `## Bugs` and before `## Decisions` (template order updated to: Yesterday Shipped, Bugs, Commit Hygiene, Decisions, Parked Threads, Today Focus). Empty when 100% compliant. When non-compliant:

```
## Commit Hygiene

- Compliance: N/M commits (PP%) over the 24 h window.
- Non-compliant:
  - `<sha>` <subject>  -- violations: missing_card_id, body_too_short
  - `<sha>` <subject>  -- violations: missing_pillar_prefix
```

**Briefing payload extension**:

Adds a top-level `commit_hygiene` field:

```json
{
  "commit_hygiene": {
    "window_compliance_rate": 0.86,
    "non_compliant_count": 2,
    "trailing_7d_compliance_rate": 0.91,
    "x_trend": "stable"
  }
}
```

The 7-day trailing rate computes from prior `audit-YYYY-MM-DD.json` files in `state/`. The trend (stable / improving / regressing) compares the 7 d rate to the 30 d rate (when available). Surfaced as a small pill in the kanban banner.

**Where to slot it in code**:

- New module `tools/daily_flow/lib/commit_hygiene.py`: pure functions `parse_commit_subject(subject) -> dict`, `validate(commit, pillars) -> list[violation]`, `compliance_rate(commits) -> float`.
- `steps/step1_audit.py`: after building the `commits` list, fold each commit through `validate()`, attach the result. The pillar list comes from `tools/kanban/state.json`.
- `steps/step6_daily_log.py`: new `_render_commit_hygiene()` helper called from `run_daily_log`. Update `DAILY_SECTIONS_ORDER` in `templates.py` to add `Commit Hygiene` between `Bugs` and `Decisions`.
- `steps/step7_briefing.py`: assemble `commit_hygiene` block from the audit's results plus historical trailing-window data.

**Phase 2A is intentionally not in v0.5** because (a) the standard's exact regex shape depends on the prevention-layer pre-commit hook a parallel session is authoring, and (b) the v0.5 ship is already a coherent unit. Coupling them risks re-opening the v0.5 merge or introducing drift between detection and prevention.

### Phase 2B (future, not yet scoped)

- Per-pillar commit-velocity dashboard in the briefing payload.
- Stale-card auto-archive after N days of zero commit activity touching the card.
- Bug-age histogram for the briefing (open bugs by filed-date bucket).
- Auto-spawn of catch-up sessions on N+ consecutive missed days (currently the orchestrator catches up sequentially; auto-spawn would parallelize).

Phase 2B items are noted here so they don't get lost; none has a hard requirement attached.

---

## Where to look

- The Claude entry prompt: [tools/daily_flow/orchestrator-prompt.md](../../tools/daily_flow/orchestrator-prompt.md).
- The mechanical pipeline: [tools/daily_flow/orchestrator.py](../../tools/daily_flow/orchestrator.py) and `steps/*`.
- The decision-id registry: [tools/daily_flow/decisions.json](../../tools/daily_flow/decisions.json).
- Project standing orders for context + git safety: [context/procedures.md](../procedures.md) sections "Git safety" and "Auto-merge".
- Parked evaluator (depended-on): [tools/parked_evaluator.py](../../tools/parked_evaluator.py).
- Smoke verify gate (depended-on): [tools/smoke-verify/run.ps1](../../tools/smoke-verify/run.ps1) and [context/designs/engine/smoke-verify-gate.md](engine/smoke-verify-gate.md).

---

SENTINEL: daily-flow-orchestrator design doc v0.5 complete.
