# Dev Window v2: Claude CLI Panel

> Cards: c125 (original ship), c127 (UX refinement). Pillar: Tooling.
> Shipped: 2026-05-12.
>
> Closes the orchestrator-blindness gap. The Dispatch orchestrator cannot see Claude
> Code CLI sessions (they live in a separate namespace). When Mike runs a CLI session
> from the new panel, the launched session writes a sprint report that the
> orchestrator consumes on its next turn. The panel composes well-formed prompts that
> mandate the report so the contract is always satisfied.
>
> **c127 refinements (2026-05-12)**: Run Tests / Run Game moved off the always-docked
> bottom bar (they obscured LAUNCH) and into the BUILD tab. The CLI tab is reduced
> to a single canonical prompt textbox; Bug ID / Branch text fields and the separate
> composed-prompt preview pane are gone. Bug Fix / Review collect their inputs
> through a small WPF input dialog. Default window size now fits everything without
> scrolling. **Post-ship correction (2026-05-12 same day)**: the overwrite-
> confirmation modal was replaced by a body-preserving wrap-swap model -- the user's
> substance is extracted from the textbox at action-click time and re-wrapped with
> the new template, so switching action buttons never destroys typed content. The
> active action button is highlighted in PD cyan with a thick border so the user
> sees at a glance which wrap is applied.

---

## Goal

A CLI launcher built into Dev Window v2 that lets Mike compose well-formed prompts
for Claude Code CLI sessions, scoped to one or more kanban cards, with mandatory
sprint-report output for the orchestrator to consume on its next turn.

Two consumers care about this:

1. **Mike**: faster than typing the full standing-rules preamble + card context
   every time. One click stages a high-quality prompt.
2. **Dispatch orchestrator**: gets a known file surface (`.claude/sprint-reports/`)
   to scan and consume after each CLI session, so it can catch back up after
   sprints that happened outside its session namespace.

---

## Where it lives

A new `CLI` tab appears in the Dev Window v2 `TabControl`, placed after the existing
`BUILD`, `LOG`, `DOCS` tabs. The tab is laid out as a single full-pane content
region (no nested ScrollViewer, no DockPanel stacking - the panel content is
self-sufficient for typical screen sizes).

The choice of a dedicated tab over an in-line panel in `BUILD`:

- Build/Release controls dominate the BUILD tab today. Adding a CLI panel there
  would push hero buttons below the fold and dilute the BUILD tab's identity.
- A tab is the existing affordance pattern in v2 (`LOG`, `DOCS`).
- Cli sessions are a peer concern to builds, not a sub-concern of them.

---

## UI map (c127)

```
+- CLI tab ---------------------------------------------------------------+
| Header: CLAUDE CLI | action buttons wrap the prompt in place. LAUNCH... |
|                                                                          |
| ACTION  [Goal] [Plan] [Investigate] [Bug Fix] [Review] [Custom] | active|
|                                                                          |
| +-- PROMPT (60%) -----------+ +-- CARDS (40%) -------------------------+|
| | label PROMPT  (custom)    | | CARDS  search:[..........] [Refresh]   ||
| |                            | | +- ListBox (multi-select) ----------+ ||
| | TextBox.AcceptsReturn      | | | c027  Catalog universality pivot   | ||
| | (single canonical prompt   | | | c029  Benchmarking - speed tune ... | ||
| |  - action buttons rewrite  | | | c030  Mod Infrastructure ...        | ||
| |  in place; LAUNCH sends    | | +------------------------------------+ ||
| |  whatever is here verbatim)| | Selected: c027, c051                   ||
| +----------------------------+ +----------------------------------------+|
|                                                                          |
| MODE: (*) Interactive (new console)  ( ) Headless (-p, log)             |
|       [ LAUNCH ]   [ Copy Prompt ]   [ Reset ]                          |
+------------------------------------------------------------------------+
```

Layout uses a DockPanel with `LastChildFill="True"`. The header, action row, and
launch row are docked Top/Top/Bottom; the prompt+cards Grid is the last child
and fills the remaining vertical space. The LAUNCH button is in the
bottom-docked row, so it is always visible regardless of how tall the middle
Grid grows. There is no outer ScrollViewer wrapper anymore -- the tab content
fits at the default window size (`MinHeight="940"`).

Run Tests / Run Game used to live in an always-docked bottom bar OUTSIDE the
TabControl, which made them appear on every tab and shadow the CLI tab's
LAUNCH button. c127 moved those into the BUILD tab as a secondary hero pair
right below BUILD / RELEASE. They are no longer visible from CLI / LOG / DOCS.

Visual conventions follow existing v2 styling: white card backgrounds (`#FFFFFF`),
PD cyan accent (`#0078A8`), light-theme primary text (`#1A2434`), `Segoe UI` body
and `Consolas` for code-style values. Buttons reuse the existing `AccentBtn`,
`ToolBtn`, `GreenBtn` styles. No new style resources are introduced.

### Body-preserving wrap-swap (c127 corrected 2026-05-12)

`TxtCliPrompt` is the single canonical source of what LAUNCH sends. Action
buttons (Goal / Plan / Investigate / Bug Fix / Review / Custom) preserve the
user's substance and only swap the syntactic frame around it.

State variables that drive this:

- `$script:CliActiveAction` -- the currently active action, or `""` if no wrap
  is applied yet (cold-start state and after Reset).
- `$script:CliBodyText` -- the user's substance. Updated when the body is
  extracted at action-click time.
- `$script:CliWrapPrefix` / `$script:CliWrapSuffix` -- the exact prefix and
  suffix strings that were prepended / appended to the body in the most recent
  apply. Used by `Extract-CliBodyFromCurrentText` to recover the body from
  whatever the user has typed inside the wrap.

When the user clicks an action button:

1. **Bug Fix** prompts for `B-NNN` via `Show-CliInputDialog` (small WPF modal
   parented to the dev-window). Cancel -> bail with no panel change. Empty
   bug ID -> bail. The accepted value is remembered in `$script:CliBugIdMemory`
   so re-clicking Bug Fix prefills.
2. **Review** prompts for an optional branch. Cancel -> bail. Empty value is
   allowed (defaults to "selected cards' affected files" scope). Last value
   is remembered in `$script:CliBranchMemory`.
3. **Goal / Plan / Investigate / Custom** skip the input step.

Then:

4. `Extract-CliBodyFromCurrentText` reads the current textbox content. If a
   wrap is currently active and the text both starts with `CliWrapPrefix` AND
   ends with `CliWrapSuffix`, the middle slice is the body. Otherwise (no
   active wrap, or the user nuked the wrap markers wholesale) the whole
   textbox content IS the body.
5. `Apply-CliActionTemplate` builds the new wrap = `prefix + body +
   actionSuffix + cardsBlock + standingRules` and writes it into TxtCliPrompt.
   `CliWrapPrefix` and `CliWrapSuffix` are updated to the new prefix and
   trailing tail respectively so the next extraction reads correctly.
6. The caret is placed at the end of the body slot so the user can keep
   typing inline.
7. `Update-CliActionButtonVisuals` highlights the new active button in PD
   cyan (`#0078A8` background, white foreground, `#005A80` border, thickness
   2) and clears the other five back to the ToolBtn defaults.

Switching action buttons therefore preserves the user's typed substance and
re-wraps it with the new frame. There is no destruction of typed content and
no overwrite-confirmation modal. The previously shipped
"Overwrite custom prompt?" modal flow was replaced in the post-ship
correction.

The Reset button clears the prompt, drops the card selection, clears all the
state variables above (including `CliActiveAction = ""`), and restores all
six action-button visuals to their default state. Cold start does the same.

### Why no separate preview pane

c125 originally had a read-only `TxtCliPreview` showing the composed prompt
beneath the inputs. c127 removes it because the action-button-rewrites-in-place
model makes the preview redundant -- `TxtCliPrompt` *is* the composed prompt.
What you see in the textbox is exactly what LAUNCH sends. This also reclaims
~200 px of vertical space, which is what lets the panel fit at the default
window height without scrolling.

---

## Action buttons (template wrapping)

Each button sets a current "action" state and wraps the user's substance with
that action's template: prefix + body + actionSuffix + cards block (if any) +
standing rules. Switching action buttons preserves the body and only swaps
the prefix / actionSuffix. LAUNCH sends the textbox content verbatim.

| Button       | Template wrap                                                                                                  |
|--------------|---------------------------------------------------------------------------------------------------------------|
| Goal         | `/goal <body>` then cards + standing rules. Use for autonomous run-to-completion.                             |
| Plan         | `Plan the following without executing any code. Output a structured plan:\n\n<body>` then cards + standing.   |
| Investigate  | `Investigate the following. Do not modify code. Report findings only:\n\n<body>` then cards + standing.       |
| Bug Fix      | Dialog asks for B-NNN. Wrap: `Fix bug B-NNN: <body>\n\nWrite or update the regression test ...`               |
| Review       | Dialog asks for optional branch. Wrap: `Review the following. Scope: <branch or selected cards>:\n\n<body>`   |
| Custom       | No prefix. Wrap: `<body>` then cards + standing rules. Use for verbatim prompts with only the standing tail.  |

Slash commands (`/goal`) survive into the launched session because Claude Code CLI
parses leading-slash skills inside an interactive prompt. The Goal button assumes a
`/goal` skill is configured for Mike's installation; if not, the prompt degrades to
a verbatim string (no failure - just no skill invocation).

---

## Composed prompt structure

The preview shows the literal text that will be sent. Structure:

```
<button wrapping prefix>
<user's text from the prompt textbox>

<if cards selected:>
Working cards:
- c027: Catalog universality pivot Steps 4-9 (active, catalog)
- c051: Drop legacy mods/ build-time deploy (backlog, catalog)
...

[Standing rules]
- Apply all PD2 standing rules from feedback auto-memory (commit-message standard
  "<Pillar> - cNNN: <summary>" with body + Refs line, dirty-tree relaxed auto-merge
  to dev, no em-dashes in any generated file, hierarchical log channels, BYOR-compliant,
  queued build tool only, descriptive headings).
- Pre-allocated card IDs for this session: c126-c130. Use these for any NEW sub-cards.
  Do NOT generate from kanban state.
- Update tools/kanban/state.json reflecting all card transitions, pending_completion
  for any completed cards (orchestrator-confirmed flow).
- At session end, write a sprint report to
  .claude/sprint-reports/sprint-YYYY-MM-DDTHHMMSS.md (UTC ISO 8601 in filename,
  hyphen-separated). Sections required:
  - ## Goal       verbatim user prompt
  - ## Shipped    what landed (commit SHAs, files touched, kanban transitions)
  - ## Decisions  calls made and rationale
  - ## Blockers   anything that stopped progress
  - ## Follow-ups work surfaced but not done
  - ## Kanban Changes  exact cNNN transitions (created, updated, columns moved)
  - ## Files Touched   list with brief change description
  - ## Verification Notes  for orchestrator: what to spot-check before disposing.
- The orchestrator (dispatch session) will read the sprint report on next interaction,
  verify against kanban + git state, then ARCHIVE the report (move into
  `.claude/sprint-reports/archive/`) once verified. Reports are NEVER deleted -
  they have long-term reference value. The active directory holds only
  unprocessed reports; the archive is the permanent record.
```

The pre-allocated card range is a soft hint - in practice the session's launching
context (orchestrator or this panel) decides the IDs. The panel itself does not
allocate IDs; it just inserts the static `c126-c130` reservation as documented in
the spawn brief format. The orchestrator that consumes the sprint report can
reconcile against the live state.

---

## Card selection

Card data is pulled from the kanban server at `http://localhost:7531/api/state`. The
panel auto-starts the kanban server (reusing `Test-KanbanServerUp` + the spawn logic
in `Invoke-OpenKanban`) when the CLI tab is first selected, if not already running.
Card list filters to columns `active` and `backlog` and sorts by:

1. Column (`active` before `backlog`)
2. Within column: `priority` ascending (lower = higher priority), then `order`
   ascending

Each row displays `cNNN - <title> (<pillar>)`. Clicking a row toggles selection.
A `Selected: <ids>` summary line under the list shows what is currently picked.
Selection persists across action-button changes; switching tabs and back preserves
selection until `Reset` is pressed.

A `Refresh` button re-fetches state. A `Search` textbox filters the displayed list
by title or pillar substring (case-insensitive). Selection is preserved across
filter changes (a card stays selected even if filter hides it).

---

## Launch mechanism

Two modes, selected via radio buttons:

### Interactive (new console) - default

1. Composed prompt written to `$env:TEMP\pd2-cli-prompt-<UTC>.txt` (UTF-8 with BOM).
2. Composed prompt also copied to Windows clipboard (Set-Clipboard).
3. `Start-Process cmd.exe /K` opens a new console at `$script:ProjectRoot` with
   the command line `claude` (no args). The window stays open after Claude exits.
4. Mike pastes the prompt with Ctrl+V into the live Claude prompt, hits Enter.
5. Mike sees Claude work in real time, can intervene, answer prompts, etc.

This mode is preferred for non-autonomous work (Plan, Investigate, Review) where
Mike wants to participate. Output stays in the new console window.

Reason for the clipboard handoff rather than positional-arg:
- Composed prompts can contain newlines, double quotes, backslashes, etc.
- cmd.exe quoting rules drop or misinterpret these in positional args.
- Clipboard paste is the only path that survives arbitrary content losslessly.

### Headless (-p, output to Log) - opt-in

1. Composed prompt written to the same temp file.
2. `Start-AsyncPoolAction` runs `claude --print --output-format text < <temp>`
   from `$script:ProjectRoot`, using a runspace in `$script:BgPool`.
3. Stdout streams to the Log tab via `Add-LogLine` from an OnComplete callback.
4. The sprint report (mandated by the standing-rules suffix) lands at
   `.claude/sprint-reports/sprint-<UTC>.md`.
5. Mike inspects results in the Log tab. The orchestrator picks up the sprint
   report on its next turn.

This mode is preferred for Goal-style autonomous runs.

---

## Status integration

The CLI tab does not modify the global status bar. The Log tab (and the new console
window) carry CLI session output. The status bar's `StatusMode` reflects only
build/release/test activity.

---

## Files touched

| File                                                           | Change                          |
|----------------------------------------------------------------|---------------------------------|
| `devtools/dev-window-v2/dev-window-v2.ps1`                     | New `CLI` tab + Section 14a CLI panel functions + Section 17 event wiring |
| `context/designs/devwindow-claude-cli-panel.md`                | This design doc (NEW)           |
| `.claude/sprint-reports/sprint-c125-demo.md`                   | Canonical sprint-report template (NEW) |
| `tools/kanban/state.json`                                      | c125 card with pending_completion |
| `context/tasks.md`                                             | Lane entry: CLI panel SHIPPED   |
| `context/session-log.md`                                       | Session entry for c125          |

---

## Future extensions

Tracked as follow-ups but not in scope for c125:

- **Saved prompt presets**: a dropdown of last-N composed prompts persisted in
  `devtools\dev-window-v2\settings.json`. Useful for repeat workflows like
  daily-flow audits.
- **Sprint report history pane**: a sub-pane (or DOCS-tab integration) showing
  the contents of `.claude/sprint-reports/` so Mike can review what the
  orchestrator has yet to consume.
- **Batch operations**: queue multiple sprints (e.g. one card per launch) and run
  them sequentially in headless mode. Output to per-card sub-logs.
- **Card metadata enrichment**: when cards are selected, fetch open_questions and
  pending_completion fields and surface them in the prompt context (so the
  session knows whether the card is blocked or in review).
- **Skill awareness**: detect installed Claude Code skills (e.g. `/goal`, `/plan`)
  from `~/.claude/skills/` and grey out action buttons whose wrapping references
  a missing skill.

---

## Sprint report consumption protocol (orchestrator contract)

This panel produces sprint reports. Consumption logic lives in the Dispatch
orchestrator's workflow, not in this code. The contract:

1. Sprint reports land in `.claude/sprint-reports/sprint-<UTC>.md` with the schema
   documented in the standing-rules suffix above. The active directory holds only
   UNPROCESSED reports; the orchestrator's job is to move each one to the archive
   after consuming it.
2. The orchestrator's session-start routine scans `.claude/sprint-reports/` (top
   level only, not the archive) for any new files since its last seen state.
3. For each new report:
   - Read the full report.
   - Cross-reference Goal vs Shipped against the live kanban (`tools/kanban/state.json`)
     and `git log` since the report's earliest mentioned commit.
   - Update orchestrator memory or context as needed based on report content (e.g.
     record decisions, surface blockers, propagate follow-ups to the daily-flow
     queue).
   - ARCHIVE the report file once verification passes by MOVING it into
     `.claude/sprint-reports/archive/<same-basename>.md`. Reports are NEVER
     deleted - they have long-term reference value. The active directory clears
     so the next session-start scan only sees newly landed reports; the archive
     is the permanent record of every sprint the orchestrator has consumed.
   - If verification fails (kanban does not show the claimed transition, git does
     not show the claimed commit), surface the discrepancy to Mike and LEAVE the
     report in the active directory for forensic review. It will be re-scanned
     on the next orchestrator turn.
4. Summarize "caught up on what happened in your sprint" to Mike at the start of
   the next interaction.

The launcher creates `.claude/sprint-reports/archive/` if it is missing, even
though only the orchestrator writes to it. This keeps the directory shape correct
on a fresh checkout where the archive has never received a report.

The orchestrator behavior is documented in the memory file
`feedback_dispatch_orchestrator_workflow.md` (created/updated in the same change
that ships this panel, or in a follow-up if scope demands).

---

## Verification

End-to-end probe procedure for c125 acceptance:

1. Launch Dev Window v2: `pwsh devtools\dev-window-v2\dev-window-v2.ps1`.
2. Switch to the new `CLI` tab.
3. Confirm the panel renders: action row, prompt textbox, card list, preview area,
   mode radios, Launch button.
4. Confirm card list populates with active + backlog cards from the kanban server
   (the panel auto-starts the server if needed).
5. Click each action button in sequence (Goal, Plan, Investigate, Bug Fix, Review,
   Custom). Confirm preview updates to reflect the wrapping each time.
6. Type "echo hello" in the prompt textbox. Pick one card (e.g. c027). Confirm the
   preview shows the working-cards block with c027.
7. Pick `Bug Fix`, type B-999 in the bug-id field, confirm preview includes the
   bug-fix wrapping with the test-first requirement.
8. Click `Copy Prompt`. Paste into a scratch file. Confirm the literal preview
   contents match the clipboard contents.
9. With Interactive selected: click `Launch`. Confirm a new cmd.exe window opens
   at the project root with `claude` running. Paste with Ctrl+V to confirm the
   prompt was on the clipboard. (Do not actually launch a sprint - cancel out.)
10. With Headless selected: click `Launch` with a no-op-safe prompt like
    "Echo hello and write a tiny sprint report at .claude/sprint-reports/sprint-test.md".
    Wait for completion. Confirm a sprint-report file lands at the documented path.

The c125 demo report at `.claude/sprint-reports/sprint-c125-demo.md` shows the
canonical schema.

---

## Where to look

- Panel XAML: `devtools/dev-window-v2/dev-window-v2.ps1` Section 9 (CLI TabItem).
- Panel functions (compose prompt, refresh cards, launch): `devtools/dev-window-v2/dev-window-v2.ps1` Section 14a.
- Event wiring: `devtools/dev-window-v2/dev-window-v2.ps1` Section 17.
- Demo sprint report: `.claude/sprint-reports/sprint-c125-demo.md`.
- Sprint report consumption contract (orchestrator-side): memory file
  `feedback_dispatch_orchestrator_workflow.md` (follow-up if not present in this ship).

SENTINEL-c125-design-doc-end
