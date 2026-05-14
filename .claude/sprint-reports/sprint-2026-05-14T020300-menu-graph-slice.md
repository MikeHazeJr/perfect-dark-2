# Sprint Report - s036-08 Menu Graph Helper-Funnel Slice

- Sprint ID: sprint-2026-05-14T020300-menu-graph-slice
- Date: 2026-05-14
- Branch: dev
- Pillar / Card: input / c036 (subtask s036-08)
- Source: 2026-05-13 incompleteness audit, Sprint 5 menu graph completion lane

## Goal

Continue the c036 s036-08 menu graph completion lane after the L.60
four-node slice (commit `593f63f0`). Migrate three more raw
`menuPopDialog()` call sites to `menuGraphFirePop(...)` in smaller
priority screens. Scope rule for this slice: one graph node per file,
one EDGE_POP per call site, no stub nodes added just to migrate a push
whose target type is not yet in the graph.

## Candidate audit

The audit identified five candidate files. After per-file inspection
the three files chosen for this slice all share a clean helper-funnel
pattern: a single per-file `*_CloseCurrentDialog` helper holds the
only raw `menuPopDialog()` in the file, and every renderer in the file
drives the same `menu_type_t` pool slot, so the helper body can be
swapped for a single `menuGraphFirePop` call without renderer-by-
renderer edits.

| File | Helper | Raw pops | Source menu_type_t | Push sites |
| --- | --- | --- | --- | --- |
| `pdgui_menu_mpsetup.cpp` | `mp_CloseCurrentDialog` | 1 | `MENU_TYPE_MP_SETUP` | 1 (deferred) |
| `pdgui_menu_mpadvanced.cpp` | `ma_CloseCurrentDialog` | 1 | `MENU_TYPE_MP_ADVANCED` | 2 (deferred) |
| `pdgui_menu_botsetup.cpp` | `bs_CloseCurrentDialog` | 1 | `MENU_TYPE_MP_BOT_SETUP` | 2 (deferred) |
| `pdgui_menu_mpsettings.cpp` | `pdms_CloseCurrentDialog` | 1 (multi-source helper) | n/a | 1 (deferred) |
| `pdgui_menu_controldiagram.cpp` | inline | 4 | mixed (no graph node yet for `MENU_TYPE_CONTROL_DIAGRAM`) | 1 (deferred) |

The two files with bold scope risk (`mpsettings.cpp`'s helper services
several different pool types and `controldiagram.cpp`'s pops are tied
to a `menu_type_t` that has no graph node yet) were skipped per the
slice rule "do not balloon by adding stub nodes". They remain on the
s036-08 backlog for a future slice.

Push migrations were deferred for the same reason: the push targets
either point at a `menu_type_t` whose graph node does not exist
(`MENU_TYPE_MP_TUNES`), at an unregistered dialogdef
(`g_ExtGameOptionsMenuDialog`, `g_MpSimulantCharacterMenuDialog`), or
at a variable target unknown at compile time
(`hubPushRow(... struct menudialogdef *target ...)`).

## Shipped

- `port/src/menugraph.c` -- 3 new graph nodes + 3 EDGE_POP edges.
  - `s_MpSetupEdges`: `EDGE_POP("back", ACTION_MENU_CANCEL, "Back")`.
  - `s_MpAdvancedEdges`: `EDGE_POP("back", ACTION_MENU_CANCEL, "Back")`.
  - `s_MpBotSetupEdges`: `EDGE_POP("back", ACTION_MENU_CANCEL, "Back")`.
  - `NODE(MENU_TYPE_MP_SETUP, "mp_setup", s_MpSetupEdges)`,
    `NODE(MENU_TYPE_MP_ADVANCED, "mp_advanced", s_MpAdvancedEdges)`,
    `NODE(MENU_TYPE_MP_BOT_SETUP, "mp_bot_setup", s_MpBotSetupEdges)`
    appended after the L.60 MP_PLAYER_CONFIG node and before the
    `MENU_TYPE_FR_DIFFICULTY` node.

- `port/fast3d/pdgui_menu_mpsetup.cpp`:
  - Added `#include "menugraph.h"` next to the existing `menupool.h`.
  - `mp_CloseCurrentDialog` body: replaced `menuPopDialog()` with
    `menuGraphFirePop(MENU_TYPE_MP_SETUP, "back")` + commented why
    the source is uniformly MP_SETUP across every renderer in the
    file (Arena, Scenario, Weapons, Limits, scenario-option family,
    Ready).

- `port/fast3d/pdgui_menu_mpadvanced.cpp`:
  - Added `#include "menugraph.h"`.
  - `ma_CloseCurrentDialog` body: replaced `menuPopDialog()` with
    `menuGraphFirePop(MENU_TYPE_MP_ADVANCED, "back")` + comment
    noting MP_ADVANCED is the uniform source (Advanced Setup hub,
    Quick Go, Quick Team variants, challenge list/details).

- `port/fast3d/pdgui_menu_botsetup.cpp`:
  - Added `#include "menugraph.h"`.
  - `bs_CloseCurrentDialog` body: replaced `menuPopDialog()` with
    `menuGraphFirePop(MENU_TYPE_MP_BOT_SETUP, "back")` + comment
    noting MP_BOT_SETUP is the uniform source (Simulants roster,
    Add / Change / Edit Simulant, Simulant Character picker).

- `tests/test_menu_graph.cpp` -- 3 new TEST_CASE blocks appended
  after the L.60 four-case slice, tagged
  `[input][menu_graph][mpsetup/mpadvanced/botsetup][static]`. Each
  case mirrors the L.60 pattern: read the source file + the graph
  source file; REQUIRE the include, REQUIRE the NODE registration,
  use `functionBlock` to extract the helper, REQUIRE the new
  `menuGraphFirePop` call inside the helper, REQUIRE absence of
  raw `menuPopDialog()` in the helper.

- `context/designs/input/input-universality-and-transitions.md` --
  appended `### L.61 s036-08 slice: three helper-funnel pop
  migrations (2026-05-14)` after L.60, before the `---` Appendix
  separator. Lines 1367-1381 (15 lines + trailing blank line before
  the `---`). Documents files touched, edge-naming rationale, the
  deferred-push call-out, and remaining s036-08 surface
  (~27 raw push/pop sites).

- `tools/kanban/state.json` -- c036 entry only:
  - `updated`: `2026-05-14T01:31:25Z` -> `2026-05-14T02:03:00Z`.
  - `c036.subtasks[s036-08].notes`: appended a one-paragraph slice
    progress note covering the three new nodes, the three migrated
    helpers, the new tests, and the deferred-push rationale.

## Decisions

1. **Three files, not four.** The audit listed five candidates with
   "1 pop + 1 push" or similar per-file counts. Two of them
   (`mpsettings.cpp`, `controldiagram.cpp`) violate the slice's
   one-graph-node-per-file invariant because their pops cross pool
   types or target a graph node that does not yet exist. Picking
   only the three files where the funnel maps cleanly preserves the
   slice's surgical character and matches the L.60 envelope.

2. **Migrate inside the helper, do not refactor the helper
   signature.** Each `*_CloseCurrentDialog` is called from 8 to 13
   sibling renderers in its file. Refactoring the signature to take
   a `menu_type_t` argument would touch every renderer and dilute
   the diff. Since every renderer in each file drives the same
   pool slot, the helper body knows the source statically and
   keeps the call sites untouched. This is the smallest correct
   change.

3. **Defer all pushes in these files.** The slice instructions
   explicitly say "if a push edge targets a destination
   `menu_type_t` whose graph node does not yet exist, skip that
   push". Every push site in the three files hits one of the three
   deferral categories. Adding stub graph nodes just to migrate
   the pushes would push the slice past the "2-4 edges" envelope
   set in s036-08's notes and would compromise the audit-driven
   per-file inventory.

4. **Keep the `void menuPopDialog(void);` forward declaration.**
   Even after the body migration, the helper's `extern "C"` block
   in each file still declares `menuPopDialog`. `menuGraphFirePop`
   itself calls `menuPopDialog` internally inside
   `port/src/menugraph.c`, so the C++ files do not need the
   declaration once the helper body migrates. Leaving the
   prototype in place is a no-op (unused decl, no warning) and
   removes a class of merge-conflict risk if a parallel slice adds
   another helper that needs the raw call.

## Verification

- Source-grep for `menuPopDialog\(\)` in each migrated file:
  zero hits in `port/fast3d/pdgui_menu_mpsetup.cpp`,
  `port/fast3d/pdgui_menu_mpadvanced.cpp`, and
  `port/fast3d/pdgui_menu_botsetup.cpp` after the edits.
- Each migrated file contains a single `menuGraphFirePop(...)` call
  with the expected source `menu_type_t` and edge id `"back"`.
- `tools/kanban/state.json` parses cleanly via
  `python -c "import json; json.load(open(...))"`.
- The L.60 four-case block in `tests/test_menu_graph.cpp` is
  preserved verbatim. The new three-case block follows the same
  pattern and uses the `functionBlock` helper for the close-helper
  extraction, matching the L.60 cases.
- Em-dash count across the four edited code files and the design
  doc L.61 entry: 0. ASCII hyphens only.
- Allowed-file list discipline: only `port/src/menugraph.c`, the
  three `port/fast3d/pdgui_menu_*.cpp` files, `tests/test_menu_
  graph.cpp`, `tools/kanban/state.json`, and `context/designs/input/
  input-universality-and-transitions.md` modified. No
  `context/session-log.md`, `context/tasks.md`, `context/README.md`,
  or `context/bugs.md` touched -- orchestrator consolidates.

## Build / test status

- Not built. Per slice instructions: "Do NOT build verify in your
  local context (orchestrator does build verify post-merge)."
- `pd-tests [input][menu_graph]` expected to PASS with 3 additional
  TEST_CASEs once orchestrator builds the tests target.

## Follow-ups for Mike

1. Orchestrator build verify (three-target client / server / tests)
   confirms the slice links cleanly and the new TEST_CASE blocks
   pass against the migrated sources.
2. Next slice in the s036-08 lane should pick up the deferred
   pushes called out in L.61: either add a graph node for
   `MENU_TYPE_MP_TUNES` so the soundtrack -> tunes push migrates,
   or factor the variable-target `hubPushRow` in `mpadvanced.cpp`
   into a per-target edge id table.
3. The four `controldiagram.cpp` pops remain awkward because the
   two renderers there drive different pool types
   (`MENU_TYPE_SOLO_OPTIONS` and `MENU_TYPE_MP_PAUSE`) whose graph
   nodes carry semantically different "back" behavior than the
   control-diagram pop. Resolving that needs a small design call
   (introduce a `MENU_TYPE_CONTROL_DIAGRAM` graph node and pool
   registration, or live with the per-renderer source split).

## Refs

- c036 / s036-08.
- Reference commit: `593f63f0` Input - c036: s036-08 slice
  (4 priority-node graph migrations).
- Design doc entry: L.61 in
  `context/designs/input/input-universality-and-transitions.md`.
- Audit source: 2026-05-13 incompleteness audit, Sprint 5 menu
  graph completion lane.

## Where to look

- `port/src/menugraph.c` -- new edge tables and node registrations
  for MP_SETUP, MP_ADVANCED, MP_BOT_SETUP.
- `port/fast3d/pdgui_menu_mpsetup.cpp` -- `mp_CloseCurrentDialog`
  migrated.
- `port/fast3d/pdgui_menu_mpadvanced.cpp` -- `ma_CloseCurrentDialog`
  migrated.
- `port/fast3d/pdgui_menu_botsetup.cpp` -- `bs_CloseCurrentDialog`
  migrated.
- `tests/test_menu_graph.cpp` -- three new TEST_CASE blocks.
- `context/designs/input/input-universality-and-transitions.md` --
  L.61 entry.
- `tools/kanban/state.json` -- c036 `updated` field + s036-08 notes
  paragraph appended.
