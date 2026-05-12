# Sprint Report (demo) - c125 Claude CLI panel

> Canonical sprint-report schema for the Dispatch orchestrator to consume.
> This file is the c125 acceptance artifact and demonstrates the format every
> CLI-launched session must produce at session end.
>
> Worktree: `adoring-turing-a53052` (claude/adoring-turing-a53052)
> Pillar: Tooling
> Author: c125 worktree session, 2026-05-12

## Goal

Add a Claude CLI button container in the Dev Window v2 with a prompt textbox,
action buttons (Goal / Plan / Investigate / Bug Fix / Review / Custom), a card
selector pulled live from the kanban, a composed-prompt preview, and a Launch
button that runs Claude Code CLI either interactively in a new console
(prompt copied to clipboard) or headlessly via `-p` with output streamed to the
Log tab. The launched session must write a sprint report to
`.claude/sprint-reports/sprint-YYYY-MM-DDTHHMMSS.md` so the orchestrator can
catch up after the sprint and ARCHIVE the report (move into
`.claude/sprint-reports/archive/`) once verified. Reports are NEVER deleted -
they have long-term reference value; the active directory is the orchestrator's
inbox, the archive is the permanent record.

## Shipped

| File | Change |
|------|--------|
| `devtools/dev-window-v2/dev-window-v2.ps1` | New `CLI` TabItem in XAML (~180 lines, action row + prompt/cards split + composed-prompt preview + mode radios + Launch/Copy/Reset), 20 new named elements registered in `$namedElements`, new Section 14a (~280 lines) of helper functions (state cache, card filtering, action wrapping, preview rendering, interactive + headless launchers), Section 17 event wiring (~25 lines), Section 21 init hook (Refresh-CliCardsList + Set-CliAction "Goal" on Window.Loaded) |
| `context/designs/devwindow-claude-cli-panel.md` | NEW design doc (326 lines, SENTINEL-terminated): UI map, action wrapping rules, composed prompt structure, kanban card source, launch mechanism choice + rationale, sprint report consumption contract, future extensions, verification procedure |
| `.claude/sprint-reports/sprint-c125-demo.md` | This file - canonical sprint-report demo |
| `tools/kanban/state.json` | c125 card with `pending_completion` block; orchestrator-flow update |
| `context/tasks.md` | Lane entry: Tooling - c125 Claude CLI panel SHIPPED |
| `context/session-log.md` | Session entry for `adoring-turing-a53052` |

Commit SHA: (filled in by the auto-merge step at session end - see Verification Notes)

## Decisions

| Decision | Rationale |
|----------|-----------|
| New dedicated `CLI` tab instead of an inline panel in BUILD | BUILD tab is dominated by build/release controls; folding CLI in would push hero buttons below the fold. Tabs are the established v2 navigation affordance. |
| Default mode = Interactive (clipboard handoff to a new cmd console) | Headless `-p` cannot show live progress and cannot accept mid-run intervention. Interactive matches the bulk of Mike's use cases (planning, investigation, review). The clipboard handoff path losslessly handles newlines, double quotes, backslashes - positional-arg quoting on cmd.exe drops or misinterprets these. |
| Headless mode reachable via radio button (opt-in) | Goal-style autonomous runs benefit from `-p` and stdout streaming to the Log tab. Keeping it opt-in rather than default protects against accidental autonomous runs when Mike intended to converse. |
| Pre-allocated card range hardcoded to `c126-c130` | Matches the orchestrator's spawn-brief format. The panel does not allocate IDs itself - it just embeds the reservation in the standing-rules suffix so the spawned session has a soft-pool to draw from. Future extension: read the reservation from a config field. |
| Kanban data source: HTTP `/api/state` with file fallback | When the kanban server is running we pick up uncommitted edits (Mike's in-flight drags). When the server is down we fall back to `tools/kanban/state.json` so the panel still works. Auto-start of the server is *not* triggered by the CLI tab opening - this stays decoupled to avoid surprising the user. Mike can use the existing `Open Kanban` button to bring the server up. |
| Card list filtered to `active` + `backlog` only | `done` and `blocked` cards are not productive sprint targets. Filter reduces noise; sort puts active first. |

## Blockers

None this slice. Claude CLI was already installed at `%APPDATA%\npm\claude.cmd`
on Mike's machine (v2.1.139); no install or PATH work was needed.

## Follow-ups

These were surfaced but intentionally out of scope for c125:

- **Orchestrator consumption logic** (memory file
  `feedback_dispatch_orchestrator_workflow.md`). The contract is documented in
  the design doc and standing-rules suffix; the orchestrator's session-start
  routine that scans `.claude/sprint-reports/`, verifies, and ARCHIVES the
  consumed report (moves into `.claude/sprint-reports/archive/`, never deletes)
  is a separate change.
- **Saved prompt presets**. A dropdown of last-N composed prompts persisted in
  `devtools/dev-window-v2/settings.json`. Useful for repeat workflows.
- **Sprint-report history pane**. A read-only view of files in
  `.claude/sprint-reports/` so Mike can see what the orchestrator has yet to
  consume.
- **Batch operations**. Queue multiple sprints (one card per launch) and run
  them sequentially in headless mode.
- **Skill awareness**. Detect installed Claude Code skills from `~/.claude/skills/`
  and grey out action buttons whose wrapping references a missing skill.

## Kanban Changes

- **c125** (Tooling): created in `active` column, then moved to `pending_completion`
  with `marked_by="claude-code-cli-session-adoring-turing-a53052"`,
  `summary="Claude CLI panel landed in Dev Window v2 with composed-prompt preview,
  card-aware context, interactive + headless launch modes, and sprint-report
  contract for orchestrator consumption."`, `evidence_refs` pointing at the
  design doc, the demo report, and the dev-window-v2.ps1 change.

No other cards moved.

## Files Touched

| Path | Lines added | Lines removed | Purpose |
|------|-------------|---------------|---------|
| `devtools/dev-window-v2/dev-window-v2.ps1` | ~510 | ~3 | XAML CLI tab + Section 14a helpers + event wiring + init hook |
| `context/designs/devwindow-claude-cli-panel.md` | 326 | 0 | NEW design doc |
| `.claude/sprint-reports/sprint-c125-demo.md` | this file | 0 | NEW demo report |
| `tools/kanban/state.json` | ~15 | 0 | NEW c125 card with pending_completion |
| `context/tasks.md` | ~6 | 0 | New lane entry |
| `context/session-log.md` | ~50 | 0 | New session block |
| `.claude/scratch/probe-cli-panel-xaml.ps1` | 50 | 0 | NEW XAML probe (gitignored) |
| `.claude/scratch/probe-cli-panel-compose.ps1` | 175 | 0 | NEW compose probe (gitignored) |
| `.claude/scratch/probe-cli-panel-launch.ps1` | 50 | 0 | NEW launch probe (gitignored) |

## Verification Notes (for orchestrator)

When the orchestrator picks this report up on its next turn it should:

1. **Spot-check the design doc** at
   `context/designs/devwindow-claude-cli-panel.md` exists and ends with
   `SENTINEL-c125-design-doc-end` (truncation guard per SP-9).
2. **Spot-check Section 14a** exists in `devtools/dev-window-v2/dev-window-v2.ps1`
   by grepping for `Section 14a: Claude CLI panel`. Should match exactly once.
3. **Run the XAML probe**:
   `powershell -NoProfile -File .claude/scratch/probe-cli-panel-xaml.ps1`. Must
   print `All CLI-tab named elements present.` and `PASS`.
4. **Run the compose probe**:
   `powershell -NoProfile -File .claude/scratch/probe-cli-panel-compose.ps1`. Must
   print `All 16 assertions passed.` and `PASS`.
5. **Run the launch probe**:
   `powershell -NoProfile -File .claude/scratch/probe-cli-panel-launch.ps1`.
   Must print `PASS` (window survived 8s without crashing). Probe closes the
   window cleanly.
6. **Kanban check**: `python tools/kanban_evaluator.py list-pending-completions`
   should include c125 in its output. If the orchestrator confirms via the
   kanban UI, the card moves to `done` and pending_completion clears.
7. **No em-dashes**: `grep -c "\xe2\x80\x94"` on each new file returns 0.
   Existing em-dashes in `dev-window-v2.ps1` (4 total: lines 217, 4194, 4324,
   4626) predate this change and are in untouched comments.
8. **Auto-merge check**: when this worktree merges to `dev`, the line counts of
   modified files post-merge must match pre-merge worktree counts. Specifically
   `dev-window-v2.ps1` is expected at ~4690 lines (was 4108 on dev).

Once items 1-7 pass, the orchestrator may archive this report by MOVING it to
`.claude/sprint-reports/archive/sprint-c125-demo.md`. The report is NEVER
deleted - it has long-term reference value. The contract is satisfied; the
catch-up payload has been consumed and the report becomes part of the permanent
sprint record.

If item 8 fails (any file shrank unexpectedly post-merge), halt the merge and
surface to Mike. Per `context/procedures.md` truncation discipline.

SENTINEL-c125-sprint-report-end
