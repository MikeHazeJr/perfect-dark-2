# Sprint Report - c127 CLI panel refinements

> Worktree: `adoring-turing-a53052` (claude/adoring-turing-a53052).
> Pillar: Tooling. Author: c127 worktree session, 2026-05-12.
> Per the c125 sprint-report contract.

## Goal

Refine the Dev Window v2 Claude CLI panel based on Mike's first-use feedback:
(1) the LAUNCH button was hidden by the always-docked Run Tests / Run Game
bottom bar; (2) the panel had too many text inputs - one prompt textbox would
be sufficient; (3) clicking an action button could overwrite manually typed
content without confirmation; (4) the layout required scrolling at the
default window size.

## Shipped

- **Run Tests / Run Game relocated** off the always-docked bottom bar
  (which lived outside the TabControl and shadowed every tab's bottom
  content) into the BUILD tab as a secondary hero pair right below
  BUILD / RELEASE. CLI / LOG / DOCS tabs no longer have that bar at all,
  so the CLI tab's LAUNCH button is uncovered.
- **CLI tab consolidated to one canonical prompt textbox.** Removed
  `TxtCliBugId`, `TxtCliBranch`, `TxtCliPreview` (3 of the prior 5 CLI
  text inputs). Action buttons now rewrite `TxtCliPrompt` in place with
  a template = prefix + empty body slot + cards block + standing rules.
  The caret lands at the body slot for immediate typing. LAUNCH sends
  `TxtCliPrompt.Text` verbatim.
- **`Show-CliInputDialog`** new small WPF modal (parented to `$window`,
  ResizeMode=NoResize, centered on owner) collects Bug Fix's B-NNN and
  Review's optional branch. Cancel aborts the action click. Last values
  remembered per session in `$script:CliBugIdMemory` /
  `$script:CliBranchMemory`.
- **Dirty-state + overwrite-confirmation modal.** `CliLastAppliedTemplate`
  vs `TxtCliPrompt.Text` divergence drives `CliPromptDirty`. A small
  `(custom)` badge next to the PROMPT label surfaces the flag.
  `Confirm-CliOverwriteIfDirty` shows a `MessageBox.Show` "Overwrite
  custom prompt?" OK/Cancel modal when a new action click would replace
  dirty content. Cancel preserves the text and the active-action label.
- **No-scroll layout.** Removed the outer `ScrollViewer` wrapper.
  DockPanel with `LastChildFill="True"`; header + action row docked Top,
  launch row docked Bottom, prompt+cards Grid fills remaining vertical
  space. LAUNCH stays visible at `MinHeight="940"` without scrolling.

Commit SHA: (filled in by the auto-merge step at session end - see
Verification Notes).

## Decisions

| Decision | Rationale |
|----------|-----------|
| Action click overwrites prompt rather than wrapping existing text | Mike's wording "overwrite custom prompt?" implies destructive replacement. The body-slot model: action click drops in a template with an empty body slot, the user types into it. Dirty flag tracks divergence from that template. |
| Custom WPF dialog (Show-CliInputDialog) over Microsoft.VisualBasic.InputBox | The dev-window already uses WPF + a light theme. A native-feeling WPF dialog parented to `$window` modal-blocks correctly and matches the rest of the UI. ~75 lines of XAML + glue. |
| No auto-apply of Goal template on tab open | The c125 init hook called `Set-CliAction "Goal"` which, under the new model, would inject template text the user did not request. Init now sets the active-action label only; the textbox stays empty until the user types or clicks. |
| Run Tests / Run Game land in the BUILD tab, not a dedicated Actions tab | BUILD tab is already the build-cycle tab. Add to it rather than spawn a new tab for two buttons. Sized as a secondary hero pair below BUILD / RELEASE so the semantic grouping reads as "produce + consume artifacts" in one tab. |
| `TxtCliCardSearch` stays | It is a search FILTER for the card list, not a prompt input. Keeping it does not violate "one prompt window." |

## Blockers

None this slice.

## Follow-ups

- Saved prompt presets (still on the c125 future-extensions list).
- Sprint-report history pane inside the CLI tab.
- Batch operations (queue multiple sprints).
- Skill awareness (grey out action buttons for skills not installed).
- Drag-resizable splitter between the prompt and cards columns - currently
  the 60/40 ratio is fixed.

## Kanban Changes

- **c127** (Tooling, active) created with `pending_completion` populated.
  `marked_by="claude-code-cli-session-adoring-turing-a53052"`,
  `summary="Run Tests / Run Game moved into BUILD tab; CLI tab consolidated
  to single prompt with overwrite-confirmation modal and small WPF input
  dialog for Bug Fix / Review; LAUNCH visible at default window size
  without scrolling."`, `evidence_refs` -> design doc, dev-window-v2.ps1,
  this sprint report.

No other cards moved.

## Files Touched

| Path | Change |
|------|--------|
| `devtools/dev-window-v2/dev-window-v2.ps1` | XAML: removed docked Run Tests/Run Game bar; added Run Tests + Run Game row inside BUILD tab; rebuilt CLI tab body (single prompt, no preview pane, no Bug ID/Branch row, bottom-docked launch row); dropped 3 names from `$namedElements`, added `LblCliPromptDirty`. Section 14a: `Get-CliWrappingPrefix`/`Get-CliWrappingSuffix` take bug-id and branch as params; new `Show-CliInputDialog`, `Build-CliActionTemplate`, `Apply-CliActionTemplate`, `Update-CliPromptDirtyState`, `Confirm-CliOverwriteIfDirty`; rewritten `Set-CliAction` with dialog + dirty flow; simplified `Build-CliComposedPrompt`. Section 21 init: no auto-apply at cold start. Section 17 event wiring: dropped preview-update calls, added dirty-tracking handler. |
| `context/designs/devwindow-claude-cli-panel.md` | Header block mentions c127. UI map rewritten for simplified layout. New section on dirty tracking + overwrite confirmation. Action-button table updated. New section explaining the dropped preview pane. |
| `context/tasks.md` | New lane 2j entry. |
| `context/session-log.md` | New session block prepended (above the c125 block). |
| `tools/kanban/state.json` | c127 card with pending_completion. |
| `.claude/sprint-reports/sprint-c127-cli-refinements.md` | This file (NEW). |
| `.claude/scratch/probe-cli-panel-xaml.ps1` | Expected names list updated; assertions added for removed names and Run Tests/Run Game presence. |
| `.claude/scratch/probe-cli-panel-compose.ps1` | Rewritten for the new template-rewrites-in-place model; 21 assertions covering every action, dirty/clean transitions, cards block, em-dash, archive language. |

## Verification Notes (for orchestrator)

When the orchestrator picks this report up on its next turn it should:

1. **Spot-check Section 14a still exists** in `devtools/dev-window-v2/dev-window-v2.ps1`
   by grepping for `Section 14a: Claude CLI panel`. Should match exactly once.
2. **Confirm the c127 removals**: grep for `TxtCliBugId`, `TxtCliBranch`,
   `TxtCliPreview`. All three should return 0 matches in the file.
3. **Confirm `Show-CliInputDialog` exists** by grepping for `function Show-CliInputDialog`.
4. **Run the XAML probe**:
   `powershell -NoProfile -File .claude/scratch/probe-cli-panel-xaml.ps1`. Must
   print `All CLI-tab named elements present.` and `PASS`. The probe asserts
   the 18-name set AND that the 3 removed names are absent AND that Run Tests
   / Run Game still exist.
5. **Run the compose probe**:
   `powershell -NoProfile -File .claude/scratch/probe-cli-panel-compose.ps1`. Must
   print `All 21 assertions passed.` and `PASS`.
6. **Run the launch probe**:
   `powershell -NoProfile -File .claude/scratch/probe-cli-panel-launch.ps1`.
   Must print `PASS` (window survived 8 s without crashing).
7. **Kanban check**: `python tools/kanban_evaluator.py list-pending-completions`
   should include c127 in its output.
8. **No em-dashes**: `grep -c "\xe2\x80\x94"` on each new/modified file
   returns 0. Pre-existing em-dashes in `dev-window-v2.ps1` (lines 217, 4194,
   4324, 4626) predate c125; this slice did not add or remove any.
9. **Auto-merge check**: when this worktree merges to `dev`, the line counts
   of every modified file post-merge must match the worktree pre-merge counts.

Once items 1-8 pass, the orchestrator may archive this report by MOVING it to
`.claude/sprint-reports/archive/sprint-c127-cli-refinements.md`. Reports are
NEVER deleted - they have long-term reference value (Mike's mid-flight
directive from the c125 ship).

SENTINEL-c127-sprint-report-end
