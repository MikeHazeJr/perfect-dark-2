# Perfect Dark 2 Workbench schemas

All Workbench data lives in `Tools/Workbench/data/`. `roadmap.json` is the only
mutable store. `notes.jsonl` and `changelog.jsonl` are append-only.

## Permanent IDs

IDs are never renamed, deleted, reused, or hand-counted.

| Type | Prefix | Example |
|---|---|---|
| Area | `RM-<AREA>-###` | `RM-ASSET-001` |
| Task | `T-<AREA>-###` | `T-ASSET-001` |
| Decision | `D-####` | `D-0001` |
| Risk | `R-####` | `R-0001` |
| Asset | `A-<AREA>-###` | `A-ARCHIVE-001` |
| Validation | `V-####` | `V-0001` |
| Performance | `P-####` | `P-0001` |
| Note | `N-####` | `N-0001` |

Ask `GET /api/roadmap/next-id?prefix=<PREFIX>` or omit `item.id` when posting a
new item. The server assigns the next number and rejects duplicates.

## Roadmap item

```json
{
  "id": "T-ASSET-001",
  "type": "task",
  "area": "ASSET",
  "title": "Audit extraction completeness",
  "status": "partial",
  "phase": 1,
  "lane": "extraction",
  "owner": "codex-session-id",
  "deps": [],
  "evidence": ["port/src/romextract.c:120"],
  "detail": "Scope, constraints, and completion contract.",
  "tags": ["asset-pipeline"],
  "model": null,
  "legacyIds": ["c3844"],
  "created": "ISO-8601",
  "updated": "ISO-8601"
}
```

Types:

- `task`
- `decision`
- `risk`
- `asset`
- `validation`
- `performance`
- `area`

Truth statuses for task, asset, validation, performance, and area items:

- `missing`: required behavior is absent.
- `stub`: a non-production skeleton exists.
- `partial`: some behavior exists but the full production path is incomplete.
- `implemented`: the full production path is connected, with evidence and model
  attribution.
- `validated`: durable passing evidence proves the contract. Validation and
  performance items additionally require a `pass` verdict and artifacts.
- `blocked`: the item cannot currently progress; `detail` states why.
- `cut`: explicitly removed from scope with user authority.

Decision statuses: `open`, `decided`, `deferred`.

Risk statuses: `open`, `mitigated`, `accepted`, `closed`, `blocked`.

Decision items require:

```json
{
  "options": [
    {"id": "A", "label": "Option one", "tradeoff": "Concise impact."},
    {"id": "B", "label": "Option two", "tradeoff": "Concise impact."}
  ],
  "resolution": null
}
```

Validation items add:

```json
{
  "verdict": "not_run",
  "artifacts": [],
  "budget": null,
  "measurements": []
}
```

Performance items require a budget and add:

```json
{
  "metric": "cold extraction time",
  "budget": "<= 30 s on reference host",
  "measurements": [],
  "verdict": "not_measured",
  "artifacts": []
}
```

Verdicts are `not_run`, `not_measured`, `pass`, `fail`, or `blocked`.

## Note events

```json
{"event":"note_add","id":"N-0001","ts":"...","target":"T-ASSET-001","author":"user","text":"...","state":"new"}
{"event":"note_state","id":"N-0001","ts":"...","state":"acknowledged","by":"codex-session-id","reason":null}
{"event":"note_state","id":"N-0001","ts":"...","state":"incorporated","by":"codex-session-id","reason":"Implemented in T-ASSET-002"}
```

Lifecycle:

`new -> acknowledged -> incorporated | rejected_with_reason |
needs_clarification | waiting_user_decision | superseded`

`rejected_with_reason` requires `reason`. `superseded` requires
`supersededBy`. A clarification or waiting note may be acknowledged again or
superseded when a later note/decision resolves it.

Every session processes new notes affecting its lane before working. Questions
requiring user choice are dedicated decision items, not bare notes.

## Changelog events

```json
{"seq":1,"ts":"...","kind":"item_create","by":"codex-session-id","id":"T-ASSET-001","item":{},"reason":"..."}
{"seq":2,"ts":"...","kind":"item_update","by":"codex-session-id","id":"T-ASSET-001","changes":{"status":{"from":"partial","to":"implemented"}},"reason":"..."}
```

`seq` increases monotonically. Kinds include `bootstrap`, `legacy_migration`,
`item_create`, `item_update`, `note_add`, `note_state`, and `export`.

## API

| Endpoint | Purpose |
|---|---|
| `GET /api/roadmap` | Full roadmap |
| `GET /api/roadmap/next-id?prefix=P` | Next permanent ID |
| `POST /api/roadmap/item` | Create and validate an item |
| `POST /api/roadmap/update` | Validate and atomically apply a patch |
| `GET /api/notes` | Folded notes with history |
| `POST /api/notes` | Add a new note |
| `POST /api/notes/state` | Append a validated lifecycle event |
| `GET /api/changelog` | Latest audit events |
| `GET /api/coordination` | Read-only coordination mirror |
| `GET /api/meta` | Counts and duplicate-ID diagnostics |
| `POST /api/export` | Generate Markdown exports |

`GET /api/meta` also exposes the canonical project root, current worktree root,
Git common directory, current and canonical branch/HEAD, canonical/isolated
mode, and active data directory. The default port accepts only the canonical
checkout and canonical data store. Explicit isolated mode requires
`WORKBENCH_ISOLATED=1`, a nondefault port, and `WORKBENCH_DATA_DIR`.

`id` and `type` are immutable after creation. Unknown update fields are
rejected. Dependencies must exist and cannot point to the item itself.
