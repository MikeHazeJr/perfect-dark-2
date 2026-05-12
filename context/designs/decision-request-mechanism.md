# Decision-Request Mechanism on Kanban Cards

> Status: SHIPPED 2026-05-11 (worktree `clever-swirles-d24f0c`, card `c121`).
> Asynchronous decision channel between Mike and AI sessions/orchestrator, mediated through
> kanban cards. Schema extension on `state.json` + 6 HTTP endpoints + UI surfaces (badge,
> banner, side panel, modal, card animation) + block-on-active gate + orchestrator CLI.

---

## 1. Why this exists

Multi-session AI work drifts when sessions guess at unresolved design questions. The cost
is twofold: (1) work proceeds on a wrong assumption that has to be torn out later, and
(2) Mike loses visibility into what assumptions were silently made.

The mechanism gives every kanban card a place to record questions that need Mike's input.
Sessions and the orchestrator add questions to a card. Mike answers asynchronously through
the kanban browser. Cards with unanswered questions BLOCK any active or upcoming work on
that card -- the orchestrator refuses to spawn a worker session on a card whose questions
are open, and the UI surfaces the block prominently so the question never gets lost.

Mike's directive (2026-05-11):
> "When surfacing something in a [card] that has open questions, allow the card to offer
> me multiple choices curated by you, or an alternate custom response from me, that gets
> interpreted, solidified, inquired further [if] needed, and put into action when a fresh
> session or current session reads the kanban board."

Followed by:
- Notification surfaces: BOTH badge + banner counter + card highlight/animation
- Question surfaces: pop-up modal AND side panel options (in addition to inline expansion)
- `allow_custom`: ALWAYS true (Mike often adds notes/specifics even when picking curated)
- Timeout: NEVER auto-resolve; questions stay open until answered
- Questions LINKED to cards; cards with unanswered questions BLOCK work when active/upcoming
- "Don't be afraid to stop me to request me to make a call"
- Active kanban use: orchestrator and sessions actively track and check kanban for updates

---

## 2. Architecture overview

```
                       SESSION / ORCHESTRATOR (asks)
                                  |
                                  v
                    POST /api/cards/<id>/questions
                                  |
                                  v
              tools/kanban/state.json  (card.open_questions[])
                                  |
              +-------------------+-------------------+
              v                                       v
       BROWSER UI (Mike answers)               kanban_evaluator.py
       - badge + banner + animation             check-active-blocks
       - side panel + modal                     interpret-pending
       - inline expansion                       cascade-on-answer
              |                                       |
              v                                       v
       POST /api/cards/<id>/questions/<q_id>/answer
       POST /api/cards/<id>/questions/<q_id>/interpret
       POST /api/cards/<id>/questions/<q_id>/confirm-interpretation
              |
              v
            UNBLOCK
       (card.open_questions becomes all-resolved or all-confirmed)
              |
              v
        ORCHESTRATOR spawns work
```

The state lives in `tools/kanban/state.json` alongside the existing card fields. Read /
write through the HTTP server (`tools/kanban/server.py`) keeps Mike's browser and any
background session in sync via the same atomic-write pattern the rest of the kanban
already uses (temp file + os.replace).

---

## 3. Schema reference

### 3.1 State file root (additive)

```jsonc
{
  "schema_version":   2,
  "semantic_version": "0.2.0",
  "x_extensibility_rule": "Additive-only. Unknown fields tolerated. Reserved x_*. Bump schema_version on breaking; bump semantic_version on additive.",
  "settings":   { ... },
  "pillars":    [ ... ],
  "columns":    [ ... ],
  "columnLabels": { ... },
  "cards":      [ ... ]
}
```

`schema_version` was added in this slice (was unset before, treated as 1). Loaders that
do not understand `open_questions` simply ignore the field; the legacy server and
browser keep working unchanged because every new field is read with `.get(..., default)`.

### 3.2 Card extension

A new optional `open_questions` array on every card. Empty / missing means no open
questions (the default for every legacy card).

```jsonc
{
  "id":     "cNNN",
  "title":  "...",
  "...":    "...",
  "open_questions": [
    {
      "id":                       "q-NNN",
      "question":                 "Should the sidebar default to collapsed or expanded?",
      "asked_by":                 "session-id-or-orchestrator-or-mike",
      "asked_date":               "2026-05-11T00:00:00Z",
      "choices": [
        {
          "id":         "a",
          "label":      "Default collapsed",
          "rationale":  "Less visual noise on first open; matches parked sidebar default.",
          "implication": "Returning users hit one extra click to see parked threads."
        },
        {
          "id":         "b",
          "label":      "Default expanded",
          "rationale":  "Discoverability win; new users see the surface immediately.",
          "implication": "More chrome on first open; the sidebar takes 300px of width."
        }
      ],
      "allow_custom":             true,
      "answer":                   null,
      "answer_type":              null,
      "answered_date":            null,
      "custom_text":              null,
      "interpretation":           null,
      "interpretation_confirmed": false,
      "follow_up_question_ids":   []
    }
  ]
}
```

**Field semantics (frozen):**

- `id` -- monotonic `q-NNN` allocated by the server on POST (next available across the whole state file).
- `question` -- the question text shown to Mike. Plain prose, may include markdown links.
- `asked_by` -- session id, orchestrator tag, or `mike` (for self-recorded questions).
- `asked_date` -- ISO 8601 UTC timestamp at write time.
- `choices` -- array of curated options. Each has `id` (short letter, unique within the question), `label`, `rationale` (why this option), and `implication` (what changes if Mike picks it). Choices may be empty (`[]`) for pure custom-answer questions. Sessions are expected to provide rationale + implication so Mike can decide without re-reading the whole card.
- `allow_custom` -- ALWAYS true per Mike's directive; the schema honours it for future flex but the UI does not let the asker disable custom answers.
- `answer` -- the chosen `choice.id` (if answer_type=choice), or echoes `custom_text` (if answer_type=custom). null until answered.
- `answer_type` -- `"choice"`, `"custom"`, or null.
- `answered_date` -- ISO 8601 UTC timestamp at answer time.
- `custom_text` -- raw text Mike typed (independent of answer_type; Mike can add a note to a curated choice).
- `interpretation` -- when answer_type=custom, the orchestrator writes a structured interpretation back so the session that picks up the card has a clear directive. null otherwise (curated answers do not need interpretation).
- `interpretation_confirmed` -- Mike confirms the orchestrator's interpretation is correct; false until then. Curated answers come in with `interpretation_confirmed=true` automatically (the choice IS the interpretation).
- `follow_up_question_ids` -- if Mike's confirm step says "refine this", the new follow-up gets a fresh q-id and links back here.

**Resolution rule**: a question is RESOLVED when (answered AND (answer_type=choice OR interpretation_confirmed=true)). Unresolved = blocking.

---

## 4. UI map

Multiple access patterns. Mike asked for ALL of these in the same response, on purpose.

### 4.1 Inline badge on each card

A yellow `?N` badge in the top-right corner of any card with open questions. Counts the
unresolved questions. Clicking opens the modal preloaded to this card. Hover shows the
first unresolved question text as a tooltip.

### 4.2 Card highlight + animation

Cards with unanswered questions get:
- A subtle pulsing border (slow yellow glow, 3s loop, gentle).
- A tinted background (`#fefce8` light yellow on the card surface).

Resolves to normal styling as soon as all questions are resolved. CSS-only, no JS timers.

### 4.3 Top banner on browser load

If any card has unanswered questions, a yellow banner appears at the top of the kanban
panel: `<N> cards awaiting your input (<M> questions)`. Review button opens the side
panel. Dismiss button hides until the next page load.

### 4.4 Side panel (right-side, collapsible)

Mirror of the existing parked sidebar pattern. Lists every unanswered question grouped
by card. Each row shows:
- Card title + card id link.
- Question text.
- Curated choice buttons (each with hover tooltip showing rationale + implication).
- Custom text input + Submit.
- Skip-to-next-card button.

Toggle from header (next to the parked toggle) so Mike can open it any time.

### 4.5 Modal (one question at a time)

Opened by:
- Clicking the `?N` badge on a card.
- Clicking Review from the banner.
- Clicking a question row in the side panel.

Shows ONE question with full context: card title, asked_by, asked_date, the question
text, every curated choice in a button row with the choice's rationale + implication
visible (not just on hover -- the modal is for deep-think mode), a custom answer textarea
labelled "Or write a custom answer", Submit + Skip + Cancel controls.

Skip closes the modal. Submit fires the answer endpoint and:
- If choice: closes modal, refreshes the card, resolves the question if it was the last one.
- If custom: closes modal, marks pending interpretation, leaves card blocked until orchestrator interpretation + Mike confirmation come back.

### 4.6 Interpretation confirmation surface

When a custom answer has an interpretation written by the orchestrator, the question
row in the side panel + the modal show:
- Mike's original custom_text.
- The orchestrator's interpretation.
- Confirm + Refine buttons. Confirm sets `interpretation_confirmed=true`. Refine opens
  a follow-up question form so Mike can clarify, which spawns a new q-NNN linked
  via `follow_up_question_ids`.

---

## 5. Lifecycle flows

### 5.1 Curated choice path (fast)

1. Session writes question with rationale-rich choices (POST `/api/cards/<id>/questions`).
2. Browser shows badge + banner + animation on the card.
3. Mike clicks badge -> modal.
4. Mike picks a choice -> POST `/api/cards/<id>/questions/<q_id>/answer` with `{choice_id: "a"}`.
5. Server records: `answer="a"`, `answer_type="choice"`, `interpretation_confirmed=true`, `answered_date=now`.
6. Card unblocks immediately (if no other open questions).
7. Orchestrator on next pass sees the resolved question; cascade-on-answer can route the resolution into downstream work.

### 5.2 Custom-answer-with-interpretation path

1. Session writes question with curated choices and `allow_custom=true`.
2. Mike opens the modal. None of the curated choices fit. Mike writes a custom answer in the textarea.
3. POST `/api/cards/<id>/questions/<q_id>/answer` with `{custom_text: "..."}`.
4. Server records: `answer=custom_text`, `answer_type="custom"`, `interpretation=null`, `interpretation_confirmed=false`.
5. Card REMAINS blocked. UI surfaces "Awaiting interpretation".
6. Orchestrator next pass (or current session if running) reads the custom_text + card context + design docs and writes an interpretation: POST `/api/cards/<id>/questions/<q_id>/interpret` with `{interpretation: "Concrete actionable interpretation including specific files/values/behaviors."}`.
7. UI surfaces the interpretation with Confirm + Refine.
8. Mike confirms -> POST `/api/cards/<id>/questions/<q_id>/confirm-interpretation` with `{confirmed: true}`. Card unblocks.
9. Or Mike refines -> POST same endpoint with `{confirmed: false, follow_up: {question, choices, ...}}`. New question gets a fresh q-id and the original's `follow_up_question_ids` array grows. Card stays blocked on the follow-up.

### 5.3 Multi-question card

Sessions may ask several questions in one batch. The card unblocks only when ALL
questions are resolved. Mike can answer them in any order; the modal's Skip jumps to
the next unanswered question on the same card.

### 5.4 Park / unpark preserves question state

If a card with open questions is parked (right-click -> Park), the questions ride along
inside the archived card payload (`x_archived_card_payload`). On unpark, the questions
return with the card. Block-on-active gate still fires when the unparked card hits the
active or upcoming queue.

---

## 6. Block-on-active gate

The mechanism's hard rule: a card with unanswered open questions cannot have work
spawned on it.

### 6.1 Where the gate fires

**Server-side (advisory only)**: GET `/api/cards/<id>/blocked-status` returns the
blocked state for any card. Used by the UI (banner / animation) and by external
callers.

**Orchestrator-side (load-bearing)**: the daily-flow orchestrator (and any future
work-spawning system) MUST call `kanban_evaluator.py check-active-blocks` before
spawning a worker session. The CLI returns a list of `{card_id, blocked_q_ids}`.
For any card scheduled to enter `active` or `upcoming` on this pass, the orchestrator:

1. Skips spawning work on the blocked card.
2. Annotates the daily log: `BLOCKED.QUESTIONS: c<id> waiting on <count> question(s)`.
3. Surfaces the blocker in the daily briefing's headlines section.

**UI-side (visual)**: cards with unanswered questions get the BLOCKED ON QUESTIONS
banner (red strip just under the card title) when they sit in `active` or are
priority-1 in `backlog`. The strip says `BLOCKED ON QUESTIONS (<N>)` and clicking it
opens the modal.

### 6.2 What it does not gate

The gate is asymmetric on purpose:
- Mike can still drag a blocked card into the active column. The orchestrator
  refuses to assign work, but Mike's manual workflow is unimpeded.
- The card can still be edited (title, description, subtasks, etc.).
- The card can still be flagged.
- The card can be parked (the question state survives -- see 5.4).

---

## 7. Server endpoint reference

All endpoints follow the existing kanban server pattern: JSON body in / JSON body out,
CORS-permissive, atomic file writes, 4xx on validation failure with `{"error": "..."}`
body.

### `POST /api/cards/<card_id>/questions`

Add a new question to a card.

Request:
```json
{
  "question": "Required prose question text.",
  "asked_by": "session-id-or-tag",
  "choices": [
    {"id": "a", "label": "...", "rationale": "...", "implication": "..."},
    {"id": "b", "label": "...", "rationale": "...", "implication": "..."}
  ]
}
```

`choices` is optional (defaults to `[]`). The server fills `id`, `asked_date`,
`allow_custom=true`, all `answer*` fields null, `interpretation_confirmed=false`,
`follow_up_question_ids=[]`. Returns `{"ok": true, "question_id": "q-NNN"}`.

### `POST /api/cards/<card_id>/questions/<q_id>/answer`

Record Mike's answer.

Request (curated):
```json
{ "choice_id": "a" }
```

Request (custom):
```json
{ "custom_text": "Mike's free-form response." }
```

Both shapes can be present at once: Mike picked a choice AND wrote a note. In that
case the answer is the choice; the custom_text is recorded as an annotation but
`answer_type=choice` and the question resolves immediately.

Custom-only: `answer_type=custom`, `interpretation_confirmed=false`. Card stays blocked.

Returns `{"ok": true, "resolved": true|false}` -- `resolved` is true iff the question
unblocks the card on this answer.

### `POST /api/cards/<card_id>/questions/<q_id>/interpret`

Orchestrator (or AI session) writes a structured interpretation of a custom answer.

Request:
```json
{ "interpretation": "Concrete actionable directive derived from custom_text + card context." }
```

Returns `{"ok": true}`. UI shows the interpretation to Mike with Confirm + Refine controls.

### `POST /api/cards/<card_id>/questions/<q_id>/confirm-interpretation`

Mike confirms or refines an interpretation.

Request (confirm):
```json
{ "confirmed": true }
```

Request (refine -- spawns a follow-up question):
```json
{
  "confirmed": false,
  "follow_up": {
    "question": "Clarify: which of these did you mean?",
    "choices": [{"id": "a", "label": "...", "rationale": "...", "implication": "..."}]
  }
}
```

Returns `{"ok": true, "follow_up_id": "q-NNN" | null}`.

### `GET /api/open-questions`

List every unresolved question across all cards. Used by the banner and the side panel.

Response:
```json
{
  "ok": true,
  "questions": [
    {
      "card_id": "c042",
      "card_title": "...",
      "question_id": "q-007",
      "question": "...",
      "asked_by": "...",
      "asked_date": "...",
      "choices": [...],
      "answer_type": null | "custom",
      "custom_text": null | "...",
      "interpretation": null | "...",
      "needs_interpretation": false | true,
      "needs_confirmation": false | true
    }
  ],
  "blocked_card_count": 3
}
```

### `GET /api/cards/<card_id>/blocked-status`

Returns whether the card is blocked, and on which questions.

Response:
```json
{
  "ok": true,
  "blocked": true,
  "blocked_question_ids": ["q-005", "q-007"],
  "unresolved_count": 2
}
```

---

## 8. Orchestrator CLI integration

`tools/kanban_evaluator.py` -- new module, parallel structure to `tools/parked_evaluator.py`.

### `check-active-blocks`

Returns blocked cards currently in `active` (or the daily-flow orchestrator's `upcoming`
priority-1 set). Used by the orchestrator before spawning work.

```
python tools/kanban_evaluator.py check-active-blocks
```

Output (JSON to stdout):
```json
{
  "blocked": [
    {"card_id": "c042", "title": "...", "column": "active", "q_ids": ["q-005"], "count": 1}
  ],
  "total_blocked_cards": 1,
  "total_open_questions": 4
}
```

Exit code 0 always (a result of zero blocks is not a failure). Exit code 2 on I/O error.

### `interpret-pending`

Lists every custom-answer question waiting on interpretation. The orchestrator's
Claude session reads this, applies reasoning over the card + design context, and POSTs
an interpretation via the API for each.

```
python tools/kanban_evaluator.py interpret-pending
```

Output:
```json
{
  "pending": [
    {
      "card_id": "c042",
      "card_title": "...",
      "question_id": "q-007",
      "question": "...",
      "custom_text": "...",
      "context_refs": ["context/designs/..."]
    }
  ]
}
```

The CLI does NOT auto-interpret -- it surfaces the queue so the orchestrator's Claude
session can apply judgment. The interpretation itself is written via the API.

### `cascade-on-answer --question-id <q_id>`

When a question resolves, check whether parked threads or downstream work was waiting on
this card's resolution. Mirrors `parked_evaluator.cascade`. Idempotent.

```
python tools/kanban_evaluator.py cascade-on-answer --question-id q-007
```

Output:
```json
{
  "question_id": "q-007",
  "card_id": "c042",
  "card_unblocked": true,
  "parked_cascade": { "matched": [...], "flipped": [...] }
}
```

---

## 9. Integration points

### 9.1 Parked threads

Park: `x_archived_card_payload` already captures the full card, so `open_questions` ride
along with no changes to `parked.json`.

Unpark: `parked_evaluator.unpark_entry` restores the card payload verbatim. The
restored card surfaces its open questions exactly as before.

### 9.2 Daily-flow orchestrator

Pipeline step 2 (state-sync) gains a substep: read `/api/open-questions`, surface
counts in the briefing's headlines, list any card with unanswered questions in the
focus shortlist with rationale "blocked on questions".

Pipeline step 4 (merge consolidation) is unaffected -- the gate is on work spawning, not
on merging.

Pipeline step 5 (priority sort) treats blocked cards as ordinary cards in their column;
the spawner is what skips them, not the sort.

Pipeline step 6 (daily log) gets a new section: `Open questions awaiting input` listing
every card with unanswered questions plus their q-NNN ids.

### 9.3 Existing sessions

Sessions in flight should POST a question when they hit a design ambiguity, then
proceed with the work that does NOT depend on the answer. When the answer comes back,
either the current session reads it (if still alive) or the next session inherits the
resolved state via the kanban.

---

## 10. Anti-patterns

These are bad uses of the mechanism. The rationale is captured here so future
sessions catch the pattern at write time.

**Questions without rationale**: a curated choice with no rationale + implication
forces Mike to re-derive the trade-off from scratch. ALWAYS include rationale +
implication on every choice. Sessions that produce questions should think through the
options first; surfacing the questions is the easy half.

**Questions with too many choices**: 2 to 4 curated options is the sweet spot. 5+
becomes a survey, not a question, and dilutes Mike's attention. Bundle related options
or split into sequential questions if there are truly 5+ axes.

**Questions that should be sub-cards instead**: if the question has its own multi-step
implementation regardless of the answer, file a sub-card and ask the question on the
sub-card. The mechanism is for in-flight ambiguities, not for whole new tracks of work.

**Questions on cards that are already done**: pointless. The work is done. If the
answer changes how the next iteration goes, ask on the next card. Sessions should
inspect `column == "done"` before posting.

**Long-running unanswered questions**: questions never time out, but if a question has
sat unanswered for 14 days the orchestrator surfaces it as stale in the daily briefing.
This is the same staleness pattern as parked threads.

---

## 11. File layout

```
context/designs/decision-request-mechanism.md   (this file)
tools/kanban/
  state.json                                     (schema_version=2; cards[].open_questions[])
  server.py                                      (+6 endpoints, +block-on-active GET)
  index.html                                     (+badge, +banner, +side panel, +modal, +animation)
tools/kanban_evaluator.py                        (new CLI: check-active-blocks, interpret-pending, cascade-on-answer)
```

Total surface area: one design doc, one new CLI module, surgical additions to three
existing files. Schema additive; no migrations needed for legacy cards (they simply
have no `open_questions` field).

---

## 12. What this explicitly does not do

- **No auto-resolve**. Per Mike, questions never auto-resolve. They stay open until answered.
- **No notification outside the kanban**. No email, no Slack, no desktop toast. The kanban browser is the surface; Mike checks it.
- **No question dependencies**. A question is not blocked by another question. Cards block on the aggregate of their own questions; questions do not block other questions. Follow-ups are the only inter-question structure, and they exist by id reference, not by gate.
- **No question categorization**. There is no `type` field on questions (architectural vs tactical vs UI). The question text + rationale are enough; the orchestrator can categorise at read time if needed.
- **No question reassignment**. Questions are answered by Mike, period. There is no `assigned_to` field.

---

## 13. Where to look

- Schema additions: `tools/kanban/state.json` root + per-card `open_questions[]`.
- Server endpoints: `tools/kanban/server.py` (new `_question_*` helpers, 6 new endpoint blocks).
- UI surfaces: `tools/kanban/index.html` (new `?N` badge in `buildCard`, new `#oq-banner`, new `#oq-sidebar`, new `#oq-modal-overlay`, new `oq-*` CSS).
- Block-on-active: `tools/kanban_evaluator.py::check-active-blocks`. Called by daily-flow orchestrator before spawning.
- Decision lifecycle: `context/designs/decision-request-mechanism.md` Sections 5 + 6 above.

SENTINEL: end of design doc.
