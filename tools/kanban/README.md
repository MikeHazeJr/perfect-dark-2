# PD2 Kanban

Bidirectional task board. Mike edits via browser. AI reads and writes `state.json` directly.

## Run

```bash
python tools/kanban/server.py
```

Then open: `http://localhost:7531/`

## Browser workflow

- **Filter by pillar**: click pillar buttons in the top bar
- **Drag cards**: move between columns or reorder within a column
- **Edit card**: click any card to open the edit modal (title, description, pillar, column, priority, notes, subtasks)
- **Add card**: click "+ Add card" at the bottom of any column
- **Expand subtasks**: click the progress bar or "X/Y" count on any parent card
- **Cycle subtask status**: click the colored dot in the inline subtask list
- **Reorder subtasks**: drag the "::" handle within the expanded subtask list
- **Add/edit subtasks**: open the card edit modal (subtasks section at the bottom)
- **Collapse column**: click "-" in a column header (saves in localStorage)
- **Auto-sort by priority**: Settings panel, Sorting toggle - Critical cards rise to the top within each column

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

**`order`** (integer): relative position within the column. Lower = higher in the list. The UI assigns midpoint values on drag-drop so exact integers stay stable.

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
2. Sort by `priority` ascending (1 first; treat absent as 3).
3. Within a priority tier, sort by `order` ascending.
4. Prefer cards in `"active"` over `"backlog"` at the same priority.
5. For parent cards, look at which subtasks are `"backlog"` or `"active"` - those are the actual work units.
6. Report your chosen card + subtask to Mike before starting, so he can override if priorities have shifted.
