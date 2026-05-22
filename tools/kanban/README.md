# PD2 Kanban

Bidirectional task board. Mike edits via browser. AI reads and writes `state.json` directly.

## Run

```bash
python tools/kanban/server.py
```

Then open: `http://localhost:7531/`

## Browser workflow

- **Select status**: use the top status tabs (`Backlogged`, `Active`, `Blocked`, `Done`).
- **Filter by pillar**: use the `Show` dropdown beside the status tabs.
- **Select a card**: click a numbered card title in the left list to show its contents on the right.
- **Reprioritize cards**: drag cards in the left list. The saved `order` field is the manual priority order for that status.
- **Edit card contents**: use the right pane for title, description, pillar, status, priority badge, card notes, and subtasks. `Save`, `Cancel`, and `Delete` are docked at the bottom of the pane.
- **Add card**: click "+ Add card" in the filter bar.
- **AI special notes**: click the docked `AI Notes` button. Notes save to root `x_special_notes` in `state.json` so AI sessions can read them when directed or when noticed.
- **Subtasks**: the right pane shows subtasks in their own scrollable box. `✅` means complete; `❎` means open. Click the indicator to toggle open/completed.
- **Auto-sort by priority**: Settings panel, Sorting toggle. Off means manual order drives priority; on means flags and priority badges group the list before manual order.

## AI workflow

Read state: `tools/kanban/state.json`

Write state: edit `state.json` directly (server must not be mid-write), or POST to `http://localhost:7531/api/state` with the full updated JSON body. The server writes atomically (temp file + rename) so direct edits are safe when the server is idle.

**To dispatch work**: scan `state.json` for cards where `column` is `"active"` or `"backlog"`, sorted by `priority` ascending (1 = highest). Prefer `priority: 1` Critical cards, then `priority: 2` High. Within a priority level, lower `order` comes first. Move a card to `"active"` when starting it; move to `"done"` when complete. Set subtask `status` fields to reflect granular progress.

---

## Schema reference

### Root

```json
{
  "settings": { "autoSortPriority": false },
  "pillars":  [...],
  "columns":  ["backlog", "active", "blocked", "done"],
  "columnLabels": {...},
  "x_special_notes": {"text": "", "updated": null},
  "cards":    [...]
}
```

### Card

```json
{
  "id":          "c027",
  "title":       "Card title",
  "description": "Optional longer description.",
  "pillar":      "catalog",
  "column":      "active",
  "order":       1000,
  "priority":    2,
  "notes":       "Short notes or reference links",
  "flag":        "star",
  "flagged_at":  "2026-05-02T00:00:00Z",
  "subtasks":    [...],
  "created":     "2026-05-02T00:00:00Z",
  "updated":     "2026-05-03T00:00:00Z"
}
```

**`priority`** (optional integer 1-5):

| Value | Label    | Color  | Meaning                                  |
|-------|----------|--------|------------------------------------------|
| 1     | Critical | red    | Blocking or foundational - do first      |
| 2     | High     | orange | Important track - do soon                |
| 3     | Medium   | yellow | Normal queue item                        |
| 4     | Low      | blue   | Nice to have, no deadline pressure       |
| 5     | Someday  | gray   | Deferred - not on the active horizon     |
| unset | -        | none   | Treated as Medium (3) when auto-sorting  |

**`flag`** (optional string): attention marker set by Mike in the browser. Values:

| Value   | Icon | Color  | Meaning                              |
|---------|------|--------|--------------------------------------|
| `null`  | -    | none   | No flag                              |
| `star`  | ★    | amber  | Needs attention / Mike is watching   |
| `alert` | !    | red    | Urgent / blocking something          |
| `watch` | ●    | purple | Monitor / may need action soon       |

**`flagged_at`** (optional ISO string): timestamp when flag was last set. `null` when `flag` is `null`.

The browser cycles `null -> star -> alert -> watch -> null` on badge click. The PATCH endpoint (`PATCH /api/cards/:id`) updates only `flag`, `flagged_at`, and `updated` without a full state rewrite.

**`order`** (integer): relative position within the column. Lower = higher in the numbered list. Manual browser reordering updates this field and is the default prioritization surface. The UI assigns midpoint values on drag-drop so exact integers stay stable.

### AI special notes

```json
{
  "x_special_notes": {
    "text": "Mike's note for future AI sessions.",
    "updated": "2026-05-22T21:00:00Z"
  }
}
```

Root `x_special_notes` is Mike-authored guidance for AI sessions. Read it when Mike directs you to, and also check it when the docked AI Notes button indicates saved notes.

### Subtask

```json
{
  "id":     "s050-01",
  "title":  "Migrate weapons (F1-F13)",
  "status": "done",
  "order":  1000,
  "notes":  "Optional notes"
}
```

**`status`** options: `"backlog"` / `"active"` / `"blocked"` / `"done"`. Subtasks inherit the parent card's pillar. The parent card's column is independent - a parent stays `"active"` until all subtasks are done (the UI shows a progress bar but does not auto-promote the parent column).

### Hierarchy rules

- One level of nesting: card -> subtasks. Subtasks cannot have subtasks.
- A card with no `subtasks` field (or `subtasks: []`) is a plain card.
- A card with subtasks is a parent card. Its progress bar shows `done/total` subtask completion.
- The parent's `column` is manually managed. Convention: move parent to `"done"` only when all subtasks are done.

### AI orchestration signal

When choosing what to work on next, use this algorithm:

1. Collect all cards where `column` is `"active"` or `"backlog"`.
2. Sort by `order` ascending first. This is Mike's manual prioritization order from the left-side numbered list.
3. Use `priority` and `flag` as severity/attention metadata, not as a replacement for manual order unless `settings.autoSortPriority` is true.
4. Prefer cards in `"active"` over `"backlog"` when the manual order is otherwise ambiguous.
5. For parent cards, look at which subtasks are `"backlog"` or `"active"` - those are the actual work units.
6. Report your chosen card + subtask to Mike before starting, so he can override if priorities have shifted.
