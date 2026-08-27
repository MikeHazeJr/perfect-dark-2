# Perfect Dark 2 Workbench

Repo-local durable project truth for Perfect Dark 2. It replaces the former
Kanban board for planning, decisions, risks, asset work, validation, and
performance evidence.

The Workbench and coordination hub have different jobs:

- Workbench records durable project truth and evidence.
- `Tools/CodexCoordination` records active sessions and FIFO reservations for
  exclusive resources.

## Start

Requirements: a recent Node.js runtime.

```powershell
.\Tools\Workbench\start-workbench.bat
```

Or:

```powershell
node .\Tools\Workbench\server.js
```

Open `http://127.0.0.1:8378`. Set `WORKBENCH_PORT` to override the port. Set
`WORKBENCH_HOST=0.0.0.0` only when LAN access is intentionally wanted.

The default server is canonical-checkout only. It resolves the Git common
directory, binds data to the main project's `Tools/Workbench/data/`, and refuses
startup from a linked worktree. `GET /api/meta` reports `projectRoot`,
`worktreeRoot`, `gitCommonDir`, branch/HEAD, `canonical`, `isolated`, and
`dataDir`; repository identity is refreshed per request so a long-running
server does not report a stale HEAD. Verify these fields before API mutations
after inheriting an old server.

Tests or recovery tools that require a separate store must opt in explicitly:

```powershell
$env:WORKBENCH_ISOLATED = '1'
$env:WORKBENCH_PORT = '18378' # never 8378
$env:WORKBENCH_DATA_DIR = 'C:\path\to\isolated-data'
node .\Tools\Workbench\server.js
```

An isolated server requires both a nondefault port and an explicit data
directory. A normal server rejects `WORKBENCH_DATA_DIR` so an override cannot
silently replace durable project truth.

## Durable data

- `data/roadmap.json`: the single mutable item store.
- `data/notes.jsonl`: append-only note and note-state events.
- `data/changelog.jsonl`: append-only audit events for every roadmap mutation.
- `exports/`: generated Markdown snapshots for human review.

The server writes `roadmap.json` through a temporary file, flushes it, then
renames it atomically. It assigns new IDs, rejects duplicates, validates
dependencies and state transitions, and appends changelog events automatically.

## Views

Board, Graph, Timeline, Decisions, Assets, Validation, Performance, Notes,
Activity, and Hub are projections over the same records. No view owns a
separate task store.

## Required agent flow

1. Read `AGENTS.md`, this file, and `SCHEMAS.md`.
   Codex automatically runs the repo-local lifecycle configuration in
   `.codex/hooks.json` after the user reviews and trusts it with `/hooks`.
   `SessionStart` registers a stable coordination identity and injects the
   current Workbench summary. File-edit hooks fail closed until that identity
   owns an active item and has processed its targeted new notes. A post-edit
   `Stop` check requires a later item update or durable note.
2. Verify `/api/meta` reports the canonical project/data root.
3. Run coordination `status`, then `register`.
4. Read `data/roadmap.json` and fold `data/notes.jsonl`.
5. Process every `new` note affecting your lane before implementation.
6. Claim item ownership and record dependencies before editing shared surfaces.
7. Use the coordination FIFO before builds, tests, game runs, captures, editor
   sessions, deployments, or other exclusive work.
8. Update Workbench items as facts change. Use `implemented` only after the
   production path is connected. Use `validated` only with durable passing
   evidence.
9. Add a handoff note when ownership changes or work pauses.

Use the API whenever the server is running. Offline edits to `roadmap.json` are
allowed only for recovery or bootstrap and must include a matching append-only
changelog event.

The lifecycle hook is a guardrail, not a replacement for judgment. It does not
choose an item, resolve ownership conflicts, infer dependencies, or reserve an
exclusive resource. Sessions remain responsible for those choices and for
truthful evidence/status updates.

## Common API calls

Get the next permanent ID:

```powershell
Invoke-RestMethod 'http://127.0.0.1:8378/api/roadmap/next-id?prefix=T-ASSET'
```

Create an item with server-assigned ID:

```powershell
$body = @{
  prefix = 'T-ASSET'
  by = 'codex-session-id'
  reason = 'audit finding'
  item = @{
    type = 'task'
    area = 'ASSET'
    title = 'Example'
    status = 'missing'
    phase = 1
    deps = @()
    owner = 'codex-session-id'
    evidence = @()
    detail = 'Scope and completion contract.'
    tags = @()
  }
} | ConvertTo-Json -Depth 8
Invoke-RestMethod http://127.0.0.1:8378/api/roadmap/item -Method Post -ContentType application/json -Body $body
```

Update an item:

```powershell
$body = @{
  id = 'T-ASSET-001'
  by = 'codex-session-id'
  reason = 'production path connected'
  patch = @{
    status = 'implemented'
    model = 'gpt-5'
    evidence = @('port/src/example.c:120')
  }
} | ConvertTo-Json -Depth 8
Invoke-RestMethod http://127.0.0.1:8378/api/roadmap/update -Method Post -ContentType application/json -Body $body
```

Add and process a note:

```powershell
$add = @{target='T-ASSET-001';author='user';text='Check nested archive use.'} | ConvertTo-Json
Invoke-RestMethod http://127.0.0.1:8378/api/notes -Method Post -ContentType application/json -Body $add

$state = @{id='N-0001';state='acknowledged';by='codex-session-id'} | ConvertTo-Json
Invoke-RestMethod http://127.0.0.1:8378/api/notes/state -Method Post -ContentType application/json -Body $state
```

Questions requiring a user choice must be decision items with two to four
concise options and trade-offs. Do not hide a question in a free-text note.

## Legacy tracker

The retired Kanban implementation is retained only as immutable migration
history under `context/_old/kanban-legacy/`. New work, decisions, status, notes,
and evidence belong in Workbench.
