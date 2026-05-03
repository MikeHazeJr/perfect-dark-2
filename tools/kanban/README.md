# PD2 Kanban

Bidirectional task board. Mike edits via browser. AI reads and writes `state.json` directly.

## Run

```bash
python tools/kanban/server.py
```

Then open: `http://localhost:7531/`

## State file

`tools/kanban/state.json` - single source of truth. Readable and writable by AI sessions directly.
The server writes it atomically (temp file + rename) on every browser save.

## Browser workflow

- **Filter**: click pillar buttons in the top bar to focus on one track
- **Drag cards**: move between columns or reorder within a column
- **Edit card**: click any card to open the edit modal
- **Add card**: click "+ Add card" at the bottom of any column
- **Collapse column**: click `-` in a column header (saves in browser localStorage)
- **Manage pillars**: click "Pillars" button top-right

## AI workflow

Read state: `tools/kanban/state.json`
Write state: edit `state.json` directly, or POST to `http://localhost:7531/api/state` with the full updated JSON body.

Cards have: `id`, `title`, `description`, `pillar`, `column` (backlog/active/blocked/done), `order` (integer, lower = higher in column), `notes`, `created`, `updated`.
