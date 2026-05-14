# Sprint Report - Jump Two-Stage Capsule Sweep Design

- Sprint ID: sprint-2026-05-14T020100-jump-design
- Date: 2026-05-13
- Branch: dev
- Pillar / Card: physics-collision / c038
- Source: 2026-05-13 incompleteness audit, Sprint 2 (Track 4)

## Goal

Author the design doc for the jump-collision two-stage capsule sweep
upgrade, scaffold a test pin to guard the doc, and re-pillar kanban
card c038 from `vehicles` to `physics-collision`. No live jump or
capsule sweep code is modified; this is design + scaffolding only,
pending Mike's approval before any implementation slices begin.

## Shipped

- `context/designs/physics-collision/jump-two-stage-sweep.md` -- new,
  130 lines, sentinel `## Where to look` terminator. Sections:
  `## Status / Premise`, `## Problem statement`, `## Current call
  path`, `## Proposed two-stage design`, `## API plan`,
  `## Per-frame cost analysis`, `## Surface-normal unlock`,
  `## Coverage parity`, `## Migration plan`, `## Risks`,
  `## Open questions for Mike`, `## Where to look`.
- `tests/test_jump_two_stage_design.cpp` -- new design pin, single
  `TEST_CASE` tagged `[physics][jump][design][static]`. Source-greps
  the design doc for non-emptiness and the 7 required section
  prefixes. No live game code is invoked.
- `CMakeLists.txt` -- one block inserted after the Cohort 4 tests
  (around line 965) listing the new design-pin test under
  `SRC_TESTS`.
- `tools/kanban/state.json` -- card c038 re-pillared:
  - `pillar`: `vehicles` -> `physics-collision`.
  - `title`: `"Vehicles: wall-jump glitch (deferred)"` ->
    `"Physics-collision: wall-jump glitch (two-stage capsule sweep
    design landed)"`.
  - `description`: replaced per Sprint 2 instructions, citing the
    `cdTestVolume` substrate gap and the new design doc.
  - `priority`: `5` -> `3`.
  - `updated`: `2026-05-02T00:00:00Z` -> `2026-05-14T02:28:26Z`.
- `tools/kanban/state.json` -- new `pillars[]` entry registered:
  `{ "id": "physics-collision", "name": "Physics-collision",
  "color": "#0ea5e9" }`. Required so the commit-msg hook's pillar
  lookup resolves `Physics-collision` to a registered pillar. The
  card's new `pillar` field references this id.

## Decisions

1. **Add `physics-collision` to the pillars registry.** The task
   spec required setting `c038.pillar = "physics-collision"`. The
   commit-msg hook (`.githooks/commit-msg.py:99-198`) requires
   the subject's pillar token to resolve to a registered pillar
   AND match the card's pillar field. Without a registry entry,
   the commit would have been rejected. The registry addition is
   the minimal change required to make the prescribed card edit
   valid under the hook -- not "another card edit". Pillar id
   matches the existing context pillar doc at
   `context/pillars/physics-collision.md`.
2. **Two-stage over wholesale replacement.** Design recommends
   keeping `cdTestVolume` Stage 1 as a cheap pre-cull and adding
   `bgTestHitInRoom` Stage 2 as a per-triangle validator only when
   Stage 1 reports collision. Rationale: zero regression on the
   dominant no-collision frame; strict superset coverage; flag-
   reversible during initial slices.
3. **Conservative override semantics for Stage 2.** Design states
   Stage 2 only overrides Stage 1's safefrac if Stage 2 reports a
   closer hit -- this avoids prop-AABB-vs-rendered-tri disagreement
   regressions where the AABB skin is more generous than the visible
   mesh.
4. **No live code change.** Per Sprint 2 instructions and the
   pre-task sanity check in CLAUDE.md, this commit is design +
   scaffolding only. Implementation deferred to follow-up slices
   gated on Mike's approval.

## Verification

- Em-dash count in design doc: `grep -cP $'[--]'` reports 0 (exit
  code 1 = no matches). Spec is met.
- JSON validity of `tools/kanban/state.json`: parses cleanly via
  `python -c "import json; json.load(open(...))"`. c038 fields read
  back as expected. New `physics-collision` pillar entry present.
- Design doc sentinel terminator: tail of file is the `## Where to
  look` section's last bullet -- confirmed.
- Test file requires the 7 section prefixes from the task spec:
  `## Status`, `## Problem statement`, `## Proposed two-stage
  design`, `## API plan`, `## Risks`, `## Open questions for Mike`,
  `## Where to look`. The actual doc uses `## Status / Premise`,
  and `find("## Status")` matches the prefix -- intentional and
  correct.
- No live game code (`src/game/bondwalk.c`, `src/lib/capsule.c`,
  `src/lib/collision.c`, `src/game/propobj.c`, `src/game/bg.c`)
  modified.
- Allowed-file list discipline: only the four prescribed files plus
  the pillars registry addition were touched. `context/session-log.md`,
  `context/tasks.md`, `context/README.md`, `context/bugs.md` not
  touched -- orchestrator consolidates.

## Build / test status

- Not built. This is a metadata + design doc + test source change.
  The new test is a pure source-grep against the design doc and will
  be exercised when `pd-tests` runs next.

## Follow-ups for Mike

1. Approve or reject the two-stage approach. Five open questions
   listed in design doc `## Open questions for Mike`.
2. If approved, the next slice is the API land (Step 1 of the
   Migration plan): add `capsuleSweepTwoStage` parallel to the
   existing `capsuleSweep`, with unit tests covering the wall-jump
   glitch repro.
3. Skedar Slice 5 (`context/designs/in-flight/skedar-surface-normal-
   locomotion.md`) gains a real `surface_up` input once Stage 2
   produces real per-triangle normals -- track the dependency in
   c038's notes if Mike wants explicit linkage (Open Question 5).

## Refs

- c038 (re-pillared, scaffold landed).
- Audit source: `context/audits/incompleteness-sweep-input-context-
  extraction-jump-2026-05-13.md` Track 4 sections 4.1-4.11.

## Where to look

- `context/designs/physics-collision/jump-two-stage-sweep.md` -- the
  design doc.
- `tests/test_jump_two_stage_design.cpp` -- the design pin.
- `CMakeLists.txt` -- new test wired into `SRC_TESTS`.
- `tools/kanban/state.json` -- c038 re-pillared; `physics-collision`
  pillar registered.
