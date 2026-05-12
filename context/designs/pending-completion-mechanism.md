# Pending-Completion Mechanism on Kanban Cards

> Status: SHIPPED 2026-05-12 (worktree `jovial-tesla-3da9fd`, card `c123`).
> Asynchronous completion-review channel between Mike and AI sessions/orchestrator,
> parallel in shape to the c121 decision-request mechanism. A session or the
> orchestrator can mark a card as "ready for completion review" without moving it
> to `done`. Mike Confirms (-> `done`) or Rejects (stays in current column, with
> optional rejection note appended to card notes).

---

## 1. Why this exists

Sessions and the orchestrator routinely judge work to be complete. They commit,
push, and update the kanban. The card flips to `done`. But Mike has no
intermediate gate: he sees the card already in `done` after the fact, and any
quality concern requires him to drag the card back out of `done` and re-open a
discussion.

The mechanism gives every card an optional `pending_completion` state that means
"a session believes this is done; Mike, take a look before it lands in done."
Sessions and the orchestrator surface a summary plus evidence refs (commit
SHAs, file paths, doc links). Mike reviews asynchronously in the kanban browser
and clicks Confirm (-> `done`) or Reject (stays in column, optional note).

Mike's directive (2026-05-12):
> "You may tag things as closed / completed, but mark them for me to confirm,
> similar to the 'review' option you gave me for open questions."

Parallel UX to c121's decision-request flow on purpose: same asynchronous-decision
shape, same review-banner pattern, same modal/sidebar infrastructure footprint.

---

## 2. Architecture overview

```
                       SESSION / ORCHESTRATOR (marks)
                                  |
                                  v
                POST /api/cards/<id>/mark-pending-completion
                                  |
                                  v
              tools/kanban/state.json  (card.pending_completion)
                                  |
              +-------------------+-------------------+
              v                                       v
       BROWSER UI (Mike reviews)                kanban_evaluator.py
       - green-ring on card                     list-pending-completions
       - READY micro-badge                      (CLI; library API)
       - bottom-docked banner counter
       - side panel with Confirm + Reject       daily-flow step 6
              |                                 (Awaiting Your Confirmation)
              v
       POST /api/cards/<id>/confirm-completion        (-> done)
       POST /api/cards/<id>/reject-completion         (stays in column)
              |
              v
        card.pending_completion = null
        card.updated refreshed
```

State lives alongside open_questions on the same card. Reads/writes go through
the existing HTTP server (`tools/kanban/server.py`) with atomic temp+rename.

---

## 3. Schema reference

### 3.1 State file root

`schema_version` stays at **2** (additive). `semantic_version` bumps to
**0.3.0** (additive change documented in `x_extensibility_rule`).

### 3.2 Card-level field

```json
{
  "id": "cNNN",
  "title": "...",
  ...
  "pending_completion": {
    "marked_at": "2026-05-12T14:23:00Z",
    "marked_by": "session-id-or-orchestrator",
    "summary": "Short prose describing what was completed and why this is ready",
    "evidence_refs": ["commit-sha", "file-path-or-URL", "..."]
  } | null
}
```

Cardinality: at most one pending_completion per card. Re-marking overwrites the
prior block with a fresh `marked_at`. Rejecting and re-marking is supported.

Fields:
- `marked_at` -- ISO-8601 UTC timestamp set by server at mark time.
- `marked_by` -- free-form string. Convention: `session-<slug>`, `orchestrator`,
  or a human handle. Required.
- `summary` -- required. Meaningful prose, not "done". The orchestrator brief
  states: e.g. "Implemented X feature with file Y and z scenarios passing
  tests; design doc at path/to/design.md".
- `evidence_refs` -- array of strings. Commit SHAs, file paths, URLs. UI
  surfaces URLs as `<a target="_blank">`; everything else opens a copy prompt.

### 3.3 Extensibility

Additive-only. Reserved `x_*` namespace. Future fields under consideration but
not in this ship: `x_blocker_bugs` (list of B-NNN ids that must be fixed first),
`x_reviewer_assigned`, `x_review_lead_time_target`.

---

## 4. Server endpoints

All four mounted in `tools/kanban/server.py`. Atomic writes. CORS-permissive.

### POST /api/cards/<id>/mark-pending-completion

Body: `{"marked_by": "...", "summary": "...", "evidence_refs": [...]}`.

Validates:
- card exists -> else 404
- card not already in `done` -> else 409
- `marked_by` non-empty -> else 400
- `summary` non-empty -> else 400
- `evidence_refs` is an array (strings; coerced + trimmed; empty filtered out)

Effect: writes the pending_completion block; refreshes `updated`. Re-marking an
already-pending card overwrites cleanly.

Response: `{"ok": true, "card_id", "marked_at"}`.

### POST /api/cards/<id>/confirm-completion

Body: ignored (empty `{}`).

Validates:
- card exists -> 404
- card has pending_completion non-null -> 409 otherwise

Effect: sets `column = "done"`, computes `order` as max-of-done+1000, clears
`pending_completion`, refreshes `updated`.

Response: `{"ok": true, "card_id", "from_column", "to_column": "done"}`.

### POST /api/cards/<id>/reject-completion

Body: `{"rejection_note": "..."}` (optional).

Validates:
- card exists -> 404
- card has pending_completion non-null -> 409 otherwise

Effect: clears `pending_completion`. Card stays in its current column with
`updated` refreshed. If `rejection_note` is non-empty, appends a timestamped
line to `card.notes` with the form
`[ISO] completion-rejected (was marked by <marker>): <note>`.

Response: `{"ok": true, "card_id", "column", "note_appended"}`.

### GET /api/pending-completions

No body. Walks every card, returns the ones with `pending_completion` non-null,
sorted by `marked_at` ascending.

Response:
```json
{
  "ok": true,
  "pending_count": N,
  "pending": [
    {
      "card_id", "card_title", "card_column", "card_pillar",
      "marked_at", "marked_by", "summary", "evidence_refs": [...]
    }, ...
  ]
}
```

---

## 5. UI surfaces

In `tools/kanban/index.html`:

- **Card-level**: cards with `pending_completion` non-null gain a green box
  shadow (`has-pending-completion`) plus a small `READY` micro-badge in the
  top-right corner next to the flag slot. Hover shows the summary. Click jumps
  to the review side panel.
- **Header counter**: a `Ready <N>` toggle button appears in the header when
  count > 0 (mirrors the `Questions` toggle from c121). Click opens the side
  panel.
- **Bottom-docked banner**: `#pc-banner` with "N cards marked ready for
  completion review" + Review + collapse + dismiss buttons. Sits at the bottom
  of the body (via `order: 97`) below `#main-area`. Collapsible to a thin
  strip, dismissable to a floating green `R` icon at bottom-right that
  restores when clicked.
- **Side panel** `#pc-sidebar` (380 px right-anchored): one row per pending
  card, listing summary, marked-by + marked-at, evidence refs as clickable
  links, plus a Confirm button, Reject button, and a small textarea for the
  optional rejection note.

### 5.1 Banner docking and collapse (companion change)

c123 also moves the c121 open-questions banner and the daily-flow briefing
banner from the top of the page to the bottom, matching the new pending-
completion banner. All three banners use the same flex-order trick:
`order: 97/98/99` to render at the visual bottom of the body's column layout.
A shared `collapsed` class collapses each banner to a thin strip showing only
its title + chevron, with the chevron toggling between `v` (expanded) and `^`
(collapsed). Each banner has its own floating restore icon (`R` for pending
completion, `?` for open questions, `B` for briefing) which appears at
bottom-right when dismissed and clears when restored.

State is persisted in `localStorage` under `pd2kb-banner-collapsed` as
`{briefing, openQuestions, pendingCompletion}` booleans.

### 5.2 Page-scroll fix (companion change)

The hardcoded `.column { max-height: calc(100vh - 110px) }` is replaced with
`max-height: 100%` plus `align-items: stretch` on `#board` (was `flex-start`)
and `overflow: hidden + min-height: 0` on `.panel#panel-kanban`. This makes
the column adapt to whatever vertical space the board parent actually has
(after banners, headers, filters claim their flex-shrink:0 space), so the
column never extends below the visible board area. Cards inside the column
continue to scroll vertically via the inner `.card-list-wrap`.

---

## 6. Lifecycle diagram

```
                        +------------------+
                        | session/orches.  |
                        | runs, ships code |
                        +---------+--------+
                                  |
                                  v
                    POST mark-pending-completion
                                  |
                                  v
                +--------------------------------+
                | card.pending_completion = {..} |
                | card.column unchanged          |
                +-------------+------------------+
                              |
                +-------------+-------------+
                v                           v
       Mike clicks Confirm           Mike clicks Reject
                |                           |
                v                           v
       POST confirm-completion       POST reject-completion
                |                           |
                v                           v
       card.column = "done"          card.column unchanged
       pending_completion = null     pending_completion = null
                |                           |
                                  |
                                  v
                       (card lifecycle continues;
                        session can re-mark after
                        responding to feedback)
```

---

## 7. Orchestrator + CLI integration

### kanban_evaluator.py

New library function `list_pending_completions(kanban) -> list[dict]` walks every
card, returns the ones with `pending_completion` non-null, sorted by
`marked_at` ascending. Each entry includes card_id, card_title, card_column,
card_pillar, marked_at, marked_by, summary, evidence_refs.

CLI subcommand: `python tools/kanban_evaluator.py list-pending-completions`
prints `{"pending": [...], "pending_count": N}`.

### Daily-flow step 6

`tools/daily_flow/steps/step6_daily_log.py::_render_awaiting_confirmation`
renders the `## Awaiting Your Confirmation` section in `context/daily-logs/
<YYYY-MM-DD>.md`. The section is template-enforced (`DAILY_SECTIONS_ORDER`
gains `"Awaiting Your Confirmation"` between `Decisions` and `Parked
Threads`), so the header is always present even when empty. When non-empty,
each pending card renders as:

```
### `cNNN` Title -- in `column` (marked today)
- Marked by: <who> at <ISO>
- Summary: <prose>
- Evidence: `ref1`, `ref2`, ...
- Review at: http://localhost:7531/  (Ready button in header, then Confirm or Reject)
```

The `(marked today)` callout fires when `marked_at` starts with the daily-log
date. Aged pending entries surface every morning until Mike acts, so a
forgotten entry cannot silently rot.

---

## 8. Anti-patterns

- **Do not mark a card pending-completion that is already in `done`.** Server
  returns 409. If the card was wrongly moved to `done`, drag it back out
  manually before marking.
- **Do not confirm a pending card from a session.** Confirm is Mike's gate.
  Sessions only mark.
- **Do not write thin summaries.** "done" / "complete" / "shipped" are not
  acceptable. The orchestrator brief explicitly requires meaningful prose
  ("Implemented X feature with file Y and z scenarios passing tests; design
  doc at path/to/design.md").
- **Do not bypass via direct state.json edits.** The HTTP path is the
  authority; direct edits skip atomic-write semantics and any future
  validators we layer on.

---

## 9. File layout

- `tools/kanban/state.json` -- schema extension on `cards[].pending_completion`
  (semantic_version 0.3.0).
- `tools/kanban/server.py` -- 4 new endpoints; 2 helper functions
  (`_card_max_order`, `_append_note`).
- `tools/kanban/index.html` -- card-level READY badge + green box shadow;
  `Ready` header toggle button + badge; `#pc-banner` (bottom-docked,
  collapsible); `#pc-sidebar` (right side panel); JS module for pending
  completions. Also: page-scroll fix + banner docking for c121 + briefing.
- `tools/kanban_evaluator.py` -- `list_pending_completions` library API plus
  `list-pending-completions` CLI subcommand.
- `tools/daily_flow/lib/templates.py` -- `DAILY_SECTIONS_ORDER` gains
  `"Awaiting Your Confirmation"`.
- `tools/daily_flow/steps/step6_daily_log.py` -- `_render_awaiting_confirmation`
  hooked into `sections` dict.
- `context/designs/pending-completion-mechanism.md` -- this doc.

---

## 10. Explicit non-goals

- **Auto-resolution.** No timeout, no auto-confirm. Mike's gate is permanent.
- **Multi-confirmer.** No reviewer-assignment, no quorum. Single-developer
  project.
- **Wire / cross-machine replication.** Single-machine kanban server; nothing
  to replicate.
- **Pillar-specific routing.** No per-pillar reviewer policy; one Mike, one
  gate.

---

## 11. SHIPPED summary

Verified end-to-end via `.claude/scratch/probe-pending-completion.py` (13
phases, all PASS). Probe walks: empty list, 400 missing-summary,
400 missing-marked_by, 404 unknown card, happy-path mark, GET list,
CLI list, reject with note, 409 reject-when-no-pc, mark+confirm,
409 mark-on-done, 409 confirm-when-no-pc, list-empty-after-confirm.

Worktree `jovial-tesla-3da9fd`, card `c123`, pillar Tooling, schema_version 2
semantic_version 0.3.0.
