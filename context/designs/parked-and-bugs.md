# Parked Threads and Bug Tracker

> Status: SHIPPED 2026-05-11 (worktree `quirky-greider-71722d`). Data layer + UI surfaces + callable evaluators
> landed as one unit. Daily-flow orchestrator (next session) consumes the CLIs documented in Section 7.

---

## 1. Architecture overview

Three task-management surfaces share one tool tree under `tools/`:

- **Active Kanban** -- the existing browser-based kanban (drag-and-drop cards, columns, subtasks, flags).
  Lives at `tools/kanban/`. State at `tools/kanban/state.json`.
- **Parked Threads** -- work that is paused with a defined resume condition (date, dependency, or manual).
  State at `tools/kanban/parked.json`. Browser surface = a tab inside the kanban UI.
- **Bug Tracker** -- one-off bugs with severity, status, repro, root-cause file:line links, optional linked
  smoke-verify test. State at `tools/bugs/state.json`. Browser surface = a tab inside the kanban UI.

The HTTP server at `tools/kanban/server.py` is the single dev-window backend. It serves the index page,
all three state files, and the action endpoints (`/api/park`, `/api/unpark`, `/api/parked/complete`,
`/api/parked/delete`, `/api/bugs/add`, `/api/bugs/update`, `/api/evaluate`, `/api/cascade`).

A headless Python module `tools/parked_evaluator.py` is both a library and a CLI:
- imported by `server.py` to drive park / unpark / cascade / evaluate from HTTP handlers,
- invoked from the shell (or from the future daily-flow orchestrator) to run the same operations headlessly.

---

## 2. Schema reference

Both `parked.json` and `bugs/state.json` follow the same extensibility rule borrowed from `.pdmod`:

- `schema_version` (int, frozen field semantics).
- `semantic_version` (string, additive-only versioning of the surrounding format).
- `x_extensibility_rule` (string, documenting the rule inline).
- Loaders use `.get()` with defaults so unknown fields do not raise.
- Reserved namespace `x_*` for additive fields that have not yet been promoted to canonical names.
- Bump `schema_version` only on a breaking change. Bump `semantic_version` on additive change.

### 2.1 parked.json

```jsonc
{
  "schema_version": 1,
  "semantic_version": "0.1.0",
  "x_extensibility_rule": "...",
  "parked": [
    {
      "id":                    "pt-001",
      "title":                 "Forge F0-F8 editor (extended scope)",
      "pillar":                "modding",
      "parked_date":           "2026-04-17",
      "parked_from_session":   "implementation-roadmap-apr16",
      "last_state_snapshot":   "free-form text dump of card state at park time",
      "resume_condition": {
        "type":       "dependency | date | manual",
        "targets":    ["card-id-1", "card-id-2"] | "YYYY-MM-DD" | null,
        "semantics":  "all-of | any-of"
      },
      "context_refs":          ["context/designs/..."],
      "kanban_card_origin":    "c042 | null",
      "last_checked":          "2026-05-11",
      "tags":                  ["editor", "forge"],
      "ready_to_resume":       false,
      "stale":                 false,
      "x_archived_card_payload": { ... full kanban card snapshot ... }
    }
  ],
  "archived": [
    { "id": "pt-008", "title": "...", "archived_date": "2026-05-11", "archived_reason": "unparked|completed", "original": { ... } }
  ]
}
```

Field semantics (frozen):

- `id` must start with `pt-` followed by a zero-padded integer. The evaluator picks the next id by
  scanning current + archived entries and incrementing the max.
- `parked_date` is the ISO date the entry was first parked. Days-idle is computed against this.
- `last_checked` is updated on every evaluator pass; staleness fires when `last_checked + 14d < today`.
- `resume_condition.type`:
  - `date` -- `targets` is an ISO date string. Ready when `today >= target`.
  - `dependency` -- `targets` is a list of kanban card ids. Ready when every target's card is in
    column `done` (or any one, if `semantics` is `any-of`).
  - `manual` -- never auto-fires. Mike's playtest gate or design-decision gate parks under this type.
- `ready_to_resume` and `stale` are derived flags maintained by the evaluator. UI does not write them
  directly.
- `x_archived_card_payload` is the full original kanban card structure, captured at park time so
  `unpark` can faithfully restore the row (id, title, subtasks, notes, priority, flag, created/updated
  timestamps).

### 2.2 bugs/state.json

```jsonc
{
  "schema_version": 1,
  "semantic_version": "0.1.0",
  "x_extensibility_rule": "...",
  "bugs": [
    {
      "id":                       "B-323",
      "title":                    "Post-boot AV in challengesInit ...",
      "severity":                 "blocker | major | minor",
      "status":                   "open | triaged | fix-pending-verification | fixed",
      "repro":                    "free-form repro steps",
      "root_cause_file_lines":    ["src/game/challenge.c:374-469"],
      "linked_test":              "tools/smoke-verify/tests/bugs/B-323.json | null",
      "fix_commit":               "competent-saha-a202bb | sha | null",
      "fixed_date":               "2026-05-03 | null",
      "related_bugs":             ["B-318", "B-324"],
      "filed_date":               "2026-05-03",
      "x_notes":                  "free-form addendum"
    }
  ],
  "archived": []
}
```

Status flow: `open -> triaged -> fix-pending-verification -> fixed`. The daily-flow orchestrator can
auto-flip `fix-pending-verification -> fixed` when the linked smoke-verify test passes; manual override
is allowed at every step.

---

## 3. Action flow diagrams

### 3.1 Park flow

```
[kanban card]
   |  right-click
   v
[context menu] -> Park this thread...
   |  click
   v
[park modal: resume type + (date | dependency targets) + note]
   |  Park button
   v
POST /api/park { card_id, resume_condition, note }
   |
   v
server.py: pe.park_card(parked, kanban, card_id, rc, note)
   - snapshot title/pillar/column/priority/notes/subtasks into last_state_snapshot text
   - store full card object in x_archived_card_payload (so unpark restores faithfully)
   - assign next pt-NNN id
   - remove card from kanban.cards
   - append entry to parked.parked
   |
   v
write parked.json + state.json atomically
   |
   v
UI reloads both, redraws kanban (card gone) + parked panel (new row, animation)
```

### 3.2 Unpark flow

```
[parked row]
   |  Unpark button
   v
POST /api/unpark { parked_id }
   |
   v
server.py: pe.unpark_entry(parked, kanban, parked_id)
   - prefer x_archived_card_payload if present (faithful restore)
   - else synthesize a minimal card from title + last_state_snapshot
   - bump updated timestamp; column = active
   - guard against id collision (suffix with -rNNNN if needed)
   - append to kanban.cards
   - move parked entry to parked.archived (reason: 'unparked')
   |
   v
write both files atomically -> reload UI
```

### 3.3 Mark complete (archive without restore)

```
[parked row] -> Mark complete -> POST /api/parked/complete { parked_id, reason }
   -> move from parked.parked to parked.archived (reason: 'completed' default)
```

### 3.4 File bug

```
[Bug Tracker tab] -> File bug button -> [bug add modal]
   POST /api/bugs/add { bug: { id, title, severity, status, repro, root_cause_file_lines, linked_test, related_bugs } }
   server fills filed_date if absent and rejects duplicate ids
```

### 3.5 Link test

```
[bug row] -> Link test button -> prompt(path) -> POST /api/bugs/update { bug_id, patch: { linked_test } }
```

---

## 4. Dev-window UI map

```
+----------------------------------------------------------+
| PD2 Dev Window                                  Settings |
+----------------------------------------------------------+
| [Active Kanban] [Parked Threads (N / M ready)] [Bug Tracker (O/T)] |
+----------------------------------------------------------+
| (Ready banner appears here when M ready > 0)             |
+----------------------------------------------------------+
| panel: kanban  | panel: parked  | panel: bugs            |
| (pillar filter | (pillar / rc-  | (status / severity     |
|  + columns)    |  type / stale  |  filter + File bug)    |
|                |  filter + Re-  |                        |
|                |  evaluate btn) |                        |
|                |                |                        |
| right-click    | Ready section  | severity-sorted        |
| on a card =>   | pinned top     | (blockers first)       |
| context menu   | (green outline | per-row status flip    |
| with Park, Open|  + pulse anim) | actions                |
| card, Cycle    | Parked section | detail = repro,        |
| flag           | Stale section  | root-cause links,      |
|                |                | linked test, related   |
+----------------+----------------+------------------------+
```

Surface notes:

- **Active Kanban**: unchanged except a `contextmenu` handler on each card surfaces Park / Open / Flag.
  Per the design call, no per-card Park button (avoids clutter).
- **Parked Threads**: three sections (Ready / Parked / Stale). Header counter `N parked - M ready -
  K stale` with `M` bolded green. Filters: pillar, resume type, stale-only toggle. Re-evaluate button
  runs the evaluator manually. Click a row to expand the detail panel (snapshot + context refs +
  tags).
- **Bug Tracker**: severity sort (blockers first), default filter hides `fixed`. Per-bug row shows id,
  title, severity badge, status badge, test-linked indicator, related-bug count. Detail expands to
  show repro, root-cause file:line links, linked test, fix commit, related bugs, x_notes.
- **Ready banner**: shows at the top of the dev window whenever the loaded parked.json contains any
  entries with `ready_to_resume: true`. Single Review button switches to the parked tab. Dismiss
  hides it for the session.

---

## 5. Right-click context menu (the no-clutter design call)

A single floating `#ctx-menu` element is positioned at the cursor on `contextmenu` events from cards.
Hidden on click-anywhere, scroll, or after an action fires. Items:

1. **Park this thread...** -- opens the park modal, primed with date = today+30 (per the default
   resume-condition design call).
2. **Open card** -- same as left-click on the card.
3. **Cycle flag** -- equivalent to clicking the flag badge.

The menu is intentionally minimal. Add new items here for any new card-level action, not as per-card
buttons.

---

## 6. Integration with the smoke-verify gate

The Bug Tracker schema reserves `linked_test` as a path string that the daily-flow orchestrator will
hand off to the smoke-verify gate. Convention:

```
tools/smoke-verify/tests/bugs/B-NNN.json
```

When the daily-flow orchestrator runs after a build:

1. For every bug with `status: fix-pending-verification` AND a non-null `linked_test`, invoke the
   smoke-verify test referenced.
2. If the test passes, flip `status` to `fixed`, set `fixed_date` to today, and write `fix_commit` to
   the HEAD SHA (or worktree name for not-yet-merged work).
3. If the test fails, leave status untouched and surface the failure to Mike via the daily-flow log
   summary.

The smoke-verify tests do not exist yet; the convention is reserved. Authoring guidance for the
next session: a smoke-verify test is a small JSON file describing a launch scenario (clean install,
fresh BYOR, specific stage load, etc.) plus a list of post-conditions to assert on `pd-client.log`.

---

## 7. CLI hooks for the daily-flow orchestrator

`tools/parked_evaluator.py` is callable both as a library and as a CLI. Expected inputs and outputs
for each subcommand:

### 7.1 update-ready

```
$ python tools/parked_evaluator.py update-ready
```

- Reads `tools/kanban/parked.json` and `tools/kanban/state.json`.
- For every entry, evaluates `resume_condition` against the kanban state and today's date.
- Sets `ready_to_resume` and `last_checked` on every entry.
- Writes parked.json atomically.
- Prints summary: `N parked entries scanned, M newly ready` plus one line per newly-ready entry.

### 7.2 update-stale

```
$ python tools/parked_evaluator.py update-stale
```

- Reads parked.json.
- Flips `stale` to true for entries whose `last_checked + 14d < today` (or `parked_date + 14d` if
  `last_checked` is absent).
- Writes parked.json atomically.
- Prints summary plus one line per newly-stale entry.

### 7.3 cascade

```
$ python tools/parked_evaluator.py cascade --card-done c036
```

- Reads parked.json + state.json.
- Finds every parked entry whose resume_condition.type is `dependency` AND targets contains `c036`.
- Re-evaluates each (now with c036 considered done as far as the dependency check is concerned, since
  the kanban already shows it done).
- Flips `ready_to_resume` for any whose condition is now satisfied.
- Writes parked.json atomically.
- Prints summary: matched count + flipped count + one line per newly-ready entry.

The daily-flow orchestrator should call `cascade --card-done <id>` whenever a kanban card is moved to
`done`. Polling via `update-ready` once per day is also valid but the cascade form is the targeted
trigger.

### 7.4 list

```
$ python tools/parked_evaluator.py list
```

- Prints human-readable parked + bug summary. Useful for handoff briefings and audit logs.

### 7.5 bug-status

```
$ python tools/parked_evaluator.py bug-status --bug B-323 --to fixed --fix-commit competent-saha-a202bb
```

- Flips a bug's status. Auto-fills `fixed_date` to today when flipping to `fixed`. Auto-records
  `fix_commit` when supplied.

---

## 8. Migration notes (2026-05-11)

Initial parked.json (7 entries) and bugs/state.json (9 entries) populated from project memory and
context/ at ship time:

- **pt-001** Forge F0-F8 editor: date:+30 (2026-06-10). Exploratory followup; no specific trigger.
- **pt-002** Skedar surface-normal locomotion (Slices 4-5): manual. Slices 1-3 shipped; Mike's playtest
  gate is the trigger.
- **pt-003** Char init experiments: manual. Findings absorbed by pt-004; resume only if char init
  diverges again.
- **pt-004** Player init architectural fixes: manual. Design draft pending Mike's greenlight.
- **pt-005** GPU swarm benchmark debug tab (Phase 2): manual. Four design calls (a-d) gate Phase 2.
- **pt-006** AllInOne content removal: date:+30 (2026-06-10). Structurally unblocked but not prioritized.
- **pt-007** Connectivity Phase 1 / P2P friend-play: dependency on `c036` (Input Cohorts 5-8). Cascade
  trigger fires when c036 moves to done.

Bugs B-318 through B-326 migrated as `fix-pending-verification` with their worktree-name fix_commits
(e.g. `gifted-bohr-62309b`). When Mike's playtest closes them, the daily-flow orchestrator will flip
each to `fixed` and write the dev SHA into `fix_commit`.

---

## 9. Where to look

- Server: `tools/kanban/server.py`.
- CLI / library: `tools/parked_evaluator.py`.
- UI: `tools/kanban/index.html` (kanban + parked + bugs tabs).
- State files: `tools/kanban/state.json`, `tools/kanban/parked.json`, `tools/bugs/state.json`.
- This doc: `context/designs/parked-and-bugs.md`.
- Bug ledger (markdown narrative): `context/bugs.md` (still authoritative for narrative; the JSON
  ledger at `tools/bugs/state.json` is the machine-readable surface that the dev window consumes).
